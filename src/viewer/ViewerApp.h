#pragma once

#include <string>
#include <memory>

struct GLFWwindow;

namespace opendcad {

class Camera;
class GridMesh;
class ShaderProgram;

class ViewerApp {
public:
    ViewerApp();
    ~ViewerApp();

    bool init(int width = 1280, int height = 800);
    int run();
    void setTitle(const std::string& title);

private:
    void render();
    static void glfwErrorCallback(int code, const char* desc);

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<GridMesh> grid_;
    std::unique_ptr<ShaderProgram> gridShader_;

    int fbWidth_ = 1280;
    int fbHeight_ = 800;

    // Mouse state
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool rotating_ = false;
    bool panning_ = false;
};

} // namespace opendcad
