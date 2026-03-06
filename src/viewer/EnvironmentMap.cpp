#include "ShaderProgram.h"  // must come first - includes <glad/glad.h>
#include "EnvironmentMap.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace opendcad {

// ===========================================================================
// Unit cube vertices (36 verts = 6 faces * 2 triangles * 3 verts)
// ===========================================================================
static const float kCubeVertices[] = {
    // back face
    -1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
    // front face
    -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    // left face
    -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f, -1.0f,
    // right face
     1.0f, -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f,
    // bottom face
    -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f,
    // top face
    -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f,
};

// ===========================================================================
// Cubemap face view matrices (look along +X, -X, +Y, -Y, +Z, -Z)
// ===========================================================================
// Column-major 4x4 view matrices for each cubemap face
// Each looks from the origin along the face direction with appropriate up vector.

static void getCubeFaceViewMatrix(int face, float out[16]) {
    // right, up, forward for each face (OpenGL cubemap convention)
    // GL_TEXTURE_CUBE_MAP_POSITIVE_X ... GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    struct FaceInfo { float right[3]; float up[3]; float forward[3]; };
    static const FaceInfo faces[6] = {
        // +X: look right
        {{ 0, 0,-1}, { 0,-1, 0}, { 1, 0, 0}},
        // -X: look left
        {{ 0, 0, 1}, { 0,-1, 0}, {-1, 0, 0}},
        // +Y: look up
        {{ 1, 0, 0}, { 0, 0, 1}, { 0, 1, 0}},
        // -Y: look down
        {{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}},
        // +Z: look forward
        {{ 1, 0, 0}, { 0,-1, 0}, { 0, 0, 1}},
        // -Z: look backward
        {{-1, 0, 0}, { 0,-1, 0}, { 0, 0,-1}},
    };

    const auto& f = faces[face];
    // Column-major lookAt from origin: columns are right, up, -forward
    // view = [R  0]  where R = [right | up | -forward]^T
    //        [0  1]
    out[ 0] = f.right[0]; out[ 1] = f.up[0]; out[ 2] = -f.forward[0]; out[ 3] = 0;
    out[ 4] = f.right[1]; out[ 5] = f.up[1]; out[ 6] = -f.forward[1]; out[ 7] = 0;
    out[ 8] = f.right[2]; out[ 9] = f.up[2]; out[10] = -f.forward[2]; out[11] = 0;
    out[12] = 0;          out[13] = 0;        out[14] = 0;             out[15] = 1;
}

// 90-degree FOV perspective projection (column-major)
static void getPerspective90(float out[16]) {
    // fov = 90 degrees, aspect = 1, near = 0.1, far = 10
    float f = 1.0f / std::tan(45.0f * 3.14159265f / 180.0f); // = 1.0
    float n = 0.1f, fa = 10.0f;
    for (int i = 0; i < 16; i++) out[i] = 0;
    out[ 0] = f;           // [0][0]
    out[ 5] = f;           // [1][1]
    out[10] = (fa + n) / (n - fa);       // [2][2]
    out[11] = -1.0f;                      // [2][3] (column-major: row 3, col 2)
    out[14] = (2.0f * fa * n) / (n - fa); // [3][2]
    out[15] = 0;
}

// ===========================================================================
// Embedded GLSL shaders for IBL precomputation
// ===========================================================================

// Cubemap vertex shader (shared by equirect-to-cubemap, irradiance, prefiltered)
static const char* kCubeVertSrc = R"glsl(
#version 410 core
layout(location = 0) in vec3 aPos;
uniform mat4 uProjection;
uniform mat4 uView;
out vec3 vLocalPos;
void main() {
    vLocalPos = aPos;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
)glsl";

// Equirectangular to cubemap fragment shader
static const char* kEquirectFragSrc = R"glsl(
#version 410 core
in vec3 vLocalPos;
out vec4 FragColor;
uniform sampler2D uEquirectMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = sampleSphericalMap(normalize(vLocalPos));
    FragColor = vec4(texture(uEquirectMap, uv).rgb, 1.0);
}
)glsl";

// Irradiance convolution fragment shader
static const char* kIrradianceFragSrc = R"glsl(
#version 410 core
in vec3 vLocalPos;
out vec4 FragColor;
uniform samplerCube uEnvironmentMap;

const float PI = 3.14159265359;

void main() {
    vec3 normal = normalize(vLocalPos);
    vec3 irradiance = vec3(0.0);

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    float sampleDelta = 0.025;
    int nrSamples = 0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 tangentSample = vec3(sin(theta) * cos(phi),
                                       sin(theta) * sin(phi),
                                       cos(theta));
            vec3 sampleVec = tangentSample.x * right +
                             tangentSample.y * up +
                             tangentSample.z * normal;
            irradiance += texture(uEnvironmentMap, sampleVec).rgb *
                          cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance / float(nrSamples);
    FragColor = vec4(irradiance, 1.0);
}
)glsl";

// Prefiltered specular environment map fragment shader (GGX importance sampling)
static const char* kPrefilteredFragSrc = R"glsl(
#version 410 core
in vec3 vLocalPos;
out vec4 FragColor;
uniform samplerCube uEnvironmentMap;
uniform float uRoughness;

const float PI = 3.14159265359;

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to cartesian (tangent space)
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world space
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

void main() {
    vec3 N = normalize(vLocalPos);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;
    vec3 prefilteredColor = vec3(0.0);

    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += texture(uEnvironmentMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;
    FragColor = vec4(prefilteredColor, 1.0);
}
)glsl";

// BRDF integration LUT vertex shader (fullscreen quad)
static const char* kBRDFVertSrc = R"glsl(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

// BRDF integration LUT fragment shader
static const char* kBRDFFragSrc = R"glsl(
#version 410 core
in vec2 vTexCoord;
out vec2 FragColor;

const float PI = 3.14159265359;

float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) *
           geometrySchlickGGX(NdotL, roughness);
}

void main() {
    float NdotV = vTexCoord.x;
    float roughness = vTexCoord.y;

    vec3 V;
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G = geometrySmith(N, V, L, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    FragColor = vec2(A, B);
}
)glsl";

// ===========================================================================
// Construction / Destruction
// ===========================================================================

EnvironmentMap::~EnvironmentMap() {
    destroy();
}

void EnvironmentMap::destroy() {
    auto deleteTex = [](GLuint& tex) {
        if (tex != 0) { glDeleteTextures(1, &tex); tex = 0; }
    };
    deleteTex(envCubemap_);
    deleteTex(irradianceMap_);
    deleteTex(prefilteredMap_);
    deleteTex(brdfLUT_);

    if (captureFBO_ != 0) {
        glDeleteFramebuffers(1, &captureFBO_);
        captureFBO_ = 0;
    }
    if (captureRBO_ != 0) {
        glDeleteRenderbuffers(1, &captureRBO_);
        captureRBO_ = 0;
    }
    destroyCubeMesh();
}

// ===========================================================================
// Cube mesh for cubemap face rendering
// ===========================================================================

void EnvironmentMap::buildCubeMesh() {
    glGenVertexArrays(1, &cubeVAO_);
    glGenBuffers(1, &cubeVBO_);
    glBindVertexArray(cubeVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void EnvironmentMap::destroyCubeMesh() {
    if (cubeVAO_ != 0) {
        glDeleteVertexArrays(1, &cubeVAO_);
        glDeleteBuffers(1, &cubeVBO_);
        cubeVAO_ = 0;
        cubeVBO_ = 0;
    }
}

// ===========================================================================
// Helper: compile a temporary shader program
// ===========================================================================

GLuint EnvironmentMap::buildShader(const char* vertSrc, const char* fragSrc) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
            std::fprintf(stderr, "EnvironmentMap shader compile error:\n%s\n", buf);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        std::fprintf(stderr, "EnvironmentMap shader link error:\n%s\n", buf);
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ===========================================================================
// Helper: render unit cube into each cubemap face
// ===========================================================================

void EnvironmentMap::renderCubeFaces(GLuint shader, GLuint cubemapTex,
                                      int faceSize, int mipLevel) {
    float projection[16];
    getPerspective90(projection);

    glUseProgram(shader);
    GLint projLoc = glGetUniformLocation(shader, "uProjection");
    GLint viewLoc = glGetUniformLocation(shader, "uView");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, projection);

    glViewport(0, 0, faceSize, faceSize);
    glBindVertexArray(cubeVAO_);

    for (int face = 0; face < 6; face++) {
        float view[16];
        getCubeFaceViewMatrix(face, view);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, view);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                               cubemapTex, mipLevel);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindVertexArray(0);
}

// ===========================================================================
// init() — generate procedural environment + precompute IBL
// ===========================================================================

bool EnvironmentMap::init() {
    // Enable seamless cubemap filtering
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    // Build cube mesh for rendering into cubemap faces
    buildCubeMesh();

    // Create capture FBO + RBO
    glGenFramebuffers(1, &captureFBO_);
    glGenRenderbuffers(1, &captureRBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, captureRBO_);

    // Save current viewport to restore later
    GLint savedViewport[4];
    glGetIntegerv(GL_VIEWPORT, savedViewport);

    // 1. Generate procedural HDR environment (equirectangular)
    GLuint equirectTex = generateProceduralHDR(1024, 512);
    if (equirectTex == 0) return false;

    // 2. Convert equirectangular to cubemap (512x512 faces)
    envCubemap_ = equirectToCubemap(equirectTex, 512);
    glDeleteTextures(1, &equirectTex);
    if (envCubemap_ == 0) return false;

    // 3. Compute irradiance map (32x32 faces)
    irradianceMap_ = computeIrradiance(envCubemap_, 32);
    if (irradianceMap_ == 0) return false;

    // 4. Compute prefiltered specular map (128x128 faces, 5 mip levels)
    prefilteredMap_ = computePrefiltered(envCubemap_, 128);
    if (prefilteredMap_ == 0) return false;

    // 5. Compute BRDF LUT (512x512)
    brdfLUT_ = computeBRDFLUT(512);
    if (brdfLUT_ == 0) return false;

    // Restore viewport and unbind
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(savedViewport[0], savedViewport[1],
               savedViewport[2], savedViewport[3]);

    // Clean up temporary resources (cube mesh kept alive is fine since it's tiny,
    // but we can destroy it since we no longer need it)
    destroyCubeMesh();

    // Clean up capture FBO/RBO
    glDeleteFramebuffers(1, &captureFBO_);
    captureFBO_ = 0;
    glDeleteRenderbuffers(1, &captureRBO_);
    captureRBO_ = 0;

    std::fprintf(stderr, "EnvironmentMap: IBL precomputation complete\n");
    return true;
}

// ===========================================================================
// generateProceduralHDR() — studio-style equirectangular environment
// ===========================================================================

GLuint EnvironmentMap::generateProceduralHDR(int width, int height) {
    std::vector<float> data(width * height * 3);

    for (int y = 0; y < height; y++) {
        float v = static_cast<float>(y) / height;  // 0=top, 1=bottom
        float elevation = (0.5f - v) * 3.14159f;   // pi/2 to -pi/2
        float sinEl = std::sin(elevation);

        float skyR, skyG, skyB;
        if (sinEl > 0) {
            // Upper hemisphere: bright at top, dimming toward horizon
            float t = sinEl;
            skyR = 0.8f + 0.4f * t;   // warm
            skyG = 0.8f + 0.35f * t;
            skyB = 0.75f + 0.3f * t;
        } else {
            // Lower hemisphere: dark ground
            float t = -sinEl;
            skyR = 0.15f * (1.0f - t * 0.5f);
            skyG = 0.15f * (1.0f - t * 0.5f);
            skyB = 0.18f * (1.0f - t * 0.5f);
        }

        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            data[idx + 0] = skyR;
            data[idx + 1] = skyG;
            data[idx + 2] = skyB;
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0,
                 GL_RGB, GL_FLOAT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// ===========================================================================
// equirectToCubemap() — convert equirectangular HDR to cubemap
// ===========================================================================

GLuint EnvironmentMap::equirectToCubemap(GLuint equirectTex, int faceSize) {
    // Create cubemap texture
    GLuint cubemap;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);
    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     faceSize, faceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Build shader
    GLuint shader = buildShader(kCubeVertSrc, kEquirectFragSrc);
    if (!shader) return 0;

    // Bind equirect texture
    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, equirectTex);
    glUniform1i(glGetUniformLocation(shader, "uEquirectMap"), 0);

    // Resize RBO to match face size
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, faceSize, faceSize);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    renderCubeFaces(shader, cubemap, faceSize);

    glDeleteProgram(shader);
    return cubemap;
}

// ===========================================================================
// computeIrradiance() — diffuse irradiance cubemap
// ===========================================================================

GLuint EnvironmentMap::computeIrradiance(GLuint envCubemap, int size) {
    // Create irradiance cubemap
    GLuint irradiance;
    glGenTextures(1, &irradiance);
    glBindTexture(GL_TEXTURE_CUBE_MAP, irradiance);
    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Build shader
    GLuint shader = buildShader(kCubeVertSrc, kIrradianceFragSrc);
    if (!shader) return 0;

    // Bind environment cubemap
    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glUniform1i(glGetUniformLocation(shader, "uEnvironmentMap"), 0);

    // Resize RBO
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    renderCubeFaces(shader, irradiance, size);

    glDeleteProgram(shader);
    return irradiance;
}

// ===========================================================================
// computePrefiltered() — prefiltered specular cubemap with mip chain
// ===========================================================================

GLuint EnvironmentMap::computePrefiltered(GLuint envCubemap, int size) {
    GLuint prefiltered;
    glGenTextures(1, &prefiltered);
    glBindTexture(GL_TEXTURE_CUBE_MAP, prefiltered);
    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    // Build shader
    GLuint shader = buildShader(kCubeVertSrc, kPrefilteredFragSrc);
    if (!shader) return 0;

    glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);
    glUniform1i(glGetUniformLocation(shader, "uEnvironmentMap"), 0);

    GLint roughnessLoc = glGetUniformLocation(shader, "uRoughness");

    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);

    // 5 mip levels for roughness 0.0, 0.25, 0.5, 0.75, 1.0
    const int maxMipLevels = 5;
    for (int mip = 0; mip < maxMipLevels; mip++) {
        int mipSize = size >> mip;  // size / 2^mip
        if (mipSize < 1) mipSize = 1;

        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

        float roughness = static_cast<float>(mip) / static_cast<float>(maxMipLevels - 1);
        glUseProgram(shader);
        glUniform1f(roughnessLoc, roughness);

        renderCubeFaces(shader, prefiltered, mipSize, mip);
    }

    glDeleteProgram(shader);
    return prefiltered;
}

// ===========================================================================
// computeBRDFLUT() — 2D LUT of (F0 scale, F0 bias) for split-sum approx
// ===========================================================================

GLuint EnvironmentMap::computeBRDFLUT(int size) {
    GLuint lut;
    glGenTextures(1, &lut);
    glBindTexture(GL_TEXTURE_2D, lut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, size, size, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Build shader
    GLuint shader = buildShader(kBRDFVertSrc, kBRDFFragSrc);
    if (!shader) return 0;

    // We need a fullscreen quad for BRDF LUT rendering
    float quadVerts[] = {
        -1.0f, -1.0f,   0.0f, 0.0f,
         1.0f, -1.0f,   1.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 1.0f,
        -1.0f, -1.0f,   0.0f, 0.0f,
         1.0f,  1.0f,   1.0f, 1.0f,
        -1.0f,  1.0f,   0.0f, 1.0f,
    };

    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));

    // Render BRDF LUT
    glBindFramebuffer(GL_FRAMEBUFFER, captureFBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, captureRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, lut, 0);

    glViewport(0, 0, size, size);
    glUseProgram(shader);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Cleanup temporary quad and shader
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteProgram(shader);

    return lut;
}

} // namespace opendcad
