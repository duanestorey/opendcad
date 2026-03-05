#include <gtest/gtest.h>
#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"
#include "Evaluator.h"
#include "ShapeRegistry.h"
#include "Error.h"

using namespace opendcad;
using namespace OpenDCAD;

class EvaluatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        ShapeRegistry::instance().registerDefaults();
    }

    std::unique_ptr<antlr4::ANTLRInputStream> input_;
    std::unique_ptr<OpenDCADLexer> lexer_;
    std::unique_ptr<antlr4::CommonTokenStream> tokens_;
    std::unique_ptr<OpenDCADParser> parser_;

    Evaluator parseAndEvaluate(const std::string& src) {
        input_ = std::make_unique<antlr4::ANTLRInputStream>(src);
        lexer_ = std::make_unique<OpenDCADLexer>(input_.get());
        tokens_ = std::make_unique<antlr4::CommonTokenStream>(lexer_.get());
        parser_ = std::make_unique<OpenDCADParser>(tokens_.get());

        auto* tree = parser_->program();
        EXPECT_EQ(parser_->getNumberOfSyntaxErrors(), 0u);

        Evaluator evaluator;
        evaluator.evaluate(tree, "<test>");
        return evaluator;
    }
};

// =============================================================================
// Phase 0 — Existing Tests
// =============================================================================

TEST_F(EvaluatorTest, LetNumber) {
    auto evaluator = parseAndEvaluate("let x = 42;");
    auto val = evaluator.environment()->lookup("x");
    EXPECT_EQ(val->type(), ValueType::NUMBER);
    EXPECT_DOUBLE_EQ(val->asNumber(), 42.0);
}

TEST_F(EvaluatorTest, LetArithmetic) {
    auto evaluator = parseAndEvaluate("let x = 21/2;");
    auto val = evaluator.environment()->lookup("x");
    EXPECT_DOUBLE_EQ(val->asNumber(), 10.5);
}

TEST_F(EvaluatorTest, LetBoxFactory) {
    auto evaluator = parseAndEvaluate("let b = box(10, 20, 30);");
    auto val = evaluator.environment()->lookup("b");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, LetChainWithMethodCalls) {
    auto evaluator = parseAndEvaluate("let c = cylinder(5, 10).translate([0,0,5]);");
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, LetAlias) {
    auto evaluator = parseAndEvaluate("let b = box(10, 20, 30);\nlet c = b;");
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
}

TEST_F(EvaluatorTest, ExportShape) {
    auto evaluator = parseAndEvaluate("export box(1,1,1) as test;");
    EXPECT_EQ(evaluator.exports().size(), 1u);
    EXPECT_EQ(evaluator.exports()[0].name, "test");
    EXPECT_TRUE(evaluator.exports()[0].shape->isValid());
}

TEST_F(EvaluatorTest, UndefinedVariableThrows) {
    EXPECT_THROW(parseAndEvaluate("let x = missing;"), EvalError);
}

TEST_F(EvaluatorTest, UnknownFactoryThrows) {
    EXPECT_THROW(parseAndEvaluate("let x = nonexistent_shape(5);"), EvalError);
}

TEST_F(EvaluatorTest, VectorLiteral) {
    auto evaluator = parseAndEvaluate("let v = [1, 2, 3];");
    auto val = evaluator.environment()->lookup("v");
    EXPECT_EQ(val->type(), ValueType::VECTOR);
    EXPECT_EQ(val->asVector().size(), 3u);
}

TEST_F(EvaluatorTest, MethodChainOnVariable) {
    auto evaluator = parseAndEvaluate(
        "let b = box(10, 20, 30);\n"
        "let c = b.translate([1, 2, 3]);\n"
    );
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, MultipleExports) {
    auto evaluator = parseAndEvaluate(
        "let b = box(10, 20, 30);\n"
        "export b as first;\n"
        "export b as second;\n"
    );
    EXPECT_EQ(evaluator.exports().size(), 2u);
    EXPECT_EQ(evaluator.exports()[0].name, "first");
    EXPECT_EQ(evaluator.exports()[1].name, "second");
}

// =============================================================================
// Phase 1 — New Factory Tests
// =============================================================================

TEST_F(EvaluatorTest, SphereFactory) {
    auto evaluator = parseAndEvaluate("let s = sphere(10);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ConeFactory) {
    auto evaluator = parseAndEvaluate("let c = cone(10, 5, 20);");
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, WedgeFactory) {
    auto evaluator = parseAndEvaluate("let w = wedge(20, 10, 15, 5);");
    auto val = evaluator.environment()->lookup("w");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, CircleFactory) {
    auto evaluator = parseAndEvaluate("let c = circle(5);");
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, RectangleFactory) {
    auto evaluator = parseAndEvaluate("let r = rectangle(10, 20);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

// =============================================================================
// Phase 1 — New Method Tests
// =============================================================================

TEST_F(EvaluatorTest, IntersectMethod) {
    auto evaluator = parseAndEvaluate(
        "let a = box(20, 20, 20);\n"
        "let b = box(10, 10, 10);\n"
        "let c = a.intersect(b);\n"
    );
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ChamferMethod) {
    auto evaluator = parseAndEvaluate("let c = box(20, 20, 20).chamfer(1);");
    auto val = evaluator.environment()->lookup("c");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, LinearExtrudeMethod) {
    auto evaluator = parseAndEvaluate("let s = circle(5).linear_extrude(10);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ScaleUniformMethod) {
    auto evaluator = parseAndEvaluate("let s = box(10, 10, 10).scale(2);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ScaleNonUniformMethod) {
    auto evaluator = parseAndEvaluate("let s = box(10, 10, 10).scale([2, 1, 0.5]);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, MirrorMethod) {
    auto evaluator = parseAndEvaluate("let s = box(10, 10, 10).mirror([0, 0, 1]);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ShellMethod) {
    auto evaluator = parseAndEvaluate("let s = box(20, 20, 20).shell(2);");
    auto val = evaluator.environment()->lookup("s");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}
