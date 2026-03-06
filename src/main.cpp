#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "antlr4-runtime.h"
#include "OpenDCADLexer.h"
#include "OpenDCADParser.h"

#include "output.h"
#include "Debug.h"
#include "Error.h"
#include "Evaluator.h"
#include "ShapeRegistry.h"
#include "StepExporter.h"
#include "StlExporter.h"
#include "ManifestExporter.h"
#include "ShapeHealer.h"
#include "CliOptions.h"

#include <Standard_Version.hxx>

using namespace opendcad;
using namespace OpenDCAD;

static void writeLogo() {
    char buf[256];
    snprintf(buf, sizeof buf, "| OpenDCAD version [%s] - OCCT version [%s] |",
             OPENDCAD_VERSION, OCC_VERSION_COMPLETE);

    std::cout << termcolor::green;
    for (size_t i = 0; i < strlen(buf); i++) std::cout << "-";
    std::cout << "\n";

    std::cout << termcolor::green << "| OpenDCAD version ["
              << termcolor::white << OPENDCAD_VERSION << termcolor::green << "]"
              << " - OCCT version ["
              << termcolor::white << OCC_VERSION_COMPLETE
              << termcolor::green << "] |" << termcolor::reset << "\n";

    std::cout << termcolor::green;
    for (size_t i = 0; i < strlen(buf); i++) std::cout << "-";
    std::cout << "\n\n";
}

static std::string loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Failed to open file: " + path);
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
}

static std::string nowISO8601() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

static const char* qualityName(QualityPreset q) {
    switch (q) {
        case QualityPreset::Draft:      return "draft";
        case QualityPreset::Standard:   return "standard";
        case QualityPreset::Production: return "production";
    }
    return "standard";
}

int main(int argc, char* argv[]) {
    auto opts = parseArgs(argc, argv);

    if (opts.showVersion) {
        std::cout << "opendcad " << OPENDCAD_VERSION << "\n";
        return 0;
    }

    if (opts.showHelp || opts.inputFile.empty()) {
        if (!opts.quiet) printUsage(argv[0]);
        return opts.inputFile.empty() ? 1 : 0;
    }

    debugQuiet() = opts.quiet;

    if (!opts.quiet) writeLogo();

    auto totalStart = std::chrono::steady_clock::now();

    // Register built-in shapes and methods
    ShapeRegistry::instance().registerDefaults();

    // Parse
    DEBUG_INFO("Loading script [" << opts.inputFile << "]");
    std::string src = loadFile(opts.inputFile);

    antlr4::ANTLRInputStream input(src);
    OpenDCADLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    OpenDCADParser parser(&tokens);

    DEBUG_INFO("...starting parser");
    auto* tree = parser.program();
    DEBUG_INFO("...parsing complete");

    if (parser.getNumberOfSyntaxErrors() > 0) {
        std::cerr << termcolor::red << "Syntax errors found" << termcolor::reset << "\n";
        return 1;
    }

    // Evaluate
    Evaluator evaluator;
    try {
        evaluator.evaluate(tree, opts.inputFile);
    } catch (const EvalError& e) {
        std::cerr << termcolor::red << "Error: " << e.what() << termcolor::reset << "\n";
        return 1;
    }

    DEBUG_INFO("Evaluation complete - " << evaluator.exports().size() << " export(s)");

    // --list-exports: print names and exit
    if (opts.listExports) {
        for (const auto& entry : evaluator.exports()) {
            std::cout << entry.name << "\n";
        }
        return 0;
    }

    // --dry-run: report what would be built
    if (opts.dryRun) {
        auto qp = opts.getQualityParams();
        for (const auto& entry : evaluator.exports()) {
            if (!opts.exportNames.empty() && opts.exportNames.count(entry.name) == 0)
                continue;
            std::cout << entry.name << ": ";
            std::cout << "step,json";
            if (opts.fmtStl) std::cout << ",stl";
            std::cout << " (" << entry.shapes.size() << " shape(s), "
                      << qualityName(opts.quality) << " quality, "
                      << "deflection=" << qp.deflection << ")\n";
        }
        return 0;
    }

    // Ensure output directory exists
    std::filesystem::create_directories(opts.outputDir);

    auto qp = opts.getQualityParams();
    StlParams stlParams{qp.deflection, qp.angle, true};

    // Export each entry
    int failures = 0;
    for (const auto& entry : evaluator.exports()) {
        // Filter by --export names
        if (!opts.exportNames.empty() && opts.exportNames.count(entry.name) == 0)
            continue;

        // Heal shapes (skip for draft quality)
        std::vector<ShapePtr> outputShapes;
        if (qp.heal) {
            for (const auto& shape : entry.shapes) {
                DEBUG_INFO("Healing shape for export: " << entry.name);
                TopoDS_Shape healed = healShape(shape->getShape());
                auto healedShape = std::make_shared<Shape>(healed);
                if (shape->color()) healedShape->setColor(shape->color());
                if (shape->material()) healedShape->setMaterial(shape->material());
                for (const auto& tag : shape->tags()) healedShape->addTag(tag);
                outputShapes.push_back(healedShape);
            }
        } else {
            outputShapes = entry.shapes;
        }

        std::string stepPath = opts.outputDir + "/" + entry.name + ".step";
        std::string stlPath  = opts.outputDir + "/" + entry.name + ".stl";
        std::string jsonPath = opts.outputDir + "/" + entry.name + ".json";

        // STEP (always written)
        if (!StepExporter::writeWithMetadata(outputShapes, entry.name, stepPath)) {
            std::cerr << termcolor::red << "STEP export failed for " << entry.name
                      << termcolor::reset << "\n";
            ++failures;
            continue;
        }

        // STL (optional)
        std::vector<size_t> triCounts;
        if (opts.fmtStl) {
            auto result = StlExporter::writeAssembly(outputShapes, stlPath, stlParams);
            if (!result.success) {
                std::cerr << termcolor::red << "STL export failed for " << entry.name
                          << termcolor::reset << "\n";
                ++failures;
                continue;
            }
            // Single triangle count for the whole assembly
            triCounts.push_back(result.triangleCount);
        }

        // JSON manifest (always written)
        auto exportEnd = std::chrono::steady_clock::now();
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            exportEnd - totalStart).count();

        BuildMetrics metrics;
        metrics.script = opts.inputFile;
        metrics.timestamp = nowISO8601();
        metrics.durationMs = durationMs;
        metrics.qualityPreset = qualityName(opts.quality);
        metrics.opendcadVersion = OPENDCAD_VERSION;
        metrics.occtVersion = OCC_VERSION_COMPLETE;
        metrics.triangleCounts = std::move(triCounts);

        if (!ManifestExporter::write(outputShapes, entry.name, jsonPath, metrics)) {
            std::cerr << termcolor::red << "Manifest export failed for " << entry.name
                      << termcolor::reset << "\n";
            ++failures;
            continue;
        }

        if (!opts.quiet) {
            std::cout << termcolor::green << "Exported: " << termcolor::white << entry.name
                      << termcolor::reset << " (" << stepPath;
            if (opts.fmtStl) std::cout << ", " << stlPath;
            std::cout << ", " << jsonPath << ")\n";
        }
    }

    return failures > 0 ? 1 : 0;
}
