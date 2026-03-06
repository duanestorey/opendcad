#include "ObjectPanel.h"
#include "RenderScene.h"
#include <imgui.h>

namespace opendcad {

void ObjectPanel::draw(RenderScene& scene, int& selectedObject) {
    ImGui::Begin("Objects");

    auto& objects = scene.objects();
    for (int i = 0; i < static_cast<int>(objects.size()); i++) {
        auto& obj = objects[i];
        ImGui::PushID(i);

        // Visibility checkbox
        ImGui::Checkbox("##vis", &obj.visible);
        ImGui::SameLine();

        // Selectable name
        bool isSelected = (selectedObject == i);
        if (ImGui::Selectable(obj.name.c_str(), isSelected)) {
            selectedObject = i;
            // Deselect all, select this one
            for (auto& o : objects) o.selected = false;
            obj.selected = true;
        }

        // Triangle count on same line
        ImGui::SameLine(ImGui::GetWindowWidth() - 60);
        ImGui::TextDisabled("%d tri", obj.faceVertexCount / 3);

        ImGui::PopID();
    }

    ImGui::End();
}

} // namespace opendcad
