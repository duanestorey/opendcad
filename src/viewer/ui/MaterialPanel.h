#pragma once

namespace opendcad {
struct RenderObject;

class MaterialPanel {
public:
    void draw(const RenderObject* selected);
};
} // namespace opendcad
