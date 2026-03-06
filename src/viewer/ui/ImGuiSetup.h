#pragma once

struct GLFWwindow;

namespace opendcad {

class ImGuiSetup {
public:
    static bool init(GLFWwindow* window);
    static void beginFrame();
    static void endFrame();
    static void shutdown();
private:
    static void setupDarkTheme();
};

} // namespace opendcad
