#include <gtest/gtest.h>
#include "CliOptions.h"

using namespace opendcad;

// Helper to build argv from strings
static CliOptions parse(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    for (auto a : args) argv.push_back(const_cast<char*>(a));
    return parseArgs(static_cast<int>(argv.size()), argv.data());
}

// =============================================================================
// Basic argument parsing
// =============================================================================

TEST(CliTest, NoArgs) {
    auto opts = parse({"opendcad"});
    EXPECT_TRUE(opts.showHelp);
    EXPECT_TRUE(opts.inputFile.empty());
}

TEST(CliTest, InputFileOnly) {
    auto opts = parse({"opendcad", "model.dcad"});
    EXPECT_EQ(opts.inputFile, "model.dcad");
    EXPECT_EQ(opts.outputDir, "build");
    EXPECT_TRUE(opts.fmtStep);
    EXPECT_TRUE(opts.fmtStl);
    EXPECT_FALSE(opts.quiet);
    EXPECT_FALSE(opts.dryRun);
    EXPECT_FALSE(opts.listExports);
    EXPECT_FALSE(opts.showHelp);
}

TEST(CliTest, OutputDirShort) {
    auto opts = parse({"opendcad", "model.dcad", "-o", "out"});
    EXPECT_EQ(opts.outputDir, "out");
}

TEST(CliTest, OutputDirLong) {
    auto opts = parse({"opendcad", "model.dcad", "--output", "dist"});
    EXPECT_EQ(opts.outputDir, "dist");
}

// =============================================================================
// Format flags
// =============================================================================

TEST(CliTest, FmtStepOnly) {
    auto opts = parse({"opendcad", "model.dcad", "--fmt", "step"});
    EXPECT_TRUE(opts.fmtStep);
    EXPECT_FALSE(opts.fmtStl);
}

TEST(CliTest, FmtStlAlsoGetsStep) {
    // STEP + JSON always written per design
    auto opts = parse({"opendcad", "model.dcad", "-f", "stl"});
    EXPECT_TRUE(opts.fmtStep);
    EXPECT_TRUE(opts.fmtStl);
}

TEST(CliTest, FmtBoth) {
    auto opts = parse({"opendcad", "model.dcad", "--fmt", "step,stl"});
    EXPECT_TRUE(opts.fmtStep);
    EXPECT_TRUE(opts.fmtStl);
}

TEST(CliTest, FmtUnknown) {
    auto opts = parse({"opendcad", "model.dcad", "--fmt", "obj"});
    EXPECT_TRUE(opts.showHelp);
}

// =============================================================================
// Quality presets
// =============================================================================

TEST(CliTest, QualityDraft) {
    auto opts = parse({"opendcad", "model.dcad", "-q", "draft"});
    EXPECT_EQ(opts.quality, QualityPreset::Draft);
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.deflection, 0.1);
    EXPECT_DOUBLE_EQ(qp.angle, 0.5);
    EXPECT_FALSE(qp.heal);
}

TEST(CliTest, QualityStandard) {
    auto opts = parse({"opendcad", "model.dcad", "--qual", "standard"});
    EXPECT_EQ(opts.quality, QualityPreset::Standard);
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.deflection, 0.01);
    EXPECT_TRUE(qp.heal);
}

TEST(CliTest, QualityProduction) {
    auto opts = parse({"opendcad", "model.dcad", "-q", "production"});
    EXPECT_EQ(opts.quality, QualityPreset::Production);
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.deflection, 0.001);
    EXPECT_DOUBLE_EQ(qp.angle, 0.05);
    EXPECT_TRUE(qp.heal);
}

TEST(CliTest, QualityUnknown) {
    auto opts = parse({"opendcad", "model.dcad", "-q", "ultra"});
    EXPECT_TRUE(opts.showHelp);
}

// =============================================================================
// Deflection/angle overrides
// =============================================================================

TEST(CliTest, DeflectionOverride) {
    auto opts = parse({"opendcad", "model.dcad", "--deflection", "0.005"});
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.deflection, 0.005);
    EXPECT_DOUBLE_EQ(qp.angle, 0.1);  // standard default
}

TEST(CliTest, AngleOverride) {
    auto opts = parse({"opendcad", "model.dcad", "--angle", "0.08"});
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.angle, 0.08);
    EXPECT_DOUBLE_EQ(qp.deflection, 0.01);  // standard default
}

TEST(CliTest, BothOverrides) {
    auto opts = parse({"opendcad", "model.dcad", "-q", "draft", "--deflection", "0.05", "--angle", "0.2"});
    auto qp = opts.getQualityParams();
    EXPECT_DOUBLE_EQ(qp.deflection, 0.05);
    EXPECT_DOUBLE_EQ(qp.angle, 0.2);
    EXPECT_FALSE(qp.heal);  // still draft
}

// =============================================================================
// Export filter
// =============================================================================

TEST(CliTest, ExportFilterSingle) {
    auto opts = parse({"opendcad", "model.dcad", "-e", "lid"});
    EXPECT_EQ(opts.exportNames.size(), 1u);
    EXPECT_TRUE(opts.exportNames.count("lid"));
}

TEST(CliTest, ExportFilterMultiple) {
    auto opts = parse({"opendcad", "model.dcad", "--export", "lid", "-e", "base"});
    EXPECT_EQ(opts.exportNames.size(), 2u);
    EXPECT_TRUE(opts.exportNames.count("lid"));
    EXPECT_TRUE(opts.exportNames.count("base"));
}

TEST(CliTest, ExportFilterEmpty) {
    auto opts = parse({"opendcad", "model.dcad"});
    EXPECT_TRUE(opts.exportNames.empty());
}

// =============================================================================
// Flags
// =============================================================================

TEST(CliTest, ListExports) {
    auto opts = parse({"opendcad", "model.dcad", "--list-exports"});
    EXPECT_TRUE(opts.listExports);
}

TEST(CliTest, DryRun) {
    auto opts = parse({"opendcad", "model.dcad", "--dry-run"});
    EXPECT_TRUE(opts.dryRun);
}

TEST(CliTest, Quiet) {
    auto opts = parse({"opendcad", "model.dcad", "--quiet"});
    EXPECT_TRUE(opts.quiet);
}

TEST(CliTest, Version) {
    auto opts = parse({"opendcad", "--version"});
    EXPECT_TRUE(opts.showVersion);
}

TEST(CliTest, Help) {
    auto opts = parse({"opendcad", "--help"});
    EXPECT_TRUE(opts.showHelp);
}

TEST(CliTest, HelpShort) {
    auto opts = parse({"opendcad", "-h"});
    EXPECT_TRUE(opts.showHelp);
}

// =============================================================================
// Error cases
// =============================================================================

TEST(CliTest, UnknownFlag) {
    auto opts = parse({"opendcad", "model.dcad", "--bogus"});
    EXPECT_TRUE(opts.showHelp);
}

TEST(CliTest, MissingOutputValue) {
    auto opts = parse({"opendcad", "model.dcad", "-o"});
    EXPECT_TRUE(opts.showHelp);
}

TEST(CliTest, ExtraPositional) {
    auto opts = parse({"opendcad", "a.dcad", "b.dcad"});
    EXPECT_TRUE(opts.showHelp);
}

// =============================================================================
// Combined flags
// =============================================================================

TEST(CliTest, FullCommand) {
    auto opts = parse({"opendcad", "model.dcad", "-o", "out", "-f", "step",
                        "-q", "production", "-e", "body", "--quiet"});
    EXPECT_EQ(opts.inputFile, "model.dcad");
    EXPECT_EQ(opts.outputDir, "out");
    EXPECT_TRUE(opts.fmtStep);
    EXPECT_FALSE(opts.fmtStl);
    EXPECT_EQ(opts.quality, QualityPreset::Production);
    EXPECT_TRUE(opts.exportNames.count("body"));
    EXPECT_TRUE(opts.quiet);
}
