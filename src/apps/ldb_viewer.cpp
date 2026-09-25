#include "viewer/App.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    const char* path = argc >= 2 ? argv[1] : 0;
    if (argc >= 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        std::fprintf(stderr,
                     "usage: %s [level.ldb | data-dir]\n\n"
                     "  No arguments: look for data/database/levels/*.ldb next to the exe.\n"
                     "  Left/Right arrows cycle levels in that folder.\n",
                     argv[0]);
        return 0;
    }
    maxfx::ViewerApp app;
    return app.run(path);
}
