#include "Shape.h"
#include "Error.h"

// Existing includes
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeWedge.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Face.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Trsf.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

using namespace opendcad;

// =============================================================================
// Constructors
// =============================================================================

Shape::Shape( const TopoDS_Shape &shape ) : mShape( shape ) {}
Shape::Shape() {}

// =============================================================================
// 3D Primitive Factories
// =============================================================================

ShapePtr Shape::createBox( double width, double depth, double height ) {
    gp_Pnt point( -width/2, -depth/2, 0 );
    return ShapePtr( new Shape( BRepPrimAPI_MakeBox( point, width, depth, height ).Shape() ) );
}

ShapePtr Shape::createBin( double width, double depth, double height, double thickness ) {
    gp_Pnt point( -width/2, -depth/2, 0 );
    gp_Pnt point2( -( width - 2*thickness )/2, -(depth - 2*thickness)/2, thickness );

    ShapePtr box = ShapePtr( new Shape( BRepPrimAPI_MakeBox( point, width, depth, height ).Shape() ) );
    ShapePtr box2 = ShapePtr( new Shape( BRepPrimAPI_MakeBox( point2, width - 2*thickness, depth - 2*thickness, height - thickness ).Shape() ) );

    return box->cut( box2 );
}

ShapePtr Shape::createCylinder( double radius, double height ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    return ShapePtr( new Shape( BRepPrimAPI_MakeCylinder(ax, radius, height ).Shape() ) );
}

ShapePtr Shape::createTorus( double r1, double r2, double angle ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    if (angle > 0 && angle < 360.0) {
        double angleRad = angle * M_PI / 180.0;
        return ShapePtr( new Shape( BRepPrimAPI_MakeTorus( ax, r1, r2, angleRad ).Shape() ) );
    }
    return ShapePtr( new Shape( BRepPrimAPI_MakeTorus( ax, r1, r2 ).Shape() ) );
}

ShapePtr Shape::createSphere( double radius ) {
    return ShapePtr( new Shape( BRepPrimAPI_MakeSphere( radius ).Shape() ) );
}

ShapePtr Shape::createCone( double r1, double r2, double height ) {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp::DZ(), gp::DX());
    return ShapePtr( new Shape( BRepPrimAPI_MakeCone( ax, r1, r2, height ).Shape() ) );
}

ShapePtr Shape::createWedge( double dx, double dy, double dz, double ltx ) {
    // BRepPrimAPI_MakeWedge builds along Y axis by default; we rotate to Z-up
    BRepPrimAPI_MakeWedge mk( dx, dy, dz, ltx );
    mk.Build();
    if (!mk.IsDone())
        throw GeometryError("wedge creation failed");

    // Rotate from Y-up to Z-up: rotate -90 degrees around X axis
    gp_Trsf rot;
    rot.SetRotation(gp::OX(), -M_PI / 2.0);
    TopoDS_Shape rotated = BRepBuilderAPI_Transform(mk.Shape(), rot, true).Shape();

    return ShapePtr( new Shape( rotated ) );
}

// =============================================================================
// 2D Primitive Factories
// =============================================================================

ShapePtr Shape::createCircle( double radius ) {
    gp_Circ circ(gp_Ax2(gp_Pnt(0,0,0), gp::DZ()), radius);
    TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(circ);
    TopoDS_Wire wire = BRepBuilderAPI_MakeWire(edge);
    TopoDS_Face face = BRepBuilderAPI_MakeFace(wire);
    return ShapePtr( new Shape( face ) );
}

ShapePtr Shape::createRectangle( double width, double height ) {
    double hw = width / 2.0;
    double hh = height / 2.0;

    gp_Pnt p1(-hw, -hh, 0), p2(hw, -hh, 0), p3(hw, hh, 0), p4(-hw, hh, 0);

    BRepBuilderAPI_MakePolygon poly;
    poly.Add(p1);
    poly.Add(p2);
    poly.Add(p3);
    poly.Add(p4);
    poly.Close();

    TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire());
    return ShapePtr( new Shape( face ) );
}

ShapePtr Shape::createPolygon( const std::vector<std::pair<double,double>>& pts ) {
    if (pts.size() < 3)
        throw GeometryError("polygon requires at least 3 points");

    BRepBuilderAPI_MakePolygon poly;
    for (const auto& [x, y] : pts) {
        poly.Add(gp_Pnt(x, y, 0));
    }
    poly.Close();

    if (!poly.IsDone())
        throw GeometryError("polygon wire creation failed");

    TopoDS_Face face = BRepBuilderAPI_MakeFace(poly.Wire());
    return ShapePtr( new Shape( face ) );
}

// =============================================================================
// Multi-Shape Factory
// =============================================================================

ShapePtr Shape::createLoft( const std::vector<ShapePtr>& profiles, bool solid, bool ruled ) {
    if (profiles.size() < 2)
        throw GeometryError("loft requires at least 2 profiles");

    BRepOffsetAPI_ThruSections loft(solid, ruled);

    for (const auto& profile : profiles) {
        TopExp_Explorer ex(profile->getShape(), TopAbs_WIRE);
        if (ex.More()) {
            loft.AddWire(TopoDS::Wire(ex.Current()));
        } else {
            throw GeometryError("loft profile must contain a wire");
        }
    }

    loft.Build();
    if (!loft.IsDone())
        throw GeometryError("loft operation failed");

    return ShapePtr( new Shape( loft.Shape() ) );
}

// =============================================================================
// Boolean Operations
// =============================================================================

ShapePtr Shape::fuse( const ShapePtr &part ) {
    BRepAlgoAPI_Fuse fuse( mShape, part->getShape() );
    fuse.SetRunParallel(Standard_True);
    fuse.SetNonDestructive(true);
    fuse.Build();
    if (!fuse.IsDone())
        throw GeometryError("fuse operation failed");
    return ShapePtr( new Shape( fuse.Shape() ) );
}

ShapePtr Shape::cut( const ShapePtr &part ) {
    BRepAlgoAPI_Cut cut( mShape, part->getShape() );
    cut.SetRunParallel(Standard_True);
    cut.SetNonDestructive(true);
    cut.Build();
    if (!cut.IsDone())
        throw GeometryError("cut operation failed");
    return ShapePtr( new Shape( cut.Shape() ) );
}

ShapePtr Shape::intersect( const ShapePtr &part ) const {
    BRepAlgoAPI_Common common( mShape, part->getShape() );
    common.SetRunParallel(Standard_True);
    common.SetNonDestructive(true);
    common.Build();
    if (!common.IsDone())
        throw GeometryError("intersect operation failed");
    return ShapePtr( new Shape( common.Shape() ) );
}

// =============================================================================
// Edge Operations
// =============================================================================

ShapePtr Shape::fillet( double amount ) const {
    BRepFilletAPI_MakeFillet mk( mShape );
    for (TopExp_Explorer ex(mShape, TopAbs_EDGE); ex.More(); ex.Next()) {
        mk.Add(amount, TopoDS::Edge(ex.Current()));
    }
    mk.Build();
    if (!mk.IsDone())
        throw GeometryError("fillet operation failed");
    return ShapePtr( new Shape( mk.Shape() ) );
}

ShapePtr Shape::chamfer( double distance ) const {
    BRepFilletAPI_MakeChamfer mk( mShape );

    for (TopExp_Explorer ex(mShape, TopAbs_EDGE); ex.More(); ex.Next()) {
        mk.Add(distance, TopoDS::Edge(ex.Current()));
    }

    mk.Build();
    if (!mk.IsDone())
        throw GeometryError("chamfer operation failed");
    return ShapePtr( new Shape( mk.Shape() ) );
}

// =============================================================================
// Transforms
// =============================================================================

ShapePtr Shape::flip() const {
    gp_Trsf mirror;
    mirror.SetMirror(gp::XOY());
    return ShapePtr(new Shape(BRepBuilderAPI_Transform(mShape, mirror, true).Shape()));
}

ShapePtr Shape::translate( double x, double y, double z ) {
    gp_Trsf tr;
    tr.SetTranslation( gp_Vec(x, y, z) );
    TopLoc_Location loc(tr);
    return ShapePtr( new Shape( mShape.Moved( loc ) ) );
}

ShapePtr Shape::rotate( double xAngle, double yAngle, double zAngle ) const {
    double toRad = M_PI / 180.0;
    gp_Trsf rotX, rotY, rotZ;
    rotX.SetRotation(gp::OX(), xAngle * toRad);
    rotY.SetRotation(gp::OY(), yAngle * toRad);
    rotZ.SetRotation(gp::OZ(), zAngle * toRad);
    gp_Trsf trsf = rotZ * rotY * rotX;
    return ShapePtr( new Shape( BRepBuilderAPI_Transform(mShape, trsf, true).Shape() ) );
}

ShapePtr Shape::scale( double factor ) const {
    gp_Trsf trsf;
    trsf.SetScale(gp_Pnt(0,0,0), factor);
    BRepBuilderAPI_Transform xform(mShape, trsf, true);
    if (!xform.IsDone())
        throw GeometryError("scale operation failed");
    return ShapePtr( new Shape( xform.Shape() ) );
}

ShapePtr Shape::scale( double fx, double fy, double fz ) const {
    gp_GTrsf gtrsf;
    gtrsf.SetValue(1, 1, fx);
    gtrsf.SetValue(2, 2, fy);
    gtrsf.SetValue(3, 3, fz);
    BRepBuilderAPI_GTransform xform(mShape, gtrsf, true);
    if (!xform.IsDone())
        throw GeometryError("non-uniform scale operation failed");
    return ShapePtr( new Shape( xform.Shape() ) );
}

ShapePtr Shape::mirror( double nx, double ny, double nz ) const {
    gp_Ax2 ax(gp_Pnt(0,0,0), gp_Dir(nx, ny, nz));
    gp_Trsf trsf;
    trsf.SetMirror(ax);
    return ShapePtr( new Shape( BRepBuilderAPI_Transform(mShape, trsf, true).Shape() ) );
}

ShapePtr Shape::placeCorners( ShapePtr shape, double xOffset, double yOffset ) {
    ShapePtr newShape = fuse( shape->translate( xOffset, yOffset ) );
    newShape = newShape->fuse( shape->translate( xOffset, -yOffset ) );
    newShape = newShape->fuse( shape->translate( -xOffset, yOffset ) );
    newShape = newShape->fuse( shape->translate( -xOffset, -yOffset ) );
    return newShape;
}

// =============================================================================
// Extrusion / Sweep
// =============================================================================

ShapePtr Shape::linearExtrude( double height ) const {
    gp_Vec direction(0, 0, height);
    BRepPrimAPI_MakePrism prism(mShape, direction);
    prism.Build();
    if (!prism.IsDone())
        throw GeometryError("linear_extrude operation failed");
    return ShapePtr( new Shape( prism.Shape() ) );
}

ShapePtr Shape::rotateExtrude( double angleDeg ) const {
    double angleRad = angleDeg * M_PI / 180.0;
    gp_Ax1 axis(gp_Pnt(0,0,0), gp::DZ());

    // Revolve the shape directly — works for faces, wires, edges
    BRepPrimAPI_MakeRevol revol(mShape, axis, angleRad, true);
    revol.Build();
    if (!revol.IsDone())
        throw GeometryError("rotate_extrude operation failed");

    TopoDS_Shape result = revol.Shape();

    // If the result is a shell (from revolving a face), try to make it a solid
    if (result.ShapeType() == TopAbs_SHELL) {
        BRepBuilderAPI_MakeSolid solidMaker;
        solidMaker.Add(TopoDS::Shell(result));
        if (solidMaker.IsDone())
            result = solidMaker.Shape();
    }

    return ShapePtr( new Shape( result ) );
}

ShapePtr Shape::sweep( const ShapePtr& pathShape ) const {
    // Extract the wire from the path shape
    TopExp_Explorer ex(pathShape->getShape(), TopAbs_WIRE);
    if (!ex.More())
        throw GeometryError("sweep path must contain a wire");

    TopoDS_Wire pathWire = TopoDS::Wire(ex.Current());
    BRepOffsetAPI_MakePipe pipe(pathWire, mShape);
    pipe.Build();
    if (!pipe.IsDone())
        throw GeometryError("sweep operation failed");
    return ShapePtr( new Shape( pipe.Shape() ) );
}

// =============================================================================
// Shell
// =============================================================================

ShapePtr Shape::shell( double thickness ) const {
    // Find the top face (highest Z centroid) to remove
    TopExp_Explorer faceEx(mShape, TopAbs_FACE);
    TopoDS_Face topFace;
    double maxZ = -1e99;

    for (; faceEx.More(); faceEx.Next()) {
        TopoDS_Face face = TopoDS::Face(faceEx.Current());
        Bnd_Box bbox;
        BRepBndLib::Add(face, bbox);
        double xmin, ymin, zmin, xmax, ymax, zmax;
        bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double zCenter = (zmin + zmax) / 2.0;
        if (zCenter > maxZ) {
            maxZ = zCenter;
            topFace = face;
        }
    }

    if (topFace.IsNull())
        throw GeometryError("shell: could not find a face to remove");

    TopTools_ListOfShape facesToRemove;
    facesToRemove.Append(topFace);

    BRepOffsetAPI_MakeThickSolid hollower;
    hollower.MakeThickSolidByJoin(mShape, facesToRemove, -thickness, 1e-3);
    hollower.Build();

    if (!hollower.IsDone())
        throw GeometryError("shell operation failed");

    return ShapePtr( new Shape( hollower.Shape() ) );
}
