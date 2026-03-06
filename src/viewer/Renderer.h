#pragma once

#include <memory>

namespace opendcad {

class ShaderProgram;
class Camera;
class GridMesh;
class RenderScene;

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
    void renderObjects(const Camera& camera, RenderScene& scene,
                       int viewportW, int viewportH);
    void renderEdges(const Camera& camera, RenderScene& scene,
                     int viewportW, int viewportH);

    std::unique_ptr<ShaderProgram> pbrShader_;
    std::unique_ptr<ShaderProgram> edgeShader_;

    // Studio lighting: 3 directional lights
    float lightDirs_[3][3];
    float lightColors_[3][3];

    bool edgesVisible_ = true;
    bool gridVisible_ = true;
    int highlightFace_ = -1;
};

} // namespace opendcad
