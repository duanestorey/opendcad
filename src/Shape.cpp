#include "Shape.h"
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
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
Shape::createCylinder( double radius, double height ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    return ShapePtr( new Shape( BRepPrimAPI_MakeCylinder(ax, radius, height ).Shape() ) );
}

Shape::Shape( const TopoDS_Shape &shape ) : mShape( shape ) {
    
}

Shape::Shape() {}

void 
Shape::translate( double x, double y, double z ) {
    gp_Trsf tr;
    tr.SetTranslation( gp_Vec(x, y, z)); 
    TopLoc_Location loc(tr);
    mShape = mShape.Moved( loc );
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