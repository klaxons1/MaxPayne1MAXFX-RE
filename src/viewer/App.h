#ifndef MAXFX_VIEWER_APP_H
#define MAXFX_VIEWER_APP_H

#include "maxfx/ldb/Ldb.h"
#include "viewer/Camera.h"
#include "viewer/Renderer.h"

#include <string>
#include <vector>

struct SDL_Window;

namespace maxfx {

class ViewerApp {
public:
    ViewerApp();
    ~ViewerApp();

    // `pathOrNull` may be a .ldb file, a directory of .ldb files, or null.
    // With no argument the viewer looks for data/database/levels next to the
    // executable (drop ldb-viewer.exe beside the game's data/ folder).
    int run(const char* pathOrNull);

private:
    bool createWindow(char* error, std::size_t errorSize);
    void destroyWindow();
    void collectLevels(const char* pathOrNull);
    bool loadLevelIndex(int index, bool showLoading);
    void placeAtSpawn(int index);
    void cycleLevel(int delta);
    void handleKeyDown(int scancode, int key);
    void update(float dt);
    void drawHud(float dt);
    void drawLoadingFrame(const std::string& message);

    SDL_Window* window_;
    void* glContext_;
    Level level_;
    Renderer renderer_;
    Camera camera_;
    bool running_;
    bool mouseCaptured_;
    bool showHelp_;
    int spawnIndex_;
    int width_;
    int height_;
    std::string fileName_;
    std::string levelsDir_;
    std::vector<std::string> levelPaths_;
    int levelIndex_;
    std::string statusMessage_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_APP_H
