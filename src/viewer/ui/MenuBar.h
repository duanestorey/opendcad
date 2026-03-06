#pragma once

namespace opendcad {
class Renderer;
class Camera;

struct MenuActions {
    bool screenshot = false;
    bool close = false;
    bool resetCamera = false;
    bool fitAll = false;
    bool isometric = false;
    bool toggleGrid = false;
    bool toggleEdges = false;
};

class MenuBar {
public:
    MenuActions draw(Renderer& renderer);
};
} // namespace opendcad
