#include <gtest/gtest.h>
#include "Shape.h"
#include "FaceRef.h"
#include "FaceSelector.h"
#include "Workplane.h"
#include "Sketch.h"
#include "EdgeSelector.h"
#include "Error.h"
#include <cmath>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>

using namespace opendcad;

// =============================================================================
// FaceRef Tests
// =============================================================================

TEST(FaceRefTest, CenterOfTopFace) {
    auto box = Shape::createBox(40, 30, 10);
    auto top = box->face(">Z");
    gp_Pnt c = top->center();
    EXPECT_NEAR(c.Z(), 10.0, 0.1);
}

TEST(FaceRefTest, NormalOfTopFace) {
    auto box = Shape::createBox(40, 30, 10);
    auto top = box->face(">Z");
    gp_Dir n = top->normal();
    EXPECT_NEAR(n.Z(), 1.0, 0.01);
    EXPECT_NEAR(std::abs(n.X()), 0.0, 0.01);
    EXPECT_NEAR(std::abs(n.Y()), 0.0, 0.01);
}

TEST(FaceRefTest, IsPlanarOnBox) {
    auto box = Shape::createBox(10, 10, 10);
    auto top = box->face(">Z");
    EXPECT_TRUE(top->isPlanar());
}

TEST(FaceRefTest, AreaOfTopFace) {
    auto box = Shape::createBox(40, 30, 10);
    auto top = box->face(">Z");
    EXPECT_NEAR(top->area(), 40.0 * 30.0, 1.0);
}

TEST(FaceRefTest, EdgeCountOfBoxFace) {
    auto box = Shape::createBox(10, 10, 10);
    auto top = box->face(">Z");
    EXPECT_EQ(top->edgeCount(), 4);
}

// =============================================================================
// FaceSelector Tests
// =============================================================================

TEST(FaceSelectorTest, BoxHasSixFaces) {
    auto box = Shape::createBox(10, 10, 10);
    auto sel = box->faces();
    EXPECT_EQ(sel->count(), 6);
}

TEST(FaceSelectorTest, TopFace) {
    auto box = Shape::createBox(10, 10, 10);
    auto top = box->faces()->top();
    gp_Dir n = top->normal();
    EXPECT_NEAR(n.Z(), 1.0, 0.01);
}

TEST(FaceSelectorTest, BottomFace) {
    auto box = Shape::createBox(10, 10, 10);
    auto bottom = box->faces()->bottom();
    gp_Dir n = bottom->normal();
    EXPECT_NEAR(n.Z(), -1.0, 0.01);
}

TEST(FaceSelectorTest, PlanarFilter) {
    auto box = Shape::createBox(10, 10, 10);
    auto planarFaces = box->faces()->planar();
    EXPECT_EQ(planarFaces->count(), 6);
}

TEST(FaceSelectorTest, CylindricalFilterOnBox) {
    auto box = Shape::createBox(10, 10, 10);
    auto cylFaces = box->faces()->cylindrical();
    EXPECT_EQ(cylFaces->count(), 0);
}

TEST(FaceSelectorTest, CylindricalFilterOnCylinder) {
    auto cyl = Shape::createCylinder(5, 10);
    auto cylFaces = cyl->faces()->cylindrical();
    EXPECT_GT(cylFaces->count(), 0);
}

TEST(FaceSelectorTest, LargestFace) {
    auto box = Shape::createBox(40, 30, 10);
    auto largest = box->faces()->largest();
    EXPECT_NEAR(largest->area(), 40.0 * 30.0, 1.0);
}

TEST(FaceSelectorTest, SmallestFace) {
    auto box = Shape::createBox(40, 30, 10);
    auto smallest = box->faces()->smallest();
    EXPECT_NEAR(smallest->area(), 30.0 * 10.0, 1.0);
}

TEST(FaceSelectorTest, ByIndexValid) {
    auto box = Shape::createBox(10, 10, 10);
    auto face = box->faces()->byIndex(0);
    EXPECT_TRUE(face->isPlanar());
}

TEST(FaceSelectorTest, ByIndexOutOfRangeThrows) {
    auto box = Shape::createBox(10, 10, 10);
    EXPECT_THROW(box->faces()->byIndex(100), GeometryError);
}

TEST(FaceSelectorTest, ByIndexNegativeThrows) {
    auto box = Shape::createBox(10, 10, 10);
    EXPECT_THROW(box->faces()->byIndex(-1), GeometryError);
}

// =============================================================================
// Workplane Tests
// =============================================================================

TEST(WorkplaneTest, OriginPosition) {
    auto box = Shape::createBox(40, 30, 10);
    auto wp = box->face(">Z")->workplane();
    gp_Pnt o = wp->origin();
    EXPECT_NEAR(o.Z(), 10.0, 0.1);
}

TEST(WorkplaneTest, NormalDirection) {
    auto box = Shape::createBox(40, 30, 10);
    auto wp = box->face(">Z")->workplane();
    gp_Dir n = wp->normal();
    EXPECT_NEAR(n.Z(), 1.0, 0.01);
}

TEST(WorkplaneTest, ToWorldOrigin) {
    auto box = Shape::createBox(40, 30, 10);
    auto wp = box->face(">Z")->workplane();
    gp_Pnt p = wp->toWorld(0, 0);
    // Should be at the face center
    gp_Pnt o = wp->origin();
    EXPECT_NEAR(p.X(), o.X(), 0.01);
    EXPECT_NEAR(p.Y(), o.Y(), 0.01);
    EXPECT_NEAR(p.Z(), o.Z(), 0.01);
}

TEST(WorkplaneTest, Offset) {
    auto box = Shape::createBox(40, 30, 10);
    auto wp = box->face(">Z")->workplane();
    auto wp2 = wp->offset(5.0);
    EXPECT_NEAR(wp2->origin().Z(), wp->origin().Z() + 5.0, 0.01);
}

// =============================================================================
// Sketch Tests
// =============================================================================

TEST(SketchTest, CircleExtrude) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->circle(8)->extrude(15);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, RectCutBlind) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->rect(20, 15)->cutBlind(3);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, CircleCutThrough) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->circle(5)->cutThrough();
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, MultipleProfiles) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->circle(8, -10, 0);
    sk->circle(8, 10, 0);
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, FreeformWire) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-5, -5);
    sk->lineTo(5, -5);
    sk->lineTo(5, 5);
    sk->lineTo(-5, 5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// EdgeSelector Tests
// =============================================================================

TEST(EdgeSelectorTest, BoxEdgeCount) {
    auto box = Shape::createBox(10, 10, 10);
    auto sel = box->edges();
    EXPECT_EQ(sel->count(), 12);
}

TEST(EdgeSelectorTest, VerticalEdges) {
    auto box = Shape::createBox(10, 10, 10);
    auto vert = box->edges()->vertical();
    EXPECT_EQ(vert->count(), 4);
}

TEST(EdgeSelectorTest, HorizontalEdges) {
    auto box = Shape::createBox(10, 10, 10);
    auto horiz = box->edges()->horizontal();
    EXPECT_EQ(horiz->count(), 8);
}

TEST(EdgeSelectorTest, FilletVerticalEdges) {
    auto box = Shape::createBox(20, 20, 20);
    auto result = box->edges()->vertical()->fillet(2);
    EXPECT_TRUE(result->isValid());
}

TEST(EdgeSelectorTest, ChamferVerticalEdges) {
    auto box = Shape::createBox(20, 20, 20);
    auto result = box->edges()->vertical()->chamfer(1);
    EXPECT_TRUE(result->isValid());
}

TEST(EdgeSelectorTest, FilletNoEdgesThrows) {
    auto box = Shape::createBox(10, 10, 10);
    auto sel = box->edges()->parallelTo(gp_Dir(1, 1, 1)); // unlikely to match
    // This might match 0 edges
    if (sel->count() == 0) {
        EXPECT_THROW(sel->fillet(1), GeometryError);
    }
}

// =============================================================================
// Face Selector String API
// =============================================================================

TEST(FaceRefTest, FaceSelectorStrings) {
    auto box = Shape::createBox(10, 10, 10);

    auto top = box->face(">Z");
    EXPECT_NEAR(top->normal().Z(), 1.0, 0.01);

    auto bottom = box->face("<Z");
    EXPECT_NEAR(bottom->normal().Z(), -1.0, 0.01);

    auto right = box->face(">X");
    EXPECT_NEAR(right->normal().X(), 1.0, 0.01);

    auto left = box->face("<X");
    EXPECT_NEAR(left->normal().X(), -1.0, 0.01);
}

TEST(FaceRefTest, InvalidSelectorThrows) {
    auto box = Shape::createBox(10, 10, 10);
    EXPECT_THROW(box->face("invalid"), GeometryError);
}

// =============================================================================
// Sketch Slot Tests
// =============================================================================

TEST(SketchTest, SlotExtrude) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->slot(20, 8)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, SlotCutThrough) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->slot(25, 8)->cutThrough();
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, SlotOffCenter) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->slot(15, 6, 5, 3)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Sketch ArcTo Tests
// =============================================================================

TEST(SketchTest, ArcToFreeformExtrude) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-10, 0);
    sk->arcTo(0, 10, 5);
    sk->arcTo(10, 0, 5);
    sk->lineTo(10, -5);
    sk->lineTo(-10, -5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, ArcToBulgePositive) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-5, -5);
    sk->arcTo(5, -5, 3);
    sk->lineTo(5, 5);
    sk->lineTo(-5, 5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, ArcToBulgeNegative) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-5, -5);
    sk->arcTo(5, -5, -3);
    sk->lineTo(5, 5);
    sk->lineTo(-5, 5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, ArcToZeroBulgeFallback) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-5, -5);
    sk->arcTo(5, -5, 0.0); // should become a straight line
    sk->lineTo(5, 5);
    sk->lineTo(-5, 5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Sketch SplineTo Tests
// =============================================================================

TEST(SketchTest, SplineToExtrude) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-10, 0);
    sk->splineTo({{-5, 5}, {0, 3}}, 10, 0);
    sk->lineTo(10, -5);
    sk->lineTo(-10, -5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, SplineMultiplePoints) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->moveTo(-10, 0);
    sk->splineTo({{-7, 4}, {-3, 6}, {3, 6}, {7, 4}}, 10, 0);
    sk->lineTo(10, -5);
    sk->lineTo(-10, -5);
    sk->close();
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Sketch Revolve Tests
// =============================================================================

TEST(SketchTest, RevolveFullCircle) {
    auto box = Shape::createBox(40, 40, 10);
    // Circle offset in Y (perpendicular to X rotation axis)
    auto result = box->face(">Z")->draw()->circle(2, 0, 12)->revolve(360);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, RevolvePartialAngle) {
    auto box = Shape::createBox(40, 40, 10);
    auto result = box->face(">Z")->draw()->circle(2, 0, 12)->revolve(180);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, RevolveFusesWithParent) {
    auto box = Shape::createBox(40, 40, 10);
    auto result = box->face(">Z")->draw()->circle(2, 0, 12)->revolve(360);
    EXPECT_TRUE(result->isValid());
    // The result should have more faces than original 6-face box
    EXPECT_GT(result->faces()->count(), 6);
}

// =============================================================================
// Sketch Wire Operations Tests
// =============================================================================

TEST(SketchTest, Fillet2DOnRect) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->rect(20, 15)->fillet2D(2)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, Fillet2DOnPolygon) {
    auto box = Shape::createBox(40, 30, 10);
    auto sk = box->face(">Z")->draw();
    sk->polygon({{-8, -6}, {8, -6}, {8, 6}, {-8, 6}});
    sk->fillet2D(1.5);
    auto result = sk->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, Chamfer2DOnRect) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->rect(20, 15)->chamfer2D(2)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, OffsetPositive) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->rect(10, 10)->offset(2)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

TEST(SketchTest, OffsetNegative) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->draw()->rect(20, 15)->offset(-2)->extrude(5);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Parametric Hole Tests
// =============================================================================

TEST(FaceRefTest, HoleBlind) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->hole(6, 5);
    EXPECT_TRUE(result->isValid());
}

TEST(FaceRefTest, HoleThrough) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->throughHole(6);
    EXPECT_TRUE(result->isValid());
}

TEST(FaceRefTest, HoleOffCenter) {
    auto box = Shape::createBox(40, 30, 10);
    auto result = box->face(">Z")->hole(4, 5, 10, 5);
    EXPECT_TRUE(result->isValid());
}

TEST(FaceRefTest, CounterboreValid) {
    auto box = Shape::createBox(40, 30, 20);
    auto result = box->face(">Z")->counterbore(5, 10, 3, 15);
    EXPECT_TRUE(result->isValid());
}

TEST(FaceRefTest, CountersinkValid) {
    auto box = Shape::createBox(40, 30, 20);
    auto result = box->face(">Z")->countersink(5, 10, 90, 15);
    EXPECT_TRUE(result->isValid());
}

TEST(FaceRefTest, HoleDimensionCheck) {
    // A hole should reduce volume
    auto box = Shape::createBox(40, 30, 10);
    auto withHole = box->face(">Z")->throughHole(6);
    // After a through-hole, there should be more faces than original 6
    EXPECT_GT(withHole->faces()->count(), 6);
}

TEST(FaceRefTest, CounterboreChainedWithThroughHole) {
    auto box = Shape::createBox(40, 30, 20);
    auto result = box->face(">Z")->counterbore(5, 10, 3, 15);
    auto result2 = result->face(">Z")->throughHole(4, 10, 5);
    EXPECT_TRUE(result2->isValid());
}

// =============================================================================
// Feature Pattern Tests
// =============================================================================

TEST(ShapeTest, LinearPatternBox) {
    auto post = Shape::createCylinder(3, 20);
    auto row = post->linearPattern(15, 0, 0, 4);
    EXPECT_TRUE(row->isValid());
}

TEST(ShapeTest, LinearPatternValid) {
    auto box = Shape::createBox(5, 5, 5);
    auto pattern = box->linearPattern(10, 0, 0, 3);
    EXPECT_TRUE(pattern->isValid());
    // Should have more faces than a single box
    EXPECT_GT(pattern->faces()->count(), 6);
}

TEST(ShapeTest, LinearPatternCount1NoOp) {
    auto box = Shape::createBox(10, 10, 10);
    auto pattern = box->linearPattern(10, 0, 0, 1);
    EXPECT_TRUE(pattern->isValid());
    EXPECT_EQ(pattern->faces()->count(), 6);
}

TEST(ShapeTest, CircularPatternBox) {
    auto post = Shape::createBox(5, 5, 20);
    post = post->translate(15, 0, 0);
    auto pattern = post->circularPattern(0, 0, 1, 4);
    EXPECT_TRUE(pattern->isValid());
}

TEST(ShapeTest, CircularPatternPartialAngle) {
    auto post = Shape::createBox(5, 5, 20);
    post = post->translate(15, 0, 0);
    auto pattern = post->circularPattern(0, 0, 1, 3, 180);
    EXPECT_TRUE(pattern->isValid());
}

TEST(ShapeTest, MirrorFeatureSymmetric) {
    auto box = Shape::createBox(10, 10, 10);
    auto mirrored = box->translate(20, 0, 0)->mirrorFeature(1, 0, 0);
    EXPECT_TRUE(mirrored->isValid());
    // Mirrored + original should have more faces
    EXPECT_GT(mirrored->faces()->count(), 6);
}

TEST(ShapeTest, MirrorFeatureVsMirror) {
    auto box = Shape::createBox(10, 10, 10);
    auto shifted = box->translate(20, 0, 0);
    // mirrorFeature fuses, mirror replaces
    auto mirrored = shifted->mirror(1, 0, 0);
    auto mirrorFeat = shifted->mirrorFeature(1, 0, 0);
    EXPECT_EQ(mirrored->faces()->count(), 6);  // still a single box
    EXPECT_GT(mirrorFeat->faces()->count(), 6); // two boxes fused
}

TEST(ShapeTest, DraftOnBox) {
    auto box = Shape::createBox(20, 20, 30);
    auto drafted = box->draft(5, 0, 0, 1);
    EXPECT_TRUE(drafted->isValid());
}

TEST(ShapeTest, SplitAtMiddle) {
    auto box = Shape::createBox(20, 20, 20);
    auto half = box->splitAt(0, 0, 0, 1, 0, 0);
    EXPECT_TRUE(half->isValid());
    // Half should have fewer faces or smaller volume
    EXPECT_TRUE(half->faces()->count() >= 5);
}

TEST(ShapeTest, SplitAtReturnsValid) {
    auto cyl = Shape::createCylinder(10, 30);
    auto half = cyl->splitAt(0, 0, 15, 0, 0, 1);
    EXPECT_TRUE(half->isValid());
}

// =============================================================================
// Advanced Face Selection Tests
// =============================================================================

TEST(FaceSelectorTest, NearestToCorner) {
    auto box = Shape::createBox(20, 20, 20);
    // Point near the top → should select top face
    auto face = box->faces()->nearestTo(gp_Pnt(0, 0, 100));
    gp_Dir n = face->normal();
    EXPECT_NEAR(n.Z(), 1.0, 0.01);
}

TEST(FaceSelectorTest, FarthestFromOrigin) {
    auto box = Shape::createBox(20, 20, 20);
    // Farthest from a point far below → top face
    auto face = box->faces()->farthestFrom(gp_Pnt(0, 0, -100));
    gp_Dir n = face->normal();
    EXPECT_NEAR(n.Z(), 1.0, 0.01);
}

TEST(FaceSelectorTest, AreaGreaterThan) {
    auto box = Shape::createBox(40, 30, 10);
    // Top/bottom faces = 1200, side faces = 400 and 300
    auto bigFaces = box->faces()->areaGreaterThan(500);
    EXPECT_EQ(bigFaces->count(), 2); // top + bottom
}

TEST(FaceSelectorTest, AreaLessThan) {
    auto box = Shape::createBox(40, 30, 10);
    auto smallFaces = box->faces()->areaLessThan(350);
    EXPECT_EQ(smallFaces->count(), 2); // the 2 narrow side faces (30*10=300)
}

// =============================================================================
// Advanced Edge Selection Tests
// =============================================================================

TEST(EdgeSelectorTest, OfFaceTopEdges) {
    auto box = Shape::createBox(10, 10, 10);
    auto topFace = box->face(">Z");
    auto topEdges = box->edges()->ofFace(topFace);
    EXPECT_EQ(topEdges->count(), 4);
}

TEST(EdgeSelectorTest, LongestEdge) {
    auto box = Shape::createBox(40, 10, 10);
    auto longest = box->edges()->longest();
    EXPECT_EQ(longest->count(), 1);
}

TEST(EdgeSelectorTest, ShortestEdge) {
    auto box = Shape::createBox(40, 30, 10);
    auto shortest = box->edges()->shortest();
    EXPECT_EQ(shortest->count(), 1);
}

TEST(EdgeSelectorTest, LongerThan) {
    auto box = Shape::createBox(40, 30, 10);
    // Edges: 4x40, 4x30, 4x10 → longerThan(35) = 4 edges of length 40
    auto longEdges = box->edges()->longerThan(35);
    EXPECT_EQ(longEdges->count(), 4);
}

TEST(EdgeSelectorTest, ShorterThan) {
    auto box = Shape::createBox(40, 30, 10);
    // shorterThan(15) = 4 edges of length 10
    auto shortEdges = box->edges()->shorterThan(15);
    EXPECT_EQ(shortEdges->count(), 4);
}

// =============================================================================
// Import Tests
// =============================================================================

TEST(ShapeTest, ImportSTEPRoundTrip) {
    // Programmatically create a test STEP file, then import it
    auto box = Shape::createBox(10, 10, 10);
    // Write STEP
    std::string path = "/tmp/opendcad_test_import.step";
    // Use STEPControl_Writer to create test file
    STEPControl_Writer writer;
    writer.Transfer(box->getShape(), STEPControl_AsIs);
    writer.Write(path.c_str());

    auto imported = Shape::importSTEP(path);
    EXPECT_TRUE(imported->isValid());
    EXPECT_EQ(imported->faces()->count(), 6);
}

TEST(ShapeTest, ImportSTEPInvalidPath) {
    EXPECT_THROW(Shape::importSTEP("/nonexistent/path.step"), GeometryError);
}

TEST(ShapeTest, ImportSTLRoundTrip) {
    auto box = Shape::createBox(10, 10, 10);
    std::string path = "/tmp/opendcad_test_import.stl";
    // Mesh the shape before writing STL
    BRepMesh_IncrementalMesh mesh(box->getShape(), 0.1);
    StlAPI_Writer writer;
    writer.Write(box->getShape(), path.c_str());

    auto imported = Shape::importSTL(path);
    EXPECT_TRUE(imported->isValid());
}

TEST(ShapeTest, ImportSTLInvalidPath) {
    EXPECT_THROW(Shape::importSTL("/nonexistent/path.stl"), GeometryError);
}
