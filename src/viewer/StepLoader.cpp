#include "StepLoader.h"

#include <STEPCAFControl_Reader.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_LabelSequence.hxx>
#include <Quantity_Color.hxx>

#include <fstream>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace opendcad {

// ---------------------------------------------------------------------------
// Simple JSON sidecar parser (no external dependency)
//
// Expected format (written by ManifestExporter):
// {
//   "shapes": [
//     {
//       "index": 0,
//       "color": { "r": 0.1, "g": 0.1, "b": 0.1, "a": 1 },
//       "material": { "preset": "ABS_BLACK", "metallic": 0, "roughness": 0.6 },
//       "tags": ["plastic"]
//     }
//   ]
// }
// ---------------------------------------------------------------------------

// Find a double value for a key in a JSON fragment
static bool findDouble(const std::string& json, const std::string& key, double& out) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    char* end = nullptr;
    out = std::strtod(json.c_str() + pos, &end);
    return end != json.c_str() + pos;
}

// Find a string value for a key in a JSON fragment
static bool findString(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    auto end = json.find('"', pos);
    if (end == std::string::npos) return false;
    out = json.substr(pos, end - pos);
    return true;
}

// Split JSON into per-shape blocks between { and }
static std::vector<std::string> splitShapeBlocks(const std::string& json) {
    std::vector<std::string> blocks;

    // Find the "shapes" array
    auto arrStart = json.find("\"shapes\"");
    if (arrStart == std::string::npos) return blocks;
    arrStart = json.find('[', arrStart);
    if (arrStart == std::string::npos) return blocks;

    // Find matching ]
    int depth = 0;
    size_t pos = arrStart;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '[') ++depth;
        else if (json[pos] == ']') { --depth; if (depth == 0) break; }
    }
    std::string arr = json.substr(arrStart + 1, pos - arrStart - 1);

    // Split by top-level { }
    depth = 0;
    size_t blockStart = 0;
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == '{') {
            if (depth == 0) blockStart = i;
            ++depth;
        } else if (arr[i] == '}') {
            --depth;
            if (depth == 0) {
                blocks.push_back(arr.substr(blockStart, i - blockStart + 1));
            }
        }
    }

    return blocks;
}

// Parse tags array from a JSON block
static std::vector<std::string> parseTags(const std::string& block) {
    std::vector<std::string> tags;
    auto pos = block.find("\"tags\"");
    if (pos == std::string::npos) return tags;
    pos = block.find('[', pos);
    if (pos == std::string::npos) return tags;
    auto end = block.find(']', pos);
    if (end == std::string::npos) return tags;

    std::string arr = block.substr(pos + 1, end - pos - 1);
    size_t p = 0;
    while (p < arr.size()) {
        auto q1 = arr.find('"', p);
        if (q1 == std::string::npos) break;
        auto q2 = arr.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        tags.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
        p = q2 + 1;
    }
    return tags;
}

void StepLoader::loadJsonSidecar(const std::string& jsonPath,
                                  std::vector<StepLoadEntry>& entries) {
    std::ifstream file(jsonPath);
    if (!file) return;

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    auto blocks = splitShapeBlocks(json);

    for (const auto& block : blocks) {
        double idx = -1;
        if (!findDouble(block, "index", idx)) continue;
        int i = static_cast<int>(idx);
        if (i < 0 || i >= static_cast<int>(entries.size())) continue;

        auto& entry = entries[i];

        // Color
        auto colorPos = block.find("\"color\"");
        if (colorPos != std::string::npos) {
            // Find the color object { ... }
            auto cStart = block.find('{', colorPos);
            auto cEnd = block.find('}', cStart);
            if (cStart != std::string::npos && cEnd != std::string::npos) {
                std::string colorBlock = block.substr(cStart, cEnd - cStart + 1);
                double r, g, b;
                if (findDouble(colorBlock, "r", r) &&
                    findDouble(colorBlock, "g", g) &&
                    findDouble(colorBlock, "b", b)) {
                    entry.color[0] = static_cast<float>(r);
                    entry.color[1] = static_cast<float>(g);
                    entry.color[2] = static_cast<float>(b);
                }
            }
        }

        // Material
        auto matPos = block.find("\"material\"");
        if (matPos != std::string::npos) {
            auto mStart = block.find('{', matPos);
            auto mEnd = block.find('}', mStart);
            if (mStart != std::string::npos && mEnd != std::string::npos) {
                std::string matBlock = block.substr(mStart, mEnd - mStart + 1);
                double met, rough;
                if (findDouble(matBlock, "metallic", met)) {
                    entry.metallic = static_cast<float>(met);
                }
                if (findDouble(matBlock, "roughness", rough)) {
                    entry.roughness = static_cast<float>(rough);
                }
                findString(matBlock, "preset", entry.materialPreset);
            }
        }

        // Tags
        entry.tags = parseTags(block);
    }
}

// ---------------------------------------------------------------------------
// load() — read STEP via XDE, then overlay JSON sidecar metadata
// ---------------------------------------------------------------------------

std::vector<StepLoadEntry> StepLoader::load(const std::string& stepPath) {
    std::vector<StepLoadEntry> entries;

    // Create XDE document
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);

    // Read STEP
    STEPCAFControl_Reader reader;
    reader.SetColorMode(true);
    reader.SetNameMode(true);

    if (reader.ReadFile(stepPath.c_str()) != IFSelect_RetDone) {
        std::fprintf(stderr, "StepLoader: failed to read %s\n", stepPath.c_str());
        app->Close(doc);
        return entries;
    }

    if (!reader.Transfer(doc)) {
        std::fprintf(stderr, "StepLoader: failed to transfer %s\n", stepPath.c_str());
        app->Close(doc);
        return entries;
    }

    // Extract shapes
    Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TDF_LabelSequence freeShapes;
    shapeTool->GetFreeShapes(freeShapes);

    for (int i = 1; i <= freeShapes.Length(); ++i) {
        TDF_Label label = freeShapes.Value(i);
        TopoDS_Shape shape = shapeTool->GetShape(label);
        if (shape.IsNull()) continue;

        StepLoadEntry entry;
        entry.shape = shape;

        // Extract name
        Handle(TDataStd_Name) nameAttr;
        if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
            TCollection_AsciiString ascii(nameAttr->Get());
            entry.name = ascii.ToCString();
        } else {
            entry.name = "shape_" + std::to_string(i - 1);
        }

        // Extract XDE color
        Quantity_Color qc;
        if (colorTool->GetColor(label, XCAFDoc_ColorSurf, qc)) {
            entry.color[0] = static_cast<float>(qc.Red());
            entry.color[1] = static_cast<float>(qc.Green());
            entry.color[2] = static_cast<float>(qc.Blue());
        }

        entries.push_back(std::move(entry));
    }

    app->Close(doc);

    // Look for JSON sidecar
    std::string jsonPath = stepPath;
    auto dotPos = jsonPath.rfind('.');
    if (dotPos != std::string::npos) {
        jsonPath = jsonPath.substr(0, dotPos) + ".json";
    } else {
        jsonPath += ".json";
    }
    loadJsonSidecar(jsonPath, entries);

    std::printf("StepLoader: loaded %zu shape(s) from %s\n", entries.size(), stepPath.c_str());

    return entries;
}

} // namespace opendcad
