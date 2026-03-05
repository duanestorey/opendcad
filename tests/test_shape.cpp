#include <gtest/gtest.h>
#include "Shape.h"

using namespace opendcad;

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
