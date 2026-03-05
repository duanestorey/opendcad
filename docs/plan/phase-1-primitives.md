# Phase 1: Shape Primitives and Operations

## Overview

Phase 1 extends the OpenDCAD Shape class with a complete set of 3D primitives, 2D profile primitives, extrusion/revolution operations, and additional geometric operations. This phase assumes Phase 0 has established the core architecture: the `Evaluator` (ANTLR visitor that produces `Value` objects), the `Value` type system (Number, Vector, Shape, String, Boolean), the `ShapeRegistry` (maps DSL function names to C++ factory/method implementations), and the `Environment` (lexical scope for `let` bindings).

All new geometry is implemented in `src/Shape.h` and `src/Shape.cpp`, registered in the `ShapeRegistry`, and exposed through the existing DSL grammar without grammar changes (the current grammar already supports constructor calls with arguments and method chaining).

### Existing Foundation

The current `Shape` class provides:
- **Factory methods**: `createBox(w,d,h)`, `createCylinder(r,h)`, `createTorus(r1,r2,angle)`, `createBin(w,d,h,t)`
- **Boolean operations**: `fuse(other)`, `cut(other)`
- **Transforms**: `translate(x,y,z)`, `rotate(rx,ry,rz)`, `fillet(amount)`, `flip()`
- **Axis shortcuts**: `x(v)`, `y(v)`, `z(v)`
- **Type**: `ShapePtr = std::shared_ptr<Shape>`

All new methods follow the same conventions: static factory methods for constructors, instance methods returning `ShapePtr` for chaining.

---

## 3D Primitives

### 1. `sphere(radius)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createSphere(double radius);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepPrimAPI_MakeSphere.hxx>

ShapePtr Shape::createSphere(double radius) {
    gp_Pnt center(0, 0, 0);
    return ShapePtr(new Shape(BRepPrimAPI_MakeSphere(center, radius).Shape()));
}
```

Key OCCT classes:
- `BRepPrimAPI_MakeSphere` -- Constructs a solid sphere. The single-argument constructor `MakeSphere(radius)` places the sphere at the origin centered on Z. The two-argument constructor `MakeSphere(gp_Pnt, radius)` allows specifying the center point. Additional overloads accept `gp_Ax2` for orientation and angle parameters for partial spheres (wedge/segment), but the default full sphere is sufficient for the standard primitive.

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("sphere", [](const std::vector<Value>& args) -> ShapePtr {
    // sphere(radius)
    if (args.size() != 1)
        throw EvalError("sphere() expects 1 argument: radius");
    double radius = args[0].asNumber();
    if (radius <= 0)
        throw EvalError("sphere() radius must be positive, got " + std::to_string(radius));
    return Shape::createSphere(radius);
});
```

**DSL Usage**

```
let ball = sphere(10);
let marble = sphere(0.5).translate([3, 0, 0]);
let hollowBall = sphere(10).cut(sphere(9));
```

**Edge Cases and Gotchas**
- Radius must be strictly positive; OCCT will fail with `Standard_ConstructionError` if radius <= 0.
- The sphere is centered at the origin (unlike `createBox` which places the bottom face at Z=0). This is intentional for consistency with OpenSCAD, but means a `sphere(10).z(10)` is needed to place it sitting on the XY plane.
- Partial spheres (angle1, angle2 parameters in OCCT) are not exposed in Phase 1. These can be added later as optional parameters or a separate `sphereSegment()` constructor.

---

### 2. `cone(r1, r2, height)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createCone(double r1, double r2, double height);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepPrimAPI_MakeCone.hxx>

ShapePtr Shape::createCone(double r1, double r2, double height) {
    gp_Ax2 ax(gp_Pnt(0, 0, 0), gp::DZ(), gp::DX());
    return ShapePtr(new Shape(BRepPrimAPI_MakeCone(ax, r1, r2, height).Shape()));
}
```

Key OCCT classes:
- `BRepPrimAPI_MakeCone(gp_Ax2, R1, R2, H)` -- Constructs a truncated cone (frustum) along the axis. `R1` is the radius at the base (Z=0), `R2` is the radius at the top (Z=H). When `R2=0`, produces a pointed cone. The `gp_Ax2` argument specifies the cone's local coordinate system (origin, Z direction, X direction).

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("cone", [](const std::vector<Value>& args) -> ShapePtr {
    // cone(r1, r2, height)
    if (args.size() != 3)
        throw EvalError("cone() expects 3 arguments: r1, r2, height");
    double r1 = args[0].asNumber();
    double r2 = args[1].asNumber();
    double height = args[2].asNumber();
    if (r1 < 0 || r2 < 0)
        throw EvalError("cone() radii must be non-negative");
    if (r1 == 0 && r2 == 0)
        throw EvalError("cone() at least one radius must be positive");
    if (height <= 0)
        throw EvalError("cone() height must be positive");
    return Shape::createCone(r1, r2, height);
});
```

**DSL Usage**

```
let pointedCone = cone(10, 0, 20);
let frustum = cone(10, 5, 15);
let invertedCone = cone(0, 10, 20);
let funnel = cone(10, 5, 15).cut(cone(9, 4, 15).z(0.5));
```

**Edge Cases and Gotchas**
- Either `r1` or `r2` may be zero (pointed cone), but not both (degenerate shape).
- Both radii must be non-negative. OCCT raises `Standard_ConstructionError` for negative radii.
- Height must be strictly positive.
- Unlike OpenSCAD's `cylinder(r1=..., r2=..., h=...)`, here we use a dedicated `cone()` constructor rather than overloading `cylinder()`. This keeps the API explicit.
- The cone base sits at Z=0 with the apex/top at Z=height, consistent with `createCylinder`.

---

### 3. `wedge(dx, dy, dz, ltx)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createWedge(double dx, double dy, double dz, double ltx);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepPrimAPI_MakeWedge.hxx>

ShapePtr Shape::createWedge(double dx, double dy, double dz, double ltx) {
    return ShapePtr(new Shape(BRepPrimAPI_MakeWedge(dx, dy, dz, ltx).Shape()));
}
```

Key OCCT classes:
- `BRepPrimAPI_MakeWedge(dx, dy, dz, ltx)` -- Constructs a wedge (tapered box). The base is a rectangle of size `dx * dz` in the XZ plane. The top face at Y=`dy` has width `ltx` in the X direction (centered), still spanning full `dz` in Z. When `ltx=0`, the top degenerates to a line (producing a true prism/wedge shape). OCCT also provides an overload `MakeWedge(dx, dy, dz, xmin, zmin, xmax, zmax)` for asymmetric tapering, but the simpler 4-parameter form is sufficient for Phase 1.

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("wedge", [](const std::vector<Value>& args) -> ShapePtr {
    // wedge(dx, dy, dz, ltx)
    if (args.size() != 4)
        throw EvalError("wedge() expects 4 arguments: dx, dy, dz, ltx");
    double dx  = args[0].asNumber();
    double dy  = args[1].asNumber();
    double dz  = args[2].asNumber();
    double ltx = args[3].asNumber();
    if (dx <= 0 || dy <= 0 || dz <= 0)
        throw EvalError("wedge() dimensions dx, dy, dz must be positive");
    if (ltx < 0 || ltx > dx)
        throw EvalError("wedge() ltx must be in range [0, dx]");
    return Shape::createWedge(dx, dy, dz, ltx);
});
```

**DSL Usage**

```
let ramp = wedge(20, 10, 30, 0);
let trapezoid = wedge(20, 10, 30, 10);
let roof = wedge(40, 15, 60, 0).translate([-20, 0, -30]);
```

**Edge Cases and Gotchas**
- `ltx` must satisfy `0 <= ltx <= dx`. When `ltx == 0`, the top edge degenerates to a single line. When `ltx == dx`, the result is a regular box.
- The wedge is NOT centered at origin by default. It spans `[0, dx] x [0, dy] x [0, dz]`. Users will need `.translate([-dx/2, -dy/2, -dz/2])` to center it, or we can add a centered variant later.
- The OCCT wedge axis convention is unusual: the taper happens along Y (height), not Z. This may be surprising to users accustomed to Z-up conventions. Document this clearly and consider whether to apply a rotation to match the project's Z-up convention (consistent with `createBox` and `createCylinder`).
- **Recommended**: Rotate the OCCT output so that height runs along Z for consistency:

```cpp
ShapePtr Shape::createWedge(double dx, double dy, double dz, double ltx) {
    // OCCT wedge tapers along Y; rotate so taper runs along Z for consistency
    TopoDS_Shape raw = BRepPrimAPI_MakeWedge(dx, dz, dy, ltx).Shape();
    gp_Trsf rot;
    rot.SetRotation(gp::OX(), M_PI / 2.0);
    // Also center on X and Y
    gp_Trsf tr;
    tr.SetTranslation(gp_Vec(-dx / 2.0, -dz / 2.0, 0));
    return ShapePtr(new Shape(BRepBuilderAPI_Transform(raw, tr * rot, true).Shape()));
}
```

---

## 2D Primitives

2D primitives produce a `TopoDS_Face` (a planar face in the XY plane at Z=0). These are not standalone solids -- they must be extruded, revolved, or swept to produce 3D geometry. The `Shape` class wraps them identically (a `TopoDS_Shape` can hold a face), and the `isValid()` check works the same way.

In the DSL, 2D primitives are used as the starting point of a chain that includes `linear_extrude`, `rotate_extrude`, or `sweep`.

### 4. `circle(radius)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createCircle(double radius);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <gp_Circ.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

ShapePtr Shape::createCircle(double radius) {
    // 1. Define a circle in the XY plane at origin
    gp_Circ circ(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), radius);

    // 2. Circle -> Edge -> Wire -> Face
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circ);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

    return ShapePtr(new Shape(face));
}
```

Key OCCT classes:
- `gp_Circ` -- Defines a geometric circle in 3D space, specified by a coordinate system (`gp_Ax2`) and a radius.
- `BRepBuilderAPI_MakeEdge` -- Converts a geometric curve (here, a circle) into a topological edge (`TopoDS_Edge`).
- `BRepBuilderAPI_MakeWire` -- Combines one or more edges into a wire (`TopoDS_Wire`). For a single closed edge like a circle, this produces a closed wire.
- `BRepBuilderAPI_MakeFace` -- Fills a closed wire to produce a face (`TopoDS_Face`). The face lies on the plane determined by the wire.

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("circle", [](const std::vector<Value>& args) -> ShapePtr {
    // circle(radius)
    if (args.size() != 1)
        throw EvalError("circle() expects 1 argument: radius");
    double radius = args[0].asNumber();
    if (radius <= 0)
        throw EvalError("circle() radius must be positive");
    return Shape::createCircle(radius);
});
```

**DSL Usage**

```
let disc = circle(10);
let pipe = circle(5).linear_extrude(100);
let tube = circle(5).linear_extrude(100).cut(circle(4).linear_extrude(100));
let dome = circle(10).rotate_extrude(180);
```

**Edge Cases and Gotchas**
- The result is a `TopoDS_Face`, not a solid. Calling `fuse()`, `cut()`, or `fillet()` on a face will fail or produce unexpected results. The DSL should warn or error if boolean operations are attempted on 2D shapes.
- Radius must be positive. Zero radius produces a degenerate edge.
- The circle lies in the XY plane at Z=0. Extrusion along Z is the natural operation.

---

### 5. `rectangle(width, height)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createRectangle(double width, double height);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

ShapePtr Shape::createRectangle(double width, double height) {
    double hw = width / 2.0;
    double hh = height / 2.0;

    // Four corner points (XY plane, centered at origin)
    gp_Pnt p1(-hw, -hh, 0);
    gp_Pnt p2( hw, -hh, 0);
    gp_Pnt p3( hw,  hh, 0);
    gp_Pnt p4(-hw,  hh, 0);

    // Four edges forming a closed rectangle
    TopoDS_Edge e1 = BRepBuilderAPI_MakeEdge(p1, p2);
    TopoDS_Edge e2 = BRepBuilderAPI_MakeEdge(p2, p3);
    TopoDS_Edge e3 = BRepBuilderAPI_MakeEdge(p3, p4);
    TopoDS_Edge e4 = BRepBuilderAPI_MakeEdge(p4, p1);

    // Assemble into a wire
    BRepBuilderAPI_MakeWire wireMaker;
    wireMaker.Add(e1);
    wireMaker.Add(e2);
    wireMaker.Add(e3);
    wireMaker.Add(e4);
    TopoDS_Wire wire = wireMaker.Wire();

    // Fill to make a face
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

    return ShapePtr(new Shape(face));
}
```

Key OCCT classes:
- `BRepBuilderAPI_MakeEdge(gp_Pnt, gp_Pnt)` -- Creates a straight-line edge between two points.
- `BRepBuilderAPI_MakeWire` -- Incrementally builds a wire from multiple edges. Edges must be added in connected order (end of one edge = start of next).
- `BRepBuilderAPI_MakeFace(TopoDS_Wire)` -- Creates a planar face bounded by the wire. The wire must be closed and planar.

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("rectangle", [](const std::vector<Value>& args) -> ShapePtr {
    // rectangle(width, height)
    if (args.size() != 2)
        throw EvalError("rectangle() expects 2 arguments: width, height");
    double w = args[0].asNumber();
    double h = args[1].asNumber();
    if (w <= 0 || h <= 0)
        throw EvalError("rectangle() dimensions must be positive");
    return Shape::createRectangle(w, h);
});
```

**DSL Usage**

```
let plate = rectangle(20, 10).linear_extrude(5);
let frame = rectangle(20, 10).linear_extrude(5)
    .cut(rectangle(18, 8).translate([0, 0, -1]).linear_extrude(7));
let beam = rectangle(2, 4).linear_extrude(100);
```

**Edge Cases and Gotchas**
- The rectangle is centered at origin in XY, consistent with `createBox` centering in X and Y.
- Both width and height must be positive. Zero dimensions produce degenerate edges.
- Like `circle()`, the result is a `TopoDS_Face` and requires extrusion for 3D geometry.
- Note the difference from `createBox`: `rectangle()` produces a 2D face, while `box()` produces a 3D solid. Extruding a rectangle produces the same result as a box, but the 2D path allows for `rotate_extrude` and `sweep` which `box()` does not.

---

### 6. `polygon(points)`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createPolygon(const std::vector<std::pair<double, double>>& points);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

ShapePtr Shape::createPolygon(const std::vector<std::pair<double, double>>& points) {
    if (points.size() < 3)
        throw std::runtime_error("polygon() requires at least 3 points");

    BRepBuilderAPI_MakePolygon polyMaker;
    for (const auto& [x, y] : points) {
        polyMaker.Add(gp_Pnt(x, y, 0));
    }
    polyMaker.Close();  // Close the polygon (connect last point to first)

    if (!polyMaker.IsDone())
        throw std::runtime_error("polygon() failed to build wire");

    TopoDS_Wire wire = polyMaker.Wire();
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);

    return ShapePtr(new Shape(face));
}
```

Key OCCT classes:
- `BRepBuilderAPI_MakePolygon` -- Incrementally builds a polygonal wire from a sequence of `gp_Pnt` points. Each consecutive pair of points defines a straight edge. The `Close()` method adds the final edge connecting the last point back to the first.
- `BRepBuilderAPI_MakeFace(TopoDS_Wire)` -- Creates a planar face from the closed wire.

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("polygon", [](const std::vector<Value>& args) -> ShapePtr {
    // polygon(points) where points is a list of 2D vectors: [[x1,y1], [x2,y2], ...]
    // In the DSL, this could be:
    //   polygon([0,0], [10,0], [5,10])          -- variadic 2D vectors
    // Each arg is a 2-element vector
    if (args.size() < 3)
        throw EvalError("polygon() requires at least 3 point arguments");

    std::vector<std::pair<double, double>> points;
    for (size_t i = 0; i < args.size(); i++) {
        auto vec = args[i].asVector();
        if (vec.size() < 2)
            throw EvalError("polygon() point " + std::to_string(i) + " must be a 2D vector [x, y]");
        points.emplace_back(vec[0], vec[1]);
    }

    return Shape::createPolygon(points);
});
```

**DSL Usage**

```
// Triangle
let tri = polygon([0, 0], [10, 0], [5, 8.66]);

// Hexagon
let hex = polygon(
    [10, 0], [5, 8.66], [-5, 8.66],
    [-10, 0], [-5, -8.66], [5, -8.66]
);

// Extruded L-bracket
let lBracket = polygon(
    [0, 0], [20, 0], [20, 5], [5, 5], [5, 15], [0, 15]
).linear_extrude(10);
```

**Edge Cases and Gotchas**
- Minimum 3 points required (otherwise not a polygon).
- Points must be coplanar (they are, since all Z=0).
- Self-intersecting polygons produce invalid faces. OCCT will construct the wire but `MakeFace` may produce unexpected topology. No automatic validation is performed; the DSL user is responsible for non-self-intersecting input.
- The polygon is automatically closed by `Close()`. Users should NOT repeat the first point as the last point.
- All points are projected to Z=0 (the XY plane). If the user provides 3D vectors, the Z component is ignored.
- Winding order matters for face orientation. Counter-clockwise winding produces a face with normal pointing in +Z. This affects extrusion direction (extrusion is along the face normal by default in some OCCT APIs, but `MakePrism` uses an explicit direction vector, so winding order only affects the face normal, not the extrusion result).

---

## Extrusion and Revolution Operations

These are instance methods on `Shape` that take a 2D profile (a `TopoDS_Face` or `TopoDS_Wire` stored in `mShape`) and produce a 3D solid.

### 7. `linear_extrude(height)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr linearExtrude(double height) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepPrimAPI_MakePrism.hxx>

ShapePtr Shape::linearExtrude(double height) const {
    gp_Vec direction(0, 0, height);  // Extrude along +Z
    BRepPrimAPI_MakePrism prism(mShape, direction);
    prism.Build();
    if (!prism.IsDone())
        throw std::runtime_error("linear_extrude() failed");
    return ShapePtr(new Shape(prism.Shape()));
}
```

Key OCCT classes:
- `BRepPrimAPI_MakePrism(TopoDS_Shape, gp_Vec)` -- Sweeps any shape along a vector direction. When applied to a `TopoDS_Face`, produces a solid. When applied to a `TopoDS_Wire`, produces a shell. When applied to a `TopoDS_Edge`, produces a face. The vector determines both direction and distance.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("linear_extrude", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.linear_extrude(height)
    if (args.size() != 1)
        throw EvalError("linear_extrude() expects 1 argument: height");
    double height = args[0].asNumber();
    if (height == 0)
        throw EvalError("linear_extrude() height must be non-zero");
    return self->linearExtrude(height);
});
```

**DSL Usage**

```
let cylinder_from_circle = circle(10).linear_extrude(50);
let box_from_rect = rectangle(20, 10).linear_extrude(5);
let prism = polygon([0,0], [10,0], [5,8.66]).linear_extrude(20);
```

**Edge Cases and Gotchas**
- The input shape should be a `TopoDS_Face` (from a 2D primitive) for solid output. If the input is already a solid, `MakePrism` will extrude each face of the solid, producing a non-manifold result. Consider adding a shape-type check.
- Negative height values extrude in the -Z direction. This is valid and useful.
- Zero height produces a degenerate shape and should be rejected.
- The extrusion is always along Z. For arbitrary direction extrusion, the user should rotate the profile first, or a future `linear_extrude(height, direction)` overload could be added.

---

### 8. `rotate_extrude(angle)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr rotateExtrude(double angleDeg = 360.0) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepPrimAPI_MakeRevol.hxx>

ShapePtr Shape::rotateExtrude(double angleDeg) const {
    double angleRad = angleDeg * M_PI / 180.0;
    gp_Ax1 axis(gp_Pnt(0, 0, 0), gp::DZ());  // Revolve around Z axis

    BRepPrimAPI_MakeRevol revol(mShape, axis, angleRad);
    revol.Build();
    if (!revol.IsDone())
        throw std::runtime_error("rotate_extrude() failed");
    return ShapePtr(new Shape(revol.Shape()));
}
```

Key OCCT classes:
- `BRepPrimAPI_MakeRevol(TopoDS_Shape, gp_Ax1, angle)` -- Revolves a shape around an axis by the specified angle (in radians). When applied to a face, produces a solid of revolution. When the angle is 2*pi (360 degrees), a full revolution produces a closed solid. The axis is defined by a point and a direction (`gp_Ax1`).

**ShapeRegistry Entry**

```cpp
registry.registerMethod("rotate_extrude", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.rotate_extrude()       -- full 360 degree revolution
    // shape.rotate_extrude(angle)  -- partial revolution in degrees
    double angle = 360.0;
    if (args.size() >= 1)
        angle = args[0].asNumber();
    if (args.size() > 1)
        throw EvalError("rotate_extrude() expects 0 or 1 arguments");
    if (angle <= 0 || angle > 360)
        throw EvalError("rotate_extrude() angle must be in range (0, 360]");
    return self->rotateExtrude(angle);
});
```

**DSL Usage**

```
// Full torus from a circle offset from origin
let torus = circle(3).translate([10, 0, 0]).rotate_extrude();

// Quarter pipe
let quarterPipe = circle(2).translate([8, 0, 0]).rotate_extrude(90);

// Vase profile (revolved polygon)
let vase = polygon(
    [5, 0], [7, 5], [6, 15], [4, 20], [4, 22], [5, 22], [5, 0]
).rotate_extrude();
```

**Edge Cases and Gotchas**
- The profile must not cross the revolution axis (Z axis). If the face straddles the axis, OCCT will produce a self-intersecting solid. The profile should be entirely on one side of the axis (typically positive X).
- The default angle is 360 degrees (full revolution). Partial angles produce an open solid with planar end caps.
- The revolution axis is always the Z axis in this implementation. The profile should be positioned in the XZ plane (positive X side) and will be swept around Z. Since 2D primitives lie in the XY plane, the user may need to rotate the profile first, OR the implementation should revolve around the Y axis. **Decision**: Revolve around Z, consistent with OpenSCAD convention. The 2D profile at Z=0 in the XY plane gets swept around Z.
- For `rotate_extrude` to produce meaningful results, the profile (face) must be offset from the Z axis. A face centered at origin will self-intersect.

---

### 9. `sweep(pathShape)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr sweep(const ShapePtr& pathShape) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepOffsetAPI_MakePipe.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

ShapePtr Shape::sweep(const ShapePtr& pathShape) const {
    // Extract the wire from the path shape
    // The path should be a wire (1D path) -- extract from the shape
    TopoDS_Wire spine;
    TopExp_Explorer wireExplorer(pathShape->getShape(), TopAbs_WIRE);
    if (wireExplorer.More()) {
        spine = TopoDS::Wire(wireExplorer.Current());
    } else {
        // Try to get an edge and convert to wire
        TopExp_Explorer edgeExplorer(pathShape->getShape(), TopAbs_EDGE);
        if (edgeExplorer.More()) {
            BRepBuilderAPI_MakeWire wireMaker;
            for (; edgeExplorer.More(); edgeExplorer.Next()) {
                wireMaker.Add(TopoDS::Edge(edgeExplorer.Current()));
            }
            spine = wireMaker.Wire();
        } else {
            throw std::runtime_error("sweep() path must contain a wire or edges");
        }
    }

    BRepOffsetAPI_MakePipe pipe(spine, mShape);
    pipe.Build();
    if (!pipe.IsDone())
        throw std::runtime_error("sweep() failed");
    return ShapePtr(new Shape(pipe.Shape()));
}
```

Key OCCT classes:
- `BRepOffsetAPI_MakePipe(TopoDS_Wire spine, TopoDS_Shape profile)` -- Sweeps a profile shape along a spine wire. The profile is placed at the start of the spine and swept to the end, producing a solid (if the profile is a face) or shell (if the profile is a wire). The profile's orientation follows the spine tangent via a Frenet trihedron by default.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("sweep", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // profile.sweep(path)
    if (args.size() != 1)
        throw EvalError("sweep() expects 1 argument: path shape");
    ShapePtr path = args[0].asShape();
    return self->sweep(path);
});
```

**DSL Usage**

```
// Sweep a circle along a path (requires a path-creation mechanism)
// For Phase 1, paths are typically edges from other shapes or dedicated path constructors

// Curved pipe (sweep circle along an arc -- requires arc primitive, future work)
// let pipe = circle(2).sweep(arc_path);

// Straight sweep is equivalent to linear_extrude but allows arbitrary directions
// (This example requires a line/edge constructor, deferred to later phase)
```

**Edge Cases and Gotchas**
- The path (spine) must be a `TopoDS_Wire` or contain extractable edges. If the path shape is a face or solid, the wire extraction may pick an arbitrary wire.
- The profile is positioned at the start of the path. Its local coordinate system must be compatible with the spine's start tangent.
- Self-intersecting sweeps (tight curves with large profiles) produce invalid geometry. OCCT does not automatically detect this.
- Path shapes are not yet easily creatable in the DSL (no arc, spline, or helix primitives). Sweep will become more useful once path constructors are added in a future phase.
- `MakePipe` uses a Frenet trihedron, which can twist the profile unexpectedly on certain curves. For better control, `BRepOffsetAPI_MakePipeShell` provides auxiliary spine and contact modes.

---

### 10. `loft(profiles[])`

**C++ Method Signature**

```cpp
// Shape.h
static ShapePtr createLoft(const std::vector<ShapePtr>& profiles, bool solid = true, bool ruled = false);
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepOffsetAPI_ThruSections.hxx>

ShapePtr Shape::createLoft(const std::vector<ShapePtr>& profiles, bool solid, bool ruled) {
    if (profiles.size() < 2)
        throw std::runtime_error("loft() requires at least 2 profiles");

    BRepOffsetAPI_ThruSections loft(solid ? Standard_True : Standard_False,
                                     ruled ? Standard_True : Standard_False);

    for (const auto& profile : profiles) {
        // Extract wire from each profile
        TopExp_Explorer wireExp(profile->getShape(), TopAbs_WIRE);
        if (wireExp.More()) {
            loft.AddWire(TopoDS::Wire(wireExp.Current()));
        } else {
            // If the profile is a vertex (point), add as vertex for tapering
            TopExp_Explorer vertExp(profile->getShape(), TopAbs_VERTEX);
            if (vertExp.More()) {
                loft.AddVertex(TopoDS::Vertex(vertExp.Current()));
            } else {
                throw std::runtime_error("loft() profile must contain a wire or vertex");
            }
        }
    }

    loft.Build();
    if (!loft.IsDone())
        throw std::runtime_error("loft() failed");
    return ShapePtr(new Shape(loft.Shape()));
}
```

Key OCCT classes:
- `BRepOffsetAPI_ThruSections(isSolid, isRuled)` -- Constructs a shape by lofting through a sequence of wire profiles. `isSolid=true` produces a solid; `false` produces a shell. `isRuled=true` creates ruled (flat) surfaces between profiles; `false` creates smooth (interpolated) surfaces.
- `ThruSections::AddWire(TopoDS_Wire)` -- Adds a cross-section profile. Profiles must be added in order from bottom to top.
- `ThruSections::AddVertex(TopoDS_Vertex)` -- Adds a point as a degenerate profile (allows tapering to a point).

**ShapeRegistry Entry**

```cpp
registry.registerConstructor("loft", [](const std::vector<Value>& args) -> ShapePtr {
    // loft(profile1, profile2, ...)
    // All args must be shapes (2D profiles)
    if (args.size() < 2)
        throw EvalError("loft() requires at least 2 profile arguments");

    std::vector<ShapePtr> profiles;
    for (size_t i = 0; i < args.size(); i++) {
        profiles.push_back(args[i].asShape());
    }

    return Shape::createLoft(profiles);
});
```

**DSL Usage**

```
// Transition from circle to rectangle
let transition = loft(
    circle(10),
    circle(15).z(10),
    rectangle(20, 20).z(25)
);

// Tapered column (two circles at different heights and radii)
let column = loft(
    circle(8),
    circle(5).z(30)
);

// Funnel shape
let funnel = loft(
    circle(20),
    circle(5).z(40)
);
```

**Edge Cases and Gotchas**
- At least 2 profiles required. Profiles must be ordered along the loft direction.
- All profiles should have compatible topology (same number of edges if possible). Mismatched edge counts cause OCCT to apply heuristic edge matching, which may produce twisted surfaces.
- Profiles must be at different Z positions (or different positions along the loft axis). Coincident profiles produce zero-thickness geometry.
- The `loft()` constructor is a static factory (not a method chain), since it takes multiple independent profiles as arguments.
- Wire extraction from faces: `createCircle` and `createRectangle` produce `TopoDS_Face` objects. The loft needs `TopoDS_Wire`. The implementation uses `TopExp_Explorer` to extract the outer wire from each face.
- Lofting to a point requires a `TopoDS_Vertex`. A future `point(x,y,z)` constructor could enable this, but for Phase 1, lofts will only support wire-based profiles.

---

## Additional Operations

### 11. `chamfer(distance)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr chamfer(double distance) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopExp.hxx>

ShapePtr Shape::chamfer(double distance) const {
    BRepFilletAPI_MakeChamfer mk(mShape);

    // Build edge-to-face map for chamfer (chamfer requires a reference face per edge)
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(mShape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    for (TopExp_Explorer ex(mShape, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge edge = TopoDS::Edge(ex.Current());
        // Get an adjacent face for this edge
        const TopTools_ListOfShape& faces = edgeFaceMap.FindFromKey(edge);
        if (!faces.IsEmpty()) {
            TopoDS_Face face = TopoDS::Face(faces.First());
            mk.Add(distance, edge, face);
        }
    }

    mk.Build();
    if (!mk.IsDone()) {
        std::cerr << termcolor::red << "Unable to chamfer edges" << "\n";
        return ShapePtr(new Shape(mShape));  // Return unchanged shape on failure
    }

    return ShapePtr(new Shape(mk.Shape()));
}
```

Key OCCT classes:
- `BRepFilletAPI_MakeChamfer` -- Creates chamfers (beveled edges) on a solid shape. Unlike `MakeFillet`, chamfer requires specifying a reference face for each edge, because the chamfer distance is measured along that face.
- `TopExp::MapShapesAndAncestors` -- Builds a map from sub-shapes (edges) to their parent shapes (faces). This is needed to find which face each edge belongs to.
- `MakeChamfer::Add(distance, edge, face)` -- Adds a chamfer to a specific edge with a given distance, measured on the specified face. An overload `Add(d1, d2, edge, face)` allows asymmetric chamfers.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("chamfer", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.chamfer(distance)
    if (args.size() != 1)
        throw EvalError("chamfer() expects 1 argument: distance");
    double distance = args[0].asNumber();
    if (distance <= 0)
        throw EvalError("chamfer() distance must be positive");
    return self->chamfer(distance);
});
```

**DSL Usage**

```
let chamferedBox = box(20, 20, 10).chamfer(2);
let mixed = box(20, 20, 10).chamfer(1).fillet(0.5);
```

**Edge Cases and Gotchas**
- Like `fillet()`, `chamfer()` applies to ALL edges. Selective chamfering (only specific edges) is not supported in Phase 1. Selective edge operations will require an edge selection mechanism in a future phase.
- Chamfer distance must be smaller than the shortest edge. Excessively large chamfers cause `IsDone()` to return false.
- The edge-face map is essential. `MakeChamfer` fails without a reference face for each edge. This is the key difference from `MakeFillet`, which only needs edges.
- `chamfer()` on a shape with fillets (or vice versa) can fail if the resulting geometry becomes self-intersecting. Apply the larger operation first.

---

### 12. `shell(thickness)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr shell(double thickness) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <TopTools_ListOfShape.hxx>

ShapePtr Shape::shell(double thickness) const {
    // Shell by removing the top face (highest Z face) and offsetting inward
    // Find the face with the highest average Z coordinate
    TopoDS_Face topFace;
    double maxZ = -1e99;

    for (TopExp_Explorer faceExp(mShape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        TopoDS_Face face = TopoDS::Face(faceExp.Current());
        // Compute bounding box of face to find its average Z
        Bnd_Box bbox;
        BRepBndLib::Add(face, bbox);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double avgZ = (zmin + zmax) / 2.0;
        if (avgZ > maxZ) {
            maxZ = avgZ;
            topFace = face;
        }
    }

    TopTools_ListOfShape facesToRemove;
    facesToRemove.Append(topFace);

    BRepOffsetAPI_MakeThickSolid thickSolid;
    thickSolid.MakeThickSolidByJoin(mShape, facesToRemove, -thickness, 1e-3);
    thickSolid.Build();

    if (!thickSolid.IsDone())
        throw std::runtime_error("shell() failed");

    return ShapePtr(new Shape(thickSolid.Shape()));
}
```

Key OCCT classes:
- `BRepOffsetAPI_MakeThickSolid` -- Creates a hollow solid (shell) from a solid shape by removing specified faces and offsetting the remaining faces by a thickness. The removed faces become the "openings" of the shell.
- `MakeThickSolidByJoin(shape, facesToRemove, offset, tolerance)` -- The main method. A negative offset thickens inward; positive offset thickens outward. The tolerance parameter controls geometric precision (typically 1e-3 for mm-scale models).
- `Bnd_Box` and `BRepBndLib` -- Used to compute bounding boxes for face selection heuristics.

Additional includes needed:

```cpp
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
```

**ShapeRegistry Entry**

```cpp
registry.registerMethod("shell", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.shell(thickness)
    if (args.size() != 1)
        throw EvalError("shell() expects 1 argument: thickness");
    double thickness = args[0].asNumber();
    if (thickness <= 0)
        throw EvalError("shell() thickness must be positive");
    return self->shell(thickness);
});
```

**DSL Usage**

```
let hollowBox = box(20, 20, 20).shell(1);
let cup = cylinder(10, 30).shell(1.5);
let bin = box(80, 120, 20).fillet(2).shell(1.2);
```

**Edge Cases and Gotchas**
- The current implementation automatically removes the "top face" (highest Z). This is a heuristic that works for boxes and cylinders in standard orientation but may fail for rotated or complex shapes. A more flexible API would accept a face selector or direction vector.
- Thickness must be positive. Negative values would offset outward, which may be unexpected for "shell" semantics.
- `MakeThickSolid` is computationally expensive on complex shapes with many faces. Performance should be monitored.
- The tolerance parameter (`1e-3`) may need adjustment for very small or very large models. Consider exposing tolerance as an optional parameter in a future phase.
- Shelling fails on shapes with very thin features or sharp concavities where the offset surface self-intersects. Apply fillets before shelling when possible.

---

### 13. `mirror(nx, ny, nz)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr mirror(double nx, double ny, double nz) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
ShapePtr Shape::mirror(double nx, double ny, double nz) const {
    gp_Ax2 mirrorPlane(gp_Pnt(0, 0, 0), gp_Dir(nx, ny, nz));
    gp_Trsf trsf;
    trsf.SetMirror(mirrorPlane);

    return ShapePtr(new Shape(BRepBuilderAPI_Transform(mShape, trsf, true).Shape()));
}
```

Key OCCT classes:
- `gp_Trsf::SetMirror(gp_Ax2)` -- Sets the transformation to a mirror reflection about a plane defined by the `gp_Ax2` (point + normal direction). The plane passes through the `gp_Ax2` origin and is perpendicular to its Z direction.
- `gp_Ax2(gp_Pnt, gp_Dir)` -- Defines a coordinate system by origin point and Z direction. For mirroring, only the origin and Z direction matter (the Z direction is the plane normal).
- `BRepBuilderAPI_Transform(shape, trsf, copy)` -- Applies a transformation to a shape. When `copy=true`, a new shape is created (the original is preserved). This is the same transform class used by `rotate()`.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("mirror", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.mirror(nx, ny, nz)  -- mirror across plane through origin with normal (nx,ny,nz)
    // OR: shape.mirror([nx, ny, nz])  -- vector form
    double nx, ny, nz;
    if (args.size() == 1 && args[0].isVector()) {
        auto vec = args[0].asVector();
        if (vec.size() != 3)
            throw EvalError("mirror() vector must have 3 components");
        nx = vec[0]; ny = vec[1]; nz = vec[2];
    } else if (args.size() == 3) {
        nx = args[0].asNumber();
        ny = args[1].asNumber();
        nz = args[2].asNumber();
    } else {
        throw EvalError("mirror() expects 3 numbers or a 3D vector");
    }
    if (nx == 0 && ny == 0 && nz == 0)
        throw EvalError("mirror() normal vector must be non-zero");
    return self->mirror(nx, ny, nz);
});
```

**DSL Usage**

```
let halfShape = box(10, 20, 5).translate([5, 0, 0]);
let mirrored = halfShape.mirror(1, 0, 0);
let fullShape = halfShape.fuse(mirrored);

// Mirror across XZ plane (Y=0)
let sym = sphere(5).translate([0, 10, 0]).mirror(0, 1, 0);

// Mirror using vector form
let reflected = box(10, 10, 10).translate([15, 0, 0]).mirror([1, 0, 0]);
```

**Edge Cases and Gotchas**
- The mirror plane always passes through the origin. To mirror about an arbitrary plane, the user must translate, mirror, then translate back. A future overload `mirror(nx, ny, nz, px, py, pz)` could accept a point on the plane.
- The normal vector `(nx, ny, nz)` is automatically normalized by `gp_Dir`. Zero-length vectors cause `Standard_ConstructionError`.
- Mirroring reverses the shape's orientation (face normals flip). This can affect boolean operations. `BRepBuilderAPI_Transform` with `copy=true` handles this correctly.
- `mirror()` only mirrors, it does not fuse. To create a symmetric shape, chain with `.fuse()`.

---

### 14. `scale(factor)` / `scale(fx, fy, fz)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr scale(double factor) const;                         // Uniform scale
ShapePtr scale(double fx, double fy, double fz) const;      // Non-uniform scale
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepBuilderAPI_GTransform.hxx>
#include <gp_GTrsf.hxx>

ShapePtr Shape::scale(double factor) const {
    gp_Trsf trsf;
    trsf.SetScale(gp_Pnt(0, 0, 0), factor);
    return ShapePtr(new Shape(BRepBuilderAPI_Transform(mShape, trsf, true).Shape()));
}

ShapePtr Shape::scale(double fx, double fy, double fz) const {
    // Non-uniform scaling requires gp_GTrsf (general transformation)
    gp_GTrsf gtrsf;
    gtrsf.SetValue(1, 1, fx);  // Scale X
    gtrsf.SetValue(2, 2, fy);  // Scale Y
    gtrsf.SetValue(3, 3, fz);  // Scale Z

    BRepBuilderAPI_GTransform transform(mShape, gtrsf, true);
    transform.Build();
    if (!transform.IsDone())
        throw std::runtime_error("scale() non-uniform scaling failed");

    return ShapePtr(new Shape(transform.Shape()));
}
```

Key OCCT classes:
- `gp_Trsf::SetScale(gp_Pnt, factor)` -- Sets a uniform scaling transformation about a point. Only uniform (isotropic) scaling is supported by `gp_Trsf`.
- `gp_GTrsf` -- General transformation matrix (3x4). Supports non-uniform (anisotropic) scaling by setting diagonal elements independently. Cannot be used with `BRepBuilderAPI_Transform`; requires `BRepBuilderAPI_GTransform`.
- `BRepBuilderAPI_GTransform(shape, gtrsf, copy)` -- Applies a general transformation. This handles the non-uniform case where `gp_Trsf` (which is limited to similarity transformations) is insufficient.
- `gp_GTrsf::SetValue(row, col, value)` -- Sets individual matrix elements. Rows/columns are 1-indexed. The diagonal elements `(1,1)`, `(2,2)`, `(3,3)` control scaling in X, Y, Z respectively.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("scale", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.scale(factor)           -- uniform
    // shape.scale(fx, fy, fz)      -- non-uniform
    // shape.scale([fx, fy, fz])    -- non-uniform (vector form)
    if (args.size() == 1) {
        if (args[0].isVector()) {
            auto vec = args[0].asVector();
            if (vec.size() != 3)
                throw EvalError("scale() vector must have 3 components");
            return self->scale(vec[0], vec[1], vec[2]);
        }
        double f = args[0].asNumber();
        if (f == 0)
            throw EvalError("scale() factor must be non-zero");
        return self->scale(f);
    } else if (args.size() == 3) {
        double fx = args[0].asNumber();
        double fy = args[1].asNumber();
        double fz = args[2].asNumber();
        if (fx == 0 || fy == 0 || fz == 0)
            throw EvalError("scale() factors must be non-zero");
        return self->scale(fx, fy, fz);
    }
    throw EvalError("scale() expects 1 or 3 arguments");
});
```

**DSL Usage**

```
let doubled = sphere(5).scale(2);
let ellipsoid = sphere(10).scale(1, 0.5, 2);
let stretched = box(10, 10, 10).scale([2, 1, 0.5]);
```

**Edge Cases and Gotchas**
- Uniform scaling with `factor=1` is a no-op. Negative factors produce a mirror+scale effect.
- Non-uniform scaling with `gp_GTrsf` converts B-Rep geometry from analytic to NURBS representation. This means a sphere scaled non-uniformly becomes a NURBS solid, losing exact circular/spherical representation. Subsequent boolean operations may be slower.
- Zero scale factors produce degenerate geometry and should be rejected.
- Scaling is always about the origin. To scale about a different center, translate to origin, scale, translate back.
- `BRepBuilderAPI_GTransform` can fail on complex shapes with certain topologies. Test thoroughly.

---

### 15. `intersect(other)`

**C++ Method Signature**

```cpp
// Shape.h
ShapePtr intersect(const ShapePtr& other) const;
```

**OCCT Implementation**

```cpp
// Shape.cpp
#include <BRepAlgoAPI_Common.hxx>

ShapePtr Shape::intersect(const ShapePtr& other) const {
    BRepAlgoAPI_Common common(mShape, other->getShape());
    common.SetRunParallel(Standard_True);
    common.SetNonDestructive(true);
    common.Build();

    if (!common.IsDone()) {
        std::cerr << termcolor::red << "Intersect failed" << "\n";
    }

    return ShapePtr(new Shape(common.Shape()));
}
```

Key OCCT classes:
- `BRepAlgoAPI_Common` -- Computes the boolean intersection (common volume) of two shapes. This is the dual of `BRepAlgoAPI_Cut` and `BRepAlgoAPI_Fuse`. The result contains only the volume that belongs to BOTH input shapes.
- The API is identical to `BRepAlgoAPI_Fuse` and `BRepAlgoAPI_Cut` in the existing codebase, with `SetRunParallel`, `SetNonDestructive`, and `Build()` following the same pattern.

**ShapeRegistry Entry**

```cpp
registry.registerMethod("intersect", [](ShapePtr self, const std::vector<Value>& args) -> ShapePtr {
    // shape.intersect(other)
    if (args.size() != 1)
        throw EvalError("intersect() expects 1 argument: shape to intersect with");
    ShapePtr other = args[0].asShape();
    return self->intersect(other);
});
```

**DSL Usage**

```
// Rounded cube: intersection of sphere and box
let roundedCube = box(20, 20, 20).intersect(sphere(14));

// Lens shape: intersection of two spheres
let lens = sphere(15).intersect(sphere(15).translate([10, 0, 0]));

// Cross-section slice
let slice = box(100, 100, 1).intersect(complexShape);
```

**Edge Cases and Gotchas**
- If the two shapes do not overlap, the result is an empty/null shape. The `isValid()` check may return false, or the shape may be a null compound. Handle gracefully in the evaluator.
- Same performance characteristics as `fuse()` and `cut()`. Parallel execution is enabled.
- The `SetNonDestructive(true)` flag preserves the input shapes, consistent with the immutable `ShapePtr` pattern.

---

### 16. `hull` -- Deferred

Convex hull is conceptually simple but OCCT does not provide a direct B-Rep convex hull API. Implementation would require:

1. Triangulating all input shapes.
2. Extracting vertex coordinates.
3. Running a convex hull algorithm (e.g., Quickhull) on the point cloud.
4. Converting the hull triangles back to B-Rep via `BRepBuilderAPI_Sewing` or `BRepBuilderAPI_MakeSolid`.

This is a significant implementation effort and is deferred to Phase 2 or later. A third-party library (e.g., CGAL, or a standalone quickhull implementation) would simplify this considerably.

---

### 17. `minkowski` -- Deferred

Minkowski sum of two 3D shapes is not natively supported by OCCT. The general algorithm requires:

1. Computing the Minkowski sum as a point-set operation.
2. Reconstructing the B-Rep boundary.

This is an extremely complex geometric operation. OpenSCAD implements it via CGAL's `Nef_polyhedron_3::minkowski_sum_3()`. Since OpenDCAD uses OCCT (not CGAL), implementing Minkowski sum would require either:

- Integrating CGAL as a dependency for this single operation.
- Implementing a voxel-based approximation.
- Using mesh-based Minkowski sum algorithms.

Deferred to a future phase when the cost/benefit is better understood.

---

## Implementation Order

The following order minimizes dependencies and builds from simplest to most complex. Each step can be implemented, tested, and committed independently.

### Tier 1: Standalone 3D Primitives (no dependencies on other new code)

| Order | Item | Rationale |
|-------|------|-----------|
| 1 | `sphere(radius)` | Simplest new primitive. Single OCCT call. Immediate visual verification. |
| 2 | `cone(r1, r2, height)` | Similar complexity to existing `createCylinder`. Single OCCT call. |
| 3 | `wedge(dx, dy, dz, ltx)` | Single OCCT call but requires axis convention decision. |
| 4 | `intersect(other)` | Follows the exact same pattern as existing `fuse()` and `cut()`. Copy-paste with class swap. |
| 5 | `chamfer(distance)` | Similar to existing `fillet()` but requires edge-face map. |

### Tier 2: 2D Primitives (needed before extrusion operations)

| Order | Item | Rationale |
|-------|------|-----------|
| 6 | `circle(radius)` | Foundation for `rotate_extrude` testing. Multi-step OCCT (Circ -> Edge -> Wire -> Face). |
| 7 | `rectangle(width, height)` | Foundation for extrusion testing. 4 edges -> Wire -> Face. |
| 8 | `polygon(points)` | Most complex 2D primitive due to variadic point input. |

### Tier 3: Extrusion Operations (depend on 2D primitives)

| Order | Item | Rationale |
|-------|------|-----------|
| 9 | `linear_extrude(height)` | Simplest extrusion. Test with circle, rectangle, polygon. |
| 10 | `rotate_extrude(angle)` | Requires 2D profile offset from axis. Test with circle. |
| 11 | `loft(profiles[])` | Multi-profile operation. Requires working 2D primitives at different Z levels. |
| 12 | `sweep(path)` | Most complex extrusion. Limited usefulness until path constructors exist. |

### Tier 4: Remaining Operations

| Order | Item | Rationale |
|-------|------|-----------|
| 13 | `scale(factor)` | Uniform scaling is straightforward (`gp_Trsf`). |
| 14 | `scale(fx, fy, fz)` | Non-uniform scaling requires `gp_GTrsf`. Separate from uniform for testing. |
| 15 | `mirror(nx, ny, nz)` | Straightforward `gp_Trsf::SetMirror`. |
| 16 | `shell(thickness)` | Most complex remaining operation. Face selection heuristic needs testing. |

---

## Required OCCT Includes Summary

New includes needed in `Shape.cpp` beyond what already exists:

```cpp
// 3D Primitives
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>

// 2D Primitives
#include <gp_Circ.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>

// Extrusion / Revolution
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>

// Additional Operations
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <gp_GTrsf.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopExp.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
```

## Required CMake Changes

The `link_occt` function in `CMakeLists.txt` must add the following OCCT toolkit dependencies (some may already be transitively included):

```cmake
set(_occt_components
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKTopAlgo TKSTL
    TKPrim          # BRepPrimAPI_MakeSphere, MakeCone, MakeWedge, MakePrism, MakeRevol
    TKFillet        # BRepFilletAPI_MakeChamfer (already have MakeFillet)
    TKBO            # BRepAlgoAPI_Common (already have Fuse, Cut)
    TKOffset        # BRepOffsetAPI_MakeThickSolid, MakePipe, ThruSections
    TKShHealing     # ShapeFix_Shape (already used)
)
```

Verify which toolkits are already linked transitively. At minimum, `TKOffset` will likely need to be explicitly added for `MakeThickSolid`, `MakePipe`, and `ThruSections`.

---

## Comprehensive Test Script

The following `.dcad` script exercises all new Phase 1 primitives and operations. It should be saved as `examples/phase1_test.dcad` and used for manual verification after implementation.

```
// ============================================================
// Phase 1 Primitives Test Script
// Exercises all new primitives and operations
// ============================================================

// --- 3D Primitives ---

// 1. Sphere
let ball = sphere(10);
let hollowBall = sphere(10).cut(sphere(8));

// 2. Cone
let pointedCone = cone(10, 0, 25);
let frustum = cone(10, 5, 20);

// 3. Wedge
let ramp = wedge(20, 10, 30, 0);
let trapezoidBlock = wedge(20, 10, 30, 10);

// --- 2D Primitives + Extrusion ---

// 4. Circle -> linear_extrude (should equal a cylinder)
let extrudedCircle = circle(8).linear_extrude(30);

// 5. Rectangle -> linear_extrude (should equal a box)
let extrudedRect = rectangle(20, 10).linear_extrude(5);

// 6. Polygon -> linear_extrude (triangular prism)
let triPrism = polygon([0, 0], [20, 0], [10, 17.32]).linear_extrude(15);

// 7. Circle -> rotate_extrude (torus)
let torusFromCircle = circle(3).translate([10, 0, 0]).rotate_extrude();

// 8. Partial revolution (quarter torus)
let quarterTorus = circle(2).translate([8, 0, 0]).rotate_extrude(90);

// 9. Loft between two circles at different Z
let taperedCylinder = loft(circle(10), circle(5).z(30));

// --- Additional Operations ---

// 10. Chamfer
let chamferedBox = box(20, 20, 10).chamfer(2);

// 11. Scale (uniform)
let bigSphere = sphere(5).scale(3);

// 12. Scale (non-uniform -> ellipsoid)
let ellipsoid = sphere(10).scale(1, 0.5, 2);

// 13. Mirror
let halfPart = box(10, 20, 5).translate([5, 0, 0]);
let fullPart = halfPart.fuse(halfPart.mirror(1, 0, 0));

// 14. Intersect (rounded cube)
let roundedCube = box(20, 20, 20).intersect(sphere(14));

// 15. Shell (hollow box open at top)
let openBox = box(30, 30, 20).shell(2);

// --- Combined Operations ---

// 16. Complex part: filleted, shelled, with holes
let base = box(60, 40, 15).fillet(2).shell(1.5);
let hole = cylinder(4, 20).translate([0, 0, -5]);
let drilledBase = base
    .cut(hole.translate([20, 10, 0]))
    .cut(hole.translate([-20, 10, 0]))
    .cut(hole.translate([20, -10, 0]))
    .cut(hole.translate([-20, -10, 0]));

// 17. Vase (revolved polygon profile)
let vaseProfile = polygon(
    [5, 0], [8, 3], [7, 10], [9, 15], [6, 25], [5, 28], [5, 0]
);
let vase = vaseProfile.rotate_extrude();

// 18. Symmetric bracket (mirror + fuse)
let bracket = box(10, 5, 20)
    .fuse(box(20, 5, 5))
    .fillet(1);
let symBracket = bracket.fuse(bracket.mirror(0, 1, 0));

// 19. Transition piece (loft from circle to rectangle)
let transition = loft(
    circle(15),
    rectangle(20, 20).z(30)
);

// 20. Extruded L-bracket from polygon
let lBracket = polygon(
    [0, 0], [30, 0], [30, 5], [5, 5], [5, 20], [0, 20]
).linear_extrude(10);

// --- Exports ---
export hollowBall as hollow_ball;
export frustum as frustum;
export extrudedCircle as extruded_circle;
export triPrism as tri_prism;
export torusFromCircle as torus_from_circle;
export chamferedBox as chamfered_box;
export roundedCube as rounded_cube;
export openBox as open_box;
export drilledBase as drilled_base;
export vase as vase;
export symBracket as sym_bracket;
export lBracket as l_bracket;
```

### Expected Results

| Export Name | Expected Geometry |
|-------------|-------------------|
| `hollow_ball` | Spherical shell, wall thickness 2mm |
| `frustum` | Truncated cone, r1=10, r2=5, h=20 |
| `extruded_circle` | Cylinder equivalent, r=8, h=30 |
| `tri_prism` | Triangular cross-section prism, h=15 |
| `torus_from_circle` | Torus, major r=10, minor r=3 |
| `chamfered_box` | Box with 2mm chamfers on all edges |
| `rounded_cube` | Cube with sphere intersection giving rounded corners |
| `open_box` | Box with top face removed, wall thickness 2mm |
| `drilled_base` | Filleted+shelled box with 4 through-holes |
| `vase` | Revolved vase profile |
| `sym_bracket` | L-bracket mirrored across Y plane |
| `l_bracket` | Extruded L-shaped polygon cross-section |

### Failure Modes to Watch For

1. **Shape healing**: All exported shapes pass through `ShapeFix_Shape`. If any primitive produces topologically invalid geometry, the healing step may silently alter the shape or fail.
2. **STL tessellation**: Very small features (e.g., `shell(0.1)` on a large shape) may not tessellate properly at the default deflection. The rough STL (deflection=0.05) may miss thin walls.
3. **Boolean operation tolerance**: `intersect()` on nearly-tangent shapes (e.g., sphere radius very close to half the box diagonal) may produce fragmented results.
4. **2D/3D mixing**: Attempting boolean operations on 2D faces (before extrusion) should produce a clear error, not silent corruption.

---

## Grammar Considerations

The current `OpenDCAD.g4` grammar already supports all the DSL syntax needed for Phase 1:

- **Constructor calls**: `sphere(10)`, `cone(10, 5, 20)`, `polygon([0,0], [10,0], [5,8.66])` all match the `call : IDENT '(' argList ')'` rule.
- **Method chains**: `.linear_extrude(30)`, `.chamfer(2)`, `.mirror(1,0,0)` all match `methodCall : IDENT '(' argList ')'`.
- **Vector arguments**: `[0, 0, -35]` matches `vectorLiteral`.
- **Arithmetic in arguments**: `21/2` in argument position matches `expr`.
- **Static constructors with shape args**: `loft(circle(10), circle(5).z(30))` works because `argList` accepts `expr`, and a `postfix` (chain) is a valid `primary`, which is a valid `expr`.

No grammar changes are required for Phase 1.

---

## Header File Changes Summary

New declarations to add to `src/Shape.h`:

```cpp
class Shape {
public:
    // ... existing declarations ...

    // Phase 1: 3D Primitives
    static ShapePtr createSphere(double radius);
    static ShapePtr createCone(double r1, double r2, double height);
    static ShapePtr createWedge(double dx, double dy, double dz, double ltx);

    // Phase 1: 2D Primitives (produce TopoDS_Face)
    static ShapePtr createCircle(double radius);
    static ShapePtr createRectangle(double width, double height);
    static ShapePtr createPolygon(const std::vector<std::pair<double, double>>& points);

    // Phase 1: Extrusion / Revolution
    ShapePtr linearExtrude(double height) const;
    ShapePtr rotateExtrude(double angleDeg = 360.0) const;
    ShapePtr sweep(const ShapePtr& pathShape) const;
    static ShapePtr createLoft(const std::vector<ShapePtr>& profiles,
                                bool solid = true, bool ruled = false);

    // Phase 1: Additional Operations
    ShapePtr chamfer(double distance) const;
    ShapePtr shell(double thickness) const;
    ShapePtr mirror(double nx, double ny, double nz) const;
    ShapePtr scale(double factor) const;
    ShapePtr scale(double fx, double fy, double fz) const;
    ShapePtr intersect(const ShapePtr& other) const;
};
```

Additional includes needed in `Shape.h`:

```cpp
#include <vector>
#include <utility>  // for std::pair
```

---

## Phase 1 Completion Criteria

Phase 1 is complete when:

1. All 16 new methods compile and link without errors.
2. The `phase1_test.dcad` script parses, evaluates, and exports all 12 shapes to STEP and STL.
3. Each exported STEP file opens correctly in a STEP viewer (e.g., FreeCAD) with expected geometry.
4. Each exported STL file is watertight (no holes) and has a reasonable triangle count at deflection=0.01.
5. Edge case inputs (zero radius, negative height, empty polygon, non-overlapping intersect) produce clear error messages rather than crashes.
6. No memory leaks (all geometry flows through `ShapePtr` / `std::shared_ptr<Shape>`).
