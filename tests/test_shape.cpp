#include <gtest/gtest.h>
#include "Shape.h"
#include "Error.h"

using namespace opendcad;

// =============================================================================
// Phase 0 — Existing 3D Primitives
// =============================================================================

TEST(ShapeTest, CreateBoxIsValid) {
    auto shape = Shape::createBox(10, 20, 30);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateCylinderIsValid) {
    auto shape = Shape::createCylinder(5, 10);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateTorusIsValid) {
    auto shape = Shape::createTorus(10, 3);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateBinIsValid) {
    auto shape = Shape::createBin(80, 120, 20, 2);
    EXPECT_TRUE(shape->isValid());
}

// =============================================================================
// Phase 1 — Tier 1: New 3D Primitives
// =============================================================================

TEST(ShapeTest, CreateSphereIsValid) {
    auto shape = Shape::createSphere(10);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateConeIsValid) {
    auto shape = Shape::createCone(10, 5, 20);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateConeZeroTopRadius) {
    auto shape = Shape::createCone(10, 0, 20);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateWedgeIsValid) {
    auto shape = Shape::createWedge(20, 10, 15, 5);
    EXPECT_TRUE(shape->isValid());
}

// =============================================================================
// Phase 1 — Tier 2: 2D Primitives
// =============================================================================

TEST(ShapeTest, CreateCircleIsValid) {
    auto shape = Shape::createCircle(5);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreateRectangleIsValid) {
    auto shape = Shape::createRectangle(10, 20);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreatePolygonTriangle) {
    std::vector<std::pair<double,double>> pts = {{0,0}, {10,0}, {5,10}};
    auto shape = Shape::createPolygon(pts);
    EXPECT_TRUE(shape->isValid());
}

TEST(ShapeTest, CreatePolygonTooFewPointsThrows) {
    std::vector<std::pair<double,double>> pts = {{0,0}, {10,0}};
    EXPECT_THROW(Shape::createPolygon(pts), GeometryError);
}

// =============================================================================
// Phase 0 — Existing Boolean Operations
// =============================================================================

TEST(ShapeTest, FuseTwoBoxes) {
    auto a = Shape::createBox(10, 10, 10);
    auto b = Shape::createBox(5, 5, 5);
    auto result = a->fuse(b);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, CutCylinderFromBox) {
    auto box = Shape::createBox(20, 20, 20);
    auto cyl = Shape::createCylinder(3, 30);
    auto result = box->cut(cyl);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Phase 1 — Tier 1: Intersect + Chamfer
// =============================================================================

TEST(ShapeTest, IntersectTwoBoxes) {
    auto a = Shape::createBox(20, 20, 20);
    auto b = Shape::createBox(10, 10, 10)->translate(5, 5, 5);
    auto result = a->intersect(b);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, ChamferBox) {
    auto box = Shape::createBox(20, 20, 20);
    auto result = box->chamfer(1.0);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Phase 0 — Existing Transforms
// =============================================================================

TEST(ShapeTest, FilletBox) {
    auto box = Shape::createBox(20, 20, 20);
    auto result = box->fillet(1.0);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, FlipIsValid) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->flip();
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, TranslateIsValid) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->translate(1, 2, 3);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, RotateIsValid) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->rotate(0, 90, 0);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, XShortcut) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->x(5);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, YShortcut) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->y(5);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, ZShortcut) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->z(5);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, PlaceCornersIsValid) {
    auto base = Shape::createBox(80, 80, 10);
    auto piece = Shape::createCylinder(3, 10);
    auto result = base->placeCorners(piece, 30, 30);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, ChainOperations) {
    auto result = Shape::createCylinder(5, 10)->translate(1, 0, 0)->rotate(0, 90, 0);
    EXPECT_TRUE(result->isValid());
}

// =============================================================================
// Phase 1 — Tier 3: Extrusion and Revolution
// =============================================================================

TEST(ShapeTest, LinearExtrudeCircle) {
    auto circle = Shape::createCircle(5);
    auto solid = circle->linearExtrude(10);
    EXPECT_TRUE(solid->isValid());
}

TEST(ShapeTest, LinearExtrudeRectangle) {
    auto rect = Shape::createRectangle(10, 20);
    auto solid = rect->linearExtrude(5);
    EXPECT_TRUE(solid->isValid());
}

TEST(ShapeTest, RotateExtrudeCircleFull) {
    // Circle in XZ plane (rotated 90° around X), offset from Z axis, revolved 360°
    auto circle = Shape::createCircle(2)->rotate(90, 0, 0)->translate(10, 0, 0);
    auto solid = circle->rotateExtrude(360);
    EXPECT_TRUE(solid->isValid());
}

TEST(ShapeTest, RotateExtrudeCirclePartial) {
    auto circle = Shape::createCircle(2)->rotate(90, 0, 0)->translate(10, 0, 0);
    auto solid = circle->rotateExtrude(180);
    EXPECT_TRUE(solid->isValid());
}

TEST(ShapeTest, LoftTwoCircles) {
    auto c1 = Shape::createCircle(10);
    auto c2 = Shape::createCircle(5)->translate(0, 0, 20);
    auto solid = Shape::createLoft({c1, c2});
    EXPECT_TRUE(solid->isValid());
}

TEST(ShapeTest, LoftTooFewProfilesThrows) {
    auto c1 = Shape::createCircle(10);
    EXPECT_THROW(Shape::createLoft({c1}), GeometryError);
}

// =============================================================================
// Phase 1 — Tier 4: Scale, Mirror, Shell
// =============================================================================

TEST(ShapeTest, ScaleUniform) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->scale(2.0);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, ScaleNonUniform) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = box->scale(2.0, 1.0, 0.5);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, MirrorXY) {
    auto box = Shape::createBox(10, 10, 10)->translate(0, 0, 5);
    auto result = box->mirror(0, 0, 1);
    EXPECT_TRUE(result->isValid());
}

TEST(ShapeTest, ShellBox) {
    auto box = Shape::createBox(20, 20, 20);
    auto result = box->shell(2.0);
    EXPECT_TRUE(result->isValid());
}
