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
    EXPECT_TRUE(evaluator.exports()[0].shapes[0]->isValid());
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

// =============================================================================
// Phase 2 — Face-Relative Modeling
// =============================================================================

TEST_F(EvaluatorTest, FaceRefFromSelector) {
    auto evaluator = parseAndEvaluate("let f = box(40, 30, 10).face(\">Z\");");
    auto val = evaluator.environment()->lookup("f");
    EXPECT_EQ(val->type(), ValueType::FACE_REF);
}

TEST_F(EvaluatorTest, FaceSelectorCount) {
    auto evaluator = parseAndEvaluate("let n = box(40, 30, 10).faces().count();");
    auto val = evaluator.environment()->lookup("n");
    EXPECT_EQ(val->type(), ValueType::NUMBER);
    EXPECT_DOUBLE_EQ(val->asNumber(), 6.0);
}

TEST_F(EvaluatorTest, SketchCircleExtrude) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().circle(8).extrude(15);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, SketchCutThrough) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().circle(5).cutThrough();");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, SketchCutBlind) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().rect(20, 15).cutBlind(3);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, EdgeFilletPipeline) {
    auto evaluator = parseAndEvaluate(
        "let r = box(20, 20, 20).edges().vertical().fillet(2);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ChainedFaceOperations) {
    auto evaluator = parseAndEvaluate(
        "let base = box(40, 30, 10);\n"
        "let withBoss = base.face(\">Z\").draw().circle(8).extrude(15);\n"
        "let withHole = withBoss.face(\">Z\").draw().circle(5).cutThrough();\n"
    );
    auto val = evaluator.environment()->lookup("withHole");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, FaceRefIsPlanar) {
    auto evaluator = parseAndEvaluate(
        "let p = box(10, 10, 10).face(\">Z\").isPlanar();");
    auto val = evaluator.environment()->lookup("p");
    EXPECT_EQ(val->type(), ValueType::BOOL);
    EXPECT_TRUE(val->asBool());
}

TEST_F(EvaluatorTest, FaceRefArea) {
    auto evaluator = parseAndEvaluate(
        "let a = box(40, 30, 10).face(\">Z\").area();");
    auto val = evaluator.environment()->lookup("a");
    EXPECT_EQ(val->type(), ValueType::NUMBER);
    EXPECT_NEAR(val->asNumber(), 1200.0, 1.0);
}

TEST_F(EvaluatorTest, WorkplaneOffset) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").workplane().offset(5).draw().circle(5).extrude(10);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_EQ(val->type(), ValueType::SHAPE);
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, UnknownMethodOnFaceRefThrows) {
    EXPECT_THROW(
        parseAndEvaluate("let x = box(10,10,10).face(\">Z\").nonexistent();"),
        EvalError);
}

TEST_F(EvaluatorTest, InvalidFaceSelectorStringThrows) {
    EXPECT_THROW(
        parseAndEvaluate("let x = box(10,10,10).face(\"invalid\");"),
        EvalError);
}

// =============================================================================
// Phase 2b — Sketch Enhancements
// =============================================================================

TEST_F(EvaluatorTest, SlotExtrude) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().slot(20, 8).extrude(5);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, RevolveSketch) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 40, 10).face(\">Z\").draw().circle(2, 0, 12).revolve(360);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, Fillet2DSketch) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().rect(20, 15).fillet2D(2).extrude(5);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, Chamfer2DSketch) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().rect(20, 15).chamfer2D(2).extrude(5);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, OffsetSketch) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").draw().rect(10, 10).offset(2).extrude(5);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

// =============================================================================
// Phase 2b — Parametric Holes
// =============================================================================

TEST_F(EvaluatorTest, HoleBlind) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").hole(6, 5);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, ThroughHole) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 10).face(\">Z\").throughHole(6);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, Counterbore) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 20).face(\">Z\").counterbore(5, 10, 3, 15);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, Countersink) {
    auto evaluator = parseAndEvaluate(
        "let r = box(40, 30, 20).face(\">Z\").countersink(5, 10, 90, 15);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

// =============================================================================
// Phase 2b — Feature Patterns
// =============================================================================

TEST_F(EvaluatorTest, LinearPattern) {
    auto evaluator = parseAndEvaluate(
        "let r = cylinder(3, 20).linearPattern([15, 0, 0], 4);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, CircularPattern) {
    auto evaluator = parseAndEvaluate(
        "let post = box(5, 5, 20).translate([15, 0, 0]);\n"
        "let r = post.circularPattern([0, 0, 1], 4);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, MirrorFeature) {
    auto evaluator = parseAndEvaluate(
        "let r = box(10, 10, 10).translate([20, 0, 0]).mirrorFeature([1, 0, 0]);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

// =============================================================================
// Phase 2b — Advanced Operations
// =============================================================================

TEST_F(EvaluatorTest, Draft) {
    auto evaluator = parseAndEvaluate(
        "let r = box(20, 20, 30).draft(5, [0, 0, 1]);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

TEST_F(EvaluatorTest, SplitAt) {
    auto evaluator = parseAndEvaluate(
        "let r = box(20, 20, 20).splitAt([0, 0, 0], [1, 0, 0]);");
    auto val = evaluator.environment()->lookup("r");
    EXPECT_TRUE(val->asShape()->isValid());
}

// =============================================================================
// Phase 2b — Advanced Selection
// =============================================================================

TEST_F(EvaluatorTest, NearestToFace) {
    auto evaluator = parseAndEvaluate(
        "let f = box(20, 20, 20).faces().nearestTo([0, 0, 100]);\n"
        "let n = f.normal();");
    auto val = evaluator.environment()->lookup("n");
    EXPECT_EQ(val->type(), ValueType::VECTOR);
    EXPECT_NEAR(val->asVector()[2], 1.0, 0.01);
}

TEST_F(EvaluatorTest, FarthestFromFace) {
    auto evaluator = parseAndEvaluate(
        "let f = box(20, 20, 20).faces().farthestFrom([0, 0, -100]);\n"
        "let n = f.normal();");
    auto val = evaluator.environment()->lookup("n");
    EXPECT_EQ(val->type(), ValueType::VECTOR);
    EXPECT_NEAR(val->asVector()[2], 1.0, 0.01);
}

TEST_F(EvaluatorTest, LongestEdge) {
    auto evaluator = parseAndEvaluate(
        "let n = box(40, 30, 10).edges().longest().count();");
    auto val = evaluator.environment()->lookup("n");
    EXPECT_DOUBLE_EQ(val->asNumber(), 1.0);
}

TEST_F(EvaluatorTest, ShortestEdge) {
    auto evaluator = parseAndEvaluate(
        "let n = box(40, 30, 10).edges().shortest().count();");
    auto val = evaluator.environment()->lookup("n");
    EXPECT_DOUBLE_EQ(val->asNumber(), 1.0);
}

TEST_F(EvaluatorTest, AreaFilters) {
    auto evaluator = parseAndEvaluate(
        "let big = box(40, 30, 10).faces().areaGreaterThan(500).count();\n"
        "let small = box(40, 30, 10).faces().areaLessThan(350).count();");
    EXPECT_DOUBLE_EQ(evaluator.environment()->lookup("big")->asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(evaluator.environment()->lookup("small")->asNumber(), 2.0);
}

TEST_F(EvaluatorTest, ZeroCountPatternNoOp) {
    auto evaluator = parseAndEvaluate(
        "let r = box(10, 10, 10).linearPattern([10, 0, 0], 1);\n"
        "let n = r.faces().count();");
    EXPECT_DOUBLE_EQ(evaluator.environment()->lookup("n")->asNumber(), 6.0);
}
