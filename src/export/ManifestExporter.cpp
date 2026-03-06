#include "ManifestExporter.h"
#include "Shape.h"
#include "Color.h"
#include "Debug.h"
#include <fstream>
#include <sstream>

namespace opendcad {

static std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

static std::string formatDouble(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

bool ManifestExporter::write(const std::vector<ShapePtr>& shapes,
                              const std::string& exportName,
                              const std::string& path,
                              const BuildMetrics& metrics) {
    DEBUG_INFO("Writing manifest [" << path << "]");

    std::ostringstream os;
    os << "{\n";
    os << "  \"version\": 1,\n";
    os << "  \"export\": \"" << escapeJson(exportName) << "\",\n";

    // Build metrics section (always present when metrics has data)
    if (!metrics.script.empty()) {
        os << "  \"build\": {\n";
        os << "    \"script\": \"" << escapeJson(metrics.script) << "\",\n";
        os << "    \"timestamp\": \"" << escapeJson(metrics.timestamp) << "\",\n";
        os << "    \"duration_ms\": " << metrics.durationMs << ",\n";
        os << "    \"quality\": \"" << escapeJson(metrics.qualityPreset) << "\",\n";
        os << "    \"opendcad_version\": \"" << escapeJson(metrics.opendcadVersion) << "\",\n";
        os << "    \"occt_version\": \"" << escapeJson(metrics.occtVersion) << "\"\n";
        os << "  },\n";
    }

    os << "  \"shapes\": [\n";

    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes[i];
        os << "    {\n";
        os << "      \"index\": " << i;

        // Triangle count if available
        if (i < metrics.triangleCounts.size() && metrics.triangleCounts[i] > 0) {
            os << ",\n      \"triangles\": " << metrics.triangleCounts[i];
        }

        if (shape->color()) {
            const auto& c = *shape->color();
            os << ",\n      \"color\": { \"r\": " << formatDouble(c.r)
               << ", \"g\": " << formatDouble(c.g)
               << ", \"b\": " << formatDouble(c.b)
               << ", \"a\": " << formatDouble(c.a) << " }";
        }

        if (shape->material()) {
            const auto& m = *shape->material();
            os << ",\n      \"material\": { \"preset\": \"" << escapeJson(m.preset) << "\""
               << ", \"metallic\": " << formatDouble(m.metallic)
               << ", \"roughness\": " << formatDouble(m.roughness) << " }";
        }

        if (!shape->tags().empty()) {
            os << ",\n      \"tags\": [";
            for (size_t t = 0; t < shape->tags().size(); ++t) {
                if (t > 0) os << ", ";
                os << "\"" << escapeJson(shape->tags()[t]) << "\"";
            }
            os << "]";
        }

        os << "\n    }";
        if (i + 1 < shapes.size()) os << ",";
        os << "\n";
    }

    os << "  ]\n";
    os << "}\n";

    std::ofstream file(path);
    if (!file) return false;
    file << os.str();
    DEBUG_INFO("...manifest written");
    return file.good();
}

} // namespace opendcad
