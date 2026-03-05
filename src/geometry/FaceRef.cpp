#include "FaceRef.h"
#include "Workplane.h"
#include "Sketch.h"
#include "Shape.h"
#include "Error.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepAlgoAPI_Fuse.hxx>

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

ShapePtr FaceRef::hole(double diameter, double depth, double cx, double cy) const {
    return draw()->circle(diameter / 2.0, cx, cy)->cutBlind(depth);
}

ShapePtr FaceRef::throughHole(double diameter, double cx, double cy) const {
    return draw()->circle(diameter / 2.0, cx, cy)->cutThrough();
}

ShapePtr FaceRef::counterbore(double holeDia, double boreDia, double boreDepth,
                               double holeDepth, double cx, double cy) const {
    auto wp = workplane();
    gp_Pnt pos = wp->toWorld(cx, cy);
    gp_Dir norm = normal();
    // Direction into the body (negative normal)
    gp_Dir inward(-norm.X(), -norm.Y(), -norm.Z());
    gp_Ax2 ax(pos, inward);

    // Build bore cylinder
    BRepPrimAPI_MakeCylinder boreCyl(ax, boreDia / 2.0, boreDepth);
    boreCyl.Build();
    if (!boreCyl.IsDone())
        throw GeometryError("counterbore: bore cylinder creation failed");

    // Build hole cylinder (deeper, narrower)
    BRepPrimAPI_MakeCylinder holeCyl(ax, holeDia / 2.0, holeDepth);
    holeCyl.Build();
    if (!holeCyl.IsDone())
        throw GeometryError("counterbore: hole cylinder creation failed");

    // Fuse bore + hole into one tool
    auto boreTool = std::make_shared<Shape>(boreCyl.Shape());
    auto holeTool = std::make_shared<Shape>(holeCyl.Shape());
    auto combinedTool = boreTool->fuse(holeTool);

    return parent_->cut(combinedTool);
}

ShapePtr FaceRef::countersink(double holeDia, double sinkDia, double sinkAngle,
                               double holeDepth, double cx, double cy) const {
    auto wp = workplane();
    gp_Pnt pos = wp->toWorld(cx, cy);
    gp_Dir norm = normal();
    gp_Dir inward(-norm.X(), -norm.Y(), -norm.Z());
    gp_Ax2 ax(pos, inward);

    // Compute cone depth from angle and diameters
    double halfAngle = sinkAngle * M_PI / 360.0; // half the included angle
    double coneDepth = (sinkDia / 2.0 - holeDia / 2.0) / std::tan(halfAngle);

    // Build cone: top radius = sinkDia/2, bottom radius = holeDia/2
    BRepPrimAPI_MakeCone cone(ax, sinkDia / 2.0, holeDia / 2.0, coneDepth);
    cone.Build();
    if (!cone.IsDone())
        throw GeometryError("countersink: cone creation failed");

    // Build hole cylinder
    // Position the hole at the bottom of the cone
    gp_Pnt holeStart(pos.X() + inward.X() * coneDepth,
                     pos.Y() + inward.Y() * coneDepth,
                     pos.Z() + inward.Z() * coneDepth);
    gp_Ax2 holeAx(holeStart, inward);
    BRepPrimAPI_MakeCylinder holeCyl(holeAx, holeDia / 2.0, holeDepth - coneDepth);
    holeCyl.Build();
    if (!holeCyl.IsDone())
        throw GeometryError("countersink: hole cylinder creation failed");

    auto coneTool = std::make_shared<Shape>(cone.Shape());
    auto holeTool = std::make_shared<Shape>(holeCyl.Shape());
    auto combinedTool = coneTool->fuse(holeTool);

    return parent_->cut(combinedTool);
}

} // namespace opendcad
