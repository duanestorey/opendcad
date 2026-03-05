#include "Sketch.h"
#include "Workplane.h"
#include "Shape.h"
#include "Error.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <TopoDS.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAPI_PointsToBSpline.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColgp_Array1OfPnt.hxx>

namespace opendcad {

Sketch::Sketch(ShapePtr parent, WorkplanePtr wp)
    : parent_(std::move(parent)), wp_(std::move(wp)) {}

gp_Pnt Sketch::toWorld3D(double u, double v) const {
    return wp_->toWorld(u, v);
}

SketchPtr Sketch::circle(double radius, double cx, double cy) {
    gp_Pnt center3d = toWorld3D(cx, cy);
    gp_Dir norm = wp_->normal();
    gp_Ax2 ax(center3d, norm);
    gp_Circ circ(ax, radius);

    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circ);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);
    wires_.push_back(wire);
    return shared_from_this();
}

SketchPtr Sketch::rect(double width, double height, double cx, double cy) {
    double hw = width / 2.0;
    double hh = height / 2.0;

    gp_Pnt p1 = toWorld3D(cx - hw, cy - hh);
    gp_Pnt p2 = toWorld3D(cx + hw, cy - hh);
    gp_Pnt p3 = toWorld3D(cx + hw, cy + hh);
    gp_Pnt p4 = toWorld3D(cx - hw, cy + hh);

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(p1);
    poly.Add(p2);
    poly.Add(p3);
    poly.Add(p4);
    poly.Close();

    wires_.push_back(poly.Wire());
    return shared_from_this();
}

SketchPtr Sketch::polygon(const std::vector<std::pair<double,double>>& pts) {
    if (pts.size() < 3)
        throw GeometryError("sketch polygon requires at least 3 points");

    BRepBuilderAPI_MakePolygon poly;
    for (const auto& [u, v] : pts) {
        poly.Add(toWorld3D(u, v));
    }
    poly.Close();

    if (!poly.IsDone())
        throw GeometryError("sketch polygon wire creation failed");

    wires_.push_back(poly.Wire());
    return shared_from_this();
}

SketchPtr Sketch::slot(double length, double width, double cx, double cy) {
    double hw = width / 2.0;
    double hl = length / 2.0;

    // Stadium shape: rect body + semicircular endcaps
    // Bottom-left to bottom-right (bottom line)
    gp_Pnt p1 = toWorld3D(cx - hl + hw, cy - hw);
    gp_Pnt p2 = toWorld3D(cx + hl - hw, cy - hw);
    // Top-right to top-left (top line)
    gp_Pnt p3 = toWorld3D(cx + hl - hw, cy + hw);
    gp_Pnt p4 = toWorld3D(cx - hl + hw, cy + hw);

    // Right semicircle center and midpoint
    gp_Pnt rightCenter = toWorld3D(cx + hl - hw, cy);
    gp_Pnt rightMid = toWorld3D(cx + hl, cy);

    // Left semicircle center and midpoint
    gp_Pnt leftCenter = toWorld3D(cx - hl + hw, cy);
    gp_Pnt leftMid = toWorld3D(cx - hl, cy);

    BRepBuilderAPI_MakeWire wireMaker;

    // Bottom line
    wireMaker.Add(BRepBuilderAPI_MakeEdge(p1, p2));

    // Right semicircle (p2 -> p3 through rightMid)
    Handle(Geom_TrimmedCurve) rightArc = GC_MakeArcOfCircle(p2, rightMid, p3);
    wireMaker.Add(BRepBuilderAPI_MakeEdge(rightArc));

    // Top line
    wireMaker.Add(BRepBuilderAPI_MakeEdge(p3, p4));

    // Left semicircle (p4 -> p1 through leftMid)
    Handle(Geom_TrimmedCurve) leftArc = GC_MakeArcOfCircle(p4, leftMid, p1);
    wireMaker.Add(BRepBuilderAPI_MakeEdge(leftArc));

    if (!wireMaker.IsDone())
        throw GeometryError("slot wire creation failed");

    wires_.push_back(wireMaker.Wire());
    return shared_from_this();
}

SketchPtr Sketch::moveTo(double x, double y) {
    // Finish any in-progress wire
    if (wireStarted_ && !wireEdges_.empty()) {
        close();
    }
    wireEdges_.clear();
    wireStart_ = toWorld3D(x, y);
    wireCurrent_ = wireStart_;
    wireStarted_ = true;
    return shared_from_this();
}

SketchPtr Sketch::lineTo(double x, double y) {
    if (!wireStarted_)
        throw GeometryError("lineTo() requires a preceding moveTo()");
    gp_Pnt endPt = toWorld3D(x, y);
    wireEdges_.push_back(BRepBuilderAPI_MakeEdge(wireCurrent_, endPt));
    wireCurrent_ = endPt;
    return shared_from_this();
}

SketchPtr Sketch::arcTo(double x, double y, double bulge) {
    if (!wireStarted_)
        throw GeometryError("arcTo() requires a preceding moveTo()");

    gp_Pnt endPt = toWorld3D(x, y);

    // If bulge is near zero, fall back to straight line
    if (std::abs(bulge) < 1e-6) {
        wireEdges_.push_back(BRepBuilderAPI_MakeEdge(wireCurrent_, endPt));
        wireCurrent_ = endPt;
        return shared_from_this();
    }

    // Compute through-point for 3-point arc
    // Chord midpoint
    gp_Pnt mid((wireCurrent_.X() + endPt.X()) / 2.0,
               (wireCurrent_.Y() + endPt.Y()) / 2.0,
               (wireCurrent_.Z() + endPt.Z()) / 2.0);

    // Chord vector and perpendicular in workplane
    gp_Vec chord(wireCurrent_, endPt);
    gp_Vec normal(wp_->normal());
    gp_Vec perp = chord.Crossed(normal);
    if (perp.Magnitude() < 1e-10)
        throw GeometryError("arcTo: degenerate arc (points on normal axis)");
    perp.Normalize();

    // Through-point at sagitta distance from midpoint
    gp_Pnt throughPt(mid.X() + perp.X() * bulge,
                     mid.Y() + perp.Y() * bulge,
                     mid.Z() + perp.Z() * bulge);

    Handle(Geom_TrimmedCurve) arc = GC_MakeArcOfCircle(wireCurrent_, throughPt, endPt);
    wireEdges_.push_back(BRepBuilderAPI_MakeEdge(arc));
    wireCurrent_ = endPt;
    return shared_from_this();
}

SketchPtr Sketch::splineTo(const std::vector<std::pair<double,double>>& throughPts, double ex, double ey) {
    if (!wireStarted_)
        throw GeometryError("splineTo() requires a preceding moveTo()");

    // Collect all points: current + through points + end
    int nPts = static_cast<int>(throughPts.size()) + 2;
    if (nPts < 3)
        throw GeometryError("splineTo() requires at least 1 through-point");

    TColgp_Array1OfPnt points(1, nPts);
    points.SetValue(1, wireCurrent_);
    for (size_t i = 0; i < throughPts.size(); ++i) {
        points.SetValue(static_cast<int>(i) + 2, toWorld3D(throughPts[i].first, throughPts[i].second));
    }
    gp_Pnt endPt = toWorld3D(ex, ey);
    points.SetValue(nPts, endPt);

    GeomAPI_PointsToBSpline bspline(points);
    if (!bspline.IsDone())
        throw GeometryError("splineTo: B-spline interpolation failed");

    wireEdges_.push_back(BRepBuilderAPI_MakeEdge(bspline.Curve()));
    wireCurrent_ = endPt;
    return shared_from_this();
}

SketchPtr Sketch::close() {
    if (!wireStarted_ || wireEdges_.empty())
        throw GeometryError("close() requires at least one line/arc segment after moveTo()");

    // Add closing edge if current point != start point
    if (wireCurrent_.Distance(wireStart_) > 1e-6) {
        wireEdges_.push_back(BRepBuilderAPI_MakeEdge(wireCurrent_, wireStart_));
    }

    BRepBuilderAPI_MakeWire wireMaker;
    for (const auto& edge : wireEdges_) {
        wireMaker.Add(edge);
    }

    if (!wireMaker.IsDone())
        throw GeometryError("freeform wire creation failed");

    wires_.push_back(wireMaker.Wire());
    wireEdges_.clear();
    wireStarted_ = false;
    return shared_from_this();
}

TopoDS_Face Sketch::makeFace() const {
    if (wires_.empty())
        throw GeometryError("sketch has no profiles to build a face from");

    // First wire is the outer boundary
    gp_Pln pln(wp_->origin(), wp_->normal());
    BRepBuilderAPI_MakeFace faceBuilder(pln, wires_[0]);

    // Additional wires are inner boundaries (holes)
    for (size_t i = 1; i < wires_.size(); ++i) {
        faceBuilder.Add(wires_[i]);
    }

    if (!faceBuilder.IsDone())
        throw GeometryError("sketch face creation failed");

    return faceBuilder.Face();
}

ShapePtr Sketch::extrude(double height) const {
    TopoDS_Face face = makeFace();
    gp_Dir norm = wp_->normal();
    gp_Vec direction(norm.X() * height, norm.Y() * height, norm.Z() * height);

    BRepPrimAPI_MakePrism prism(face, direction);
    prism.Build();
    if (!prism.IsDone())
        throw GeometryError("sketch extrude failed");

    auto extruded = std::make_shared<Shape>(prism.Shape());
    return parent_->fuse(extruded);
}

ShapePtr Sketch::cutBlind(double depth) const {
    TopoDS_Face face = makeFace();
    gp_Dir norm = wp_->normal();
    // Cut in the opposite direction of the normal (into the body)
    gp_Vec direction(-norm.X() * depth, -norm.Y() * depth, -norm.Z() * depth);

    BRepPrimAPI_MakePrism prism(face, direction);
    prism.Build();
    if (!prism.IsDone())
        throw GeometryError("sketch cutBlind failed");

    auto tool = std::make_shared<Shape>(prism.Shape());
    return parent_->cut(tool);
}

double Sketch::computeBoundingSize() const {
    Bnd_Box bbox;
    BRepBndLib::Add(parent_->getShape(), bbox);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    double dx = xmax - xmin;
    double dy = ymax - ymin;
    double dz = zmax - zmin;
    double maxDim = dx;
    if (dy > maxDim) maxDim = dy;
    if (dz > maxDim) maxDim = dz;
    return maxDim * 2.0;
}

ShapePtr Sketch::cutThrough() const {
    TopoDS_Face face = makeFace();
    double size = computeBoundingSize();
    gp_Dir norm = wp_->normal();

    // Cut in both directions to ensure through-all
    gp_Vec dir1(-norm.X() * size, -norm.Y() * size, -norm.Z() * size);
    gp_Vec dir2(norm.X() * size, norm.Y() * size, norm.Z() * size);

    BRepPrimAPI_MakePrism prism1(face, dir1);
    prism1.Build();
    if (!prism1.IsDone())
        throw GeometryError("sketch cutThrough failed (dir1)");

    BRepPrimAPI_MakePrism prism2(face, dir2);
    prism2.Build();
    if (!prism2.IsDone())
        throw GeometryError("sketch cutThrough failed (dir2)");

    auto tool1 = std::make_shared<Shape>(prism1.Shape());
    auto tool2 = std::make_shared<Shape>(prism2.Shape());
    auto combinedTool = tool1->fuse(tool2);
    return parent_->cut(combinedTool);
}

ShapePtr Sketch::revolve(double angleDeg) const {
    TopoDS_Face face = makeFace();
    gp_Ax1 axis(wp_->origin(), wp_->axes().XDirection());

    if (std::abs(angleDeg - 360.0) < 1e-6) {
        BRepPrimAPI_MakeRevol revol(face, axis);
        revol.Build();
        if (!revol.IsDone())
            throw GeometryError("sketch revolve failed");
        auto revolved = std::make_shared<Shape>(revol.Shape());
        return parent_->fuse(revolved);
    }

    double angleRad = angleDeg * M_PI / 180.0;
    BRepPrimAPI_MakeRevol revol(face, axis, angleRad);
    revol.Build();
    if (!revol.IsDone())
        throw GeometryError("sketch revolve failed");

    auto revolved = std::make_shared<Shape>(revol.Shape());
    return parent_->fuse(revolved);
}

} // namespace opendcad
