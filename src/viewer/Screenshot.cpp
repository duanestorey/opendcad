#include <glad/glad.h>
#include "Screenshot.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <vector>
#include <cstdio>
#include <cstring>

namespace opendcad {

bool Screenshot::capture(int viewportW, int viewportH, const std::string& path) {
    if (viewportW <= 0 || viewportH <= 0) return false;

    int rowSize = viewportW * 3;
    std::vector<unsigned char> pixels(static_cast<size_t>(rowSize) * viewportH);
    glReadPixels(0, 0, viewportW, viewportH, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL origin is bottom-left, PNG expects top-left)
    std::vector<unsigned char> flipped(pixels.size());
    for (int y = 0; y < viewportH; y++) {
        std::memcpy(flipped.data() + y * rowSize,
                    pixels.data() + (viewportH - 1 - y) * rowSize,
                    static_cast<size_t>(rowSize));
    }

    if (!stbi_write_png(path.c_str(), viewportW, viewportH, 3, flipped.data(), rowSize)) {
        std::fprintf(stderr, "Failed to write screenshot: %s\n", path.c_str());
        return false;
    }

    std::fprintf(stdout, "Screenshot saved: %s\n", path.c_str());
    return true;
}

} // namespace opendcad
