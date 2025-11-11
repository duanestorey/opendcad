#include "Shape.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepBuilderAPI_Transform.hxx>

#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include "output.h"

using namespace opendcad;

ShapePtr 
Shape::createBox( double width, double depth, double height ) {
    gp_Pnt point( -width/2, -depth/2, 0 );
    return ShapePtr( new Shape( BRepPrimAPI_MakeBox( point, width, depth, height ).Shape() ) );
}

ShapePtr 
Shape::createBin( double width, double depth, double height, double thickness ) {
    gp_Pnt point( -width/2, -depth/2, 0 );
    gp_Pnt point2( -( width - 2*thickness )/2, -(depth - 2*thickness)/2, thickness );
 
    ShapePtr box = ShapePtr( new Shape( BRepPrimAPI_MakeBox( point, width, depth, height ).Shape() ) );
    ShapePtr box2 = ShapePtr( new Shape( BRepPrimAPI_MakeBox( point2, width - 2*thickness, depth - 2*thickness, height - thickness ).Shape() ) );

    return box->cut( box2 );
}

ShapePtr 
Shape::createCylinder( double radius, double height ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    return ShapePtr( new Shape( BRepPrimAPI_MakeCylinder(ax, radius, height ).Shape() ) );
}

ShapePtr 
Shape::createTorus( double r1, double r2, double angle ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    return ShapePtr( new Shape( BRepPrimAPI_MakeTorus( ax, r1, r2 ).Shape() ) );
}

ShapePtr 
Shape::placeCorners( ShapePtr shape, double xOffset, double yOffset ) {
    ShapePtr newShape = fuse( shape->translate( xOffset, yOffset ) );
    newShape = newShape->fuse( shape->translate( xOffset, -yOffset ) );
    newShape = newShape->fuse( shape->translate( -xOffset, yOffset ) );
    newShape = newShape->fuse( shape->translate( -xOffset, -yOffset ) );

    return newShape;
}

Shape::Shape( const TopoDS_Shape &shape ) : mShape( shape ) {
    
}

Shape::Shape() {}

ShapePtr 
Shape::flip() const {


}

ShapePtr 
Shape::fillet( double amount ) const {
    BRepFilletAPI_MakeFillet mk( mShape );

    for (TopExp_Explorer ex(mShape, TopAbs_EDGE); ex.More(); ex.Next()) {
        mk.Add(amount, TopoDS::Edge(ex.Current()));
    }

    mk.Build();

    if ( !mk.IsDone() ) {
        std::cout << termcolor::red << "Unable to fillet edges" << "\n";
    }

    return ShapePtr( new Shape( mk.Shape() ) );
}

ShapePtr 
Shape::translate( double x, double y, double z ) {
    gp_Trsf tr;
    tr.SetTranslation( gp_Vec(x, y, z) ); 
    TopLoc_Location loc(tr);
    return ShapePtr( new Shape( mShape.Moved( loc ) ) );
}

ShapePtr 
Shape::rotate( double xAngle, double yAngle, double zAngle ) const {
    double toRad = M_PI / 180.0;

    gp_Trsf rotX, rotY, rotZ;

    rotX.SetRotation(gp::OX(), xAngle * toRad);
    rotY.SetRotation(gp::OY(), yAngle * toRad);
    rotZ.SetRotation(gp::OZ(), zAngle * toRad);

    gp_Trsf trsf = rotZ * rotY * rotX;

    return ShapePtr( new Shape( BRepBuilderAPI_Transform(mShape, trsf, true).Shape() ) );
}

ShapePtr
Shape::fuse( const ShapePtr &part  ) {
    BRepAlgoAPI_Fuse fuse( mShape, part->getShape() );

    fuse.SetRunParallel(Standard_True);       // ✅ multi-threaded if possible
    fuse.SetNonDestructive(true);             // ✅ keeps original shapes intact
    fuse.Build();                             // explicitly build result
    if (!fuse.IsDone()) {
        std::cerr << termcolor::red << "Fuse failed" << "\n";
    } 

    return ShapePtr( new Shape( fuse.Shape() ) );
}

 ShapePtr 
 Shape::cut( const ShapePtr &part ) {
    BRepAlgoAPI_Cut cut( mShape, part->getShape() );
    cut.SetRunParallel(Standard_True);       // ✅ multi-threaded if possible
    cut.SetNonDestructive(true);
    cut.Build();   

    if (!cut.IsDone()) {
        std::cerr << termcolor::red << "Cut failed" << "\n";
    } 

    return ShapePtr( new Shape( cut.Shape() ) );
 }