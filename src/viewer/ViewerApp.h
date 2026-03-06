#pragma once

#include <string>
#include <memory>

struct GLFWwindow;

namespace opendcad {

class Camera;
class GridMesh;
class ShaderProgram;
class Renderer;
class RenderScene;
class ObjectPanel;
class LayerPanel;
class PropertiesPanel;
class MaterialPanel;
class MenuBar;
class ViewportOverlay;

class ViewerApp {
public:
    ViewerApp();
    ~ViewerApp();

    bool init(int width = 1280, int height = 800);
    int run();
    void setTitle(const std::string& title);

    /// Load and evaluate a .dcad script, populate scene with exports.
    bool loadDcad(const std::string& path);

    /// Set input file to load on init.
    void setInputFile(const std::string& path) { inputFile_ = path; }

private:
    void render();
    static void glfwErrorCallback(int code, const char* desc);

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<GridMesh> grid_;
    std::unique_ptr<ShaderProgram> gridShader_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<RenderScene> scene_;
    std::string inputFile_;

    int fbWidth_ = 1280;
    int fbHeight_ = 800;

    // Mouse state
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool rotating_ = false;
    bool panning_ = false;

    // UI panels
    std::unique_ptr<ObjectPanel> objectPanel_;
    std::unique_ptr<LayerPanel> layerPanel_;
    std::unique_ptr<PropertiesPanel> propertiesPanel_;
    std::unique_ptr<MaterialPanel> materialPanel_;
    std::unique_ptr<MenuBar> menuBar_;
    std::unique_ptr<ViewportOverlay> viewportOverlay_;
    int selectedObject_ = -1;
    bool imguiInitialized_ = false;
};

} // namespace opendcad
