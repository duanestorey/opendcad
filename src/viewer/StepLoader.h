#pragma once

#include <string>
#include <vector>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct StepLoadEntry {
    TopoDS_Shape shape;
    std::string name;
    float color[3] = {0.72f, 0.78f, 0.86f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string materialPreset;
    std::vector<std::string> tags;
};

class StepLoader {
public:
    static std::vector<StepLoadEntry> load(const std::string& stepPath);
private:
    static void loadJsonSidecar(const std::string& jsonPath, std::vector<StepLoadEntry>& entries);
};

} // namespace opendcad
