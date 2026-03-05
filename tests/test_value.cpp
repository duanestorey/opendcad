#include <gtest/gtest.h>
#include "Value.h"
#include "Error.h"

using namespace opendcad;

TEST(ValueTest, MakeNumberReturnsCorrectType) {
    auto v = Value::makeNumber(42.0);
    EXPECT_EQ(v->type(), ValueType::NUMBER);
    EXPECT_DOUBLE_EQ(v->asNumber(), 42.0);
}

TEST(ValueTest, MakeStringReturnsCorrectType) {
    auto v = Value::makeString("hello");
    EXPECT_EQ(v->type(), ValueType::STRING);
    EXPECT_EQ(v->asString(), "hello");
}

TEST(ValueTest, MakeBoolReturnsCorrectType) {
    auto v = Value::makeBool(true);
    EXPECT_EQ(v->type(), ValueType::BOOL);
    EXPECT_TRUE(v->asBool());
}

TEST(ValueTest, MakeVectorReturnsCorrectType) {
    auto v = Value::makeVector({1.0, 2.0, 3.0});
    EXPECT_EQ(v->type(), ValueType::VECTOR);
    EXPECT_EQ(v->asVector().size(), 3u);
    EXPECT_DOUBLE_EQ(v->asVector()[2], 3.0);
}

TEST(ValueTest, MakeNilReturnsCorrectType) {
    auto v = Value::makeNil();
    EXPECT_EQ(v->type(), ValueType::NIL);
}

TEST(ValueTest, AsNumberThrowsOnWrongType) {
    auto v = Value::makeString("hello");
    EXPECT_THROW(v->asNumber(), EvalError);
}

TEST(ValueTest, AsShapeThrowsOnNumber) {
    auto v = Value::makeNumber(5.0);
    EXPECT_THROW(v->asShape(), EvalError);
}

// Arithmetic
TEST(ValueTest, AddNumbers) {
    auto a = Value::makeNumber(5.0);
    auto b = Value::makeNumber(3.0);
    auto result = a->add(b);
    EXPECT_DOUBLE_EQ(result->asNumber(), 8.0);
}

TEST(ValueTest, DivideNumbers) {
    auto a = Value::makeNumber(21.0);
    auto b = Value::makeNumber(2.0);
    auto result = a->divide(b);
    EXPECT_DOUBLE_EQ(result->asNumber(), 10.5);
}

TEST(ValueTest, ModuloNumbers) {
    auto a = Value::makeNumber(10.0);
    auto b = Value::makeNumber(3.0);
    auto result = a->modulo(b);
    EXPECT_DOUBLE_EQ(result->asNumber(), 1.0);
}

TEST(ValueTest, DivisionByZeroThrows) {
    auto a = Value::makeNumber(5.0);
    auto b = Value::makeNumber(0.0);
    EXPECT_THROW(a->divide(b), EvalError);
}

TEST(ValueTest, VectorAddition) {
    auto a = Value::makeVector({1.0, 2.0, 3.0});
    auto b = Value::makeVector({4.0, 5.0, 6.0});
    auto result = a->add(b);
    EXPECT_EQ(result->type(), ValueType::VECTOR);
    auto& v = result->asVector();
    EXPECT_DOUBLE_EQ(v[0], 5.0);
    EXPECT_DOUBLE_EQ(v[1], 7.0);
    EXPECT_DOUBLE_EQ(v[2], 9.0);
}

TEST(ValueTest, VectorSubtraction) {
    auto a = Value::makeVector({4.0, 5.0, 6.0});
    auto b = Value::makeVector({1.0, 2.0, 3.0});
    auto result = a->subtract(b);
    auto& v = result->asVector();
    EXPECT_DOUBLE_EQ(v[0], 3.0);
    EXPECT_DOUBLE_EQ(v[1], 3.0);
    EXPECT_DOUBLE_EQ(v[2], 3.0);
}

TEST(ValueTest, ScalarMultiplyVector) {
    auto scalar = Value::makeNumber(2.0);
    auto vec = Value::makeVector({1.0, 2.0, 3.0});
    auto result = scalar->multiply(vec);
    auto& v = result->asVector();
    EXPECT_DOUBLE_EQ(v[0], 2.0);
    EXPECT_DOUBLE_EQ(v[1], 4.0);
    EXPECT_DOUBLE_EQ(v[2], 6.0);
}

TEST(ValueTest, NegateNumber) {
    auto a = Value::makeNumber(5.0);
    auto result = a->negate();
    EXPECT_DOUBLE_EQ(result->asNumber(), -5.0);
}

TEST(ValueTest, NegateVector) {
    auto a = Value::makeVector({1.0, -2.0, 3.0});
    auto result = a->negate();
    auto& v = result->asVector();
    EXPECT_DOUBLE_EQ(v[0], -1.0);
    EXPECT_DOUBLE_EQ(v[1], 2.0);
    EXPECT_DOUBLE_EQ(v[2], -3.0);
}

TEST(ValueTest, TypeMismatchAddThrows) {
    auto a = Value::makeNumber(5.0);
    auto b = Value::makeString("hello");
    EXPECT_THROW(a->add(b), EvalError);
}

TEST(ValueTest, TypeMismatchSubtractThrows) {
    auto a = Value::makeString("hello");
    auto b = Value::makeNumber(5.0);
    EXPECT_THROW(a->subtract(b), EvalError);
}

TEST(ValueTest, IsTruthyZeroIsFalse) {
    EXPECT_FALSE(Value::makeNumber(0.0)->isTruthy());
}

TEST(ValueTest, IsTruthyNonZeroIsTrue) {
    EXPECT_TRUE(Value::makeNumber(1.0)->isTruthy());
}

TEST(ValueTest, IsTruthyNilIsFalse) {
    EXPECT_FALSE(Value::makeNil()->isTruthy());
}

TEST(ValueTest, ToStringNumber) {
    auto v = Value::makeNumber(42.0);
    EXPECT_FALSE(v->toString().empty());
}

TEST(ValueTest, ToStringVector) {
    auto v = Value::makeVector({1.0, 2.0});
    std::string s = v->toString();
    EXPECT_NE(s.find('['), std::string::npos);
}

TEST(ValueTest, ToStringNil) {
    EXPECT_EQ(Value::makeNil()->toString(), "nil");
}

TEST(ValueTest, TypeName) {
    EXPECT_EQ(Value::makeNumber(0)->typeName(), "number");
    EXPECT_EQ(Value::makeString("")->typeName(), "string");
    EXPECT_EQ(Value::makeBool(true)->typeName(), "bool");
    EXPECT_EQ(Value::makeVector({})->typeName(), "vector");
    EXPECT_EQ(Value::makeNil()->typeName(), "nil");
}

// =============================================================================
// Phase 2 — New Value Types
// =============================================================================

TEST(ValueTest, MakeFaceRefType) {
    auto v = Value::makeFaceRef(nullptr);
    EXPECT_EQ(v->type(), ValueType::FACE_REF);
    EXPECT_EQ(v->typeName(), "face_ref");
}

TEST(ValueTest, MakeFaceSelectorType) {
    auto v = Value::makeFaceSelector(nullptr);
    EXPECT_EQ(v->type(), ValueType::FACE_SELECTOR);
    EXPECT_EQ(v->typeName(), "face_selector");
}

TEST(ValueTest, MakeWorkplaneType) {
    auto v = Value::makeWorkplane(nullptr);
    EXPECT_EQ(v->type(), ValueType::WORKPLANE);
    EXPECT_EQ(v->typeName(), "workplane");
}

TEST(ValueTest, MakeSketchType) {
    auto v = Value::makeSketch(nullptr);
    EXPECT_EQ(v->type(), ValueType::SKETCH);
    EXPECT_EQ(v->typeName(), "sketch");
}

TEST(ValueTest, MakeEdgeSelectorType) {
    auto v = Value::makeEdgeSelector(nullptr);
    EXPECT_EQ(v->type(), ValueType::EDGE_SELECTOR);
    EXPECT_EQ(v->typeName(), "edge_selector");
}

TEST(ValueTest, NewTypesToString) {
    EXPECT_EQ(Value::makeFaceRef(nullptr)->toString(), "<face_ref>");
    EXPECT_EQ(Value::makeFaceSelector(nullptr)->toString(), "<face_selector>");
    EXPECT_EQ(Value::makeWorkplane(nullptr)->toString(), "<workplane>");
    EXPECT_EQ(Value::makeSketch(nullptr)->toString(), "<sketch>");
    EXPECT_EQ(Value::makeEdgeSelector(nullptr)->toString(), "<edge_selector>");
}

TEST(ValueTest, NewTypesIsTruthyNull) {
    EXPECT_FALSE(Value::makeFaceRef(nullptr)->isTruthy());
    EXPECT_FALSE(Value::makeFaceSelector(nullptr)->isTruthy());
    EXPECT_FALSE(Value::makeWorkplane(nullptr)->isTruthy());
    EXPECT_FALSE(Value::makeSketch(nullptr)->isTruthy());
    EXPECT_FALSE(Value::makeEdgeSelector(nullptr)->isTruthy());
}

TEST(ValueTest, AsFaceRefThrowsOnWrongType) {
    EXPECT_THROW(Value::makeNumber(5)->asFaceRef(), EvalError);
}

TEST(ValueTest, AsFaceSelectorThrowsOnWrongType) {
    EXPECT_THROW(Value::makeNumber(5)->asFaceSelector(), EvalError);
}

TEST(ValueTest, AsWorkplaneThrowsOnWrongType) {
    EXPECT_THROW(Value::makeNumber(5)->asWorkplane(), EvalError);
}

TEST(ValueTest, AsSketchThrowsOnWrongType) {
    EXPECT_THROW(Value::makeNumber(5)->asSketch(), EvalError);
}

TEST(ValueTest, AsEdgeSelectorThrowsOnWrongType) {
    EXPECT_THROW(Value::makeNumber(5)->asEdgeSelector(), EvalError);
}
