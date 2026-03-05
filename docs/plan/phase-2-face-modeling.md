# Phase 2: Face-Relative Modeling

## Goal

Enable building geometry relative to existing topological faces. Users should be able to select a face on an existing shape, establish a local coordinate system (workplane) on that face, sketch 2D profiles, and extrude or cut them --- producing new solids that are automatically fused to or subtracted from the parent shape. This is the foundation for parametric, history-aware CAD modeling in OpenDCAD, analogous to CadQuery's Workplane concept.

## Motivation

Phase 0 established the pipeline: `.dcad` source to parse tree to evaluated geometry to STEP/STL export. Phase 1 (assumed) added additional primitives. However, all geometry is still positioned in global coordinates with explicit `translate()` and `rotate()` calls. Real mechanical design demands the ability to say "on the top face of this box, draw a circle and extrude it upward" without manually computing world coordinates. This phase closes that gap.

## Pipeline Extension

```
Shape
  --> .faces()       --> FaceSelector  --> .top() / .byDirection() --> FaceRef
  --> .face(">Z")    --> FaceRef (shorthand)
  --> .edges()       --> EdgeSelector  --> .parallelTo() / .longest() --> edge list

FaceRef
  --> .workplane()   --> Workplane
  --> .sketch()      --> Sketch (shorthand: auto-creates workplane)
  --> .center()      --> gp_Pnt
  --> .normal()      --> gp_Dir

Workplane
  --> .sketch()      --> Sketch
  --> .offset(dist)  --> Workplane (shifted along normal)

Sketch
  --> .circle(r)     --> Sketch (adds wire)
  --> .rect(w,h)     --> Sketch (adds wire)
  --> .extrude(dist) --> Shape (prism fused to parent)
  --> .cutBlind(d)   --> Shape (prism cut from parent)
  --> .cutThrough()  --> Shape (through-all cut from parent)
```

---

## New Classes

### 1. FaceRef (`src/geometry/FaceRef.h` / `src/geometry/FaceRef.cpp`)

A handle to a single topological face on a parent shape. Provides geometric queries and serves as the entry point for workplane creation.

#### C++ Class Interface

```cpp
#pragma once

#include <memory>
#include <vector>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class FaceRef;
class Workplane;
class Sketch;
class Shape;

using FaceRefPtr = std::shared_ptr<FaceRef>;
using WorkplanePtr = std::shared_ptr<Workplane>;
using ShapePtr = std::shared_ptr<Shape>;

class FaceRef {
public:
    /// Construct from a specific face and its parent shape.
    /// The parent reference is required for auto fuse/cut operations
    /// triggered by Sketch termination methods.
    FaceRef(const TopoDS_Face& face, ShapePtr parentShape);

    // ---- Geometric queries ----

    /// Centroid of the face (area-weighted center of mass).
    gp_Pnt center() const;

    /// Outward-pointing unit normal at the face centroid.
    /// For planar faces this is constant; for curved faces it is
    /// evaluated at the parametric midpoint.
    gp_Dir normal() const;

    /// The underlying plane (throws GeometryError if !isPlanar()).
    gp_Pln plane() const;

    /// True if the underlying surface is GeomAbs_Plane.
    bool isPlanar() const;

    /// Surface area of the face.
    double area() const;

    // ---- Topology queries ----

    /// All edges bounding this face, in wire order.
    std::vector<TopoDS_Edge> edges() const;

    /// Number of edges bounding this face.
    size_t edgeCount() const;

    // ---- Workplane / Sketch creation ----

    /// Create a Workplane anchored at this face's centroid,
    /// with axes derived from the surface parameterization.
    /// Throws GeometryError if the face is not planar.
    WorkplanePtr workplane() const;

    /// Shorthand: create a Workplane and immediately open a Sketch on it.
    /// Equivalent to workplane()->sketch().
    std::shared_ptr<Sketch> sketch() const;

    // ---- Accessors ----

    const TopoDS_Face& topoFace() const { return face_; }
    ShapePtr parentShape() const { return parent_; }

private:
    TopoDS_Face face_;
    ShapePtr parent_;
};

} // namespace opendcad
```

#### OCCT Implementation Details

| Method | OCCT approach |
|--------|---------------|
| `center()` | `BRepGProp::SurfaceProperties(face_, props)` then `props.CentreOfMass()`. This yields the area-weighted centroid, which is correct for both planar and curved faces. |
| `normal()` | `BRepAdaptor_Surface adaptor(face_)`. Evaluate at parametric midpoint: `adaptor.D1(uMid, vMid, pnt, dU, dV)`, then `gp_Dir normal = dU.Crossed(dV)`. If the face orientation is `TopAbs_REVERSED`, negate the normal. The midpoint `(uMid, vMid)` is computed as `(uMin+uMax)/2, (vMin+vMax)/2` from `BRepTools::UVBounds(face_, uMin, uMax, vMin, vMax)`. |
| `plane()` | After checking `isPlanar()`, use `BRepAdaptor_Surface(face_).Plane()`. Adjust orientation if `face_.Orientation() == TopAbs_REVERSED`. |
| `isPlanar()` | `BRepAdaptor_Surface(face_).GetType() == GeomAbs_Plane`. |
| `area()` | `BRepGProp::SurfaceProperties(face_, props)` then `props.Mass()`. |
| `edges()` | `TopExp_Explorer(face_, TopAbs_EDGE)` iterating all edges. Collect into `std::vector<TopoDS_Edge>`. |
| `workplane()` | Calls `plane()` to get the `gp_Pln`, then constructs a `Workplane` from the plane's coordinate system (`gp_Pln::Position()`) and this `FaceRef`'s parent shape. |

#### Integration with the Evaluator / Value System

The `Value` class (from Phase 0) gains a new `ValueType::FACE_REF` variant:

```cpp
// In Value.h
enum class ValueType { NUMBER, STRING, BOOL, VECTOR, SHAPE, FACE_REF, WORKPLANE, SKETCH, NIL };

// New factory and accessor
static ValuePtr makeFaceRef(FaceRefPtr v);
FaceRefPtr asFaceRef() const;
```

When the Evaluator encounters a method chain like `base.face(">Z")`:
1. `base` evaluates to `ValueType::SHAPE`.
2. The method `face` is dispatched on the SHAPE type. The `ShapeRegistry` entry for `face` calls `Shape::face(selector)` which returns a `FaceRefPtr`.
3. The result is wrapped as `Value::makeFaceRef(faceRef)`.
4. Subsequent method calls (`.sketch()`, `.workplane()`) are dispatched on `ValueType::FACE_REF`.

#### DSL Usage Examples

```
// Select the top face of a box
let top = box(40, 30, 10).face(">Z");

// Query face properties (future: expose as DSL values)
// For now, face() is primarily used as a stepping stone to sketch()

// Direct sketch on a face
let result = box(40, 30, 10).face(">Z").sketch().circle(8).extrude(15);

// Using workplane explicitly
let wp = box(40, 30, 10).face(">Z").workplane();
let result = wp.sketch().rect(20, 10).cutBlind(5);
```

---

### 2. FaceSelector (`src/geometry/FaceSelector.h` / `src/geometry/FaceSelector.cpp`)

A query engine that enumerates, filters, and ranks faces on a shape. Returned by `Shape::faces()`. All query methods return either a `FaceRefPtr` (for single-result selectors) or a new `FaceSelector` (for filtering operations that narrow the set).

#### C++ Class Interface

```cpp
#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Face.hxx>
#include <GeomAbs_SurfaceType.hxx>

namespace opendcad {

class FaceSelector;
class FaceRef;
class Shape;

using FaceSelectorPtr = std::shared_ptr<FaceSelector>;
using FaceRefPtr = std::shared_ptr<FaceRef>;
using ShapePtr = std::shared_ptr<Shape>;

class FaceSelector {
public:
    explicit FaceSelector(ShapePtr shape);

    // ---- Direction-based selectors (return single face) ----

    /// Face whose outward normal most closely aligns with +Z.
    /// DSL: .top() or .byDirection(">Z")
    FaceRefPtr top() const;

    /// Face whose outward normal most closely aligns with -Z.
    /// DSL: .bottom() or .byDirection("<Z")
    FaceRefPtr bottom() const;

    /// Face whose outward normal most closely aligns with +Y.
    FaceRefPtr front() const;

    /// Face whose outward normal most closely aligns with -Y.
    FaceRefPtr back() const;

    /// Face whose outward normal most closely aligns with -X.
    FaceRefPtr left() const;

    /// Face whose outward normal most closely aligns with +X.
    FaceRefPtr right() const;

    /// Face whose outward normal has the highest dot product with `dir`.
    /// This is the general form used by all directional selectors.
    FaceRefPtr byDirection(const gp_Dir& dir) const;

    // ---- Position-based selectors (return single face) ----

    /// Face whose centroid is closest to `point`.
    FaceRefPtr nearestTo(const gp_Pnt& point) const;

    /// Face whose centroid is farthest from `point`.
    FaceRefPtr farthestFrom(const gp_Pnt& point) const;

    /// Face at index `n` in enumeration order (0-based).
    /// Throws if out of range. Enumeration order is deterministic
    /// (TopExp_Explorer iteration order).
    FaceRefPtr byIndex(size_t n) const;

    // ---- Type-based filters (return new FaceSelector with reduced set) ----

    /// Only planar faces (GeomAbs_Plane).
    FaceSelectorPtr planar() const;

    /// Only cylindrical faces (GeomAbs_Cylinder).
    FaceSelectorPtr cylindrical() const;

    /// Only faces of a specific OCCT surface type.
    FaceSelectorPtr byType(GeomAbs_SurfaceType type) const;

    // ---- Size-based selectors (return single face) ----

    /// Face with the largest surface area.
    FaceRefPtr largest() const;

    /// Face with the smallest surface area.
    FaceRefPtr smallest() const;

    // ---- Geometric relationship filters (return new FaceSelector) ----

    /// Faces whose normal is parallel to `dir` (dot product > 1 - tolerance).
    FaceSelectorPtr parallelTo(const gp_Dir& dir) const;

    /// Faces whose normal is perpendicular to `dir` (|dot product| < tolerance).
    FaceSelectorPtr perpendicularTo(const gp_Dir& dir) const;

    // ---- Inspection ----

    /// Number of faces currently in the selector's working set.
    size_t count() const;

    /// All faces in the working set.
    std::vector<FaceRefPtr> all() const;

private:
    /// Private constructor for filtered subsets.
    FaceSelector(ShapePtr shape, std::vector<TopoDS_Face> faces);

    /// Lazily enumerate faces on first query.
    void ensureFaces() const;

    /// Score all faces by a function, return the one with the highest score.
    FaceRefPtr bestBy(std::function<double(const TopoDS_Face&)> scorer) const;

    /// Filter faces by a predicate, return a new FaceSelector with the subset.
    FaceSelectorPtr filter(std::function<bool(const TopoDS_Face&)> pred) const;

    ShapePtr shape_;
    mutable std::vector<TopoDS_Face> faces_;
    mutable bool facesLoaded_ = false;

    static constexpr double kAngleTolerance = 1e-6;  // ~0.00006 degrees
};

} // namespace opendcad
```

#### OCCT Implementation Details

**Face enumeration (`ensureFaces()`):**
```cpp
void FaceSelector::ensureFaces() const {
    if (facesLoaded_) return;
    for (TopExp_Explorer ex(shape_->getShape(), TopAbs_FACE); ex.More(); ex.Next()) {
        faces_.push_back(TopoDS::Face(ex.Current()));
    }
    facesLoaded_ = true;
}
```

**Direction-based selection (`byDirection`):**
The algorithm computes the dot product between each face's outward normal and the target direction. The face with the highest dot product wins.

```cpp
FaceRefPtr FaceSelector::byDirection(const gp_Dir& dir) const {
    return bestBy([&dir](const TopoDS_Face& face) -> double {
        BRepAdaptor_Surface adaptor(face);
        double uMid, vMid;
        // compute parametric midpoint
        double u1, u2, v1, v2;
        BRepTools::UVBounds(face, u1, u2, v1, v2);
        uMid = (u1 + u2) / 2.0;
        vMid = (v1 + v2) / 2.0;

        gp_Pnt pnt;
        gp_Vec dU, dV;
        adaptor.D1(uMid, vMid, pnt, dU, dV);
        gp_Dir normal(dU.Crossed(dV));

        // Respect face orientation
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();

        return normal.Dot(dir);  // higher = better alignment
    });
}
```

**Directional convenience methods map to unit vectors:**

| Method | Direction vector |
|--------|-----------------|
| `top()` | `gp_Dir(0, 0, 1)` i.e. `+Z` |
| `bottom()` | `gp_Dir(0, 0, -1)` i.e. `-Z` |
| `front()` | `gp_Dir(0, 1, 0)` i.e. `+Y` |
| `back()` | `gp_Dir(0, -1, 0)` i.e. `-Y` |
| `right()` | `gp_Dir(1, 0, 0)` i.e. `+X` |
| `left()` | `gp_Dir(-1, 0, 0)` i.e. `-X` |

**Position-based selection (`nearestTo`):**
```cpp
FaceRefPtr FaceSelector::nearestTo(const gp_Pnt& point) const {
    // Negate distance so bestBy (which maximizes) finds the minimum
    return bestBy([&point](const TopoDS_Face& face) -> double {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        gp_Pnt centroid = props.CentreOfMass();
        return -centroid.Distance(point);
    });
}
```

**Type-based filtering:**
```cpp
FaceSelectorPtr FaceSelector::byType(GeomAbs_SurfaceType type) const {
    return filter([type](const TopoDS_Face& face) -> bool {
        BRepAdaptor_Surface adaptor(face);
        return adaptor.GetType() == type;
    });
}

FaceSelectorPtr FaceSelector::planar() const {
    return byType(GeomAbs_Plane);
}

FaceSelectorPtr FaceSelector::cylindrical() const {
    return byType(GeomAbs_Cylinder);
}
```

**Area-based selection:**
```cpp
FaceRefPtr FaceSelector::largest() const {
    return bestBy([](const TopoDS_Face& face) -> double {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        return props.Mass();  // area
    });
}

FaceRefPtr FaceSelector::smallest() const {
    return bestBy([](const TopoDS_Face& face) -> double {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        return -props.Mass();  // negate so bestBy finds minimum
    });
}
```

#### Integration with the Evaluator / Value System

The `Value` class gains a `ValueType::FACE_SELECTOR` variant:

```cpp
static ValuePtr makeFaceSelector(FaceSelectorPtr v);
FaceSelectorPtr asFaceSelector() const;
```

Method dispatch in the Evaluator when the current value is `FACE_SELECTOR`:

| DSL method | FaceSelector C++ method | Returns |
|------------|------------------------|---------|
| `.top()` | `selector->top()` | `FACE_REF` |
| `.bottom()` | `selector->bottom()` | `FACE_REF` |
| `.front()` | `selector->front()` | `FACE_REF` |
| `.back()` | `selector->back()` | `FACE_REF` |
| `.left()` | `selector->left()` | `FACE_REF` |
| `.right()` | `selector->right()` | `FACE_REF` |
| `.largest()` | `selector->largest()` | `FACE_REF` |
| `.smallest()` | `selector->smallest()` | `FACE_REF` |
| `.planar()` | `selector->planar()` | `FACE_SELECTOR` |
| `.cylindrical()` | `selector->cylindrical()` | `FACE_SELECTOR` |
| `.byIndex(n)` | `selector->byIndex(n)` | `FACE_REF` |
| `.count()` | `selector->count()` | `NUMBER` |

#### DSL Usage Examples

```
// Explicit face selector usage
let base = box(40, 30, 10);
let topFace = base.faces().top();

// Chain through selector into sketch
let result = box(40, 30, 10).faces().top().sketch().circle(8).extrude(15);

// Filter then select
let biggestFlat = box(40, 30, 10).faces().planar().largest();

// Count faces
let n = box(40, 30, 10).faces().count();  // 6 for a box
```

---

### 3. EdgeSelector (`src/geometry/EdgeSelector.h` / `src/geometry/EdgeSelector.cpp`)

A query engine over edges of a shape. Supports filtering and selection for targeted fillet/chamfer operations. Returned by `Shape::edges()` or `FaceRef::edges()`.

#### C++ Class Interface

```cpp
#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class EdgeSelector;
class FaceRef;
class Shape;

using EdgeSelectorPtr = std::shared_ptr<EdgeSelector>;
using FaceRefPtr = std::shared_ptr<FaceRef>;
using ShapePtr = std::shared_ptr<Shape>;

class EdgeSelector {
public:
    /// Construct from all edges on a shape.
    explicit EdgeSelector(ShapePtr shape);

    /// Construct from a pre-filtered set of edges (e.g., edges of a specific face).
    EdgeSelector(ShapePtr shape, std::vector<TopoDS_Edge> edges);

    // ---- Direction-based selectors ----

    /// Edges whose tangent direction is parallel to `dir`.
    /// Only meaningful for linear edges; curved edges are excluded.
    EdgeSelectorPtr parallelTo(const gp_Dir& dir) const;

    /// Edges whose tangent direction is perpendicular to `dir`.
    EdgeSelectorPtr perpendicularTo(const gp_Dir& dir) const;

    /// Shorthand: edges parallel to the Z axis (vertical edges).
    EdgeSelectorPtr vertical() const;

    /// Shorthand: edges perpendicular to the Z axis (horizontal edges).
    EdgeSelectorPtr horizontal() const;

    // ---- Topology-based selectors ----

    /// Edges belonging to a specific face.
    EdgeSelectorPtr ofFace(FaceRefPtr faceRef) const;

    /// Edges shared between two faces (i.e., the intersection edge).
    EdgeSelectorPtr betweenFaces(FaceRefPtr f1, FaceRefPtr f2) const;

    // ---- Position-based selectors ----

    /// Edge whose midpoint is closest to `point`.
    TopoDS_Edge nearestTo(const gp_Pnt& point) const;

    // ---- Size-based selectors ----

    /// The longest edge by arc length.
    TopoDS_Edge longest() const;

    /// The shortest edge by arc length.
    TopoDS_Edge shortest() const;

    // ---- Bulk access ----

    /// All edges in the current working set.
    const std::vector<TopoDS_Edge>& all() const;

    /// Number of edges in the current working set.
    size_t count() const;

    // ---- Operations using selected edges ----

    /// Fillet all edges in the working set with the given radius.
    /// Returns a new Shape with the fillets applied.
    ShapePtr fillet(double radius) const;

    /// Chamfer all edges in the working set with the given distance.
    /// Returns a new Shape with the chamfers applied.
    ShapePtr chamfer(double distance) const;

private:
    void ensureEdges() const;

    EdgeSelectorPtr filter(std::function<bool(const TopoDS_Edge&)> pred) const;

    ShapePtr shape_;
    mutable std::vector<TopoDS_Edge> edges_;
    mutable bool edgesLoaded_ = false;
};

} // namespace opendcad
```

#### OCCT Implementation Details

**Edge enumeration (`ensureEdges()`):**
```cpp
void EdgeSelector::ensureEdges() const {
    if (edgesLoaded_) return;
    for (TopExp_Explorer ex(shape_->getShape(), TopAbs_EDGE); ex.More(); ex.Next()) {
        edges_.push_back(TopoDS::Edge(ex.Current()));
    }
    edgesLoaded_ = true;
}
```

**Direction-based filtering (`parallelTo`):**
Uses `BRepAdaptor_Curve` to get the tangent direction of linear edges, then checks the dot product:
```cpp
EdgeSelectorPtr EdgeSelector::parallelTo(const gp_Dir& dir) const {
    return filter([&dir](const TopoDS_Edge& edge) -> bool {
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line) return false;
        gp_Dir tangent = curve.Line().Direction();
        double dot = std::abs(tangent.Dot(dir));
        return dot > 1.0 - 1e-6;  // parallel within tolerance
    });
}
```

**Edge between two faces (`betweenFaces`):**
Uses `TopExp::MapShapesAndAncestors` to build an edge-to-face ancestry map. For each edge in the current set, checks whether both `f1` and `f2` appear as ancestor faces:
```cpp
EdgeSelectorPtr EdgeSelector::betweenFaces(FaceRefPtr f1, FaceRefPtr f2) const {
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(shape_->getShape(), TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    return filter([&](const TopoDS_Edge& edge) -> bool {
        if (!edgeFaceMap.Contains(edge)) return false;
        const TopTools_ListOfShape& ancestors = edgeFaceMap.FindFromKey(edge);
        bool hasF1 = false, hasF2 = false;
        for (auto it = ancestors.begin(); it != ancestors.end(); ++it) {
            if (it->IsSame(f1->topoFace())) hasF1 = true;
            if (it->IsSame(f2->topoFace())) hasF2 = true;
        }
        return hasF1 && hasF2;
    });
}
```

**Edge length measurement (`longest`, `shortest`):**
Uses `GCPnts_AbscissaPoint::Length()` from the `BRepAdaptor_Curve` to compute arc length:
```cpp
TopoDS_Edge EdgeSelector::longest() const {
    ensureEdges();
    if (edges_.empty()) throw GeometryError("no edges in selector");

    double maxLen = -1.0;
    TopoDS_Edge best;
    for (const auto& edge : edges_) {
        BRepAdaptor_Curve curve(edge);
        double len = GCPnts_AbscissaPoint::Length(curve);
        if (len > maxLen) {
            maxLen = len;
            best = edge;
        }
    }
    return best;
}
```

**Selective fillet (`fillet`):**
```cpp
ShapePtr EdgeSelector::fillet(double radius) const {
    ensureEdges();
    BRepFilletAPI_MakeFillet mk(shape_->getShape());
    for (const auto& edge : edges_) {
        mk.Add(radius, edge);
    }
    mk.Build();
    if (!mk.IsDone()) throw GeometryError("fillet failed on selected edges");
    return std::make_shared<Shape>(mk.Shape());
}
```

#### Integration with the Evaluator / Value System

The `Value` class gains a `ValueType::EDGE_SELECTOR` variant:

```cpp
static ValuePtr makeEdgeSelector(EdgeSelectorPtr v);
EdgeSelectorPtr asEdgeSelector() const;
```

Method dispatch on `EDGE_SELECTOR` values:

| DSL method | EdgeSelector C++ method | Returns |
|------------|------------------------|---------|
| `.parallelTo(dir)` | `selector->parallelTo(dir)` | `EDGE_SELECTOR` |
| `.vertical()` | `selector->vertical()` | `EDGE_SELECTOR` |
| `.horizontal()` | `selector->horizontal()` | `EDGE_SELECTOR` |
| `.fillet(r)` | `selector->fillet(r)` | `SHAPE` |
| `.chamfer(d)` | `selector->chamfer(d)` | `SHAPE` |
| `.count()` | `selector->count()` | `NUMBER` |

#### DSL Usage Examples

```
// Fillet only vertical edges of a box
let result = box(40, 30, 10).edges().vertical().fillet(2);

// Fillet edges between two specific faces
let base = box(40, 30, 10);
let topEdges = base.edges().ofFace(base.face(">Z"));
let rounded = topEdges.fillet(1.5);

// Chamfer the longest edge
let base = box(40, 30, 10);
let e = base.edges().longest();
// (single-edge chamfer via future API)
```

---

### 4. Workplane (`src/geometry/Workplane.h` / `src/geometry/Workplane.cpp`)

A local coordinate system anchored on a face. Provides coordinate transformation between local 2D space and world 3D space, and serves as the context for sketch operations and direct extrude/cut.

#### C++ Class Interface

```cpp
#pragma once

#include <memory>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Face.hxx>

namespace opendcad {

class Workplane;
class Sketch;
class Shape;
class FaceRef;

using WorkplanePtr = std::shared_ptr<Workplane>;
using ShapePtr = std::shared_ptr<Shape>;
using FaceRefPtr = std::shared_ptr<FaceRef>;

class Workplane {
public:
    /// Construct from a face reference. The workplane origin is the face
    /// centroid, the normal is the face outward normal, and the X/Y
    /// directions are derived from the surface parameterization.
    explicit Workplane(FaceRefPtr faceRef);

    /// Construct from explicit coordinate system components.
    /// Used internally for offset() and manual workplane creation.
    Workplane(const gp_Pnt& origin, const gp_Dir& normal,
              const gp_Dir& xDir, ShapePtr parentShape);

    // ---- Coordinate system queries ----

    /// Origin point in world coordinates.
    gp_Pnt origin() const { return origin_; }

    /// Outward normal (Z direction of the local frame).
    gp_Dir normal() const { return normal_; }

    /// X direction of the local frame (lies in the face plane).
    gp_Dir xDirection() const { return xDir_; }

    /// Y direction of the local frame (computed as normal x xDir).
    gp_Dir yDirection() const;

    /// The underlying gp_Ax2 coordinate system.
    gp_Ax2 axes() const;

    /// The underlying gp_Pln plane.
    gp_Pln plane() const;

    // ---- Coordinate transformations ----

    /// Convert local 2D coordinates (x, y) to a world 3D point.
    /// The local point (0, 0) maps to the workplane origin.
    /// Local X maps along xDirection(), local Y along yDirection().
    gp_Pnt toWorld(double localX, double localY) const;

    /// Convert a world 3D point to local 2D coordinates (x, y).
    /// The Z component (distance from plane) is discarded.
    /// Returns a pair (localX, localY).
    std::pair<double, double> toLocal(const gp_Pnt& worldPoint) const;

    // ---- Derived workplanes ----

    /// Create a new workplane offset along the normal by `distance`.
    /// Positive distance moves in the normal direction;
    /// negative moves against it.
    WorkplanePtr offset(double distance) const;

    // ---- Direct geometry operations ----

    /// Extrude a face-shaped profile along the normal by `distance`.
    /// The profile is the entire face that this workplane was created from.
    /// Fuses the result with the parent shape.
    /// For custom profiles, use sketch() instead.
    ShapePtr extrude(double distance) const;

    /// Cut into the parent shape along the negative normal by `depth`.
    ShapePtr cutBlind(double depth) const;

    /// Cut through the entire parent shape along the negative normal.
    ShapePtr cutThrough() const;

    // ---- Sketch creation ----

    /// Open a 2D sketch context on this workplane.
    std::shared_ptr<Sketch> sketch() const;

    // ---- Accessors ----

    ShapePtr parentShape() const { return parent_; }

private:
    gp_Pnt origin_;
    gp_Dir normal_;
    gp_Dir xDir_;
    ShapePtr parent_;
    FaceRefPtr faceRef_;  // may be null for manually constructed workplanes
};

} // namespace opendcad
```

#### OCCT Implementation Details

**Constructor from FaceRef:**
```cpp
Workplane::Workplane(FaceRefPtr faceRef)
    : parent_(faceRef->parentShape())
    , faceRef_(faceRef)
{
    if (!faceRef->isPlanar()) {
        throw GeometryError("workplane requires a planar face");
    }

    origin_ = faceRef->center();
    normal_ = faceRef->normal();

    // Derive X direction from surface parameterization
    BRepAdaptor_Surface adaptor(faceRef->topoFace());
    double u1, u2, v1, v2;
    BRepTools::UVBounds(faceRef->topoFace(), u1, u2, v1, v2);
    double uMid = (u1 + u2) / 2.0;
    double vMid = (v1 + v2) / 2.0;

    gp_Pnt pnt;
    gp_Vec dU, dV;
    adaptor.D1(uMid, vMid, pnt, dU, dV);

    // dU is the "natural" X direction of the surface
    xDir_ = gp_Dir(dU);

    // If face is reversed, the normal was negated; we keep X as-is
    // so the local frame remains right-handed.
}
```

**Coordinate transformation (`toWorld`):**
```cpp
gp_Pnt Workplane::toWorld(double localX, double localY) const {
    gp_Vec xVec(xDir_);
    gp_Vec yVec(yDirection());
    gp_Pnt result = origin_;
    result.Translate(xVec * localX);
    result.Translate(yVec * localY);
    return result;
}
```

**Offset workplane:**
```cpp
WorkplanePtr Workplane::offset(double distance) const {
    gp_Pnt newOrigin = origin_;
    newOrigin.Translate(gp_Vec(normal_) * distance);
    return std::make_shared<Workplane>(newOrigin, normal_, xDir_, parent_);
}
```

**Extrude from workplane (`extrude`):**
Uses `BRepPrimAPI_MakePrism` with the face as the profile and the normal direction scaled by distance as the prism vector:
```cpp
ShapePtr Workplane::extrude(double distance) const {
    if (!faceRef_) throw GeometryError("extrude requires a face-backed workplane");

    gp_Vec prismVec(normal_);
    prismVec.Scale(distance);

    BRepPrimAPI_MakePrism prism(faceRef_->topoFace(), prismVec);
    prism.Build();
    if (!prism.IsDone()) throw GeometryError("workplane extrude failed");

    ShapePtr extruded = std::make_shared<Shape>(prism.Shape());
    return parent_->fuse(extruded);
}
```

**Cut through (`cutThrough`):**
Computes a bounding box of the parent shape to determine a safe through-all distance:
```cpp
ShapePtr Workplane::cutThrough() const {
    if (!faceRef_) throw GeometryError("cutThrough requires a face-backed workplane");

    // Compute bounding box diagonal as a safe through-all distance
    Bnd_Box bbox;
    BRepBndLib::Add(parent_->getShape(), bbox);
    double xMin, yMin, zMin, xMax, yMax, zMax;
    bbox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    double diagonal = gp_Pnt(xMin, yMin, zMin).Distance(gp_Pnt(xMax, yMax, zMax));
    double throughDistance = diagonal * 2.0;  // generous safety margin

    return cutBlind(throughDistance);
}
```

#### Integration with the Evaluator / Value System

The `Value` class gains a `ValueType::WORKPLANE` variant:

```cpp
static ValuePtr makeWorkplane(WorkplanePtr v);
WorkplanePtr asWorkplane() const;
```

Method dispatch on `WORKPLANE` values:

| DSL method | Workplane C++ method | Returns |
|------------|---------------------|---------|
| `.sketch()` | `wp->sketch()` | `SKETCH` |
| `.offset(dist)` | `wp->offset(dist)` | `WORKPLANE` |
| `.extrude(dist)` | `wp->extrude(dist)` | `SHAPE` |
| `.cutBlind(depth)` | `wp->cutBlind(depth)` | `SHAPE` |
| `.cutThrough()` | `wp->cutThrough()` | `SHAPE` |

#### DSL Usage Examples

```
// Offset workplane
let base = box(40, 30, 10);
let raised = base.face(">Z").workplane().offset(5);
let result = raised.sketch().circle(8).extrude(10);

// Direct extrude from workplane (extrudes the entire face profile)
let base = box(40, 30, 10);
let taller = base.face(">Z").workplane().extrude(20);
```

---

### 5. Sketch (`src/geometry/Sketch.h` / `src/geometry/Sketch.cpp`)

A 2D drawing context bound to a Workplane. Accumulates 2D wire profiles (circles, rectangles, polygons, freeform paths) in the workplane's local coordinate system. Terminated by an extrude or cut operation that produces a 3D shape, which is automatically fused to or cut from the parent shape.

#### C++ Class Interface

```cpp
#pragma once

#include <memory>
#include <vector>
#include <gp_Pnt2d.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>

namespace opendcad {

class Sketch;
class Workplane;
class Shape;

using SketchPtr = std::shared_ptr<Sketch>;
using WorkplanePtr = std::shared_ptr<Workplane>;
using ShapePtr = std::shared_ptr<Shape>;

class Sketch {
public:
    explicit Sketch(WorkplanePtr workplane);

    // ---- Closed-shape primitives ----
    // All coordinates are in the workplane's local frame.
    // Center defaults to (0, 0) which is the workplane origin.

    /// Add a circle centered at (cx, cy) with radius r.
    SketchPtr circle(double r, double cx = 0.0, double cy = 0.0);

    /// Add an axis-aligned rectangle centered at (cx, cy).
    SketchPtr rect(double width, double height, double cx = 0.0, double cy = 0.0);

    /// Add a slot (stadium shape): a rectangle of length l and width w
    /// with semicircular end caps. Centered at (cx, cy), oriented along
    /// the local X axis.
    SketchPtr slot(double length, double width, double cx = 0.0, double cy = 0.0);

    /// Add a closed polygon from a list of 2D points.
    /// Points are in the workplane's local frame.
    /// The polygon is automatically closed (last point connects to first).
    SketchPtr polygon(const std::vector<std::pair<double, double>>& points);

    // ---- Freeform wire building ----
    // These methods build a single open/closed wire incrementally.
    // Call close() to finalize the wire.

    /// Move the pen to (x, y) without drawing. Starts a new wire.
    SketchPtr moveTo(double x, double y);

    /// Draw a line from the current pen position to (x, y).
    SketchPtr lineTo(double x, double y);

    /// Draw a circular arc from the current pen position to (x, y),
    /// passing through the tangent point (tx, ty).
    /// The tangent point defines the arc's curvature direction.
    SketchPtr arcTo(double x, double y, double tx, double ty);

    /// Close the current wire by connecting the last point to the first.
    SketchPtr close();

    // ---- Termination operations ----
    // These finalize the sketch, create 3D geometry from the accumulated
    // wires, and merge the result with the parent shape.

    /// Extrude all sketch profiles along the workplane normal by `distance`.
    /// Positive distance extrudes in the normal direction (outward).
    /// The result is fused with the parent shape.
    ShapePtr extrude(double distance);

    /// Cut into the parent shape along the negative normal by `depth`.
    /// Positive depth cuts in the direction opposite to the normal.
    /// The extruded profiles are subtracted from the parent shape.
    ShapePtr cutBlind(double depth);

    /// Cut through the entire parent shape.
    /// Uses the bounding box diagonal as a safe through-all distance.
    ShapePtr cutThrough();

    /// Revolve the sketch profiles around the workplane X axis by `angle`
    /// degrees. The result is fused with the parent shape.
    ShapePtr revolve(double angleDegrees = 360.0);

    // ---- Inspection ----

    /// Number of closed wires accumulated so far.
    size_t wireCount() const { return wires_.size(); }

private:
    /// Build a TopoDS_Face on the workplane from a TopoDS_Wire.
    TopoDS_Face makePlanarFace(const TopoDS_Wire& wire) const;

    /// Extrude a face along a direction vector, return the solid.
    TopoDS_Shape extrudeProfile(const TopoDS_Face& profile, const gp_Vec& vec) const;

    /// Convert local 2D coordinates to a world 3D point (delegates to workplane).
    gp_Pnt toWorld(double x, double y) const;

    WorkplanePtr workplane_;
    std::vector<TopoDS_Wire> wires_;

    // Freeform wire building state
    bool wireInProgress_ = false;
    gp_Pnt2d wireStart_;   // first point of current wire (for close())
    gp_Pnt2d penPos_;      // current pen position
    std::vector<TopoDS_Edge> pendingEdges_;  // edges for current wire
};

} // namespace opendcad
```

#### OCCT Implementation Details

**Circle (`circle`):**
```cpp
SketchPtr Sketch::circle(double r, double cx, double cy) {
    gp_Pnt center = toWorld(cx, cy);
    gp_Ax2 ax(center, workplane_->normal(), workplane_->xDirection());
    gp_Circ circ(ax, r);

    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circ).Edge();
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge).Wire();
    wires_.push_back(wire);

    return std::shared_ptr<Sketch>(this, [](Sketch*){});
    // Note: actual implementation returns shared_from_this() via
    // std::enable_shared_from_this
}
```

The actual implementation should use `std::enable_shared_from_this<Sketch>` so that `circle()`, `rect()`, etc. can return `shared_from_this()` for fluent chaining.

**Rectangle (`rect`):**
```cpp
SketchPtr Sketch::rect(double width, double height, double cx, double cy) {
    double hw = width / 2.0, hh = height / 2.0;

    gp_Pnt p1 = toWorld(cx - hw, cy - hh);
    gp_Pnt p2 = toWorld(cx + hw, cy - hh);
    gp_Pnt p3 = toWorld(cx + hw, cy + hh);
    gp_Pnt p4 = toWorld(cx - hw, cy + hh);

    BRepBuilderAPI_MakeWire mkWire;
    mkWire.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(p2, p3).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(p3, p4).Edge());
    mkWire.Add(BRepBuilderAPI_MakeEdge(p4, p1).Edge());

    wires_.push_back(mkWire.Wire());
    return shared_from_this();
}
```

**Slot (stadium shape):**
A slot is a rectangle with semicircular end caps. The implementation builds two lines and two 180-degree arcs:
```cpp
SketchPtr Sketch::slot(double length, double width, double cx, double cy) {
    double halfLen = length / 2.0;
    double halfW = width / 2.0;
    double r = halfW;  // semicircle radius equals half-width

    // The straight sections run along local X, the semicircles cap each end.
    // Points in local coordinates:
    //   Top-left of straight: (cx - halfLen + r, cy + halfW)
    //   Top-right of straight: (cx + halfLen - r, cy + halfW)
    //   etc.

    // Build as wire with 2 lines + 2 arcs using BRepBuilderAPI_MakeEdge
    // with Handle(Geom_Circle) for the arcs.
    // ... (detailed implementation)

    wires_.push_back(wire);
    return shared_from_this();
}
```

**Extrude (`extrude`):**
```cpp
ShapePtr Sketch::extrude(double distance) {
    if (wires_.empty()) throw GeometryError("sketch has no profiles to extrude");

    gp_Vec prismVec(workplane_->normal());
    prismVec.Scale(distance);

    ShapePtr result = workplane_->parentShape();

    for (const auto& wire : wires_) {
        TopoDS_Face profile = makePlanarFace(wire);
        BRepPrimAPI_MakePrism prism(profile, prismVec);
        prism.Build();
        if (!prism.IsDone()) throw GeometryError("sketch extrude failed");

        ShapePtr extruded = std::make_shared<Shape>(prism.Shape());
        result = result->fuse(extruded);
    }

    return result;
}
```

**Cut blind (`cutBlind`):**
```cpp
ShapePtr Sketch::cutBlind(double depth) {
    if (wires_.empty()) throw GeometryError("sketch has no profiles to cut");

    // Cut in the direction opposite to the normal
    gp_Vec cutVec(workplane_->normal());
    cutVec.Scale(-depth);

    ShapePtr result = workplane_->parentShape();

    for (const auto& wire : wires_) {
        TopoDS_Face profile = makePlanarFace(wire);
        BRepPrimAPI_MakePrism prism(profile, cutVec);
        prism.Build();
        if (!prism.IsDone()) throw GeometryError("sketch cutBlind failed");

        ShapePtr cutter = std::make_shared<Shape>(prism.Shape());
        result = result->cut(cutter);
    }

    return result;
}
```

**Make planar face from wire:**
```cpp
TopoDS_Face Sketch::makePlanarFace(const TopoDS_Wire& wire) const {
    BRepBuilderAPI_MakeFace mkFace(workplane_->plane(), wire);
    mkFace.Build();
    if (!mkFace.IsDone()) throw GeometryError("failed to create face from sketch wire");
    return mkFace.Face();
}
```

**Revolve (`revolve`):**
```cpp
ShapePtr Sketch::revolve(double angleDegrees) {
    if (wires_.empty()) throw GeometryError("sketch has no profiles to revolve");

    double angleRad = angleDegrees * M_PI / 180.0;
    gp_Ax1 revolveAxis(workplane_->origin(), workplane_->xDirection());

    ShapePtr result = workplane_->parentShape();

    for (const auto& wire : wires_) {
        TopoDS_Face profile = makePlanarFace(wire);
        BRepPrimAPI_MakeRevol revol(profile, revolveAxis, angleRad);
        revol.Build();
        if (!revol.IsDone()) throw GeometryError("sketch revolve failed");

        ShapePtr revolved = std::make_shared<Shape>(revol.Shape());
        result = result->fuse(revolved);
    }

    return result;
}
```

#### Integration with the Evaluator / Value System

The `Value` class gains a `ValueType::SKETCH` variant:

```cpp
static ValuePtr makeSketch(SketchPtr v);
SketchPtr asSketch() const;
```

Method dispatch on `SKETCH` values:

| DSL method | Sketch C++ method | Returns |
|------------|------------------|---------|
| `.circle(r)` | `sketch->circle(r)` | `SKETCH` |
| `.circle(r, cx, cy)` | `sketch->circle(r, cx, cy)` | `SKETCH` |
| `.rect(w, h)` | `sketch->rect(w, h)` | `SKETCH` |
| `.rect(w, h, cx, cy)` | `sketch->rect(w, h, cx, cy)` | `SKETCH` |
| `.slot(l, w)` | `sketch->slot(l, w)` | `SKETCH` |
| `.polygon(points)` | `sketch->polygon(points)` | `SKETCH` |
| `.moveTo(x, y)` | `sketch->moveTo(x, y)` | `SKETCH` |
| `.lineTo(x, y)` | `sketch->lineTo(x, y)` | `SKETCH` |
| `.arcTo(x, y, tx, ty)` | `sketch->arcTo(x, y, tx, ty)` | `SKETCH` |
| `.close()` | `sketch->close()` | `SKETCH` |
| `.extrude(dist)` | `sketch->extrude(dist)` | `SHAPE` |
| `.cutBlind(depth)` | `sketch->cutBlind(depth)` | `SHAPE` |
| `.cutThrough()` | `sketch->cutThrough()` | `SHAPE` |
| `.revolve(angle)` | `sketch->revolve(angle)` | `SHAPE` |

#### DSL Usage Examples

```
// Simple boss on top of a box
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .circle(8)
    .extrude(15);

// Hole through a box
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .circle(5)
    .cutThrough();

// Rectangular pocket
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .rect(20, 10)
    .cutBlind(5);

// Multiple profiles in one sketch
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .circle(3, -10, 0)
    .circle(3, 10, 0)
    .cutThrough();

// Slot on a face
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .slot(20, 6)
    .cutBlind(3);

// Freeform wire
let result = box(40, 30, 10)
    .face(">Z")
    .sketch()
    .moveTo(-10, -5)
    .lineTo(10, -5)
    .lineTo(10, 5)
    .lineTo(-10, 5)
    .close()
    .extrude(8);

// Boss on top of boss (chained face selection)
let step1 = box(40, 30, 10)
    .face(">Z").sketch().circle(12).extrude(5);
let step2 = step1
    .face(">Z").sketch().circle(8).extrude(10);
let step3 = step2
    .face(">Z").sketch().circle(4).cutThrough();
```

---

## Shape Class Additions

The existing `Shape` class in `src/geometry/Shape.h` gains several new methods to serve as the entry point for face and edge operations.

### New Methods

```cpp
// In Shape.h, add to the public interface:

// ---- Face selection ----

/// Return a FaceSelector for querying this shape's faces.
FaceSelectorPtr faces() const;

/// Select a single face using a direction selector string.
/// Supported selectors:
///   ">Z" (top), "<Z" (bottom), ">Y" (front), "<Y" (back),
///   ">X" (right), "<X" (left)
/// Throws EvalError for unrecognized selector strings.
FaceRefPtr face(const std::string& selector) const;

/// Shorthand for face(">Z").
FaceRefPtr topFace() const;

/// Shorthand for face("<Z").
FaceRefPtr bottomFace() const;

// ---- Edge selection ----

/// Return an EdgeSelector for querying this shape's edges.
EdgeSelectorPtr edges() const;

/// Fillet specific edges with a given radius.
/// Unlike the existing fillet(amount) which fillets ALL edges,
/// this accepts a list of specific edges.
ShapePtr filletEdges(const std::vector<TopoDS_Edge>& edges, double radius) const;

/// Chamfer specific edges with a given distance.
ShapePtr chamferEdges(const std::vector<TopoDS_Edge>& edges, double distance) const;
```

### Implementation

```cpp
// In Shape.cpp

FaceSelectorPtr Shape::faces() const {
    return std::make_shared<FaceSelector>(
        std::const_pointer_cast<Shape>(shared_from_this())
    );
}

FaceRefPtr Shape::face(const std::string& selector) const {
    auto sel = faces();
    if (selector == ">Z") return sel->top();
    if (selector == "<Z") return sel->bottom();
    if (selector == ">Y") return sel->front();
    if (selector == "<Y") return sel->back();
    if (selector == ">X") return sel->right();
    if (selector == "<X") return sel->left();
    throw EvalError("unknown face selector: '" + selector + "'");
}

FaceRefPtr Shape::topFace() const { return face(">Z"); }
FaceRefPtr Shape::bottomFace() const { return face("<Z"); }

EdgeSelectorPtr Shape::edges() const {
    return std::make_shared<EdgeSelector>(
        std::const_pointer_cast<Shape>(shared_from_this())
    );
}

ShapePtr Shape::filletEdges(const std::vector<TopoDS_Edge>& edges, double radius) const {
    BRepFilletAPI_MakeFillet mk(mShape);
    for (const auto& edge : edges) {
        mk.Add(radius, edge);
    }
    mk.Build();
    if (!mk.IsDone()) throw GeometryError("fillet failed on selected edges");
    return std::make_shared<Shape>(mk.Shape());
}

ShapePtr Shape::chamferEdges(const std::vector<TopoDS_Edge>& edges, double distance) const {
    BRepFilletAPI_MakeChamfer mk(mShape);
    // Need edge-to-face mapping for chamfer
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaceMap;
    TopExp::MapShapesAndAncestors(mShape, TopAbs_EDGE, TopAbs_FACE, edgeFaceMap);

    for (const auto& edge : edges) {
        if (edgeFaceMap.Contains(edge)) {
            const TopoDS_Face& adjFace = TopoDS::Face(
                edgeFaceMap.FindFromKey(edge).First()
            );
            mk.Add(distance, edge, adjFace);
        }
    }
    mk.Build();
    if (!mk.IsDone()) throw GeometryError("chamfer failed on selected edges");
    return std::make_shared<Shape>(mk.Shape());
}
```

### Shape must become `enable_shared_from_this`

The `faces()` and `edges()` methods need a `shared_ptr` to `this` to pass as the parent shape reference. This requires `Shape` to inherit from `std::enable_shared_from_this<Shape>`:

```cpp
class Shape : public std::enable_shared_from_this<Shape> {
    // ... existing interface ...
};
```

This is a **breaking change** that affects how `Shape` objects are constructed. All `new Shape(...)` calls must be replaced with `std::make_shared<Shape>(...)`, and `Shape` objects must never be created on the stack. Existing code already uses `ShapePtr` (i.e., `std::shared_ptr<Shape>`) throughout, so this change is safe.

---

## DSL Syntax

### Face Selector String Syntax

The `face()` method on shapes accepts a string selector that identifies a face by its outward normal direction:

| Selector | Meaning | Normal direction |
|----------|---------|-----------------|
| `">Z"` | Top face | `+Z` |
| `"<Z"` | Bottom face | `-Z` |
| `">Y"` | Front face | `+Y` |
| `"<Y"` | Back face | `-Y` |
| `">X"` | Right face | `+X` |
| `"<X"` | Left face | `-X` |

The `>` prefix means "most aligned with the positive axis direction." The `<` prefix means "most aligned with the negative axis direction." This is the same convention used by CadQuery.

### Grammar Considerations

The existing grammar already supports the required syntax. The `face(">Z")` call parses as a `methodCall` with a `STRING` argument (assuming the Phase 0 `STRING` token addition is complete). No grammar changes are needed for Phase 2, only:

1. The `STRING` lexer rule from Phase 0 must be in place.
2. The `argList?` optional change from Phase 0 must be in place (for zero-arg methods like `.cutThrough()`, `.close()`).

### Complete DSL Example

```
// Start with a base plate
let base = box(60, 40, 5);

// Add a cylindrical boss on top
let withBoss = base
    .face(">Z")
    .sketch()
    .circle(10)
    .extrude(20);

// Drill a through-hole in the boss
let withHole = withBoss
    .face(">Z")
    .sketch()
    .circle(6)
    .cutThrough();

// Add mounting holes in the four corners of the base
let hole = withHole
    .face(">Z")       // this selects the highest Z face, which is the boss top
    .sketch()
    .circle(2, 20, 12)
    .circle(2, -20, 12)
    .circle(2, 20, -12)
    .circle(2, -20, -12)
    .cutThrough();

// Fillet only the vertical edges of the base plate
let final = hole
    .edges()
    .vertical()
    .fillet(3);

export final as mounting_plate;
```

---

## Type-Aware Method Dispatch in the Evaluator

### The Problem

In Phase 0, the Evaluator's `applyMethodChain` only handles `ValueType::SHAPE` values. Method chains in Phase 2 traverse multiple types:

```
box(40,30,10)     --> SHAPE
.face(">Z")       --> FACE_REF
.sketch()         --> SKETCH
.circle(8)        --> SKETCH
.extrude(15)      --> SHAPE
```

The Evaluator must track the current value type and dispatch to the correct method registry for each step.

### Solution: Multi-Type Method Registry

Extend `ShapeRegistry` (or create a new `MethodRegistry`) to dispatch methods based on `(ValueType, methodName)` pairs.

#### Registry Extension

```cpp
// In ShapeRegistry.h (or a new TypedMethodRegistry.h)

// A method that operates on any Value type and returns a Value
using TypedMethod = std::function<ValuePtr(ValuePtr self, const std::vector<ValuePtr>& args)>;

class ShapeRegistry {
public:
    // ... existing factory/method registration ...

    // New: register method for a specific value type
    void registerTypedMethod(ValueType type, const std::string& name, TypedMethod fn);

    // New: check if a method exists for a type
    bool hasTypedMethod(ValueType type, const std::string& name) const;

    // New: call a typed method
    ValuePtr callTypedMethod(ValueType type, const std::string& name,
                             ValuePtr self, const std::vector<ValuePtr>& args) const;

private:
    // Key: (ValueType, method name)
    std::unordered_map<ValueType,
        std::unordered_map<std::string, TypedMethod>> typedMethods_;
};
```

#### Registration for Phase 2 Types

```cpp
void ShapeRegistry::registerDefaults() {
    // ... existing SHAPE methods ...

    // ---- SHAPE face/edge methods ----
    registerTypedMethod(ValueType::SHAPE, "face", [](ValuePtr self, const auto& args) -> ValuePtr {
        auto shape = self->asShape();
        if (args.size() == 1 && args[0]->type() == ValueType::STRING) {
            return Value::makeFaceRef(shape->face(args[0]->asString()));
        }
        throw EvalError("face() requires a string selector argument");
    });

    registerTypedMethod(ValueType::SHAPE, "faces", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceSelector(self->asShape()->faces());
    });

    registerTypedMethod(ValueType::SHAPE, "edges", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeEdgeSelector(self->asShape()->edges());
    });

    registerTypedMethod(ValueType::SHAPE, "topFace", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asShape()->topFace());
    });

    registerTypedMethod(ValueType::SHAPE, "bottomFace", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asShape()->bottomFace());
    });

    // ---- FACE_REF methods ----
    registerTypedMethod(ValueType::FACE_REF, "sketch", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asFaceRef()->sketch());
    });

    registerTypedMethod(ValueType::FACE_REF, "workplane", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeWorkplane(self->asFaceRef()->workplane());
    });

    registerTypedMethod(ValueType::FACE_REF, "center", [](ValuePtr self, const auto& args) -> ValuePtr {
        gp_Pnt c = self->asFaceRef()->center();
        return Value::makeVector({c.X(), c.Y(), c.Z()});
    });

    registerTypedMethod(ValueType::FACE_REF, "normal", [](ValuePtr self, const auto& args) -> ValuePtr {
        gp_Dir n = self->asFaceRef()->normal();
        return Value::makeVector({n.X(), n.Y(), n.Z()});
    });

    registerTypedMethod(ValueType::FACE_REF, "area", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeNumber(self->asFaceRef()->area());
    });

    registerTypedMethod(ValueType::FACE_REF, "isPlanar", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeBool(self->asFaceRef()->isPlanar());
    });

    // ---- FACE_SELECTOR methods ----
    registerTypedMethod(ValueType::FACE_SELECTOR, "top", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->top());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "bottom", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->bottom());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "front", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->front());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "back", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->back());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "left", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->left());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "right", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->right());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "largest", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->largest());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "smallest", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceRef(self->asFaceSelector()->smallest());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "planar", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceSelector(self->asFaceSelector()->planar());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "cylindrical", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeFaceSelector(self->asFaceSelector()->cylindrical());
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "byIndex", [](ValuePtr self, const auto& args) -> ValuePtr {
        if (args.empty()) throw EvalError("byIndex() requires an index argument");
        return Value::makeFaceRef(self->asFaceSelector()->byIndex(
            static_cast<size_t>(args[0]->asNumber())));
    });

    registerTypedMethod(ValueType::FACE_SELECTOR, "count", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeNumber(static_cast<double>(self->asFaceSelector()->count()));
    });

    // ---- WORKPLANE methods ----
    registerTypedMethod(ValueType::WORKPLANE, "sketch", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asWorkplane()->sketch());
    });

    registerTypedMethod(ValueType::WORKPLANE, "offset", [](ValuePtr self, const auto& args) -> ValuePtr {
        if (args.empty()) throw EvalError("offset() requires a distance argument");
        return Value::makeWorkplane(self->asWorkplane()->offset(args[0]->asNumber()));
    });

    registerTypedMethod(ValueType::WORKPLANE, "extrude", [](ValuePtr self, const auto& args) -> ValuePtr {
        if (args.empty()) throw EvalError("extrude() requires a distance argument");
        return Value::makeShape(self->asWorkplane()->extrude(args[0]->asNumber()));
    });

    registerTypedMethod(ValueType::WORKPLANE, "cutBlind", [](ValuePtr self, const auto& args) -> ValuePtr {
        if (args.empty()) throw EvalError("cutBlind() requires a depth argument");
        return Value::makeShape(self->asWorkplane()->cutBlind(args[0]->asNumber()));
    });

    registerTypedMethod(ValueType::WORKPLANE, "cutThrough", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asWorkplane()->cutThrough());
    });

    // ---- SKETCH methods ----
    registerTypedMethod(ValueType::SKETCH, "circle", [](ValuePtr self, const auto& args) -> ValuePtr {
        auto sk = self->asSketch();
        double r = args.at(0)->asNumber();
        double cx = args.size() > 1 ? args[1]->asNumber() : 0.0;
        double cy = args.size() > 2 ? args[2]->asNumber() : 0.0;
        return Value::makeSketch(sk->circle(r, cx, cy));
    });

    registerTypedMethod(ValueType::SKETCH, "rect", [](ValuePtr self, const auto& args) -> ValuePtr {
        auto sk = self->asSketch();
        double w = args.at(0)->asNumber();
        double h = args.at(1)->asNumber();
        double cx = args.size() > 2 ? args[2]->asNumber() : 0.0;
        double cy = args.size() > 3 ? args[3]->asNumber() : 0.0;
        return Value::makeSketch(sk->rect(w, h, cx, cy));
    });

    registerTypedMethod(ValueType::SKETCH, "slot", [](ValuePtr self, const auto& args) -> ValuePtr {
        auto sk = self->asSketch();
        double l = args.at(0)->asNumber();
        double w = args.at(1)->asNumber();
        double cx = args.size() > 2 ? args[2]->asNumber() : 0.0;
        double cy = args.size() > 3 ? args[3]->asNumber() : 0.0;
        return Value::makeSketch(sk->slot(l, w, cx, cy));
    });

    registerTypedMethod(ValueType::SKETCH, "moveTo", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asSketch()->moveTo(
            args.at(0)->asNumber(), args.at(1)->asNumber()));
    });

    registerTypedMethod(ValueType::SKETCH, "lineTo", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asSketch()->lineTo(
            args.at(0)->asNumber(), args.at(1)->asNumber()));
    });

    registerTypedMethod(ValueType::SKETCH, "arcTo", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asSketch()->arcTo(
            args.at(0)->asNumber(), args.at(1)->asNumber(),
            args.at(2)->asNumber(), args.at(3)->asNumber()));
    });

    registerTypedMethod(ValueType::SKETCH, "close", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeSketch(self->asSketch()->close());
    });

    registerTypedMethod(ValueType::SKETCH, "extrude", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asSketch()->extrude(args.at(0)->asNumber()));
    });

    registerTypedMethod(ValueType::SKETCH, "cutBlind", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asSketch()->cutBlind(args.at(0)->asNumber()));
    });

    registerTypedMethod(ValueType::SKETCH, "cutThrough", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asSketch()->cutThrough());
    });

    registerTypedMethod(ValueType::SKETCH, "revolve", [](ValuePtr self, const auto& args) -> ValuePtr {
        double angle = args.empty() ? 360.0 : args[0]->asNumber();
        return Value::makeShape(self->asSketch()->revolve(angle));
    });

    // ---- EDGE_SELECTOR methods ----
    registerTypedMethod(ValueType::EDGE_SELECTOR, "parallelTo", [](ValuePtr self, const auto& args) -> ValuePtr {
        // args[0] is a vector [x, y, z] representing a direction
        auto v = args.at(0)->asVector();
        gp_Dir dir(v[0], v[1], v[2]);
        return Value::makeEdgeSelector(self->asEdgeSelector()->parallelTo(dir));
    });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "vertical", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeEdgeSelector(self->asEdgeSelector()->vertical());
    });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "horizontal", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeEdgeSelector(self->asEdgeSelector()->horizontal());
    });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "fillet", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asEdgeSelector()->fillet(args.at(0)->asNumber()));
    });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "chamfer", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeShape(self->asEdgeSelector()->chamfer(args.at(0)->asNumber()));
    });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "count", [](ValuePtr self, const auto& args) -> ValuePtr {
        return Value::makeNumber(static_cast<double>(self->asEdgeSelector()->count()));
    });
}
```

#### Updated `applyMethodChain` in Evaluator

```cpp
ValuePtr Evaluator::applyMethodChain(
    ValuePtr current,
    const std::vector<OpenDCADParser::MethodCallContext*>& methods)
{
    auto& registry = ShapeRegistry::instance();

    for (auto* method : methods) {
        std::string methodName = method->IDENT()->getText();
        std::vector<ValuePtr> args = evaluateArgs(method->argList());
        ValueType currentType = current->type();

        // First, try the typed method registry (handles all types)
        if (registry.hasTypedMethod(currentType, methodName)) {
            current = registry.callTypedMethod(currentType, methodName, current, args);
            continue;
        }

        // Fallback: legacy SHAPE-only methods (for backward compatibility)
        if (currentType == ValueType::SHAPE && registry.hasMethod(methodName)) {
            current = registry.callMethod(methodName, current->asShape(), args);
            continue;
        }

        throw EvalError(
            "unknown method '" + methodName + "' on type " + current->typeName(),
            locFrom(method));
    }

    return current;
}
```

### Value Type Flow Diagram

The following diagram shows how the value type transitions through a typical method chain:

```
box(40, 30, 10)              --> ValueType::SHAPE
    .face(">Z")              --> ValueType::FACE_REF
    .sketch()                --> ValueType::SKETCH
    .circle(8)               --> ValueType::SKETCH
    .circle(3, -10, 0)       --> ValueType::SKETCH
    .extrude(15)             --> ValueType::SHAPE
    .face(">Z")              --> ValueType::FACE_REF
    .sketch()                --> ValueType::SKETCH
    .circle(5)               --> ValueType::SKETCH
    .cutThrough()            --> ValueType::SHAPE
    .edges()                 --> ValueType::EDGE_SELECTOR
    .vertical()              --> ValueType::EDGE_SELECTOR
    .fillet(2)               --> ValueType::SHAPE
```

Each transition is enforced at runtime by the typed method dispatch. If a user calls `.circle()` on a `SHAPE` value, the registry lookup fails and throws an `EvalError` with a clear message: `"unknown method 'circle' on type shape"`.

---

## Required OCCT Headers

The following OCCT headers are needed for Phase 2 classes, in addition to those already used:

| Header | Used by | Purpose |
|--------|---------|---------|
| `BRepAdaptor_Surface.hxx` | FaceRef, FaceSelector | Surface type queries, parameterization |
| `BRepGProp.hxx` | FaceRef, FaceSelector | `SurfaceProperties` for area, centroid |
| `GProp_GProps.hxx` | FaceRef, FaceSelector | Properties result container |
| `BRepTools.hxx` | FaceRef, Workplane | `UVBounds` for parametric midpoint |
| `BRepAdaptor_Curve.hxx` | EdgeSelector | Curve type queries, tangent direction |
| `GCPnts_AbscissaPoint.hxx` | EdgeSelector | Arc length measurement |
| `BRepBuilderAPI_MakeEdge.hxx` | Sketch | Creating edges from points, circles |
| `BRepBuilderAPI_MakeWire.hxx` | Sketch | Assembling edges into wires |
| `BRepBuilderAPI_MakeFace.hxx` | Sketch | Creating faces from wires |
| `BRepPrimAPI_MakePrism.hxx` | Sketch, Workplane | Extrude operation |
| `BRepPrimAPI_MakeRevol.hxx` | Sketch | Revolve operation |
| `BRepFilletAPI_MakeChamfer.hxx` | Shape, EdgeSelector | Chamfer operation |
| `Bnd_Box.hxx` | Sketch, Workplane | Bounding box for cutThrough distance |
| `BRepBndLib.hxx` | Sketch, Workplane | Adding shapes to bounding box |
| `TopExp.hxx` | EdgeSelector, Shape | `MapShapesAndAncestors` |
| `TopTools_IndexedDataMapOfShapeListOfShape.hxx` | EdgeSelector, Shape | Edge-face ancestry map |
| `gp_Circ.hxx` | Sketch | Circle geometry |
| `gp_Pln.hxx` | FaceRef, Workplane | Plane geometry |
| `gp_Ax1.hxx` | Sketch | Revolve axis |
| `GeomAbs_SurfaceType.hxx` | FaceSelector | Surface type enumeration |
| `GeomAbs_CurveType.hxx` | EdgeSelector | Curve type enumeration |

### Additional OCCT CMake Components

Add to `_occt_components` in `CMakeLists.txt`:

```cmake
TKGeomAlgo    # BRepAdaptor_Surface, BRepAdaptor_Curve, GCPnts_AbscissaPoint
TKBool        # BRepAlgoAPI (already indirectly included but explicit is safer)
```

---

## CMakeLists.txt Changes

### New Source Files

```cmake
add_executable(opendcad
    # ... existing sources ...
    src/geometry/FaceRef.cpp
    src/geometry/FaceSelector.cpp
    src/geometry/EdgeSelector.cpp
    src/geometry/Workplane.cpp
    src/geometry/Sketch.cpp
)
```

### New Include Directories

```cmake
target_include_directories(opendcad PRIVATE
    # ... existing includes ...
    ${CMAKE_SOURCE_DIR}/src/geometry
)
```

This should already be present from Phase 0, but verify the geometry subdirectory is included.

---

## Implementation Order

The implementation should proceed in dependency order. Each step produces compilable, testable code.

```
Step 1: Value type extensions           (no new files, modify Value.h/Value.cpp)
Step 2: FaceRef                         (src/geometry/FaceRef.h, FaceRef.cpp)
Step 3: FaceSelector                    (src/geometry/FaceSelector.h, FaceSelector.cpp)
Step 4: Shape class additions           (modify Shape.h, Shape.cpp)
Step 5: Workplane                       (src/geometry/Workplane.h, Workplane.cpp)
Step 6: Sketch                          (src/geometry/Sketch.h, Sketch.cpp)
Step 7: EdgeSelector                    (src/geometry/EdgeSelector.h, EdgeSelector.cpp)
Step 8: ShapeRegistry typed methods     (modify ShapeRegistry.cpp)
Step 9: Evaluator multi-type dispatch   (modify Evaluator.cpp)
Step 10: CMakeLists.txt updates         (add new source files)
```

### Step-by-Step Details

**Step 1: Value type extensions.** Add `FACE_REF`, `FACE_SELECTOR`, `WORKPLANE`, `SKETCH`, `EDGE_SELECTOR` to the `ValueType` enum. Add corresponding factory methods and accessors to the `Value` class. Add forward declarations and `#include` for the new geometry headers. This step compiles but the new types are not yet used.

**Step 2: FaceRef.** Implement the `FaceRef` class. Depends only on OCCT and Shape. Test by constructing a box, enumerating its faces with `TopExp_Explorer`, wrapping one in a `FaceRef`, and calling `center()`, `normal()`, `isPlanar()`, `area()`.

**Step 3: FaceSelector.** Implement the `FaceSelector` class. Depends on `FaceRef`. Test by constructing a box and calling `FaceSelector(boxShape).top()`, verifying the returned face has a normal of `(0, 0, 1)`.

**Step 4: Shape class additions.** Add `enable_shared_from_this`, `faces()`, `face()`, `topFace()`, `bottomFace()`, `edges()`, `filletEdges()`, `chamferEdges()`. This wires FaceSelector and EdgeSelector into the Shape API. Test with `shape->face(">Z")->isPlanar()`.

**Step 5: Workplane.** Implement the `Workplane` class. Depends on `FaceRef` and `Shape`. Test by creating a workplane on the top face of a box, verifying `origin()` is at `(0, 0, 10)` for a box of height 10, and `normal()` is `(0, 0, 1)`.

**Step 6: Sketch.** Implement the `Sketch` class. This is the largest step. Depends on `Workplane` and `Shape`. Test with:
- `Sketch(wp).circle(5).extrude(10)` should produce a cylinder fused to the box.
- `Sketch(wp).rect(10, 10).cutBlind(3)` should produce a rectangular pocket.
- `Sketch(wp).circle(5).cutThrough()` should produce a through-hole.

**Step 7: EdgeSelector.** Implement the `EdgeSelector` class. Depends on `Shape` and `FaceRef`. Test with `shape->edges()->vertical()->fillet(2)` on a box.

**Step 8: ShapeRegistry typed methods.** Register all the typed methods shown in the "Registration for Phase 2 Types" section. This wires the C++ API into the DSL dispatch system.

**Step 9: Evaluator multi-type dispatch.** Update `applyMethodChain` to use the typed method registry. The existing SHAPE-only dispatch becomes a fallback.

**Step 10: CMakeLists.txt.** Add the new `.cpp` files to the build and verify the build succeeds end to end.

---

## Files to Create

| File | Lines (est.) | Purpose |
|------|-------------|---------|
| `src/geometry/FaceRef.h` | 60 | FaceRef class declaration |
| `src/geometry/FaceRef.cpp` | 100 | FaceRef implementation (OCCT queries) |
| `src/geometry/FaceSelector.h` | 80 | FaceSelector class declaration |
| `src/geometry/FaceSelector.cpp` | 180 | FaceSelector implementation (direction/type/size queries) |
| `src/geometry/EdgeSelector.h` | 65 | EdgeSelector class declaration |
| `src/geometry/EdgeSelector.cpp` | 150 | EdgeSelector implementation (direction/topology queries) |
| `src/geometry/Workplane.h` | 70 | Workplane class declaration |
| `src/geometry/Workplane.cpp` | 120 | Workplane implementation (coordinate transforms, extrude) |
| `src/geometry/Sketch.h` | 75 | Sketch class declaration |
| `src/geometry/Sketch.cpp` | 250 | Sketch implementation (primitives, wire building, termination) |

## Files to Modify

| File | Changes |
|------|---------|
| `src/parser/Value.h` | Add 5 new ValueType enum values, factory methods, accessors, forward declarations |
| `src/parser/Value.cpp` | Implement new factory methods and accessors (~40 lines) |
| `src/geometry/Shape.h` | Add `enable_shared_from_this`, 8 new method declarations, forward declarations |
| `src/geometry/Shape.cpp` | Implement `faces()`, `face()`, `topFace()`, `bottomFace()`, `edges()`, `filletEdges()`, `chamferEdges()` (~60 lines) |
| `src/geometry/ShapeRegistry.h` | Add `registerTypedMethod`, `hasTypedMethod`, `callTypedMethod` declarations, `typedMethods_` member |
| `src/geometry/ShapeRegistry.cpp` | Register all Phase 2 typed methods (~200 lines) |
| `src/parser/Evaluator.cpp` | Update `applyMethodChain` to use typed dispatch (~15 lines changed) |
| `CMakeLists.txt` | Add 5 new `.cpp` files, potentially add `TKGeomAlgo` component |

---

## Verification

### Test Script

After implementation, create `examples/face_modeling.dcad`:

```
// Test 1: Boss on a box
let base = box(40, 30, 10);
let withBoss = base.face(">Z").sketch().circle(8).extrude(15);

// Test 2: Through-hole in the boss
let withHole = withBoss.face(">Z").sketch().circle(5).cutThrough();

// Test 3: Rectangular pocket on the bottom
let withPocket = withHole.face("<Z").sketch().rect(20, 15).cutBlind(3);

// Test 4: Selective edge fillet
let final = withPocket.edges().vertical().fillet(2);

export final as face_test;
```

### Expected Results

```bash
./build/bin/opendcad examples/face_modeling.dcad build/bin
```

Should produce `build/bin/face_test.step` and `build/bin/face_test.stl`.

Opening `face_test.step` in a CAD viewer should show:
- A 40x30x10 box base
- A cylindrical boss (r=8, h=15) centered on the top face
- A through-hole (r=5) centered on the boss top, going all the way through the boss and base
- A rectangular pocket (20x15, 3 deep) on the bottom face
- 2mm fillets on all vertical edges of the base

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Non-planar faces** | Workplane creation on cylindrical or freeform faces will fail. Users may not understand why `face(">X")` on a cylinder does not work as expected. | Restrict Workplane/Sketch to planar faces in Phase 2. Throw a clear `GeometryError("workplane requires a planar face")`. Document this limitation. Phase 3+ can add cylindrical surface unwrapping. |
| **Face selection ambiguity** | On a box, `face(">Z")` is unambiguous. After boolean operations, there may be multiple faces with similar normals. The "best dot product" heuristic may not pick the intended face. | Provide `faces().planar().largest()` as a more explicit selector. Document that `face(">Z")` picks the face whose normal is most aligned with +Z, which may not be unique after booleans. |
| **`enable_shared_from_this` migration** | Changing `Shape` to inherit from `enable_shared_from_this` requires that all `Shape` objects are managed by `shared_ptr`. Any stack-allocated `Shape` or raw `new Shape()` will cause undefined behavior when `shared_from_this()` is called. | Audit all `Shape` construction sites. The existing code already uses `ShapePtr` everywhere. Replace `ShapePtr(new Shape(...))` with `std::make_shared<Shape>(...)`. |
| **Sketch wire orientation** | OCCT is sensitive to wire orientation. If a wire is wound in the wrong direction relative to the face normal, `BRepBuilderAPI_MakeFace` may interpret it as a hole rather than a profile (or vice versa). | Always construct wires counter-clockwise when viewed from the normal direction. Use `BRepTools::RemoveInternals` or `ShapeAnalysis::FreeBounds` to validate wire orientation if problems arise. |
| **Through-cut bounding box overestimate** | `cutThrough()` uses 2x the bounding box diagonal as the cut distance. For very large shapes, this creates unnecessarily deep cuts that may slow OCCT boolean operations. | The safety margin is acceptable for Phase 2. If performance becomes an issue, switch to `BRepAlgoAPI_Cut` with a half-space solid instead of a finite prism. |
| **OCCT boolean operation robustness** | Fusing or cutting small features on faces near edges or vertices can cause OCCT boolean failures ("BOPAlgo_AlertTooFewArguments", null shape results). | Wrap all OCCT boolean operations in try/catch. Use `ShapeFix_Shape` on intermediate results. Report the failing DSL source location in the error message. Consider `BRepAlgoAPI_Fuse::SetFuzzyValue()` to improve tolerance handling. |
| **Memory: parent shape references** | `FaceRef` and `Workplane` hold `ShapePtr` to the parent shape. If a sketch extrude creates a new shape and the chain continues with `.face(">Z").sketch().extrude()`, the intermediate shapes remain alive in memory. | This is acceptable for typical scripts. Document that extremely long chains with many intermediate shapes may consume significant memory. A future garbage collection pass could break these references. |
| **Evaluator backward compatibility** | The updated `applyMethodChain` must still handle all Phase 0 method calls correctly. | The typed dispatch falls back to the legacy `ShapeMethod` dispatch for `SHAPE` values, so all existing scripts continue to work without modification. |
