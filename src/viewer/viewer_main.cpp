#include "ViewerApp.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    opendcad::ViewerApp app;

    if (argc > 1) {
        app.setInputFile(argv[1]);
    }

    if (!app.init()) return 1;

    std::string title = "OpenDCAD Viewer";
    if (argc > 1) {
        title += " - ";
        title += argv[1];
    }
    app.setTitle(title);

    return app.run();
}
