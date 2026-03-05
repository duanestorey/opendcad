#include "EdgeSelector.h"
#include "Shape.h"
#include "Error.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_CurveType.hxx>

namespace opendcad {

EdgeSelector::EdgeSelector(ShapePtr parent)
    : parent_(std::move(parent))
{
    collectEdges();
}

EdgeSelector::EdgeSelector(ShapePtr parent, const std::vector<TopoDS_Edge>& edges)
    : parent_(std::move(parent)), edges_(edges) {}

void EdgeSelector::collectEdges() {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(parent_->getShape(), TopAbs_EDGE, edgeMap);
    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        edges_.push_back(TopoDS::Edge(edgeMap(i)));
    }
}

static gp_Dir edgeDirection(const TopoDS_Edge& edge) {
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_Line) {
        return curve.Line().Direction();
    }
    // For non-line edges, approximate direction from endpoints
    gp_Pnt p1 = curve.Value(curve.FirstParameter());
    gp_Pnt p2 = curve.Value(curve.LastParameter());
    gp_Vec v(p1, p2);
    if (v.Magnitude() < 1e-10)
        return gp::DZ(); // degenerate
    v.Normalize();
    return gp_Dir(v);
}

static bool isLinearEdge(const TopoDS_Edge& edge) {
    BRepAdaptor_Curve curve(edge);
    return curve.GetType() == GeomAbs_Line;
}

EdgeSelectorPtr EdgeSelector::parallelTo(const gp_Dir& dir) const {
    std::vector<TopoDS_Edge> filtered;
    for (const auto& e : edges_) {
        if (!isLinearEdge(e)) continue;
        gp_Dir d = edgeDirection(e);
        double dot = std::abs(d.X() * dir.X() + d.Y() * dir.Y() + d.Z() * dir.Z());
        if (dot > 0.999)
            filtered.push_back(e);
    }
    return std::make_shared<EdgeSelector>(parent_, filtered);
}

EdgeSelectorPtr EdgeSelector::perpendicularTo(const gp_Dir& dir) const {
    std::vector<TopoDS_Edge> filtered;
    for (const auto& e : edges_) {
        if (!isLinearEdge(e)) continue;
        gp_Dir d = edgeDirection(e);
        double dot = std::abs(d.X() * dir.X() + d.Y() * dir.Y() + d.Z() * dir.Z());
        if (dot < 0.001)
            filtered.push_back(e);
    }
    return std::make_shared<EdgeSelector>(parent_, filtered);
}

EdgeSelectorPtr EdgeSelector::vertical() const {
    return parallelTo(gp::DZ());
}

EdgeSelectorPtr EdgeSelector::horizontal() const {
    return perpendicularTo(gp::DZ());
}

ShapePtr EdgeSelector::fillet(double radius) const {
    if (edges_.empty())
        throw GeometryError("no edges selected for fillet");

    BRepFilletAPI_MakeFillet mk(parent_->getShape());
    for (const auto& e : edges_) {
        mk.Add(radius, e);
    }
    mk.Build();
    if (!mk.IsDone())
        throw GeometryError("edge fillet operation failed");

    return std::make_shared<Shape>(mk.Shape());
}

ShapePtr EdgeSelector::chamfer(double distance) const {
    if (edges_.empty())
        throw GeometryError("no edges selected for chamfer");

    BRepFilletAPI_MakeChamfer mk(parent_->getShape());
    for (const auto& e : edges_) {
        mk.Add(distance, e);
    }
    mk.Build();
    if (!mk.IsDone())
        throw GeometryError("edge chamfer operation failed");

    return std::make_shared<Shape>(mk.Shape());
}

int EdgeSelector::count() const {
    return static_cast<int>(edges_.size());
}

} // namespace opendcad
