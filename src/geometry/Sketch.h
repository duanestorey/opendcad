#pragma once

#include "Value.h"
#include <memory>
#include <vector>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
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

    // Freeform wire building
    SketchPtr moveTo(double x, double y);
    SketchPtr lineTo(double x, double y);
    SketchPtr close();

    // Operations that produce geometry
    ShapePtr extrude(double height) const;
    ShapePtr cutBlind(double depth) const;
    ShapePtr cutThrough() const;

    WorkplanePtr workplane() const { return wp_; }
    ShapePtr parent() const { return parent_; }

private:
    ShapePtr parent_;
    WorkplanePtr wp_;
    std::vector<TopoDS_Wire> wires_;

    // Freeform wire state
    std::vector<gp_Pnt> wirePoints_;
    bool wireStarted_ = false;

    TopoDS_Face makeFace() const;
    gp_Pnt toWorld3D(double u, double v) const;
    double computeBoundingSize() const;
};

} // namespace opendcad
