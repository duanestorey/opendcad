# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenDCAD is a C++ CAD compiler that translates a custom DSL (.dcad files) into 3D geometry using OpenCascade (OCCT), outputting STEP and STL files. The language is imperative with C-style syntax, supporting functions, loops, conditionals, imports, lists, and color/material metadata.

Pipeline: **DSL source (.dcad) → ANTLR4 Lexer/Parser → Parse Tree → Evaluator (visitor) → Shape API → OCCT geometry → STEP/STL export**

## Build Commands

```bash
# Configure (Linux — OCCT from system packages)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Configure (macOS — OCCT from Homebrew)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix opencascade)"

# Build (skip viewer on Linux — has pre-existing GL errors)
cmake --build build --config Release --target opendcad opendcad_tests

# Run tests (270 tests, GoogleTest)
./build/bin/opendcad_tests

# Run a script (outputs to build/ by default)
./build/bin/opendcad examples/battery.dcad

# Common CLI options
./build/bin/opendcad model.dcad -o output/           # custom output dir
./build/bin/opendcad model.dcad --fmt step            # STEP+JSON only (no STL)
./build/bin/opendcad model.dcad -q draft              # fast preview quality
./build/bin/opendcad model.dcad -q production         # manufacturing quality
./build/bin/opendcad model.dcad -e lid                # export only "lid"
./build/bin/opendcad model.dcad --list-exports        # print export names
./build/bin/opendcad model.dcad --dry-run             # evaluate, write nothing
./build/bin/opendcad model.dcad --quiet               # suppress banner/info

# Clean rebuild (when grammar or CMake changes)
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target opendcad opendcad_tests
```

## Architecture

### Directory Structure

```
grammar/OpenDCAD.g4          — ANTLR4 grammar (single source of truth for syntax)
src/
  main.cpp                   — CLI entry point with argument parsing, export loop
  cli/
    CliOptions.h             — CLI argument parser, quality presets, CliOptions struct
  parser/
    Evaluator.h/.cpp         — Visitor that walks parse tree, evaluates to Values
    Value.h/.cpp             — 14 value types (NUMBER, STRING, BOOL, VECTOR, SHAPE, NIL,
                               FACE_REF, FACE_SELECTOR, WORKPLANE, SKETCH, EDGE_SELECTOR,
                               LIST, FUNCTION, COLOR, MATERIAL)
    Environment.h/.cpp       — Symbol table with parent chain (let/const/assign)
    FunctionDef.h            — User function struct (params, defaults, body, closure)
    Error.h                  — EvalError, GeometryError, SourceLoc
  geometry/
    Shape.h/.cpp             — Wrapper around TopoDS_Shape, fluent API (60+ methods)
    ShapeRegistry.h/.cpp     — Factory + typed method dispatch registry
    FaceRef.h/.cpp           — Single face reference with workplane
    FaceSelector.h/.cpp      — Multi-face selection and filtering
    Workplane.h/.cpp         — 2D coordinate system on a face
    Sketch.h/.cpp            — 2D profile builder (circle, rect, slot, freeform wire)
    EdgeSelector.h/.cpp      — Edge selection and filtering
    Color.h                  — Color (RGBA) and Material (PBR) structs
  export/
    StepExporter.h/.cpp      — STEP file output (with XDE color metadata)
    StlExporter.h/.cpp       — STL file output (returns StlResult with triangle count)
    ManifestExporter.h/.cpp  — JSON manifest output (with build metrics)
  viewer/viewer.cpp          — GLFW + OpenGL 3D viewer (standalone)
  core/
    output.h/.cpp            — ANSI terminal colors
    Timer.h/.cpp             — Performance timing
tests/                       — GoogleTest test files (270 tests across 12 suites)
examples/                    — Example .dcad scripts
  lib/                       — Library files for import
docs/plan/                   — Design documents and phase plans
```

### How the Grammar Works

`grammar/OpenDCAD.g4` defines both parser rules and lexer rules. CMake runs ANTLR4 to generate `OpenDCADLexer`, `OpenDCADParser`, `OpenDCADBaseVisitor` into `build/generated/antlr/`. The `Evaluator` extends `OpenDCADBaseVisitor` and overrides `visit*` methods for each grammar rule.

**Adding new syntax requires:**
1. Add parser/lexer rules to `grammar/OpenDCAD.g4`
2. Rebuild (CMake regenerates ANTLR files automatically)
3. Add `visit*` override in `Evaluator.h/.cpp`
4. Handle any new value types in `Value.h/.cpp`

**Grammar precedence** (ANTLR4 left-recursive expr rule — first = lowest precedence):
```
|| < && < ==,!= < <,>,<=,>= < +,- < *,/,% < !,- (unary) < [index] < primary
```

**Keywords** (defined BEFORE `IDENT` in lexer to prevent keyword-as-identifier):
`let`, `const`, `export`, `as`, `if`, `else`, `for`, `while`, `in`, `fn`, `return`, `import`, `true`, `false`, `nil`

### How Call Dispatch Works

When the evaluator encounters `name(args)`, it checks in order:
1. **Builtins** — `vec()`, `color()`, `material()`, `list()`, `range()`, `len()`, `print()`
2. **User functions** — lookup `name` in environment, check if it's a FUNCTION value
3. **Shape factories** — `box()`, `cylinder()`, etc. via ShapeRegistry

Method chains (`.method()`) dispatch via ShapeRegistry's typed method system:
1. Try `typedMethods_[valueType][methodName]` — works on any ValueType
2. Fallback to `methods_[methodName]` for legacy SHAPE methods

### How Imports Work

`import "file.dcad"` evaluates the file in the current environment. All `let`/`const`/`fn` definitions become available. Exports in imported files are suppressed (no file output). Circular imports detected via `importStack_`. Parse trees stored in `importedTrees_` to keep function body AST nodes alive.

### Export Model

`export shape as name;` names a shape as a script output. The CLI controls which formats are written and at what quality:

- **STEP + JSON always** — STEP is master geometry; JSON carries metadata (color, material, tags, build metrics)
- **STL optional** — on by default, skip with `--fmt step`
- **Quality presets** — `draft` (fast, no healing), `standard` (default), `production` (fine mesh, full healing)
- **Export filter** — `--export name` builds only named exports; default: all

### CLI Interface

```
opendcad <input.dcad> [options]

  -o, --output <dir>        Output directory (default: build/)
  -f, --fmt <formats>       step, stl, or step,stl (default: step,stl)
  -q, --qual <preset>       draft, standard, production (default: standard)
  -e, --export <name>       Build only this export (repeatable)
      --deflection <value>  Override STL mesh deflection
      --angle <value>       Override STL angular deflection
      --list-exports        Print export names, exit
      --dry-run             Evaluate, report exports, write nothing
      --quiet               Suppress banner and info
      --help / --version
```

## DSL Language Reference

See `docs/plan/language-reference.md` for the complete specification.

## Conventions

- **C++20** with strict warnings (`-Wall -Wextra -Wpedantic -O2`)
- All shapes use `ShapePtr` (`std::shared_ptr<Shape>`) — no raw pointers for geometry
- `enable_shared_from_this` requires `std::make_shared<T>(...)` everywhere
- Fluent API pattern — all Shape methods return `ShapePtr` for chaining
- Namespace: `opendcad`, ANTLR package: `OpenDCAD`
- Cast `ValueType` to `int` for `std::unordered_map` keys in typed method dispatch
- Forward declarations in Value.h for shared_ptr members; `.cpp` files need full includes

## Dependencies

| Library | Purpose |
|---------|---------|
| ANTLR4 (tool + C++ runtime) | DSL parsing, lexer/parser generation |
| OpenCascade (OCCT) | CAD kernel — solid modeling, boolean ops, export |
| GLFW3 + OpenGL | 3D viewer window and rendering |
| GoogleTest | Test framework (fetched via CMake FetchContent) |
| CMake 3.16+ | Build system |
