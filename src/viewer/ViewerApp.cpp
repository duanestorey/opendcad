#include "ViewerApp.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "Camera.h"
#include "GridMesh.h"
#include "ShaderProgram.h"

#include <cstdio>

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
        if (action != GLFW_PRESS) return;
        auto* self = static_cast<ViewerApp*>(glfwGetWindowUserPointer(w));
        switch (key) {
            case GLFW_KEY_F:
                self->camera_->fitToBounds(
                    -50.0f, -50.0f, -50.0f,
                     50.0f,  50.0f,  50.0f,
                    self->fbWidth_, self->fbHeight_
                );
                break;
            case GLFW_KEY_SPACE:
                self->camera_->reset(self->fbWidth_, self->fbHeight_);
                break;
            case GLFW_KEY_I:
                self->camera_->setIsometric(self->fbWidth_, self->fbHeight_);
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
    // GL state
    // -----------------------------------------------------------------------
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    return true;
}

// ---------------------------------------------------------------------------
// run() — main loop
// ---------------------------------------------------------------------------

int ViewerApp::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
        glViewport(0, 0, fbWidth_, fbHeight_);

        // Dark background
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        render();

        glfwSwapBuffers(window_);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// render()
// ---------------------------------------------------------------------------

void ViewerApp::render() {
    Mat4 mvp = camera_->viewProjectionMatrix(fbWidth_, fbHeight_);
    grid_->draw(*gridShader_, mvp.m);
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
