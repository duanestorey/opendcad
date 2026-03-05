# Phase 3: Complete DSL Language Design

## Goal

Define the complete OpenDCAD language -- every syntactic construct, semantic rule, and built-in library element needed to write real-world parametric CAD programs. This phase transforms OpenDCAD from a minimal shape-chaining DSL into a full scripting language with functions, conditionals, loops, a units system, sketch-based modeling, DFM analysis, and a standard library of mechanical/electronic components.

All designs in this document build on the Phase 0 infrastructure: `Value`, `Environment`, `Evaluator`, `ShapeRegistry`, and the ANTLR4-based parser pipeline.

---

## 1. Core Syntax

### 1.1 Bindings: `let` and `const`

OpenDCAD provides two binding forms. `let` creates a mutable binding that can be reassigned within its scope. `const` creates an immutable binding that cannot be reassigned after initialization.

```
let wall = 2.0mm;          // mutable -- can be reassigned
const PI = 3.14159;        // immutable -- assignment after init is a compile error
let wall = 3.0mm;          // OK: rebinds 'wall' to new value
const PI = 2.0;            // ERROR: cannot reassign const binding
```

#### Scoping rules

Bindings are block-scoped. A block is delimited by `{ }` and introduces a new child `Environment`. When the block ends, all bindings defined within it are discarded. Inner scopes can shadow outer bindings.

```
let x = 10;
{
    let x = 20;            // shadows outer 'x'
    let y = x + 1;         // y = 21
}
// x is 10 here; y is not accessible
```

#### Grammar additions

```antlr
stmt
  : letStmt ';'
  | constStmt ';'
  | assignStmt ';'
  | exportStmt ';'
  | ifStmt
  | forStmt
  | fnDecl
  | moduleDecl
  | blockStmt
  | exprStmt ';'
  ;

letStmt
  : 'let' IDENT '=' expr
  ;

constStmt
  : 'const' IDENT '=' expr
  ;

assignStmt
  : IDENT '=' expr
  ;

blockStmt
  : '{' stmt* '}'
  ;
```

#### Evaluator implementation

The `Environment` class (defined in Phase 0) already supports parent-chain lookup. To support `const`, add a `frozen` flag per binding:

```cpp
struct Binding {
    ValuePtr value;
    bool isConst = false;
};

class Environment {
public:
    void define(const std::string& name, ValuePtr value, bool isConst = false);
    void assign(const std::string& name, ValuePtr value);  // walks parent chain, throws on const
    ValuePtr lookup(const std::string& name) const;
private:
    std::unordered_map<std::string, Binding> bindings_;
    EnvironmentPtr parent_;
};
```

`define()` creates a new binding in the current scope (shadowing is allowed). `assign()` finds the existing binding in the nearest enclosing scope and updates it; it throws `EvalError` if the binding is `const` or does not exist. The `let` statement calls `define(name, value, false)`. The `const` statement calls `define(name, value, true)`. A bare `x = expr;` reassignment calls `assign(name, value)`.

---

### 1.2 Functions: `fn` and `module`

Two callable forms exist. `fn` defines a general-purpose function that returns a value. `module` defines a geometry-producing function that implicitly collects shapes for boolean composition.

#### `fn` -- value-returning function

```
fn rounded_box(w, d, h, r = 1.0mm) {
    let box = box(w, d, h);
    return box.fillet(r);
}

let part = rounded_box(40, 60, 10);
let part2 = rounded_box(40, 60, 10, r: 2.0mm);
```

A function body is a block of statements. The `return` statement immediately exits the function and provides the return value. If execution reaches the end of the body without a `return`, the function returns `nil`.

#### `module` -- geometry-producing function

```
module mounting_plate(w, d, hole_r, hole_inset) {
    let plate = box(w, d, 3mm);
    let hole = cylinder(hole_r, 10mm).translate([0, 0, -2mm]);

    plate;                                  // adds to implicit geometry list
    hole.translate([hole_inset, hole_inset, 0]);   // also added
    hole.translate([w - hole_inset, hole_inset, 0]);
    hole.translate([hole_inset, d - hole_inset, 0]);
    hole.translate([w - hole_inset, d - hole_inset, 0]);
}

let mount = mounting_plate(80, 60, 3mm, 8mm);
```

Inside a `module`, every statement that evaluates to a shape value is collected. The first shape becomes the base. Subsequent shapes are fused with the base if they are bare expressions, or cut from the base if preceded by `-`:

```
module enclosure_lid(w, d) {
    box(w, d, 2mm);                         // base shape (fused)
    -cylinder(2mm, 10mm).translate([5, 5, -5mm]);  // cut from base
}
```

If only one shape is produced, that shape is the module's return value. If multiple shapes are produced, they are combined (fuse for additive, cut for subtractive) into a single shape.

#### Default parameters

Both `fn` and `module` support default parameter values. Default parameters must appear after all required parameters. Default values are evaluated at call time, not at definition time.

```
fn hole(r, depth, chamfer = 0.5mm, through = false) {
    let h = cylinder(r, depth);
    if chamfer > 0 {
        // apply chamfer logic
    }
    return h;
}
```

At the call site, arguments can be passed positionally or by name. Named arguments can appear in any order but must come after all positional arguments:

```
hole(3mm, 10mm);                        // chamfer = 0.5mm, through = false
hole(3mm, 10mm, chamfer: 1.0mm);       // through = false
hole(3mm, 10mm, through: true);         // chamfer = 0.5mm
hole(r: 3mm, depth: 10mm);             // all named
```

#### Grammar additions

```antlr
fnDecl
  : 'fn' IDENT '(' paramList? ')' blockStmt
  ;

moduleDecl
  : 'module' IDENT '(' paramList? ')' blockStmt
  ;

paramList
  : param (',' param)*
  ;

param
  : IDENT ('=' expr)?
  ;

returnStmt
  : 'return' expr? ';'
  ;
```

The `stmt` rule is updated to include `returnStmt`.

#### Evaluator implementation

Functions and modules are stored as `Value::FUNCTION` in the environment. The `Value` class gains a new variant:

```cpp
enum class ValueType { NUMBER, STRING, BOOL, VECTOR, SHAPE, FUNCTION, NIL };

struct FunctionDef {
    std::string name;
    std::vector<ParamDef> params;   // {name, optional default expr}
    antlr4::ParserRuleContext* body;
    EnvironmentPtr closureEnv;      // captured lexical scope
    bool isModule;                  // true for 'module', false for 'fn'
};
```

When a function is called:

1. Create a new child `Environment` with the closure environment as parent.
2. Bind each parameter name to its argument value (or default if omitted).
3. For `fn`: execute the body; if a `return` is hit, catch the `ReturnSignal` exception to unwind and extract the return value.
4. For `module`: execute the body with an implicit shape collector. Each expression statement that evaluates to a shape is pushed onto a `std::vector<ShapeEntry>`. After the body completes, combine all shapes (fuse additive, cut subtractive) and return the result.

The `ReturnSignal` is a lightweight exception (not derived from `std::runtime_error`) used solely for control flow:

```cpp
struct ReturnSignal {
    ValuePtr value;
};
```

---

### 1.3 Conditionals

#### Block form

```
if wall_thickness < 1.0mm {
    // warn or adjust
} else if wall_thickness > 5.0mm {
    // different behavior
} else {
    // default case
}
```

Braces are mandatory. There is no ambiguity about dangling `else`. The condition expression can be any expression; truthiness rules apply (0 and `nil` are falsy; everything else is truthy).

#### Expression form (ternary)

```
let r = if curved then 2.0mm else 0;
let label = if version > 2 then "v2+" else "legacy";
```

The ternary form is an expression, not a statement. It evaluates and returns one of the two branches. Both branches must be present (no bare `if` expression without `else`).

#### Grammar additions

```antlr
ifStmt
  : 'if' expr blockStmt ('else' 'if' expr blockStmt)* ('else' blockStmt)?
  ;

// In the expr rule, add ternary at lowest precedence:
expr
  : 'if' expr 'then' expr 'else' expr    # ternaryExpr
  | expr ('||') expr                      # logicOr
  | expr ('&&') expr                      # logicAnd
  | expr ('==' | '!=' | '<' | '<=' | '>' | '>=') expr  # comparison
  | expr ('+' | '-') expr                # addSub
  | expr ('*' | '/' | '%') expr          # mulDivMod
  | '-' expr                             # unaryNeg
  | '!' expr                             # unaryNot
  | primary                              # primaryExpr
  ;
```

#### Evaluator implementation

`visitIfStmt` evaluates conditions in order. The first truthy condition's block is executed; remaining branches are skipped. If no condition is truthy and an `else` block exists, that block is executed.

`visitTernaryExpr` evaluates the condition, then evaluates and returns exactly one of the two branch expressions (not both).

---

### 1.4 Loops

#### Range-based `for`

```
for i in 0..8 {
    let angle = i * 45;
    let post = cylinder(3mm, 20mm).rotate([0, 0, angle]).translate([30mm, 0, 0]);
    // use post
}
```

The `0..8` syntax creates an exclusive range `[0, 1, 2, 3, 4, 5, 6, 7]`. For inclusive ranges, use `0..=8` which produces `[0, 1, 2, 3, 4, 5, 6, 7, 8]`.

#### `range()` function

For more control, use the built-in `range()` function:

```
for x in range(-40, 40, step: 10) {
    // x takes values: -40, -30, -20, -10, 0, 10, 20, 30
}

for y in range(0, 360, step: 45) {
    // angular iteration
}
```

`range(start, end)` produces values from `start` up to but not including `end`, with step 1. `range(start, end, step: s)` uses the specified step. Negative steps are allowed for descending ranges.

#### List iteration

```
let sizes = [10, 20, 30, 40];
for s in sizes {
    let part = box(s, s, s);
}

let offsets = [[0,0,0], [10,0,0], [20,0,0]];
for pos in offsets {
    let moved = part.translate(pos);
}
```

#### Loop body as geometry collector

When a `for` loop appears inside a `module`, each iteration's shape expressions are collected and composed, just like at the module's top level. This enables pattern generation:

```
module bolt_circle(count, radius, hole_r) {
    for i in 0..count {
        let angle = i * (360 / count);
        cylinder(hole_r, 100mm)
            .translate([radius, 0, 0])
            .rotate([0, 0, angle]);
    }
}
```

#### Grammar additions

```antlr
forStmt
  : 'for' IDENT 'in' rangeExpr blockStmt
  ;

rangeExpr
  : expr '..' expr             # exclusiveRange
  | expr '..=' expr            # inclusiveRange
  | expr                       # iterableExpr
  ;
```

The `range()` function is a built-in function, not special syntax. It returns a list `Value` that the `for` loop iterates over.

#### Evaluator implementation

`visitForStmt` evaluates the range expression to produce an iterable (either a `RangeValue` from `..` syntax or a list `Value` from `range()` or a literal list). For each element, it creates a child `Environment`, binds the loop variable, and executes the body block. If inside a module context, shape expressions from each iteration are appended to the module's shape collector.

---

### 1.5 Imports

OpenDCAD supports importing definitions from other `.dcad` files and from the standard library.

#### File imports

```
// Import everything from a file into the current scope
import "enclosure_utils.dcad";

// Import into a namespace
import "enclosure_utils.dcad" as utils;
let lid = utils.make_lid(80, 60);

// Selective import
import { make_lid, make_base } from "enclosure_utils.dcad";

// Relative paths are resolved relative to the importing file
import "../common/materials.dcad" as mat;
```

#### Standard library imports

```
// Import a standard library module using dot notation
import std.fasteners;
let hole = std.fasteners.clearance_hole("M3");

// Import with alias
import std.fasteners as f;
let hole = f.clearance_hole("M3");

// Selective import from std library
import { clearance_hole, heat_set_hole } from std.fasteners;
```

#### Import resolution order

When a bare path like `"utils.dcad"` is used, the resolver searches in this order:

1. **Relative to current file:** `./utils.dcad` relative to the file containing the `import` statement.
2. **User library path:** `$OPENDCAD_LIB/utils.dcad` (environment variable, defaults to `~/.opendcad/lib/`).
3. **System library path:** `/usr/local/share/opendcad/lib/utils.dcad` (or platform equivalent).
4. **Standard library:** Built-in `std/` modules bundled with the compiler.

If the path starts with `./` or `../`, only the relative resolution (step 1) is attempted. If the path uses `std.` prefix, only the standard library (step 4) is searched.

#### Grammar additions

```antlr
importStmt
  : 'import' STRING ';'                                      # importAll
  | 'import' STRING 'as' IDENT ';'                           # importAs
  | 'import' '{' identList '}' 'from' STRING ';'             # importSelective
  | 'import' qualifiedName ';'                                # importStdAll
  | 'import' qualifiedName 'as' IDENT ';'                    # importStdAs
  | 'import' '{' identList '}' 'from' qualifiedName ';'      # importStdSelective
  ;

qualifiedName
  : IDENT ('.' IDENT)*
  ;

identList
  : IDENT (',' IDENT)*
  ;
```

#### Evaluator implementation

The `Evaluator` gains an `ImportResolver` collaborator:

```cpp
class ImportResolver {
public:
    void addSearchPath(const std::string& path);
    void setStdLibPath(const std::string& path);

    // Resolves import path to absolute file path
    std::string resolve(const std::string& importPath,
                        const std::string& currentFile) const;

    // Parse and evaluate a file, returning its exported environment
    EnvironmentPtr loadModule(const std::string& absPath,
                              Evaluator& evaluator);

    // Cache to prevent re-evaluation of already-loaded modules
    bool isLoaded(const std::string& absPath) const;

private:
    std::vector<std::string> searchPaths_;
    std::string stdLibPath_;
    std::unordered_map<std::string, EnvironmentPtr> cache_;
};
```

When processing an `import` statement:

1. Resolve the path using the search order.
2. Check the module cache. If already loaded, reuse the cached environment.
3. Parse and evaluate the target file in a fresh `Environment`.
4. For `import "file.dcad"`: copy all exported bindings into the current scope.
5. For `import "file.dcad" as name`: create a namespace value wrapping the module's environment and bind it to `name`.
6. For `import { a, b } from "file.dcad"`: copy only the named bindings into the current scope, throwing `EvalError` if any are not found.

Circular imports are detected by tracking which files are currently being evaluated. If a cycle is found, an `EvalError` is raised.

---

### 1.6 Comments

Comments are unchanged from the current grammar. Both forms are already supported:

```
// Single-line comment (to end of line)

/* Multi-line comment
   spanning several lines */

let r = 5mm; // inline comment after a statement
```

Both comment forms are sent to the `HIDDEN` channel by the lexer and are invisible to the parser. No changes needed.

---

## 2. Units System

### 2.1 Design Principles

All numeric literals in OpenDCAD can carry an optional unit suffix. Internally, all values are stored in **millimeters** for length and **degrees** for angles. The unit suffix acts as a compile-time multiplication factor -- `2.5cm` is syntactic sugar for `2.5 * 10.0`, yielding `25.0` (mm).

The default unit for bare numbers depends on context:
- Length context: bare numbers are interpreted as **millimeters**.
- Angle context: bare numbers are interpreted as **degrees**.

This means `fillet(2)` and `fillet(2mm)` are equivalent.

### 2.2 Supported Units

#### Length units

| Suffix | Full name | Conversion to mm |
|--------|-----------|-------------------|
| `mm` | millimeter | 1.0 (identity) |
| `cm` | centimeter | 10.0 |
| `m` | meter | 1000.0 |
| `in` | inch | 25.4 |
| `ft` | foot | 304.8 |
| `thou` | thousandth of an inch | 0.0254 |

#### Angle units

| Suffix | Full name | Conversion to degrees |
|--------|-----------|------------------------|
| `deg` | degree | 1.0 (identity) |
| `rad` | radian | 180.0 / PI (approx 57.2958) |

### 2.3 Usage Examples

```
let wall = 2.5mm;                  // 2.5
let width = 3.2cm;                 // 32.0
let plate = 0.5in;                 // 12.7
let room = 2.4m;                   // 2400.0

let angle = 45deg;                 // 45.0
let sweep = 1.5rad;               // 85.9437...

// Units in expressions
let total = 1in + 5mm;            // 30.4  (25.4 + 5.0)
let half = 2cm / 2;               // 10.0  (20.0 / 2)

// Units in function arguments
let part = box(2in, 3in, 0.5in);
let fillet_part = part.fillet(1.5mm);
let rotated = part.rotate([0, 0, 90deg]);
```

### 2.4 Grammar additions

Unit suffixes are handled at the lexer level. The `NUMBER` token is extended to include an optional unit suffix, or alternatively a new `UNIT_NUMBER` token is introduced:

```antlr
UNIT_NUMBER
  : NUMBER_BODY UNIT_SUFFIX
  ;

NUMBER
  : NUMBER_BODY
  ;

fragment NUMBER_BODY
  : [0-9]+ ('.' [0-9]+)? ( [eE] [+-]? [0-9]+ )?
  | '.' [0-9]+ ( [eE] [+-]? [0-9]+ )?
  ;

fragment UNIT_SUFFIX
  : 'mm' | 'cm' | 'm' | 'in' | 'ft' | 'thou' | 'deg' | 'rad'
  ;
```

The `primary` rule is updated:

```antlr
primary
  : UNIT_NUMBER
  | NUMBER
  | STRING
  | vectorLiteral
  | postfix
  | IDENT
  | '(' expr ')'
  ;
```

### 2.5 Evaluator implementation

A `UnitConverter` utility handles the conversion:

```cpp
struct UnitInfo {
    std::string name;
    double factor;
    enum Category { LENGTH, ANGLE } category;
};

class UnitConverter {
public:
    static double convert(double value, const std::string& unitSuffix);
    static UnitInfo lookup(const std::string& unitSuffix);

private:
    static const std::unordered_map<std::string, UnitInfo> units_;
};
```

The conversion table:

```cpp
const std::unordered_map<std::string, UnitInfo> UnitConverter::units_ = {
    {"mm",   {"millimeter",  1.0,       UnitInfo::LENGTH}},
    {"cm",   {"centimeter",  10.0,      UnitInfo::LENGTH}},
    {"m",    {"meter",       1000.0,    UnitInfo::LENGTH}},
    {"in",   {"inch",        25.4,      UnitInfo::LENGTH}},
    {"ft",   {"foot",        304.8,     UnitInfo::LENGTH}},
    {"thou", {"thou",        0.0254,    UnitInfo::LENGTH}},
    {"deg",  {"degree",      1.0,       UnitInfo::ANGLE}},
    {"rad",  {"radian",      57.29577951308232, UnitInfo::ANGLE}},
};
```

When the evaluator encounters a `UNIT_NUMBER` token, it extracts the numeric part and the suffix, converts the value to the internal representation (mm or degrees), and returns a `Value::NUMBER`.

### 2.6 Unit Ambiguity: the `m` suffix

The `m` suffix for meters conflicts with potential identifier names beginning with `m`. The lexer resolves this by requiring `UNIT_NUMBER` to have no whitespace between the number and the suffix. The token `5m` is a unit number (5 meters = 5000mm). The tokens `5 m` are a number `5` followed by an identifier `m`. The ANTLR4 maximal-munch rule ensures that `5m` is consumed as a single `UNIT_NUMBER` token because `UNIT_NUMBER` is listed before `NUMBER` in the grammar and matches a longer input.

---

## 3. Geometry Syntax

### 3.1 Boolean Operations

Boolean operations remain method-only. No operator overloading is used for geometry, preserving clarity about which operations are expensive B-Rep computations.

```
let body = box(100, 80, 40);
let hole = cylinder(5mm, 50mm).translate([20, 20, -5]);

let result = body.fuse(other_part);       // union
let result = body.cut(hole);              // subtraction
let result = body.intersect(clamp);       // intersection
```

All three return a new `ShapePtr`. The original shapes are not mutated.

#### `intersect` implementation

Add to `Shape`:

```cpp
ShapePtr Shape::intersect(const ShapePtr& part) const {
    BRepAlgoAPI_Common common(mShape, part->getShape());
    common.SetRunParallel(Standard_True);
    common.SetNonDestructive(true);
    common.Build();
    if (!common.IsDone()) {
        throw GeometryError("intersect operation failed");
    }
    return ShapePtr(new Shape(common.Shape()));
}
```

Register in `ShapeRegistry::registerDefaults()`:

```cpp
registerMethod("intersect", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
    if (args.size() != 1 || args[0]->type() != ValueType::SHAPE)
        throw EvalError("intersect() requires exactly 1 shape argument");
    return Value::makeShape(self->intersect(args[0]->asShape()));
});
```

### 3.2 Named Arguments

All function and method calls support named arguments. Named arguments provide self-documenting call sites and allow arguments to be passed in any order (after positional arguments).

```
let part = box(width: 100, depth: 80, height: 40);
let cyl = cylinder(radius: 10mm, height: 50mm);
let moved = part.translate(x: 10, y: 20, z: 5);
let filleted = part.fillet(radius: 2mm);
```

Named arguments are resolved by the `ShapeRegistry` dispatch layer. Each factory and method registration includes a parameter name list:

```cpp
struct ParamDef {
    std::string name;
    std::optional<ValuePtr> defaultValue;
};

using ShapeFactory = std::function<ShapePtr(const std::vector<ValuePtr>& args)>;

void registerFactory(const std::string& name,
                     const std::vector<ParamDef>& params,
                     ShapeFactory fn);
```

At call time, the evaluator produces a list of `(name, value)` pairs. The registry matches named arguments to parameter positions, fills in defaults for omitted parameters, and calls the factory/method with a positional argument vector.

#### Grammar additions

```antlr
argList
  : arg (',' arg)*
  ;

arg
  : IDENT ':' expr            # namedArg
  | expr                      # positionalArg
  ;
```

### 3.3 Face Selection

Face selection allows targeting specific faces of a solid for operations like chamfer, fillet, or sketch placement. Faces are selected by direction using a string selector.

```
let top = part.face(">Z");          // face with highest Z normal
let bottom = part.face("<Z");       // face with lowest Z normal (negative Z normal)
let front = part.face(">Y");       // face with highest Y normal
let right = part.face(">X");       // face with highest X normal

// Convenience aliases
let top = part.topFace();           // equivalent to .face(">Z")
let bottom = part.bottomFace();     // equivalent to .face("<Z")
let front = part.frontFace();       // equivalent to .face(">Y")
let back = part.backFace();         // equivalent to .face("<Y")
let right = part.rightFace();       // equivalent to .face(">X")
let left = part.leftFace();        // equivalent to .face("<X")
```

#### Face selector syntax

| Selector | Meaning |
|----------|---------|
| `">Z"` | Face whose outward normal has the largest Z component |
| `"<Z"` | Face whose outward normal has the smallest (most negative) Z component |
| `">X"` | Face with largest X-component normal |
| `"<X"` | Face with smallest X-component normal |
| `">Y"` | Face with largest Y-component normal |
| `"<Y"` | Face with smallest Y-component normal |
| `"#N"` | Face by index (0-based, ordered by face explorer) |

#### Implementation

Face selection uses `TopExp_Explorer` with `TopAbs_FACE` to iterate over all faces. For directional selectors, each face's outward normal is computed at the face centroid using `BRepGProp_Face::Normal()`. The face with the extremal normal component in the specified direction is returned.

```cpp
class FaceSelector {
public:
    static TopoDS_Face select(const TopoDS_Shape& shape, const std::string& selector);
    static std::vector<TopoDS_Face> selectAll(const TopoDS_Shape& shape, const std::string& selector);

private:
    static gp_Dir faceNormal(const TopoDS_Face& face);
    static gp_Pnt faceCentroid(const TopoDS_Face& face);
};
```

The return type for `.face()` in the DSL is a new `Value::FACE` type that wraps a `TopoDS_Face` along with the parent shape. This allows chaining face-specific operations:

```
let top_filleted = part.face(">Z").fillet(1mm);   // fillet only top face edges
```

### 3.4 Edge Selectors with `@` Prefix

The `@` prefix denotes a predefined edge selector. Edge selectors are used as arguments to operations like `fillet()` and `chamfer()` to limit which edges are affected.

```
let filleted = part.fillet(2mm, edges: @vertical);
let chamfered = part.chamfer(1mm, edges: @topFace);
```

#### Built-in edge selectors

| Selector | Meaning |
|----------|---------|
| `@all` | All edges (default behavior, same as calling without selector) |
| `@vertical` | Edges parallel to Z axis (within tolerance) |
| `@horizontal` | Edges perpendicular to Z axis |
| `@topFace` | Edges belonging to the face with highest Z normal |
| `@bottomFace` | Edges belonging to the face with lowest Z normal |
| `@parallel(axis)` | Edges parallel to a given axis vector |
| `@perpendicular(axis)` | Edges perpendicular to a given axis vector |
| `@longest` | The single longest edge |
| `@shortest` | The single shortest edge |
| `@radius(r)` | Circular edges with the specified radius (within tolerance) |

#### Grammar additions

```antlr
primary
  : UNIT_NUMBER
  | NUMBER
  | STRING
  | SELECTOR              // NEW
  | vectorLiteral
  | postfix
  | IDENT
  | '(' expr ')'
  ;

SELECTOR
  : '@' [a-zA-Z_] [a-zA-Z_0-9]*
  ;
```

Selectors that take arguments (like `@parallel` and `@radius`) use function-call syntax:

```
part.fillet(2mm, edges: @parallel([0, 0, 1]));
part.fillet(1mm, edges: @radius(5mm));
```

#### Implementation

An `EdgeSelector` class evaluates selector expressions against a shape:

```cpp
class EdgeSelector {
public:
    static std::vector<TopoDS_Edge> select(const TopoDS_Shape& shape,
                                           const std::string& selectorName,
                                           const std::vector<ValuePtr>& args = {});
private:
    static bool isParallel(const TopoDS_Edge& edge, const gp_Dir& dir, double tol = 1e-6);
    static bool isPerpendicular(const TopoDS_Edge& edge, const gp_Dir& dir, double tol = 1e-6);
    static double edgeLength(const TopoDS_Edge& edge);
    static std::optional<double> circularRadius(const TopoDS_Edge& edge);
};
```

The `fillet()` and `chamfer()` methods in `ShapeRegistry` check for an `edges:` named argument. If present, they use `EdgeSelector` to filter edges. If absent, they default to `@all`.

### 3.5 Sketch Blocks

Sketch blocks define 2D profiles on a workplane. The profile can then be extruded, revolved, or swept to create 3D geometry.

```
let profile = sketch(plane: "XY") {
    rect(40, 20);
    circle(8mm).translate([10, 0]);
};

let solid = profile.extrude(15mm);
let revolved = profile.revolve(axis: [1, 0, 0], angle: 180deg);
```

#### Sketch primitives

```
sketch(plane: "XY", origin: [0, 0, 0]) {
    // Rectangles
    rect(width, height);
    rect(width, height, center: true);           // centered at origin

    // Circles
    circle(radius);
    circle(radius, center: [x, y]);

    // Arcs
    arc(radius, start_angle, end_angle);

    // Lines and polylines
    line([x1, y1], [x2, y2]);
    polyline([[0,0], [10,0], [10,5], [0,5]]);

    // Polygons
    polygon(sides: 6, radius: 10mm);             // regular polygon

    // Text (for engraving)
    text("LABEL", size: 5mm, font: "mono");

    // Fillets and chamfers on 2D profiles
    fillet(radius, vertices: @all);
    chamfer(distance, vertices: @all);
}
```

#### Sketch boolean operations

Within a sketch block, overlapping 2D shapes are composed:

```
sketch(plane: "XY") {
    rect(40, 20);                        // additive
    -circle(5mm).translate([10, 5]);     // subtractive (hole)
    -circle(5mm).translate([30, 5]);     // another hole
};
```

Bare shapes are fused (2D union). Shapes preceded by `-` are cut (2D subtraction).

#### Workplane specification

The `plane` argument accepts:
- Predefined planes: `"XY"`, `"XZ"`, `"YZ"` (and lowercase variants)
- A face reference: `part.topFace()`
- Three points: `plane([0,0,0], [1,0,0], [0,1,0])`
- A point and normal: `plane(origin: [0,0,5], normal: [0,0,1])`

#### Extrusion and sweep operations on sketches

```
let part = profile.extrude(height);                          // linear extrusion along plane normal
let part = profile.extrude(height, draft: 5deg);             // with draft angle
let part = profile.extrude(height, symmetric: true);          // extrude equally both directions
let part = profile.revolve(axis: [0,0,1], angle: 360deg);    // full revolution
let part = profile.revolve(axis: [0,0,1], angle: 90deg);     // partial revolution
let part = profile.sweep(path);                               // sweep along a path curve
let part = profile.loft(other_profile);                       // loft between two profiles
```

#### Grammar additions

```antlr
sketchExpr
  : 'sketch' '(' argList? ')' '{' sketchStmt* '}'
  ;

sketchStmt
  : '-'? expr ';'
  | letStmt ';'
  | forStmt
  | ifStmt
  ;
```

The `primary` rule includes `sketchExpr`.

#### Implementation

Sketches are built using OCCT's 2D wire/face tools:

```cpp
class SketchBuilder {
public:
    SketchBuilder(const gp_Pln& workplane);

    void addRect(double w, double h, bool center = false);
    void addCircle(double radius, double cx = 0, double cy = 0);
    void addArc(double radius, double startAngle, double endAngle);
    void addPolyline(const std::vector<gp_Pnt2d>& points);
    void addPolygon(int sides, double radius);

    void subtractShape(const TopoDS_Shape& shape2d);
    void fuseShape(const TopoDS_Shape& shape2d);

    TopoDS_Face buildFace() const;       // composite 2D face
    ShapePtr extrude(double height, double draft = 0) const;
    ShapePtr revolve(const gp_Ax1& axis, double angle) const;

private:
    gp_Pln plane_;
    std::vector<TopoDS_Shape> additive_;
    std::vector<TopoDS_Shape> subtractive_;
};
```

### 3.6 Pattern Operations

Pattern methods replicate geometry in regular arrangements.

#### Linear pattern

```
let holes = cylinder(3mm, 20mm);
let row = holes.linear_pattern(dir: [1, 0, 0], count: 5, spacing: 10mm);
// Creates 5 copies spaced 10mm apart along the X axis

let grid = holes.linear_pattern(dir: [1, 0, 0], count: 5, spacing: 10mm)
               .linear_pattern(dir: [0, 1, 0], count: 3, spacing: 15mm);
// Creates a 5x3 grid
```

#### Circular pattern

```
let spoke = box(30, 5, 5).translate([15, 0, 0]);
let wheel = spoke.circular_pattern(axis: [0, 0, 1], count: 8);
// Creates 8 copies evenly spaced around the Z axis (45 degrees apart)

let partial = spoke.circular_pattern(axis: [0, 0, 1], count: 4, angle: 180deg);
// 4 copies spread over 180 degrees
```

#### Mirror pattern

```
let half = box(50, 80, 20).translate([25, 0, 0]);
let full = half.mirror(plane: "YZ");
// Mirrors across the YZ plane and fuses with original
```

#### Implementation

Pattern methods are registered in `ShapeRegistry`:

```cpp
registerMethod("linear_pattern", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
    // Extract named args: dir (vector), count (number), spacing (number)
    // For each i in 1..count-1, translate self by dir * spacing * i and fuse
});

registerMethod("circular_pattern", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
    // Extract named args: axis (vector), count (number), angle (number, default 360)
    // For each i in 1..count-1, rotate self by (angle/count)*i around axis and fuse
});

registerMethod("mirror", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
    // Extract named arg: plane (string "XY"/"XZ"/"YZ" or 3-element vector normal)
    // Mirror using BRepBuilderAPI_Transform with gp_Trsf::SetMirror
    // Fuse original with mirrored copy
});
```

---

## 4. DFM Module

### 4.1 Overview

The Design for Manufacturing (DFM) module provides automated analysis of geometry against manufacturing process constraints. It identifies potential manufacturing issues before export, allowing designers to fix problems early.

### 4.2 Process Profiles with `@process`

A `@process` block defines a named manufacturing profile with constraint parameters:

```
@process("fdm") {
    min_wall_thickness: 0.4mm,
    max_wall_thickness: 10.0mm,
    min_draft_angle: 0deg,
    max_overhang_angle: 45deg,
    min_radius: 0.2mm,
    layer_height: 0.2mm,
    nozzle_diameter: 0.4mm,
}

@process("injection_molding") {
    min_wall_thickness: 0.8mm,
    max_wall_thickness: 4.0mm,
    min_draft_angle: 1.0deg,
    min_radius: 0.25mm,
    max_overhang_angle: 0deg,
    undercut_allowed: false,
}

@process("cnc_milling") {
    min_radius: 1.5mm,
    min_wall_thickness: 0.5mm,
    max_depth_ratio: 4.0,
    min_tool_diameter: 3.0mm,
    min_draft_angle: 0deg,
}

@process("sheet_metal") {
    material_thickness: 1.5mm,
    min_bend_radius: 1.0mm,
    min_hole_diameter: 1.5mm,
    min_hole_edge_distance: 4.0mm,
    min_bend_edge_distance: 6.0mm,
}
```

Process profiles can be defined in `.dcad` files and imported, or loaded from the standard library. The standard library ships with common profiles for FDM, SLA, injection molding, CNC milling, and sheet metal.

### 4.3 DFM Check Invocation

DFM analysis is triggered with the `@dfm` annotation before an export statement:

```
@dfm("fdm") export body as enclosure;
```

This runs the analysis, prints warnings/errors to the console, and optionally writes a report. The export still proceeds unless `@dfm` is configured to block on errors:

```
@dfm("injection_molding", strict: true) export body as enclosure;
// In strict mode, export is blocked if any DFM errors are found
```

DFM analysis can also be invoked programmatically:

```
let issues = dfm_check(body, process: "fdm");
// issues is a list of DFMIssue values
for issue in issues {
    // issue.severity, issue.message, issue.location, issue.suggestion
}
```

### 4.4 DFM Checks

The following checks are performed based on the process profile:

#### Wall thickness analysis

Inspect all walls (pairs of opposing faces) and verify thickness is within `[min_wall_thickness, max_wall_thickness]`. Uses ray casting from face centroids along the inward normal to find opposing faces. Reports thin walls that may not print or mold correctly.

#### Draft angle analysis

For injection molding and other mold-based processes, analyze all faces that are not perpendicular to the parting direction. Faces with insufficient draft angle are flagged. The parting direction defaults to Z but can be specified:

```
@dfm("injection_molding", parting_dir: [0, 0, 1]) export body as enclosure;
```

#### Minimum radius analysis

Inspect all fillet and edge radii. Edges with radius below `min_radius` are flagged. Sharp edges (radius = 0) are flagged as errors for all processes.

#### Undercut detection

For injection molding, detect geometry features that prevent straight mold pull. A face is an undercut if its normal has a component opposing the parting direction and is occluded by other geometry.

#### Overhang angle analysis

For FDM/SLA printing, identify faces whose angle relative to the build plate (Z-up) exceeds `max_overhang_angle`. Overhangs beyond this angle may require support material.

#### Tool accessibility

For CNC milling, verify that all features can be reached by the minimum tool diameter. Internal corners with radius smaller than half the minimum tool diameter are flagged.

#### Sharp edge detection

Flag all edges with zero or near-zero fillet radius. Sharp edges cause stress concentrations in mechanical parts and are difficult to achieve in most manufacturing processes.

### 4.5 C++ Implementation

```cpp
namespace opendcad {

enum class DFMSeverity { INFO, WARNING, ERROR };

struct DFMIssue {
    DFMSeverity severity;
    std::string checkName;        // "wall_thickness", "draft_angle", etc.
    std::string message;          // human-readable description
    std::string suggestion;       // suggested fix
    TopoDS_Shape location;        // the offending face/edge/region
    double measuredValue;         // the actual value found
    double requiredValue;         // the constraint value
};

struct DFMProfile {
    std::string name;
    std::unordered_map<std::string, double> numericParams;
    std::unordered_map<std::string, bool> boolParams;

    double get(const std::string& key, double defaultVal = 0) const;
    bool getBool(const std::string& key, bool defaultVal = false) const;
};

class DFMAnalyzer {
public:
    explicit DFMAnalyzer(const DFMProfile& profile);

    std::vector<DFMIssue> analyze(const TopoDS_Shape& shape) const;

    // Individual checks (can be run selectively)
    std::vector<DFMIssue> checkWallThickness(const TopoDS_Shape& shape) const;
    std::vector<DFMIssue> checkDraftAngle(const TopoDS_Shape& shape,
                                          const gp_Dir& partingDir = gp::DZ()) const;
    std::vector<DFMIssue> checkMinRadius(const TopoDS_Shape& shape) const;
    std::vector<DFMIssue> checkUndercuts(const TopoDS_Shape& shape,
                                         const gp_Dir& partingDir = gp::DZ()) const;
    std::vector<DFMIssue> checkOverhangAngle(const TopoDS_Shape& shape) const;
    std::vector<DFMIssue> checkToolAccess(const TopoDS_Shape& shape) const;
    std::vector<DFMIssue> checkSharpEdges(const TopoDS_Shape& shape) const;

private:
    DFMProfile profile_;

    // Helpers
    double measureWallThickness(const TopoDS_Face& face,
                                const TopoDS_Shape& shape) const;
    double measureDraftAngle(const TopoDS_Face& face,
                             const gp_Dir& partingDir) const;
    std::vector<TopoDS_Edge> findSharpEdges(const TopoDS_Shape& shape,
                                            double minRadius) const;
};

class DFMProfileRegistry {
public:
    static DFMProfileRegistry& instance();

    void registerProfile(const std::string& name, const DFMProfile& profile);
    const DFMProfile& getProfile(const std::string& name) const;
    bool hasProfile(const std::string& name) const;

    void registerDefaults();  // register fdm, sla, injection_molding, cnc_milling, sheet_metal

private:
    std::unordered_map<std::string, DFMProfile> profiles_;
};

} // namespace opendcad
```

### 4.6 Grammar additions

```antlr
dfmAnnotation
  : '@dfm' '(' argList ')' exportStmt ';'
  ;

processDecl
  : '@process' '(' STRING ')' '{' processParam (',' processParam)* ','? '}'
  ;

processParam
  : IDENT ':' expr
  ;
```

The `stmt` rule includes `dfmAnnotation` and `processDecl`.

### 4.7 DFM Report Output

When DFM analysis is triggered, results are printed to the console using ANSI colors:

```
[DFM] Analyzing 'enclosure' against profile 'fdm'
  [WARN] Wall thickness: 0.35mm at face #12 (min: 0.4mm)
         Suggestion: Increase wall thickness to at least 0.4mm
  [WARN] Overhang angle: 52.3deg at face #7 (max: 45deg)
         Suggestion: Add support or reduce overhang angle
  [OK]   Draft angle: all faces pass
  [OK]   Minimum radius: all edges pass
  [OK]   Sharp edges: none found
[DFM] 2 warnings, 0 errors
```

In `strict` mode, any `ERROR`-level issues cause the export to fail with a nonzero exit code.

---

## 5. Standard Library & Enclosure Helpers

### 5.1 Connector Cutout Library (`std/connectors/`)

Pre-built modules for common electronic connectors. Each module produces a shape that can be cut from an enclosure wall to create a connector opening. All dimensions follow official connector specifications with configurable tolerances.

#### Available connectors

| Module | Function | Parameters |
|--------|----------|-----------|
| `std.connectors` | `usb_c()` | `tolerance = 0.2mm` |
| `std.connectors` | `usb_a()` | `tolerance = 0.2mm` |
| `std.connectors` | `usb_micro()` | `tolerance = 0.2mm` |
| `std.connectors` | `hdmi()` | `type = "standard"` (or `"mini"`, `"micro"`), `tolerance = 0.2mm` |
| `std.connectors` | `ethernet_rj45()` | `shielded = true`, `tolerance = 0.2mm` |
| `std.connectors` | `barrel_jack()` | `od = 5.5mm`, `id = 2.1mm`, `tolerance = 0.2mm` |
| `std.connectors` | `audio_35mm()` | `type = "stereo"` (or `"trrs"`), `tolerance = 0.2mm` |
| `std.connectors` | `sd_card()` | `type = "full"` (or `"micro"`), `tolerance = 0.2mm` |
| `std.connectors` | `db9()` | `tolerance = 0.3mm` |
| `std.connectors` | `iec_c14()` | `with_fuse = false`, `tolerance = 0.3mm` |

#### Usage

```
import std.connectors;

let wall = box(120, 3, 80);
let usb_hole = std.connectors.usb_c(tolerance: 0.3mm)
    .rotate([90, 0, 0])
    .translate([20, 0, 15]);
let result = wall.cut(usb_hole);
```

#### Implementation

Each connector module is a `.dcad` file in the standard library that defines a `module` returning the cutout shape. The USB-C cutout, for example:

```
// std/connectors/usb_c.dcad
module usb_c(tolerance = 0.2mm, depth = 10mm) {
    // USB-C opening: 8.94mm x 3.26mm with rounded ends
    let w = 8.94mm + tolerance * 2;
    let h = 3.26mm + tolerance * 2;
    let r = h / 2;

    let profile = sketch(plane: "XY") {
        rect(w - 2 * r, h, center: true);
        circle(r, center: [-(w/2 - r), 0]);
        circle(r, center: [(w/2 - r), 0]);
    };
    profile.extrude(depth, symmetric: true);
}
```

### 5.2 Display/Screen Cutout Library (`std/displays/`)

Pre-built modules for common display cutouts, including mounting holes.

| Module | Function | Notes |
|--------|----------|-------|
| `std.displays` | `lcd_1602()` | 16x2 character LCD, 80x36mm viewing area |
| `std.displays` | `oled_096()` | 0.96" OLED, 25.5x14mm viewing area |
| `std.displays` | `oled_13()` | 1.3" OLED, 30x16mm viewing area |
| `std.displays` | `tft_28()` | 2.8" TFT, 57x43mm viewing area |
| `std.displays` | `custom_lcd(w, h, mounting_holes)` | Custom dimensions |

Each display module returns a compound shape: the screen cutout plus optional mounting holes. Parameters control tolerance and whether mounting holes are included.

```
import std.displays;

let screen = std.displays.oled_096(
    tolerance: 0.3mm,
    mounting_holes: true,
    mounting_hole_diameter: 2.5mm
);
```

### 5.3 Switch/Button Cutout Library (`std/controls/`)

Pre-built modules for common controls and indicators.

| Module | Function | Parameters |
|--------|----------|-----------|
| `std.controls` | `pushbutton()` | `diameter = 12mm`, `type = "momentary"` |
| `std.controls` | `toggle_switch()` | `diameter = 6mm` |
| `std.controls` | `rocker_switch()` | `width = 21mm`, `height = 15mm` |
| `std.controls` | `potentiometer()` | `shaft_diameter = 7mm`, `body_diameter = 17mm` |
| `std.controls` | `led_bezel()` | `led_diameter = 5mm` (or `3mm`) |

```
import std.controls;

let button_hole = std.controls.pushbutton(diameter: 16mm, tolerance: 0.2mm);
let led_mount = std.controls.led_bezel(led_diameter: 3mm);
```

### 5.4 Enclosure Building Helpers (Built-in Methods)

These methods are registered directly in `ShapeRegistry` as methods on shapes. They provide common enclosure construction patterns.

#### `placeCorners` (existing)

Places copies of a shape at the four corners of the target shape's bounding box, offset inward by `xOffset` and `yOffset`. Already implemented.

```
let result = bin.placeCorners(boss, 5mm, 5mm);
```

#### `placeGrid`

Places copies of a shape in a rectangular grid pattern on the target shape's top face.

```
let result = plate.placeGrid(hole,
    cols: 5, rows: 3,
    col_spacing: 10mm, row_spacing: 10mm,
    margin: [10mm, 10mm]       // offset from edge
);
```

Implementation: compute bounding box of target, calculate start positions from margin, iterate and place copies via `translate` and `fuse`.

#### `placeRing`

Places copies of a shape in a circular pattern centered on the target shape.

```
let result = plate.placeRing(hole, count: 8, radius: 30mm);
```

Implementation: equivalent to calling `circular_pattern` on the placed shape.

#### `screwBoss`

Creates a cylindrical boss with a screw hole, suitable for self-tapping screws or heat-set inserts.

```
let boss = screwBoss(
    od: 8mm,               // outer diameter
    id: 2.5mm,             // inner diameter (screw hole)
    height: 12mm,
    chamfer_top: 0.5mm,    // optional chamfer on top
);
```

This is a factory function, not a method. It creates and returns a standalone shape.

#### `standoff`

Creates a cylindrical standoff (hollow or solid) for PCB mounting.

```
let post = standoff(
    od: 6mm,
    id: 3mm,               // 0 for solid
    height: 10mm,
    base_od: 8mm,          // optional flared base
    base_height: 1mm
);
```

#### `snapFit`

Creates a cantilever snap-fit feature.

```
let snap = snapFit(
    length: 10mm,
    width: 3mm,
    thickness: 1.5mm,
    hook_height: 0.8mm,
    hook_angle: 30deg
);
```

#### `pcbMount`

Generates a complete PCB mounting system: four standoffs positioned for a specified PCB size.

```
let mounts = pcbMount(
    pcb_width: 60mm,
    pcb_depth: 40mm,
    standoff_height: 5mm,
    hole_diameter: 2.5mm,
    hole_inset: 3mm          // distance from PCB edge to hole center
);
```

Returns a compound shape that can be fused with an enclosure base.

#### `ventSlots`

Creates a pattern of rectangular ventilation slots.

```
let vents = ventSlots(
    area_width: 40mm,
    area_height: 20mm,
    slot_width: 2mm,
    slot_spacing: 3mm,
    slot_depth: 5mm           // extrusion depth for cutting
);
```

#### `ventHoneycomb`

Creates a honeycomb pattern of hexagonal ventilation holes.

```
let vents = ventHoneycomb(
    area_width: 40mm,
    area_height: 30mm,
    cell_size: 4mm,
    wall_thickness: 1mm,
    depth: 5mm
);
```

#### `cableSlot`

Creates a rounded slot for cable routing.

```
let slot = cableSlot(
    width: 8mm,
    height: 4mm,
    depth: 5mm,
    radius: 2mm              // end cap radius
);
```

#### `ribGrid`

Creates a grid of reinforcing ribs on the interior of a shape.

```
let ribs = ribGrid(
    width: 60mm,
    depth: 40mm,
    height: 8mm,
    thickness: 1mm,
    spacing_x: 15mm,
    spacing_y: 15mm
);
```

#### `lipGroove`

Creates a mating lip-and-groove feature for enclosure halves.

```
let lip = lipGroove(
    width: 80mm,
    depth: 60mm,
    lip_height: 2mm,
    lip_width: 1.2mm,
    tolerance: 0.15mm,
    type: "lip"             // or "groove" for the mating part
);
```

#### `labelRecess`

Creates a recessed area for a label or badge on a surface.

```
let recess = labelRecess(
    width: 30mm,
    height: 10mm,
    depth: 0.5mm,
    corner_radius: 1mm
);
```

### 5.5 Fastener Library (`std/fasteners/`)

Parametric fastener features based on standard specifications (ISO/ANSI).

#### `clearance_hole`

Creates a through-hole for a bolt or screw to pass through freely.

```
import std.fasteners;

let hole = std.fasteners.clearance_hole(
    size: "M3",                 // ISO metric size
    fit: "normal",              // "close", "normal", or "loose"
    depth: 10mm
);
// M3 normal clearance: 3.4mm diameter
```

Clearance table (subset):

| Size | Close fit | Normal fit | Loose fit |
|------|-----------|------------|-----------|
| M2 | 2.2mm | 2.4mm | 2.6mm |
| M2.5 | 2.7mm | 2.9mm | 3.1mm |
| M3 | 3.2mm | 3.4mm | 3.6mm |
| M4 | 4.3mm | 4.5mm | 4.8mm |
| M5 | 5.3mm | 5.5mm | 5.8mm |
| M6 | 6.4mm | 6.6mm | 7.0mm |
| M8 | 8.4mm | 9.0mm | 10.0mm |

#### `tap_drill`

Creates a hole sized for thread tapping (75% thread engagement).

```
let hole = std.fasteners.tap_drill(size: "M3", depth: 8mm);
// M3 tap drill: 2.5mm diameter
```

#### `boss_od`

Returns the recommended outer diameter for a screw boss given a screw size.

```
let od = std.fasteners.boss_od(size: "M3");
// Returns 6.0mm (2x screw diameter)
```

This is a value-returning function, not a shape factory.

#### `head_recess`

Creates a counterbore or countersink for a screw head.

```
let cbore = std.fasteners.head_recess(
    size: "M3",
    type: "socket_head",        // or "flat_head", "pan_head", "button_head"
    depth: 3.5mm                // defaults to head height
);
```

#### `nut_trap`

Creates a hexagonal pocket for capturing a nut.

```
let trap = std.fasteners.nut_trap(
    size: "M3",
    depth: 2.4mm,              // defaults to nut height
    tolerance: 0.2mm
);
```

#### `heat_set_hole`

Creates a hole sized for heat-set threaded inserts. Dimensions are based on common insert manufacturer specifications.

```
let insert_hole = std.fasteners.heat_set_hole(
    size: "M3",
    length: "short",           // "short", "standard", or "long"
);
// M3 short: 4.0mm diameter, 4.5mm depth
```

Heat-set insert table (subset):

| Size | Length | Hole diameter | Hole depth |
|------|--------|---------------|------------|
| M2 | short | 3.2mm | 3.0mm |
| M2 | standard | 3.2mm | 4.0mm |
| M3 | short | 4.0mm | 4.5mm |
| M3 | standard | 4.0mm | 5.7mm |
| M3 | long | 4.0mm | 8.0mm |
| M4 | short | 5.6mm | 5.0mm |
| M4 | standard | 5.6mm | 8.0mm |
| M5 | short | 6.4mm | 7.0mm |
| M5 | standard | 6.4mm | 9.5mm |

### 5.6 User Module Workflow and Import Resolution

#### Import resolution order (detailed)

When the evaluator encounters an `import` statement, the `ImportResolver` resolves the path in this order:

1. **Relative to current file:** If the import path starts with `./` or `../`, resolve relative to the directory containing the importing file. Only this resolution is attempted.

2. **Bare file path (e.g., `"utils.dcad"`):**
   - a. Relative to current file directory.
   - b. User library: `$OPENDCAD_LIB/utils.dcad`. The `OPENDCAD_LIB` environment variable defaults to `~/.opendcad/lib/`.
   - c. System library: `/usr/local/share/opendcad/lib/utils.dcad`.

3. **Standard library (e.g., `std.fasteners`):**
   - Map `std.fasteners` to `<install_dir>/std/fasteners/mod.dcad`.
   - Each standard library module has a `mod.dcad` that re-exports all public names.

4. If not found in any location, raise `EvalError("module not found: <path>")`.

#### Module file structure

A module can be a single `.dcad` file or a directory with a `mod.dcad` entry point:

```
std/
  fasteners/
    mod.dcad              # re-exports: import { clearance_hole, ... } from "./clearance.dcad"
    clearance.dcad
    tap_drill.dcad
    heat_set.dcad
    tables.dcad           # data tables (not re-exported, internal)
  connectors/
    mod.dcad
    usb_c.dcad
    usb_a.dcad
    hdmi.dcad
    ...
```

#### Publishing user modules

Users can create reusable modules by placing `.dcad` files in their library directory:

```
~/.opendcad/lib/
  my_enclosure_style.dcad
  my_connectors/
    mod.dcad
    custom_port.dcad
```

These can then be imported from any project:

```
import "my_enclosure_style.dcad";
import "my_connectors" as mc;
```

---

## 6. Complete Expression Precedence Table

The full expression grammar, from lowest to highest precedence:

| Precedence | Operator | Associativity | Description |
|------------|----------|---------------|-------------|
| 1 (lowest) | `if ... then ... else ...` | right | ternary conditional |
| 2 | `\|\|` | left | logical OR |
| 3 | `&&` | left | logical AND |
| 4 | `==` `!=` | left | equality |
| 5 | `<` `<=` `>` `>=` | left | comparison |
| 6 | `+` `-` | left | addition, subtraction |
| 7 | `*` `/` `%` | left | multiplication, division, modulo |
| 8 | `-` (unary) `!` (unary) | right | negation, logical NOT |
| 9 | `.method()` | left | method call chain |
| 10 (highest) | `()` `[]` literals | n/a | grouping, vectors, primaries |

---

## 7. Complete Grammar (ANTLR4)

The full grammar incorporating all Phase 3 features. This extends the Phase 0 grammar.

```antlr
grammar OpenDCAD;

// ==================== Parser Rules ====================

program
  : stmt* EOF
  ;

stmt
  : letStmt ';'
  | constStmt ';'
  | assignStmt ';'
  | exportStmt ';'
  | ifStmt
  | forStmt
  | fnDecl
  | moduleDecl
  | returnStmt
  | importStmt
  | processDecl
  | dfmAnnotation
  | blockStmt
  | exprStmt ';'
  ;

// ---------- Bindings ----------

letStmt
  : 'let' IDENT '=' expr
  ;

constStmt
  : 'const' IDENT '=' expr
  ;

assignStmt
  : IDENT '=' expr
  ;

// ---------- Export ----------

exportStmt
  : 'export' expr 'as' IDENT
  ;

// ---------- Control flow ----------

ifStmt
  : 'if' expr blockStmt ('else' 'if' expr blockStmt)* ('else' blockStmt)?
  ;

forStmt
  : 'for' IDENT 'in' rangeExpr blockStmt
  ;

rangeExpr
  : expr '..' expr             # exclusiveRange
  | expr '..=' expr            # inclusiveRange
  | expr                       # iterableExpr
  ;

returnStmt
  : 'return' expr? ';'
  ;

// ---------- Functions ----------

fnDecl
  : 'fn' IDENT '(' paramList? ')' blockStmt
  ;

moduleDecl
  : 'module' IDENT '(' paramList? ')' blockStmt
  ;

paramList
  : param (',' param)*
  ;

param
  : IDENT ('=' expr)?
  ;

// ---------- Imports ----------

importStmt
  : 'import' STRING ';'                                      # importAll
  | 'import' STRING 'as' IDENT ';'                           # importAs
  | 'import' '{' identList '}' 'from' STRING ';'             # importSelectiveFile
  | 'import' qualifiedName ';'                                # importStdAll
  | 'import' qualifiedName 'as' IDENT ';'                    # importStdAs
  | 'import' '{' identList '}' 'from' qualifiedName ';'      # importStdSelective
  ;

qualifiedName
  : IDENT ('.' IDENT)+
  ;

identList
  : IDENT (',' IDENT)*
  ;

// ---------- DFM ----------

processDecl
  : '@process' '(' STRING ')' '{' processParam (',' processParam)* ','? '}'
  ;

processParam
  : IDENT ':' expr
  ;

dfmAnnotation
  : '@dfm' '(' argList ')' exportStmt ';'
  ;

// ---------- Blocks ----------

blockStmt
  : '{' stmt* '}'
  ;

exprStmt
  : '-'? expr
  ;

// ---------- Expressions ----------

expr
  : 'if' expr 'then' expr 'else' expr                        # ternaryExpr
  | expr '||' expr                                            # logicOr
  | expr '&&' expr                                            # logicAnd
  | expr ('==' | '!=') expr                                   # equality
  | expr ('<' | '<=' | '>' | '>=') expr                       # comparison
  | expr ('+' | '-') expr                                     # addSub
  | expr ('*' | '/' | '%') expr                               # mulDivMod
  | '-' expr                                                  # unaryNeg
  | '!' expr                                                  # unaryNot
  | postfix                                                   # postfixExpr
  | primary                                                   # primaryExpr
  ;

postfix
  : call ('.' methodCall)*                                    # postFromCall
  | IDENT ('.' methodCall)+                                   # postFromVar
  ;

call
  : IDENT '(' argList? ')'
  ;

methodCall
  : IDENT '(' argList? ')'
  ;

// ---------- Arguments ----------

argList
  : arg (',' arg)*
  ;

arg
  : IDENT ':' expr                                            # namedArg
  | expr                                                      # positionalArg
  ;

// ---------- Sketch ----------

sketchExpr
  : 'sketch' '(' argList? ')' '{' sketchStmt* '}'
  ;

sketchStmt
  : '-'? expr ';'
  | letStmt ';'
  | forStmt
  | ifStmt
  ;

// ---------- Primaries ----------

primary
  : UNIT_NUMBER
  | NUMBER
  | STRING
  | BOOL_LITERAL
  | SELECTOR
  | vectorLiteral
  | sketchExpr
  | IDENT
  | '(' expr ')'
  ;

vectorLiteral
  : '[' expr (',' expr)* ']'
  ;

// ==================== Lexer Rules ====================

BOOL_LITERAL
  : 'true' | 'false'
  ;

SELECTOR
  : '@' [a-zA-Z_] [a-zA-Z_0-9]*
  ;

UNIT_NUMBER
  : NUMBER_BODY UNIT_SUFFIX
  ;

NUMBER
  : NUMBER_BODY
  ;

fragment NUMBER_BODY
  : [0-9]+ ('.' [0-9]+)? ( [eE] [+-]? [0-9]+ )?
  | '.' [0-9]+ ( [eE] [+-]? [0-9]+ )?
  ;

fragment UNIT_SUFFIX
  : 'mm' | 'cm' | 'thou' | 'ft' | 'in' | 'deg' | 'rad' | 'm'
  ;

STRING
  : '"' (~["\\\r\n] | '\\' .)* '"'
  ;

IDENT
  : [a-zA-Z_] [a-zA-Z_0-9]*
  ;

WS
  : [ \t\r\n]+ -> channel(HIDDEN)
  ;

LINE_COMMENT
  : '//' ~[\r\n]* -> channel(HIDDEN)
  ;

BLOCK_COMMENT
  : '/*' .*? '*/' -> channel(HIDDEN)
  ;
```

**Note on UNIT_SUFFIX ordering:** The `UNIT_SUFFIX` fragment lists `mm` before `m` to ensure that `5mm` is matched as `UNIT_NUMBER` with suffix `mm`, not as `UNIT_NUMBER` with suffix `m` followed by identifier `m`. ANTLR's maximal munch rule handles this correctly: the longest possible token match wins, so `5mm` matches `UNIT_NUMBER` (with `mm` suffix) rather than `NUMBER` `5` + `IDENT` `mm`.

---

## 8. Complete Example Programs

### 8.1 Parametric Electronics Enclosure

A two-part snap-fit enclosure with screw bosses, ventilation, cable slot, and DFM validation.

```
// parametric_enclosure.dcad
// A parametric electronics enclosure with snap-fit lid

import std.fasteners;
import std.connectors;
import std.controls;

// ---------- Parameters ----------
const WIDTH = 100mm;
const DEPTH = 70mm;
const HEIGHT = 35mm;
const WALL = 2mm;
const FILLET_R = 1.5mm;

const PCB_W = 80mm;
const PCB_D = 50mm;
const PCB_STANDOFF_H = 5mm;
const PCB_HOLE_INSET = 3.5mm;

const SCREW_SIZE = "M3";
const SCREW_BOSS_OD = std.fasteners.boss_od(size: SCREW_SIZE);
const SCREW_BOSS_H = HEIGHT - WALL;

// ---------- Base shell ----------
let base_outer = box(WIDTH, DEPTH, HEIGHT);
let base_inner = box(WIDTH - 2 * WALL, DEPTH - 2 * WALL, HEIGHT - WALL)
    .translate([0, 0, WALL]);
let base = base_outer.cut(base_inner).fillet(FILLET_R);

// ---------- Screw bosses ----------
let boss = screwBoss(od: SCREW_BOSS_OD, id: 2.5mm, height: SCREW_BOSS_H);
let boss_x = WIDTH / 2 - WALL - SCREW_BOSS_OD / 2 - 1mm;
let boss_y = DEPTH / 2 - WALL - SCREW_BOSS_OD / 2 - 1mm;
base = base.placeCorners(boss, boss_x, boss_y);

// ---------- PCB standoffs ----------
let mounts = pcbMount(
    pcb_width: PCB_W,
    pcb_depth: PCB_D,
    standoff_height: PCB_STANDOFF_H,
    hole_diameter: 2.5mm,
    hole_inset: PCB_HOLE_INSET
);
base = base.fuse(mounts.translate([0, 0, WALL]));

// ---------- Ventilation slots on bottom ----------
let vents = ventSlots(
    area_width: 40mm,
    area_height: 30mm,
    slot_width: 1.5mm,
    slot_spacing: 3mm,
    slot_depth: WALL + 1mm
);
base = base.cut(vents.translate([0, 0, -0.5mm]));

// ---------- Connector cutouts on back wall ----------
let usb = std.connectors.usb_c(tolerance: 0.3mm)
    .rotate([90, 0, 0])
    .translate([-(WIDTH / 2 - 25mm), -(DEPTH / 2), PCB_STANDOFF_H + WALL + 3mm]);

let barrel = std.connectors.barrel_jack(tolerance: 0.3mm)
    .rotate([90, 0, 0])
    .translate([WIDTH / 2 - 20mm, -(DEPTH / 2), PCB_STANDOFF_H + WALL + 5mm]);

base = base.cut(usb).cut(barrel);

// ---------- LED bezel on front wall ----------
let led = std.controls.led_bezel(led_diameter: 3mm)
    .rotate([-90, 0, 0])
    .translate([WIDTH / 2 - 15mm, DEPTH / 2, PCB_STANDOFF_H + WALL + 5mm]);

base = base.cut(led);

// ---------- Cable slot on side wall ----------
let cable = cableSlot(width: 8mm, height: 5mm, depth: WALL + 2mm, radius: 2mm)
    .rotate([0, 0, 90])
    .translate([WIDTH / 2, 0, 5mm]);
base = base.cut(cable);

// ---------- Lip for lid mating ----------
let lip = lipGroove(
    width: WIDTH - 2 * WALL,
    depth: DEPTH - 2 * WALL,
    lip_height: 2mm,
    lip_width: 1.2mm,
    tolerance: 0.15mm,
    type: "groove"
);
base = base.fuse(lip.translate([0, 0, HEIGHT - WALL]));

// ---------- DFM check and export ----------
@process("fdm") {
    min_wall_thickness: 0.8mm,
    max_overhang_angle: 50deg,
    min_radius: 0.2mm,
}

@dfm("fdm") export base as enclosure_base;
```

### 8.2 Battery Holder (Evolution of battery.dcad)

An evolved version of the existing battery.dcad example, demonstrating loops, functions, parameterization, and named arguments.

```
// battery_holder.dcad
// Parametric battery holder for AA/AAA/18650 cells

import std.fasteners;

// ---------- Battery specifications ----------
fn battery_dims(type) {
    // Returns [diameter, length] for common battery types
    if type == "AA" {
        return [14.5mm, 50.5mm];
    } else if type == "AAA" {
        return [10.5mm, 44.5mm];
    } else if type == "18650" {
        return [18.6mm, 65.3mm];
    } else {
        return [14.5mm, 50.5mm];    // default to AA
    }
}

// ---------- Parameters ----------
const BATTERY_TYPE = "18650";
const CELL_COUNT = 4;
const CELL_SPACING = 2mm;
const WALL = 2mm;
const BASE_THICKNESS = 2.5mm;
const FILLET = 0.8mm;
const CONTACT_RECESS = 1.5mm;

let dims = battery_dims(BATTERY_TYPE);
const CELL_D = dims[0];
const CELL_L = dims[1];
const TOLERANCE = 0.5mm;

// ---------- Computed dimensions ----------
const SLOT_D = CELL_D + TOLERANCE;
const BIN_W = SLOT_D + 2 * WALL;
const BIN_D = CELL_COUNT * (SLOT_D + CELL_SPACING) - CELL_SPACING + 2 * WALL;
const BIN_H = SLOT_D / 2 + BASE_THICKNESS + 3mm;

// ---------- Base bin ----------
let bin = bin(BIN_W, BIN_D, BIN_H, WALL).fillet(FILLET);

// ---------- Battery slots ----------
fn battery_slot(diameter, length) {
    let slot = cylinder(diameter / 2, length + 2 * CONTACT_RECESS)
        .rotate([0, 90, 0])
        .translate([0, 0, diameter / 2 + BASE_THICKNESS]);
    return slot;
}

let slot = battery_slot(SLOT_D, CELL_L);
let slot_start_y = -(BIN_D / 2) + WALL + SLOT_D / 2;

for i in 0..CELL_COUNT {
    let y_pos = slot_start_y + i * (SLOT_D + CELL_SPACING);
    bin = bin.cut(slot.translate([0, y_pos, 0]));
}

// ---------- Contact spring recesses ----------
fn contact_recess(diameter, depth) {
    return cylinder(diameter / 2 - 1mm, depth)
        .rotate([0, 90, 0]);
}

let recess = contact_recess(SLOT_D, CONTACT_RECESS + 1mm);
for i in 0..CELL_COUNT {
    let y_pos = slot_start_y + i * (SLOT_D + CELL_SPACING);
    let z_pos = SLOT_D / 2 + BASE_THICKNESS;
    // Positive contact (left side)
    bin = bin.cut(recess.translate([-(CELL_L / 2 + CONTACT_RECESS), y_pos, z_pos]));
    // Negative contact (right side)
    bin = bin.cut(recess.translate([(CELL_L / 2 + CONTACT_RECESS), y_pos, z_pos]));
}

// ---------- Wire channel in base ----------
let channel = box(BIN_W - 4 * WALL, BIN_D - 2 * WALL, BASE_THICKNESS + 1mm)
    .translate([0, 0, -0.5mm]);
let channel_cut = box(BIN_W - 6 * WALL, BIN_D - 4 * WALL, BASE_THICKNESS)
    .translate([0, 0, 0]);
let wire_channel = channel.cut(channel_cut);
// Skip wire channel for simplicity -- would need more complex geometry

// ---------- Mounting holes ----------
let mount_hole = std.fasteners.clearance_hole(
    size: "M3",
    fit: "normal",
    depth: BASE_THICKNESS + 2mm
);
let mount_x = BIN_W / 2 - WALL - 2mm;
let mount_y = BIN_D / 2 - WALL - 5mm;
bin = bin.placeCorners(mount_hole, mount_x, mount_y);

// ---------- Retention clips ----------
fn retention_clip(width, height, thickness) {
    let clip = box(thickness, width, height)
        .fillet(0.3mm);
    return clip;
}

let clip = retention_clip(8mm, SLOT_D / 2 + 2mm, 1.5mm);
for i in 0..CELL_COUNT {
    let y_pos = slot_start_y + i * (SLOT_D + CELL_SPACING);
    let z_pos = BASE_THICKNESS + SLOT_D / 2;
    bin = bin.fuse(clip.translate([BIN_W / 2 - WALL, y_pos, z_pos]));
    bin = bin.fuse(clip.translate([-(BIN_W / 2 - WALL), y_pos, z_pos]));
}

// ---------- Export ----------
export bin as battery_holder;
```

### 8.3 CNC Bracket with Sketch/Extrude Workflow

A machined L-bracket demonstrating sketch-based modeling, named arguments, and CNC-specific DFM checks.

```
// cnc_bracket.dcad
// CNC-machined L-bracket with mounting holes and reinforcing gusset

import std.fasteners;

// ---------- Parameters ----------
const MATERIAL_THICKNESS = 6mm;
const BASE_W = 80mm;
const BASE_D = 50mm;
const UPRIGHT_H = 60mm;
const FILLET_R = 2mm;
const GUSSET_SIZE = 25mm;

// ---------- Base plate ----------
let base = sketch(plane: "XY") {
    rect(BASE_W, BASE_D, center: true);
}.extrude(MATERIAL_THICKNESS);

// ---------- Upright ----------
let upright = sketch(plane: "XZ", origin: [0, -(BASE_D / 2), MATERIAL_THICKNESS]) {
    rect(BASE_W, UPRIGHT_H, center: false);
}.extrude(MATERIAL_THICKNESS);

// ---------- Gusset (triangular reinforcement) ----------
let gusset_profile = sketch(plane: "YZ", origin: [BASE_W / 2 - MATERIAL_THICKNESS, -(BASE_D / 2), MATERIAL_THICKNESS]) {
    polyline([
        [0, 0],
        [0, GUSSET_SIZE],
        [GUSSET_SIZE, 0],
        [0, 0]
    ]);
}.extrude(MATERIAL_THICKNESS);

let gusset_right = gusset_profile;
let gusset_left = gusset_profile.mirror(plane: "YZ");

// ---------- Assemble L-bracket ----------
let bracket = base.fuse(upright).fuse(gusset_right).fuse(gusset_left);

// ---------- Fillet the main inside corner ----------
bracket = bracket.fillet(FILLET_R, edges: @parallel([1, 0, 0]));

// ---------- Base mounting holes ----------
let base_hole = std.fasteners.clearance_hole(
    size: "M6",
    fit: "normal",
    depth: MATERIAL_THICKNESS + 2mm
);

let base_cbore = std.fasteners.head_recess(
    size: "M6",
    type: "socket_head"
);

// 4 holes in a rectangular pattern on the base
let hole_x = BASE_W / 2 - 12mm;
let hole_y = BASE_D / 2 - 12mm;
bracket = bracket.placeCorners(base_hole, hole_x, hole_y);
bracket = bracket.placeCorners(base_cbore, hole_x, hole_y);

// ---------- Upright mounting slots ----------
fn mounting_slot(width, height, depth) {
    let slot_profile = sketch(plane: "XY") {
        rect(width, height, center: true);
        circle(height / 2, center: [-(width / 2 - height / 2), 0]);
        circle(height / 2, center: [(width / 2 - height / 2), 0]);
    };
    return slot_profile.extrude(depth);
}

let slot = mounting_slot(width: 15mm, height: 6.5mm, depth: MATERIAL_THICKNESS + 2mm);

// Two horizontal slots on the upright face
let slot_z1 = MATERIAL_THICKNESS + UPRIGHT_H * 0.3;
let slot_z2 = MATERIAL_THICKNESS + UPRIGHT_H * 0.7;
let slot_y = -(BASE_D / 2) - 0.5mm;

bracket = bracket.cut(
    slot.rotate([90, 0, 0]).translate([0, slot_y, slot_z1])
);
bracket = bracket.cut(
    slot.rotate([90, 0, 0]).translate([0, slot_y, slot_z2])
);

// ---------- Weight reduction pocket on base ----------
let pocket = sketch(plane: "XY") {
    rect(BASE_W - 30mm, BASE_D - 20mm, center: true);
    -rect(BASE_W - 34mm, BASE_D - 24mm, center: true);
}.extrude(MATERIAL_THICKNESS / 2);
// Skip the pocket if material is thin
if MATERIAL_THICKNESS >= 8mm {
    bracket = bracket.cut(pocket.translate([0, 0, MATERIAL_THICKNESS / 2]));
}

// ---------- Chamfer sharp edges on bottom ----------
bracket = bracket.chamfer(0.5mm, edges: @bottomFace);

// ---------- DFM analysis ----------
@process("cnc_milling") {
    min_radius: 1.5mm,
    min_wall_thickness: 1.0mm,
    max_depth_ratio: 4.0,
    min_tool_diameter: 3.0mm,
}

@dfm("cnc_milling") export bracket as l_bracket;
```

### 8.4 Multi-Part Assembly with Imports and Standard Library

A Raspberry Pi case demonstrating multi-file imports, the connector library, and assembly composition.

```
// rpi_case.dcad
// Raspberry Pi 4 case -- bottom shell + top lid with snap-fit

import std.connectors;
import std.controls;
import std.displays;
import std.fasteners;
import "common/enclosure_style.dcad" as style;

// ---------- Pi 4 board dimensions ----------
const PCB_W = 85mm;
const PCB_D = 56mm;
const PCB_H = 1.6mm;
const PCB_HOLE_INSET_X = 3.5mm;
const PCB_HOLE_INSET_Y = 3.5mm;
const PCB_HOLE_DIA = 2.75mm;

// ---------- Case parameters ----------
const WALL = style.WALL_THICKNESS;     // from imported style module
const CLEARANCE = 1.5mm;
const CASE_W = PCB_W + 2 * (WALL + CLEARANCE);
const CASE_D = PCB_D + 2 * (WALL + CLEARANCE);
const BOTTOM_H = 15mm;
const TOP_H = 10mm;
const STANDOFF_H = 3mm;

// ===============================================
// Bottom shell
// ===============================================
module bottom_shell() {
    // Outer shell
    let shell = bin(CASE_W, CASE_D, BOTTOM_H, WALL)
        .fillet(style.FILLET_RADIUS);

    // PCB standoffs
    let standoff = standoff(od: 6mm, id: PCB_HOLE_DIA, height: STANDOFF_H);
    let sx = PCB_W / 2 - PCB_HOLE_INSET_X;
    let sy = PCB_D / 2 - PCB_HOLE_INSET_Y;
    let base = shell.placeCorners(standoff.translate([0, 0, WALL]), sx, sy);

    // Additional Pi4 standoff (asymmetric hole pattern)
    let extra_standoff = standoff.translate([
        PCB_W / 2 - PCB_HOLE_INSET_X,
        -(PCB_D / 2 - PCB_HOLE_INSET_Y - 49mm),
        WALL
    ]);
    base = base.fuse(extra_standoff);

    // ---------- Connector cutouts (left side: USB, Ethernet) ----------
    let eth = std.connectors.ethernet_rj45(tolerance: 0.3mm)
        .rotate([0, 0, 90])
        .translate([CASE_W / 2, -(CASE_D / 2 - 10.25mm - CLEARANCE - WALL),
                    WALL + STANDOFF_H + PCB_H + 2mm]);

    let usb1 = std.connectors.usb_a(tolerance: 0.3mm)
        .rotate([0, 0, 90])
        .translate([CASE_W / 2, -(CASE_D / 2 - 29mm - CLEARANCE - WALL),
                    WALL + STANDOFF_H + PCB_H + 2mm]);

    let usb2 = std.connectors.usb_a(tolerance: 0.3mm)
        .rotate([0, 0, 90])
        .translate([CASE_W / 2, -(CASE_D / 2 - 47mm - CLEARANCE - WALL),
                    WALL + STANDOFF_H + PCB_H + 2mm]);

    base = base.cut(eth).cut(usb1).cut(usb2);

    // ---------- Connector cutouts (short side: USB-C power, micro HDMI) ----------
    let usb_power = std.connectors.usb_c(tolerance: 0.3mm)
        .rotate([90, 0, 0])
        .translate([-(CASE_W / 2 - 11.2mm - CLEARANCE - WALL),
                    -(CASE_D / 2),
                    WALL + STANDOFF_H + PCB_H + 1mm]);

    let hdmi1 = std.connectors.hdmi(type: "micro", tolerance: 0.3mm)
        .rotate([90, 0, 0])
        .translate([-(CASE_W / 2 - 26mm - CLEARANCE - WALL),
                    -(CASE_D / 2),
                    WALL + STANDOFF_H + PCB_H + 1mm]);

    let hdmi2 = std.connectors.hdmi(type: "micro", tolerance: 0.3mm)
        .rotate([90, 0, 0])
        .translate([-(CASE_W / 2 - 39.5mm - CLEARANCE - WALL),
                    -(CASE_D / 2),
                    WALL + STANDOFF_H + PCB_H + 1mm]);

    base = base.cut(usb_power).cut(hdmi1).cut(hdmi2);

    // ---------- Audio jack cutout ----------
    let audio = std.connectors.audio_35mm(tolerance: 0.3mm)
        .rotate([90, 0, 0])
        .translate([-(CASE_W / 2 - 54mm - CLEARANCE - WALL),
                    -(CASE_D / 2),
                    WALL + STANDOFF_H + PCB_H + 2mm]);

    base = base.cut(audio);

    // ---------- SD card slot (opposite short side) ----------
    let sd = std.connectors.sd_card(type: "micro", tolerance: 0.3mm)
        .rotate([-90, 0, 0])
        .translate([-(CASE_W / 2 - 24mm - CLEARANCE - WALL),
                    CASE_D / 2,
                    WALL + STANDOFF_H - 1mm]);

    base = base.cut(sd);

    // ---------- Bottom ventilation ----------
    let vents = ventHoneycomb(
        area_width: CASE_W - 20mm,
        area_height: CASE_D - 20mm,
        cell_size: 3mm,
        wall_thickness: 0.8mm,
        depth: WALL + 1mm
    );
    base = base.cut(vents.translate([0, 0, -0.5mm]));

    // ---------- Lip for lid mating ----------
    let groove = lipGroove(
        width: CASE_W - 2 * WALL,
        depth: CASE_D - 2 * WALL,
        lip_height: 2mm,
        lip_width: 1.2mm,
        tolerance: 0.15mm,
        type: "groove"
    );
    base = base.fuse(groove.translate([0, 0, BOTTOM_H - WALL]));

    base;   // implicit return for module
}

// ===============================================
// Top lid
// ===============================================
module top_lid() {
    let lid = bin(CASE_W, CASE_D, TOP_H, WALL)
        .fillet(style.FILLET_RADIUS)
        .flip();     // flip so open side faces down

    // Mating lip
    let lip = lipGroove(
        width: CASE_W - 2 * WALL,
        depth: CASE_D - 2 * WALL,
        lip_height: 2mm,
        lip_width: 1.2mm,
        tolerance: 0.15mm,
        type: "lip"
    );
    lid = lid.fuse(lip);

    // Top ventilation (CPU area)
    let top_vents = ventHoneycomb(
        area_width: 30mm,
        area_height: 30mm,
        cell_size: 3mm,
        wall_thickness: 0.8mm,
        depth: WALL + 1mm
    );
    lid = lid.cut(top_vents.translate([10mm, 5mm, TOP_H - WALL - 0.5mm]));

    // Status LED window
    let led_window = std.controls.led_bezel(led_diameter: 3mm)
        .translate([-(CASE_W / 2 - 8mm), CASE_D / 2 - 15mm, TOP_H - WALL]);
    lid = lid.cut(led_window);

    // Label recess on top
    let label = labelRecess(
        width: 40mm,
        height: 12mm,
        depth: 0.5mm,
        corner_radius: 2mm
    );
    lid = lid.cut(label.translate([0, -(CASE_D / 4), TOP_H]));

    lid;
}

// ===============================================
// Assemble and export
// ===============================================
let bottom = bottom_shell();
let top = top_lid().translate([0, 0, BOTTOM_H]);

@process("fdm") {
    min_wall_thickness: 0.8mm,
    max_overhang_angle: 50deg,
    min_radius: 0.2mm,
    layer_height: 0.2mm,
}

// Export parts separately for printing
@dfm("fdm") export bottom as rpi4_case_bottom;
@dfm("fdm") export top as rpi4_case_top;

// Export assembled view
export bottom.fuse(top) as rpi4_case_assembly;
```

---

## 9. Implementation Roadmap

Phase 3 is a large language expansion. The implementation should be staged:

### Stage 3a: Core Language (estimated: 2-3 weeks)

| Step | Component | Dependencies |
|------|-----------|-------------|
| 1 | `const` bindings, `assign` statement | Environment changes |
| 2 | Block scoping `{ }` | Environment child scopes |
| 3 | Comparison and logical operators | Value class extensions |
| 4 | `if` / `else if` / `else` statements | Evaluator additions |
| 5 | Ternary expressions | Evaluator additions |
| 6 | `for` loops with `..` ranges | Evaluator, range iterator |
| 7 | `range()` built-in function | Built-in function registry |
| 8 | `fn` declarations with `return` | Function values, ReturnSignal |
| 9 | `module` declarations | Shape collector context |
| 10 | Default parameters and named arguments | Arg resolution in registry |

### Stage 3b: Units and Geometry (estimated: 2-3 weeks)

| Step | Component | Dependencies |
|------|-----------|-------------|
| 11 | Unit suffix lexing and conversion | UnitConverter, grammar change |
| 12 | `intersect` boolean operation | Shape.cpp, ShapeRegistry |
| 13 | Face selection (`.face()`, `.topFace()`, etc.) | FaceSelector class |
| 14 | Edge selectors (`@vertical`, `@topFace`, etc.) | EdgeSelector class |
| 15 | `fillet` / `chamfer` with edge selectors | ShapeRegistry update |
| 16 | `linear_pattern`, `circular_pattern`, `mirror` | ShapeRegistry methods |
| 17 | Sketch blocks (basic: `rect`, `circle`) | SketchBuilder class |
| 18 | Sketch extrude / revolve | SketchBuilder extensions |
| 19 | Advanced sketch primitives (polyline, polygon, arc) | SketchBuilder extensions |

### Stage 3c: Imports and Standard Library (estimated: 2-3 weeks)

| Step | Component | Dependencies |
|------|-----------|-------------|
| 20 | `import` statement (file-based) | ImportResolver class |
| 21 | Namespace imports (`import ... as name`) | Namespace value type |
| 22 | Selective imports (`import { a, b } from ...`) | ImportResolver |
| 23 | Standard library path resolution (`std.*`) | ImportResolver |
| 24 | `std/fasteners/` library | .dcad module files |
| 25 | `std/connectors/` library | .dcad module files |
| 26 | `std/displays/` library | .dcad module files |
| 27 | `std/controls/` library | .dcad module files |
| 28 | Enclosure helper methods (built-in) | ShapeRegistry registrations |

### Stage 3d: DFM Analysis (estimated: 1-2 weeks)

| Step | Component | Dependencies |
|------|-----------|-------------|
| 29 | `DFMProfile` and `DFMProfileRegistry` | C++ classes |
| 30 | `@process` declaration parsing | Grammar, Evaluator |
| 31 | Wall thickness check | DFMAnalyzer |
| 32 | Draft angle check | DFMAnalyzer |
| 33 | Minimum radius / sharp edge check | DFMAnalyzer |
| 34 | Overhang angle check (FDM) | DFMAnalyzer |
| 35 | `@dfm` annotation and report output | Evaluator, console output |

### Stage 3e: Integration and Examples (estimated: 1 week)

| Step | Component | Dependencies |
|------|-----------|-------------|
| 36 | Grammar consolidation and conflict resolution | Full grammar |
| 37 | Example programs (all four) validated | End-to-end pipeline |
| 38 | Error message quality pass | All error paths |

---

## 10. Files to Create

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `src/core/UnitConverter.h` | 30 | Unit conversion declarations |
| `src/core/UnitConverter.cpp` | 50 | Unit conversion tables and logic |
| `src/geometry/FaceSelector.h` | 25 | Face selection by direction |
| `src/geometry/FaceSelector.cpp` | 100 | OCCT face normal computation and selection |
| `src/geometry/EdgeSelector.h` | 30 | Edge filtering by geometric predicates |
| `src/geometry/EdgeSelector.cpp` | 150 | Edge analysis (parallel, perpendicular, radius) |
| `src/geometry/SketchBuilder.h` | 50 | 2D sketch construction |
| `src/geometry/SketchBuilder.cpp` | 300 | OCCT 2D wire/face building, extrude, revolve |
| `src/geometry/PatternBuilder.h` | 25 | Linear/circular/mirror pattern helpers |
| `src/geometry/PatternBuilder.cpp` | 100 | Pattern replication with transform + fuse |
| `src/parser/ImportResolver.h` | 30 | Module path resolution and caching |
| `src/parser/ImportResolver.cpp` | 150 | File search, parsing, environment export |
| `src/dfm/DFMProfile.h` | 30 | Manufacturing process profile |
| `src/dfm/DFMProfile.cpp` | 60 | Profile storage and default registrations |
| `src/dfm/DFMAnalyzer.h` | 40 | DFM analysis engine declaration |
| `src/dfm/DFMAnalyzer.cpp` | 400 | All DFM check implementations |
| `std/fasteners/mod.dcad` | 20 | Fastener library entry point |
| `std/fasteners/clearance.dcad` | 80 | Clearance hole with size tables |
| `std/fasteners/tap_drill.dcad` | 60 | Tap drill sizes |
| `std/fasteners/heat_set.dcad` | 60 | Heat-set insert holes |
| `std/fasteners/head_recess.dcad` | 80 | Counterbore/countersink |
| `std/fasteners/nut_trap.dcad` | 50 | Hex nut trap |
| `std/connectors/mod.dcad` | 15 | Connector library entry point |
| `std/connectors/usb_c.dcad` | 30 | USB-C cutout |
| `std/connectors/usb_a.dcad` | 25 | USB-A cutout |
| `std/connectors/hdmi.dcad` | 35 | HDMI cutout (standard/mini/micro) |
| `std/connectors/ethernet.dcad` | 25 | RJ45 cutout |
| `std/connectors/barrel_jack.dcad` | 20 | Barrel jack cutout |
| `std/connectors/audio.dcad` | 20 | 3.5mm audio jack cutout |
| `std/connectors/sd_card.dcad` | 25 | SD/microSD card slot cutout |
| `std/displays/mod.dcad` | 10 | Display library entry point |
| `std/displays/oled.dcad` | 40 | OLED display cutouts |
| `std/displays/lcd.dcad` | 40 | LCD display cutouts |
| `std/controls/mod.dcad` | 10 | Controls library entry point |
| `std/controls/buttons.dcad` | 30 | Button/switch cutouts |
| `std/controls/led.dcad` | 20 | LED bezel cutout |
| `examples/parametric_enclosure.dcad` | 80 | Example 1 |
| `examples/battery_holder.dcad` | 75 | Example 2 |
| `examples/cnc_bracket.dcad` | 70 | Example 3 |
| `examples/rpi_case.dcad` | 80 | Example 4 |

## 11. Files to Modify

| File | Changes |
|------|---------|
| `grammar/OpenDCAD.g4` | Complete rewrite with all Phase 3 syntax (Section 7) |
| `src/parser/Value.h` | Add `FUNCTION`, `FACE`, `LIST` types; add `FunctionDef` struct |
| `src/parser/Value.cpp` | Implement new type constructors and accessors |
| `src/parser/Environment.h` | Add `Binding` struct with `isConst`; add `assign()` method |
| `src/parser/Environment.cpp` | Implement `assign()` with const checking |
| `src/parser/Evaluator.h` | Add visitor methods for all new grammar rules |
| `src/parser/Evaluator.cpp` | Implement all new visitors (if, for, fn, module, import, dfm) |
| `src/geometry/Shape.h` | Add `intersect()`, `chamfer()` declarations |
| `src/geometry/Shape.cpp` | Implement `intersect()`, `chamfer()` |
| `src/geometry/ShapeRegistry.h` | Add `ParamDef` for named args; add parameter lists to registrations |
| `src/geometry/ShapeRegistry.cpp` | Register all new methods (patterns, face/edge selection, helpers) |
| `CMakeLists.txt` | Add all new source files, include directories, OCCT components |

---

## 12. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Grammar ambiguity between range `..` and member access `.` | Parser rejects valid programs | Use syntactic predicates; `..` is only valid inside `for ... in` context |
| Unit suffix `m` conflicts with identifiers starting with `m` | `5m` misinterpreted | Lexer maximal-munch ensures `5m` is one `UNIT_NUMBER` token; `5 m` is `NUMBER` + `IDENT` |
| `UNIT_SUFFIX` `in` conflicts with `for x in` keyword | `5in` parsed as `5` + keyword `in` | `UNIT_NUMBER` is listed before keywords in lexer; ANTLR tries longest match first. Add `in` as a context-sensitive keyword or require `for x in` to use a different keyword |
| `module` implicit shape collection is fragile | Users get unexpected shapes in output | Only collect top-level expression statements, not shapes buried in `let` bindings |
| `ReturnSignal` exception for control flow is slow | Performance impact in tight loops | Functions in tight loops are rare in CAD; if needed, switch to a continuation-passing approach |
| Circular imports cause infinite loops | Compiler hangs | Track evaluation stack in `ImportResolver`; throw on cycle detection |
| DFM wall thickness via ray casting is approximate | False positives/negatives | Document limitations; provide `@dfm_ignore` annotation for known-good geometry |
| Standard library `.dcad` files double as test cases and documentation | Breaking changes ripple | Version the standard library separately; test all std examples in CI |
| Sketch boolean operations on open wires | OCCT crashes on invalid 2D geometry | Validate wire closure before boolean ops; report clear error on open profiles |
| Named argument ordering rules are complex | User confusion | Clear error messages: "positional argument after named argument" |
