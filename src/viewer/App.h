#ifndef MAXFX_VIEWER_APP_H
#define MAXFX_VIEWER_APP_H

#include "maxfx/char/Character.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/db/Database.h"
#include "maxfx/ldb/Ldb.h"
#include "maxfx/levels/Levels.h"
#include "viewer/Audio.h"
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

    // `pathOrNull` may be a .ldb file, a directory, or null.
    // With no argument the viewer reads data/database/levels/levels.txt next
    // to the executable (the official playlist: Directory/Level under part1/).
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
    void spawnActors();
    void updateActors(float dt);
    void drawHud(float dt);
    void drawLoadingFrame(const std::string& message);

    SDL_Window* window_;
    void* glContext_;
    Level level_;
    Database database_;
    Renderer renderer_;
    ViewerAudio audio_;
    Camera camera_;
    CollisionWorld collision_;
    std::vector<CharacterActor> actors_;
    bool running_;
    bool mouseCaptured_;
    bool showHelp_;
    int spawnIndex_;
    int width_;
    int height_;
    std::string fileName_;
    std::string levelsDir_;
    std::vector<std::string> levelPaths_;
    std::vector<LevelInfo> levelInfos_;
    int levelIndex_;
    std::string statusMessage_;
    std::string startPlace_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_APP_H
