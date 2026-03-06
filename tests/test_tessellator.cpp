#include <gtest/gtest.h>
#include "Tessellator.h"
#include "Shape.h"
#include <set>

using namespace opendcad;

TEST(Tessellator, BoxProducesTriangles) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_GT(result.totalTriangles(), 0);
    EXPECT_EQ(result.vertices.size() % 3, 0u);
}

TEST(Tessellator, BoxHasSixFaces) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_EQ(result.faces.size(), 6u);
}

TEST(Tessellator, BoxHasEdges) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_GT(result.edges.size(), 0u);
    EXPECT_GT(result.edgeVertices.size(), 0u);
}

TEST(Tessellator, BoundsAreCorrect) {
    auto box = Shape::createBox(10, 20, 30);
    auto result = Tessellator::tessellate(box->getShape());
    EXPECT_NEAR(result.bboxMin[0], -5.0f, 0.1f);
    EXPECT_NEAR(result.bboxMax[0],  5.0f, 0.1f);
    EXPECT_NEAR(result.bboxMin[2],  0.0f, 0.1f);
    EXPECT_NEAR(result.bboxMax[2], 30.0f, 0.1f);
}

TEST(Tessellator, CylinderHasCurvedFaces) {
    auto cyl = Shape::createCylinder(5, 20);
    auto result = Tessellator::tessellate(cyl->getShape());
    EXPECT_GT(result.totalTriangles(), 12);
    EXPECT_GE(result.faces.size(), 3u);
}

TEST(Tessellator, FaceIDsAreUnique) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape());
    std::set<int> ids;
    for (const auto& f : result.faces) ids.insert(f.faceID);
    EXPECT_EQ(ids.size(), result.faces.size());
}

TEST(Tessellator, DefaultColorsApplied) {
    auto box = Shape::createBox(10, 10, 10);
    auto result = Tessellator::tessellate(box->getShape(),
                                           0.8f, 0.2f, 0.1f, 0.0f, 0.5f);
    EXPECT_GT(result.vertices.size(), 0u);
    EXPECT_NEAR(result.vertices[0].color[0], 0.8f, 0.01f);
    EXPECT_NEAR(result.vertices[0].metallic, 0.0f, 0.01f);
    EXPECT_NEAR(result.vertices[0].roughness, 0.5f, 0.01f);
}
