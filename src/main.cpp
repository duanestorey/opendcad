#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>

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
#include "ShapeHealer.h"

#include <Standard_Version.hxx>

using namespace opendcad;
using namespace OpenDCAD;

static const char VERSION[] = "1.00.00";

static void writeLogo() {
    char buf[256];
    snprintf(buf, sizeof buf, "| OpenDCAD version [%s] - OCCT version [%s] |",
             VERSION, OCC_VERSION_COMPLETE);

    std::cout << termcolor::green;
    for (size_t i = 0; i < strlen(buf); i++) std::cout << "-";
    std::cout << "\n";

    std::cout << termcolor::green << "| OpenDCAD version ["
              << termcolor::white << VERSION << termcolor::green << "]"
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

int main(int argc, char* argv[]) {
    writeLogo();

    if (argc < 2) {
        std::cerr << "Usage: opendcad <input.dcad> [output_dir]\n";
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputDir = argc > 2 ? argv[2] : ".";

    // Ensure output directory exists
    std::filesystem::create_directories(outputDir);

    // Register built-in shapes and methods
    ShapeRegistry::instance().registerDefaults();

    // Parse
    DEBUG_INFO("Loading script [" << inputFile << "]");
    std::string src = loadFile(inputFile);

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
        evaluator.evaluate(tree, inputFile);
    } catch (const EvalError& e) {
        std::cerr << termcolor::red << "Error: " << e.what() << termcolor::reset << "\n";
        return 1;
    }

    DEBUG_INFO("Evaluation complete - " << evaluator.exports().size() << " export(s)");

    // Export each shape
    for (const auto& entry : evaluator.exports()) {
        DEBUG_INFO("Healing shape for export: " << entry.name);
        TopoDS_Shape healed = healShape(entry.shape->getShape());

        std::string stepPath = outputDir + "/" + entry.name + ".step";
        std::string stlPath = outputDir + "/" + entry.name + ".stl";

        if (!StepExporter::write(healed, stepPath)) {
            std::cerr << termcolor::red << "STEP export failed for " << entry.name
                      << termcolor::reset << "\n";
            return 1;
        }

        if (!StlExporter::write(healed, stlPath, {0.01, 0.1, true})) {
            std::cerr << termcolor::red << "STL export failed for " << entry.name
                      << termcolor::reset << "\n";
            return 1;
        }

        std::cout << termcolor::green << "Exported: " << termcolor::white << entry.name
                  << termcolor::reset << " (" << stepPath << ", " << stlPath << ")\n";
    }

    return 0;
}
