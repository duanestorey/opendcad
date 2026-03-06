#pragma once
#include <map>
#include <string>

namespace opendcad {
class RenderScene;

class LayerPanel {
public:
    void draw(RenderScene& scene);
private:
    std::map<std::string, bool> layerVisibility_;
};
} // namespace opendcad
