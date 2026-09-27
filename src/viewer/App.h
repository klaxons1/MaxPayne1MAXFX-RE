#ifndef MAXFX_VIEWER_APP_H
#define MAXFX_VIEWER_APP_H

#include "maxfx/char/Character.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/db/Database.h"
#include "maxfx/game/Runtime.h"
#include "maxfx/game/Hud.h"
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
    void handleMouseButton(float x, float y, int button);
    void update(float dt);
    void spawnActors();
    void updateActors(float dt);
    void fillActorMoveSpeed(CharacterActor& actor);
    void drawHud(float dt);
    void drawWeaponHud();  // hud.txt sprites + weapon list overlay
    // Cinematics: start the next scripted clip of the player skin (key C)
    // and drive the cutscene camera / letterbox / fade each frame.
    void startNextCinematic();
    void applyCinematicCamera(Camera* cam);
    void tickCinematicFrame(float dt);
    void drawMenuHud();
    void drawComicHud();
    void drawLoadingFrame(const std::string& message);
    void enterLevel(int index);
    void enterComic(int page, bool fromMenu);
    void leaveComic();
    void stepComic(int delta);
    void playComicSound();
    void syncCameraFromPlayer();
    Vec3 playerLookLdb() const;

    SDL_Window* window_;
    void* glContext_;
    Level level_;
    Database database_;
    Renderer renderer_;
    ViewerAudio audio_;
    Camera camera_;
    CollisionWorld collision_;
    std::vector<CharacterActor> actors_;
    GameRuntime game_;
    // hud.txt definition (data/hud/hud.txt) + decal texture lookup fed to the
    // renderer's effects pass.
    HudDef hud_;
    std::map<std::string, std::pair<std::string, std::string> > decalFiles_;
    float weaponListTimer_;  // seconds left to show the weapon cycle overlay
    // Cinematic state (mirrors GameRuntime::cine for presentation).
    std::string playerSkinName_;
    Mat4x3 cinematicStartEntity_;  // player entity when the clip started
    Mat4x3 cinematicEntity_;       // start * movement root motion
    std::string cameraPathName_;   // path the camera base was captured for
    Mat4x3 cameraBase_;            // LDB-space base for parented camera paths
    int cinematicClipCursor_;
    bool wasCineOn_;
    // Dynamic-mesh (door) collision bookkeeping: solid state per mesh so the
    // collision world is rebuilt only when a door actually opens / closes.
    std::vector<char> doorSolid_;
    void rebuildCollision();
    bool running_;
    bool levelLoaded_;
    bool mouseCaptured_;
    bool showHelp_;
    bool showItemDebug_;  // F8: per-item model resolution + LDB transform
    int spawnIndex_;
    int width_;
    int height_;
    std::string fileName_;
    std::string levelsDir_;
    std::vector<std::string> levelPaths_;
    std::vector<LevelInfo> levelInfos_;
    int levelIndex_;
    int lastComicPage_;
    std::string statusMessage_;
    std::string startPlace_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_APP_H
