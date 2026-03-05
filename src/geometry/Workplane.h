#pragma once

#include "Value.h"
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>
#include <memory>

namespace opendcad {

class Workplane : public std::enable_shared_from_this<Workplane> {
public:
    Workplane(ShapePtr parent, const gp_Pnt& origin, const gp_Dir& normal);

    gp_Pnt origin() const { return origin_; }
    gp_Dir normal() const { return normal_; }
    gp_Ax2 axes() const { return axes_; }
    gp_Pln plane() const { return gp_Pln(origin_, normal_); }

    // Transform 2D local coords to 3D world coords
    gp_Pnt toWorld(double u, double v) const;

    WorkplanePtr offset(double distance) const;
    SketchPtr draw();

    ShapePtr parent() const { return parent_; }

private:
    ShapePtr parent_;
    gp_Pnt origin_;
    gp_Dir normal_;
    gp_Ax2 axes_;
};

} // namespace opendcad
