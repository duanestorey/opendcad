# Viewer Overhaul Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the 780-line GL 2.1 prototype viewer with a production-quality PBR renderer using OpenGL 4.1, ImGui docking, BRep tessellation, hot-reload, and screenshot export.

**Architecture:** Multi-pass deferred renderer (G-Buffer → SSAO → PBR lighting → edge overlay → ImGui). The viewer evaluates .dcad scripts directly or loads STEP+JSON sidecars. BRep shapes are tessellated in-process, preserving face IDs, edge topology, and per-shape color/material metadata. ImGui docking provides object tree, layer toggles, material inspector, and properties panels.

**Tech Stack:** C++20, OpenGL 4.1 core (glad loader), GLFW3, Dear ImGui (docking branch), OCCT (BRepMesh tessellation + XCAF STEP reader), stb_image.h (HDR), stb_image_write.h (PNG screenshots)

**Build/test commands:**
- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build --config Release --target opendcad opendcad_viewer opendcad_tests`
- Tests: `./build/bin/opendcad_tests`
- Run viewer: `./build/bin/opendcad_viewer examples/battery.dcad`

---

## Phase A: Foundation — OpenGL 4.1 Window + Glad

Gets a blank window with GL 4.1 core profile running. Everything else builds on this.

### Task 1: Vendor glad (OpenGL 4.1 core loader)

**Files:**
- Create: `vendor/glad/include/glad/glad.h`
- Create: `vendor/glad/include/KHR/khrplatform.h`
- Create: `vendor/glad/src/glad.c`

**Step 1: Generate glad files**

Go to https://gen.glad.sh/ and generate with settings:
- API: gl 4.1
- Profile: Core
- Generator: C/C++
- Options: loader

Or use the glad CLI:
```bash
pip install glad2
python -m glad --api gl:core=4.1 --out-path vendor/glad c
```

This produces 3 files: `include/glad/glad.h`, `include/KHR/khrplatform.h`, `src/glad.c`.

**Step 2: Verify files exist**

```bash
ls vendor/glad/include/glad/glad.h vendor/glad/include/KHR/khrplatform.h vendor/glad/src/glad.c
```

**Step 3: Commit**

```bash
git add vendor/glad/
git commit -m "vendor: add glad OpenGL 4.1 core loader"
```

---

### Task 2: Create ShaderProgram utility

**Files:**
- Create: `src/viewer/ShaderProgram.h`
- Create: `src/viewer/ShaderProgram.cpp`

**Step 1: Write ShaderProgram.h**

```cpp
#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>

namespace opendcad {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    // Non-copyable, movable
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // Compile and link from source strings. Returns false on error (check error()).
    bool build(const char* vertexSrc, const char* fragmentSrc);

    void use() const;
    GLuint id() const { return program_; }

    // Uniform setters (cache locations)
    void setInt(const char* name, int value);
    void setFloat(const char* name, float value);
    void setVec2(const char* name, float x, float y);
    void setVec3(const char* name, float x, float y, float z);
    void setVec3(const char* name, const float* v);
    void setMat3(const char* name, const float* m);
    void setMat4(const char* name, const float* m);

    const std::string& error() const { return error_; }

private:
    GLint getLocation(const char* name);
    GLuint compileShader(GLenum type, const char* src);

    GLuint program_ = 0;
    std::unordered_map<std::string, GLint> locationCache_;
    std::string error_;
};

} // namespace opendcad
```

**Step 2: Write ShaderProgram.cpp**

```cpp
#include "ShaderProgram.h"
#include <vector>

namespace opendcad {

ShaderProgram::~ShaderProgram() {
    if (program_) glDeleteProgram(program_);
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_), locationCache_(std::move(other.locationCache_)),
      error_(std::move(other.error_)) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (program_) glDeleteProgram(program_);
        program_ = other.program_;
        locationCache_ = std::move(other.locationCache_);
        error_ = std::move(other.error_);
        other.program_ = 0;
    }
    return *this;
}

GLuint ShaderProgram::compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        error_ = std::string(log.data(), len);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool ShaderProgram::build(const char* vertexSrc, const char* fragmentSrc) {
    error_.clear();
    locationCache_.clear();

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return false;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) { glDeleteShader(vs); return false; }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len);
        glGetProgramInfoLog(program_, len, nullptr, log.data());
        error_ = std::string(log.data(), len);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    return true;
}

void ShaderProgram::use() const {
    glUseProgram(program_);
}

GLint ShaderProgram::getLocation(const char* name) {
    auto it = locationCache_.find(name);
    if (it != locationCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, name);
    locationCache_[name] = loc;
    return loc;
}

void ShaderProgram::setInt(const char* name, int value) {
    glUniform1i(getLocation(name), value);
}

void ShaderProgram::setFloat(const char* name, float value) {
    glUniform1f(getLocation(name), value);
}

void ShaderProgram::setVec2(const char* name, float x, float y) {
    glUniform2f(getLocation(name), x, y);
}

void ShaderProgram::setVec3(const char* name, float x, float y, float z) {
    glUniform3f(getLocation(name), x, y, z);
}

void ShaderProgram::setVec3(const char* name, const float* v) {
    glUniform3fv(getLocation(name), 1, v);
}

void ShaderProgram::setMat3(const char* name, const float* m) {
    glUniformMatrix3fv(getLocation(name), 1, GL_FALSE, m);
}

void ShaderProgram::setMat4(const char* name, const float* m) {
    glUniformMatrix4fv(getLocation(name), 1, GL_FALSE, m);
}

} // namespace opendcad
```

**Step 3: Commit**

```bash
git add src/viewer/ShaderProgram.h src/viewer/ShaderProgram.cpp
git commit -m "feat(viewer): add ShaderProgram utility class"
```

---

### Task 3: Create Camera module

Port the existing orbital camera from `src/viewer/viewer.cpp` lines 137-172, 452-606 into a standalone class.

**Files:**
- Create: `src/viewer/Camera.h`
- Create: `src/viewer/Camera.cpp`

**Step 1: Write Camera.h**

```cpp
#pragma once

#include <cmath>
#include <algorithm>

namespace opendcad {

struct Mat4 { float m[16]; };
struct Vec3f { float x, y, z; };

// 4x4 matrix operations (column-major, Z-up)
Mat4 mat4Identity();
Mat4 mat4Mul(const Mat4& a, const Mat4& b);
Mat4 mat4Perspective(float fovyDeg, float aspect, float znear, float zfar);
Mat4 mat4LookAt(Vec3f eye, Vec3f target, Vec3f up);

// Vector math
Vec3f vec3Sub(const Vec3f& a, const Vec3f& b);
Vec3f vec3Cross(const Vec3f& a, const Vec3f& b);
float vec3Dot(const Vec3f& a, const Vec3f& b);
Vec3f vec3Normalize(Vec3f v);

class Camera {
public:
    Camera();

    // Orbital controls
    void rotate(float deltaYaw, float deltaPitch);
    void pan(float deltaRight, float deltaUp, int viewportW, int viewportH);
    void zoomToCursor(float scrollY, double cursorX, double cursorY,
                      int viewportW, int viewportH);

    // Fit camera to bounding box
    void fitToBounds(float minX, float minY, float minZ,
                     float maxX, float maxY, float maxZ,
                     int viewportW, int viewportH);

    // Reset to defaults
    void reset(int viewportW, int viewportH);
    void setIsometric(int viewportW, int viewportH);

    // Get matrices for rendering
    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(int viewportW, int viewportH) const;
    Mat4 viewProjectionMatrix(int viewportW, int viewportH) const;

    // Get camera world position (for specular lighting)
    Vec3f eyePosition() const;
    Vec3f targetPosition() const;

    // Forward/right/up basis vectors
    Vec3f forward() const;
    Vec3f right() const;
    Vec3f up() const;

    // Bounding box (set once, used for fit/reset/zoom)
    void setBounds(float minX, float minY, float minZ,
                   float maxX, float maxY, float maxZ);

    float fovyDeg() const { return fovyDeg_; }
    float distance() const { return dist_; }

private:
    float yaw_ = 0.0f;    // azimuth around Z (radians)
    float pitch_ = 0.0f;  // elevation from XY (radians)
    float dist_ = 200.0f; // distance from target
    float panR_ = 0.0f;   // camera-space pan right (world units)
    float panU_ = 0.0f;   // camera-space pan up (world units)
    float fovyDeg_ = 45.0f;

    // Bounding box center
    float bboxMin_[3] = {0, 0, 0};
    float bboxMax_[3] = {1, 1, 1};

    float fitDistance(int viewportW, int viewportH) const;
};

} // namespace opendcad
```

**Step 2: Write Camera.cpp**

Port the math from `src/viewer/viewer.cpp` (lines 178-213 for Mat4/Vec3f, lines 146-171 for bounds/fit, lines 452-606 for orbit/pan/zoom). The implementation is a direct extraction of the existing code into the class structure.

Key points:
- `mat4Identity()`, `mat4Mul()`, `mat4Perspective()`, `mat4LookAt()` are direct copies from the current viewer
- `zoomToCursor()` ports the scroll callback logic (lines 498-586)
- `fitToBounds()` ports `fit_distance_from_bbox()` (lines 156-171)
- Camera basis vectors (forward, right, up) computed from yaw/pitch same as current viewer (lines 626-635)

**Step 3: Commit**

```bash
git add src/viewer/Camera.h src/viewer/Camera.cpp
git commit -m "feat(viewer): extract Camera module from prototype"
```

---

### Task 4: Create GridMesh (VAO/VBO, no immediate mode)

**Files:**
- Create: `src/viewer/GridMesh.h`
- Create: `src/viewer/GridMesh.cpp`

**Step 1: Write GridMesh.h**

```cpp
#pragma once

#include <glad/glad.h>

namespace opendcad {

class ShaderProgram;

class GridMesh {
public:
    GridMesh() = default;
    ~GridMesh();

    // Build grid lines + axis lines into a single VBO.
    // Vertex format: [x, y, z, r, g, b, a] = 28 bytes.
    void build(float gridSize = 500.0f, float gridStep = 10.0f,
               float majorStep = 50.0f, float axisLength = 30.0f);

    void draw(ShaderProgram& shader, const float* mvp);
    void destroy();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei gridVertexCount_ = 0;
    GLsizei axisVertexCount_ = 0;
    GLsizei totalVertexCount_ = 0;
};

} // namespace opendcad
```

**Step 2: Write GridMesh.cpp**

Build all grid lines as `GL_LINES` pairs with [pos(3), color(4)] = 7 floats per vertex = 28 bytes stride.

- Minor grid lines: alpha 0.10
- Major grid lines (every `majorStep`): alpha 0.22
- X axis: red, Y axis: green, Z axis: blue (full alpha)
- Grid drawn with depth test disabled, axes on top

Vertex data is pre-computed in a `std::vector<float>`, uploaded to a VBO, and drawn from a VAO.

**Step 3: Commit**

```bash
git add src/viewer/GridMesh.h src/viewer/GridMesh.cpp
git commit -m "feat(viewer): add GridMesh with VAO/VBO (no immediate mode)"
```

---

### Task 5: Create ViewerApp skeleton + CMake build

**Files:**
- Create: `src/viewer/ViewerApp.h`
- Create: `src/viewer/ViewerApp.cpp`
- Create: `src/viewer/viewer_main.cpp` (entry point, replaces old viewer.cpp)
- Modify: `CMakeLists.txt` (lines 195-230, viewer section)

**Step 1: Write ViewerApp.h**

```cpp
#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

namespace opendcad {

class Camera;
class GridMesh;
class ShaderProgram;

class ViewerApp {
public:
    ViewerApp();
    ~ViewerApp();

    // Initialize GLFW + glad + window. Returns false on failure.
    bool init(int width = 1280, int height = 800);

    // Main loop. Returns exit code.
    int run();

    // Set the title
    void setTitle(const std::string& title);

private:
    void render();
    void handleInput();
    static void glfwErrorCallback(int code, const char* desc);

    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Camera> camera_;
    std::unique_ptr<GridMesh> grid_;
    std::unique_ptr<ShaderProgram> gridShader_;

    int fbWidth_ = 1280;
    int fbHeight_ = 800;
};

} // namespace opendcad
```

**Step 2: Write ViewerApp.cpp**

Initialize GLFW with GL 4.1 core profile hints:
```cpp
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
glfwWindowHint(GLFW_SAMPLES, 4);
```

After `glfwMakeContextCurrent()`, call `gladLoadGLLoader()`. Build the grid shader (simple passthrough with per-vertex color), build the grid mesh, set up the camera, and enter the render loop.

Grid shader (GLSL 410 core):
```glsl
// Vertex
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); vColor = aColor; }

// Fragment
#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
```

Port mouse/key callbacks from the old viewer (lines 465-606) to call `camera_->rotate()`, `camera_->pan()`, `camera_->zoomToCursor()`, etc. via GLFW user pointer.

**Step 3: Write viewer_main.cpp**

```cpp
#include "ViewerApp.h"
#include <cstdio>

int main(int argc, char** argv) {
    opendcad::ViewerApp app;
    if (!app.init()) return 1;
    return app.run();
}
```

**Step 4: Update CMakeLists.txt**

Replace the viewer section (lines 195-230) with:

```cmake
# ──────────────────────────────────────────────────────────────────────
# Vendor: glad (OpenGL 4.1 core loader)
# ──────────────────────────────────────────────────────────────────────
add_library(glad STATIC vendor/glad/src/glad.c)
target_include_directories(glad PUBLIC vendor/glad/include)
if(NOT MSVC)
  target_compile_options(glad PRIVATE -w)  # suppress warnings in vendored code
endif()

# ──────────────────────────────────────────────────────────────────────
# Viewer (GLFW + OpenGL 4.1) — optional
# ──────────────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
  if (NOT GLFW_FOUND)
    pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw)
  endif()
endif()

if(GLFW_FOUND)
  add_executable(opendcad_viewer
    src/viewer/viewer_main.cpp
    src/viewer/ViewerApp.cpp
    src/viewer/Camera.cpp
    src/viewer/ShaderProgram.cpp
    src/viewer/GridMesh.cpp
  )

  target_include_directories(opendcad_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/viewer
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/geometry
  )

  target_link_libraries(opendcad_viewer PRIVATE glad PkgConfig::GLFW)

  if (APPLE)
    target_link_libraries(opendcad_viewer PRIVATE
      "-framework OpenGL"
      "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    )
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(opendcad_viewer PRIVATE OpenGL::GL)
  endif()

  target_compile_features(opendcad_viewer PRIVATE cxx_std_20)
  target_compile_options(opendcad_viewer PRIVATE -Wall -Wextra -Wpedantic -O2)
  link_occt(opendcad_viewer)
else()
  message(STATUS "GLFW not found — skipping opendcad_viewer")
endif()
```

**Step 5: Build and verify**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target opendcad_viewer
./build/bin/opendcad_viewer
```

Expected: window opens with dark background, grid, and colored axes. Camera rotates/pans/zooms.

**Step 6: Run existing tests to confirm nothing broke**

```bash
./build/bin/opendcad_tests
```

Expected: 270 tests pass (viewer is a separate target, doesn't affect tests).

**Step 7: Commit**

```bash
git add src/viewer/ViewerApp.h src/viewer/ViewerApp.cpp src/viewer/viewer_main.cpp CMakeLists.txt
git commit -m "feat(viewer): GL 4.1 core profile window with grid and camera"
```

---

## Phase B: BRep Tessellation

Converts OCCT TopoDS_Shape into GPU-ready vertex data with face IDs, normals, per-face colors, and edge polylines.

### Task 6: Write Tessellator

**Files:**
- Create: `src/viewer/Tessellator.h`
- Create: `src/viewer/Tessellator.cpp`

**Step 1: Write Tessellator.h**

```cpp
#pragma once

#include <vector>
#include <cstdint>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct TessVertex {
    float pos[3];
    float normal[3];
    float faceID;       // integer as float for shader attribute
    float color[3];     // per-face color (default: object color)
    float metallic;
    float roughness;
};

struct TessEdgeVertex {
    float pos[3];
    float edgeID;       // integer as float
};

struct TessFaceInfo {
    int faceID;
    int triangleOffset;    // first vertex index / 3
    int triangleCount;
    int surfaceType;       // GeomAbs_SurfaceType enum
    double area;
};

struct TessEdgeInfo {
    int edgeID;
    int vertexOffset;
    int vertexCount;       // number of line segment vertices
    double length;
};

struct TessResult {
    std::vector<TessVertex> vertices;          // 3 per triangle, flat
    std::vector<TessFaceInfo> faces;
    std::vector<TessEdgeVertex> edgeVertices;  // 2 per line segment
    std::vector<TessEdgeInfo> edges;

    float bboxMin[3];
    float bboxMax[3];

    int totalTriangles() const { return static_cast<int>(vertices.size()) / 3; }
};

class Tessellator {
public:
    static TessResult tessellate(const TopoDS_Shape& shape,
                                 double deflection = 0.01,
                                 double angle = 0.5);

    // Overload with per-face color defaults
    static TessResult tessellate(const TopoDS_Shape& shape,
                                 float defaultR, float defaultG, float defaultB,
                                 float metallic, float roughness,
                                 double deflection = 0.01,
                                 double angle = 0.5);

private:
    static void extractFaces(const TopoDS_Shape& shape, TessResult& result,
                             float r, float g, float b, float met, float rough);
    static void extractEdges(const TopoDS_Shape& shape, TessResult& result);
    static void computeBounds(TessResult& result);
};

} // namespace opendcad
```

**Step 2: Write Tessellator.cpp**

Key implementation details:
- Call `BRepMesh_IncrementalMesh` to mesh the shape
- Iterate faces via `TopExp_Explorer(shape, TopAbs_FACE)`, extract `Poly_Triangulation` from each face
- Check `face.Orientation() == TopAbs_REVERSED` and swap winding
- Apply `TopLoc_Location` transform to all node positions
- Use per-vertex normals if `tri->HasNormals()` (OCCT 7.6+), otherwise compute flat normals from cross product
- Use `TopTools_IndexedMapOfShape` for unique edges via `TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap)`
- Try `BRep_Tool::Polygon3D` first, fall back to `BRep_Tool::PolygonOnTriangulation`

OCCT headers needed:
```cpp
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_Orientation.hxx>
```

CMake: Add `TKMesh` to the viewer's OCCT link list (for `BRepMesh_IncrementalMesh`).

**Step 3: Commit**

```bash
git add src/viewer/Tessellator.h src/viewer/Tessellator.cpp
git commit -m "feat(viewer): BRep tessellator with face IDs and edge polylines"
```

---

### Task 7: Write Tessellator tests

**Files:**
- Create: `tests/test_tessellator.cpp`
- Modify: `CMakeLists.txt` (add test file to opendcad_tests)

**Step 1: Write tests**

```cpp
#include <gtest/gtest.h>
#include "Tessellator.h"
#include "Shape.h"

using namespace opendcad;

TEST(Tessellator, BoxProducesTriangles) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_GT(result.totalTriangles(), 0);
    EXPECT_EQ(result.vertices.size() % 3, 0);  // multiple of 3
}

TEST(Tessellator, BoxHasSixFaces) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_EQ(result.faces.size(), 6);
}

TEST(Tessellator, BoxHasEdges) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_GT(result.edges.size(), 0);
    EXPECT_GT(result.edgeVertices.size(), 0);
}

TEST(Tessellator, BoundsAreCorrect) {
    auto box = Shape::createBox(10, 20, 30);
    auto result = Tessellator::tessellate(box->getShape());
    // Box is centered at origin in OCCT: [-5,5] x [-10,10] x [0,30]
    EXPECT_NEAR(result.bboxMin[0], -5.0f, 0.1f);
    EXPECT_NEAR(result.bboxMax[0],  5.0f, 0.1f);
    EXPECT_NEAR(result.bboxMin[2],  0.0f, 0.1f);
    EXPECT_NEAR(result.bboxMax[2], 30.0f, 0.1f);
}

TEST(Tessellator, CylinderHasCurvedFaces) {
    auto cyl = Shape::createCylinder(5, 20);
    auto result = Tessellator::tessellate(cyl->getShape());
    EXPECT_GT(result.totalTriangles(), 12);  // more than a box
    EXPECT_GE(result.faces.size(), 3);       // top, bottom, side
}

TEST(Tessellator, FaceIDsAreUnique) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    std::set<int> ids;
    for (const auto& f : result.faces) ids.insert(f.faceID);
    EXPECT_EQ(ids.size(), result.faces.size());
}

TEST(Tessellator, DefaultColorsApplied) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape(),
                                           0.8f, 0.2f, 0.1f, 0.0f, 0.5f);
    EXPECT_GT(result.vertices.size(), 0);
    EXPECT_NEAR(result.vertices[0].color[0], 0.8f, 0.01f);
    EXPECT_NEAR(result.vertices[0].metallic, 0.0f, 0.01f);
    EXPECT_NEAR(result.vertices[0].roughness, 0.5f, 0.01f);
}
```

**Step 2: Add to CMakeLists.txt test target**

Add `tests/test_tessellator.cpp` to the `opendcad_tests` source list and add the viewer include dir + TKMesh to test dependencies.

**Step 3: Build and run tests**

```bash
cmake --build build --config Release --target opendcad_tests
./build/bin/opendcad_tests --gtest_filter="Tessellator*"
```

Expected: All Tessellator tests pass.

**Step 4: Commit**

```bash
git add tests/test_tessellator.cpp CMakeLists.txt
git commit -m "test: add Tessellator unit tests"
```

---

## Phase C: RenderScene + Forward PBR

Gets real geometry rendering with per-shape colors and materials.

### Task 8: Create RenderObject and RenderScene

**Files:**
- Create: `src/viewer/RenderObject.h`
- Create: `src/viewer/RenderScene.h`
- Create: `src/viewer/RenderScene.cpp`

**RenderObject.h** — per-export GPU resources:
```cpp
struct RenderObject {
    std::string name;           // from "export ... as <name>"
    std::vector<std::string> tags;

    // Face mesh
    GLuint faceVAO = 0, faceVBO = 0;
    GLsizei faceVertexCount = 0;

    // Edge mesh
    GLuint edgeVAO = 0, edgeVBO = 0;
    GLsizei edgeVertexCount = 0;

    // Bounding box
    float bboxMin[3], bboxMax[3];

    // State
    bool visible = true;
    bool selected = false;

    // Face/edge metadata for properties panel
    int faceCount = 0, edgeCount = 0;
    double volume = 0.0, surfaceArea = 0.0;

    void destroy();  // delete GPU resources
};
```

**RenderScene** — manages all render objects:
- `addShape(name, TopoDS_Shape, Color, Material, tags, deflection)` — tessellate + upload to GPU
- `clear()` — destroy all objects
- `objects()` — vector of RenderObject
- `uniqueTags()` — set of all tags (for layer panel)
- `setTagVisibility(tag, bool)` — toggle layer

**Step 1-3: Write files, commit**

```bash
git add src/viewer/RenderObject.h src/viewer/RenderScene.h src/viewer/RenderScene.cpp
git commit -m "feat(viewer): add RenderObject and RenderScene"
```

---

### Task 9: Forward PBR shader

**Files:**
- Create: `src/viewer/shaders/pbr_vert.glsl` (embedded as string constant in Renderer.cpp)
- Create: `src/viewer/Renderer.h`
- Create: `src/viewer/Renderer.cpp`

The initial PBR renderer uses forward rendering (single pass, no G-Buffer yet). This gets geometry on screen with proper materials quickly. The G-Buffer + SSAO pass is added in Phase E.

**PBR vertex shader (GLSL 410 core):**
```glsl
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
```

**PBR fragment shader (Cook-Torrance BRDF):**
```glsl
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

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith's method with Schlick-GGX
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

// Fresnel-Schlick
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

    // Tone mapping (ACES filmic) + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
```

**Renderer** class manages:
- PBR shader compilation
- Edge shader compilation
- Grid shader (already done)
- Setting up 3 directional lights (key at 45deg, fill from left, rim from behind)
- Drawing all visible RenderObjects
- Drawing edges on top with depth bias

**Step 1-3: Write files, commit**

```bash
git add src/viewer/Renderer.h src/viewer/Renderer.cpp
git commit -m "feat(viewer): forward PBR renderer with Cook-Torrance BRDF"
```

---

### Task 10: Wire up ViewerApp to load shapes and render

**Files:**
- Modify: `src/viewer/ViewerApp.h` — add RenderScene, Renderer members
- Modify: `src/viewer/ViewerApp.cpp` — load .dcad or .step, tessellate, render
- Modify: `src/viewer/viewer_main.cpp` — pass CLI args to ViewerApp
- Modify: `CMakeLists.txt` — link viewer to ANTLR + shared lib sources

**Implementation:**
- `ViewerApp::loadDcad(path)` — evaluate script using existing Evaluator, iterate `exports()`, add each shape to RenderScene
- `ViewerApp::loadStep(path)` — use `STEPCAFControl_Reader` to load shapes with XDE colors, read companion `.json` for materials/tags
- Main render loop: clear → grid → PBR objects → edges → swap

The viewer_main.cpp parses the first argument as an input file. If it ends in `.dcad`, evaluate it. If it ends in `.step`, load it.

**Step 1-3: Update files, build, test manually**

```bash
cmake --build build --config Release --target opendcad_viewer
./build/bin/opendcad_viewer examples/battery.dcad
```

Expected: window shows the battery model with PBR shading, grid, axes, orbital camera.

**Step 4: Commit**

```bash
git add src/viewer/ CMakeLists.txt
git commit -m "feat(viewer): load .dcad scripts and render with PBR"
```

---

## Phase D: G-Buffer + SSAO

Upgrades from forward to deferred rendering for SSAO support.

### Task 11: G-Buffer framebuffer

**Files:**
- Modify: `src/viewer/Renderer.h` — add G-Buffer FBO, textures
- Modify: `src/viewer/Renderer.cpp` — G-Buffer pass, create/resize FBO

**Implementation:**
- Create FBO with 4 color attachments + depth:
  - RT0: RGBA16F (albedo + alpha)
  - RT1: RGBA16F (normal + metallic)
  - RT2: RGBA16F (position + roughness)
  - RT3: R32F (face ID)
  - Depth: DEPTH24_STENCIL8
- G-Buffer shader writes to all 4 outputs using `layout(location = N) out vec4`
- Resize FBO when window resizes

**Step 1-2: Implement, commit**

```bash
git commit -m "feat(viewer): add G-Buffer framebuffer for deferred rendering"
```

---

### Task 12: SSAO pass

**Files:**
- Modify: `src/viewer/Renderer.h` — add SSAO FBO, kernel, noise texture
- Modify: `src/viewer/Renderer.cpp` — SSAO pass, blur pass

**Implementation:**
- Generate 64-sample hemisphere kernel (random directions, weighted toward surface)
- Generate 4x4 random rotation texture (tile across screen)
- SSAO shader: for each fragment, sample hemisphere around normal, check depth occlusion
- Bilateral blur pass to smooth noise
- Output: single-channel AO texture

**Step 1-2: Implement, commit**

```bash
git commit -m "feat(viewer): add SSAO (screen-space ambient occlusion)"
```

---

### Task 13: Deferred PBR lighting composite

**Files:**
- Modify: `src/viewer/Renderer.cpp` — fullscreen quad pass, reads G-Buffer + SSAO

**Implementation:**
- Full-screen quad shader reads all G-Buffer textures + SSAO texture
- Same Cook-Torrance BRDF as forward pass but applied to G-Buffer data
- AO multiplies ambient term
- ACES filmic tone mapping + gamma
- Edge overlay rendered forward on top of composited result

**Step 1-2: Implement, test visually, commit**

```bash
git commit -m "feat(viewer): deferred PBR lighting with SSAO composite"
```

---

## Phase E: Environment Map + IBL

Adds realistic reflections via image-based lighting.

### Task 14: Vendor stb_image.h

**Files:**
- Create: `vendor/stb/stb_image.h`
- Create: `vendor/stb/stb_image_write.h`

Download from https://github.com/nothings/stb:
```bash
mkdir -p vendor/stb
curl -o vendor/stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -o vendor/stb/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

```bash
git add vendor/stb/
git commit -m "vendor: add stb_image.h and stb_image_write.h"
```

---

### Task 15: Environment map + IBL precomputation

**Files:**
- Create: `src/viewer/EnvironmentMap.h`
- Create: `src/viewer/EnvironmentMap.cpp`
- Create: `assets/env/studio.hdr` (default HDR environment)

**Implementation:**
- Load HDR equirectangular image via `stb_image.h`
- Convert to cubemap (render equirect to 6 faces)
- Pre-filter cubemap at 5 mip levels (roughness 0.0 to 1.0) for specular IBL
- Generate irradiance map (32x32 cubemap) for diffuse IBL
- Generate BRDF integration LUT (512x512 texture)
- All precomputation done via GPU shader passes at startup

For the default studio HDR, use a simple procedural gradient (avoids shipping a binary asset):
- Top: warm white (sun)
- Horizon: light grey
- Bottom: dark grey (ground)
- This produces clean studio-style reflections

**Step 1-3: Implement, commit**

```bash
git add src/viewer/EnvironmentMap.h src/viewer/EnvironmentMap.cpp
git commit -m "feat(viewer): IBL environment map with irradiance and prefiltered cubemap"
```

---

### Task 16: Integrate IBL into PBR lighting

**Files:**
- Modify: `src/viewer/Renderer.cpp` — add IBL sampling to deferred lighting pass

**Implementation:**
Update the deferred lighting fragment shader:
- Sample irradiance cubemap for diffuse IBL: `texture(uIrradianceMap, N) * albedo * (1-metallic)`
- Sample pre-filtered cubemap at roughness mip level for specular IBL
- Sample BRDF LUT with `(NdotV, roughness)`
- Combine: `(irradiance * kD * albedo + prefilteredColor * (F * brdf.x + brdf.y)) * ao`

```bash
git commit -m "feat(viewer): integrate IBL into PBR lighting pass"
```

---

## Phase F: ImGui Integration

### Task 17: Vendor Dear ImGui (docking branch)

**Files:**
- Create: `vendor/imgui/` (all ImGui source files)

Clone the docking branch and copy core files:
```bash
git clone --branch docking https://github.com/ocornut/imgui.git /tmp/imgui-docking
mkdir -p vendor/imgui
cp /tmp/imgui-docking/imgui.h vendor/imgui/
cp /tmp/imgui-docking/imgui.cpp vendor/imgui/
cp /tmp/imgui-docking/imgui_demo.cpp vendor/imgui/
cp /tmp/imgui-docking/imgui_draw.cpp vendor/imgui/
cp /tmp/imgui-docking/imgui_internal.h vendor/imgui/
cp /tmp/imgui-docking/imgui_tables.cpp vendor/imgui/
cp /tmp/imgui-docking/imgui_widgets.cpp vendor/imgui/
cp /tmp/imgui-docking/imconfig.h vendor/imgui/
cp /tmp/imgui-docking/imstb_rectpack.h vendor/imgui/
cp /tmp/imgui-docking/imstb_textedit.h vendor/imgui/
cp /tmp/imgui-docking/imstb_truetype.h vendor/imgui/
cp /tmp/imgui-docking/backends/imgui_impl_glfw.h vendor/imgui/backends/
cp /tmp/imgui-docking/backends/imgui_impl_glfw.cpp vendor/imgui/backends/
cp /tmp/imgui-docking/backends/imgui_impl_opengl3.h vendor/imgui/backends/
cp /tmp/imgui-docking/backends/imgui_impl_opengl3.cpp vendor/imgui/backends/
cp /tmp/imgui-docking/backends/imgui_impl_opengl3_loader.h vendor/imgui/backends/
rm -rf /tmp/imgui-docking
```

Update CMakeLists.txt to build imgui as a static library:
```cmake
# Vendor: Dear ImGui (docking branch)
file(GLOB IMGUI_SOURCES vendor/imgui/*.cpp
     vendor/imgui/backends/imgui_impl_glfw.cpp
     vendor/imgui/backends/imgui_impl_opengl3.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC vendor/imgui vendor/imgui/backends)
target_link_libraries(imgui PRIVATE PkgConfig::GLFW glad)
if(NOT MSVC)
  target_compile_options(imgui PRIVATE -w)
endif()
```

Add `imgui` to opendcad_viewer link libraries.

```bash
git add vendor/imgui/ CMakeLists.txt
git commit -m "vendor: add Dear ImGui (docking branch)"
```

---

### Task 18: ImGui setup and docking layout

**Files:**
- Create: `src/viewer/ui/ImGuiSetup.h`
- Create: `src/viewer/ui/ImGuiSetup.cpp`

**Implementation:**
- `ImGuiSetup::init(GLFWwindow*)` — initialize ImGui context, set dark theme, enable docking
- `ImGuiSetup::beginFrame()` — `ImGui_ImplOpenGL3_NewFrame()` + `ImGui_ImplGlfw_NewFrame()` + `ImGui::NewFrame()` + dockspace
- `ImGuiSetup::endFrame()` — `ImGui::Render()` + `ImGui_ImplOpenGL3_RenderDrawData()`
- `ImGuiSetup::shutdown()` — cleanup

Dark theme customization for professional CAD look:
- Dark backgrounds (#1A1A1E)
- Subtle borders (#2A2A2E)
- Accent color (blue: #4488CC)
- Clean fonts (ImGui default at 16px or embed a better font later)

```bash
git add src/viewer/ui/ImGuiSetup.h src/viewer/ui/ImGuiSetup.cpp
git commit -m "feat(viewer): ImGui docking setup with dark CAD theme"
```

---

### Task 19: Object Panel

**Files:**
- Create: `src/viewer/ui/ObjectPanel.h`
- Create: `src/viewer/ui/ObjectPanel.cpp`

**Implementation:**
- Tree of RenderObjects with checkboxes for visibility
- Click to select (sets `selected = true` on object, populates other panels)
- Shows name and triangle count per object

```bash
git add src/viewer/ui/ObjectPanel.h src/viewer/ui/ObjectPanel.cpp
git commit -m "feat(viewer): ImGui object panel with visibility toggles"
```

---

### Task 20: Layer Panel

**Files:**
- Create: `src/viewer/ui/LayerPanel.h`
- Create: `src/viewer/ui/LayerPanel.cpp`

**Implementation:**
- Lists unique tags from all RenderObjects
- Checkbox toggles visibility for all objects with that tag
- Auto-assigned layer colors for visual distinction

```bash
git add src/viewer/ui/LayerPanel.h src/viewer/ui/LayerPanel.cpp
git commit -m "feat(viewer): ImGui layer panel (tags as layers)"
```

---

### Task 21: Properties Panel + Material Panel

**Files:**
- Create: `src/viewer/ui/PropertiesPanel.h`
- Create: `src/viewer/ui/PropertiesPanel.cpp`
- Create: `src/viewer/ui/MaterialPanel.h`
- Create: `src/viewer/ui/MaterialPanel.cpp`

**Properties Panel:**
- Shows selected object name, face count, edge count, triangle count
- Volume and surface area (computed via `BRepGProp`)
- Bounding box dimensions

**Material Panel:**
- Shows selected object's material preset, metallic, roughness as read-only sliders
- Color swatch (uses ImGui::ColorButton)

```bash
git add src/viewer/ui/PropertiesPanel.h src/viewer/ui/PropertiesPanel.cpp \
        src/viewer/ui/MaterialPanel.h src/viewer/ui/MaterialPanel.cpp
git commit -m "feat(viewer): ImGui properties and material panels"
```

---

### Task 22: Menu Bar + Viewport Overlay

**Files:**
- Create: `src/viewer/ui/MenuBar.h`
- Create: `src/viewer/ui/MenuBar.cpp`
- Create: `src/viewer/ui/ViewportOverlay.h`
- Create: `src/viewer/ui/ViewportOverlay.cpp`

**Menu Bar:**
- File: Screenshot (Ctrl+S), Close
- View: Reset camera (Space), Fit all (F), Isometric (I), Toggle grid (G), Toggle edges (E)
- Render: Quality presets, wireframe/shaded/x-ray modes
- Help: Keyboard shortcuts dialog, About dialog

**Viewport Overlay:**
- Bottom status bar: triangle count, quality preset, file watch status, FPS

```bash
git add src/viewer/ui/MenuBar.h src/viewer/ui/MenuBar.cpp \
        src/viewer/ui/ViewportOverlay.h src/viewer/ui/ViewportOverlay.cpp
git commit -m "feat(viewer): ImGui menu bar and viewport overlay"
```

---

### Task 23: Wire up all ImGui panels to ViewerApp

**Files:**
- Modify: `src/viewer/ViewerApp.h` — add UI member pointers
- Modify: `src/viewer/ViewerApp.cpp` — integrate ImGui into render loop
- Modify: `CMakeLists.txt` — add all UI source files

**Render loop order:**
1. Begin ImGui frame + dockspace
2. Draw all panels (Object, Layer, Properties, Material, Menu, Overlay)
3. Get viewport region from ImGui dockspace central node
4. Render 3D scene into viewport region (G-Buffer → SSAO → PBR → edges)
5. End ImGui frame (renders UI on top)

```bash
git add src/viewer/ CMakeLists.txt
git commit -m "feat(viewer): integrate all ImGui panels into render loop"
```

---

## Phase G: CLI Integration + Hot Reload + Screenshot

### Task 24: Add `view` subcommand to CLI

**Files:**
- Modify: `src/cli/CliOptions.h` — add `viewMode` flag and subcommand detection
- Modify: `src/main.cpp` — detect `view` subcommand, launch viewer

**Implementation:**

In `parseArgs()`, check if `argv[1]` is `"view"`. If so, set `opts.viewMode = true` and shift remaining args.

In `main()`, after evaluation, if `viewMode`:
1. Create `ViewerApp`
2. Pass evaluated exports directly to viewer
3. Start file watcher on the .dcad source
4. Enter viewer run loop (blocks until window closed)

```bash
git add src/cli/CliOptions.h src/main.cpp
git commit -m "feat(cli): add 'opendcad view' subcommand"
```

---

### Task 25: STEP + JSON sidecar loading

**Files:**
- Create: `src/viewer/StepLoader.h`
- Create: `src/viewer/StepLoader.cpp`

**Implementation:**
- `StepLoader::load(path)` — load STEP via `STEPCAFControl_Reader`, read companion `.json` sidecar
- Returns vector of shapes with colors, materials, tags populated from JSON
- Uses `XCAFDoc_ShapeTool` for geometry, `XCAFDoc_ColorTool` for XDE colors
- Parses JSON sidecar (hand-written parser or simple sscanf — no external JSON library needed for the simple format)

```bash
git add src/viewer/StepLoader.h src/viewer/StepLoader.cpp
git commit -m "feat(viewer): STEP + JSON sidecar loading"
```

---

### Task 26: File watcher + hot reload

**Files:**
- Create: `src/viewer/FileWatcher.h`
- Create: `src/viewer/FileWatcher.cpp`

**Implementation:**
- `FileWatcher::watch(path)` — start watching a file
- `FileWatcher::addPath(path)` — add additional paths (imported files)
- `FileWatcher::poll()` — returns true if any watched file changed (non-blocking)
- macOS: `kqueue` + `EVFILT_VNODE` with `NOTE_WRITE`
- Linux: `inotify_init()` + `inotify_add_watch()` with `IN_MODIFY`
- Fallback: `stat()` polling every 500ms

In ViewerApp, on each frame: check `fileWatcher_->poll()`. If changed:
1. Re-evaluate the script
2. Compare exports — re-tessellate changed shapes
3. Update RenderScene
4. Show "Reloading..." in status bar
5. On error: show error message, keep last good state

```bash
git add src/viewer/FileWatcher.h src/viewer/FileWatcher.cpp
git commit -m "feat(viewer): file watcher with hot-reload"
```

---

### Task 27: Screenshot export

**Files:**
- Create: `src/viewer/Screenshot.h`
- Create: `src/viewer/Screenshot.cpp`

**Implementation:**
- `Screenshot::capture(renderer, width, height, path)`:
  1. Create offscreen FBO at specified resolution
  2. Render scene (without ImGui) into FBO
  3. `glReadPixels()` into buffer
  4. Flip vertically (OpenGL origin is bottom-left)
  5. Write PNG via `stb_image_write.h`
- Default filename: `<export-name>_screenshot_<timestamp>.png`
- Triggered by Ctrl+S / menu item
- Optional: 2x resolution mode for marketing screenshots

```bash
git add src/viewer/Screenshot.h src/viewer/Screenshot.cpp
git commit -m "feat(viewer): PNG screenshot export"
```

---

## Phase H: Polish + Delete Old Viewer

### Task 28: Delete old viewer code

**Files:**
- Delete: `src/viewer/viewer.cpp` (the 780-line prototype)
- Delete: `src/viewer/stb_easy_font.h`

```bash
git rm src/viewer/viewer.cpp src/viewer/stb_easy_font.h
git commit -m "chore: remove old GL 2.1 prototype viewer"
```

---

### Task 29: Final integration test

**Step 1: Build everything**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target opendcad opendcad_viewer opendcad_tests
```

**Step 2: Run unit tests**

```bash
./build/bin/opendcad_tests
```

Expected: All tests pass (including new Tessellator tests).

**Step 3: Manual viewer test**

```bash
./build/bin/opendcad_viewer examples/battery.dcad
```

Expected:
- PBR-rendered geometry with proper shading
- Grid and axes
- ImGui panels: Object, Layer, Properties, Material
- Camera orbit/pan/zoom works
- F key fits, I key goes isometric
- Screenshots work via menu or Ctrl+S

**Step 4: Test hot-reload**

```bash
./build/bin/opendcad view examples/battery.dcad &
# Edit battery.dcad, save — viewer should update automatically
```

**Step 5: Test STEP loading**

```bash
./build/bin/opendcad examples/battery.dcad  # generates build/battery.step + battery.json
./build/bin/opendcad_viewer build/battery.step
```

Expected: Same geometry with colors/materials loaded from JSON sidecar.

**Step 6: Commit final polish**

```bash
git commit -m "feat(viewer): complete viewer overhaul — PBR, SSAO, ImGui, hot-reload"
```

---

## Task Dependency Graph

```
Phase A (Foundation):     T1 → T2 → T3 → T4 → T5
Phase B (Tessellation):   T5 → T6 → T7
Phase C (Rendering):      T5 + T6 → T8 → T9 → T10
Phase D (Deferred):       T10 → T11 → T12 → T13
Phase E (IBL):            T13 → T14 → T15 → T16
Phase F (ImGui):          T10 → T17 → T18 → T19 → T20 → T21 → T22 → T23
Phase G (Integration):    T23 → T24 → T25 → T26 → T27
Phase H (Polish):         T27 → T28 → T29
```

Phases D/E and F can run in parallel (deferred rendering + ImGui are independent until Task 23 wires them together).

---

## Key Gotchas Reference

1. **OCCT 1-based indexing** — `tri->Triangle(i)` and `tri->Node(i)` start at 1
2. **Face orientation** — check `TopAbs_REVERSED`, swap winding to fix inverted normals
3. **Location transforms** — apply `TopLoc_Location` to all node positions
4. **Edge fallback** — `Poly_Polygon3D` not always available, need `PolygonOnTriangulation` fallback
5. **macOS OpenGL** — max 4.1, `GL_SILENCE_DEPRECATION` required, no `glLineWidth > 1.0`
6. **VAO required** — GL 4.1 core profile requires bound VAO for all draw calls
7. **No immediate mode** — no `glBegin/glEnd`, `glPushAttrib`, `glMatrixMode`
8. **glad before GLFW** — `#include <glad/glad.h>` must come before `#include <GLFW/glfw3.h>`
9. **enable_shared_from_this** — use `std::make_shared<T>(...)` everywhere for Shape objects
10. **ReturnValue exception** — loops in evaluator must NOT catch it (function return unwinding)
