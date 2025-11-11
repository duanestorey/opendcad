#pragma once

#include <memory>
#include <TopoDS_Shape.hxx>


namespace opendcad {

class Shape;
typedef std::shared_ptr<Shape> ShapePtr;

class Shape {
public:
    Shape( const TopoDS_Shape &shape );
    Shape();

    static ShapePtr createBox( double width, double depth, double height );
    static ShapePtr createCylinder( double radius, double height );

    TopoDS_Shape getShape() const { return mShape; }
    ShapePtr fuse( const ShapePtr &part );
    ShapePtr cut( const ShapePtr &part );
    
    void translate( double x, double y, double z );
    void x( double x ) { translate( x, 0, 0 ); }
    void y( double y ) { translate( 0, y, 0 ); }
    void z( double z ) { translate( 0, 0, z ); }

    bool isValid() const { return !mShape.IsNull(); }
private: 
    TopoDS_Shape mShape;
};


}