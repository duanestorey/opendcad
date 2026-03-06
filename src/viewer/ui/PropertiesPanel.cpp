#include "PropertiesPanel.h"
#include "RenderObject.h"
#include <imgui.h>

namespace opendcad {

void PropertiesPanel::draw(const RenderObject* selected) {
    ImGui::Begin("Properties");

    if (!selected) {
        ImGui::TextDisabled("Select an object");
        ImGui::End();
        return;
    }

    ImGui::Text("Name: %s", selected->name.c_str());
    ImGui::Separator();

    ImGui::Text("Faces: %d", selected->faceCount);
    ImGui::Text("Edges: %d", selected->edgeCount);
    ImGui::Text("Triangles: %d", selected->faceVertexCount / 3);
    ImGui::Separator();

    ImGui::Text("Volume: %.2f mm\xC2\xB3", selected->volume);
    ImGui::Text("Surface: %.2f mm\xC2\xB2", selected->surfaceArea);
    ImGui::Separator();

    float dx = selected->bboxMax[0] - selected->bboxMin[0];
    float dy = selected->bboxMax[1] - selected->bboxMin[1];
    float dz = selected->bboxMax[2] - selected->bboxMin[2];
    ImGui::Text("Size: %.1f x %.1f x %.1f mm", dx, dy, dz);

    ImGui::End();
}

} // namespace opendcad
