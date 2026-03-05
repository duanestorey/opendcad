#pragma once

#include "Value.h"
#include <vector>
#include <TopoDS_Edge.hxx>
#include <gp_Dir.hxx>

namespace opendcad {

class EdgeSelector {
public:
    explicit EdgeSelector(ShapePtr parent);
    EdgeSelector(ShapePtr parent, const std::vector<TopoDS_Edge>& edges);

    // Filter selectors
    EdgeSelectorPtr parallelTo(const gp_Dir& dir) const;
    EdgeSelectorPtr perpendicularTo(const gp_Dir& dir) const;
    EdgeSelectorPtr vertical() const;
    EdgeSelectorPtr horizontal() const;

    // Operations on selected edges
    ShapePtr fillet(double radius) const;
    ShapePtr chamfer(double distance) const;

    int count() const;

    ShapePtr parent() const { return parent_; }
    const std::vector<TopoDS_Edge>& edges() const { return edges_; }

private:
    ShapePtr parent_;
    std::vector<TopoDS_Edge> edges_;

    void collectEdges();
};

} // namespace opendcad
