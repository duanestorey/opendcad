#include "ShaderProgram.h"  // must come first — includes <glad/glad.h>
#include "Renderer.h"
#include "Camera.h"
#include "GridMesh.h"
#include "RenderScene.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace opendcad {

// ===========================================================================
// Embedded GLSL shaders
// ===========================================================================

// ---------------------------------------------------------------------------
// PBR vertex shader (Cook-Torrance)
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
// PBR fragment shader (Cook-Torrance BRDF with 3-point studio lighting)
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
Renderer::~Renderer() = default;

// ===========================================================================
// init()
// ===========================================================================

bool Renderer::init() {
    // -----------------------------------------------------------------------
    // Compile PBR shader
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
// renderFrame()
// ===========================================================================

void Renderer::renderFrame(const Camera& camera, RenderScene& scene,
                           GridMesh& grid, ShaderProgram& gridShader,
                           int viewportW, int viewportH) {
    // Clear with dark background
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw grid first (uses its own shader)
    if (gridVisible_) {
        Mat4 vp = camera.viewProjectionMatrix(viewportW, viewportH);
        grid.draw(gridShader, vp.m);
    }

    // Enable depth testing for 3D objects
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Draw solid faces with PBR lighting
    renderObjects(camera, scene, viewportW, viewportH);

    // Draw edges on top (with depth bias in shader)
    if (edgesVisible_) {
        renderEdges(camera, scene, viewportW, viewportH);
    }
}

// ===========================================================================
// renderObjects() — PBR lit faces
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
