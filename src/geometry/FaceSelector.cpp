#include "FaceSelector.h"
#include "FaceRef.h"
#include "Shape.h"
#include "Error.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <GeomAbs_SurfaceType.hxx>

namespace opendcad {

FaceSelector::FaceSelector(ShapePtr parent)
    : parent_(std::move(parent))
{
    collectFaces();
}

FaceSelector::FaceSelector(ShapePtr parent, const std::vector<TopoDS_Face>& faces)
    : parent_(std::move(parent)), faces_(faces) {}

void FaceSelector::collectFaces() {
    for (TopExp_Explorer ex(parent_->getShape(), TopAbs_FACE); ex.More(); ex.Next()) {
        faces_.push_back(TopoDS::Face(ex.Current()));
    }
}

static gp_Dir faceNormal(const TopoDS_Face& face) {
    BRepAdaptor_Surface surf(face);
    double uMid = (surf.FirstUParameter() + surf.LastUParameter()) / 2.0;
    double vMid = (surf.FirstVParameter() + surf.LastVParameter()) / 2.0;

    gp_Pnt p;
    gp_Vec d1u, d1v;
    surf.D1(uMid, vMid, p, d1u, d1v);
    gp_Vec n = d1u.Crossed(d1v);

    if (n.Magnitude() < 1e-10)
        return gp::DZ();

    n.Normalize();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();

    return gp_Dir(n);
}

FaceRefPtr FaceSelector::bestByNormal(const gp_Dir& dir) const {
    if (faces_.empty())
        throw GeometryError("no faces to select from");

    double bestDot = -1e99;
    int bestIdx = 0;

    for (size_t i = 0; i < faces_.size(); ++i) {
        gp_Dir n = faceNormal(faces_[i]);
        double dot = n.X() * dir.X() + n.Y() * dir.Y() + n.Z() * dir.Z();
        if (dot > bestDot) {
            bestDot = dot;
            bestIdx = static_cast<int>(i);
        }
    }

    return std::make_shared<FaceRef>(parent_, faces_[bestIdx]);
}

FaceRefPtr FaceSelector::bestByArea(bool wantLargest) const {
    if (faces_.empty())
        throw GeometryError("no faces to select from");

    double bestArea = wantLargest ? -1.0 : 1e99;
    int bestIdx = 0;

    for (size_t i = 0; i < faces_.size(); ++i) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(faces_[i], props);
        double a = props.Mass();

        if ((wantLargest && a > bestArea) || (!wantLargest && a < bestArea)) {
            bestArea = a;
            bestIdx = static_cast<int>(i);
        }
    }

    return std::make_shared<FaceRef>(parent_, faces_[bestIdx]);
}

FaceRefPtr FaceSelector::top() const { return bestByNormal(gp::DZ()); }
FaceRefPtr FaceSelector::bottom() const { return bestByNormal(gp_Dir(0, 0, -1)); }
FaceRefPtr FaceSelector::front() const { return bestByNormal(gp_Dir(0, -1, 0)); }
FaceRefPtr FaceSelector::back() const { return bestByNormal(gp_Dir(0, 1, 0)); }
FaceRefPtr FaceSelector::left() const { return bestByNormal(gp_Dir(-1, 0, 0)); }
FaceRefPtr FaceSelector::right() const { return bestByNormal(gp_Dir(1, 0, 0)); }

FaceRefPtr FaceSelector::largest() const { return bestByArea(true); }
FaceRefPtr FaceSelector::smallest() const { return bestByArea(false); }

FaceSelectorPtr FaceSelector::planar() const {
    std::vector<TopoDS_Face> filtered;
    for (const auto& f : faces_) {
        BRepAdaptor_Surface surf(f);
        if (surf.GetType() == GeomAbs_Plane)
            filtered.push_back(f);
    }
    return std::make_shared<FaceSelector>(parent_, filtered);
}

FaceSelectorPtr FaceSelector::cylindrical() const {
    std::vector<TopoDS_Face> filtered;
    for (const auto& f : faces_) {
        BRepAdaptor_Surface surf(f);
        if (surf.GetType() == GeomAbs_Cylinder)
            filtered.push_back(f);
    }
    return std::make_shared<FaceSelector>(parent_, filtered);
}

FaceRefPtr FaceSelector::nearestTo(const gp_Pnt& point) const {
    if (faces_.empty())
        throw GeometryError("no faces to select from");

    double bestDist = 1e99;
    int bestIdx = 0;
    for (size_t i = 0; i < faces_.size(); ++i) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(faces_[i], props);
        double dist = props.CentreOfMass().Distance(point);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = static_cast<int>(i);
        }
    }
    return std::make_shared<FaceRef>(parent_, faces_[bestIdx]);
}

FaceRefPtr FaceSelector::farthestFrom(const gp_Pnt& point) const {
    if (faces_.empty())
        throw GeometryError("no faces to select from");

    double bestDist = -1.0;
    int bestIdx = 0;
    for (size_t i = 0; i < faces_.size(); ++i) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(faces_[i], props);
        double dist = props.CentreOfMass().Distance(point);
        if (dist > bestDist) {
            bestDist = dist;
            bestIdx = static_cast<int>(i);
        }
    }
    return std::make_shared<FaceRef>(parent_, faces_[bestIdx]);
}

FaceSelectorPtr FaceSelector::areaGreaterThan(double minArea) const {
    std::vector<TopoDS_Face> filtered;
    for (const auto& f : faces_) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(f, props);
        if (props.Mass() > minArea)
            filtered.push_back(f);
    }
    return std::make_shared<FaceSelector>(parent_, filtered);
}

FaceSelectorPtr FaceSelector::areaLessThan(double maxArea) const {
    std::vector<TopoDS_Face> filtered;
    for (const auto& f : faces_) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(f, props);
        if (props.Mass() < maxArea)
            filtered.push_back(f);
    }
    return std::make_shared<FaceSelector>(parent_, filtered);
}

FaceRefPtr FaceSelector::byIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(faces_.size()))
        throw GeometryError("face index " + std::to_string(index) +
                            " out of range [0, " + std::to_string(faces_.size()) + ")");
    return std::make_shared<FaceRef>(parent_, faces_[index]);
}

int FaceSelector::count() const {
    return static_cast<int>(faces_.size());
}

} // namespace opendcad
