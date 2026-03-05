#include <gtest/gtest.h>
#include "Shape.h"
#include "FaceRef.h"
#include "FaceSelector.h"
#include "Workplane.h"
#include "Sketch.h"
#include "EdgeSelector.h"
#include "Error.h"
#include <cmath>

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
