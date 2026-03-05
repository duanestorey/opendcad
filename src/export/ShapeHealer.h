#pragma once

#include <TopoDS_Shape.hxx>
#include <ShapeFix_Shape.hxx>
#include <BRepCheck_Analyzer.hxx>

namespace opendcad {

inline TopoDS_Shape healShape(const TopoDS_Shape& shape) {
    // Only heal shapes that actually have topology issues.
    // OCCT-generated primitives are already valid; running ShapeFix_Shape
    // on them can damage the geometry (e.g., demoting solids to shells).
    BRepCheck_Analyzer checker(shape);
    if (checker.IsValid())
        return shape;

    Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(shape);
    fixer->Perform();
    return fixer->Shape();
}

} // namespace opendcad
