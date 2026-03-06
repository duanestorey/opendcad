#include "ViewportOverlay.h"
#include "RenderScene.h"
#include <imgui.h>

namespace opendcad {

void ViewportOverlay::draw(RenderScene& scene, const std::string& filename, const std::string& status) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float height = 24.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, height));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 0.95f));

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Triangle count
        int totalTris = 0;
        for (const auto& obj : scene.objects()) {
            if (obj.visible) totalTris += obj.faceVertexCount / 3;
        }
        ImGui::Text("Tris: %d", totalTris);

        ImGui::SameLine(200);
        if (!filename.empty()) {
            ImGui::Text("%s", filename.c_str());
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        ImGui::Text("%s", status.c_str());
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace opendcad
