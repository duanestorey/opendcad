#pragma once

#include "Value.h"
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

namespace opendcad {

class FaceRef {
public:
    FaceRef(ShapePtr parent, const TopoDS_Face& face);

    gp_Pnt center() const;
    gp_Dir normal() const;
    gp_Pln plane() const;
    bool isPlanar() const;
    double area() const;
    int edgeCount() const;

    WorkplanePtr workplane() const;
    SketchPtr draw() const;

    const TopoDS_Face& face() const { return face_; }
    ShapePtr parent() const { return parent_; }

private:
    ShapePtr parent_;
    TopoDS_Face face_;
};

} // namespace opendcad
