#pragma once

#include <string>

namespace opendcad {

class Screenshot {
public:
    /// Capture the current OpenGL framebuffer and write a PNG file.
    static bool capture(int viewportW, int viewportH, const std::string& path);
};

} // namespace opendcad
