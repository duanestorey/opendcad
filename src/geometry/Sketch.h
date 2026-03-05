#pragma once

#include "Value.h"
#include <memory>
#include <vector>
#include <utility>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>

namespace opendcad {

class Workplane;

class Sketch : public std::enable_shared_from_this<Sketch> {
public:
    Sketch(ShapePtr parent, WorkplanePtr wp);

    // Profile builders (return self for chaining)
    SketchPtr circle(double radius, double cx = 0, double cy = 0);
    SketchPtr rect(double width, double height, double cx = 0, double cy = 0);
    SketchPtr polygon(const std::vector<std::pair<double,double>>& pts);
    SketchPtr slot(double length, double width, double cx = 0, double cy = 0);

    // Freeform wire building
    SketchPtr moveTo(double x, double y);
    SketchPtr lineTo(double x, double y);
    SketchPtr arcTo(double x, double y, double bulge);
    SketchPtr splineTo(const std::vector<std::pair<double,double>>& throughPts, double ex, double ey);
    SketchPtr close();

    // Operations that produce geometry
    ShapePtr extrude(double height) const;
    ShapePtr cutBlind(double depth) const;
    ShapePtr cutThrough() const;
    ShapePtr revolve(double angleDeg = 360.0) const;

    WorkplanePtr workplane() const { return wp_; }
    ShapePtr parent() const { return parent_; }

private:
    ShapePtr parent_;
    WorkplanePtr wp_;
    std::vector<TopoDS_Wire> wires_;

    // Freeform wire state (edge-based for mixed line/arc/spline wires)
    std::vector<TopoDS_Edge> wireEdges_;
    gp_Pnt wireStart_;
    gp_Pnt wireCurrent_;
    bool wireStarted_ = false;

    TopoDS_Face makeFace() const;
    gp_Pnt toWorld3D(double u, double v) const;
    double computeBoundingSize() const;
};

} // namespace opendcad
