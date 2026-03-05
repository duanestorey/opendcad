#pragma once

#include "Value.h"
#include <vector>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>

namespace opendcad {

class FaceSelector {
public:
    explicit FaceSelector(ShapePtr parent);
    FaceSelector(ShapePtr parent, const std::vector<TopoDS_Face>& faces);

    // Directional selectors
    FaceRefPtr top() const;
    FaceRefPtr bottom() const;
    FaceRefPtr front() const;
    FaceRefPtr back() const;
    FaceRefPtr left() const;
    FaceRefPtr right() const;

    // Filter selectors
    FaceSelectorPtr planar() const;
    FaceSelectorPtr cylindrical() const;

    // Size selectors
    FaceRefPtr largest() const;
    FaceRefPtr smallest() const;

    // Distance selectors
    FaceRefPtr nearestTo(const gp_Pnt& point) const;
    FaceRefPtr farthestFrom(const gp_Pnt& point) const;

    // Area filter selectors
    FaceSelectorPtr areaGreaterThan(double minArea) const;
    FaceSelectorPtr areaLessThan(double maxArea) const;

    // Index
    FaceRefPtr byIndex(int index) const;

    int count() const;

    ShapePtr parent() const { return parent_; }
    const std::vector<TopoDS_Face>& faces() const { return faces_; }

private:
    ShapePtr parent_;
    std::vector<TopoDS_Face> faces_;

    FaceRefPtr bestByNormal(const gp_Dir& dir) const;
    FaceRefPtr bestByArea(bool wantLargest) const;
    void collectFaces();
};

} // namespace opendcad
