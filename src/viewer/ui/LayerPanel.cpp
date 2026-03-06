#include "LayerPanel.h"
#include "RenderScene.h"
#include <imgui.h>

namespace opendcad {

// Auto-assign colors to layers
static ImVec4 layerColor(int index) {
    const ImVec4 palette[] = {
        {0.40f, 0.65f, 0.90f, 1.0f},  // blue
        {0.90f, 0.50f, 0.30f, 1.0f},  // orange
        {0.40f, 0.80f, 0.45f, 1.0f},  // green
        {0.85f, 0.45f, 0.65f, 1.0f},  // pink
        {0.70f, 0.60f, 0.90f, 1.0f},  // purple
        {0.90f, 0.80f, 0.30f, 1.0f},  // yellow
    };
    return palette[index % 6];
}

void LayerPanel::draw(RenderScene& scene) {
    ImGui::Begin("Layers");

    auto tags = scene.uniqueTags();
    if (tags.empty()) {
        ImGui::TextDisabled("No layers (add tags to shapes)");
    }

    int idx = 0;
    for (const auto& tag : tags) {
        // Initialize visibility if not tracked yet
        if (layerVisibility_.find(tag) == layerVisibility_.end()) {
            layerVisibility_[tag] = true;
        }

        ImGui::PushID(idx);

        // Color indicator
        ImVec4 color = layerColor(idx);
        ImGui::ColorButton("##color", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(12, 12));
        ImGui::SameLine();

        // Visibility toggle
        bool& vis = layerVisibility_[tag];
        if (ImGui::Checkbox(tag.c_str(), &vis)) {
            scene.setTagVisibility(tag, vis);
        }

        ImGui::PopID();
        idx++;
    }

    ImGui::End();
}

} // namespace opendcad
