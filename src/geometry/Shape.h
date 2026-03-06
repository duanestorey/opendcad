#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <string>
#include <TopoDS_Shape.hxx>
#include "Color.h"

namespace opendcad {

class Shape;
typedef std::shared_ptr<Shape> ShapePtr;

class FaceRef;
using FaceRefPtr = std::shared_ptr<FaceRef>;

class FaceSelector;
using FaceSelectorPtr = std::shared_ptr<FaceSelector>;

class EdgeSelector;
using EdgeSelectorPtr = std::shared_ptr<EdgeSelector>;

class Shape : public std::enable_shared_from_this<Shape> {
public:
    Shape( const TopoDS_Shape &shape );
    Shape();

    // --- 3D Primitive Factories ---
    static ShapePtr createBox( double width, double depth, double height );
    static ShapePtr createCylinder( double radius, double height );
    static ShapePtr createTorus( double r1, double r2, double angle = 0 );
    static ShapePtr createBin( double width, double depth, double height, double thickness );
    static ShapePtr createSphere( double radius );
    static ShapePtr createCone( double r1, double r2, double height );
    static ShapePtr createWedge( double dx, double dy, double dz, double ltx );

    // --- 2D Primitive Factories ---
    static ShapePtr createCircle( double radius );
    static ShapePtr createRectangle( double width, double height );
    static ShapePtr createPolygon( const std::vector<std::pair<double,double>>& pts );

    // --- Multi-Shape Factory ---
    static ShapePtr createLoft( const std::vector<ShapePtr>& profiles, bool solid = true, bool ruled = false );

    // --- External Import ---
    static ShapePtr importSTEP(const std::string& filePath);
    static ShapePtr importSTL(const std::string& filePath);

    TopoDS_Shape getShape() const { return mShape; }

    // --- Boolean Operations ---
    ShapePtr fuse( const ShapePtr &part );
    ShapePtr cut( const ShapePtr &part );
    ShapePtr intersect( const ShapePtr &part ) const;

    // --- Edge Operations ---
    ShapePtr fillet( double amount ) const;
    ShapePtr chamfer( double distance ) const;

    // --- Transforms ---
    ShapePtr flip() const;
    ShapePtr translate( double x, double y = 0, double z = 0 );
    ShapePtr rotate( double xAngle, double yAngle, double zAngle ) const;
    ShapePtr scale( double factor ) const;
    ShapePtr scale( double fx, double fy, double fz ) const;
    ShapePtr mirror( double nx, double ny, double nz ) const;
    ShapePtr placeCorners( ShapePtr shape, double xOffset, double yOffset );

    // --- Extrusion / Sweep ---
    ShapePtr linearExtrude( double height ) const;
    ShapePtr rotateExtrude( double angleDeg = 360.0 ) const;
    ShapePtr sweep( const ShapePtr& pathShape ) const;

    // --- Shell ---
    ShapePtr shell( double thickness ) const;

    // --- Feature Patterns ---
    ShapePtr linearPattern(double dx, double dy, double dz, int count) const;
    ShapePtr circularPattern(double ax, double ay, double az, int count, double angleDeg = 360.0) const;
    ShapePtr mirrorFeature(double nx, double ny, double nz) const;

    // --- Advanced Operations ---
    ShapePtr draft(double angleDeg, double nx, double ny, double nz) const;
    ShapePtr splitAt(double px, double py, double pz, double nx, double ny, double nz) const;

    // --- Axis Shortcuts ---
    ShapePtr x( double x ) { return translate( x, 0, 0 ); }
    ShapePtr y( double y ) { return translate( 0, y, 0 ); }
    ShapePtr z( double z ) { return translate( 0, 0, z ); }

    // --- Face/Edge Selection ---
    FaceSelectorPtr faces() const;
    FaceRefPtr face(const std::string& selector) const;
    FaceRefPtr topFace() const;
    FaceRefPtr bottomFace() const;
    EdgeSelectorPtr edges() const;

    bool isValid() const { return !mShape.IsNull(); }

    // --- Color/Material metadata ---
    void setColor(ColorPtr c) { color_ = std::move(c); }
    void setMaterial(MaterialPtr m) { material_ = std::move(m); }
    ColorPtr color() const { return color_; }
    MaterialPtr material() const { return material_; }

    // --- Tags ---
    void addTag(const std::string& tag) { tags_.push_back(tag); }
    bool hasTag(const std::string& tag) const {
        for (const auto& t : tags_) if (t == tag) return true;
        return false;
    }
    const std::vector<std::string>& tags() const { return tags_; }

    // Copy color/material/tags from this shape onto another
    void copyMetaTo(ShapePtr target) const {
        if (color_) target->setColor(color_);
        if (material_) target->setMaterial(material_);
        for (const auto& t : tags_) target->addTag(t);
    }

private:
    TopoDS_Shape mShape;
    ColorPtr color_;
    MaterialPtr material_;
    std::vector<std::string> tags_;
};

} // namespace opendcad