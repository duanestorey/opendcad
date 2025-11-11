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
    static ShapePtr createTorus( double r1, double r2, double angle = 0 );
    static ShapePtr createBin( double width, double depth, double height, double thickness );

    TopoDS_Shape getShape() const { return mShape; }
    ShapePtr fuse( const ShapePtr &part );
    ShapePtr cut( const ShapePtr &part );

    ShapePtr fillet( double amount ) const;
    ShapePtr flip() const;
    
    ShapePtr translate( double x, double y = 0, double z = 0 );
    ShapePtr rotate( double xAngle, double yAngle, double zAngle ) const;
    ShapePtr placeCorners( ShapePtr shape, double xOffset, double yOffset );

    ShapePtr x( double x ) { return translate( x, 0, 0 ); }
    ShapePtr y( double y ) { return translate( 0, y, 0 ); }
    ShapePtr z( double z ) { return translate( 0, 0, z ); }

    bool isValid() const { return !mShape.IsNull(); }
private: 
    TopoDS_Shape mShape;
};

class LetValue {
public:
	LetValue() : mType( NOTSET ), mDouble( 0 ), mBool( false ) {}

	enum {
        NOTSET,
		SHAPE,
		DOUBLE,
		STRING,
		BOOLEAN
	} mType;

	ShapePtr mShape;
	double mDouble;
	std::string mString;
	bool mBool;
} ;


}