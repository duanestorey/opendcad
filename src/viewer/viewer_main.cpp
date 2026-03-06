#include "ViewerApp.h"
#include <cstdio>

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    opendcad::ViewerApp app;
    if (!app.init()) return 1;
    app.setTitle("OpenDCAD Viewer");
    return app.run();
}
