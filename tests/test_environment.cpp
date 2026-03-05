#include <gtest/gtest.h>
#include "Environment.h"
#include "Error.h"

using namespace opendcad;

TEST(EnvironmentTest, DefineAndLookup) {
    auto env = std::make_shared<Environment>();
    env->define("x", Value::makeNumber(42.0));
    auto val = env->lookup("x");
    EXPECT_EQ(val->type(), ValueType::NUMBER);
    EXPECT_DOUBLE_EQ(val->asNumber(), 42.0);
}

TEST(EnvironmentTest, LookupUndefinedThrows) {
    auto env = std::make_shared<Environment>();
    EXPECT_THROW(env->lookup("missing"), EvalError);
}

TEST(EnvironmentTest, HasReturnsTrueForDefined) {
    auto env = std::make_shared<Environment>();
    env->define("x", Value::makeNumber(1.0));
    EXPECT_TRUE(env->has("x"));
}

TEST(EnvironmentTest, HasReturnsFalseForUndefined) {
    auto env = std::make_shared<Environment>();
    EXPECT_FALSE(env->has("missing"));
}

TEST(EnvironmentTest, ParentChainLookup) {
    auto parent = std::make_shared<Environment>();
    parent->define("x", Value::makeNumber(10.0));

    auto child = std::make_shared<Environment>(parent);
    auto val = child->lookup("x");
    EXPECT_DOUBLE_EQ(val->asNumber(), 10.0);
}

TEST(EnvironmentTest, ChildShadowsParent) {
    auto parent = std::make_shared<Environment>();
    parent->define("x", Value::makeNumber(10.0));

    auto child = std::make_shared<Environment>(parent);
    child->define("x", Value::makeNumber(20.0));

    EXPECT_DOUBLE_EQ(child->lookup("x")->asNumber(), 20.0);
    EXPECT_DOUBLE_EQ(parent->lookup("x")->asNumber(), 10.0);
}

TEST(EnvironmentTest, HasSearchesParentChain) {
    auto parent = std::make_shared<Environment>();
    parent->define("x", Value::makeNumber(1.0));
    auto child = std::make_shared<Environment>(parent);
    EXPECT_TRUE(child->has("x"));
    EXPECT_FALSE(child->has("y"));
}
