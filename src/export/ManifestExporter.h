#pragma once

#include <string>
#include <vector>
#include <memory>

namespace opendcad {

class Shape;
using ShapePtr = std::shared_ptr<Shape>;

struct BuildMetrics {
    std::string script;
    std::string timestamp;        // ISO 8601
    long long durationMs = 0;
    std::string qualityPreset;
    std::string opendcadVersion;
    std::string occtVersion;
    std::vector<size_t> triangleCounts;  // per shape, empty if STL not built
};

class ManifestExporter {
public:
    static bool write(const std::vector<ShapePtr>& shapes,
                      const std::string& exportName,
                      const std::string& path,
                      const BuildMetrics& metrics = {});
};

} // namespace opendcad
