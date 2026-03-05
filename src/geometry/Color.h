#pragma once

#include <memory>
#include <string>
#include <cstdint>

namespace opendcad {

struct Color {
    double r = 0.5, g = 0.5, b = 0.5, a = 1.0;  // 0.0-1.0

    static Color fromRGB(double r, double g, double b, double a = 1.0) {
        return {r / 255.0, g / 255.0, b / 255.0, a};
    }

    static Color fromHex(const std::string& hex) {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.size() == 6) {
            unsigned int val = std::stoul(h, nullptr, 16);
            return {((val >> 16) & 0xFF) / 255.0,
                    ((val >> 8) & 0xFF) / 255.0,
                    (val & 0xFF) / 255.0, 1.0};
        }
        return {0.5, 0.5, 0.5, 1.0};  // fallback grey
    }

    std::string toString() const {
        char buf[64];
        snprintf(buf, sizeof(buf), "color(%.0f, %.0f, %.0f, %.2f)",
                 r * 255, g * 255, b * 255, a);
        return buf;
    }
};

using ColorPtr = std::shared_ptr<Color>;

struct Material {
    std::string preset;
    double metallic = 0.0;
    double roughness = 0.5;
    ColorPtr baseColor;

    std::string toString() const {
        return "material(\"" + preset + "\", metallic=" +
               std::to_string(metallic) + ", roughness=" +
               std::to_string(roughness) + ")";
    }
};

using MaterialPtr = std::shared_ptr<Material>;

} // namespace opendcad
