# Phase 0: Architecture Review & Refactor

## Goal

Fix the critical disconnect between parser and geometry execution, restructure the codebase for growth, and produce a working end-to-end pipeline: `.dcad` source → parse → evaluate → geometry → STEP/STL.

## Current Problems

### 1. Parser is disconnected from execution
`TraceVisitor` (src/Listener.h) extends `OpenDCADBaseVisitor` but returns `nullptr` for every visit method. It only prints debug output — no shapes are created, no variables stored, no exports collected.

### 2. Main hardcodes geometry
`main()` in opendcad.cpp (lines 279–304) discards the parse tree entirely and builds hardcoded cylinders:
```cpp
ShapePtr cylinder1 = Shape::createCylinder( 54/2, 25 );
ShapePtr cylinder2 = Shape::createCylinder( 51/2, 25 );
cylinder1 = cylinder1->cut( cylinder2 )->z( 24 );
// ...
model = cylinder3->getShape();
```

### 3. LetValue is abandoned
`LetValue` class defined in Shape.h (lines 42–58) has the right idea (discriminated type for DSL values) but is never instantiated anywhere.

### 4. `flip()` has undefined behavior
Shape.cpp lines 64–68: declared to return `ShapePtr` but has an empty body — no return statement.

### 5. No error handling
OCCT failures print to stderr but execution continues with potentially invalid shapes.

### 6. Flat file structure
All source files in a single `src/` directory won't scale as we add face modeling, workplanes, DFM analysis, etc.

---

## Directory Structure

### Before
```
src/
├── opendcad.cpp          # main + file loading + STEP/STL export + hardcoded geometry
├── Shape.h               # Shape class + abandoned LetValue
├── Shape.cpp             # Shape implementation with empty flip()
├── Listener.h            # TraceVisitor (debug-only AST walk)
├── output.h / .cpp       # ANSI color utilities
├── Timer.h / .cpp        # Performance timing singleton
├── Debug.h               # Debug macros
├── viewer.cpp            # GLFW+OpenGL viewer
└── stb_easy_font.h       # Font library
```

### After
```
src/
├── main.cpp                    # Slim orchestrator
├── core/
│   ├── output.h / .cpp         # ANSI color utilities (unchanged)
│   ├── Timer.h / .cpp          # Performance timing singleton (unchanged)
│   ├── Debug.h                 # Debug macros (unchanged, update include paths)
│   └── Error.h                 # EvalError, GeometryError exception classes
├── geometry/
│   ├── Shape.h                 # Shape class (LetValue removed)
│   ├── Shape.cpp               # Shape implementation (flip fixed, errors throw)
│   └── ShapeRegistry.h / .cpp  # Factory + method dispatch tables
├── parser/
│   ├── Value.h / .cpp          # Discriminated value type (replaces LetValue)
│   ├── Environment.h / .cpp    # Symbol table with lexical scoping
│   └── Evaluator.h / .cpp      # THE KEY PIECE — walks parse tree, executes it
├── export/
│   ├── Exporter.h              # Abstract base (IExporter)
│   ├── StepExporter.h / .cpp   # STEP file writer
│   ├── StlExporter.h / .cpp    # STL file writer (with deflection params)
│   └── ShapeHealer.h / .cpp    # ShapeFix_Shape wrapper
└── viewer/
    ├── viewer.cpp              # GLFW+OpenGL viewer (unchanged for now)
    └── stb_easy_font.h         # Font library (unchanged)
```

**Rationale:** Group by responsibility. Parser classes (`Value`, `Environment`, `Evaluator`) stay together. Geometry (`Shape`, `ShapeRegistry`) separate from export. Viewer is isolated — it will get major changes in Phase 5 but stays untouched now.

---

## Class Designs

### 1. Error.h — Exception Hierarchy

**File:** `src/core/Error.h`

```cpp
namespace opendcad {

struct SourceLoc {
    std::string filename;
    size_t line = 0;
    size_t col = 0;
    std::string str() const;  // "file.dcad:12:5" or "<input>:12:5"
};

class EvalError : public std::runtime_error {
public:
    EvalError(const std::string& msg, const SourceLoc& loc = {});
    const SourceLoc& location() const;
private:
    SourceLoc loc_;
};

class GeometryError : public std::runtime_error {
public:
    explicit GeometryError(const std::string& msg);
};

} // namespace opendcad
```

**Usage in Shape.cpp:** Replace all `std::cerr << ... "Fuse failed"` with `throw GeometryError("fuse operation failed")`.

**Usage in Evaluator:** Catch `GeometryError` and re-throw as `EvalError` with source location from the parser context.

---

### 2. Value — Discriminated Value Type

**File:** `src/parser/Value.h`, `src/parser/Value.cpp`

Replaces the abandoned `LetValue` class. Every DSL expression evaluates to a `ValuePtr`.

```cpp
namespace opendcad {

class Value;
using ValuePtr = std::shared_ptr<Value>;

enum class ValueType { NUMBER, STRING, BOOL, VECTOR, SHAPE, NIL };

class Value {
public:
    // Factory methods
    static ValuePtr makeNumber(double v);
    static ValuePtr makeString(const std::string& v);
    static ValuePtr makeBool(bool v);
    static ValuePtr makeVector(const std::vector<double>& v);
    static ValuePtr makeShape(ShapePtr v);
    static ValuePtr makeNil();

    // Type info
    ValueType type() const;
    std::string typeName() const;   // "number", "string", etc.

    // Accessors (throw EvalError on type mismatch)
    double asNumber() const;
    const std::string& asString() const;
    bool asBool() const;
    const std::vector<double>& asVector() const;
    ShapePtr asShape() const;
    bool isTruthy() const;

    // Arithmetic (return new ValuePtr)
    ValuePtr add(const ValuePtr& other) const;
    ValuePtr subtract(const ValuePtr& other) const;
    ValuePtr multiply(const ValuePtr& other) const;
    ValuePtr divide(const ValuePtr& other) const;
    ValuePtr modulo(const ValuePtr& other) const;
    ValuePtr negate() const;

    // Display
    std::string toString() const;

private:
    ValueType type_;
    double number_ = 0;
    std::string string_;
    bool bool_ = false;
    std::vector<double> vector_;
    ShapePtr shape_;
};

} // namespace opendcad
```

**Type compatibility rules for arithmetic:**
| Operation | NUMBER × NUMBER | VECTOR × VECTOR | NUMBER × VECTOR | STRING × STRING |
|-----------|----------------|-----------------|-----------------|-----------------|
| `+` | add | element-wise add | — | concatenation |
| `-` | subtract | element-wise sub | — | error |
| `*` | multiply | error | scalar multiply | error |
| `/` | divide | error | vector / scalar | error |
| `%` | fmod | error | error | error |
| unary `-` | negate | element-wise neg | — | error |

---

### 3. Environment — Symbol Table

**File:** `src/parser/Environment.h`, `src/parser/Environment.cpp`

```cpp
namespace opendcad {

class Environment;
using EnvironmentPtr = std::shared_ptr<Environment>;

class Environment {
public:
    explicit Environment(EnvironmentPtr parent = nullptr);

    void define(const std::string& name, ValuePtr value);
    ValuePtr lookup(const std::string& name) const;  // searches parent chain
    bool has(const std::string& name) const;          // searches parent chain

    EnvironmentPtr parent() const;

private:
    EnvironmentPtr parent_;
    std::unordered_map<std::string, ValuePtr> bindings_;
};

} // namespace opendcad
```

**Scoping semantics for Phase 0:**
- Single global scope (no functions/blocks yet)
- `let x = expr;` calls `env->define("x", value)`
- Variable references call `env->lookup("x")` — throws `EvalError` if not found
- `let x = y;` (alias) calls `env->define("x", env->lookup("y"))`

**Future (Phase 4):** Child environments for function bodies, for-loop bodies, block scopes. The parent-chain lookup already supports this.

---

### 4. ShapeRegistry — Factory & Method Dispatch

**File:** `src/geometry/ShapeRegistry.h`, `src/geometry/ShapeRegistry.cpp`

```cpp
namespace opendcad {

using ShapeFactory = std::function<ShapePtr(const std::vector<ValuePtr>& args)>;
using ShapeMethod = std::function<ValuePtr(ShapePtr self, const std::vector<ValuePtr>& args)>;

class ShapeRegistry {
public:
    static ShapeRegistry& instance();

    void registerFactory(const std::string& name, ShapeFactory fn);
    void registerMethod(const std::string& name, ShapeMethod fn);

    bool hasFactory(const std::string& name) const;
    bool hasMethod(const std::string& name) const;

    ShapePtr callFactory(const std::string& name, const std::vector<ValuePtr>& args) const;
    ValuePtr callMethod(const std::string& name, ShapePtr self,
                        const std::vector<ValuePtr>& args) const;

    void registerDefaults();  // Wire all built-in factories and methods

private:
    ShapeRegistry() = default;
    std::unordered_map<std::string, ShapeFactory> factories_;
    std::unordered_map<std::string, ShapeMethod> methods_;
};

} // namespace opendcad
```

**Default registrations (`registerDefaults()`):**

| DSL name | Type | Maps to | Arg handling |
|----------|------|---------|-------------|
| `box` | factory | `Shape::createBox(w, d, h)` | 3 numbers |
| `cylinder` | factory | `Shape::createCylinder(r, h)` | 2 numbers |
| `torus` | factory | `Shape::createTorus(r1, r2)` | 2 numbers |
| `bin` | factory | `Shape::createBin(w, d, h, t)` | 4 numbers |
| `fuse` | method | `self->fuse(arg)` | 1 shape |
| `cut` | method | `self->cut(arg)` | 1 shape |
| `fillet` | method | `self->fillet(amount)` | 1 number |
| `flip` | method | `self->flip()` | 0 args |
| `translate` | method | special: accepts vector OR 1-3 numbers | see below |
| `rotate` | method | special: accepts vector OR 3 numbers | see below |
| `x` | method | `self->x(val)` | 1 number |
| `y` | method | `self->y(val)` | 1 number |
| `z` | method | `self->z(val)` | 1 number |
| `placeCorners` | method | `self->placeCorners(shape, x, y)` | 1 shape + 2 numbers |

**`translate` argument handling:**
- `translate([x, y, z])` → extract vector components
- `translate(x, y, z)` → 3 separate number args
- `translate(x, y)` → z defaults to 0
- `translate(x)` → y, z default to 0

**`rotate` argument handling:**
- `rotate([rx, ry, rz])` → extract vector components
- `rotate(rx, ry, rz)` → 3 separate number args

**Why a registry?** The Evaluator doesn't need to know about specific shapes. When the grammar adds new primitives (Phase 1: sphere, cone, etc.), we just add registry entries — no Evaluator changes needed.

---

### 5. Evaluator — The Key Missing Piece

**File:** `src/parser/Evaluator.h`, `src/parser/Evaluator.cpp`

This is the class that **bridges parsing and execution**. It extends `OpenDCADBaseVisitor` and actually builds geometry.

```cpp
namespace opendcad {

struct ExportEntry {
    std::string name;
    ShapePtr shape;
};

class Evaluator : public OpenDCADBaseVisitor {
public:
    Evaluator();

    // Main entry point
    void evaluate(OpenDCADParser::ProgramContext* tree,
                  const std::string& filename = "");

    // Results
    const std::vector<ExportEntry>& exports() const;

    // ---- Visitor overrides (all return antlrcpp::Any wrapping ValuePtr) ----

    antlrcpp::Any visitProgram(OpenDCADParser::ProgramContext* ctx) override;

    // Let statements
    antlrcpp::Any visitLetAlias(OpenDCADParser::LetAliasContext* ctx) override;
    antlrcpp::Any visitLetChain(OpenDCADParser::LetChainContext* ctx) override;
    antlrcpp::Any visitLetValue(OpenDCADParser::LetValueContext* ctx) override;

    // Export
    antlrcpp::Any visitExportStmt(OpenDCADParser::ExportStmtContext* ctx) override;

    // Chains
    antlrcpp::Any visitChainExpr(OpenDCADParser::ChainExprContext* ctx) override;
    antlrcpp::Any visitPostFromCall(OpenDCADParser::PostFromCallContext* ctx) override;
    antlrcpp::Any visitPostFromVar(OpenDCADParser::PostFromVarContext* ctx) override;

    // Expressions
    antlrcpp::Any visitMulDivMod(OpenDCADParser::MulDivModContext* ctx) override;
    antlrcpp::Any visitAddSub(OpenDCADParser::AddSubContext* ctx) override;
    antlrcpp::Any visitUnaryNeg(OpenDCADParser::UnaryNegContext* ctx) override;
    antlrcpp::Any visitPrimaryExpr(OpenDCADParser::PrimaryExprContext* ctx) override;
    antlrcpp::Any visitPrimary(OpenDCADParser::PrimaryContext* ctx) override;
    antlrcpp::Any visitVectorLiteral(OpenDCADParser::VectorLiteralContext* ctx) override;

private:
    EnvironmentPtr env_;
    std::vector<ExportEntry> exports_;
    std::string filename_;

    // Helpers
    SourceLoc locFrom(antlr4::ParserRuleContext* ctx) const;
    ValuePtr toValue(antlrcpp::Any result);
    std::vector<ValuePtr> evaluateArgs(OpenDCADParser::ArgListContext* ctx);
    ValuePtr applyMethodChain(ValuePtr current,
                              const std::vector<OpenDCADParser::MethodCallContext*>& methods);
};

} // namespace opendcad
```

**Visitor method specifications:**

#### `visitProgram`
```
for each stmt in ctx->stmt():
    visit(stmt)
return nil
```

#### `visitLetAlias` — `let a = b;`
```
name = ctx->IDENT(0)->getText()
source = ctx->IDENT(1)->getText()
value = env_->lookup(source)  // throws if not found
env_->define(name, value)
```

#### `visitLetChain` — `let a = bin(...).fillet(...);`
```
name = ctx->IDENT()->getText()
value = toValue(visit(ctx->chainExpr()))
env_->define(name, value)
```

#### `visitLetValue` — `let a = expr;`
```
name = ctx->IDENT()->getText()
value = toValue(visit(ctx->expr()))
env_->define(name, value)
```

#### `visitExportStmt` — `export expr as name;`
```
value = toValue(visit(ctx->expr()))
name = ctx->IDENT()->getText()
shape = value->asShape()  // throws if not a shape
exports_.push_back({name, shape})
```

#### `visitPostFromCall` — `cylinder(21/2, 70).translate([0,0,-35])`
```
callCtx = ctx->c
factoryName = callCtx->IDENT()->getText()
args = evaluateArgs(callCtx->argList())
shape = ShapeRegistry::instance().callFactory(factoryName, args)
current = Value::makeShape(shape)
current = applyMethodChain(current, ctx->methodCall())
return current
```

#### `visitPostFromVar` — `battery.translate([0, 22, 0])`
```
varName = ctx->var->getText()
current = env_->lookup(varName)
current = applyMethodChain(current, ctx->methodCall())
return current
```

#### `applyMethodChain` (private helper)
```
for each method in methods:
    methodName = method->IDENT()->getText()
    args = evaluateArgs(method->argList())
    if current is SHAPE and ShapeRegistry has method:
        current = ShapeRegistry::instance().callMethod(methodName, current->asShape(), args)
    else:
        throw EvalError("unknown method '" + methodName + "' on " + current->typeName())
return current
```

#### `visitMulDivMod` — `expr * expr`, `expr / expr`, `expr % expr`
```
left = toValue(visit(ctx->expr(0)))
right = toValue(visit(ctx->expr(1)))
op = ctx->children[1]->getText()
if op == "*": return left->multiply(right)
if op == "/": return left->divide(right)
if op == "%": return left->modulo(right)
```

#### `visitAddSub` — `expr + expr`, `expr - expr`
```
left = toValue(visit(ctx->expr(0)))
right = toValue(visit(ctx->expr(1)))
op = ctx->children[1]->getText()
if op == "+": return left->add(right)
if op == "-": return left->subtract(right)
```

#### `visitUnaryNeg` — `-expr`
```
val = toValue(visit(ctx->expr()))
return val->negate()
```

#### `visitPrimary`
```
if ctx->NUMBER():
    return Value::makeNumber(std::stod(ctx->NUMBER()->getText()))
if ctx->vectorLiteral():
    return visit(ctx->vectorLiteral())
if ctx->postfix():
    return visit(ctx->postfix())
if ctx->IDENT():
    return env_->lookup(ctx->IDENT()->getText())
if ctx->expr():  // parenthesized
    return visit(ctx->expr())
```

#### `visitVectorLiteral` — `[x, y, z]`
```
components = []
for each expr in ctx->expr():
    val = toValue(visit(expr))
    components.push_back(val->asNumber())
return Value::makeVector(components)
```

---

### 6. Export Module

#### StepExporter (`src/export/StepExporter.h/.cpp`)
```cpp
namespace opendcad {

class StepExporter {
public:
    static bool write(const TopoDS_Shape& shape, const std::string& path);
};

} // namespace opendcad
```
Extracted from `write_step()` in opendcad.cpp lines 124–137. Silences OCCT messaging, uses `STEPControl_Writer`.

#### StlExporter (`src/export/StlExporter.h/.cpp`)
```cpp
namespace opendcad {

struct StlParams {
    double deflection = 0.1;
    double angle = 0.5;
    bool parallel = true;
};

class StlExporter {
public:
    static bool write(const TopoDS_Shape& shape, const std::string& path,
                      const StlParams& params = {});
};

} // namespace opendcad
```
Extracted from `write_stl()` in opendcad.cpp lines 139–167. Meshes with `BRepMesh_IncrementalMesh`, counts triangles, writes binary STL.

#### ShapeHealer (`src/export/ShapeHealer.h/.cpp`)
```cpp
namespace opendcad {

class ShapeHealer {
public:
    static TopoDS_Shape heal(const TopoDS_Shape& shape);
};

} // namespace opendcad
```
Wraps `ShapeFix_Shape` from opendcad.cpp lines 309–311.

---

### 7. Shape.h / Shape.cpp Fixes

#### Remove LetValue
Delete lines 42–58 of Shape.h (the entire `LetValue` class). Its role is now filled by `Value`.

#### Fix `flip()`
Implement as mirror about the XY plane:
```cpp
ShapePtr Shape::flip() const {
    gp_Trsf mirror;
    mirror.SetMirror(gp::XOY());
    return ShapePtr(new Shape(BRepBuilderAPI_Transform(mShape, mirror, true).Shape()));
}
```

#### Replace cerr with throws
In `fuse()`:
```cpp
// Before:
std::cerr << termcolor::red << "Fuse failed" << "\n";
// After:
throw GeometryError("fuse operation failed");
```
Same for `cut()` and `fillet()`.

---

### 8. Grammar Changes

**File:** `grammar/OpenDCAD.g4`

#### Add STRING token
```antlr
// In primary rule, add STRING alternative:
primary
  : NUMBER
  | STRING           // NEW
  | vectorLiteral
  | postfix
  | IDENT
  | '(' expr ')'
  ;

// New lexer rule:
STRING
  : '"' (~["\\\r\n] | '\\' .)* '"'
  ;
```

#### Make argList optional
```antlr
// Before:
call      : IDENT '(' argList ')' ;
methodCall: IDENT '(' argList ')' ;

// After:
call      : IDENT '(' argList? ')' ;
methodCall: IDENT '(' argList? ')' ;
```

This allows zero-arg calls like `.flip()` to parse correctly.

---

### 9. New main.cpp

**File:** `src/main.cpp` (replaces `src/opendcad.cpp`)

```
main(argc, argv):
    writeLogo()

    // Parse command line
    if argc < 2:
        print "Usage: opendcad <input.dcad> [output_dir]"
        return 1
    inputFile = argv[1]
    outputDir = argc > 2 ? argv[2] : "."

    // Register built-in shapes
    ShapeRegistry::instance().registerDefaults()

    // Parse
    src = loadFile(inputFile)
    ANTLRInputStream input(src)
    OpenDCADLexer lexer(&input)
    CommonTokenStream tokens(&lexer)
    OpenDCADParser parser(&tokens)
    tree = parser.program()

    // Check for syntax errors
    if parser.getNumberOfSyntaxErrors() > 0:
        print "Syntax errors found"
        return 1

    // Evaluate
    Evaluator evaluator
    try:
        evaluator.evaluate(tree, inputFile)
    catch EvalError& e:
        print e.what()
        return 1

    // Export each exported shape
    for export in evaluator.exports():
        shape = ShapeHealer::heal(export.shape->getShape())
        StepExporter::write(shape, outputDir + "/" + export.name + ".step")
        StlExporter::write(shape, outputDir + "/" + export.name + ".stl", {0.01, 0.1, true})
        print "Exported: " + export.name

    return 0
```

---

### 10. CMakeLists.txt Updates

#### New source file list
```cmake
add_executable(opendcad
  src/main.cpp
  src/core/output.cpp
  src/core/Timer.cpp
  src/geometry/Shape.cpp
  src/geometry/ShapeRegistry.cpp
  src/parser/Value.cpp
  src/parser/Environment.cpp
  src/parser/Evaluator.cpp
  src/export/StepExporter.cpp
  src/export/StlExporter.cpp
  src/export/ShapeHealer.cpp
)
```

#### Include directories
```cmake
target_include_directories(opendcad PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/core
  ${CMAKE_SOURCE_DIR}/src/geometry
  ${CMAKE_SOURCE_DIR}/src/parser
  ${CMAKE_SOURCE_DIR}/src/export
  ${GEN_DIR}
)
```

#### Additional OCCT components
Add to the component list:
```cmake
set(_occt_components
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKTopAlgo TKSTL
  TKPrim TKBO TKFillet TKOffset TKShHealing
  TKSTEP TKSTEPBase TKSTEPAttr TKXSBase
)
```
And to the imported targets block:
```cmake
OpenCASCADE::TKPrim
OpenCASCADE::TKBO
OpenCASCADE::TKFillet
OpenCASCADE::TKOffset
OpenCASCADE::TKShHealing
OpenCASCADE::TKSTEP
OpenCASCADE::TKSTEPBase
OpenCASCADE::TKSTEPAttr
OpenCASCADE::TKXSBase
```

#### Fix capitalization
Change `Src/Timer.cpp` to `src/core/Timer.cpp` and `Src/viewer.cpp` to `src/viewer/viewer.cpp`.

---

## Implementation Order

```
Step 1: Error.h                    (no dependencies)
Step 2: Value.h / Value.cpp        (depends on: Shape.h, Error.h)
Step 3: Environment.h / .cpp       (depends on: Value.h)
Step 4: ShapeRegistry.h / .cpp     (depends on: Value.h, Shape.h)
Step 5: Fix Shape.h / Shape.cpp    (depends on: Error.h)
Step 6: Export classes              (depends on: nothing new)
Step 7: Evaluator.h / .cpp         (depends on: everything above)
Step 8: Grammar changes            (independent, but Evaluator must match)
Step 9: New main.cpp               (depends on: everything)
Step 10: CMakeLists.txt            (depends on: file layout)
```

Steps 1–4 can be done in any order. Step 5 is independent. Step 6 is independent. Steps 7–10 must come after 1–6.

---

## Files to Create

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `src/core/Error.h` | 40 | Exception classes with source location |
| `src/parser/Value.h` | 65 | Discriminated value type header |
| `src/parser/Value.cpp` | 200 | Value arithmetic and type operations |
| `src/parser/Environment.h` | 25 | Symbol table header |
| `src/parser/Environment.cpp` | 30 | Symbol table with parent-chain lookup |
| `src/parser/Evaluator.h` | 55 | Evaluator class declaration |
| `src/parser/Evaluator.cpp` | 250 | Parse tree walker / executor |
| `src/geometry/ShapeRegistry.h` | 35 | Registry class declaration |
| `src/geometry/ShapeRegistry.cpp` | 120 | Default registrations for all built-ins |
| `src/export/StepExporter.h` | 15 | STEP writer header |
| `src/export/StepExporter.cpp` | 25 | STEP writer implementation |
| `src/export/StlExporter.h` | 20 | STL writer header |
| `src/export/StlExporter.cpp` | 40 | STL writer implementation |
| `src/export/ShapeHealer.h` | 15 | Shape healing header |
| `src/export/ShapeHealer.cpp` | 15 | Shape healing implementation |

## Files to Modify

| File | Changes |
|------|---------|
| `src/geometry/Shape.h` | Remove LetValue class (17 lines) |
| `src/geometry/Shape.cpp` | Fix flip(), replace cerr with throws, add `#include "Error.h"` |
| `grammar/OpenDCAD.g4` | Add STRING token, make argList optional |
| `CMakeLists.txt` | New source files, include dirs, OCCT components, fix Src/ capitalization |
| `src/main.cpp` | Complete rewrite (was opendcad.cpp) |

## Files to Delete (after restructure)

| File | Reason |
|------|--------|
| `src/opendcad.cpp` | Replaced by `src/main.cpp` |
| `src/Shape.h` | Moved to `src/geometry/Shape.h` |
| `src/Shape.cpp` | Moved to `src/geometry/Shape.cpp` |
| `src/Listener.h` | Replaced by `src/parser/Evaluator.h` |
| `src/output.h` | Moved to `src/core/output.h` |
| `src/output.cpp` | Moved to `src/core/output.cpp` |
| `src/Timer.h` | Moved to `src/core/Timer.h` |
| `src/Timer.cpp` | Moved to `src/core/Timer.cpp` |
| `src/Debug.h` | Moved to `src/core/Debug.h` |

---

## Verification

### Test script
Run:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bin/opendcad examples/battery.dcad build/bin
```

### Expected behavior
The `battery.dcad` script exercises:
- **Factory calls:** `bin(80,120,20,2)`, `cylinder(21/2, 70)`, `cylinder(5, 20)`, `cylinder(4.8/2, 6)`
- **Arithmetic:** `21/2` → 10.5, `4.8/2` → 2.4
- **Vector literals:** `[0, 0, -35]`, `[0, 90, 0]`, `[0, 0, 12]`, etc.
- **Method chains:** `.fillet(0.5)`, `.translate([0,0,-35]).rotate([0,90,0]).translate([0,0,12])`
- **Boolean operations:** `.fuse(...)`, `.cut(...)`
- **Variable binding:** `let bin = ...`, `let battery = ...`, `let four = ...`, etc.
- **Variable aliasing:** `let fourBackup = four;`
- **Method calls on variables:** `battery.translate([0, 22, 0])`
- **Multi-argument method:** `.placeCorners(piece, 34, 54)`
- **Export:** `export model as bin;`, `export model.cut(piece) as bin_with_cut;`

### Expected output files
```
build/bin/bin.step
build/bin/bin.stl
build/bin/bin_with_cut.step
build/bin/bin_with_cut.stl
```

### Correctness check
Open `bin.step` in a CAD viewer (FreeCAD, CAD Exchanger). Should show:
- An 80×120×20 rectangular bin with 2mm walls and 0.5mm filleted edges
- Four cylindrical standoffs at the corners (r=5, h=20, with r=2.4 bore at z=14)
- Four batteries (r=10.5, h=70) rotated 90° and arranged in a row

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| ANTLR visitor `antlrcpp::Any` type erasure bugs | Wrap all returns in `ValuePtr`; use `toValue()` helper that checks type |
| Grammar ambiguity between `letChain` and `letValue` | ANTLR resolves by rule order — `letChain` tries first. Test with various inputs. |
| `argList?` making it optional might cause grammar conflicts | ANTLR handles this cleanly — empty parens `()` match `argList?` as absent |
| Existing viewer breaks | Viewer is separate executable, not touched in Phase 0. Verify it still compiles. |
| Integer vs float arithmetic (`21/2` = 10 in C++) | All DSL numbers are doubles. `21/2` in the DSL becomes `stod("21") / stod("2")` = 10.5. The integer truncation bug was in the hardcoded C++ (`54/2` = 27 integer division). |
