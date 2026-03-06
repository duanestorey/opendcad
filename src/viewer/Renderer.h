#pragma once

#include <glad/glad.h>
#include <memory>

namespace opendcad {

class ShaderProgram;
class Camera;
class GridMesh;
class RenderScene;
class EnvironmentMap;

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init();  // compile shaders, set up lights

    // Render a full frame
    void renderFrame(const Camera& camera, RenderScene& scene,
                     GridMesh& grid, ShaderProgram& gridShader,
                     int viewportW, int viewportH);

    // Toggle options
    void setEdgesVisible(bool visible) { edgesVisible_ = visible; }
    void setGridVisible(bool visible) { gridVisible_ = visible; }
    bool edgesVisible() const { return edgesVisible_; }
    bool gridVisible() const { return gridVisible_; }

    // Face highlighting for picking
    void setHighlightFace(int faceID) { highlightFace_ = faceID; }

private:
    // Forward PBR (legacy, replaced by deferred pipeline)
    void renderObjects(const Camera& camera, RenderScene& scene,
                       int viewportW, int viewportH);
    void renderEdges(const Camera& camera, RenderScene& scene,
                     int viewportW, int viewportH);

    // --- Deferred rendering passes ---
    void gBufferPass(const Camera& camera, RenderScene& scene,
                     int viewportW, int viewportH);
    void ssaoPass(const Camera& camera, int viewportW, int viewportH);
    void lightingPass(const Camera& camera, int viewportW, int viewportH);

    // --- G-Buffer resources ---
    GLuint gBufferFBO_ = 0;
    GLuint gAlbedoTex_ = 0;    // RT0: RGBA16F (albedo RGB + alpha)
    GLuint gNormalTex_ = 0;    // RT1: RGBA16F (normal XYZ + metallic)
    GLuint gPositionTex_ = 0;  // RT2: RGBA16F (position XYZ + roughness)
    GLuint gFaceIDTex_ = 0;    // RT3: R32F (face ID for picking)
    GLuint gDepthRBO_ = 0;     // DEPTH24_STENCIL8
    int gBufferWidth_ = 0, gBufferHeight_ = 0;

    void createGBuffer(int width, int height);
    void destroyGBuffer();

    // --- SSAO resources ---
    GLuint ssaoFBO_ = 0, ssaoBlurFBO_ = 0;
    GLuint ssaoTex_ = 0, ssaoBlurTex_ = 0;
    GLuint ssaoNoiseTex_ = 0;

    void createSSAO(int width, int height);
    void destroySSAO();

    // --- Fullscreen quad (shared by SSAO + lighting passes) ---
    GLuint quadVAO_ = 0, quadVBO_ = 0;
    void buildFullscreenQuad();
    void drawFullscreenQuad();

    // --- Shaders ---
    std::unique_ptr<ShaderProgram> pbrShader_;       // forward PBR (kept for reference)
    std::unique_ptr<ShaderProgram> edgeShader_;
    std::unique_ptr<ShaderProgram> gBufferShader_;
    std::unique_ptr<ShaderProgram> ssaoShader_;
    std::unique_ptr<ShaderProgram> ssaoBlurShader_;
    std::unique_ptr<ShaderProgram> lightingShader_;

    // Studio lighting: 3 directional lights
    float lightDirs_[3][3];
    float lightColors_[3][3];

    // --- IBL environment map ---
    std::unique_ptr<EnvironmentMap> envMap_;

    bool edgesVisible_ = true;
    bool gridVisible_ = true;
    int highlightFace_ = -1;
};

} // namespace opendcad
