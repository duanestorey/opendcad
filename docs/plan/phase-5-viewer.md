# Phase 5: Viewer Enhancement

## Goal

Replace the current 780-line prototype viewer (`src/viewer.cpp`) with a production-quality 3D CAD viewer. The current viewer loads STL files, uses OpenGL 2.1 with GLSL 1.2 shaders, renders with immediate mode for grid/axes/HUD text, and has a basic orbital camera. Phase 5 upgrades every layer: modern OpenGL 3.3+ core profile, direct BRep tessellation (eliminating the STL round-trip), multi-object rendering, face/edge selection via ID buffer picking, ImGui-based UI, measurement tools, DFM visualization overlays, hot-reload, and screenshot/export.

---

## Current State Analysis

### What exists (`src/viewer.cpp`, 780 lines)

| Aspect | Current Implementation |
|--------|----------------------|
| OpenGL version | GL 2.1 compatibility profile (`GLFW_CONTEXT_VERSION_MAJOR=2`) |
| Shaders | GLSL `#version 120`, `attribute`/`varying`, `gl_FragColor` |
| Geometry source | Loads STL file via `RWStl::ReadFile`, builds interleaved VBO `[pos(3), normal(3)]` |
| Grid/axes | Immediate mode `glBegin(GL_LINES)`/`glEnd()` with fixed-function matrix stack |
| HUD text | `stb_easy_font.h` rendered via `glBegin(GL_QUADS)`/`glEnd()` |
| Camera | Orbital with Z-up spherical coords, camera-space pan, zoom-to-cursor via scroll |
| State management | Mixed: shader program for mesh, fixed-function pipeline for overlays, `glPushAttrib(GL_ALL_ATTRIB_BITS)` for isolation |
| Objects | Single mesh, single color |
| Selection | None |
| UI framework | None (raw GLFW callbacks) |

### Problems to solve

1. **GL 2.1 / GLSL 1.2 is deprecated** -- core profile 3.3+ removes immediate mode, `gl_FragColor`, `attribute`/`varying`, fixed-function matrix stack.
2. **STL loading discards BRep topology** -- no face IDs, no edge data, no per-face colors, no surface type information.
3. **Single object only** -- cannot render multiple `export` entries independently.
4. **No selection or picking** -- cannot inspect faces, edges, or measurements.
5. **`stb_easy_font` text is crude** -- no scalable text, no UI panels, no interactive controls.
6. **Triple `glfwSwapBuffers` call** on lines 765, 768, 770 -- a bug causing wasted frames.
7. **No hot-reload** -- must restart viewer after script changes.

---

## 1. OpenGL 3.3+ Core Profile Upgrade

### GLFW Window Hints

Replace the current GL 2.1 hints:

```cpp
// Before (src/viewer.cpp lines 377-381):
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);

// After:
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);  // Required on macOS
```

### GL Function Loader

Core profile on non-Apple platforms requires a loader. Use `glad` (single-file, public domain).

```cmake
# CMakeLists.txt addition
# glad (OpenGL 3.3 core loader)
add_library(glad STATIC vendor/glad/src/glad.c)
target_include_directories(glad PUBLIC vendor/glad/include)
target_link_libraries(opendcad_viewer PRIVATE glad)
```

On macOS, the system OpenGL framework provides core profile functions directly, but `glad` works cross-platform and is the safer choice. Wrap the include:

```cpp
#include <glad/glad.h>  // must come before GLFW
#include <GLFW/glfw3.h>
```

After `glfwMakeContextCurrent(win)`, initialize glad:

```cpp
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::fprintf(stderr, "Failed to initialize GLAD\n");
    return 1;
}
```

### Shader Upgrade: GLSL 1.2 to 3.30

**Vertex shader (main mesh):**

```glsl
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aFaceID;    // for picking
layout(location = 3) in vec3 aColor;      // per-vertex color (optional)

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;               // inverse-transpose of model 3x3
uniform vec3 uLightDir;
uniform vec3 uObjectColor;                // per-object base color

out vec3 vWorldNormal;
out vec3 vWorldPos;
out vec3 vViewDir;
out vec3 vObjectColor;
flat out float vFaceID;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uMVP * vec4(aPos, 1.0);

    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vWorldPos = worldPos.xyz;
    vViewDir = normalize(-worldPos.xyz);   // camera at origin in view space
    vObjectColor = uObjectColor;
    vFaceID = aFaceID;
}
```

**Fragment shader (Blinn-Phong + rim):**

```glsl
#version 330 core

in vec3 vWorldNormal;
in vec3 vWorldPos;
in vec3 vViewDir;
in vec3 vObjectColor;
flat in float vFaceID;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform int uViewMode;        // 0=shaded, 1=wireframe, 2=shaded+edges, 3=xray
uniform float uAlpha;         // for x-ray mode (0.3) or normal (1.0)
uniform int uHighlightFace;   // face ID to highlight (-1 = none)
uniform vec3 uHighlightColor; // highlight overlay color

out vec4 FragColor;

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);

    // Blinn-Phong
    float NdotL = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // Rim light
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.5);

    vec3 ambient  = vObjectColor * 0.25;
    vec3 diffuse  = vObjectColor * NdotL;
    vec3 specular = vec3(0.9) * spec * 0.5;
    vec3 tint     = mix(vec3(0.0), 0.5 + 0.5 * N, 0.15);

    vec3 col = ambient + diffuse + specular + rim * 0.15 + tint;

    // Face highlight
    if (uHighlightFace >= 0 && abs(vFaceID - float(uHighlightFace)) < 0.5) {
        col = mix(col, uHighlightColor, 0.4);
    }

    // Gamma correction
    col = pow(col, vec3(1.0 / 2.2));

    FragColor = vec4(col, uAlpha);
}
```

**ID buffer fragment shader (for picking pass):**

```glsl
#version 330 core

flat in float vFaceID;

out vec4 FragColor;

void main() {
    // Encode face ID as RGB: up to 16M unique faces
    int id = int(vFaceID);
    float r = float((id >>  0) & 0xFF) / 255.0;
    float g = float((id >>  8) & 0xFF) / 255.0;
    float b = float((id >> 16) & 0xFF) / 255.0;
    FragColor = vec4(r, g, b, 1.0);
}
```

**Edge vertex shader:**

```glsl
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aEdgeID;

uniform mat4 uMVP;
uniform float uDepthBias;  // e.g., -0.0005

flat out float vEdgeID;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    // Apply slight depth bias to push edges in front of faces
    gl_Position.z += uDepthBias * gl_Position.w;
    vEdgeID = aEdgeID;
}
```

**Edge fragment shader:**

```glsl
#version 330 core

flat in float vEdgeID;

uniform vec3 uEdgeColor;  // default: vec3(0.1, 0.1, 0.12)
uniform int uHighlightEdge;
uniform vec3 uHighlightColor;

out vec4 FragColor;

void main() {
    vec3 col = uEdgeColor;
    if (uHighlightEdge >= 0 && abs(vEdgeID - float(uHighlightEdge)) < 0.5) {
        col = uHighlightColor;
    }
    FragColor = vec4(col, 1.0);
}
```

**Grid/axes vertex shader:**

```glsl
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
```

**Grid/axes fragment shader:**

```glsl
#version 330 core

in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
```

### Shader Uniform Summary

| Shader | Uniform | Type | Purpose |
|--------|---------|------|---------|
| Main mesh VS | `uMVP` | `mat4` | Model-view-projection matrix |
| Main mesh VS | `uModel` | `mat4` | Model matrix (for world-space normals) |
| Main mesh VS | `uNormalMatrix` | `mat3` | Inverse-transpose of model 3x3 |
| Main mesh VS | `uLightDir` | `vec3` | Directional light direction (normalized) |
| Main mesh VS | `uObjectColor` | `vec3` | Base object color from palette |
| Main mesh FS | `uLightDir` | `vec3` | Same as VS (repeated for fragment lighting) |
| Main mesh FS | `uCameraPos` | `vec3` | Camera world position (for specular) |
| Main mesh FS | `uViewMode` | `int` | 0=shaded, 1=wireframe, 2=shaded+edges, 3=xray |
| Main mesh FS | `uAlpha` | `float` | Fragment opacity (1.0 normal, 0.3 x-ray) |
| Main mesh FS | `uHighlightFace` | `int` | Face ID to highlight (-1 = none) |
| Main mesh FS | `uHighlightColor` | `vec3` | Overlay color for highlighted face |
| Edge VS | `uMVP` | `mat4` | Model-view-projection matrix |
| Edge VS | `uDepthBias` | `float` | NDC Z-bias to prevent z-fighting (-0.0005) |
| Edge FS | `uEdgeColor` | `vec3` | Default edge color (dark gray) |
| Edge FS | `uHighlightEdge` | `int` | Edge ID to highlight (-1 = none) |
| Edge FS | `uHighlightColor` | `vec3` | Overlay color for highlighted edge |
| Grid VS | `uMVP` | `mat4` | Model-view-projection matrix |
| ID buffer FS | (none beyond vFaceID varying) | | Encodes face ID as RGB |

### Replace Immediate-Mode Grid and Axes

The current `draw_grid()` and `draw_axes()` functions (lines 336-360) use `glBegin`/`glEnd`. Replace with a `GridMesh` class that pre-builds a VBO/VAO.

```cpp
// src/viewer/GridMesh.h
namespace opendcad {

class GridMesh {
public:
    // Build grid lines + axis lines into a single VBO
    // Vertex format: [x, y, z, r, g, b, a]
    void build(float gridSize = 500.0f, float gridStep = 10.0f,
               float majorStep = 50.0f, float axisLength = 30.0f);

    void draw(GLuint shaderProgram, const float* mvp);

    void destroy();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei gridVertexCount_ = 0;   // grid lines
    GLsizei axisVertexCount_ = 0;   // axis lines (drawn after grid)
    GLsizei totalVertexCount_ = 0;
};

} // namespace opendcad
```

**Vertex buffer layout for grid/axes:**

| Offset (bytes) | Component | Type | Description |
|----------------|-----------|------|-------------|
| 0 | position.x | `float` | X coordinate |
| 4 | position.y | `float` | Y coordinate |
| 8 | position.z | `float` | Z coordinate |
| 12 | color.r | `float` | Red channel |
| 16 | color.g | `float` | Green channel |
| 20 | color.b | `float` | Blue channel |
| 24 | color.a | `float` | Alpha channel |
| **28** | | | **Stride** |

Implementation approach:
- Pre-compute all grid line endpoints with colors (minor lines: alpha 0.10, major lines: alpha 0.22).
- Append axis endpoints (X=red, Y=green, Z=blue, full alpha).
- Upload once to a single VBO. Bind VAO. Draw with `glDrawArrays(GL_LINES, ...)`.
- Grid drawn with depth test disabled (as currently done), axes on top.

### Replace Immediate-Mode HUD Text

Eliminated entirely in favor of ImGui (see Section 9). The `draw_text_screen()` function, `stb_easy_font.h`, and the 80-line overlay block (lines 679-762) are all removed.

### VAO Requirement

OpenGL 3.3 core profile requires a VAO to be bound for any `glDrawArrays`/`glDrawElements` call. Every renderable resource (mesh, grid, edges) must have its own VAO.

```cpp
// Pattern for all VBO/VAO creation:
GLuint vao, vbo;
glGenVertexArrays(1, &vao);
glGenBuffers(1, &vbo);

glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, dataSize, dataPtr, GL_STATIC_DRAW);

// Position (location = 0)
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

// Normal (location = 1)
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));

// ... additional attributes ...

glBindVertexArray(0);
```

### Gotchas

- **macOS deprecation warnings:** macOS deprecated OpenGL after 10.14 but still supports 4.1 core. Keep `GL_SILENCE_DEPRECATION` define. OpenGL 3.3 is well within the supported range.
- **No `glPushAttrib`/`glPopAttrib` in core profile:** These are removed. All state must be managed explicitly. The current 80-line overlay block that uses `glPushAttrib(GL_ALL_ATTRIB_BITS)` must be deleted.
- **No `glMatrixMode`/`glLoadIdentity`/`glMultMatrixf`:** All matrix operations are CPU-side (already partially done with `Mat4` helpers). Pass matrices via uniforms only.
- **No `glBegin`/`glEnd`:** All geometry must go through VBO/VAO.
- **No `glDisableClientState`/`glEnableClientState`:** These are compatibility-only. Use `glEnableVertexAttribArray`/`glDisableVertexAttribArray`.
- **Triple `glfwSwapBuffers`:** Fix the bug on lines 765, 768, 770 -- keep only one call.

---

## 2. BRep Tessellation (Replace STL Loading)

### Motivation

The current pipeline exports `.stl` from `opendcad`, then the viewer loads it back. This discards all BRep topology: face boundaries, edge curves, surface types, face IDs. Phase 5 tessellates the `TopoDS_Shape` directly, preserving per-face and per-edge identity.

### Tessellator Class

```cpp
// src/viewer/Tessellator.h
#pragma once

#include <vector>
#include <cstdint>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct TessVertex {
    float pos[3];
    float normal[3];
    float faceID;     // integer encoded as float for shader attribute
    float color[3];   // per-face color (default: object color)
};

struct TessEdgeVertex {
    float pos[3];
    float edgeID;     // integer encoded as float
};

struct TessFaceInfo {
    int faceID;
    int triangleOffset;    // index into vertex array (first vertex of this face)
    int triangleCount;     // number of triangles for this face
    // BRep metadata for properties panel
    int surfaceType;       // GeomAbs_SurfaceType enum value
    double area;           // computed via BRepGProp
};

struct TessEdgeInfo {
    int edgeID;
    int vertexOffset;
    int vertexCount;       // number of line segment vertices
    double length;         // computed via BRepGProp
};

struct TessResult {
    std::vector<TessVertex> vertices;          // 3 per triangle, flat
    std::vector<TessFaceInfo> faces;
    std::vector<TessEdgeVertex> edgeVertices;  // 2 per line segment
    std::vector<TessEdgeInfo> edges;

    // Bounding box
    float bboxMin[3];
    float bboxMax[3];
};

class Tessellator {
public:
    // Tessellate a TopoDS_Shape, extracting face triangulations and edge polylines.
    //
    // deflection: controls mesh density (smaller = finer mesh).
    //             0.01 for detailed, 0.05 for rough preview.
    // angle:      angular deflection (radians). Default 0.5.
    static TessResult tessellate(const TopoDS_Shape& shape,
                                 double deflection = 0.01,
                                 double angle = 0.5);

private:
    static void extractFaces(const TopoDS_Shape& shape, TessResult& result);
    static void extractEdges(const TopoDS_Shape& shape, TessResult& result);
    static void computeBounds(TessResult& result);
};

} // namespace opendcad
```

### Vertex Buffer Layout (Face Mesh)

| Offset (bytes) | Component | Type | Shader Location | Description |
|----------------|-----------|------|-----------------|-------------|
| 0 | pos[0] | `float` | 0 | X position |
| 4 | pos[1] | `float` | 0 | Y position |
| 8 | pos[2] | `float` | 0 | Z position |
| 12 | normal[0] | `float` | 1 | Normal X |
| 16 | normal[1] | `float` | 1 | Normal Y |
| 20 | normal[2] | `float` | 1 | Normal Z |
| 24 | faceID | `float` | 2 | Face identifier (integer as float) |
| 28 | color[0] | `float` | 3 | Per-vertex red |
| 32 | color[1] | `float` | 3 | Per-vertex green |
| 36 | color[2] | `float` | 3 | Per-vertex blue |
| **40** | | | | **Stride = sizeof(TessVertex)** |

### Vertex Buffer Layout (Edge Mesh)

| Offset (bytes) | Component | Type | Shader Location | Description |
|----------------|-----------|------|-----------------|-------------|
| 0 | pos[0] | `float` | 0 | X position |
| 4 | pos[1] | `float` | 0 | Y position |
| 8 | pos[2] | `float` | 0 | Z position |
| 12 | edgeID | `float` | 1 | Edge identifier (integer as float) |
| **16** | | | | **Stride = sizeof(TessEdgeVertex)** |

### Implementation Details

**`tessellate()` method:**

```cpp
TessResult Tessellator::tessellate(const TopoDS_Shape& shape,
                                   double deflection, double angle) {
    TessResult result;

    // Step 1: Mesh the shape (required before accessing Poly_Triangulation)
    BRepMesh_IncrementalMesh mesher(shape, deflection, false, angle, true);
    mesher.Perform();
    if (!mesher.IsDone()) {
        throw std::runtime_error("BRepMesh_IncrementalMesh failed");
    }

    // Step 2: Extract triangulated faces
    extractFaces(shape, result);

    // Step 3: Extract edge polylines
    extractEdges(shape, result);

    // Step 4: Compute bounding box
    computeBounds(result);

    return result;
}
```

**`extractFaces()` implementation outline:**

```cpp
void Tessellator::extractFaces(const TopoDS_Shape& shape, TessResult& result) {
    int faceID = 0;

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face& face = TopoDS::Face(explorer.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);

        if (tri.IsNull()) {
            faceID++;
            continue;
        }

        TessFaceInfo faceInfo;
        faceInfo.faceID = faceID;
        faceInfo.triangleOffset = static_cast<int>(result.vertices.size()) / 3;
        faceInfo.triangleCount = tri->NbTriangles();

        // Get surface type
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (!surf.IsNull()) {
            GeomAdaptor_Surface adaptor(surf);
            faceInfo.surfaceType = static_cast<int>(adaptor.GetType());
        }

        // Get face area
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        faceInfo.area = props.Mass();

        const gp_Trsf& trsf = loc.Transformation();
        bool reversed = (face.Orientation() == TopAbs_REVERSED);

        for (int i = 1; i <= tri->NbTriangles(); i++) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);

            // Respect face orientation
            if (reversed) std::swap(n2, n3);

            // Get positions, apply location transform
            gp_Pnt p1 = tri->Node(n1).Transformed(trsf);
            gp_Pnt p2 = tri->Node(n2).Transformed(trsf);
            gp_Pnt p3 = tri->Node(n3).Transformed(trsf);

            // Compute face normal
            gp_Vec v1(p1, p2);
            gp_Vec v2(p1, p3);
            gp_Vec normal = v1.Crossed(v2);
            if (normal.SquareMagnitude() > 1e-12) normal.Normalize();

            // If triangulation has per-vertex normals, use those instead
            // (tri->HasNormals() in OCCT 7.6+)

            auto pushVertex = [&](const gp_Pnt& p, const gp_Vec& n) {
                TessVertex v;
                v.pos[0] = static_cast<float>(p.X());
                v.pos[1] = static_cast<float>(p.Y());
                v.pos[2] = static_cast<float>(p.Z());
                v.normal[0] = static_cast<float>(n.X());
                v.normal[1] = static_cast<float>(n.Y());
                v.normal[2] = static_cast<float>(n.Z());
                v.faceID = static_cast<float>(faceID);
                v.color[0] = v.color[1] = v.color[2] = 0.72f;  // default
                result.vertices.push_back(v);
            };

            pushVertex(p1, normal);
            pushVertex(p2, normal);
            pushVertex(p3, normal);
        }

        result.faces.push_back(faceInfo);
        faceID++;
    }
}
```

**`extractEdges()` implementation outline:**

```cpp
void Tessellator::extractEdges(const TopoDS_Shape& shape, TessResult& result) {
    int edgeID = 0;

    for (TopExp_Explorer explorer(shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(explorer.Current());

        TessEdgeInfo edgeInfo;
        edgeInfo.edgeID = edgeID;
        edgeInfo.vertexOffset = static_cast<int>(result.edgeVertices.size());

        // Get edge length
        GProp_GProps props;
        BRepGProp::LinearProperties(edge, props);
        edgeInfo.length = props.Mass();

        // Get polygon on triangulation from the first face adjacent to this edge
        TopLoc_Location loc;
        Handle(Poly_Polygon3D) poly3d = BRep_Tool::Polygon3D(edge, loc);

        if (!poly3d.IsNull()) {
            const TColgp_Array1OfPnt& nodes = poly3d->Nodes();
            const gp_Trsf& trsf = loc.Transformation();

            for (int i = nodes.Lower(); i < nodes.Upper(); i++) {
                gp_Pnt p1 = nodes(i).Transformed(trsf);
                gp_Pnt p2 = nodes(i + 1).Transformed(trsf);

                TessEdgeVertex ev1, ev2;
                ev1.pos[0] = static_cast<float>(p1.X());
                ev1.pos[1] = static_cast<float>(p1.Y());
                ev1.pos[2] = static_cast<float>(p1.Z());
                ev1.edgeID = static_cast<float>(edgeID);

                ev2.pos[0] = static_cast<float>(p2.X());
                ev2.pos[1] = static_cast<float>(p2.Y());
                ev2.pos[2] = static_cast<float>(p2.Z());
                ev2.edgeID = static_cast<float>(edgeID);

                result.edgeVertices.push_back(ev1);
                result.edgeVertices.push_back(ev2);
            }
        } else {
            // Fallback: try PolygonOnTriangulation for edges that only have
            // polygon data relative to an adjacent face's triangulation.
            // Iterate faces to find one sharing this edge.
            // Use BRep_Tool::PolygonOnTriangulation(edge, poly, tri, loc)
            // then index into tri->Node() using poly->Nodes().
        }

        edgeInfo.vertexCount = static_cast<int>(result.edgeVertices.size()) - edgeInfo.vertexOffset;
        if (edgeInfo.vertexCount > 0) {
            result.edges.push_back(edgeInfo);
        }
        edgeID++;
    }
}
```

### OCCT Headers Required

```cpp
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopLoc_Location.hxx>
#include <GeomAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_Orientation.hxx>
```

### CMake: Additional OCCT Components

The viewer's `link_occt()` call already links the base components. Add these for tessellation + properties:

```cmake
set(_occt_viewer_components
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKTopAlgo TKSTL
    TKMesh         # BRepMesh_IncrementalMesh
    TKGeomAlgo     # GeomAdaptor_Surface
    TKShHealing    # ShapeFix (for robust tessellation)
)
```

### Gotchas

- **OCCT indexing is 1-based.** `tri->Triangle(i)` and `tri->Node(i)` start at 1, not 0. Off-by-one errors here cause crashes or garbage geometry.
- **Face orientation.** `TopoDS_Face` can be `TopAbs_REVERSED`, meaning the triangle winding must be flipped. Failing to handle this causes inverted normals on ~half the faces.
- **Location transforms.** Each face has a `TopLoc_Location` that must be applied to node positions. Forgetting this causes faces to appear at the origin instead of their correct position.
- **`Poly_Polygon3D` vs `PolygonOnTriangulation`.** Some edges only have polygon data relative to a face's triangulation, not standalone 3D polygon data. The fallback path through `BRep_Tool::PolygonOnTriangulation` is essential for complete edge coverage.
- **Smooth normals.** OCCT 7.6+ provides per-vertex normals via `tri->HasNormals()` / `tri->Normal(i)`. Earlier versions only give face normals from the cross product. Prefer per-vertex normals when available for smooth shading on curved surfaces.

---

## 3. Edge Rendering

### Separate Edge Pass

Edges are rendered in a separate draw pass after faces, using the edge shader with depth bias to prevent z-fighting.

```cpp
void renderEdges(const RenderObject& obj, GLuint edgeShader, const float* mvp) {
    if (obj.edgeVertexCount == 0) return;

    glUseProgram(edgeShader);
    glUniformMatrix4fv(glGetUniformLocation(edgeShader, "uMVP"), 1, GL_FALSE, mvp);
    glUniform1f(glGetUniformLocation(edgeShader, "uDepthBias"), -0.0005f);
    glUniform3f(glGetUniformLocation(edgeShader, "uEdgeColor"), 0.1f, 0.1f, 0.12f);
    glUniform1i(glGetUniformLocation(edgeShader, "uHighlightEdge"), obj.highlightedEdge);
    glUniform3f(glGetUniformLocation(edgeShader, "uHighlightColor"), 0.2f, 0.6f, 1.0f);

    glBindVertexArray(obj.edgeVAO);
    glDrawArrays(GL_LINES, 0, obj.edgeVertexCount);
    glBindVertexArray(0);
}
```

### Edge VBO and Shader

The edge mesh uses a separate VBO with the `TessEdgeVertex` layout (16 bytes per vertex: position + edgeID). The edge VAO configures two vertex attributes:

```cpp
// Edge VAO setup
glBindVertexArray(edgeVAO);
glBindBuffer(GL_ARRAY_BUFFER, edgeVBO);

// location 0: position (vec3)
glEnableVertexAttribArray(0);
glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(TessEdgeVertex),
                      (void*)offsetof(TessEdgeVertex, pos));

// location 1: edgeID (float)
glEnableVertexAttribArray(1);
glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(TessEdgeVertex),
                      (void*)offsetof(TessEdgeVertex, edgeID));

glBindVertexArray(0);
```

### Depth Bias Strategy

The vertex shader applies bias via `gl_Position.z += uDepthBias * gl_Position.w`. This shifts edges slightly toward the camera in NDC space without affecting screen position. The bias value of `-0.0005` is tuned so edges are visible on flat faces without causing artifacts on acute angles.

Alternative approach using `glPolygonOffset` for the face pass:
```cpp
// Before drawing faces:
glEnable(GL_POLYGON_OFFSET_FILL);
glPolygonOffset(1.0f, 1.0f);  // push faces back
// ... draw faces ...
glDisable(GL_POLYGON_OFFSET_FILL);
// ... draw edges normally (no bias needed) ...
```

Both approaches work. The vertex shader bias is more explicit and portable across drivers.

### Edge Picking

Edge IDs are stored in the `TessEdgeVertex.edgeID` field for potential future picking. For Phase 5, edge selection uses **Option B** (face-based proxy): clicking near an edge selects the adjacent face. True edge picking via rendered quads is deferred.

### Gotchas

- **Line width:** `glLineWidth(width)` with `width > 1.0` is not guaranteed in core profile. On macOS, only 1.0 is supported. For thicker edges, consider rendering edges as thin screen-aligned quads (a geometry shader or instanced quad approach). For Phase 5, 1px lines are acceptable.
- **Aliasing:** Enable multisampling (`glfwWindowHint(GLFW_SAMPLES, 4)`) to reduce edge aliasing.

---

## 4. Multi-Object Rendering

### RenderObject

Each `export` statement in the DSL produces an independent `RenderObject` with its own GPU resources, color, and visibility state.

```cpp
// src/viewer/RenderObject.h
#pragma once

#include <string>
#include <cstdint>
#include <glad/glad.h>
#include <TopoDS_Shape.hxx>
#include "Tessellator.h"

namespace opendcad {

struct RenderObject {
    std::string name;                  // from "export ... as <name>"
    TopoDS_Shape brepShape;            // original BRep for measurements/DFM

    // Face mesh GPU resources
    GLuint faceVAO = 0;
    GLuint faceVBO = 0;
    GLsizei faceVertexCount = 0;

    // Edge mesh GPU resources
    GLuint edgeVAO = 0;
    GLuint edgeVBO = 0;
    GLsizei edgeVertexCount = 0;

    // Tessellation metadata (CPU-side, for picking lookups)
    TessResult tessData;

    // Per-object state
    float color[3] = {0.72f, 0.78f, 0.86f};   // default: steel blue-grey
    bool visible = true;
    bool selected = false;
    int highlightedFace = -1;   // -1 = none
    int highlightedEdge = -1;

    // Bounding box
    float bboxMin[3] = {0, 0, 0};
    float bboxMax[3] = {0, 0, 0};

    // GPU upload
    void uploadToGPU(const TessResult& tess);

    // Cleanup
    void destroy();
};

} // namespace opendcad
```

**`uploadToGPU()` implementation:**

```cpp
void RenderObject::uploadToGPU(const TessResult& tess) {
    tessData = tess;
    faceVertexCount = static_cast<GLsizei>(tess.vertices.size());

    // Face VAO/VBO
    glGenVertexArrays(1, &faceVAO);
    glGenBuffers(1, &faceVBO);
    glBindVertexArray(faceVAO);
    glBindBuffer(GL_ARRAY_BUFFER, faceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 tess.vertices.size() * sizeof(TessVertex),
                 tess.vertices.data(), GL_STATIC_DRAW);

    // TessVertex layout: pos(3f) + normal(3f) + faceID(1f) + color(3f) = 10 floats
    const GLsizei stride = sizeof(TessVertex);
    // location 0: position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(TessVertex, pos));
    // location 1: normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(TessVertex, normal));
    // location 2: faceID
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(TessVertex, faceID));
    // location 3: color
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(TessVertex, color));

    glBindVertexArray(0);

    // Edge VAO/VBO
    edgeVertexCount = static_cast<GLsizei>(tess.edgeVertices.size());
    if (edgeVertexCount > 0) {
        glGenVertexArrays(1, &edgeVAO);
        glGenBuffers(1, &edgeVBO);
        glBindVertexArray(edgeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, edgeVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     tess.edgeVertices.size() * sizeof(TessEdgeVertex),
                     tess.edgeVertices.data(), GL_STATIC_DRAW);

        const GLsizei eStride = sizeof(TessEdgeVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, eStride,
                              (void*)offsetof(TessEdgeVertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, eStride,
                              (void*)offsetof(TessEdgeVertex, edgeID));

        glBindVertexArray(0);
    }

    // Copy bounding box
    std::memcpy(bboxMin, tess.bboxMin, sizeof(bboxMin));
    std::memcpy(bboxMax, tess.bboxMax, sizeof(bboxMax));
}
```

### Render Loop Structure

```cpp
// In the main render loop:
for (auto& obj : renderObjects_) {
    if (!obj.visible) continue;

    // Set per-object uniforms
    glUniform3fv(loc_uObjectColor, 1, obj.color);
    glUniform1i(loc_uHighlightFace, obj.highlightedFace);

    // Draw faces
    glBindVertexArray(obj.faceVAO);
    glDrawArrays(GL_TRIANGLES, 0, obj.faceVertexCount);

    // Draw edges (if view mode includes edges)
    if (viewMode_ == ViewMode::ShadedEdges || viewMode_ == ViewMode::Wireframe) {
        glUseProgram(edgeShader_);
        // set edge uniforms ...
        glBindVertexArray(obj.edgeVAO);
        glDrawArrays(GL_LINES, 0, obj.edgeVertexCount);
        glUseProgram(mainShader_);
    }
}
```

### Default Object Colors

Assign distinct colors to differentiate multiple objects:

```cpp
static const float kObjectColors[][3] = {
    {0.72f, 0.78f, 0.86f},   // steel blue-grey
    {0.86f, 0.72f, 0.72f},   // muted red
    {0.72f, 0.86f, 0.75f},   // muted green
    {0.82f, 0.78f, 0.70f},   // warm tan
    {0.75f, 0.72f, 0.86f},   // muted purple
    {0.86f, 0.82f, 0.72f},   // muted gold
};
```

Object at index `i` receives `kObjectColors[i % 6]`. Colors are user-overridable via the Part Tree panel (right-click color picker).

---

## 5. View Modes

### Enum and Keyboard Shortcuts

```cpp
// src/viewer/ViewMode.h
namespace opendcad {

enum class ViewMode {
    Shaded = 0,       // faces only, no edges
    Wireframe = 1,    // edges only, no faces
    ShadedEdges = 2,  // faces + edges (default)
    XRay = 3          // translucent faces + edges
};

} // namespace opendcad
```

### Keyboard Handling

Add to the existing key callback:

```cpp
// In glfwSetKeyCallback:
if (key == GLFW_KEY_1 && action == GLFW_PRESS) viewMode_ = ViewMode::Shaded;
if (key == GLFW_KEY_2 && action == GLFW_PRESS) viewMode_ = ViewMode::Wireframe;
if (key == GLFW_KEY_3 && action == GLFW_PRESS) viewMode_ = ViewMode::ShadedEdges;
if (key == GLFW_KEY_4 && action == GLFW_PRESS) viewMode_ = ViewMode::XRay;
```

### Rendering Behavior Per Mode

| Mode | Faces | Edges | Alpha | Depth Write | Notes |
|------|-------|-------|-------|-------------|-------|
| `1` Shaded | yes | no | 1.0 | yes | Clean CAD look |
| `2` Wireframe | no | yes | - | yes | Edges only |
| `3` Shaded+Edges | yes | yes | 1.0 | yes | Default, most useful |
| `4` X-Ray | yes | yes | 0.3 | no (faces) | See-through; disable depth write for faces, keep for edges |

### Keyboard Shortcut Summary

| Key | Action |
|-----|--------|
| `1` | Shaded mode |
| `2` | Wireframe mode |
| `3` | Shaded+Edges mode (default) |
| `4` | X-Ray mode |
| `F` | Fit view to model bounds |
| `I` | Snap to isometric view |
| `Space` | Reset view (front, centered) |
| `D` | Toggle point-to-point distance mode |
| `P` | Screenshot (standard resolution) |
| `Shift+P` | Screenshot (4x resolution) |
| `Escape` | Cancel current measurement / deselect |

### X-Ray Rendering Order

```cpp
if (viewMode_ == ViewMode::XRay) {
    // 1. Draw edges first (opaque, depth write ON)
    glDepthMask(GL_TRUE);
    for (auto& obj : renderObjects_) renderEdges(obj, ...);

    // 2. Draw translucent faces (depth write OFF, blend ON)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1f(loc_uAlpha, 0.3f);
    for (auto& obj : renderObjects_) renderFaces(obj, ...);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
```

---

## 6. Face/Edge Selection (ID Buffer Picking)

### PickBuffer Class

```cpp
// src/viewer/PickBuffer.h
namespace opendcad {

class PickBuffer {
public:
    void create(int width, int height);
    void resize(int width, int height);
    void destroy();

    void bind();          // bind FBO for rendering
    void unbind();        // restore default framebuffer

    // Read the face/edge ID at pixel (x, y).
    // Returns -1 if background was clicked.
    int readID(int x, int y) const;

    GLuint fbo() const { return fbo_; }

private:
    GLuint fbo_ = 0;
    GLuint colorTex_ = 0;    // RGB8 texture storing encoded IDs
    GLuint depthRBO_ = 0;    // depth renderbuffer
    int width_ = 0;
    int height_ = 0;
};

} // namespace opendcad
```

### Offscreen FBO Setup

```cpp
void PickBuffer::create(int width, int height) {
    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Color attachment: RGB8 for ID encoding (24-bit = 16M unique IDs)
    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_, 0);

    // Depth attachment
    glGenRenderbuffers(1, &depthRBO_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRBO_);

    // Verify
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "PickBuffer FBO incomplete\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
```

### ID Encoding and Decoding

Face IDs are encoded as `faceID + 1` in the shader so that background pixels (RGB 0,0,0) map to "no selection". The `readID()` method decodes:

```cpp
int PickBuffer::readID(int x, int y) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    unsigned char pixel[3] = {0, 0, 0};
    glReadPixels(x, height_ - y - 1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // Background is rendered as (0, 0, 0) which maps to ID 0.
    // We use ID 0 as "no selection" sentinel, so actual face IDs start at 1.
    int id = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
    return (id == 0) ? -1 : id - 1;  // -1 for background, 0-based ID otherwise
}
```

### Picking Pass

Render all objects using the ID buffer shader, encoding `faceID + 1`:

```cpp
void renderPickPass(const std::vector<RenderObject>& objects,
                    GLuint idShader, PickBuffer& pickBuf,
                    const float* mvp) {
    pickBuf.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);  // background = ID 0
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(idShader);
    glUniformMatrix4fv(glGetUniformLocation(idShader, "uMVP"), 1, GL_FALSE, mvp);

    // Disable blending, multisampling for clean integer IDs
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);

    for (auto& obj : objects) {
        if (!obj.visible) continue;
        glBindVertexArray(obj.faceVAO);
        glDrawArrays(GL_TRIANGLES, 0, obj.faceVertexCount);
    }

    pickBuf.unbind();
}
```

### Click Handling

```cpp
// In mouse button callback:
if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !shift) {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);

    // Account for Retina scaling
    int fbW, fbH, winW, winH;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    glfwGetWindowSize(win, &winW, &winH);
    float scaleX = (float)fbW / (float)winW;
    float scaleY = (float)fbH / (float)winH;

    int pixelX = (int)(mx * scaleX);
    int pixelY = (int)(my * scaleY);

    int faceID = pickBuffer_.readID(pixelX, pixelY);

    // Find which object owns this faceID and highlight it
    clearHighlights();
    if (faceID >= 0) {
        for (auto& obj : renderObjects_) {
            for (auto& fi : obj.tessData.faces) {
                if (fi.faceID == faceID) {
                    obj.highlightedFace = faceID;
                    // Update properties panel with face info
                    selectedFaceInfo_ = fi;
                    break;
                }
            }
        }
    }
}
```

### Gotchas

- **Retina / HiDPI displays:** The pixel coordinates from `glfwGetCursorPos` are in screen coordinates, not framebuffer pixels. On Retina displays, multiply by the scale factor (`framebufferSize / windowSize`).
- **MSAA interference:** Multisampling blends adjacent pixels, which corrupts integer IDs. Disable MSAA for the pick FBO (`GL_TEXTURE_2D`, not `GL_TEXTURE_2D_MULTISAMPLE`). The pick pass renders to a non-MSAA FBO while the visual pass uses the MSAA default framebuffer.
- **Performance:** The pick pass only needs to run on click, not every frame. Cache the result until the next camera move or geometry change.
- **ID offset:** Encode face IDs as `faceID + 1` so that background (0,0,0) is distinguishable from face ID 0.

---

## 7. Measurement Tools

### Architecture

```cpp
// src/viewer/Measurement.h
#pragma once

#include <vector>
#include <string>
#include <gp_Pnt.hxx>

namespace opendcad {

enum class MeasurementType {
    PointToPoint,
    FaceArea,
    EdgeLength
};

struct Measurement {
    MeasurementType type;
    std::string label;       // e.g., "23.45 mm"
    double value;            // raw numeric value

    // For point-to-point: two endpoints
    gp_Pnt p1, p2;

    // For face/edge: the ID of the measured feature
    int featureID = -1;
};

enum class MeasureMode {
    None,
    PointToPoint_First,   // waiting for first click
    PointToPoint_Second,  // waiting for second click
    FaceArea,             // click a face
    EdgeLength            // click an edge
};

class MeasurementManager {
public:
    void setMode(MeasureMode mode);
    MeasureMode mode() const;

    // Called when user clicks during measurement mode
    void handleClick(int faceID, const gp_Pnt& surfacePoint,
                     const TopoDS_Shape& shape);

    const std::vector<Measurement>& measurements() const;
    void clear();

    // Render dimension lines/labels (called during overlay pass)
    void render(GLuint lineShader, const float* mvp, const float* viewMatrix);

private:
    MeasureMode mode_ = MeasureMode::None;
    std::vector<Measurement> measurements_;
    gp_Pnt pendingPoint_;  // first point for point-to-point
};

} // namespace opendcad
```

### Point-to-Point Distance

When the user clicks in `PointToPoint_First` mode:
1. Use the face ID from picking to identify which face was clicked.
2. Unproject the screen click position to a ray.
3. Intersect the ray with the actual BRep face using `IntCurvesFace_Intersector` for the exact surface point.
4. Store as `pendingPoint_`.
5. Switch to `PointToPoint_Second` mode.
6. On second click, compute `pendingPoint_.Distance(secondPoint)`, create a `Measurement`.

**OCCT API for ray-face intersection:**

```cpp
#include <IntCurvesFace_Intersector.hxx>
#include <gp_Lin.hxx>

// Build ray from camera through click position
gp_Lin ray(eyePoint, gp_Dir(rayDirection));

// Intersect with the specific BRep face
IntCurvesFace_Intersector intersector(face, 1e-6);
intersector.Perform(ray, 0.0, 1e12);

if (intersector.NbPnt() > 0) {
    gp_Pnt hitPoint = intersector.Pnt(1);  // first intersection
    // Use hitPoint as the measured surface point
}
```

### Face Area

```cpp
// Using BRepGProp when a face is clicked:
GProp_GProps props;
BRepGProp::SurfaceProperties(face, props);
double area = props.Mass();
```

### Edge Length

```cpp
GProp_GProps props;
BRepGProp::LinearProperties(edge, props);
double length = props.Mass();
```

### Dimension Line Rendering

Dimension lines are rendered as screen-space overlays using ImGui's foreground draw list. This avoids the complexity of 3D dimension geometry and ensures labels are always readable regardless of camera angle.

```cpp
void MeasurementManager::renderImGui(const float* mvp, int fbW, int fbH) {
    auto project = [&](const gp_Pnt& p) -> ImVec2 {
        float clip[4];
        float v[4] = {(float)p.X(), (float)p.Y(), (float)p.Z(), 1.0f};
        // multiply by MVP
        for (int i = 0; i < 4; i++)
            clip[i] = mvp[i]*v[0] + mvp[i+4]*v[1] + mvp[i+8]*v[2] + mvp[i+12]*v[3];
        if (clip[3] <= 0) return {-1, -1};
        float ndcX = clip[0] / clip[3];
        float ndcY = clip[1] / clip[3];
        float sx = (ndcX * 0.5f + 0.5f) * fbW;
        float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * fbH;
        return {sx, sy};
    };

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (auto& m : measurements_) {
        if (m.type == MeasurementType::PointToPoint) {
            ImVec2 s1 = project(m.p1);
            ImVec2 s2 = project(m.p2);
            dl->AddLine(s1, s2, IM_COL32(255, 200, 50, 255), 2.0f);
            ImVec2 mid = {(s1.x + s2.x) * 0.5f, (s1.y + s2.y) * 0.5f};
            dl->AddText(mid, IM_COL32(255, 255, 255, 255), m.label.c_str());
        }
    }
}
```

### OCCT Classes Used

| Class | Purpose |
|-------|---------|
| `BRepGProp` | Compute surface area (`SurfaceProperties`) and edge length (`LinearProperties`) |
| `GProp_GProps` | Property container -- `Mass()` returns area or length depending on which `BRepGProp` method was called |
| `IntCurvesFace_Intersector` | Ray-face intersection for precise surface point picking |
| `gp_Lin` | Geometric line (ray definition) |
| `gp_Dir` | Normalized direction vector |

---

## 8. DFM Visualization

### Data Model

```cpp
// src/viewer/DFMVisualization.h
#pragma once

#include <vector>
#include <string>

namespace opendcad {

enum class DFMSeverity {
    Error,     // red overlay    -- rgb(0.9, 0.2, 0.2)
    Warning,   // yellow overlay -- rgb(0.9, 0.8, 0.2)
    Info       // blue overlay   -- rgb(0.3, 0.5, 0.9)
};

struct DFMIssue {
    DFMSeverity severity;
    std::string ruleName;       // e.g., "min_wall_thickness"
    std::string description;    // e.g., "Wall thickness 0.8mm < minimum 1.0mm"
    int faceID = -1;            // affected face (-1 if edge-based)
    int edgeID = -1;            // affected edge (-1 if face-based)
    float location[3] = {};     // centroid of affected region (for camera fly-to)
};

} // namespace opendcad
```

### Severity Color Table

| Severity | RGB | Hex | Use |
|----------|-----|-----|-----|
| Error | (0.9, 0.2, 0.2) | `#E63333` | Geometry that cannot be manufactured |
| Warning | (0.9, 0.8, 0.2) | `#E6CC33` | Geometry that may cause problems |
| Info | (0.3, 0.5, 0.9) | `#4D80E6` | Informational annotations |

### Color Overlay Rendering

Apply DFM colors by modifying the per-vertex color in the tessellation data, then re-uploading to the GPU:

```cpp
void applyDFMOverlays(RenderObject& obj, const std::vector<DFMIssue>& issues) {
    // Reset all vertex colors to default
    for (auto& v : obj.tessData.vertices) {
        v.color[0] = obj.color[0];
        v.color[1] = obj.color[1];
        v.color[2] = obj.color[2];
    }

    // Apply issue colors
    for (const auto& issue : issues) {
        if (issue.faceID < 0) continue;

        float issueColor[3];
        switch (issue.severity) {
            case DFMSeverity::Error:   issueColor[0]=0.9f; issueColor[1]=0.2f; issueColor[2]=0.2f; break;
            case DFMSeverity::Warning: issueColor[0]=0.9f; issueColor[1]=0.8f; issueColor[2]=0.2f; break;
            case DFMSeverity::Info:    issueColor[0]=0.3f; issueColor[1]=0.5f; issueColor[2]=0.9f; break;
        }

        // Find vertices belonging to this face
        for (auto& v : obj.tessData.vertices) {
            if (static_cast<int>(v.faceID) == issue.faceID) {
                v.color[0] = issueColor[0];
                v.color[1] = issueColor[1];
                v.color[2] = issueColor[2];
            }
        }
    }

    // Re-upload VBO
    glBindBuffer(GL_ARRAY_BUFFER, obj.faceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    obj.tessData.vertices.size() * sizeof(TessVertex),
                    obj.tessData.vertices.data());
}
```

### Pulsing Animation (Optional)

For active issues, pulse the overlay color intensity using a sine wave in the fragment shader:

```glsl
// In fragment shader, add:
uniform float uTime;      // seconds since start
uniform int uPulseFace;   // face ID to pulse (-1 = none)

// In main():
if (uPulseFace >= 0 && abs(vFaceID - float(uPulseFace)) < 0.5) {
    float pulse = 0.7 + 0.3 * sin(uTime * 4.0);  // 4 Hz pulse
    col *= pulse;
}
```

The `uTime` uniform is set from `glfwGetTime()` each frame. The pulse face ID is set when the user hovers over a DFM issue in the report panel.

### Camera Fly-To

When the user clicks a DFM issue row in the panel, animate the camera to focus on the affected region:

```cpp
void flyToFace(std::vector<RenderObject>& objects, Camera& cam, int faceID) {
    // Find face centroid from tessellation data
    for (auto& obj : objects) {
        for (auto& fi : obj.tessData.faces) {
            if (fi.faceID == faceID) {
                // Compute centroid of the face's triangles
                float cx = 0, cy = 0, cz = 0;
                int count = 0;
                for (int i = fi.triangleOffset * 3;
                     i < (fi.triangleOffset + fi.triangleCount) * 3; i++) {
                    cx += obj.tessData.vertices[i].pos[0];
                    cy += obj.tessData.vertices[i].pos[1];
                    cz += obj.tessData.vertices[i].pos[2];
                    count++;
                }
                if (count > 0) { cx /= count; cy /= count; cz /= count; }

                // Set camera target to face centroid (animate over ~0.3s)
                cam.targetPanR = /* compute from centroid */;
                cam.targetPanU = /* compute from centroid */;
                cam.targetDist = /* close-up distance */;
                cam.animating = true;
                cam.animStart = glfwGetTime();
                cam.animDuration = 0.3;
                return;
            }
        }
    }
}
```

Camera animation uses linear interpolation (lerp) between current and target camera parameters, driven by elapsed time. The `Camera` struct gains animation state:

```cpp
struct Camera {
    // ... existing fields ...

    // Animation state
    bool animating = false;
    double animStart = 0.0;
    double animDuration = 0.3;  // seconds
    float targetPanR = 0.0f, targetPanU = 0.0f;
    float targetDist = 0.0f;
    float startPanR = 0.0f, startPanU = 0.0f;
    float startDist = 0.0f;
};
```

In the render loop:
```cpp
if (cam.animating) {
    double t = (glfwGetTime() - cam.animStart) / cam.animDuration;
    t = std::clamp(t, 0.0, 1.0);
    // Smooth step for ease-in-ease-out
    float s = (float)(t * t * (3.0 - 2.0 * t));
    cam.panR = cam.startPanR + s * (cam.targetPanR - cam.startPanR);
    cam.panU = cam.startPanU + s * (cam.targetPanU - cam.startPanU);
    cam.dist = cam.startDist + s * (cam.targetDist - cam.startDist);
    if (t >= 1.0) cam.animating = false;
}
```

---

## 9. ImGui Integration

### CMake FetchContent Setup

```cmake
# In CMakeLists.txt, before add_executable(opendcad_viewer ...):

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8  # pin to a specific release
)
FetchContent_MakeAvailable(imgui)

# Build ImGui as a static library with GLFW + OpenGL3 backends
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PRIVATE PkgConfig::GLFW glad)
# Tell ImGui backend to use glad loader
target_compile_definitions(imgui_lib PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

# Link to viewer
target_link_libraries(opendcad_viewer PRIVATE imgui_lib glad)
```

### Initialization

```cpp
// After creating GLFW window and initializing glad:

IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // enable docking

// Dark theme with CAD-appropriate adjustments
ImGui::StyleColorsDark();
ImGuiStyle& style = ImGui::GetStyle();
style.WindowRounding = 4.0f;
style.FrameRounding = 2.0f;
style.GrabRounding = 2.0f;
style.WindowBorderSize = 0.0f;
style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.95f);

ImGui_ImplGlfw_InitForOpenGL(win, true);  // installs GLFW callbacks
ImGui_ImplOpenGL3_Init("#version 330 core");
```

### Render Loop Integration

```cpp
// At the start of each frame:
ImGui_ImplOpenGL3_NewFrame();
ImGui_ImplGlfw_NewFrame();
ImGui::NewFrame();

// ... draw all UI panels ...

// At the end, after all GL rendering:
ImGui::Render();
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
```

### Input Handling

ImGui installs its own GLFW callbacks. To coexist with the existing camera callbacks, check `io.WantCaptureMouse` and `io.WantCaptureKeyboard`:

```cpp
// In mouse/key callbacks:
ImGuiIO& io = ImGui::GetIO();
if (io.WantCaptureMouse) return;    // ImGui is handling this click
if (io.WantCaptureKeyboard) return; // ImGui is handling this key
```

### Panel Layout

```
+------------------------------------------------------------------+
|  [Toolbar]  Shaded | Wire | Shaded+Edges | X-Ray  ||  Distance   |
+----------+-------------------------------------------+-----------+
|          |                                           |           |
| Part     |                                           | Proper-   |
| Tree     |           3D Viewport                     | ties      |
|          |                                           |           |
| [v] bin  |                                           | Object:   |
| [v] cut  |                                           |  Volume   |
|          |                                           |  Area     |
+----------+-------------------------------------------+-----------+
|  [DFM Report]  Errors(2) | Warnings(1) | Info(0)                |
|  Sev  | Rule              | Description          | Feature      |
|  ERR  | min_wall_thick    | Wall 0.8mm < 1.0mm   | [Go]         |
+------------------------------------------------------------------+
|  [Error Overlay - shown only when parse/geometry error exists]   |
+------------------------------------------------------------------+
```

### Panel Designs

#### Part Tree Panel (Left)

```cpp
void drawPartTreePanel(std::vector<RenderObject>& objects) {
    ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 300), ImGuiCond_FirstUseEver);

    ImGui::Begin("Part Tree");

    for (size_t i = 0; i < objects.size(); i++) {
        auto& obj = objects[i];

        // Visibility checkbox
        ImGui::Checkbox(("##vis" + std::to_string(i)).c_str(), &obj.visible);
        ImGui::SameLine();

        // Selectable name
        bool isSelected = obj.selected;
        if (ImGui::Selectable(obj.name.c_str(), &isSelected)) {
            // Deselect all others
            for (auto& o : objects) o.selected = false;
            obj.selected = true;
        }

        // Color picker on right-click
        if (ImGui::BeginPopupContextItem()) {
            ImGui::ColorEdit3("Color", obj.color);
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();
    ImGui::Text("Objects: %zu", objects.size());

    ImGui::End();
}
```

#### Properties Panel (Right)

```cpp
void drawPropertiesPanel(const RenderObject* selectedObj,
                         const TessFaceInfo* selectedFace,
                         const TessEdgeInfo* selectedEdge) {
    ImGui::SetNextWindowPos(ImVec2(screenW - 280, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(270, 350), ImGuiCond_FirstUseEver);

    ImGui::Begin("Properties");

    if (selectedObj) {
        ImGui::Text("Object: %s", selectedObj->name.c_str());
        ImGui::Separator();

        // Volume (computed via BRepGProp::VolumeProperties on brepShape)
        ImGui::Text("Volume: %.3f mm^3", computeVolume(selectedObj->brepShape));

        // Surface area
        ImGui::Text("Surface Area: %.3f mm^2",
                     computeSurfaceArea(selectedObj->brepShape));

        // Bounding box dimensions
        float dx = selectedObj->bboxMax[0] - selectedObj->bboxMin[0];
        float dy = selectedObj->bboxMax[1] - selectedObj->bboxMin[1];
        float dz = selectedObj->bboxMax[2] - selectedObj->bboxMin[2];
        ImGui::Text("Bounds: %.2f x %.2f x %.2f", dx, dy, dz);

        // Face count, edge count
        ImGui::Text("Faces: %zu", selectedObj->tessData.faces.size());
        ImGui::Text("Edges: %zu", selectedObj->tessData.edges.size());
        ImGui::Text("Triangles: %d", selectedObj->faceVertexCount / 3);
    }

    if (selectedFace) {
        ImGui::Separator();
        ImGui::Text("Selected Face #%d", selectedFace->faceID);
        ImGui::Text("Area: %.4f mm^2", selectedFace->area);
        ImGui::Text("Surface Type: %s",
                     surfaceTypeName(selectedFace->surfaceType));
        ImGui::Text("Triangles: %d", selectedFace->triangleCount);
    }

    if (selectedEdge) {
        ImGui::Separator();
        ImGui::Text("Selected Edge #%d", selectedEdge->edgeID);
        ImGui::Text("Length: %.4f mm", selectedEdge->length);
    }

    ImGui::End();
}
```

**Surface type name helper:**

```cpp
const char* surfaceTypeName(int geomAbsType) {
    switch (geomAbsType) {
        case 0: return "Plane";
        case 1: return "Cylinder";
        case 2: return "Cone";
        case 3: return "Sphere";
        case 4: return "Torus";
        case 5: return "BezierSurface";
        case 6: return "BSplineSurface";
        case 7: return "SurfaceOfRevolution";
        case 8: return "SurfaceOfExtrusion";
        case 9: return "OffsetSurface";
        default: return "Other";
    }
}
```

#### DFM Report Panel (Bottom)

```cpp
void drawDFMPanel(const std::vector<DFMIssue>& issues,
                  std::vector<RenderObject>& objects,
                  Camera& cam) {
    ImGui::SetNextWindowPos(ImVec2(10, screenH - 200), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(screenW - 20, 190), ImGuiCond_FirstUseEver);

    ImGui::Begin("DFM Report");

    // Severity filter
    static bool showErrors = true, showWarnings = true, showInfo = true;
    ImGui::Checkbox("Errors", &showErrors);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &showWarnings);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::Separator();

    // Issue table
    if (ImGui::BeginTable("dfm_issues", 4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY)) {

        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Feature", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (const auto& issue : issues) {
            if (issue.severity == DFMSeverity::Error && !showErrors) continue;
            if (issue.severity == DFMSeverity::Warning && !showWarnings) continue;
            if (issue.severity == DFMSeverity::Info && !showInfo) continue;

            ImGui::TableNextRow();

            // Severity with color
            ImGui::TableSetColumnIndex(0);
            ImVec4 sevColor = severityColor(issue.severity);
            ImGui::TextColored(sevColor, "%s", severityLabel(issue.severity));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", issue.ruleName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", issue.description.c_str());

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton(("Go##" + std::to_string(issue.faceID)).c_str())) {
                // Fly camera to the affected face
                flyToFace(objects, cam, issue.faceID);
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
```

#### Toolbar (Top)

```cpp
void drawToolbar(ViewMode& viewMode, MeasureMode& measureMode) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(screenW, 34));

    ImGui::Begin("Toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    // View mode buttons
    if (ImGui::RadioButton("Shaded", viewMode == ViewMode::Shaded))
        viewMode = ViewMode::Shaded;
    ImGui::SameLine();
    if (ImGui::RadioButton("Wire", viewMode == ViewMode::Wireframe))
        viewMode = ViewMode::Wireframe;
    ImGui::SameLine();
    if (ImGui::RadioButton("Shaded+Edges", viewMode == ViewMode::ShadedEdges))
        viewMode = ViewMode::ShadedEdges;
    ImGui::SameLine();
    if (ImGui::RadioButton("X-Ray", viewMode == ViewMode::XRay))
        viewMode = ViewMode::XRay;

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Measurement tools
    if (ImGui::Button("Distance"))
        measureMode = MeasureMode::PointToPoint_First;
    ImGui::SameLine();
    if (ImGui::Button("Area"))
        measureMode = MeasureMode::FaceArea;

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Preset views
    if (ImGui::Button("Front")) { /* set yaw=0, pitch=0 */ }
    ImGui::SameLine();
    if (ImGui::Button("Top")) { /* set yaw=0, pitch=pi/2 */ }
    ImGui::SameLine();
    if (ImGui::Button("Iso")) { /* set yaw=45deg, pitch=35.264deg */ }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) { /* fit to bounds */ }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("Screenshot")) { /* trigger screenshot */ }

    ImGui::End();
}
```

#### Error Overlay (Top Bar)

Shown only when a parse error or geometry error exists (during hot-reload). Positioned immediately below the toolbar.

```cpp
void drawErrorOverlay(const std::string& errorMsg, int errorLine) {
    if (errorMsg.empty()) return;

    ImGui::SetNextWindowPos(ImVec2(0, 34));
    ImGui::SetNextWindowSize(ImVec2(screenW, 28));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.7f, 0.1f, 0.1f, 0.95f));
    ImGui::Begin("Error", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

    if (errorLine > 0) {
        ImGui::Text("Error (line %d): %s", errorLine, errorMsg.c_str());
    } else {
        ImGui::Text("Error: %s", errorMsg.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
```

### Cleanup

```cpp
// Before glfwTerminate():
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
ImGui::DestroyContext();
```

### Gotchas

- **ImGui callback interception:** `ImGui_ImplGlfw_InitForOpenGL(win, true)` installs its own GLFW callbacks. If you also set callbacks with `glfwSetMouseButtonCallback` etc., you must call ImGui's callback handlers manually or use `ImGui_ImplGlfw_InitForOpenGL(win, false)` and chain callbacks yourself. The recommended approach is to let ImGui install callbacks and check `io.WantCaptureMouse` in your own logic.
- **DPI / Retina:** ImGui needs the correct content scale for crisp text on HiDPI. Call `glfwGetWindowContentScale(win, &xscale, &yscale)` and set `io.FontGlobalScale = xscale`. Load fonts at a higher resolution (e.g., 18px * scale) for crisp rendering.
- **Font loading:** The default ImGui font is very small. Load a better font:
  ```cpp
  io.Fonts->AddFontFromFileTTF("fonts/Inter-Regular.ttf", 16.0f * contentScale);
  ```
  Bundle the font file in the project or use the system font path.
- **Draw order:** ImGui rendering (`ImGui_ImplOpenGL3_RenderDrawData`) must be the **last** GL draw call before `glfwSwapBuffers`. It restores its own GL state, so it must not interfere with your 3D rendering.

---

## 10. Hot-Reload Integration

### FileWatcher

```cpp
// src/viewer/FileWatcher.h
#pragma once

#include <string>
#include <functional>
#include <ctime>

namespace opendcad {

class FileWatcher {
public:
    explicit FileWatcher(const std::string& path);

    // Check if the file has been modified since last check.
    // Call this every frame. Only stats the file, very cheap.
    bool poll();

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::timespec lastModTime_ = {};
};

} // namespace opendcad
```

**Implementation:**

```cpp
#include <sys/stat.h>

FileWatcher::FileWatcher(const std::string& path) : path_(path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
#ifdef __APPLE__
        lastModTime_ = st.st_mtimespec;
#else
        lastModTime_ = st.st_mtim;
#endif
    }
}

bool FileWatcher::poll() {
    struct stat st;
    if (stat(path_.c_str(), &st) != 0) return false;

#ifdef __APPLE__
    auto currentMod = st.st_mtimespec;
#else
    auto currentMod = st.st_mtim;
#endif

    if (currentMod.tv_sec != lastModTime_.tv_sec ||
        currentMod.tv_nsec != lastModTime_.tv_nsec) {
        lastModTime_ = currentMod;
        return true;
    }
    return false;
}
```

### Rebuild Pipeline

```cpp
// In the render loop:
if (fileWatcher_.poll()) {
    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        // 1. Re-parse the .dcad file
        std::string src = loadFile(dcadPath_);
        // ... run ANTLR, evaluate, get exports ...

        // 2. Re-tessellate each exported shape
        for (size_t i = 0; i < exports.size(); i++) {
            TessResult tess = Tessellator::tessellate(exports[i].shape->getShape());

            if (i < renderObjects_.size()) {
                // Update existing render object
                renderObjects_[i].destroy();  // free old GPU buffers
                renderObjects_[i].uploadToGPU(tess);
                renderObjects_[i].brepShape = exports[i].shape->getShape();
            } else {
                // New export, create render object
                RenderObject obj;
                obj.name = exports[i].name;
                obj.brepShape = exports[i].shape->getShape();
                obj.uploadToGPU(tess);
                renderObjects_.push_back(std::move(obj));
            }
        }

        // Remove extra render objects if exports decreased
        while (renderObjects_.size() > exports.size()) {
            renderObjects_.back().destroy();
            renderObjects_.pop_back();
        }

        // Recompute global bounding box
        recomputeGlobalBounds();

        auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
        double ms = std::chrono::duration<double, std::milli>(elapsed).count();
        statusMessage_ = "Rebuilt in " + std::to_string((int)ms) + " ms";
        errorMessage_.clear();

    } catch (const std::exception& e) {
        errorMessage_ = e.what();
        // Keep previous geometry visible
    }
}
```

### Design Decisions

- **Previous geometry stays visible during rebuild.** If the rebuild fails (parse error, geometry error), the error message is shown in the error overlay bar, but the last successful geometry remains rendered. The user sees what went wrong without losing visual context.
- **Grayed-out stale geometry.** When `hasError` is true, the main shader receives a desaturated object color (multiply by 0.3) to visually indicate that the displayed geometry is stale.
- **Debounce.** File saves often trigger multiple filesystem events (write + chmod + rename). The `stat()` approach naturally debounces because it checks the modification time, which only changes once per actual write.
- **Thread safety.** For Phase 5, rebuilding happens synchronously on the main thread (blocking the render loop for one frame). This is acceptable since tessellation of typical parts takes <100ms. Future optimization: move rebuild to a background thread, swap VBOs on completion.

### Gotchas

- **macOS `st_mtim` vs `st_mtimespec`:** On macOS, `struct stat` uses `st_mtimespec` instead of `st_mtim`. Use a platform ifdef as shown in the implementation above.
- **Editor temp files.** Some editors write to a temp file then rename. The watcher should track the canonical path. `stat()` follows symlinks, which is the desired behavior.

---

## 11. Screenshot / Export

### Screenshot Class

```cpp
// src/viewer/Screenshot.h
#pragma once

#include <string>
#include <functional>

namespace opendcad {

class Screenshot {
public:
    // Capture the current default framebuffer to a PNG file.
    // If highRes > 1, renders to an FBO at highRes * viewport size.
    static bool capture(const std::string& path,
                        int viewportW, int viewportH,
                        int highRes = 1);

    // Capture to a high-res FBO
    static bool captureHighRes(const std::string& path,
                               int viewportW, int viewportH,
                               int multiplier,
                               std::function<void(int, int)> renderFunc);
};

} // namespace opendcad
```

### Standard Resolution Capture

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

bool Screenshot::capture(const std::string& path, int w, int h, int highRes) {
    if (highRes > 1) {
        // Delegate to high-res capture
        return false; // see captureHighRes
    }

    std::vector<unsigned char> pixels(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL origin is bottom-left, PNG is top-left)
    std::vector<unsigned char> flipped(w * h * 3);
    for (int y = 0; y < h; y++) {
        std::memcpy(&flipped[y * w * 3],
                     &pixels[(h - 1 - y) * w * 3],
                     w * 3);
    }

    return stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3) != 0;
}
```

### High-Resolution Capture (2x/4x)

```cpp
bool Screenshot::captureHighRes(const std::string& path,
                                 int baseW, int baseH, int mult,
                                 std::function<void(int, int)> renderFunc) {
    int w = baseW * mult;
    int h = baseH * mult;

    // Check GPU limits
    GLint maxTexSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    if (w > maxTexSize || h > maxTexSize) {
        std::fprintf(stderr, "Screenshot size %dx%d exceeds GL_MAX_TEXTURE_SIZE %d\n",
                     w, h, maxTexSize);
        return false;
    }

    // Create temporary FBO at high resolution
    GLuint fbo, colorTex, depthRBO;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    glGenRenderbuffers(1, &depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &colorTex);
        glDeleteRenderbuffers(1, &depthRBO);
        glDeleteFramebuffers(1, &fbo);
        return false;
    }

    // Render at high resolution
    glViewport(0, 0, w, h);
    renderFunc(w, h);

    // Read pixels and save
    std::vector<unsigned char> pixels(w * h * 3);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip and write
    std::vector<unsigned char> flipped(w * h * 3);
    for (int y = 0; y < h; y++) {
        std::memcpy(&flipped[y * w * 3], &pixels[(h - 1 - y) * w * 3], w * 3);
    }
    bool ok = stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3) != 0;

    // Cleanup
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &colorTex);
    glDeleteRenderbuffers(1, &depthRBO);
    glDeleteFramebuffers(1, &fbo);

    return ok;
}
```

### Export Selected Object

Export the selected object directly from its `TopoDS_Shape`, bypassing any tessellation loss:

```cpp
void exportSelectedObject(const RenderObject& obj, const std::string& directory) {
    // STEP export
    std::string stepPath = directory + "/" + obj.name + ".step";
    StepExporter::write(obj.brepShape, stepPath);

    // STL export
    std::string stlPath = directory + "/" + obj.name + ".stl";
    StlExporter::write(obj.brepShape, stlPath, {0.01, 0.1, true});
}
```

### stb_image_write Integration

Add `stb_image_write.h` to the vendor directory. It is a single-header library (public domain). Define `STB_IMAGE_WRITE_IMPLEMENTATION` in exactly one `.cpp` file.

```cmake
# In CMakeLists.txt:
target_include_directories(opendcad_viewer PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
```

### Gotchas

- **MSAA FBO:** `glReadPixels` on an MSAA framebuffer reads resolved samples. For high-res screenshots, use a non-MSAA FBO or explicitly blit from MSAA to non-MSAA before reading.
- **Max texture size:** Check `GL_MAX_TEXTURE_SIZE` before creating the high-res FBO. On most GPUs this is 16384, so 4x of a 4K display (15360x8640) may exceed it. Cap the multiplier accordingly.
- **PNG file size:** High-res screenshots of complex models can be large (10-50 MB). Consider offering JPEG as an alternative for smaller files.

---

## 12. File Layout After Phase 5

```
src/
|-- main.cpp
|-- core/
|   |-- Error.h
|   |-- output.h / .cpp
|   |-- Timer.h / .cpp
|   +-- Debug.h
|-- geometry/
|   |-- Shape.h / .cpp
|   +-- ShapeRegistry.h / .cpp
|-- parser/
|   |-- Value.h / .cpp
|   |-- Environment.h / .cpp
|   +-- Evaluator.h / .cpp
|-- export/
|   |-- StepExporter.h / .cpp
|   |-- StlExporter.h / .cpp
|   +-- ShapeHealer.h / .cpp
+-- viewer/
    |-- ViewerApp.h / .cpp         # Main viewer class (replaces monolithic viewer.cpp)
    |-- Tessellator.h / .cpp       # BRep -> triangle/edge mesh
    |-- RenderObject.h / .cpp      # Per-object GPU resources + state
    |-- GridMesh.h / .cpp          # Ground grid + axis VBO/VAO
    |-- ShaderManager.h / .cpp     # Compile, link, cache shader programs
    |-- PickBuffer.h / .cpp        # Offscreen FBO for ID-buffer picking
    |-- ViewMode.h                 # ViewMode enum
    |-- Measurement.h / .cpp       # Measurement tools
    |-- DFMVisualization.h / .cpp  # DFM overlay logic
    |-- FileWatcher.h / .cpp       # File modification polling
    |-- Screenshot.h / .cpp        # Screenshot capture
    +-- Camera.h / .cpp            # Camera state + orbital math (extracted from viewer.cpp)

vendor/
|-- glad/
|   |-- include/glad/glad.h
|   |-- include/KHR/khrplatform.h
|   +-- src/glad.c
+-- stb/
    |-- stb_easy_font.h            # (removed -- replaced by ImGui)
    +-- stb_image_write.h
```

### ViewerApp Class

The monolithic `main()` in `viewer.cpp` is refactored into a class:

```cpp
// src/viewer/ViewerApp.h
namespace opendcad {

class ViewerApp {
public:
    int run(int argc, char** argv);

private:
    // Initialization
    bool initWindow();
    bool initGL();
    bool initImGui();
    bool loadScene(const std::string& dcadPath);

    // Per-frame
    void update();
    void render();
    void renderUI();

    // Cleanup
    void shutdown();

    // State
    GLFWwindow* window_ = nullptr;
    Camera camera_;
    ViewMode viewMode_ = ViewMode::ShadedEdges;
    std::vector<RenderObject> renderObjects_;
    GridMesh gridMesh_;
    PickBuffer pickBuffer_;
    FileWatcher fileWatcher_;
    MeasurementManager measurements_;

    // Shader programs
    GLuint mainShader_ = 0;
    GLuint edgeShader_ = 0;
    GLuint idShader_ = 0;
    GLuint gridShader_ = 0;

    // UI state
    std::string errorMessage_;
    std::string statusMessage_;
    MeasureMode measureMode_ = MeasureMode::None;
    std::vector<DFMIssue> dfmIssues_;
};

} // namespace opendcad
```

---

## 13. CMakeLists.txt Updates

### Full Viewer Target

```cmake
# --- GL function loader ---
add_library(glad STATIC vendor/glad/src/glad.c)
target_include_directories(glad PUBLIC vendor/glad/include)

# --- ImGui ---
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PRIVATE PkgConfig::GLFW glad)
target_compile_definitions(imgui_lib PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

# --- Viewer executable ---
add_executable(opendcad_viewer
    src/viewer/ViewerApp.cpp
    src/viewer/Tessellator.cpp
    src/viewer/RenderObject.cpp
    src/viewer/GridMesh.cpp
    src/viewer/ShaderManager.cpp
    src/viewer/PickBuffer.cpp
    src/viewer/Measurement.cpp
    src/viewer/DFMVisualization.cpp
    src/viewer/FileWatcher.cpp
    src/viewer/Screenshot.cpp
    src/viewer/Camera.cpp
)

target_include_directories(opendcad_viewer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/viewer
    ${CMAKE_SOURCE_DIR}/vendor
)

target_compile_features(opendcad_viewer PRIVATE cxx_std_20)
target_compile_options(opendcad_viewer PRIVATE -Wall -Wextra -Wpedantic)

# Link dependencies
if (APPLE)
    target_link_libraries(opendcad_viewer PRIVATE
        PkgConfig::GLFW glad imgui_lib
        "-framework OpenGL"
        "-framework Cocoa" "-framework IOKit" "-framework CoreVideo"
    )
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(opendcad_viewer PRIVATE
        PkgConfig::GLFW glad imgui_lib OpenGL::GL
    )
endif()

link_occt(opendcad_viewer)
```

### Additional OCCT Components for Viewer

Update the `link_occt` function or add viewer-specific components:

```cmake
# The viewer needs additional OCCT libraries for tessellation and properties
if(TARGET OpenCASCADE::TKMesh)
    target_link_libraries(opendcad_viewer PRIVATE
        OpenCASCADE::TKMesh       # BRepMesh_IncrementalMesh
        OpenCASCADE::TKGeomAlgo   # GeomAdaptor
    )
endif()
```

---

## 14. Implementation Order

The implementation proceeds in dependency order. Each step produces a compilable, testable state.

| Step | Component | Dependencies | Estimated Lines | Verification |
|------|-----------|-------------|----------------|--------------|
| 1 | **OpenGL 3.3 upgrade + modern shaders** | glad setup, GLFW hints, shader rewrite, remove all `glBegin`/`glEnd` | ~300 | Window opens, dark background, grid renders via VBO, existing mesh renders with new shaders |
| 2 | **Tessellator class** | OCCT headers, BRepMesh | ~250 | Unit test: tessellate a box, verify triangle count and face IDs |
| 3 | **Multi-object rendering** | Tessellator, RenderObject | ~200 | Load a script with 2+ exports, see distinct colored objects |
| 4 | **Edge rendering** | Tessellator (edge extraction), edge shader | ~100 | Edges visible as dark lines on all faces without z-fighting |
| 5 | **View modes** | Edge rendering, shaders (alpha uniform) | ~80 | Press 1-4 keys, see Shaded/Wire/Shaded+Edges/X-Ray |
| 6 | **ImGui integration** | FetchContent setup, glad | ~200 | Toolbar visible at top, empty panels on sides |
| 7 | **ID buffer picking** | PickBuffer FBO, ID shader | ~180 | Click a face, see it highlighted; console prints face ID |
| 8 | **Part tree + properties panels** | ImGui, picking, Tessellator metadata | ~250 | Left panel shows objects with toggles; right panel shows face area on click |
| 9 | **Hot-reload integration** | FileWatcher, rebuild pipeline | ~120 | Edit .dcad file, viewer updates geometry within 1 second |
| 10 | **Measurement tools** | Picking, BRepGProp, ImGui overlay | ~200 | Click two points, see distance line and label |
| 11 | **DFM visualization** | DFMIssue model, vertex color overlays, ImGui table | ~250 | Faces colored by severity, bottom panel lists issues |
| 12 | **Screenshot / export** | stb_image_write, high-res FBO | ~120 | Press P, get PNG file; Shift+P for 4x resolution |

**Total estimated new code: ~2,250 lines** (replacing the current 780-line monolith).

### Critical Path

```
Step 1 (GL 3.3) --> Step 2 (Tessellator) --> Step 3 (Multi-object)
                                          |-> Step 4 (Edges) --> Step 5 (View modes)
                                          +-> Step 7 (Picking) --> Step 10 (Measurement)
                                                               +-> Step 11 (DFM)
Step 6 (ImGui) --> Step 8 (Panels) --> Step 9 (Hot-reload)
                                   +-> Step 12 (Screenshot)
```

Steps 1-2 are prerequisites for everything. Step 6 (ImGui) is independent of steps 2-5 and can be done in parallel. Steps 7 and 4 are independent of each other. Steps 10, 11, 12 can be done in any order after their prerequisites.

### Risk Mitigation Per Step

| Step | Primary Risk | Mitigation |
|------|-------------|-----------|
| 1 | macOS GL 3.3 core profile incompatibilities | Test on macOS early; `GL_SILENCE_DEPRECATION`; stay at 3.3, not 4.x |
| 2 | OCCT version differences in `Poly_Triangulation` API | Check `OCCT_VERSION_HEX` for `HasNormals()` availability; provide fallback flat normals |
| 3 | VBO re-upload performance with many objects | Use `GL_STATIC_DRAW`; only re-upload on geometry change, not every frame |
| 6 | ImGui + GLFW callback conflicts | Check `io.WantCaptureMouse` before processing camera input |
| 7 | MSAA corrupts pick buffer IDs | Use separate non-MSAA FBO for pick buffer |
| 9 | Race condition on file save (partial writes) | `stat()` debounce + catch parse exceptions gracefully |
| 12 | Exceeding `GL_MAX_TEXTURE_SIZE` for high-res screenshots | Query limit and cap multiplier |
