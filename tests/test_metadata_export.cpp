#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"
#include "Evaluator.h"
#include "ShapeRegistry.h"
#include "Shape.h"
#include "Color.h"
#include "ManifestExporter.h"
#include "StlExporter.h"
#include "Error.h"

using namespace opendcad;
using namespace OpenDCAD;

class MetadataExportTest : public ::testing::Test {
protected:
    void SetUp() override {
        ShapeRegistry::instance().registerDefaults();
        tmpDir_ = std::filesystem::temp_directory_path() / "opendcad_test";
        std::filesystem::create_directories(tmpDir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmpDir_);
    }

    std::unique_ptr<antlr4::ANTLRInputStream> input_;
    std::unique_ptr<OpenDCADLexer> lexer_;
    std::unique_ptr<antlr4::CommonTokenStream> tokens_;
    std::unique_ptr<OpenDCADParser> parser_;
    std::filesystem::path tmpDir_;

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

    std::string readFile(const std::string& path) {
        std::ifstream f(path);
        return std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    }
};

// =============================================================================
// Tag Methods
// =============================================================================

TEST_F(MetadataExportTest, TagOnShape) {
    auto evaluator = parseAndEvaluate(R"(
        let b = box(10, 10, 10).tag("enclosure");
        export b as part;
    )");
    ASSERT_EQ(evaluator.exports().size(), 1u);
    ASSERT_EQ(evaluator.exports()[0].shapes.size(), 1u);
    EXPECT_TRUE(evaluator.exports()[0].shapes[0]->hasTag("enclosure"));
    EXPECT_FALSE(evaluator.exports()[0].shapes[0]->hasTag("other"));
}

TEST_F(MetadataExportTest, MultipleTags) {
    auto evaluator = parseAndEvaluate(R"(
        let b = box(10, 10, 10).tag("part").tag("metal");
        export b as part;
    )");
    ASSERT_EQ(evaluator.exports().size(), 1u);
    auto& shape = evaluator.exports()[0].shapes[0];
    EXPECT_TRUE(shape->hasTag("part"));
    EXPECT_TRUE(shape->hasTag("metal"));
    EXPECT_EQ(shape->tags().size(), 2u);
}

TEST_F(MetadataExportTest, TagsMethodReturnsList) {
    auto evaluator = parseAndEvaluate(R"(
        let b = box(10, 10, 10).tag("a").tag("b");
        let t = b.tags();
        export b as part;
    )");
    auto tags = evaluator.environment()->lookup("t");
    EXPECT_EQ(tags->type(), ValueType::LIST);
    EXPECT_EQ(tags->listLength(), 2);
    EXPECT_EQ(tags->listGet(0)->asString(), "a");
    EXPECT_EQ(tags->listGet(1)->asString(), "b");
}

TEST_F(MetadataExportTest, HasTagMethod) {
    auto evaluator = parseAndEvaluate(R"(
        let b = box(10, 10, 10).tag("test");
        let yes = b.hasTag("test");
        let no = b.hasTag("other");
    )");
    EXPECT_TRUE(evaluator.environment()->lookup("yes")->asBool());
    EXPECT_FALSE(evaluator.environment()->lookup("no")->asBool());
}

TEST_F(MetadataExportTest, TagRequiresString) {
    EXPECT_THROW(parseAndEvaluate(R"(
        let b = box(10, 10, 10).tag(42);
    )"), EvalError);
}

// =============================================================================
// Assembly Exports (list of shapes)
// =============================================================================

TEST_F(MetadataExportTest, ExportSingleShape) {
    auto evaluator = parseAndEvaluate(R"(
        let b = box(10, 10, 10);
        export b as part;
    )");
    ASSERT_EQ(evaluator.exports().size(), 1u);
    EXPECT_EQ(evaluator.exports()[0].name, "part");
    EXPECT_EQ(evaluator.exports()[0].shapes.size(), 1u);
}

TEST_F(MetadataExportTest, ExportListOfShapes) {
    auto evaluator = parseAndEvaluate(R"(
        let a = box(10, 10, 10);
        let b = cylinder(5, 20);
        export [a, b] as assembly;
    )");
    ASSERT_EQ(evaluator.exports().size(), 1u);
    EXPECT_EQ(evaluator.exports()[0].name, "assembly");
    EXPECT_EQ(evaluator.exports()[0].shapes.size(), 2u);
}

TEST_F(MetadataExportTest, ExportListPreservesMetadata) {
    auto evaluator = parseAndEvaluate(R"(
        let a = box(10, 10, 10).color(RED).tag("body");
        let b = cylinder(5, 20).material(STEEL).tag("pin");
        export [a, b] as device;
    )");
    ASSERT_EQ(evaluator.exports().size(), 1u);
    const auto& shapes = evaluator.exports()[0].shapes;
    ASSERT_EQ(shapes.size(), 2u);

    // Shape a: has color and tag
    EXPECT_NE(shapes[0]->color(), nullptr);
    EXPECT_TRUE(shapes[0]->hasTag("body"));
    EXPECT_EQ(shapes[0]->material(), nullptr);

    // Shape b: has material and tag
    EXPECT_NE(shapes[1]->material(), nullptr);
    EXPECT_TRUE(shapes[1]->hasTag("pin"));
    EXPECT_EQ(shapes[1]->color(), nullptr);
}

TEST_F(MetadataExportTest, ExportNonShapeInListFails) {
    EXPECT_THROW(parseAndEvaluate(R"(
        let a = box(10, 10, 10);
        export [a, 42] as bad;
    )"), EvalError);
}

TEST_F(MetadataExportTest, ExportEmptyListFails) {
    EXPECT_THROW(parseAndEvaluate(R"(
        export [] as empty;
    )"), EvalError);
}

TEST_F(MetadataExportTest, ExportNonShapeFails) {
    EXPECT_THROW(parseAndEvaluate(R"(
        export 42 as bad;
    )"), EvalError);
}

// =============================================================================
// JSON Manifest
// =============================================================================

TEST_F(MetadataExportTest, ManifestWritesSingleShape) {
    auto shape = Shape::createBox(10, 10, 10);
    shape->setColor(std::make_shared<Color>(Color::fromRGB(255, 0, 0)));
    shape->addTag("test");

    std::string path = (tmpDir_ / "single.json").string();
    EXPECT_TRUE(ManifestExporter::write({shape}, "single", path));

    std::string json = readFile(path);
    EXPECT_NE(json.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"export\": \"single\""), std::string::npos);
    EXPECT_NE(json.find("\"index\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"color\""), std::string::npos);
    EXPECT_NE(json.find("\"tags\": [\"test\"]"), std::string::npos);
}

TEST_F(MetadataExportTest, ManifestWritesAssembly) {
    auto a = Shape::createBox(10, 10, 10);
    a->setColor(std::make_shared<Color>(Color::fromRGB(255, 0, 0)));
    a->addTag("body");

    auto b = Shape::createCylinder(5, 20);
    auto mat = std::make_shared<Material>();
    mat->preset = "steel";
    mat->metallic = 0.9;
    mat->roughness = 0.3;
    b->setMaterial(mat);
    b->addTag("pin");

    std::string path = (tmpDir_ / "assembly.json").string();
    EXPECT_TRUE(ManifestExporter::write({a, b}, "device", path));

    std::string json = readFile(path);
    EXPECT_NE(json.find("\"export\": \"device\""), std::string::npos);
    EXPECT_NE(json.find("\"index\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"index\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"tags\": [\"body\"]"), std::string::npos);
    EXPECT_NE(json.find("\"tags\": [\"pin\"]"), std::string::npos);
    EXPECT_NE(json.find("\"preset\": \"steel\""), std::string::npos);
}

TEST_F(MetadataExportTest, ManifestNoMetadata) {
    auto shape = Shape::createBox(10, 10, 10);

    std::string path = (tmpDir_ / "bare.json").string();
    EXPECT_TRUE(ManifestExporter::write({shape}, "bare", path));

    std::string json = readFile(path);
    EXPECT_NE(json.find("\"index\": 0"), std::string::npos);
    // No color/material/tags keys when absent
    EXPECT_EQ(json.find("\"color\""), std::string::npos);
    EXPECT_EQ(json.find("\"material\""), std::string::npos);
    EXPECT_EQ(json.find("\"tags\""), std::string::npos);
}

// =============================================================================
// STL Assembly
// =============================================================================

TEST_F(MetadataExportTest, StlAssemblyWrite) {
    auto a = Shape::createBox(10, 10, 10);
    auto b = Shape::createCylinder(5, 20);

    std::string path = (tmpDir_ / "assembly.stl").string();
    auto result = StlExporter::writeAssembly({a, b}, path, {0.1, 0.5, true});
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.triangleCount, 0u);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_GT(std::filesystem::file_size(path), 0u);
}

TEST_F(MetadataExportTest, StlSingleShape) {
    auto shape = Shape::createBox(10, 10, 10);

    std::string path = (tmpDir_ / "single.stl").string();
    auto result = StlExporter::writeAssembly({shape}, path, {0.1, 0.5, true});
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(std::filesystem::exists(path));
}

// =============================================================================
// Color + Material on Shape (direct API)
// =============================================================================

TEST_F(MetadataExportTest, ShapeColorMetadata) {
    auto shape = Shape::createBox(10, 10, 10);
    EXPECT_EQ(shape->color(), nullptr);

    auto color = std::make_shared<Color>(Color::fromRGB(128, 64, 32));
    shape->setColor(color);
    EXPECT_NE(shape->color(), nullptr);
    EXPECT_NEAR(shape->color()->r, 128.0/255.0, 0.01);
}

TEST_F(MetadataExportTest, ShapeMaterialMetadata) {
    auto shape = Shape::createBox(10, 10, 10);
    EXPECT_EQ(shape->material(), nullptr);

    auto mat = std::make_shared<Material>();
    mat->preset = "aluminum";
    mat->metallic = 0.9;
    mat->roughness = 0.4;
    shape->setMaterial(mat);
    EXPECT_NE(shape->material(), nullptr);
    EXPECT_EQ(shape->material()->preset, "aluminum");
}

// =============================================================================
// Mixed metadata via DSL
// =============================================================================

TEST_F(MetadataExportTest, ColorMaterialTagViaDSL) {
    auto evaluator = parseAndEvaluate(R"(
        let part = box(20, 20, 10)
            .color(RED)
            .material(STEEL)
            .tag("housing")
            .tag("structural");
        export part as component;
    )");

    ASSERT_EQ(evaluator.exports().size(), 1u);
    auto& shape = evaluator.exports()[0].shapes[0];
    EXPECT_NE(shape->color(), nullptr);
    EXPECT_NE(shape->material(), nullptr);
    EXPECT_TRUE(shape->hasTag("housing"));
    EXPECT_TRUE(shape->hasTag("structural"));
    EXPECT_EQ(shape->tags().size(), 2u);
}
