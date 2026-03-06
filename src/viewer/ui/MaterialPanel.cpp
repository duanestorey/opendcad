#include "MaterialPanel.h"
#include "RenderObject.h"
#include <imgui.h>

namespace opendcad {

void MaterialPanel::draw(const RenderObject* selected) {
    ImGui::Begin("Material");

    if (!selected) {
        ImGui::TextDisabled("Select an object");
        ImGui::End();
        return;
    }

    // Color swatch
    ImVec4 col(selected->materialColor[0], selected->materialColor[1], selected->materialColor[2], 1.0f);
    ImGui::ColorButton("Color", col, 0, ImVec2(40, 40));
    ImGui::SameLine();
    ImGui::Text("%.2f, %.2f, %.2f", col.x, col.y, col.z);

    ImGui::Separator();

    if (!selected->materialPreset.empty()) {
        ImGui::Text("Preset: %s", selected->materialPreset.c_str());
    }

    // Read-only sliders
    float met = selected->materialMetallic;
    float rough = selected->materialRoughness;
    ImGui::SliderFloat("Metallic", &met, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput);
    ImGui::SliderFloat("Roughness", &rough, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_NoInput);

    ImGui::End();
}

} // namespace opendcad
