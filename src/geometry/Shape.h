#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class Shape;
typedef std::shared_ptr<Shape> ShapePtr;

class Shape {
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

    // --- Axis Shortcuts ---
    ShapePtr x( double x ) { return translate( x, 0, 0 ); }
    ShapePtr y( double y ) { return translate( 0, y, 0 ); }
    ShapePtr z( double z ) { return translate( 0, 0, z ); }

    bool isValid() const { return !mShape.IsNull(); }
private:
    TopoDS_Shape mShape;
};

} // namespace opendcad