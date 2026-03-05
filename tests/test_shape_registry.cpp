#include <gtest/gtest.h>
#include "ShapeRegistry.h"
#include "Error.h"

using namespace opendcad;

class ShapeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        ShapeRegistry::instance().registerDefaults();
    }
};

TEST_F(ShapeRegistryTest, HasFactoryBox) {
    EXPECT_TRUE(ShapeRegistry::instance().hasFactory("box"));
}

TEST_F(ShapeRegistryTest, HasFactoryNonexistent) {
    EXPECT_FALSE(ShapeRegistry::instance().hasFactory("nonexistent"));
}

TEST_F(ShapeRegistryTest, CallFactoryCylinder) {
    std::vector<ValuePtr> args = {Value::makeNumber(10.5), Value::makeNumber(70)};
    auto shape = ShapeRegistry::instance().callFactory("cylinder", args);
    EXPECT_TRUE(shape->isValid());
}

TEST_F(ShapeRegistryTest, CallFactoryWrongArgCountThrows) {
    std::vector<ValuePtr> args = {Value::makeNumber(10)};
    EXPECT_THROW(ShapeRegistry::instance().callFactory("box", args), EvalError);
}

TEST_F(ShapeRegistryTest, HasMethodFuse) {
    EXPECT_TRUE(ShapeRegistry::instance().hasMethod("fuse"));
}

TEST_F(ShapeRegistryTest, CallMethodTranslateWithVector) {
    auto shape = Shape::createBox(10, 10, 10);
    std::vector<ValuePtr> args = {Value::makeVector({1.0, 2.0, 3.0})};
    auto result = ShapeRegistry::instance().callMethod("translate", shape, args);
    EXPECT_EQ(result->type(), ValueType::SHAPE);
    EXPECT_TRUE(result->asShape()->isValid());
}

TEST_F(ShapeRegistryTest, CallMethodTranslateWithNumbers) {
    auto shape = Shape::createBox(10, 10, 10);
    std::vector<ValuePtr> args = {
        Value::makeNumber(1), Value::makeNumber(2), Value::makeNumber(3)
    };
    auto result = ShapeRegistry::instance().callMethod("translate", shape, args);
    EXPECT_TRUE(result->asShape()->isValid());
}
