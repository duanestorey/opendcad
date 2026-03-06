#include "ShaderProgram.h"  // must come first — includes <glad/glad.h>
#include "Renderer.h"
#include "Camera.h"
#include "GridMesh.h"
#include "RenderScene.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace opendcad {

// ===========================================================================
// Embedded GLSL shaders
// ===========================================================================

// ---------------------------------------------------------------------------
// G-Buffer vertex shader (writes to multiple render targets)
// ---------------------------------------------------------------------------
static const char* kGBufferVertSrc = R"glsl(
#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aFaceID;
layout(location = 3) in vec3 aColor;
layout(location = 4) in vec2 aMatParams;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vAlbedo;
out float vMetallic;
out float vRoughness;
flat out float vFaceID;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vAlbedo = aColor;
    vMetallic = aMatParams.x;
    vRoughness = aMatParams.y;
    vFaceID = aFaceID;
}
)glsl";

// ---------------------------------------------------------------------------
// G-Buffer fragment shader (writes to 4 render targets)
// ---------------------------------------------------------------------------
static const char* kGBufferFragSrc = R"glsl(
#version 410 core

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vAlbedo;
in float vMetallic;
in float vRoughness;
flat in float vFaceID;

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gPosition;
layout(location = 3) out float gFaceID;

void main() {
    gAlbedo = vec4(vAlbedo, 1.0);
    gNormal = vec4(normalize(vWorldNormal), vMetallic);
    gPosition = vec4(vWorldPos, vRoughness);
    gFaceID = vFaceID;
}
)glsl";

// ---------------------------------------------------------------------------
// SSAO vertex shader (fullscreen quad)
// ---------------------------------------------------------------------------
static const char* kSSAOVertSrc = R"glsl(
#version 410 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

// ---------------------------------------------------------------------------
// SSAO fragment shader (64-sample hemisphere kernel)
// ---------------------------------------------------------------------------
static const char* kSSAOFragSrc = R"glsl(
#version 410 core

in vec2 vTexCoord;
out float FragColor;

uniform sampler2D uPositionTex;
uniform sampler2D uNormalTex;
uniform sampler2D uNoiseTex;
uniform vec3 uSamples[64];
uniform mat4 uProjection;
uniform vec2 uNoiseScale;
uniform float uRadius;
uniform float uBias;

void main() {
    vec3 fragPos = texture(uPositionTex, vTexCoord).xyz;
    vec3 normal = normalize(texture(uNormalTex, vTexCoord).xyz);
    vec3 randomVec = normalize(texture(uNoiseTex, vTexCoord * uNoiseScale).xyz);

    // If no geometry at this pixel, no occlusion
    if (length(fragPos) < 0.001) { FragColor = 1.0; return; }

    // Create TBN matrix from normal + random rotation
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < 64; i++) {
        vec3 samplePos = fragPos + TBN * uSamples[i] * uRadius;

        // Project sample to screen space
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Sample depth at that screen position
        float sampleDepth = texture(uPositionTex, offset.xy).z;

        // Range check + occlusion
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    FragColor = 1.0 - (occlusion / 64.0);
}
)glsl";

// ---------------------------------------------------------------------------
// SSAO blur fragment shader (5x5 box blur)
// ---------------------------------------------------------------------------
static const char* kSSAOBlurFragSrc = R"glsl(
#version 410 core

in vec2 vTexCoord;
out float FragColor;

uniform sampler2D uSSAOInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(uSSAOInput, 0));
    float result = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            result += texture(uSSAOInput, vTexCoord + vec2(float(x), float(y)) * texelSize).r;
        }
    }
    FragColor = result / 25.0;
}
)glsl";

// ---------------------------------------------------------------------------
// Deferred lighting fragment shader (Cook-Torrance PBR with SSAO)
// ---------------------------------------------------------------------------
static const char* kLightingFragSrc = R"glsl(
#version 410 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uAlbedoTex;
uniform sampler2D uNormalTex;
uniform sampler2D uPositionTex;
uniform sampler2D uSSAOTex;

uniform vec3 uCameraPos;
uniform vec3 uLightDir[3];
uniform vec3 uLightColor[3];
uniform int uHighlightFace;
uniform vec3 uHighlightColor;
uniform sampler2D uFaceIDTex;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 albedoData = texture(uAlbedoTex, vTexCoord);
    vec3 albedo = albedoData.rgb;
    if (albedoData.a < 0.01) discard;  // no geometry here

    vec4 normalData = texture(uNormalTex, vTexCoord);
    vec3 N = normalize(normalData.xyz);
    float metallic = normalData.w;

    vec4 posData = texture(uPositionTex, vTexCoord);
    vec3 worldPos = posData.xyz;
    float roughness = posData.w;

    float ao = texture(uSSAOTex, vTexCoord).r;

    vec3 V = normalize(uCameraPos - worldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        vec3 L = normalize(uLightDir[i]);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = D * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kD * albedo / PI + specular) * uLightColor[i] * NdotL;
    }

    vec3 ambient = 0.15 * albedo * ao;  // AO modulates ambient
    vec3 color = ambient + Lo;

    // Face highlight
    float faceID = texture(uFaceIDTex, vTexCoord).r;
    if (uHighlightFace >= 0 && abs(faceID - float(uHighlightFace)) < 0.5) {
        color = mix(color, uHighlightColor, 0.4);
    }

    // Tone mapping (Reinhard) + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
)glsl";

// ---------------------------------------------------------------------------
// PBR vertex shader (forward, kept for reference / fallback)
// ---------------------------------------------------------------------------
static const char* kPbrVertSrc = R"glsl(
#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aFaceID;
layout(location = 3) in vec3 aColor;
layout(location = 4) in vec2 aMatParams;  // metallic, roughness

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vAlbedo;
out float vMetallic;
out float vRoughness;
flat out float vFaceID;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vAlbedo = aColor;
    vMetallic = aMatParams.x;
    vRoughness = aMatParams.y;
    vFaceID = aFaceID;
}
)glsl";

// ---------------------------------------------------------------------------
// PBR fragment shader (forward, kept for reference / fallback)
// ---------------------------------------------------------------------------
static const char* kPbrFragSrc = R"glsl(
#version 410 core

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vAlbedo;
in float vMetallic;
in float vRoughness;
flat in float vFaceID;

uniform vec3 uCameraPos;
uniform vec3 uLightDir[3];
uniform vec3 uLightColor[3];
uniform int uHighlightFace;
uniform vec3 uHighlightColor;

out vec4 FragColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);

    vec3 F0 = mix(vec3(0.04), vAlbedo, vMetallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 3; i++) {
        vec3 L = normalize(uLightDir[i]);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        float D = distributionGGX(N, H, vRoughness);
        float G = geometrySmith(N, V, L, vRoughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = D * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - vMetallic);
        Lo += (kD * vAlbedo / PI + specular) * uLightColor[i] * NdotL;
    }

    vec3 ambient = 0.15 * vAlbedo;
    vec3 color = ambient + Lo;

    // Face highlight
    if (uHighlightFace >= 0 && abs(vFaceID - float(uHighlightFace)) < 0.5) {
        color = mix(color, uHighlightColor, 0.4);
    }

    // Tone mapping (Reinhard) + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
)glsl";

// ---------------------------------------------------------------------------
// Edge vertex shader (simple passthrough with depth bias)
// ---------------------------------------------------------------------------
static const char* kEdgeVertSrc = R"glsl(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aEdgeID;
uniform mat4 uMVP;
void main() {
    vec4 p = uMVP * vec4(aPos, 1.0);
    p.z -= 0.0005 * p.w;  // depth bias to prevent z-fighting
    gl_Position = p;
}
)glsl";

// ---------------------------------------------------------------------------
// Edge fragment shader
// ---------------------------------------------------------------------------
static const char* kEdgeFragSrc = R"glsl(
#version 410 core
uniform vec3 uEdgeColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uEdgeColor, 1.0);
}
)glsl";

// ===========================================================================
// Helper: normalize a 3-component vector in place
// ===========================================================================
static void normalizeDir(float dir[3]) {
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len > 1e-8f) {
        dir[0] /= len;
        dir[1] /= len;
        dir[2] /= len;
    }
}

// ===========================================================================
// Construction / Destruction
// ===========================================================================

Renderer::Renderer() = default;

Renderer::~Renderer() {
    destroyGBuffer();
    destroySSAO();
    if (quadVAO_ != 0) {
        glDeleteVertexArrays(1, &quadVAO_);
        glDeleteBuffers(1, &quadVBO_);
    }
}

// ===========================================================================
// init()
// ===========================================================================

bool Renderer::init() {
    // -----------------------------------------------------------------------
    // Compile PBR shader (forward — kept for fallback)
    // -----------------------------------------------------------------------
    pbrShader_ = std::make_unique<ShaderProgram>();
    if (!pbrShader_->build(kPbrVertSrc, kPbrFragSrc)) {
        std::fprintf(stderr, "Renderer: PBR shader error: %s\n",
                     pbrShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile edge shader
    // -----------------------------------------------------------------------
    edgeShader_ = std::make_unique<ShaderProgram>();
    if (!edgeShader_->build(kEdgeVertSrc, kEdgeFragSrc)) {
        std::fprintf(stderr, "Renderer: edge shader error: %s\n",
                     edgeShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile G-Buffer shader
    // -----------------------------------------------------------------------
    gBufferShader_ = std::make_unique<ShaderProgram>();
    if (!gBufferShader_->build(kGBufferVertSrc, kGBufferFragSrc)) {
        std::fprintf(stderr, "Renderer: G-Buffer shader error: %s\n",
                     gBufferShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile SSAO shader
    // -----------------------------------------------------------------------
    ssaoShader_ = std::make_unique<ShaderProgram>();
    if (!ssaoShader_->build(kSSAOVertSrc, kSSAOFragSrc)) {
        std::fprintf(stderr, "Renderer: SSAO shader error: %s\n",
                     ssaoShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile SSAO blur shader
    // -----------------------------------------------------------------------
    ssaoBlurShader_ = std::make_unique<ShaderProgram>();
    if (!ssaoBlurShader_->build(kSSAOVertSrc, kSSAOBlurFragSrc)) {
        std::fprintf(stderr, "Renderer: SSAO blur shader error: %s\n",
                     ssaoBlurShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Compile deferred lighting shader
    // -----------------------------------------------------------------------
    lightingShader_ = std::make_unique<ShaderProgram>();
    if (!lightingShader_->build(kSSAOVertSrc, kLightingFragSrc)) {
        std::fprintf(stderr, "Renderer: lighting shader error: %s\n",
                     lightingShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Build fullscreen quad (shared by post-processing passes)
    // -----------------------------------------------------------------------
    buildFullscreenQuad();

    // -----------------------------------------------------------------------
    // Studio lighting: 3 directional lights
    // -----------------------------------------------------------------------

    // Key light — warm, dominant
    lightDirs_[0][0] = 1.0f;  lightDirs_[0][1] = 0.8f;  lightDirs_[0][2] = 1.2f;
    normalizeDir(lightDirs_[0]);
    lightColors_[0][0] = 1.2f; lightColors_[0][1] = 1.15f; lightColors_[0][2] = 1.1f;

    // Fill light — cool fill
    lightDirs_[1][0] = -0.8f; lightDirs_[1][1] = 0.4f;  lightDirs_[1][2] = 0.6f;
    normalizeDir(lightDirs_[1]);
    lightColors_[1][0] = 0.3f; lightColors_[1][1] = 0.35f; lightColors_[1][2] = 0.4f;

    // Rim light — subtle back light
    lightDirs_[2][0] = 0.0f;  lightDirs_[2][1] = -1.0f; lightDirs_[2][2] = 0.3f;
    normalizeDir(lightDirs_[2]);
    lightColors_[2][0] = 0.15f; lightColors_[2][1] = 0.15f; lightColors_[2][2] = 0.2f;

    return true;
}

// ===========================================================================
// Fullscreen quad (shared by SSAO, SSAO blur, and lighting passes)
// ===========================================================================

void Renderer::buildFullscreenQuad() {
    float quadVertices[] = {
        // pos      texcoord
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f, -1.0f,    1.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 1.0f,
    };

    glGenVertexArrays(1, &quadVAO_);
    glGenBuffers(1, &quadVBO_);

    glBindVertexArray(quadVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // location 0 = vec2 pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // location 1 = vec2 texcoord
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::drawFullscreenQuad() {
    glBindVertexArray(quadVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ===========================================================================
// G-Buffer creation / destruction
// ===========================================================================

void Renderer::createGBuffer(int width, int height) {
    if (gBufferFBO_ != 0) destroyGBuffer();
    gBufferWidth_ = width;
    gBufferHeight_ = height;

    glGenFramebuffers(1, &gBufferFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO_);

    auto createTex = [](GLuint& tex, int w, int h, GLenum internalFmt, GLenum attachment) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0,
                     (internalFmt == GL_R32F) ? GL_RED : GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex, 0);
    };

    createTex(gAlbedoTex_, width, height, GL_RGBA16F, GL_COLOR_ATTACHMENT0);
    createTex(gNormalTex_, width, height, GL_RGBA16F, GL_COLOR_ATTACHMENT1);
    createTex(gPositionTex_, width, height, GL_RGBA16F, GL_COLOR_ATTACHMENT2);
    createTex(gFaceIDTex_, width, height, GL_R32F, GL_COLOR_ATTACHMENT3);

    GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                              GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
    glDrawBuffers(4, drawBuffers);

    // Depth renderbuffer
    glGenRenderbuffers(1, &gDepthRBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, gDepthRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, gDepthRBO_);

    // Check completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "G-Buffer FBO incomplete!\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyGBuffer() {
    if (gBufferFBO_ != 0) {
        glDeleteFramebuffers(1, &gBufferFBO_);
        gBufferFBO_ = 0;
    }
    auto deleteTex = [](GLuint& tex) {
        if (tex != 0) { glDeleteTextures(1, &tex); tex = 0; }
    };
    deleteTex(gAlbedoTex_);
    deleteTex(gNormalTex_);
    deleteTex(gPositionTex_);
    deleteTex(gFaceIDTex_);
    if (gDepthRBO_ != 0) {
        glDeleteRenderbuffers(1, &gDepthRBO_);
        gDepthRBO_ = 0;
    }
    gBufferWidth_ = 0;
    gBufferHeight_ = 0;
}

// ===========================================================================
// SSAO creation / destruction
// ===========================================================================

void Renderer::createSSAO(int width, int height) {
    if (ssaoFBO_ != 0) destroySSAO();

    // -----------------------------------------------------------------------
    // Generate 64-sample hemisphere kernel (cosine-weighted, biased toward surface)
    // -----------------------------------------------------------------------
    std::mt19937 rng(42);  // fixed seed for reproducibility
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distNeg(-1.0f, 1.0f);

    std::vector<float> ssaoKernel(64 * 3);
    for (int i = 0; i < 64; i++) {
        // Random direction in hemisphere (z >= 0)
        float x = distNeg(rng);
        float y = distNeg(rng);
        float z = dist01(rng);  // hemisphere — positive z only
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-6f) { x = 0; y = 0; z = 1; len = 1; }
        x /= len; y /= len; z /= len;

        // Scale with accelerating interpolation — more samples closer to origin
        float scale = static_cast<float>(i) / 64.0f;
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale*scale)
        x *= scale; y *= scale; z *= scale;

        ssaoKernel[i * 3 + 0] = x;
        ssaoKernel[i * 3 + 1] = y;
        ssaoKernel[i * 3 + 2] = z;
    }

    // Upload kernel to SSAO shader
    ssaoShader_->use();
    for (int i = 0; i < 64; i++) {
        std::string name = "uSamples[" + std::to_string(i) + "]";
        ssaoShader_->setVec3(name.c_str(),
                             ssaoKernel[i * 3 + 0],
                             ssaoKernel[i * 3 + 1],
                             ssaoKernel[i * 3 + 2]);
    }

    // -----------------------------------------------------------------------
    // Generate 4x4 random rotation noise texture (rotations around Z)
    // -----------------------------------------------------------------------
    std::vector<float> noiseData(16 * 3);
    for (int i = 0; i < 16; i++) {
        noiseData[i * 3 + 0] = distNeg(rng);  // x rotation
        noiseData[i * 3 + 1] = distNeg(rng);  // y rotation
        noiseData[i * 3 + 2] = 0.0f;          // z = 0 (rotate around z)
    }

    glGenTextures(1, &ssaoNoiseTex_);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noiseData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // -----------------------------------------------------------------------
    // Create SSAO FBO with single R16F attachment
    // -----------------------------------------------------------------------
    glGenFramebuffers(1, &ssaoFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_);

    glGenTextures(1, &ssaoTex_);
    glBindTexture(GL_TEXTURE_2D, ssaoTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoTex_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "SSAO FBO incomplete!\n");
    }

    // -----------------------------------------------------------------------
    // Create blur FBO with single R16F attachment
    // -----------------------------------------------------------------------
    glGenFramebuffers(1, &ssaoBlurFBO_);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO_);

    glGenTextures(1, &ssaoBlurTex_);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoBlurTex_, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "SSAO blur FBO incomplete!\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroySSAO() {
    auto deleteTex = [](GLuint& tex) {
        if (tex != 0) { glDeleteTextures(1, &tex); tex = 0; }
    };
    auto deleteFBO = [](GLuint& fbo) {
        if (fbo != 0) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    };

    deleteFBO(ssaoFBO_);
    deleteFBO(ssaoBlurFBO_);
    deleteTex(ssaoTex_);
    deleteTex(ssaoBlurTex_);
    deleteTex(ssaoNoiseTex_);
}

// ===========================================================================
// G-Buffer pass: render all geometry to MRT
// ===========================================================================

void Renderer::gBufferPass(const Camera& camera, RenderScene& scene,
                            int viewportW, int viewportH) {
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO_);
    glViewport(0, 0, viewportW, viewportH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    gBufferShader_->use();

    // Model matrix = identity (shapes are already in world space)
    Mat4 model = mat4Identity();
    Mat4 mvp = camera.viewProjectionMatrix(viewportW, viewportH);

    // Normal matrix (3x3 identity since model = identity)
    float normalMat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    gBufferShader_->setMat4("uModel", model.m);
    gBufferShader_->setMat4("uMVP", mvp.m);
    gBufferShader_->setMat3("uNormalMatrix", normalMat);

    // Draw each visible object's face mesh
    for (auto& obj : scene.objects()) {
        if (!obj.visible || obj.faceVAO == 0 || obj.faceVertexCount == 0) {
            continue;
        }
        glBindVertexArray(obj.faceVAO);
        glDrawArrays(GL_TRIANGLES, 0, obj.faceVertexCount);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ===========================================================================
// SSAO pass: compute ambient occlusion from G-Buffer depth + normals
// ===========================================================================

void Renderer::ssaoPass(const Camera& camera, int viewportW, int viewportH) {
    // --- SSAO pass ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO_);
    glViewport(0, 0, viewportW, viewportH);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    ssaoShader_->use();

    // Bind G-Buffer textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gPositionTex_);
    ssaoShader_->setInt("uPositionTex", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormalTex_);
    ssaoShader_->setInt("uNormalTex", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ssaoNoiseTex_);
    ssaoShader_->setInt("uNoiseTex", 2);

    // Projection matrix for reprojection
    Mat4 proj = camera.projectionMatrix(viewportW, viewportH);
    ssaoShader_->setMat4("uProjection", proj.m);

    // Noise scale: viewport / noise texture size (4x4)
    ssaoShader_->setVec2("uNoiseScale",
                          static_cast<float>(viewportW) / 4.0f,
                          static_cast<float>(viewportH) / 4.0f);

    ssaoShader_->setFloat("uRadius", 0.5f);
    ssaoShader_->setFloat("uBias", 0.025f);

    drawFullscreenQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // --- SSAO blur pass ---
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFBO_);
    glViewport(0, 0, viewportW, viewportH);
    glClear(GL_COLOR_BUFFER_BIT);

    ssaoBlurShader_->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ssaoTex_);
    ssaoBlurShader_->setInt("uSSAOInput", 0);

    drawFullscreenQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ===========================================================================
// Deferred lighting pass: fullscreen quad reads G-Buffer + SSAO
// ===========================================================================

void Renderer::lightingPass(const Camera& camera, int /*viewportW*/, int /*viewportH*/) {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    lightingShader_->use();

    // Bind G-Buffer textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gAlbedoTex_);
    lightingShader_->setInt("uAlbedoTex", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gNormalTex_);
    lightingShader_->setInt("uNormalTex", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gPositionTex_);
    lightingShader_->setInt("uPositionTex", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, ssaoBlurTex_);
    lightingShader_->setInt("uSSAOTex", 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, gFaceIDTex_);
    lightingShader_->setInt("uFaceIDTex", 4);

    // Camera position
    Vec3f eye = camera.eyePosition();
    lightingShader_->setVec3("uCameraPos", eye.x, eye.y, eye.z);

    // Lights
    for (int i = 0; i < 3; i++) {
        std::string dirName  = "uLightDir[" + std::to_string(i) + "]";
        std::string colName  = "uLightColor[" + std::to_string(i) + "]";
        lightingShader_->setVec3(dirName.c_str(),  lightDirs_[i]);
        lightingShader_->setVec3(colName.c_str(),  lightColors_[i]);
    }

    // Face highlight
    lightingShader_->setInt("uHighlightFace", highlightFace_);
    lightingShader_->setVec3("uHighlightColor", 0.3f, 0.7f, 1.0f);

    drawFullscreenQuad();

    glDisable(GL_BLEND);
}

// ===========================================================================
// renderFrame() — deferred rendering pipeline
// ===========================================================================

void Renderer::renderFrame(const Camera& camera, RenderScene& scene,
                           GridMesh& grid, ShaderProgram& gridShader,
                           int viewportW, int viewportH) {
    // Ensure G-Buffer and SSAO FBOs are correct size
    if (gBufferWidth_ != viewportW || gBufferHeight_ != viewportH) {
        createGBuffer(viewportW, viewportH);
        createSSAO(viewportW, viewportH);
    }

    // 1. G-Buffer pass: render geometry to MRT
    gBufferPass(camera, scene, viewportW, viewportH);

    // 2. SSAO pass: compute ambient occlusion from depth + normals
    ssaoPass(camera, viewportW, viewportH);

    // 3. Back to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, viewportW, viewportH);

    // 4. Grid (forward rendered, behind everything)
    if (gridVisible_) {
        Mat4 vp = camera.viewProjectionMatrix(viewportW, viewportH);
        grid.draw(gridShader, vp.m);
    }

    // 5. Deferred lighting composite (fullscreen quad reads G-Buffer + SSAO)
    lightingPass(camera, viewportW, viewportH);

    // 6. Copy G-Buffer depth to default framebuffer for edge rendering
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, viewportW, viewportH, 0, 0, viewportW, viewportH,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 7. Edge overlay (forward rendered with depth from G-Buffer)
    if (edgesVisible_) {
        glEnable(GL_DEPTH_TEST);
        renderEdges(camera, scene, viewportW, viewportH);
    }
}

// ===========================================================================
// renderObjects() — forward PBR lit faces (kept for reference / fallback)
// ===========================================================================

void Renderer::renderObjects(const Camera& camera, RenderScene& scene,
                             int viewportW, int viewportH) {
    pbrShader_->use();

    // Model matrix = identity (shapes are already in world space)
    Mat4 model = mat4Identity();
    Mat4 mvp = camera.viewProjectionMatrix(viewportW, viewportH);

    // Normal matrix (3x3 identity since model = identity)
    float normalMat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    pbrShader_->setMat4("uModel", model.m);
    pbrShader_->setMat4("uMVP", mvp.m);
    pbrShader_->setMat3("uNormalMatrix", normalMat);

    // Camera position
    Vec3f eye = camera.eyePosition();
    pbrShader_->setVec3("uCameraPos", eye.x, eye.y, eye.z);

    // Lights — set each array element individually
    for (int i = 0; i < 3; i++) {
        std::string dirName  = "uLightDir[" + std::to_string(i) + "]";
        std::string colName  = "uLightColor[" + std::to_string(i) + "]";
        pbrShader_->setVec3(dirName.c_str(),  lightDirs_[i]);
        pbrShader_->setVec3(colName.c_str(),  lightColors_[i]);
    }

    // Face highlight
    pbrShader_->setInt("uHighlightFace", highlightFace_);
    pbrShader_->setVec3("uHighlightColor", 0.3f, 0.7f, 1.0f);  // highlight blue

    // Draw each visible object's face mesh
    for (auto& obj : scene.objects()) {
        if (!obj.visible || obj.faceVAO == 0 || obj.faceVertexCount == 0) {
            continue;
        }
        glBindVertexArray(obj.faceVAO);
        glDrawArrays(GL_TRIANGLES, 0, obj.faceVertexCount);
    }

    glBindVertexArray(0);
}

// ===========================================================================
// renderEdges() — dark edge lines
// ===========================================================================

void Renderer::renderEdges(const Camera& camera, RenderScene& scene,
                           int viewportW, int viewportH) {
    edgeShader_->use();

    Mat4 mvp = camera.viewProjectionMatrix(viewportW, viewportH);
    edgeShader_->setMat4("uMVP", mvp.m);
    edgeShader_->setVec3("uEdgeColor", 0.2f, 0.2f, 0.2f);  // dark grey

    for (auto& obj : scene.objects()) {
        if (!obj.visible || obj.edgeVAO == 0 || obj.edgeVertexCount == 0) {
            continue;
        }
        glBindVertexArray(obj.edgeVAO);
        glDrawArrays(GL_LINES, 0, obj.edgeVertexCount);
    }

    glBindVertexArray(0);
}

} // namespace opendcad
