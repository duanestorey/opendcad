#include "MenuBar.h"
#include "Renderer.h"
#include <imgui.h>

namespace opendcad {

MenuActions MenuBar::draw(Renderer& renderer) {
    MenuActions actions;

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Screenshot", "Ctrl+S")) actions.screenshot = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Close", "Escape")) actions.close = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Camera", "Space")) actions.resetCamera = true;
            if (ImGui::MenuItem("Fit All", "F")) actions.fitAll = true;
            if (ImGui::MenuItem("Isometric", "I")) actions.isometric = true;
            ImGui::Separator();
            bool grid = renderer.gridVisible();
            if (ImGui::MenuItem("Grid", "G", &grid)) renderer.setGridVisible(grid);
            bool edges = renderer.edgesVisible();
            if (ImGui::MenuItem("Edges", "E", &edges)) renderer.setEdgesVisible(edges);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard Shortcuts")) { /* TODO: show shortcuts dialog */ }
            ImGui::Separator();
            ImGui::MenuItem("OpenDCAD Viewer", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    return actions;
}

} // namespace opendcad
