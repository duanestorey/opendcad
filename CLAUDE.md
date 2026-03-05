# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenDCAD is a C++ CAD compiler that translates an OpenSCAD-like DSL into 3D geometry using OpenCascade (OCCT), outputting STEP and STL files. The pipeline: **ANTLR4 parse → AST → OpenCascade geometry → STEP/STL export**.

## Build Commands

```bash
# Install dependencies (macOS)
brew install cmake antlr opencascade

# Configure (first time, or after CMakeLists.txt / grammar changes)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)"

# Build
cmake --build build --config Release

# Run
./build/bin/opendcad                # parser + geometry export
./build/bin/opendcad_viewer         # GLFW + OpenGL 3D viewer

# Clean rebuild (when grammar or CMake changes)
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)" && cmake --build build --config Release
```

No test framework is currently integrated. No linter configured.

## Architecture

### Pipeline

```
DSL source (.dcad) → ANTLR4 Lexer/Parser → AST → TraceVisitor → Shape API → OCCT geometry → STEP/STL
```

### Key Components

- **`grammar/OpenDCAD.g4`** — ANTLR4 grammar defining the DSL. Supports `let` bindings, `export` statements, method chains, arithmetic expressions, and vector literals. CMake auto-generates lexer/parser/visitor classes into `build/generated/antlr/`.

- **`src/Shape.h` / `src/Shape.cpp`** — Geometry wrapper around `TopoDS_Shape`. Fluent API with shared pointers (`ShapePtr`). Factory methods: `createBox`, `createCylinder`, `createTorus`, `createBin`. Operations: `fuse`, `cut`, `fillet`, `flip`, `translate`, `rotate`, `placeCorners`, and axis shortcuts `x()`, `y()`, `z()`.

- **`src/Listener.h`** — `TraceVisitor` class extending ANTLR-generated `OpenDCADBaseVisitor`. Walks the parse tree with depth-based indentation for debugging.

- **`src/opendcad.cpp`** — Main entry point. Loads `.dcad` scripts, runs the parser, builds geometry via Shape API, applies `ShapeFix_Shape`, exports to STEP and two STL files (rough: deflection=0.05, detailed: deflection=0.01).

- **`src/viewer.cpp`** — Standalone GLFW + OpenGL viewer for 3D visualization.

- **`src/output.h` / `src/output.cpp`** — ANSI color terminal output utilities.

- **`src/Timer.h` / `src/Timer.cpp`** — Performance timing singleton.

### DSL Example (`examples/battery.dcad`)

```
let bin = bin(80,120,20,2).fillet(0.5);
let battery = cylinder(21/2, 70).translate([0,0,-35]).rotate([0,90,0]).translate([0,0,12]);
export model as bin;
```

### Build Outputs

- `build/bin/opendcad` and `build/bin/opendcad_viewer` — executables
- `build/generated/antlr/` — ANTLR-generated C++ sources
- `build/bin/opendcad_test.step`, `opendcad_test_rough.stl`, `opendcad_test_detailed.stl` — geometry output

## Conventions

- **C++20** with strict warnings (`-Wall -Wextra -Wpedantic -O3`)
- All shapes use `ShapePtr` (`std::shared_ptr<Shape>`) — no raw pointers for geometry
- Fluent API pattern — all Shape methods return `ShapePtr` for chaining
- Namespace: `opendcad`
- ANTLR-generated code uses the `OpenDCAD` package name
- Source files use mixed case directory (`src/` and `Src/` both appear in CMakeLists.txt — note the capitalization inconsistency for `Src/Timer.cpp` and `Src/viewer.cpp`)

## Dependencies

| Library | Purpose |
|---------|---------|
| ANTLR4 (tool + C++ runtime) | DSL parsing, lexer/parser generation |
| OpenCascade (OCCT) | CAD kernel — solid modeling, boolean ops, export |
| GLFW3 + OpenGL | 3D viewer window and rendering |
| CMake 3.16+ | Build system |
