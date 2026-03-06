# Viewer Overhaul Design

**Date:** 2026-03-06
**Status:** Approved
**Replaces:** docs/plan/phase-5-viewer.md (original prototype plan)

## Goal

Replace the 780-line prototype viewer with a production-quality 3D CAD viewer featuring PBR rendering, SSAO, per-shape colors and materials from metadata, layer system via tags, ImGui docking UI, hot-reload, and screenshot export. The viewer must produce screenshot-quality renders that sell the product.

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Graphics API | OpenGL 4.1 core profile | Max supported on macOS, universal on Linux, proven ecosystem |
| GL loader | glad | Single-file, cross-platform, public domain |
| UI framework | Dear ImGui (docking branch) | Professional dockable panels, excellent OpenGL backend |
| Shading model | PBR (Cook-Torrance BRDF) | Physically accurate materials, screenshot quality |
| Ambient occlusion | SSAO | Soft contact shadows, modern look |
| Geometry source | Direct BRep tessellation | Preserves face IDs, edge topology, per-face colors — no STL round-trip |
| Metadata source | Shape objects (direct eval) or JSON sidecar (STEP loading) | JSON manifest already has color, material, tags |
| Layer system | Tags as layers | No language changes needed, shapes already support tags |
| Integration | `opendcad view` subcommand | Integrated evaluate + view + watch workflow |
| Primary platform | macOS (Linux supported) | Screenshots from macOS |

## Architecture

```
opendcad view model.dcad
     |
     +-- Evaluate .dcad script (existing Evaluator)
     |    +-- Produces: vector<ExportEntry> with ShapePtr + Color + Material + Tags
     |
     +-- Tessellate BRep shapes (new Tessellator)
     |    +-- TopoDS_Shape -> TessResult (face vertices, normals, faceIDs, edge polylines)
     |
     +-- Load into GPU (new RenderScene)
     |    +-- Per-object VAO/VBO for faces + edges, with per-face color/material
     |
     +-- Render loop (new Renderer)
     |    +-- G-Buffer pass (albedo, normals, position, metallic/roughness, faceID)
     |    +-- SSAO pass (screen-space ambient occlusion from depth + normals)
     |    +-- PBR lighting pass (Cook-Torrance + IBL + tone mapping)
     |    +-- Edge overlay pass (depth-biased lines)
     |    +-- ImGui UI pass (panels, overlays)
     |
     +-- File watcher (hot-reload)
          +-- kqueue(macOS)/inotify(Linux)/polling fallback
          +-- re-evaluate -> re-tessellate -> update GPU buffers
          +-- preserve camera position, panel state, layer visibility
```

### Directory Structure

```
src/viewer/
  ViewerApp.h/.cpp         -- main application class, owns window + render loop
  Renderer.h/.cpp          -- PBR render pipeline, shader management, G-Buffer
  Tessellator.h/.cpp       -- BRep -> GPU mesh conversion
  RenderScene.h/.cpp       -- collection of RenderObjects, GPU resource management
  RenderObject.h           -- per-export GPU resources + metadata
  Camera.h/.cpp            -- orbital camera (port existing zoom-to-cursor logic)
  ShaderProgram.h/.cpp     -- shader compile/link/uniform helpers
  GridMesh.h/.cpp          -- grid + axes VBO (core profile, no immediate mode)
  EnvironmentMap.h/.cpp    -- HDR environment map, IBL precomputation (irradiance + prefiltered)
  FileWatcher.h/.cpp       -- cross-platform file change detection
  Screenshot.h/.cpp        -- viewport capture to PNG
  ui/
    ImGuiSetup.h/.cpp      -- ImGui initialization, docking setup, render
    ObjectPanel.h/.cpp     -- object tree (show/hide, select, rename)
    LayerPanel.h/.cpp      -- layer toggles (derived from shape tags)
    MaterialPanel.h/.cpp   -- material/color inspector for selected object
    PropertiesPanel.h/.cpp -- geometry stats (faces, edges, volume, area)
    ViewportOverlay.h/.cpp -- status bar (triangle count, quality, file status)
    MenuBar.h/.cpp         -- File/View/Render/Help menus
```

## STEP Loading + Metadata

### Path 1: Direct evaluation (`opendcad view model.dcad`)

The evaluator produces `ExportEntry` objects containing `ShapePtr` with Color, Material, and Tags already attached. The viewer tessellates these shapes directly -- no file I/O needed for geometry or metadata.

### Path 2: STEP file viewing (`opendcad view build/model.step`)

1. Load STEP via `STEPCAFControl_Reader` (preserves XDE color metadata + assembly structure)
2. Load companion JSON sidecar (`model.json`) for materials, tags, and full color data
3. STEP contains geometry + XDE colors. JSON adds: material (metallic, roughness, preset), tags, build metrics

The JSON manifest is already produced by the build pipeline and contains:
```json
{
  "shapes": [
    {
      "index": 0,
      "color": { "r": 0.1, "g": 0.1, "b": 0.1, "a": 1 },
      "material": { "preset": "ABS_BLACK", "metallic": 0, "roughness": 0.6 },
      "tags": ["plastic"]
    }
  ]
}
```

## BRep Tessellation

Converts each shape's BRep to GPU-ready data using OCCT's `BRepMesh_IncrementalMesh`.

### Face Vertex Layout (40 bytes per vertex)

| Offset | Component | Type | Shader Location |
|--------|-----------|------|-----------------|
| 0 | position (xyz) | vec3 | 0 |
| 12 | normal (xyz) | vec3 | 1 |
| 24 | faceID | float | 2 |
| 28 | color (rgb) | vec3 | 3 |
| 40 | metallic, roughness | vec2 | 4 |
| **48** | | | **Stride** |

### Edge Vertex Layout (16 bytes per vertex)

| Offset | Component | Type | Shader Location |
|--------|-----------|------|-----------------|
| 0 | position (xyz) | vec3 | 0 |
| 12 | edgeID | float | 1 |
| **16** | | | **Stride** |

### Key implementation details

- OCCT indexing is 1-based (Triangle/Node arrays)
- Face orientation: check `TopAbs_REVERSED`, swap winding
- Apply `TopLoc_Location` transform to all node positions
- Use per-vertex normals when `tri->HasNormals()` (OCCT 7.6+), else compute flat normals
- Use `TopTools_IndexedMapOfShape` for unique edges (avoid TopExp_Explorer duplicates)
- `Poly_Polygon3D` for standalone edge polylines, fall back to `PolygonOnTriangulation` for edges only stored relative to face triangulations

## PBR Rendering Pipeline

### G-Buffer Pass

Renders all objects to multiple render targets (MRT):

| RT | Format | Content |
|----|--------|---------|
| RT0 | RGBA16F | Albedo (RGB) + Alpha |
| RT1 | RGBA16F | World Normal (RGB) + Metallic |
| RT2 | RGBA16F | World Position (RGB) + Roughness |
| RT3 | R32F | Face ID (for picking) |
| Depth | DEPTH24_STENCIL8 | Depth buffer |

~30MB VRAM at 1080p. Trivial for any modern GPU.

### SSAO Pass

Screen-space ambient occlusion computed from G-Buffer depth + normals:
- 64-sample hemisphere kernel
- 4x4 random rotation texture to reduce banding
- Bilateral blur to smooth noise
- Outputs single-channel AO texture

### PBR Lighting Pass (Full-screen quad)

Cook-Torrance BRDF reading from G-Buffer:

**Diffuse:** Lambertian * (1 - metallic) * albedo
**Specular:** GGX normal distribution * Smith geometry * Fresnel-Schlick
**IBL:** Pre-filtered environment map (specular reflections) + irradiance map (diffuse)
**AO:** SSAO texture modulates ambient
**Lights:** Three directional lights (key, fill, rim) for studio lighting
**Output:** HDR color -> ACES filmic tone mapping -> sRGB gamma correction

### Edge Overlay Pass

Forward-rendered lines over the composited image:
- Depth bias via `gl_Position.z += bias * gl_Position.w` to prevent z-fighting
- Dark grey default color, highlight color for selected edges
- MSAA 4x for anti-aliasing

### Environment Map

Ships with a default HDR studio environment (neutral, professional lighting).
Loaded via `stb_image.h` (HDR format).
Pre-computed at startup:
- **Irradiance map** (32x32 cubemap, diffuse IBL)
- **Pre-filtered map** (128x128 cubemap, 5 mip levels for roughness)
- **BRDF LUT** (512x512, pre-integrated BRDF lookup)

### Material Defaults

Shapes without explicit materials get: light blue-grey (0.72, 0.78, 0.86), metallic=0.0, roughness=0.5. Matches the current viewer's pleasant default but with proper PBR lighting.

Material presets from the DSL stdlib (STEEL, ALUMINUM, BRASS, ABS_BLACK, etc.) already set metallic/roughness/baseColor correctly. The viewer reads these values directly from the shape -- no hardcoded mapping needed.

## ImGui UI Layout

```
+------------------------------------------------------------------+
|  Menu: File | View | Render | Help                               |
+-------------+----------------------------------+-----------------+
| Objects     |                                  | Properties      |
| ----------- |                                  | ----------      |
| > model     |                                  | Name: bracket   |
|   [x] bracket|       3D Viewport              | Faces: 42       |
|   [x] lid    |                                 | Edges: 68       |
|   [x] insert |    (PBR rendered scene)         | Volume: 12.3ml  |
|              |                                  |                 |
| Layers      |                                  | Material        |
| ----------- |                                  | ----------      |
| [x] plastic |                                  | Preset: ABS     |
| [x] metal   |                                  | Metallic: 0.0   |
| [x] pcb     |                                  | Roughness: 0.6  |
| [ ] connectors|                                | Color: #1A1A1A  |
+-------------+----------------------------------+-----------------+
| > Tris: 8,432 | Quality: standard | model.dcad (watching)       |
+------------------------------------------------------------------+
```

### Object Panel
- Tree of export entries
- Checkbox: toggle visibility
- Click: select (highlights in viewport, populates Properties/Material panels)
- Right-click: focus camera on object

### Layer Panel
- Derived from shape tags (each unique tag = one layer)
- Checkbox: toggle visibility for all shapes with that tag
- Layer colors auto-assigned for visual distinction

### Properties Panel
- Shows selected object's geometry stats
- Face count, edge count, vertex count
- Volume (BRepGProp::VolumeProperties)
- Surface area (BRepGProp::SurfaceProperties)
- Bounding box dimensions

### Material Panel
- Shows selected object's material metadata
- Preset name, metallic, roughness sliders (read-only, from metadata)
- Color swatch
- Preview sphere with the material applied (optional)

### Menu Bar
- **File:** Open STEP, Screenshot (PNG), Close
- **View:** Reset camera, Fit all, Isometric, Front/Top/Right views, Toggle grid, Toggle edges
- **Render:** Quality preset (draft/standard/production), Wireframe/Shaded/X-ray modes
- **Help:** Keyboard shortcuts, About

## Hot Reload

File watcher monitors the `.dcad` source file and all imported files.

**macOS:** kqueue (native, no dependencies)
**Linux:** inotify (native, no dependencies)
**Fallback:** stat() polling every 500ms

On file change:
1. Re-evaluate the script (same Evaluator pipeline, fresh environment)
2. Compare export names/shapes — only re-tessellate changed shapes
3. Update GPU buffers (delete old VAO/VBO, create new)
4. Preserve: camera position, zoom, pan, panel state, layer visibility, selected object
5. Status bar shows "Reloading..." during update, then "Watching" when done

Error handling: if re-evaluation fails (syntax error, runtime error), show the error in the status bar / a toast notification and keep the last good state rendered.

## Screenshot Export

**Keyboard shortcut:** Ctrl+S (or Cmd+S on macOS)
**Menu:** File > Screenshot

Captures the current viewport (without ImGui panels) to a PNG file:
1. Render the scene to an offscreen FBO at the viewport resolution
2. Read pixels with `glReadPixels`
3. Write PNG via `stb_image_write.h` (single-header, vendored)
4. Default filename: `<export-name>_screenshot_<timestamp>.png`
5. Save to the output directory (or prompt via file dialog)

Optional: high-resolution screenshot (2x or 4x viewport resolution) for marketing materials.

## CLI Integration

```bash
opendcad view model.dcad              # evaluate + view + watch for changes
opendcad view model.dcad -q draft     # fast preview quality
opendcad view model.dcad -q production # high quality for screenshots
opendcad view build/model.step        # open existing STEP file (+ companion .json)
```

The `view` subcommand:
- Reuses existing Evaluator, Environment, ShapeRegistry, CliOptions
- For .dcad input: evaluate in-process, pass shapes directly to tessellator
- For .step input: load via STEPCAFControl_Reader, read companion .json for materials/tags
- Quality preset controls tessellation density (same deflection/angle params as CLI)
- `--quiet` suppresses console output during hot-reload cycles

## Dependencies

| Library | Files | Purpose | License |
|---------|-------|---------|---------|
| glad | 2 files (glad.c, glad.h + khrplatform.h) | OpenGL 4.1 core function loader | Public domain |
| Dear ImGui (docking) | ~15 files | UI panels, docking layout | MIT |
| stb_image.h | 1 file | HDR environment map loading | Public domain |
| stb_image_write.h | 1 file | PNG screenshot export | Public domain |

All vendored in `vendor/` directory. No package manager needed.

### CMake Changes

```cmake
# vendor/glad
add_library(glad STATIC vendor/glad/src/glad.c)
target_include_directories(glad PUBLIC vendor/glad/include)

# vendor/imgui (docking branch)
file(GLOB IMGUI_SOURCES vendor/imgui/*.cpp vendor/imgui/backends/imgui_impl_glfw.cpp
     vendor/imgui/backends/imgui_impl_opengl3.cpp)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC vendor/imgui vendor/imgui/backends)
target_link_libraries(imgui PRIVATE PkgConfig::GLFW)

# Viewer executable
add_executable(opendcad_viewer
  src/viewer/ViewerApp.cpp
  src/viewer/Renderer.cpp
  src/viewer/Tessellator.cpp
  src/viewer/RenderScene.cpp
  src/viewer/Camera.cpp
  src/viewer/ShaderProgram.cpp
  src/viewer/GridMesh.cpp
  src/viewer/EnvironmentMap.cpp
  src/viewer/FileWatcher.cpp
  src/viewer/Screenshot.cpp
  src/viewer/ui/ImGuiSetup.cpp
  src/viewer/ui/ObjectPanel.cpp
  src/viewer/ui/LayerPanel.cpp
  src/viewer/ui/MaterialPanel.cpp
  src/viewer/ui/PropertiesPanel.cpp
  src/viewer/ui/ViewportOverlay.cpp
  src/viewer/ui/MenuBar.cpp
  ${OPENDCAD_LIB_SOURCES}  # shared with main executable
)
target_link_libraries(opendcad_viewer PRIVATE glad imgui antlr4_shared)
link_occt(opendcad_viewer)
# + TKMesh for BRepMesh_IncrementalMesh
```

## Camera

Port the existing orbital camera (it's well-implemented):
- Z-up spherical coordinates (yaw, pitch, distance)
- Camera-space pan (right/up vectors)
- Zoom-to-cursor via scroll (preserves world point under cursor)
- Keyboard: F=fit, Space=reset, I=isometric
- Add: numpad views (1=front, 3=right, 7=top), Ctrl+S=screenshot

## OpenGL Upgrade Notes

### Removed (GL 2.1 compatibility, no longer used)
- `glBegin`/`glEnd` (immediate mode)
- `glPushAttrib`/`glPopAttrib`
- `glMatrixMode`/`glLoadIdentity`/`glMultMatrixf`
- `glDisableClientState`/`glEnableClientState`
- `attribute`/`varying` in GLSL
- `gl_FragColor` in GLSL
- `stb_easy_font.h`
- Triple `glfwSwapBuffers` bug

### Added (GL 4.1 core profile)
- VAO required for all draw calls
- `layout(location = N)` in GLSL
- `in`/`out` instead of `attribute`/`varying`
- `out vec4 FragColor` instead of `gl_FragColor`
- MRT (multiple render targets) for G-Buffer
- Framebuffer objects for SSAO + offscreen rendering
- All matrix math CPU-side, passed as uniforms
