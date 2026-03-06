#include "ViewerApp.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "Camera.h"
#include "GridMesh.h"
#include "ShaderProgram.h"
#include "Renderer.h"
#include "RenderScene.h"

// UI panels
#include "ui/ImGuiSetup.h"
#include "ui/ObjectPanel.h"
#include "ui/LayerPanel.h"
#include "ui/PropertiesPanel.h"
#include "ui/MaterialPanel.h"
#include "ui/MenuBar.h"
#include "ui/ViewportOverlay.h"

// ANTLR + evaluator includes (for loadDcad)
#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"
#include "Evaluator.h"
#include "ShapeRegistry.h"
#include "Debug.h"

#include <cstdio>
#include <fstream>

namespace opendcad {

// ---------------------------------------------------------------------------
// Grid shader sources (GLSL 410 core)
// ---------------------------------------------------------------------------

static const char* kGridVertexSrc = R"(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

static const char* kGridFragmentSrc = R"(
#version 410 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)";

// ---------------------------------------------------------------------------
// GLFW error callback
// ---------------------------------------------------------------------------

void ViewerApp::glfwErrorCallback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ViewerApp::ViewerApp() = default;

ViewerApp::~ViewerApp() {
    if (imguiInitialized_) {
        ImGuiSetup::shutdown();
        imguiInitialized_ = false;
    }

    // Reset UI panels before GL context is destroyed
    objectPanel_.reset();
    layerPanel_.reset();
    propertiesPanel_.reset();
    materialPanel_.reset();
    menuBar_.reset();
    viewportOverlay_.reset();

    scene_.reset();
    renderer_.reset();
    grid_.reset();
    gridShader_.reset();
    camera_.reset();

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

bool ViewerApp::init(int width, int height) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }

    // Request OpenGL 4.1 core profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA

    window_ = glfwCreateWindow(width, height, "OpenDCAD Viewer", nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);

    // Load OpenGL functions via glad2
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to load OpenGL via glad\n");
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }

    std::printf("OpenGL %s, GLSL %s\n",
                reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    // Store user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

    // -----------------------------------------------------------------------
    // GLFW callbacks
    // -----------------------------------------------------------------------

    // Mouse button
    glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int mods) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(w, &self->lastMouseX_, &self->lastMouseY_);
            if (button == GLFW_MOUSE_BUTTON_LEFT && (mods & GLFW_MOD_SHIFT)) {
                self->panning_ = true;
            } else if (button == GLFW_MOUSE_BUTTON_LEFT) {
                self->rotating_ = true;
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
                self->panning_ = true;
            }
        } else if (action == GLFW_RELEASE) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                self->rotating_ = false;
                self->panning_ = false; // also release shift+LMB pan
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
                self->panning_ = false;
            }
        }
    });

    // Cursor position
    glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double xpos, double ypos) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        double dx = xpos - self->lastMouseX_;
        double dy = ypos - self->lastMouseY_;
        self->lastMouseX_ = xpos;
        self->lastMouseY_ = ypos;

        if (self->rotating_) {
            self->camera_->rotate(
                static_cast<float>(-dx) * 0.005f,
                static_cast<float>(-dy) * 0.005f
            );
        }
        if (self->panning_) {
            self->camera_->pan(
                static_cast<float>(-dx),
                static_cast<float>(-dy),
                self->fbWidth_, self->fbHeight_
            );
        }
    });

    // Scroll
    glfwSetScrollCallback(window_, [](GLFWwindow* w, double /*xoffset*/, double yoffset) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        double cx, cy;
        glfwGetCursorPos(w, &cx, &cy);
        self->camera_->zoomToCursor(
            static_cast<float>(yoffset), cx, cy,
            self->fbWidth_, self->fbHeight_
        );
    });

    // Key
    glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
        if (ImGui::GetIO().WantCaptureKeyboard) return;
        if (action != GLFW_PRESS) return;
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        switch (key) {
            case GLFW_KEY_F: {
                // Fit to scene bounds if we have a scene, otherwise default
                float smin[3] = {-50.0f, -50.0f, -50.0f};
                float smax[3] = { 50.0f,  50.0f,  50.0f};
                if (self->scene_ && !self->scene_->objects().empty()) {
                    self->scene_->sceneBounds(smin, smax);
                }
                self->camera_->fitToBounds(
                    smin[0], smin[1], smin[2],
                    smax[0], smax[1], smax[2],
                    self->fbWidth_, self->fbHeight_
                );
                break;
            }
            case GLFW_KEY_SPACE:
                self->camera_->reset(self->fbWidth_, self->fbHeight_);
                break;
            case GLFW_KEY_I:
                self->camera_->setIsometric(self->fbWidth_, self->fbHeight_);
                break;
            case GLFW_KEY_E:
                if (self->renderer_) {
                    self->renderer_->setEdgesVisible(!self->renderer_->edgesVisible());
                }
                break;
            case GLFW_KEY_G:
                if (self->renderer_) {
                    self->renderer_->setGridVisible(!self->renderer_->gridVisible());
                }
                break;
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(w, GLFW_TRUE);
                break;
            default:
                break;
        }
    });

    // Framebuffer size
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        self->fbWidth_ = width;
        self->fbHeight_ = height;
    });

    // -----------------------------------------------------------------------
    // Build grid shader
    // -----------------------------------------------------------------------
    gridShader_ = std::make_unique<ShaderProgram>();
    if (!gridShader_->build(kGridVertexSrc, kGridFragmentSrc)) {
        std::fprintf(stderr, "Grid shader error: %s\n", gridShader_->error().c_str());
        return false;
    }

    // -----------------------------------------------------------------------
    // Build grid mesh
    // -----------------------------------------------------------------------
    grid_ = std::make_unique<GridMesh>();
    grid_->build();

    // -----------------------------------------------------------------------
    // Create camera and set isometric view
    // -----------------------------------------------------------------------
    camera_ = std::make_unique<Camera>();
    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    camera_->setIsometric(fbWidth_, fbHeight_);

    // -----------------------------------------------------------------------
    // Initialize PBR renderer
    // -----------------------------------------------------------------------
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init()) {
        std::fprintf(stderr, "Failed to initialize renderer\n");
        return false;
    }
    scene_ = std::make_unique<RenderScene>();

    // -----------------------------------------------------------------------
    // Initialize ImGui
    // -----------------------------------------------------------------------
    ImGuiSetup::init(window_);
    imguiInitialized_ = true;

    // Create UI panels
    objectPanel_ = std::make_unique<ObjectPanel>();
    layerPanel_ = std::make_unique<LayerPanel>();
    propertiesPanel_ = std::make_unique<PropertiesPanel>();
    materialPanel_ = std::make_unique<MaterialPanel>();
    menuBar_ = std::make_unique<MenuBar>();
    viewportOverlay_ = std::make_unique<ViewportOverlay>();

    // -----------------------------------------------------------------------
    // Load input file if provided
    // -----------------------------------------------------------------------
    if (!inputFile_.empty()) {
        loadDcad(inputFile_);

        // Fit camera to scene bounds
        float smin[3], smax[3];
        scene_->sceneBounds(smin, smax);
        camera_->setBounds(smin[0], smin[1], smin[2], smax[0], smax[1], smax[2]);
        camera_->setIsometric(fbWidth_, fbHeight_);
    }

    // -----------------------------------------------------------------------
    // GL state
    // -----------------------------------------------------------------------
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    return true;
}

// ---------------------------------------------------------------------------
// loadDcad() — parse and evaluate a .dcad script, populate the scene
// ---------------------------------------------------------------------------

bool ViewerApp::loadDcad(const std::string& path) {
    // Suppress debug output in the viewer
    debugQuiet() = true;

    // Read file
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "Cannot open: %s\n", path.c_str());
        return false;
    }
    std::string src((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

    // Parse
    antlr4::ANTLRInputStream input(src);
    OpenDCAD::OpenDCADLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    OpenDCAD::OpenDCADParser parser(&tokens);
    auto* tree = parser.program();

    if (parser.getNumberOfSyntaxErrors() > 0) {
        std::fprintf(stderr, "Syntax errors in %s\n", path.c_str());
        return false;
    }

    // Register shape factories and evaluate
    ShapeRegistry::instance().registerDefaults();
    Evaluator evaluator;
    try {
        evaluator.evaluate(tree, path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error evaluating %s: %s\n", path.c_str(), e.what());
        return false;
    }

    // Load exports into scene
    scene_->clear();
    for (const auto& entry : evaluator.exports()) {
        for (const auto& shape : entry.shapes) {
            float color[3] = {0.72f, 0.78f, 0.86f};  // default steel-blue
            float metallic = 0.0f, roughness = 0.5f;

            if (shape->color()) {
                color[0] = static_cast<float>(shape->color()->r);
                color[1] = static_cast<float>(shape->color()->g);
                color[2] = static_cast<float>(shape->color()->b);
            }
            if (shape->material()) {
                metallic = static_cast<float>(shape->material()->metallic);
                roughness = static_cast<float>(shape->material()->roughness);
                if (shape->material()->baseColor) {
                    color[0] = static_cast<float>(shape->material()->baseColor->r);
                    color[1] = static_cast<float>(shape->material()->baseColor->g);
                    color[2] = static_cast<float>(shape->material()->baseColor->b);
                }
            }

            scene_->addShape(entry.name, shape->getShape(),
                           color, metallic, roughness, shape->tags());
        }
    }

    std::printf("Loaded %zu export(s) from %s\n",
                evaluator.exports().size(), path.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// run() -- main loop
// ---------------------------------------------------------------------------

int ViewerApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
        glViewport(0, 0, fbWidth_, fbHeight_);

        render();

        // --- ImGui frame ---
        ImGuiSetup::beginFrame();

        // Menu bar (inside the dockspace window which has MenuBar flag)
        auto actions = menuBar_->draw(*renderer_);
        if (actions.close) glfwSetWindowShouldClose(window_, GLFW_TRUE);
        if (actions.resetCamera) camera_->reset(fbWidth_, fbHeight_);
        if (actions.fitAll) {
            float smin[3] = {-50.0f, -50.0f, -50.0f};
            float smax[3] = { 50.0f,  50.0f,  50.0f};
            if (scene_ && !scene_->objects().empty()) {
                scene_->sceneBounds(smin, smax);
            }
            camera_->fitToBounds(smin[0], smin[1], smin[2],
                                 smax[0], smax[1], smax[2],
                                 fbWidth_, fbHeight_);
        }
        if (actions.isometric) camera_->setIsometric(fbWidth_, fbHeight_);

        // Panels
        objectPanel_->draw(*scene_, selectedObject_);
        layerPanel_->draw(*scene_);

        const RenderObject* sel = nullptr;
        if (selectedObject_ >= 0 && selectedObject_ < static_cast<int>(scene_->objects().size())) {
            sel = &scene_->objects()[selectedObject_];
        }
        propertiesPanel_->draw(sel);
        materialPanel_->draw(sel);
        viewportOverlay_->draw(*scene_, inputFile_, "Ready");

        ImGuiSetup::endFrame();

        glfwSwapBuffers(window_);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// render()
// ---------------------------------------------------------------------------

void ViewerApp::render() {
    if (renderer_ && scene_) {
        renderer_->renderFrame(*camera_, *scene_, *grid_, *gridShader_,
                              fbWidth_, fbHeight_);
    } else {
        // Fallback: just draw grid
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        Mat4 mvp = camera_->viewProjectionMatrix(fbWidth_, fbHeight_);
        grid_->draw(*gridShader_, mvp.m);
    }
}

// ---------------------------------------------------------------------------
// setTitle()
// ---------------------------------------------------------------------------

void ViewerApp::setTitle(const std::string& title) {
    if (window_) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

} // namespace opendcad
