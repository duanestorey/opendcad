#pragma once

namespace opendcad {
class RenderScene;

class ObjectPanel {
public:
    void draw(RenderScene& scene, int& selectedObject);
};
} // namespace opendcad
