# Phase 2b: Advanced Modeling Roadmap

## Overview

Phase 2 delivered face-relative modeling (face selection, workplanes, sketches, extrude/cut, edge selection). Phase 2b defines the full feature roadmap beyond Phase 2, capturing everything needed for a complete parametric modeling engine.

## 1. Sketch Improvements

- `slot(length, width)` — stadium shape (rectangle with semicircular endcaps)
- `arcTo(x, y, tx, ty)` — freeform arc from tangent point
- `revolve(angle)` — revolve sketch profiles around workplane X-axis
- 2D fillet/chamfer on sketch corners
- Offset curves (inset/outset a wire by distance)
- Splines/B-splines for organic shapes
- Construction geometry (reference lines/circles for alignment, not extruded)

## 2. Feature Patterns

- `linearPattern(shape, direction, count, spacing)` — repeat a feature along a vector
- `circularPattern(shape, axis, count, angle)` — repeat around an axis
- `mirrorFeature(shape, plane)` — mirror a feature across a plane
- These work on any Shape, not just sketch features

## 3. Parametric Holes

- `hole(diameter, depth)` — simple blind hole
- `throughHole(diameter)` — through-all hole
- `counterbore(holeDia, boreDia, boreDepth, holeDepth)` — counterbore
- `countersink(holeDia, sinkDia, sinkAngle, holeDepth)` — countersink
- Applied to faces like sketch features: `shape.face(">Z").hole(5, 10)`

## 4. Advanced Face/Edge Selection

- `FaceSelector.nearestTo(point)`, `farthestFrom(point)` — position-based
- `EdgeSelector.betweenFaces(f1, f2)` — edge at face intersection
- `EdgeSelector.ofFace(faceRef)` — edges of a specific face
- `EdgeSelector.longest()`, `shortest()` — by arc length
- Select by area/length range

## 5. Advanced Operations

- Draft angles on faces (tapered walls for molds/casting)
- Split body at plane
- Rib/web features
- Thicken surface to solid

## 6. Reusable Parametric Components

```
// enclosure.dcad
param width = 80;
param depth = 120;
param height = 40;
param wall = 2;

let shell = box(width, depth, height).shell(wall);
export shell as enclosure;
```

```
// main.dcad
use "enclosure.dcad" as enc(width=100, depth=80, height=30);
let pcb_cutout = enc.face(">Z").sketch().rect(90, 70).cutBlind(25);
export pcb_cutout as final;
```

## 7. External Model Import

- `import_step("path/to/model.step")` — load STEP file as Shape
- `import_stl("path/to/model.stl")` — load STL mesh as Shape
- Imported shapes participate fully in method chains

## 8. Additional Export Formats

- 3MF export (modern replacement for STL, supports color/material)
- DXF/SVG export for 2D sketch profiles (laser cutting)

## Implementation Priority

1. Reusable components (`use`/`param`) — highest value, enables library ecosystem
2. Feature patterns — very common in mechanical design
3. Parametric holes — every part has holes
4. External model import (STEP first, then STL) — enables KiCad workflow
5. Sketch improvements — incremental polish
6. Advanced selection — convenience features
7. Advanced operations — specialized use cases
8. Additional exports — nice to have

## Dependencies

Most of these features depend on Phase 3 (Language & Parser) which adds functions, parameters, conditionals, and loops to the DSL. Reusable components (item 6) specifically requires `param` declarations and `use` statements from Phase 3.
