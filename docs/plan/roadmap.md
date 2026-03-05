# OpenDCAD Roadmap

## Completed

### Phase 0: Core Architecture
Parser pipeline (ANTLR4 → Evaluator → Value/Environment), Shape API wrapper around OCCT.

### Phase 1: Shape Primitives & Operations
box, cylinder, sphere, cone, wedge, torus, bin, circle, rectangle, polygon, loft.
Boolean ops (fuse/cut/intersect), fillet, chamfer, transforms, scale, mirror, shell.
Import STEP/STL files.

### Phase 2: Face-Relative Modeling
FaceRef, FaceSelector, Workplane, Sketch, EdgeSelector.
Face selection by direction (`face(">Z")`), face filters (planar, cylindrical, area, nearest/farthest).
Sketch operations (circle, rect, slot, freeform wire, fillet2D, chamfer2D, offset).
Sketch → Shape (extrude, cutBlind, cutThrough, revolve).
Parametric holes (hole, throughHole, counterbore, countersink).
Edge selection and filtering (vertical, horizontal, parallelTo, longest/shortest, ofFace).

### Phase 2b: Advanced Modeling
Feature patterns (linearPattern, circularPattern, mirrorFeature).
Draft angles, splitAt, sweep.

### Language Expansion (Phases 1-8)
Full imperative language: `let`/`const`, assignment, compound assignment (`+=`), increment (`++`),
comparison and logical operators, `if`/`else`, `for`/`while`, lists, `vec()`, functions with
default/named args, `return`, closures, `import`, color/material types, built-in stdlib
(17 colors, 17 materials).

---

## Next: Phase 9 — CLI Refactor & Export Control

**Goal:** Separate geometry evaluation from file output. The evaluator produces named exports; the CLI decides what to write.

### 9.1 CLI Argument Parsing
- `opendcad <input.dcad> [options]`
- `--output <dir>` — output directory (default: `.`)
- `--format step,stl,both` — output format(s) (default: `both`)
- `--quality draft|standard|production` — mesh quality presets
  - `draft`: STL deflection=0.1, no healing
  - `standard`: STL deflection=0.01, healing (current behavior)
  - `production`: STL deflection=0.001, full healing, fine tessellation
- `--export <name>` — build only a specific export (can repeat)
- `--list-exports` — print export names and exit (no file output)
- `--dry-run` — evaluate script, report exports, but write nothing

### 9.2 Decouple Evaluator from File I/O
- `main.cpp` currently hardcodes STEP+STL for every export
- Refactor: evaluator returns `vector<ExportEntry>` (already does), main applies CLI filters and writes requested formats
- Export entries carry color/material metadata for STEP color embedding

### 9.3 STEP Color Embedding
- OCCT supports per-shape colors in STEP via `XCAFDoc_ColorTool`
- When shapes have `.color()` metadata, embed it in STEP output
- This makes imported STEP files render correctly in FreeCAD/Fusion360

**Files:** `src/main.cpp`, `src/export/StepExporter.cpp`, `src/export/StlExporter.cpp`

---

## Phase 10 — 3D Viewer Overhaul

**Goal:** Production-quality viewer with PBR rendering, multi-object display, and layer system.

### 10.1 OpenGL 3.3+ Core Profile
- Replace GL 2.1 immediate mode with modern VAO/VBO pipeline
- GLSL 330 shaders (vertex + fragment)
- glad loader for cross-platform GL function loading

### 10.2 PBR Rendering
- Physically-based shading (metallic/roughness workflow)
- Use material metadata from shapes (STEEL → metallic=0.9, roughness=0.3)
- Environment mapping for reflections
- Multiple light sources
- Ambient occlusion (SSAO)
- Optional: ray-traced shadows (if GPU supports compute shaders)

### 10.3 Direct BRep Tessellation
- Tessellate directly from OCCT shapes (skip STL round-trip)
- Per-face colors from shape metadata
- Edge wireframe overlay (toggleable)
- Face ID buffer for picking/selection

### 10.4 Multi-Object Rendering
- Render each `export` as a separate object
- Object list panel (show/hide, select, rename)
- Per-object transform gizmo

### 10.5 Layer System
- `layer("name")` — DSL construct to assign shapes to named layers
- Example usage:
  ```dcad
  let case = box(80, 60, 20).layer("plastic").material(ABS_BLACK);
  let insert = cylinder(3, 8).layer("metal").material(BRASS);
  let pcb = box(70, 50, 1.6).layer("pcb").color(GREEN);
  let usb = box(12, 7, 5).layer("connectors").color(GREY);
  ```
- Viewer layer panel: toggle visibility per layer, adjust opacity
- Layer colors/icons for quick identification
- Presets: "All layers", "Printable only", "Electronics only"
- Similar to KiCad's layer system but for 3D assemblies

### 10.6 UI Framework (ImGui)
- Replace raw GLFW callbacks with Dear ImGui panels
- Object tree, layer panel, material inspector
- Measurement tools (point-to-point, face area, volume)
- Screenshot and viewport export (PNG, HDR)

### 10.7 Hot Reload
- Watch `.dcad` file for changes (inotify/kqueue/polling)
- Re-evaluate on save, update viewport
- Preserve camera position across reloads

**Files:** `src/viewer/` (major rewrite), new `src/viewer/renderer/`, `src/viewer/ui/`
**Dependencies:** glad, Dear ImGui, stb_image (for environment maps)

---

## Phase 11 — 2D Drawing Generation

**Goal:** Generate dimensioned 2D profile drawings from 3D models for manufacturing documentation.

### 11.1 Orthographic Projection
- Project 3D shape onto 2D planes (front, top, right, isometric)
- Hidden line removal (OCCT's `HLRBRep` algorithm)
- Output as SVG or DXF

### 11.2 Automatic Dimensioning
- Detect and annotate key dimensions (overall size, hole positions, radii)
- Leader lines, dimension arrows, tolerance annotations
- Configurable dimension style (ANSI, ISO, custom)

### 11.3 Section Views
- Cross-section at specified planes
- Hatching for cut surfaces
- Section labels (A-A, B-B)

### 11.4 Drawing DSL Integration
- `drawing()` builtin or export option
- Example:
  ```dcad
  export model as bracket;
  // CLI: opendcad bracket.dcad --format drawing --views front,top,right
  ```
- Or in-script:
  ```dcad
  let dwg = drawing(model, views=["front", "top", "right", "iso"]);
  export dwg as bracket_drawing;
  ```

### 11.5 Output Formats
- SVG (for web/documentation)
- DXF (for CAM/CNC)
- PDF (for print)

**Files:** new `src/export/DrawingExporter.h/.cpp`, `src/geometry/Projection.h/.cpp`
**OCCT APIs:** `HLRBRep_Algo`, `HLRBRep_HLRToShape`, `BRepProj_Projection`

---

## Phase 12 — DFM Analysis

**Goal:** Automated design-for-manufacturing checks that flag issues before export.

### 12.1 Process Profiles
Define manufacturing constraints as data (not grammar extensions yet — keep it simple):

```dcad
const fdm_profile = dfm_profile("FDM",
    min_wall=0.8, max_overhang=45, min_radius=0.4, layer_height=0.2);

const injection_profile = dfm_profile("Injection Molding",
    min_wall=0.8, max_wall=4.0, min_draft=1.0, min_radius=0.25);
```

### 12.2 DFM Check Functions
- `dfm_check(shape, profile)` → returns a list of DFM issues
- Each issue: location (face/edge), severity (warning/error), description
- Checks:
  - **Wall thickness** — find thin walls below process minimum
  - **Draft angle** — check faces for demoldability (injection molding)
  - **Minimum radius** — flag sharp internal edges
  - **Overhang angle** — identify unsupported overhangs (FDM)
  - **Aspect ratio** — tall thin features that may warp
  - **Hole depth ratio** — deep narrow holes that are hard to machine

### 12.3 DFM Report Output
- Console output with colored warnings/errors and face/edge locations
- JSON output for programmatic consumption
- Viewer overlay: highlight flagged faces/edges in red/yellow

### 12.4 DFM in Build Pipeline
- `opendcad model.dcad --dfm fdm` — run DFM checks after evaluation
- Exit code reflects DFM severity (0=pass, 1=warnings, 2=errors)
- CI/CD integration: fail build on DFM errors

**Files:** new `src/analysis/DFMProfile.h`, `src/analysis/DFMAnalyzer.h/.cpp`
**OCCT APIs:** `BRepGProp`, `BRepAdaptor_Surface`, `TopExp_Explorer`

---

## Phase 13 — Standard Parts Library

**Goal:** Ship a library of parametric mechanical/electronic components as `.dcad` files.

### 13.1 Fasteners (`lib/std/fasteners/`)
- `m2_bolt(length)` through `m10_bolt(length)` — hex socket cap screws
- `m2_nut()` through `m10_nut()` — hex nuts
- `m2_washer()` through `m10_washer()` — flat washers
- `m3_standoff(height, thread_length)` — PCB standoffs
- `m3_insert(length)` — heat-set inserts for 3D printing

### 13.2 Connectors (`lib/std/connectors/`)
- `usb_c_cutout(tolerance=0.2)` — USB-C port opening
- `usb_a_cutout(tolerance=0.2)` — USB-A port opening
- `barrel_jack_cutout(od=5.5, id=2.1)` — DC barrel jack
- `rj45_cutout()` — Ethernet port
- `hdmi_cutout(type="standard")` — HDMI (standard/mini/micro)
- `audio_jack_cutout()` — 3.5mm audio

### 13.3 Electronics (`lib/std/electronics/`)
- `pcb(width, depth, thickness=1.6)` — basic PCB shape
- `raspberry_pi_4()` — RPi4 board outline with mounting holes
- `arduino_uno()` — Arduino Uno board outline
- `esp32_devkit()` — ESP32 DevKit board outline

### 13.4 Enclosure Helpers (`lib/std/enclosure/`)
- `enclosure(width, depth, height, wall, split="horizontal")` — parametric enclosure
- `snap_fit(wall_thickness)` — snap-fit clip geometry
- `screw_boss(screw_dia, boss_dia, height)` — screw mounting boss
- `vent_pattern(width, height, slot_width, slot_gap, count)` — ventilation slots

### 13.5 Library Distribution
- Ship as `.dcad` files in a `lib/std/` directory alongside the binary
- Import via: `import "std/fasteners/m3.dcad";` or search path
- Community libraries via git repos (future: package manager)

---

## Phase 14 — AI Integration & Developer Experience

**Goal:** Make OpenDCAD the best CAD tool for AI-assisted design workflows.

### 14.1 Structured Error Output
- `--output-format json` — machine-readable errors, warnings, export info
- Error messages include source location, expected types, suggestions
- DFM results as structured JSON

### 14.2 Language Server Protocol (LSP)
- Syntax highlighting, completion, hover docs
- Go-to-definition for functions/imports
- Inline DFM warnings
- VS Code extension

### 14.3 AI Prompt Templates
- Ship example prompts that work with Claude/GPT for generating .dcad
- Include language-reference.md in prompt context
- Example: "Design an enclosure for a Raspberry Pi 4 with USB-C and HDMI cutouts, ventilation, and snap-fit lid"

### 14.4 Watch Mode + REPL
- `opendcad --watch model.dcad` — rebuild on file change
- `opendcad --repl` — interactive mode for exploration
- Pipe-friendly: `echo 'export box(10,10,10) as test;' | opendcad --stdin --format stl > test.stl`

---

## Priority Order

| Priority | Phase | Rationale |
|----------|-------|-----------|
| 1 | **Phase 9: CLI Refactor** | Foundation for everything else — all other phases need CLI flexibility |
| 2 | **Phase 12: DFM Analysis** | High-impact differentiator, no open-source CAD tool does this well |
| 3 | **Phase 10: Viewer Overhaul** | Visual impact, PBR + layers makes the tool compelling to share |
| 4 | **Phase 11: 2D Drawings** | Manufacturing workflow completeness |
| 5 | **Phase 13: Standard Library** | Community growth, practical utility |
| 6 | **Phase 14: AI/DX** | Multiplier on all other features |

Each phase is independently valuable and shippable. No phase blocks another (except Phase 9 which should go first since it's small and foundational).
