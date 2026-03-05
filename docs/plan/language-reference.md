# OpenDCAD Language Reference

## Variables

```dcad
let x = 42;              // mutable — can be reassigned
const PI = 3.14159;      // immutable — cannot be reassigned
x = 100;                 // reassignment (no 'let' keyword)
x += 10;                 // compound assignment (also -=, *=, /=)
x++;                     // increment (also x--)
```

## Data Types

| Type | Examples | Notes |
|------|----------|-------|
| NUMBER | `42`, `3.14`, `.5`, `1e-3` | 64-bit double |
| STRING | `"hello"`, `">Z"` | Double-quoted, escape with `\` |
| BOOL | `true`, `false` | |
| VECTOR | `vec(1, 2, 3)`, `[1, 2]` | 2-3 doubles for coordinates. `vec()` factory preferred; `[x,y]`/`[x,y,z]` brackets still work for backward compat |
| LIST | `[]`, `[1, "a", true]`, `[5, 10, 15, 20]` | Growable, holds any type. 4+ element brackets → list. Empty brackets → list |
| SHAPE | `box(10,20,30)` | 3D geometry from OCCT |
| NIL | `nil` | Null/nothing |
| FUNCTION | `fn f() { ... }` | First-class, with closure |
| COLOR | `color(255, 0, 0)` | RGBA metadata for rendering |
| MATERIAL | `material("steel")` | PBR metadata for rendering |
| FACE_REF | `shape.face(">Z")` | Single face + local workplane |
| FACE_SELECTOR | `shape.faces()` | Multiple faces for filtering |
| WORKPLANE | `faceRef.workplane()` | 2D coordinate system on face |
| SKETCH | `faceRef.draw().circle(5)` | 2D profile being built |
| EDGE_SELECTOR | `shape.edges()` | Multiple edges for filtering |

## Operators

| Category | Operators | Notes |
|----------|-----------|-------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` | Works on numbers; `+` also concatenates strings; `+`/`-`/`*`/`/` work on vectors |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` | Return BOOL. `==`/`!=` work across types (different types → false). `<`/`>`/`<=`/`>=` require numbers |
| Logical | `&&`, `\|\|`, `!` | Short-circuit. `&&` returns left if falsy, else right. `\|\|` returns left if truthy, else right |
| Compound | `+=`, `-=`, `*=`, `/=` | `x += 5` is `x = x + 5` |
| Increment | `++`, `--` | Postfix only. `i++` increments and returns void (statement only) |
| Index | `list[i]` | Access list/vector elements by integer index |

**Truthiness:** `false`, `nil`, `0`, `""`, empty vector/list → falsy. Everything else → truthy.

## Control Flow

```dcad
// If/else
if (x > 10) {
    // ...
} else if (x > 5) {
    // ...
} else {
    // ...
}

// C-style for loop
for (let i = 0; i < 10; i++) {
    // i is scoped to the loop
}

// For-each (iterates over lists or vectors)
for (item in myList) {
    print(item);
}

// While loop
while (condition) {
    // max 100,000 iterations (safety limit)
}
```

All blocks `{ }` create a new scope. Variables defined inside are not visible outside.

## Functions

```dcad
// Basic function
fn add(a, b) {
    return a + b;
}

// Default parameters
fn plate(width, depth, thickness = 5) {
    return box(width, depth, thickness);
}

// Named arguments at call site
let p = plate(80, 60, thickness=8);

// Functions are values (can be stored, passed)
let f = add;
print(f(3, 4));  // 7

// Recursion
fn factorial(n) {
    if (n <= 1) { return 1; }
    return n * factorial(n - 1);
}

// Closure (captures enclosing scope)
fn makeCounter() {
    let count = 0;
    fn increment() {
        count = count + 1;
        return count;
    }
    return increment;
}
```

## Lists

```dcad
let items = [1, 2, 3, 4, 5];   // list literal
let empty = [];                  // empty list
let first = items[0];            // index access (0-based)
items[2] = 99;                   // index assignment
items.push(6);                   // append element
let n = items.length();          // list length
let n2 = len(items);             // builtin alternative

// Iterate
for (x in items) {
    print(x);
}

// range() builtin
for (i in range(5)) { ... }         // [0, 1, 2, 3, 4]
for (i in range(2, 8)) { ... }      // [2, 3, 4, 5, 6, 7]
for (i in range(0, 10, 2)) { ... }  // [0, 2, 4, 6, 8]
```

## Vectors

```dcad
let pos = vec(10, 20, 30);      // 3D vector
let pos2d = vec(5, 10);         // 2D vector
let x = pos[0];                 // index access

// Vector arithmetic
let sum = vec(1,2,3) + vec(4,5,6);   // vec(5,7,9)
let scaled = vec(1,2,3) * 2;         // vec(2,4,6)

// Legacy bracket syntax still works (backward compat)
box(10,10,10).translate([5, 0, 0]);   // auto-coerces to vector
```

## Imports

```dcad
import "lib/fasteners.dcad";    // import everything from file

// All functions, constants, and variables from the file are available
let bolt = m3_bolt(12);
print(M3_DIA);

// Exports in imported files are suppressed (no file output)
// Circular imports are detected and throw an error
// Paths are relative to the importing file's directory
```

## Exports

```dcad
export shape as name;

// Multiple exports per file
export lid as lid;
export base as base;
```

`export` names a shape as a script output. The CLI decides what format to write.
When a file is imported, its exports make shapes available by name but don't produce files.

## Color & Material

```dcad
// Create colors
const red = color(255, 0, 0);
const blue = color("#0055FF");
const semi = color(100, 150, 200, 0.5);   // with alpha

// Create materials
const steel = material("steel");

// Apply to shapes (returns shape with metadata)
let body = box(80, 60, 20).color(red);
let bolt = cylinder(3, 12).material(steel);

// Built-in color constants (available without import)
// RED, GREEN, BLUE, WHITE, BLACK, YELLOW, CYAN, MAGENTA,
// ORANGE, PURPLE, PINK, GREY/GRAY, DARK_RED, DARK_GREEN,
// DARK_BLUE, LIGHT_GREY/LIGHT_GRAY

// Built-in material presets (available without import)
// STEEL, ALUMINUM, BRASS, COPPER, CHROME, TITANIUM, CAST_IRON,
// ABS_WHITE, ABS_BLACK, NYLON, POLYCARBONATE, ACRYLIC,
// RUBBER, WOOD, GLASS, CARBON_FIBER, CONCRETE

let body = box(80, 60, 20).material(ALUMINUM);
```

## Comments

```dcad
// Line comment
/* Block
   comment */
```

## Built-in Functions

| Function | Description |
|----------|-------------|
| `vec(x, y)` / `vec(x, y, z)` | Create a 2D/3D vector |
| `color(r, g, b)` / `color(r, g, b, a)` / `color("#hex")` | Create a color (RGB 0-255) |
| `material("preset")` | Create a material |
| `list(a, b, c, ...)` | Create a list explicitly |
| `range(stop)` / `range(start, stop)` / `range(start, stop, step)` | Create a list of numbers |
| `len(collection)` | Length of list, vector, or string |
| `print(args...)` | Print values to stdout |

## Shape Factories (via ShapeRegistry)

### 3D Primitives
| Factory | Args | Description |
|---------|------|-------------|
| `box(w, d, h)` | width, depth, height | Rectangular solid |
| `cylinder(r, h)` | radius, height | Cylinder |
| `sphere(r)` | radius | Sphere |
| `cone(r1, r2, h)` | bottom radius, top radius, height | Cone/frustum |
| `wedge(dx, dy, dz, ltx)` | x-size, y-size, z-size, top-x-size | Wedge/ramp |
| `torus(r1, r2)` | major radius, minor radius | Torus |
| `bin(w, d, h, t)` | width, depth, height, wall thickness | Hollow box |

### 2D Primitives
| Factory | Args | Description |
|---------|------|-------------|
| `circle(r)` | radius | 2D circle wire |
| `rectangle(w, h)` | width, height | 2D rectangle wire |
| `polygon(pt1, pt2, ...)` | vec(x,y) points | 2D polygon wire |

### Multi-Shape
| Factory | Args | Description |
|---------|------|-------------|
| `loft(p1, p2, ...)` | profile shapes | Loft between profiles |

### Import
| Factory | Args | Description |
|---------|------|-------------|
| `import_step(path)` | file path string | Load STEP file |
| `import_stl(path)` | file path string | Load STL file |

## Shape Methods

### Boolean Operations
`.fuse(other)`, `.cut(other)`, `.intersect(other)`

### Edge Operations
`.fillet(radius)`, `.chamfer(distance)`

### Transforms
`.translate(x, y, z)` or `.translate(vec)`, `.rotate(rx, ry, rz)` or `.rotate(vec)`,
`.scale(factor)` or `.scale(vec)`, `.mirror(nx, ny, nz)` or `.mirror(vec)`,
`.x(dist)`, `.y(dist)`, `.z(dist)` — axis shortcuts,
`.flip()` — reverse normal

### Extrusion/Revolution
`.linear_extrude(height)`, `.rotate_extrude(angle)`, `.sweep(path)`, `.shell(thickness)`

### Feature Patterns
`.linearPattern(vec, count)`, `.circularPattern(axis_vec, count, angle?)`, `.mirrorFeature(plane_vec)`

### Advanced
`.draft(angle, normal_vec)`, `.splitAt(point_vec, normal_vec)`, `.placeCorners(shape, xOff, yOff)`

### Metadata
`.color(colorValue)`, `.material(materialValue)`

## Face Selection & Operations

```dcad
shape.face(">Z")       // top face (by selector string)
shape.face("<Z")       // bottom face
shape.face(">X")       // right face, etc.
shape.faces()          // all faces → FaceSelector
shape.topFace()        // shortcut for face(">Z")
shape.bottomFace()     // shortcut for face("<Z")
```

### FaceSelector Filters
`.top()`, `.bottom()`, `.front()`, `.back()`, `.left()`, `.right()` → FaceRef
`.largest()`, `.smallest()` → FaceRef
`.nearestTo(vec)`, `.farthestFrom(vec)` → FaceRef
`.planar()`, `.cylindrical()` → FaceSelector
`.areaGreaterThan(n)`, `.areaLessThan(n)` → FaceSelector
`.byIndex(n)` → FaceRef
`.count()` → number

### FaceRef Methods
`.draw()` → Sketch, `.workplane()` → Workplane
`.center()` → vec, `.normal()` → vec, `.area()` → number, `.isPlanar()` → bool
`.hole(dia, depth, cx?, cy?)`, `.throughHole(dia, cx?, cy?)`
`.counterbore(holeDia, boreDia, boreDepth, holeDepth, cx?, cy?)`
`.countersink(holeDia, sinkDia, sinkAngle, holeDepth, cx?, cy?)`

## Sketch Operations (on face)

```dcad
let result = base.face(">Z").draw()
    .circle(8)          // sketch a circle
    .extrude(15);       // extrude up → new Shape

// Available sketch profiles
.circle(r, cx?, cy?), .rect(w, h, cx?, cy?), .polygon(pts...)
.slot(length, width, cx?, cy?)

// Freeform wire building
.moveTo(x, y), .lineTo(x, y), .arcTo(x, y, bulge), .splineTo(pts, ex, ey), .close()

// 2D modification
.fillet2D(r), .chamfer2D(d), .offset(d)

// Geometry generation (Sketch → Shape)
.extrude(height)     — linear extrude up from face
.cutBlind(depth)     — cut into parent face
.cutThrough()        — cut completely through
.revolve(angle?)     — revolve around axis
```

## Edge Selection

```dcad
shape.edges()              // all edges → EdgeSelector
.vertical(), .horizontal() // filter by orientation
.parallelTo(vec)           // filter by direction
.longest(), .shortest()    // select extreme
.longerThan(n), .shorterThan(n) // filter by length
.ofFace(faceRef)           // edges on specific face
.fillet(r), .chamfer(d)    // apply to selected edges
.count()                   // count selected
```

## Complete Grammar (ANTLR4)

The grammar file is at `grammar/OpenDCAD.g4`. Key structure:

```
program → stmt* EOF

stmt → letStmt ';' | constStmt ';' | assignStmt ';' | compoundAssignStmt ';'
     | postfixIncrStmt ';' | exportStmt ';' | importStmt ';' | returnStmt ';'
     | exprStmt ';' | ifStmt | forStmt | whileStmt | fnDecl

letStmt    → 'let' IDENT '=' expr
constStmt  → 'const' IDENT '=' expr
assignStmt → IDENT '=' expr | IDENT '[' expr ']' '=' expr
exportStmt → 'export' expr 'as' IDENT
importStmt → 'import' STRING
fnDecl     → 'fn' IDENT '(' paramList? ')' block

ifStmt     → 'if' '(' expr ')' block ('else' 'if' '(' expr ')' block)* ('else' block)?
forStmt    → 'for' '(' forInit ';' expr ';' forUpdate ')' block     (C-style)
           | 'for' '(' IDENT 'in' expr ')' block                    (for-each)
whileStmt  → 'while' '(' expr ')' block

expr       → (precedence chain from || down to primary)
primary    → NUMBER | STRING | TRUE | FALSE | NIL_LIT | vectorLiteral
           | listLiteral | postfix | IDENT | '(' expr ')'

postfix    → call ('.' methodCall)*             (factory call + chain)
           | IDENT '.' methodCall+              (variable + chain)
call       → IDENT '(' argList? ')'
argList    → arg (',' arg)*
arg        → IDENT '=' expr  (named) | expr  (positional)
```

## Future Work (Planned)

- CLI refactor: split file writing from evaluation, add `--format`, `--quality`, `--export` flags
- 2D profile dimensional drawings for manufacturing
- DFM (Design for Manufacturing) inspections
- Standard parts library (fasteners, connectors, enclosures)
- Layer system for 3D viewer (separate plastic, metal, PCB, connectors)
- Named argument support in `material()` builtin for PBR properties
- Selective imports: `import { x, y } from "file.dcad"`
