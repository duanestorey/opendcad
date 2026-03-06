#pragma once
#include <string>

namespace opendcad {
class RenderScene;

class ViewportOverlay {
public:
    void draw(RenderScene& scene, const std::string& filename, const std::string& status);
};
} // namespace opendcad
