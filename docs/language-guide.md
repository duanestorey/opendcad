# OpenDCAD Language Guide

OpenDCAD is a scripting language for parametric 3D CAD. Scripts describe solid geometry using an imperative language with method chaining. The compiler outputs STEP files (with per-shape colors), STL files, and JSON metadata manifests.

## Quick Example

```dcad
let base = box(80, 60, 10).fillet(2);
let withHoles = base.face(">Z").throughHole(4, -30, -20);
withHoles = withHoles.face(">Z").throughHole(4, 30, -20);
let final = withHoles.edges().vertical().fillet(1);
export final as bracket;
```

## Coordinate System

The world uses a **right-hand coordinate system**: X = width, Y = depth, Z = height (up).

Most primitives are **XY-centered and Z-grounded** — the bottom face sits on the Z=0 plane. Use `.z(h/2)` to center vertically, or `.z(-h/2)` to lower a centered shape to the ground.

| Primitive | X extent | Y extent | Z extent |
|-----------|----------|----------|----------|
| `box(w, d, h)` | -w/2 to +w/2 | -d/2 to +d/2 | 0 to h |
| `cylinder(r, h)` | -r to +r | -r to +r | 0 to h |
| `cone(r1, r2, h)` | -max(r) to +max(r) | same | 0 to h |
| `sphere(r)` | -r to +r | -r to +r | -r to +r |
| `torus(r1, r2)` | -(r1+r2) to +(r1+r2) | same | -r2 to +r2 |

`box`, `cylinder`, `cone`, `bin` are Z-grounded. `sphere` and `torus` are fully centered at origin.

## Variables

```dcad
let x = 42;              // mutable
const PI = 3.14159;      // immutable
x = 100;                 // reassignment
x += 10;                 // compound: +=, -=, *=, /=
x++;                     // increment/decrement: ++, --
```

## Data Types

| Type | Examples | Notes |
|------|----------|-------|
| NUMBER | `42`, `3.14`, `.5`, `1e-3` | 64-bit double |
| STRING | `"hello"`, `">Z"` | Double-quoted, escape with `\` |
| BOOL | `true`, `false` | |
| VECTOR | `vec(1, 2, 3)`, `[1, 2]` | 2-3 component coordinate vectors |
| LIST | `[]`, `[a, b, c]`, `list(1, 2)` | Growable, any type. Brackets with non-numeric elements create a list |
| SHAPE | `box(10,20,30)` | 3D solid geometry |
| NIL | `nil` | Null value |
| FUNCTION | `fn f() { ... }` | First-class, with closures |
| COLOR | `color(255, 0, 0)` | RGBA for rendering |
| MATERIAL | `material("steel")` | PBR metadata |
| FACE_REF | `shape.face(">Z")` | Single face reference |
| FACE_SELECTOR | `shape.faces()` | Multi-face filter chain |
| WORKPLANE | `faceRef.workplane()` | 2D coordinate system on face |
| SKETCH | `faceRef.draw().circle(5)` | 2D profile builder |
| EDGE_SELECTOR | `shape.edges()` | Multi-edge filter chain |

**Truthiness:** `false`, `nil`, `0`, `""`, empty vector/list are falsy. Everything else is truthy.

## Operators

| Category | Operators | Notes |
|----------|-----------|-------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` | Numbers; `+` concatenates strings; arithmetic works on vectors |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` | `==`/`!=` across types (different types = false); `<`/`>` numbers only |
| Logical | `&&`, `\|\|`, `!` | Short-circuit evaluation |
| Compound | `+=`, `-=`, `*=`, `/=` | Shorthand assignment |
| Increment | `++`, `--` | Postfix, statement only |
| Index | `list[i]` | 0-based access on lists and vectors |

## Control Flow

```dcad
if (x > 10) {
    // ...
} else if (x > 5) {
    // ...
} else {
    // ...
}

for (let i = 0; i < 10; i++) {
    // C-style loop, i scoped to loop
}

for (item in myList) {
    // for-each over lists or vectors
}

while (condition) {
    // max 100,000 iterations (safety limit)
}
```

All blocks `{ }` create a new scope.

## Functions

```dcad
fn add(a, b) {
    return a + b;
}

fn plate(width, depth, thickness = 5) {
    return box(width, depth, thickness);
}

// Named arguments at call site
let p = plate(80, 60, thickness=8);

// Functions are values
let f = add;
print(f(3, 4));  // 7

// Closures capture enclosing scope
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
let items = [1, 2, 3, 4, 5];
let empty = [];
let first = items[0];       // index access
items[2] = 99;              // index assignment
items.push(6);              // append
let n = items.length();     // or len(items)

for (x in items) { print(x); }

// range() builtin
for (i in range(5)) { ... }           // 0..4
for (i in range(2, 8)) { ... }        // 2..7
for (i in range(0, 10, 2)) { ... }    // 0, 2, 4, 6, 8
```

## Vectors

```dcad
let pos = vec(10, 20, 30);
let x = pos[0];

// Arithmetic
let sum = vec(1,2,3) + vec(4,5,6);   // vec(5,7,9)
let scaled = vec(1,2,3) * 2;         // vec(2,4,6)

// Bracket syntax works for translate args
shape.translate([5, 0, 0]);
```

## Imports

```dcad
import "lib/fasteners.dcad";

// All definitions from the file become available
let bolt = m3_bolt(12);
print(M3_DIA);

// Paths are relative to the importing file
// Circular imports are detected and throw an error
// Exports in imported files are suppressed (no output files)
```

## Exports

```dcad
export shape as name;              // single shape
export [a, b, c] as assembly;     // assembly (list of shapes)

// Multiple exports per file
export lid as lid;
export base as base;
```

Each export produces: `name.step` (always, with per-shape colors), `name.json` (always, metadata manifest with build metrics, colors, materials, tags), and optionally `name.stl` (on by default, skip with `--fmt step`).

The CLI controls output format, quality, and which exports to build:
```bash
opendcad model.dcad                      # all exports, standard quality, STEP+STL+JSON
opendcad model.dcad --fmt step           # STEP+JSON only
opendcad model.dcad -q draft             # fast preview mesh, no healing
opendcad model.dcad -q production        # fine mesh for manufacturing
opendcad model.dcad -e lid               # build only the "lid" export
opendcad model.dcad --list-exports       # print export names, exit
opendcad model.dcad --dry-run            # evaluate, write nothing
```

When a file is imported, its exports are suppressed (no output files).

## Color & Material

```dcad
// Create colors (RGB 0-255)
let red = color(255, 0, 0);
let blue = color("#0055FF");
let semi = color(100, 150, 200, 0.5);   // with alpha

// Apply to shapes
let body = box(80, 60, 20).color(RED).material(ABS_BLACK);
```

**Built-in colors:** `RED`, `GREEN`, `BLUE`, `WHITE`, `BLACK`, `YELLOW`, `CYAN`, `MAGENTA`, `ORANGE`, `PURPLE`, `PINK`, `GREY`/`GRAY`, `DARK_RED`, `DARK_GREEN`, `DARK_BLUE`, `LIGHT_GREY`/`LIGHT_GRAY`

**Built-in materials:** `STEEL`, `ALUMINUM`, `BRASS`, `COPPER`, `CHROME`, `TITANIUM`, `CAST_IRON`, `ABS_WHITE`, `ABS_BLACK`, `NYLON`, `POLYCARBONATE`, `ACRYLIC`, `RUBBER`, `WOOD`, `GLASS`, `CARBON_FIBER`, `CONCRETE`

## Tags

```dcad
let housing = box(80, 60, 20).tag("enclosure").tag("structural");
let bolt = cylinder(3, 15).tag("fastener");

let tags = housing.tags();                // ["enclosure", "structural"]
let check = housing.hasTag("enclosure");  // true
```

Tags appear in the JSON metadata manifest for grouping/filtering in viewers.

**Important:** Transforms (translate, rotate, scale, etc.) and boolean ops (fuse, cut, intersect) create new shapes **without** metadata. Apply color, material, and tags after the final transform.

## Comments

```dcad
// Line comment
/* Block comment */
```

## Built-in Functions

| Function | Description |
|----------|-------------|
| `vec(x, y)` / `vec(x, y, z)` | Create a vector |
| `color(r, g, b)` / `color(r, g, b, a)` / `color("#hex")` | Create a color (RGB 0-255) |
| `material("preset")` | Create a material |
| `list(a, b, ...)` | Create a list |
| `range(stop)` / `range(start, stop)` / `range(start, stop, step)` | Generate number list |
| `len(x)` | Length of list, vector, or string |
| `print(args...)` | Print to stdout |

## Shape Factories

### 3D Primitives

| Factory | Args | Description |
|---------|------|-------------|
| `box(w, d, h)` | width, depth, height | Rectangular solid, XY-centered, Z-grounded |
| `cylinder(r, h)` | radius, height | Cylinder along Z, Z-grounded |
| `sphere(r)` | radius | Sphere, centered at origin |
| `cone(r1, r2, h)` | bottom radius, top radius, height | Cone/frustum, Z-grounded |
| `torus(r1, r2)` | major radius, minor radius | Torus, centered at origin |
| `wedge(dx, dy, dz, ltx)` | sizes + top-x taper | Wedge shape |
| `bin(w, d, h, t)` | width, depth, height, wall thickness | Hollow box, Z-grounded |

### 2D Primitives (for extrusion/revolution)

| Factory | Args | Description |
|---------|------|-------------|
| `circle(r)` | radius | 2D circle wire |
| `rectangle(w, h)` | width, height | 2D rectangle wire |
| `polygon(pt1, pt2, ...)` | vec(x,y) points | 2D polygon wire |

### Other Factories

| Factory | Args | Description |
|---------|------|-------------|
| `loft(p1, p2, ...)` | profile shapes | Loft between profiles |
| `import_step(path)` | file path string | Load STEP file |
| `import_stl(path)` | file path string | Load STL file |

## Shape Methods

### Boolean Operations
| Method | Returns | Description |
|--------|---------|-------------|
| `.fuse(other)` | Shape | Boolean union |
| `.cut(other)` | Shape | Boolean subtraction |
| `.intersect(other)` | Shape | Boolean intersection |

### Edge Operations (all edges)
| Method | Returns | Description |
|--------|---------|-------------|
| `.fillet(r)` | Shape | Round all edges |
| `.chamfer(d)` | Shape | Chamfer all edges |

### Transforms
| Method | Returns | Description |
|--------|---------|-------------|
| `.translate(x, y, z)` or `.translate(vec)` | Shape | Move shape |
| `.rotate(rx, ry, rz)` or `.rotate(vec)` | Shape | Rotate (degrees) |
| `.scale(factor)` or `.scale(fx, fy, fz)` | Shape | Uniform or per-axis scale |
| `.mirror(nx, ny, nz)` or `.mirror(vec)` | Shape | Mirror across plane |
| `.flip()` | Shape | Reverse normals |
| `.x(dist)`, `.y(dist)`, `.z(dist)` | Shape | Axis shortcuts for translate |

### Extrusion / Shell
| Method | Returns | Description |
|--------|---------|-------------|
| `.linear_extrude(h)` | Shape | Extrude 2D profile along Z |
| `.rotate_extrude(angle?)` | Shape | Revolve 2D profile (default 360) |
| `.sweep(path)` | Shape | Sweep profile along path |
| `.shell(thickness)` | Shape | Hollow out a solid |

### Feature Patterns
| Method | Returns | Description |
|--------|---------|-------------|
| `.linearPattern(vec, count)` | Shape | Repeat along vector |
| `.circularPattern(axis_vec, count, angle?)` | Shape | Repeat around axis |
| `.mirrorFeature(plane_vec)` | Shape | Mirror across plane |

### Advanced
| Method | Returns | Description |
|--------|---------|-------------|
| `.draft(angle, normal_vec)` | Shape | Apply draft angle |
| `.splitAt(point_vec, normal_vec)` | Shape | Split at plane |
| `.placeCorners(shape, xOff, yOff)` | Shape | Place shape at 4 corners |

### Metadata
| Method | Returns | Description |
|--------|---------|-------------|
| `.color(colorValue)` | Shape | Set display color |
| `.material(materialValue)` | Shape | Set material properties |
| `.tag("name")` | Shape | Add a string tag |
| `.tags()` | List | Get all tags as string list |
| `.hasTag("name")` | Bool | Check if tag exists |

## Face Selection

```dcad
// Select a single face by direction
shape.face(">Z")       // top face (normal points +Z)
shape.face("<Z")       // bottom face
shape.face(">X")       // right, "<X" left, ">Y" front, "<Y" back
shape.topFace()        // shortcut for face(">Z")
shape.bottomFace()     // shortcut for face("<Z")

// Select all faces for filtering
shape.faces()          // → FaceSelector
```

### FaceSelector Filters

| Method | Returns | Description |
|--------|---------|-------------|
| `.top()`, `.bottom()`, `.front()`, `.back()`, `.left()`, `.right()` | FaceRef | Directional pick |
| `.largest()`, `.smallest()` | FaceRef | By area |
| `.nearestTo(vec)`, `.farthestFrom(vec)` | FaceRef | By distance |
| `.planar()`, `.cylindrical()` | FaceSelector | By surface type |
| `.areaGreaterThan(n)`, `.areaLessThan(n)` | FaceSelector | By area range |
| `.byIndex(n)` | FaceRef | By index |
| `.count()` | Number | Count faces |

### FaceRef Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `.draw()` | Sketch | Start sketching on face |
| `.workplane()` | Workplane | Get 2D coordinate system |
| `.center()` | Vector | Face center point |
| `.normal()` | Vector | Face normal direction |
| `.area()` | Number | Face area |
| `.isPlanar()` | Bool | Check if face is flat |
| `.hole(dia, depth, cx?, cy?)` | Shape | Blind hole on face |
| `.throughHole(dia, cx?, cy?)` | Shape | Through hole on face |
| `.counterbore(holeDia, boreDia, boreDepth, holeDepth, cx?, cy?)` | Shape | Counterbore hole |
| `.countersink(holeDia, sinkDia, sinkAngle, holeDepth, cx?, cy?)` | Shape | Countersink hole |

The `cx`, `cy` parameters position holes in the face's local 2D coordinate system (origin at face center).

## Sketch Operations

Start a sketch with `.draw()` on a FaceRef, add profiles, then generate geometry:

```dcad
let result = base.face(">Z").draw()
    .circle(8)
    .extrude(15);
```

### Sketch Profiles
| Method | Description |
|--------|-------------|
| `.circle(r, cx?, cy?)` | Circle at position |
| `.rect(w, h, cx?, cy?)` | Rectangle at position |
| `.slot(length, width, cx?, cy?)` | Rounded slot |
| `.polygon(pt1, pt2, ...)` | Polygon from vec(x,y) points |

### Freeform Wire Building
| Method | Description |
|--------|-------------|
| `.moveTo(x, y)` | Set pen position |
| `.lineTo(x, y)` | Line to point |
| `.arcTo(x, y, bulge)` | Arc to point (bulge = curvature) |
| `.splineTo(throughPts..., ex, ey)` | Spline through points |
| `.close()` | Close wire back to start |

### 2D Modification
| Method | Description |
|--------|-------------|
| `.fillet2D(r)` | Fillet sketch corners |
| `.chamfer2D(d)` | Chamfer sketch corners |
| `.offset(d)` | Offset sketch outline |

### Sketch to Shape
| Method | Returns | Description |
|--------|---------|-------------|
| `.extrude(h)` | Shape | Extrude up from face |
| `.cutBlind(depth)` | Shape | Cut into parent shape |
| `.cutThrough()` | Shape | Cut through entire shape |
| `.revolve(angle?)` | Shape | Revolve around axis (default 360) |

## Edge Selection

```dcad
shape.edges()                     // all edges → EdgeSelector
    .vertical()                   // filter: parallel to Z
    .horizontal()                 // filter: perpendicular to Z
    .parallelTo(vec)              // filter: parallel to direction
    .longest() / .shortest()      // select by length
    .longerThan(n) / .shorterThan(n)  // filter by length
    .ofFace(faceRef)              // edges of a specific face
    .fillet(r)                    // apply fillet → Shape
    .chamfer(d)                   // apply chamfer → Shape
    .count()                      // count selected
```

## Common Patterns

### Stack parts vertically
```dcad
let base = box(80, 60, 20);                     // Z: 0 to 20
let mid = box(60, 40, 10).z(20);                // Z: 20 to 30
let top = cylinder(15, 5).z(30);                // Z: 30 to 35
let model = base.fuse(mid).fuse(top);
export model as tower;
```

### Subtractive modeling (start solid, cut features)
```dcad
let block = box(80, 60, 30);
let withPocket = block.face(">Z").draw().rect(60, 40).cutBlind(20);
let withHole = withPocket.face(">Z").throughHole(10);
let final = withHole.edges().vertical().fillet(3);
export final as housing;
```

### Parametric function
```dcad
fn standoff(dia, height, holeDia = 2) {
    let post = cylinder(dia / 2, height);
    return post.face(">Z").throughHole(holeDia);
}

let base = box(60, 60, 3);
let s1 = standoff(8, 12).translate([20, 20, 3]);
let s2 = standoff(8, 12).translate([-20, 20, 3]);
let s3 = standoff(8, 12).translate([20, -20, 3]);
let s4 = standoff(8, 12).translate([-20, -20, 3]);
let model = base.fuse(s1).fuse(s2).fuse(s3).fuse(s4);
export model as pcb_mount;
```

### Reusable library with import
```dcad
// lib/fasteners.dcad
const M3_DIA = 3.0;

fn m3_bolt(length) {
    let head = cylinder(M3_DIA, 2).z(length);
    let shaft = cylinder(M3_DIA / 2, length);
    return head.fuse(shaft);
}
```
```dcad
// main.dcad
import "lib/fasteners.dcad";
let bolt = m3_bolt(15);
let plate = box(40, 40, 5).face(">Z").throughHole(M3_DIA + 0.2);
export plate as plate;
```

### Assembly export with metadata
```dcad
let housing = box(80, 60, 20)
    .fillet(2)
    .color(color(80, 80, 90))
    .material(ABS_BLACK)
    .tag("enclosure");

let bolt1 = cylinder(3, 15)
    .translate([30, 20, 0])
    .material(STEEL)
    .tag("fastener");

let bolt2 = cylinder(3, 15)
    .translate([-30, 20, 0])
    .material(STEEL)
    .tag("fastener");

// Assembly: each shape keeps its own color/material/tags
export [housing, bolt1, bolt2] as device;

// Also export housing alone
export housing as housing_only;
```

### Sketch-based features on faces
```dcad
let base = box(40, 30, 10);

// Boss on top face
let withBoss = base.face(">Z").draw().circle(8).extrude(15);

// Hole through the boss
let withHole = withBoss.face(">Z").draw().circle(5).cutThrough();

// Pocket on bottom face
let withPocket = withHole.face("<Z").draw().rect(20, 15).cutBlind(3);

// Selective edge fillet
let final = withPocket.edges().vertical().fillet(2);
export final as bracket;
```

### Mounting plate with hole pattern
```dcad
let base = box(80, 60, 10);

// Mounting holes at specific positions on top face
let h1 = base.face(">Z").throughHole(4, -30, -20);
let h2 = h1.face(">Z").throughHole(4, 30, -20);
let h3 = h2.face(">Z").throughHole(4, -30, 20);
let h4 = h3.face(">Z").throughHole(4, 30, 20);

// Center counterbore
let h5 = h4.face(">Z").counterbore(5, 10, 3, 8);

// Slot for cable routing
let withSlot = h5.face(">Z").draw().slot(25, 8).cutThrough();

let final = withSlot.edges().vertical().fillet(2);
export final as mounting_plate;
```
