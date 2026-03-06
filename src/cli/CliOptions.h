#pragma once

#include <string>
#include <vector>
#include <set>
#include <iostream>
#include <cstring>

namespace opendcad {

static const char OPENDCAD_VERSION[] = "1.00.00";

enum class QualityPreset { Draft, Standard, Production };

struct QualityParams {
    double deflection;
    double angle;
    bool heal;
};

inline QualityParams qualityFor(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::Draft:      return {0.1,   0.5,  false};
        case QualityPreset::Standard:   return {0.01,  0.1,  true};
        case QualityPreset::Production: return {0.001, 0.05, true};
    }
    return {0.01, 0.1, true};
}

struct CliOptions {
    std::string inputFile;
    std::string outputDir = "build";
    QualityPreset quality = QualityPreset::Standard;
    bool fmtStep = true;
    bool fmtStl = true;
    std::set<std::string> exportNames;   // empty = all
    double deflectionOverride = -1.0;    // negative = use preset
    double angleOverride = -1.0;
    bool listExports = false;
    bool dryRun = false;
    bool quiet = false;
    bool showHelp = false;
    bool showVersion = false;

    QualityParams getQualityParams() const {
        auto p = qualityFor(quality);
        if (deflectionOverride >= 0) p.deflection = deflectionOverride;
        if (angleOverride >= 0)      p.angle = angleOverride;
        return p;
    }
};

inline void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " <input.dcad> [options]\n"
        << "\n"
        << "Options:\n"
        << "  -o, --output <dir>        Output directory (default: build/)\n"
        << "  -f, --fmt <formats>       Geometry formats: step, stl, or step,stl (default: step,stl)\n"
        << "  -q, --qual <preset>       Quality preset: draft, standard, production (default: standard)\n"
        << "  -e, --export <name>       Build only this export (repeatable). Default: all exports\n"
        << "      --deflection <value>  Override STL mesh deflection (default per quality preset)\n"
        << "      --angle <value>       Override STL angular deflection in radians\n"
        << "      --list-exports        Parse script, print export names, exit\n"
        << "      --dry-run             Evaluate script, report exports, write nothing\n"
        << "      --quiet               Suppress banner and info messages\n"
        << "      --help                Show usage\n"
        << "      --version             Show version\n";
}

// Returns empty inputFile on error (caller should check)
inline CliOptions parseArgs(int argc, char* argv[]) {
    CliOptions opts;

    if (argc < 2) {
        opts.showHelp = true;
        return opts;
    }

    bool fmtExplicit = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
            return opts;
        }
        if (arg == "--version") {
            opts.showVersion = true;
            return opts;
        }

        if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            opts.outputDir = argv[i];
        } else if (arg == "-f" || arg == "--fmt") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            fmtExplicit = true;
            std::string val = argv[i];
            opts.fmtStep = false;
            opts.fmtStl = false;
            // Parse comma-separated formats
            size_t pos = 0;
            while (pos < val.size()) {
                size_t next = val.find(',', pos);
                std::string fmt = val.substr(pos, next == std::string::npos ? next : next - pos);
                if (fmt == "step")      opts.fmtStep = true;
                else if (fmt == "stl")  opts.fmtStl = true;
                else {
                    std::cerr << "Error: unknown format '" << fmt << "' (use step, stl, or step,stl)\n";
                    opts.showHelp = true;
                    return opts;
                }
                pos = (next == std::string::npos) ? val.size() : next + 1;
            }
        } else if (arg == "-q" || arg == "--qual") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            std::string val = argv[i];
            if (val == "draft")           opts.quality = QualityPreset::Draft;
            else if (val == "standard")   opts.quality = QualityPreset::Standard;
            else if (val == "production") opts.quality = QualityPreset::Production;
            else {
                std::cerr << "Error: unknown quality preset '" << val << "' (use draft, standard, production)\n";
                opts.showHelp = true;
                return opts;
            }
        } else if (arg == "-e" || arg == "--export") {
            if (++i >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            opts.exportNames.insert(argv[i]);
        } else if (arg == "--deflection") {
            if (++i >= argc) {
                std::cerr << "Error: --deflection requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            opts.deflectionOverride = std::stod(argv[i]);
        } else if (arg == "--angle") {
            if (++i >= argc) {
                std::cerr << "Error: --angle requires a value\n";
                opts.showHelp = true;
                return opts;
            }
            opts.angleOverride = std::stod(argv[i]);
        } else if (arg == "--list-exports") {
            opts.listExports = true;
        } else if (arg == "--dry-run") {
            opts.dryRun = true;
        } else if (arg == "--quiet") {
            opts.quiet = true;
        } else if (arg[0] == '-') {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            opts.showHelp = true;
            return opts;
        } else {
            // Positional argument = input file
            if (opts.inputFile.empty()) {
                opts.inputFile = arg;
            } else {
                std::cerr << "Error: unexpected argument '" << arg << "'\n";
                opts.showHelp = true;
                return opts;
            }
        }
    }

    // STEP + JSON always written. If user only asked for stl, also include step.
    if (fmtExplicit && !opts.fmtStep) {
        opts.fmtStep = true;
    }

    return opts;
}

} // namespace opendcad
