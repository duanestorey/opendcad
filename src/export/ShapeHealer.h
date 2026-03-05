#pragma once

#include <TopoDS_Shape.hxx>
#include <ShapeFix_Shape.hxx>

namespace opendcad {

inline TopoDS_Shape healShape(const TopoDS_Shape& shape) {
    Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(shape);
    fixer->Perform();
    return fixer->Shape();
}

} // namespace opendcad
