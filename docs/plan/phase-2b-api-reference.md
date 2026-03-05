# Phase 2b API Reference — Programmatic C++ Usage

All methods use `ShapePtr` (`std::shared_ptr<Shape>`) and related smart pointer types. All constructors use `std::make_shared`.

---

## Shape Factories

### 3D Primitives
```cpp
ShapePtr box     = Shape::createBox(width, depth, height);
ShapePtr cyl     = Shape::createCylinder(radius, height);
ShapePtr sphere  = Shape::createSphere(radius);
ShapePtr cone    = Shape::createCone(r1, r2, height);
ShapePtr torus   = Shape::createTorus(r1, r2);          // optional angle
ShapePtr wedge   = Shape::createWedge(dx, dy, dz, ltx);
ShapePtr bin     = Shape::createBin(w, d, h, thickness);
```

### 2D Primitives (for extrusion)
```cpp
ShapePtr circ = Shape::createCircle(radius);
ShapePtr rect = Shape::createRectangle(width, height);
ShapePtr poly = Shape::createPolygon({{x1,y1}, {x2,y2}, ...});
```

### Multi-Shape
```cpp
ShapePtr lofted = Shape::createLoft({profile1, profile2}, solid, ruled);
```

### External Import (Phase 2b)
```cpp
ShapePtr fromStep = Shape::importSTEP("/path/to/model.step");
ShapePtr fromStl  = Shape::importSTL("/path/to/mesh.stl");
```

---

## Boolean Operations
```cpp
ShapePtr result = a->fuse(b);       // union
ShapePtr result = a->cut(b);        // subtraction
ShapePtr result = a->intersect(b);  // intersection
```

---

## Transforms
```cpp
ShapePtr moved   = shape->translate(x, y, z);
ShapePtr rotated = shape->rotate(xDeg, yDeg, zDeg);
ShapePtr scaled  = shape->scale(factor);         // uniform
ShapePtr scaled  = shape->scale(fx, fy, fz);     // non-uniform
ShapePtr flipped = shape->flip();                 // mirror across XY
ShapePtr mirrored = shape->mirror(nx, ny, nz);   // mirror across plane (replaces)

// Axis shortcuts
ShapePtr moved = shape->x(10);  // translate along X
ShapePtr moved = shape->y(5);
ShapePtr moved = shape->z(-3);
```

---

## Edge Operations
```cpp
ShapePtr filleted  = shape->fillet(radius);    // all edges
ShapePtr chamfered = shape->chamfer(distance); // all edges
```

---

## Extrusion / Sweep
```cpp
ShapePtr solid  = shape2D->linearExtrude(height);
ShapePtr revol  = shape2D->rotateExtrude(angleDeg);
ShapePtr swept  = profile->sweep(pathShape);
```

---

## Shell
```cpp
ShapePtr shelled = shape->shell(thickness); // hollows, removes top face
```

---

## Feature Patterns (Phase 2b)
```cpp
// Linear: replicate along direction vector, count total copies
ShapePtr row = post->linearPattern(dx, dy, dz, count);
// Example: 4 copies spaced 15mm along X
auto row = cylinder(3, 20)->linearPattern(15, 0, 0, 4);

// Circular: replicate around axis through origin
ShapePtr ring = peg->circularPattern(ax, ay, az, count, angleDeg);
// Example: 6 copies evenly around Z axis
auto ring = post->translate(20, 0, 0)->circularPattern(0, 0, 1, 6);

// Partial angle: 3 copies in 180 degrees
auto arc = post->circularPattern(0, 0, 1, 3, 180.0);

// Mirror + fuse (unlike mirror() which replaces)
ShapePtr symmetric = half->mirrorFeature(nx, ny, nz);
// Example: mirror across YZ plane and fuse both halves
auto whole = halfBody->mirrorFeature(1, 0, 0);
```

---

## Advanced Operations (Phase 2b)
```cpp
// Draft: apply taper angle to faces perpendicular to direction
ShapePtr drafted = box->draft(angleDeg, nx, ny, nz);
// Example: 5-degree draft on vertical faces (pull direction = Z)
auto drafted = Shape::createBox(20, 20, 30)->draft(5, 0, 0, 1);

// Split at plane: returns the piece on positive-normal side
ShapePtr half = body->splitAt(px, py, pz, nx, ny, nz);
// Example: split box in half at X=0
auto half = box->splitAt(0, 0, 0, 1, 0, 0);
```

---

## Face Selection

### From Shape
```cpp
FaceSelectorPtr allFaces = shape->faces();
FaceRefPtr face = shape->face(">Z");  // ">Z", "<Z", ">X", "<X", ">Y", "<Y"
FaceRefPtr top  = shape->topFace();
FaceRefPtr bot  = shape->bottomFace();
```

### FaceSelector Filters
```cpp
FaceRefPtr    top     = selector->top();        // best +Z normal
FaceRefPtr    bottom  = selector->bottom();     // best -Z normal
FaceRefPtr    front   = selector->front();      // best -Y normal
FaceRefPtr    back    = selector->back();       // best +Y normal
FaceRefPtr    left    = selector->left();       // best -X normal
FaceRefPtr    right   = selector->right();      // best +X normal
FaceRefPtr    biggest = selector->largest();
FaceRefPtr    tiniest = selector->smallest();
FaceRefPtr    byIdx   = selector->byIndex(0);
FaceSelectorPtr flat  = selector->planar();
FaceSelectorPtr round = selector->cylindrical();
int count             = selector->count();
```

### Advanced Face Selection (Phase 2b)
```cpp
// Distance-based
FaceRefPtr nearest  = selector->nearestTo(gp_Pnt(0, 0, 100));
FaceRefPtr farthest = selector->farthestFrom(gp_Pnt(0, 0, 0));

// Area-based filtering
FaceSelectorPtr big   = selector->areaGreaterThan(500.0);
FaceSelectorPtr small = selector->areaLessThan(100.0);
```

---

## FaceRef Properties
```cpp
gp_Pnt center  = faceRef->center();
gp_Dir normal  = faceRef->normal();
gp_Pln plane   = faceRef->plane();
bool   planar  = faceRef->isPlanar();
double area    = faceRef->area();
int    nEdges  = faceRef->edgeCount();
```

---

## Workplane
```cpp
WorkplanePtr wp = faceRef->workplane();
WorkplanePtr offset = wp->offset(distance);
SketchPtr sk = wp->draw();                    // or faceRef->draw()
```

---

## Sketch Profiles
```cpp
SketchPtr sk = faceRef->draw();

// Built-in profiles
sk->circle(radius, cx, cy);
sk->rect(width, height, cx, cy);
sk->polygon({{x1,y1}, {x2,y2}, {x3,y3}});

// Phase 2b: Stadium slot
sk->slot(length, width, cx, cy);
```

---

## Freeform Wire Building
```cpp
sk->moveTo(x, y);
sk->lineTo(x, y);

// Phase 2b: Arc segment (bulge = sagitta distance, 0 = straight line)
sk->arcTo(x, y, bulge);

// Phase 2b: B-spline through points
sk->splineTo({{tx1,ty1}, {tx2,ty2}}, endX, endY);

sk->close();
```

### Example: Mixed arc+line profile
```cpp
auto sk = box->face(">Z")->draw();
sk->moveTo(-10, 0);
sk->arcTo(0, 10, 5);       // arc with 5mm bulge
sk->arcTo(10, 0, 5);
sk->lineTo(10, -5);
sk->lineTo(-10, -5);
sk->close();
auto result = sk->extrude(5);
```

---

## Sketch Wire Modifications (Phase 2b)
```cpp
// Round all corners of the last wire
sk->fillet2D(radius);

// Chamfer all corners (45-degree symmetric)
sk->chamfer2D(distance);

// Expand/shrink wire profile
sk->offset(distance);  // positive = outward, negative = inward
```

### Example: Rounded rectangle extrusion
```cpp
auto result = box->face(">Z")->draw()
    ->rect(20, 15)
    ->fillet2D(3)
    ->extrude(5);
```

---

## Sketch Operations (Geometry Producers)
```cpp
ShapePtr solid = sk->extrude(height);      // extrude + fuse with parent
ShapePtr cut1  = sk->cutBlind(depth);      // cut into body by depth
ShapePtr cut2  = sk->cutThrough();         // cut through entire body

// Phase 2b: Revolve around workplane X-axis + fuse with parent
ShapePtr revolved = sk->revolve(angleDeg); // default 360
```

---

## Parametric Holes (Phase 2b)

Convenience methods on `FaceRef` that create precisely positioned holes:

```cpp
// Blind hole (diameter, depth, optional offset from face center)
ShapePtr h1 = face->hole(6.0, 5.0);              // centered
ShapePtr h2 = face->hole(4.0, 5.0, 10.0, 5.0);   // offset

// Through-all hole
ShapePtr h3 = face->throughHole(6.0);
ShapePtr h4 = face->throughHole(4.0, 10.0, 5.0);  // offset

// Counterbore: bore cylinder + deeper narrow hole
ShapePtr cb = face->counterbore(holeDia, boreDia, boreDepth, holeDepth);
// Example: M5 counterbore
auto cb = box->face(">Z")->counterbore(5, 10, 3, 15);

// Countersink: conical entry + narrow hole
ShapePtr cs = face->countersink(holeDia, sinkDia, sinkAngle, holeDepth);
// Example: 90-degree countersink
auto cs = box->face(">Z")->countersink(5, 10, 90, 15);
```

---

## Edge Selection

### From Shape
```cpp
EdgeSelectorPtr allEdges = shape->edges();
```

### EdgeSelector Filters
```cpp
EdgeSelectorPtr vert  = selector->vertical();
EdgeSelectorPtr horiz = selector->horizontal();
EdgeSelectorPtr par   = selector->parallelTo(gp_Dir(1, 0, 0));
EdgeSelectorPtr perp  = selector->perpendicularTo(gp_Dir(0, 0, 1));
int count             = selector->count();
```

### Advanced Edge Selection (Phase 2b)
```cpp
// Filter to edges of a specific face
EdgeSelectorPtr faceEdges = selector->ofFace(faceRef);

// Single-edge extremum selectors
EdgeSelectorPtr longest  = selector->longest();   // 1 edge
EdgeSelectorPtr shortest = selector->shortest();  // 1 edge

// Length-based filtering
EdgeSelectorPtr long_  = selector->longerThan(25.0);
EdgeSelectorPtr short_ = selector->shorterThan(5.0);
```

### Edge Operations
```cpp
ShapePtr filleted  = edgeSelector->fillet(radius);
ShapePtr chamfered = edgeSelector->chamfer(distance);
```

---

## DSL Syntax Reference

All C++ methods are exposed in the DSL via the ShapeRegistry:

```dcad
// Factories
let b = box(40, 30, 10);
let c = cylinder(5, 20);

// Face selection and holes
let h = b.face(">Z").throughHole(4, -15, -10);
let cb = b.face(">Z").counterbore(5, 10, 3, 15);

// Sketch profiles
let boss = b.face(">Z").draw().circle(8).extrude(15);
let slot = b.face(">Z").draw().slot(25, 8).cutThrough();
let rounded = b.face(">Z").draw().rect(20, 15).fillet2D(2).extrude(5);

// Revolve
let ring = b.face(">Z").draw().circle(2, 0, 12).revolve(360);

// Patterns
let row = c.linearPattern([15, 0, 0], 4);
let ring = c.translate([15, 0, 0]).circularPattern([0, 0, 1], 6);
let sym = b.mirrorFeature([1, 0, 0]);

// Advanced operations
let drafted = b.draft(5, [0, 0, 1]);
let half = b.splitAt([0, 0, 0], [1, 0, 0]);

// Selection
let topFace = b.faces().nearestTo([0, 0, 100]);
let bigFaces = b.faces().areaGreaterThan(500);
let longEdges = b.edges().longerThan(25);
let topEdges = b.edges().ofFace(b.face(">Z"));

// Import
let ext = import_step("part.step");
let mesh = import_stl("scan.stl");

// Edge operations
let filleted = b.edges().vertical().fillet(2);
```

---

## Method Registry Summary

| Type | Method | Returns | DSL Name |
|------|--------|---------|----------|
| Factory | `box(w,d,h)` | Shape | `box` |
| Factory | `cylinder(r,h)` | Shape | `cylinder` |
| Factory | `sphere(r)` | Shape | `sphere` |
| Factory | `cone(r1,r2,h)` | Shape | `cone` |
| Factory | `torus(r1,r2)` | Shape | `torus` |
| Factory | `wedge(dx,dy,dz,ltx)` | Shape | `wedge` |
| Factory | `bin(w,d,h,t)` | Shape | `bin` |
| Factory | `circle(r)` | Shape | `circle` |
| Factory | `rectangle(w,h)` | Shape | `rectangle` |
| Factory | `polygon(pts...)` | Shape | `polygon` |
| Factory | `loft(profiles...)` | Shape | `loft` |
| Factory | `import_step(path)` | Shape | `import_step` |
| Factory | `import_stl(path)` | Shape | `import_stl` |
| Shape | `fuse(other)` | Shape | `fuse` |
| Shape | `cut(other)` | Shape | `cut` |
| Shape | `intersect(other)` | Shape | `intersect` |
| Shape | `fillet(r)` | Shape | `fillet` |
| Shape | `chamfer(d)` | Shape | `chamfer` |
| Shape | `translate(v)` | Shape | `translate` |
| Shape | `rotate(v)` | Shape | `rotate` |
| Shape | `scale(f)` | Shape | `scale` |
| Shape | `mirror(v)` | Shape | `mirror` |
| Shape | `shell(t)` | Shape | `shell` |
| Shape | `linear_extrude(h)` | Shape | `linear_extrude` |
| Shape | `rotate_extrude(a)` | Shape | `rotate_extrude` |
| Shape | `sweep(path)` | Shape | `sweep` |
| Shape | `linearPattern(v,n)` | Shape | `linearPattern` |
| Shape | `circularPattern(v,n[,a])` | Shape | `circularPattern` |
| Shape | `mirrorFeature(v)` | Shape | `mirrorFeature` |
| Shape | `draft(a,v)` | Shape | `draft` |
| Shape | `splitAt(p,n)` | Shape | `splitAt` |
| Shape | `face(sel)` | FaceRef | `face` |
| Shape | `faces()` | FaceSelector | `faces` |
| Shape | `edges()` | EdgeSelector | `edges` |
| FaceRef | `draw()` | Sketch | `draw` |
| FaceRef | `hole(d,depth[,cx,cy])` | Shape | `hole` |
| FaceRef | `throughHole(d[,cx,cy])` | Shape | `throughHole` |
| FaceRef | `counterbore(hd,bd,bdep,hdep[,cx,cy])` | Shape | `counterbore` |
| FaceRef | `countersink(hd,sd,sa,hdep[,cx,cy])` | Shape | `countersink` |
| FaceRef | `area()` | Number | `area` |
| FaceRef | `normal()` | Vector | `normal` |
| FaceRef | `center()` | Vector | `center` |
| FaceRef | `isPlanar()` | Bool | `isPlanar` |
| FaceSelector | `top/bottom/front/back/left/right()` | FaceRef | same |
| FaceSelector | `largest/smallest()` | FaceRef | same |
| FaceSelector | `nearestTo(v)` | FaceRef | `nearestTo` |
| FaceSelector | `farthestFrom(v)` | FaceRef | `farthestFrom` |
| FaceSelector | `areaGreaterThan(n)` | FaceSelector | `areaGreaterThan` |
| FaceSelector | `areaLessThan(n)` | FaceSelector | `areaLessThan` |
| FaceSelector | `planar/cylindrical()` | FaceSelector | same |
| FaceSelector | `count()` | Number | `count` |
| EdgeSelector | `vertical/horizontal()` | EdgeSelector | same |
| EdgeSelector | `parallelTo(v)` | EdgeSelector | `parallelTo` |
| EdgeSelector | `longest/shortest()` | EdgeSelector | same |
| EdgeSelector | `longerThan(n)/shorterThan(n)` | EdgeSelector | same |
| EdgeSelector | `ofFace(faceRef)` | EdgeSelector | `ofFace` |
| EdgeSelector | `fillet(r)` | Shape | `fillet` |
| EdgeSelector | `chamfer(d)` | Shape | `chamfer` |
| EdgeSelector | `count()` | Number | `count` |
| Sketch | `circle(r[,cx,cy])` | Sketch | `circle` |
| Sketch | `rect(w,h[,cx,cy])` | Sketch | `rect` |
| Sketch | `polygon(pts...)` | Sketch | `polygon` |
| Sketch | `slot(l,w[,cx,cy])` | Sketch | `slot` |
| Sketch | `moveTo(x,y)` | Sketch | `moveTo` |
| Sketch | `lineTo(x,y)` | Sketch | `lineTo` |
| Sketch | `arcTo(x,y,bulge)` | Sketch | `arcTo` |
| Sketch | `splineTo(pts...,ex,ey)` | Sketch | `splineTo` |
| Sketch | `close()` | Sketch | `close` |
| Sketch | `fillet2D(r)` | Sketch | `fillet2D` |
| Sketch | `chamfer2D(d)` | Sketch | `chamfer2D` |
| Sketch | `offset(d)` | Sketch | `offset` |
| Sketch | `extrude(h)` | Shape | `extrude` |
| Sketch | `cutBlind(d)` | Shape | `cutBlind` |
| Sketch | `cutThrough()` | Shape | `cutThrough` |
| Sketch | `revolve([a])` | Shape | `revolve` |
| Workplane | `draw()` | Sketch | `draw` |
| Workplane | `offset(d)` | Workplane | `offset` |
