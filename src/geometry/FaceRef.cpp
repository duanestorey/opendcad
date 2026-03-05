#include "FaceRef.h"
#include "Workplane.h"
#include "Sketch.h"
#include "Error.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>

namespace opendcad {

FaceRef::FaceRef(ShapePtr parent, const TopoDS_Face& face)
    : parent_(std::move(parent)), face_(face) {}

gp_Pnt FaceRef::center() const {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face_, props);
    return props.CentreOfMass();
}

gp_Dir FaceRef::normal() const {
    BRepAdaptor_Surface surf(face_);
    double uMid = (surf.FirstUParameter() + surf.LastUParameter()) / 2.0;
    double vMid = (surf.FirstVParameter() + surf.LastVParameter()) / 2.0;

    gp_Pnt p;
    gp_Vec d1u, d1v;
    surf.D1(uMid, vMid, p, d1u, d1v);
    gp_Vec n = d1u.Crossed(d1v);

    if (n.Magnitude() < 1e-10)
        throw GeometryError("cannot compute face normal (degenerate)");

    n.Normalize();

    // Account for face orientation
    if (face_.Orientation() == TopAbs_REVERSED)
        n.Reverse();

    return gp_Dir(n);
}

gp_Pln FaceRef::plane() const {
    if (!isPlanar())
        throw GeometryError("face is not planar");
    return gp_Pln(center(), normal());
}

bool FaceRef::isPlanar() const {
    BRepAdaptor_Surface surf(face_);
    return surf.GetType() == GeomAbs_Plane;
}

double FaceRef::area() const {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face_, props);
    return props.Mass();
}

int FaceRef::edgeCount() const {
    int count = 0;
    for (TopExp_Explorer ex(face_, TopAbs_EDGE); ex.More(); ex.Next())
        ++count;
    return count;
}

WorkplanePtr FaceRef::workplane() const {
    return std::make_shared<Workplane>(parent_, center(), normal());
}

SketchPtr FaceRef::draw() const {
    auto wp = workplane();
    return std::make_shared<Sketch>(parent_, wp);
}

} // namespace opendcad
