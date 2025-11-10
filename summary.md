# 🧱 OpenDCAD — High-Level Project Summary

## 🚀 Goal
**OpenDCAD** is a modern C++ CAD compiler that takes an **OpenSCAD-like DSL** and outputs **STEP** and **STL** solids using **OpenCascade**, with a clean, extensible parser built using **ANTLR4**.

---

## ⚙️ Core Architecture

| Component | Purpose | Tech Used |
|------------|----------|-----------|
| **Parser** | Parses DSL like `Box(w:60,h:30,d:10).fillet(r:2);` into an AST | **ANTLR4 C++ runtime** and generated lexer/parser |
| **Geometry Engine** | Builds 3D solids from parsed AST | **OpenCascade** (BRepPrimAPI, BRepAlgoAPI, etc.) |
| **Export** | Writes geometry to CAD/CAM formats | `STEPControl_Writer` + `StlAPI_Writer` |
| **Build System** | Orchestrates everything | **CMake 3.16+** with custom grammar generation step |
| **Console Output** | Colored, formatted logging | ANSI escape codes via stream manipulators |

---

## 🧩 Implementation Overview

### 1. **CMake Project Structure**
- Single executable target: `opendcad`
- Automatically:
  - Runs the `antlr` tool on `.g4` grammar(s)
  - Compiles generated C++ sources
  - Links against `antlr4_shared` and `OpenCascade` libraries
- Build directories:
  ```
  grammar/      → DSL grammar (OpenDCAD.g4)
  src/          → C++ source (main, output, visitors)
  build/        → Generated + compiled output
  ```

### 2. **ANTLR Integration**
- Grammar example: `OpenDCAD.g4`
  ```
  grammar OpenDCAD;

  program : (expr ';')* EOF ;
  expr    : IDENT '(' argList? ')' ('.' IDENT '(' argList? ')')* ;
  argList : arg (',' arg)* ;
  arg     : (IDENT ':')? NUMBER ;

  IDENT   : [A-Za-z_][A-Za-z0-9_]* ;
  NUMBER  : [0-9]+ ('.' [0-9]+)? ;
  WS      : [ \t\r\n]+ -> skip ;
  ```
- CMake auto-generates:
  - `OpenDCADLexer.*`
  - `OpenDCADParser.*`
  - Visitor and listener classes
- C++ runtime linked via `antlr4_shared`
- Parser verified via test program printing parse tree.

### 3. **OpenCascade Integration**
- Installed via Homebrew:
  ```
  brew install opencascade
  ```
- Linked via CMake:
  ```
  find_package(OpenCASCADE REQUIRED)
  target_link_libraries(opendcad PRIVATE ${OpenCASCADE_LIBRARIES})
  ```
- Verified by generating geometry:
  ```
  TopoDS_Shape a = BRepPrimAPI_MakeBox(60.0, 30.0, 10.0).Shape();
  TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(20,10,0), 20.0, 10.0, 12.0).Shape();
  TopoDS_Shape fused = BRepAlgoAPI_Fuse(a, b).Shape();
  ```
- Exported via:
  ```
  STEPControl_Writer writer;
  StlAPI_Writer stlWriter;
  ```

### 4. **Output Verification**
- Successful build on macOS (Apple Silicon)
- STEP and STL files viewable in FreeCAD/CAD Assistant
- Verified geometry: one box fused with another smaller one

### 5. **Console Utilities**
- Color-coded terminal output via stream manipulators:
  ```
  std::cout << termcolor::green << "Build OK" << termcolor::reset;
  ```

---

## ✅ Working End-to-End Demo

### Pipeline
```
ANTLR (parse) → AST → OpenCascade (build solids) → STEP/STL export
```

### Example Output Files
```
build/bin/opendcad_test.step
build/bin/opendcad_test.stl
```

### Example Program Output
```
OpenDCAD version [1.00.00]
OpenDCAD test parse OK
Input: Box(w:60,h:30,d:10).fillet(r:2);
Parse tree: (program (expr Box ( (argList (arg w : 60) , (arg h : 30) , (arg d : 10)) ) . fillet ( (argList (arg r : 2)) )) ; <EOF>)
Wrote STEP: build/bin/opendcad_test.step
Wrote STL:  build/bin/opendcad_test.stl
```

---

## 🧠 Next Steps

1. **AST → Geometry Visitor**
   - `Box(...)` → `BRepPrimAPI_MakeBox`
   - `Cylinder(...)` → `BRepPrimAPI_MakeCylinder`
   - `.fillet()` → `BRepFilletAPI_MakeFillet`
   - `.translate()` → `gp_Trsf` + `BRepBuilderAPI_Transform`

2. **Shape Composition**
   - `.union()` → `BRepAlgoAPI_Fuse`
   - `.subtract()` → `BRepAlgoAPI_Cut`

3. **Command-Line Interface**
   ```
   ./opendcad input.ocd --out step
   ```

4. **Preview Mode**
   - Generate temporary STL for live viewer refresh

---

## 🧭 In Summary
OpenDCAD is a modern **C++ CAD compiler** where:

- ANTLR parses a concise modeling language
- OpenCascade constructs robust, CNC-friendly geometry
- CMake automates generation and linking
- STEP and STL outputs are ready for manufacturing or visualization

You now have a complete working foundation:
**parser → geometry → export**, ready to grow into a full parametric modeling environment.
