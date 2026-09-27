#include "viewer/App.h"

#include "maxfx/char/Character.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/core/Fs.h"
#include "maxfx/db/Database.h"
#include "maxfx/game/Runtime.h"
#include "maxfx/kf2/Kf2.h"
#include "maxfx/ldb/LdbReader.h"
#include "maxfx/levels/Levels.h"
#include "maxfx/script/Script.h"
#include "viewer/GL.h"

#include <SDL3/SDL.h>

// Build stamp from CMake (git short hash). Shown in the window title, the
// HUD and stdout so a stale exe copied next to the game data is easy to
// spot: scripts/build-windows.bat copies the exe out of build\Release, and
// that copy never updates on rebuild.
#ifndef MAXFX_BUILD_HASH
#define MAXFX_BUILD_HASH "dev"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace maxfx {
namespace {

void* glProc(const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

void appendUniqueDir(std::vector<std::string>* dirs, const std::string& dir) {
    if (dir.empty() || !isDirectory(dir)) {
        return;
    }
    for (std::size_t i = 0; i < dirs->size(); ++i) {
        if ((*dirs)[i] == dir) {
            return;
        }
    }
    dirs->push_back(dir);
}

int findPathIndex(const std::vector<std::string>& paths, const std::string& path) {
    const std::string want = lowerCopy(fileName(path));
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (lowerCopy(fileName(paths[i])) == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool frameComicCamera(const Kf2File& kf, float aspect, Camera* cam) {
    std::vector<Kf2DrawMesh> draws;
    kf2BuildDrawMeshes(kf, draws);
    if (draws.empty()) {
        return false;
    }
    Vec3 mn(1.0e9f, 1.0e9f, 1.0e9f);
    Vec3 mx(-1.0e9f, -1.0e9f, -1.0e9f);
    bool any = false;
    std::vector<std::string> names;
    std::vector<Mat4x3> worlds;
    kf2NodeWorldTransforms(kf, &names, &worlds);
    for (std::size_t m = 0; m < draws.size(); ++m) {
        Mat4x3 local;
        for (std::size_t n = 0; n < names.size(); ++n) {
            if (names[n] == draws[m].nodeName) {
                local = worlds[n];
                break;
            }
        }
        for (std::size_t p = 0; p < draws[m].parts.size(); ++p) {
            const Kf2DrawPart& part = draws[m].parts[p];
            for (std::size_t v = 0; v < part.vertices.size(); ++v) {
                Vec3 pos = transformPoint(local, part.vertices[v].position);
                pos.x = -pos.x;
                if (!any) {
                    mn = mx = pos;
                    any = true;
                } else {
                    if (pos.x < mn.x) {
                        mn.x = pos.x;
                    }
                    if (pos.y < mn.y) {
                        mn.y = pos.y;
                    }
                    if (pos.z < mn.z) {
                        mn.z = pos.z;
                    }
                    if (pos.x > mx.x) {
                        mx.x = pos.x;
                    }
                    if (pos.y > mx.y) {
                        mx.y = pos.y;
                    }
                    if (pos.z > mx.z) {
                        mx.z = pos.z;
                    }
                }
            }
        }
    }
    if (!any) {
        return false;
    }
    const Vec3 center((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f);
    const float halfW = (mx.x - mn.x) * 0.5f * 1.08f;
    const float halfH = (mx.y - mn.y) * 0.5f * 1.08f;
    const float fov = toRadians(40.0f);
    const float tanHalf = std::tan(fov * 0.5f);
    float dist = 0.25f;
    if (tanHalf > 1.0e-4f) {
        const float distH = halfH / tanHalf;
        const float distW = aspect > 0.1f ? halfW / (tanHalf * aspect) : distH;
        dist = distH > distW ? distH : distW;
    }
    if (dist < 0.12f) {
        dist = 0.12f;
    }
    cam->position = Vec3(center.x, center.y, center.z + dist);
    cam->yaw = 0.0f;
    cam->pitch = 0.0f;
    return true;
}

// hud.txt / decals.txt image paths are game-relative with backslashes; try
// the candidate roots under the data folder and return the first hit.
std::string resolveGameAsset(const std::string& dataRoot, const std::string& rel,
                             const char* const* roots, int rootCount) {
    if (rel.empty()) {
        return std::string();
    }
    std::string n = rel;
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (n[i] == '\\') {
            n[i] = '/';
        }
    }
    for (int r = 0; r < rootCount; ++r) {
        const std::string cand = joinPath(joinPath(dataRoot, roots[r]), n);
        if (isFile(cand)) {
            return cand;
        }
    }
    // Some scripts reference the file with a sub path or a different folder
    // ("decals\foo.bmp"); fall back to the bare file name per root.
    const std::string base = fileName(n);
    if (base != n) {
        for (int r = 0; r < rootCount; ++r) {
            const std::string cand = joinPath(joinPath(dataRoot, roots[r]), base);
            if (isFile(cand)) {
                return cand;
            }
        }
    }
    return std::string();
}

// Camera-pitch / yaw from a direction vector in viewer space.
void yawPitchFromDirection(const Vec3& dir, float* yaw, float* pitch) {
    Vec3 d = dir;
    const float l = length(d);
    if (l < 1.0e-5f) {
        *yaw = 0.0f;
        *pitch = 0.0f;
        return;
    }
    d = Vec3(d.x / l, d.y / l, d.z / l);
    *pitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y)));
    // Camera::forward() = (sin(yaw)cp, sp, -cos(yaw)cp)
    *yaw = std::atan2(d.x, -d.z);
}

bool hitRow(float x, float y, int x0, int y0, int w, int h) {
    return x >= static_cast<float>(x0) && x <= static_cast<float>(x0 + w) &&
           y >= static_cast<float>(y0) && y <= static_cast<float>(y0 + h);
}

}  // namespace

ViewerApp::ViewerApp()
    : window_(0),
      glContext_(0),
      running_(true),
      levelLoaded_(false),
      mouseCaptured_(false),
      showHelp_(true),
      showItemDebug_(false),
      spawnIndex_(-1),
      width_(1280),
      height_(720),
      levelIndex_(-1),
      lastComicPage_(-1),
      cinematicClipCursor_(-1),
      wasCineOn_(false),
      wasCineActive_(false) {}

ViewerApp::~ViewerApp() { destroyWindow(); }

void ViewerApp::destroyWindow() {
    audio_.shutdown();
    renderer_.shutdown();
    if (glContext_) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = 0;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = 0;
    }
    SDL_Quit();
}

bool ViewerApp::createWindow(char* error, std::size_t errorSize) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::snprintf(error, errorSize, "SDL_Init: %s", SDL_GetError());
            return false;
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    std::printf("ldb-viewer  build %s  (%s %s)\n", MAXFX_BUILD_HASH, __DATE__, __TIME__);
    std::fflush(stdout);
    char stampTitle[128];
    std::snprintf(stampTitle, sizeof(stampTitle), "MAXFX Level Viewer  b%s", MAXFX_BUILD_HASH);
    window_ = SDL_CreateWindow(stampTitle, width_, height_,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window_ == 0) {
        std::snprintf(error, errorSize, "SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    glContext_ = SDL_GL_CreateContext(window_);
    if (glContext_ == 0) {
        std::snprintf(error, errorSize, "SDL_GL_CreateContext: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    if (!loadGL(glProc)) {
        std::snprintf(error, errorSize, "failed to load OpenGL 3.3 entry points");
        return false;
    }
    SDL_SetWindowRelativeMouseMode(window_, false);
    return true;
}

void ViewerApp::collectLevels(const char* pathOrNull) {
    levelPaths_.clear();
    levelInfos_.clear();
    levelsDir_.clear();
    levelIndex_ = -1;
    startPlace_.clear();

    std::string explicitFile;
    std::vector<std::string> searchDirs;

    if (pathOrNull && pathOrNull[0] != 0) {
        const std::string given = pathOrNull;
        if (isDirectory(given)) {
            searchDirs.push_back(given);
            searchDirs.push_back(joinPath(given, "database"));
            searchDirs.push_back(joinPath(joinPath(given, "database"), "levels"));
            searchDirs.push_back(joinPath(given, "levels"));
            searchDirs.push_back(joinPath(joinPath(joinPath(given, "data"), "database"), "levels"));
        } else if (isFile(given)) {
            const std::string lower = lowerCopy(given);
            if (lower.size() >= 10 &&
                lower.compare(lower.size() - 10, 10, "levels.txt") == 0) {
                searchDirs.push_back(parentDir(given));
            } else {
                explicitFile = given;
                searchDirs.push_back(parentDir(given));
                // data/database/levels/part1/foo.ldb → levels folder is two up.
                const std::string up = parentDir(parentDir(given));
                if (!up.empty()) {
                    searchDirs.push_back(up);
                }
            }
        }
    }

    const std::string exeDir = executableDir();
    appendUniqueDir(&searchDirs, joinPath(joinPath(joinPath(exeDir, "data"), "database"), "levels"));
    appendUniqueDir(&searchDirs, joinPath(exeDir, "data"));
    appendUniqueDir(&searchDirs, joinPath(joinPath(joinPath(std::string("."), "data"), "database"), "levels"));
    appendUniqueDir(&searchDirs, joinPath(joinPath("data", "database"), "levels"));
    if (!exeDir.empty()) {
        appendUniqueDir(&searchDirs,
                        joinPath(joinPath(joinPath(parentDir(exeDir), "data"), "database"), "levels"));
        appendUniqueDir(&searchDirs, exeDir);
    }

    std::string levelsTxt = LevelsReader::locateLevelsTxt(pathOrNull ? pathOrNull : "");
    if (levelsTxt.empty()) {
        for (std::size_t i = 0; i < searchDirs.size(); ++i) {
            const std::string cand = joinPath(searchDirs[i], "levels.txt");
            if (isFile(cand)) {
                levelsTxt = cand;
                break;
            }
        }
    }

    if (!levelsTxt.empty()) {
        try {
            const LevelDatabase db = LevelsReader::loadFile(levelsTxt);
            levelsDir_ = db.dataDirectory;
            int prefer = -1;
            for (std::size_t i = 0; i < db.levels.size(); ++i) {
                const std::string ldb = db.levels[i].absoluteLdbPath(db.dataDirectory);
                if (!isFile(ldb)) {
                    std::fprintf(stderr, "levels.txt: missing %s\n", ldb.c_str());
                    continue;
                }
                if (db.levels[i].startupLevel && prefer < 0) {
                    prefer = static_cast<int>(levelPaths_.size());
                }
                levelPaths_.push_back(ldb);
                levelInfos_.push_back(db.levels[i]);
            }
            if (prefer >= 0) {
                levelIndex_ = prefer;
            } else if (!levelPaths_.empty()) {
                levelIndex_ = 0;
            }
            std::fprintf(stderr, "levels.txt: %s  (%zu maps)\n", levelsTxt.c_str(),
                         levelPaths_.size());
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "levels.txt parse failed (%s): %s\n", levelsTxt.c_str(),
                         ex.what());
            levelPaths_.clear();
            levelInfos_.clear();
        }
    }

    if (levelPaths_.empty()) {
        for (std::size_t i = 0; i < searchDirs.size(); ++i) {
            std::vector<std::string> found = listFilesWithExtensionRecursive(searchDirs[i], ".ldb");
            if (found.empty()) {
                found = listFilesWithExtension(searchDirs[i], ".ldb");
            }
            if (found.empty()) {
                continue;
            }
            levelsDir_ = searchDirs[i];
            levelPaths_ = found;
            break;
        }
    }

    if (!explicitFile.empty()) {
        const int existing = findPathIndex(levelPaths_, explicitFile);
        if (existing >= 0) {
            levelIndex_ = existing;
        } else {
            levelPaths_.insert(levelPaths_.begin(), explicitFile);
            if (!levelInfos_.empty()) {
                levelInfos_.insert(levelInfos_.begin(), LevelInfo());
            }
            levelIndex_ = 0;
            if (levelsDir_.empty()) {
                levelsDir_ = parentDir(explicitFile);
            }
        }
    } else if (levelIndex_ < 0 && !levelPaths_.empty()) {
        int prefer = findPathIndex(levelPaths_, "Part1_Level1.ldb");
        if (prefer < 0) {
            prefer = findPathIndex(levelPaths_, "Part0_Level1.ldb");
        }
        levelIndex_ = prefer >= 0 ? prefer : 0;
    }
}

void ViewerApp::drawLoadingFrame(const std::string& message) {
    glViewport(0, 0, width_, height_);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer_.drawHudText(12, 12, "MAXFX Level Viewer", 0.95f, 0.85f, 0.45f);
    renderer_.drawHudText(12, 32, message.c_str(), 0.85f, 0.85f, 0.85f);
    renderer_.presentHud();
    SDL_GL_SwapWindow(window_);
}

bool ViewerApp::loadLevelIndex(int index, bool showLoading) {
    if (index < 0 || index >= static_cast<int>(levelPaths_.size())) {
        return false;
    }
    const std::string& path = levelPaths_[static_cast<std::size_t>(index)];
    fileName_ = fileName(path);
    startPlace_.clear();
    if (index >= 0 && static_cast<std::size_t>(index) < levelInfos_.size()) {
        startPlace_ = levelInfos_[static_cast<std::size_t>(index)].playerStartingPlace;
        if (!levelInfos_[static_cast<std::size_t>(index)].levelName.empty()) {
            fileName_ = levelInfos_[static_cast<std::size_t>(index)].levelName + "  (" +
                        fileName_ + ")";
        }
    }
    if (showLoading && window_) {
        char msg[512];
        std::snprintf(msg, sizeof(msg), "Loading %s  (%d / %zu) ...", fileName_.c_str(), index + 1,
                      levelPaths_.size());
        drawLoadingFrame(msg);
    }

    Level loaded;
    try {
        loaded = LdbReader::loadFromFile(path);
    } catch (const std::exception& ex) {
        statusMessage_ = std::string("failed: ") + ex.what();
        std::fprintf(stderr, "%s\n", statusMessage_.c_str());
        return false;
    }

    if (!database_.root.empty()) {
        std::vector<std::string> skins;
        std::vector<std::string> items;
        for (std::size_t i = 0; i < loaded.characters.size(); ++i) {
            skins.push_back(loaded.characters[i].characterName);
        }
        for (std::size_t i = 0; i < loaded.items.size(); ++i) {
            items.push_back(loaded.items[i].itemName);
        }
        DatabaseReader::loadModels(database_, skins, items);
        std::string sphere;
        if (index >= 0 && static_cast<std::size_t>(index) < levelInfos_.size()) {
            sphere = levelInfos_[static_cast<std::size_t>(index)].worldSphereName;
        }
        DatabaseReader::loadWorldSphere(database_, sphere);
        audio_.playLevel(database_, sphere);
        std::vector<SoundCueRequest> cues;
        collectLevelSoundCues(loaded, &cues);
        audio_.startCues(database_, cues);
    } else {
        audio_.playLevel(database_, std::string());
        audio_.startCues(database_, std::vector<SoundCueRequest>());
    }

    char error[1024];
    if (!renderer_.loadLevel(loaded, database_.root.empty() ? 0 : &database_, error, sizeof(error))) {
        statusMessage_ = std::string("gpu upload failed: ") + error;
        std::fprintf(stderr, "%s\n", statusMessage_.c_str());
        return false;
    }

    level_ = loaded;
    levelIndex_ = index;
    levelLoaded_ = true;
    statusMessage_.clear();
    renderer_.resize(width_, height_);
    spawnActors();
    placeAtSpawn(-1);
    game_.resetLevel(level_,
                     (index >= 0 && static_cast<std::size_t>(index) < levelInfos_.size())
                         ? &levelInfos_[static_cast<std::size_t>(index)]
                         : 0,
                     ldbFromView(camera_.position, game_.player.eyeHeight), camera_.yaw);
    // Movement network: <level>.ai beside the LDB (X_GlobalAIObject).
    {
        std::string aiPath = path;
        if (aiPath.size() > 4 && aiPath.compare(aiPath.size() - 4, 4, ".ldb") == 0) {
            aiPath.replace(aiPath.size() - 4, 4, ".ai");
        } else {
            aiPath += ".ai";
        }
        game_.loadLevelAi(existingPathIgnoreCase(aiPath), level_);
    }
    playerSkinName_ =
        (index >= 0 && static_cast<std::size_t>(index) < levelInfos_.size() &&
         !levelInfos_[static_cast<std::size_t>(index)].playerSkinName.empty())
            ? levelInfos_[static_cast<std::size_t>(index)].playerSkinName
            : std::string("max_payne");
    const SkinDef* playerSkin = database_.findSkin(playerSkinName_);
    game_.applyPlayerOnInit(playerSkin != 0 ? &playerSkin->character : 0);
    cinematicClipCursor_ = -1;
    // Camera paths parented to the character (CAM_AnimateParented) need a
    // sane origin even before any clip ran: anchor the cinematic entity to
    // the spawn point.
    cinematicStartEntity_ = makeEntity(game_.player.position, -game_.player.yaw);
    cinematicEntity_ = cinematicStartEntity_;
    wasCineActive_ = false;
    wasCineOn_ = false;
    game_.mode = kModePlaying;
    // X_LevelRuntimeFSM startup messages run at level load — on Part1_Level1
    // ::startroom::fsm_start queues the intro graphic-novel pages and switches
    // to the reader once.
    game_.startLevel(level_, actors_);
    mouseCaptured_ = true;
    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, true);
    }
    syncCameraFromPlayer();

    char title[256];
    std::snprintf(title, sizeof(title), "MAXFX Level Viewer b%s  -  %s  [%d/%zu]", MAXFX_BUILD_HASH,
                  fileName_.c_str(), levelIndex_ + 1, levelPaths_.size());
    if (window_) {
        SDL_SetWindowTitle(window_, title);
    }
    return true;
}

void ViewerApp::cycleLevel(int delta) {
    if (levelPaths_.size() < 2) {
        return;
    }
    int next = levelIndex_ + delta;
    const int count = static_cast<int>(levelPaths_.size());
    if (next < 0) {
        next = count - 1;
    }
    if (next >= count) {
        next = 0;
    }
    loadLevelIndex(next, true);
}

void ViewerApp::placeAtSpawn(int index) {
    const std::vector<SpawnPoint>& spawns = renderer_.spawns();
    if (spawns.empty()) {
        camera_.position = Vec3(0, 1.6f, 0);
        camera_.yaw = 0;
        camera_.pitch = 0;
        spawnIndex_ = -1;
        return;
    }
    if (index < 0) {
        index = 0;
        const std::string want = lowerCopy(startPlace_);
        for (std::size_t i = 0; i < spawns.size(); ++i) {
            const std::string name = lowerCopy(spawns[i].name);
            if (!want.empty() && name == want) {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index == 0 && !want.empty()) {
            for (std::size_t i = 0; i < spawns.size(); ++i) {
                const std::string name = lowerCopy(spawns[i].name);
                if (name.find(want) != std::string::npos || want.find(name) != std::string::npos) {
                    index = static_cast<int>(i);
                    break;
                }
            }
        }
        if (index == 0) {
            for (std::size_t i = 0; i < spawns.size(); ++i) {
                if (spawns[i].name.find("startroom") != std::string::npos &&
                    spawns[i].name.find("Jumppoint") != std::string::npos) {
                    index = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (index >= static_cast<int>(spawns.size())) {
        index = 0;
    }
    spawnIndex_ = index;
    camera_.position = spawns[static_cast<std::size_t>(index)].position;
    camera_.yaw = spawns[static_cast<std::size_t>(index)].yaw;
    camera_.pitch = 0.0f;
}

void ViewerApp::syncCameraFromPlayer() {
    camera_.position = game_.viewPosition();
    camera_.yaw = game_.viewYaw();
    camera_.pitch = game_.player.pitch;
}

Vec3 ViewerApp::playerLookLdb() const {
    const Vec3 f = camera_.forward();
    return Vec3(-f.x, f.y, f.z);
}

void ViewerApp::enterLevel(int index) { loadLevelIndex(index, true); }

void ViewerApp::enterComic(int page, bool fromMenu) {
    game_.comicIndex = page;
    game_.comicFromMenu = fromMenu;
    game_.mode = kModeGraphicNovel;
    mouseCaptured_ = false;
    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, false);
    }
    lastComicPage_ = -1;
    playComicSound();
}

void ViewerApp::leaveComic() {
    audio_.stop2d();
    lastComicPage_ = -1;
    // MPGNM_PickUpNote queue: show the next picked note (level intro pages,
    // in-game note pickups) before returning to the game.
    if (!game_.comicFromMenu) {
        const int next = game_.nextPendingNote();
        if (next >= 0) {
            game_.comicIndex = next;
            game_.mode = kModeGraphicNovel;
            playComicSound();
            return;
        }
    }
    if (game_.comicFromMenu || !levelLoaded_) {
        game_.mode = kModeMenu;
        game_.menu = kMenuComicPages;
        if (game_.catalog.chapters.empty()) {
            game_.menu = kMenuComic;
        }
        mouseCaptured_ = false;
        if (window_) {
            SDL_SetWindowRelativeMouseMode(window_, false);
        }
        return;
    }
    game_.mode = kModePlaying;
    mouseCaptured_ = true;
    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, true);
    }
}

void ViewerApp::stepComic(int delta) {
    const int n = static_cast<int>(game_.catalog.pages.size());
    if (n <= 0) {
        return;
    }
    int next = game_.comicIndex + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= n) {
        next = n - 1;
    }
    if (next == game_.comicIndex) {
        return;
    }
    game_.comicIndex = next;
    playComicSound();
}

void ViewerApp::playComicSound() {
    if (game_.comicIndex < 0 ||
        static_cast<std::size_t>(game_.comicIndex) >= game_.catalog.pages.size()) {
        return;
    }
    if (game_.comicIndex == lastComicPage_) {
        return;
    }
    lastComicPage_ = game_.comicIndex;
    const GraphicNovelPageDef& page = game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)];
    const std::vector<GameMessage> msgs = parseGameMessages(page.initSound);
    bool played = false;
    for (std::size_t i = 0; i < msgs.size(); ++i) {
        if (methodIs(msgs[i], "a_playsound") && msgs[i].args.size() >= 2) {
            audio_.play2d(database_, msgs[i].args[0], msgs[i].args[1]);
            played = true;
            break;
        }
    }
    if (!played) {
        audio_.stop2d();
    }
}

void ViewerApp::handleMouseButton(float x, float y, int button) {
    if (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT) {
        return;
    }
    if (game_.mode == kModeGraphicNovel) {
        if (button == SDL_BUTTON_RIGHT || x < 80.0f) {
            stepComic(-1);
        } else {
            stepComic(1);
        }
        return;
    }
    if (game_.mode != kModeMenu) {
        return;
    }
    if (game_.menu == kMenuRoot) {
        for (int i = 0; i < 4; ++i) {
            if (hitRow(x, y, 36, 76 + i * 20, 270, 18)) {
                game_.menuCursor = i;
                int jump = -1;
                int page = -1;
                bool quit = false;
                bool newGame = false;
                game_.menuChoose(&jump, &page, &quit, &newGame);
                if (quit) {
                    running_ = false;
                } else if (newGame) {
                    int start = 0;
                    for (std::size_t k = 0; k < levelInfos_.size(); ++k) {
                        if (levelInfos_[k].startupLevel) {
                            start = static_cast<int>(k);
                            break;
                        }
                    }
                    enterLevel(start);
                }
                return;
            }
        }
        return;
    }
    if (game_.menu == kMenuJumpLevel) {
        int start = game_.menuCursor - 8;
        if (start < 0) {
            start = 0;
        }
        const int n = static_cast<int>(levelPaths_.size());
        for (int i = start; i < n && i < start + 16; ++i) {
            if (hitRow(x, y, 36, 90 + (i - start) * 16, 510, 16)) {
                enterLevel(i);
                return;
            }
        }
        return;
    }
    if (game_.menu == kMenuComic) {
        int start = game_.menuCursor - 8;
        if (start < 0) {
            start = 0;
        }
        const int n = static_cast<int>(game_.catalog.chapters.size());
        for (int i = start; i < n && i < start + 16; ++i) {
            if (hitRow(x, y, 36, 90 + (i - start) * 16, 550, 16)) {
                game_.menuCursor = i;
                game_.comicChapter = i;
                game_.menu = kMenuComicPages;
                game_.menuCursor = 0;
                return;
            }
        }
        return;
    }
    if (game_.menu == kMenuComicPages) {
        int start = game_.menuCursor - 8;
        if (start < 0) {
            start = 0;
        }
        const GraphicNovelChapter* ch = 0;
        if (game_.comicChapter >= 0 &&
            static_cast<std::size_t>(game_.comicChapter) < game_.catalog.chapters.size()) {
            ch = &game_.catalog.chapters[static_cast<std::size_t>(game_.comicChapter)];
        }
        const int n =
            ch ? static_cast<int>(ch->pageIndices.size()) : static_cast<int>(game_.catalog.pages.size());
        for (int i = start; i < n && i < start + 16; ++i) {
            if (hitRow(x, y, 36, 106 + (i - start) * 16, 550, 16)) {
                int page = i;
                if (ch && static_cast<std::size_t>(i) < ch->pageIndices.size()) {
                    page = ch->pageIndices[static_cast<std::size_t>(i)];
                }
                enterComic(page, true);
                return;
            }
        }
    }
}

void ViewerApp::handleKeyDown(int scancode, int key) {
    if (game_.mode == kModeMenu) {
        if (key == SDLK_ESCAPE) {
            if (game_.menu == kMenuComicPages) {
                game_.menu = kMenuComic;
                game_.menuCursor = game_.comicChapter;
            } else if (game_.menu != kMenuRoot) {
                game_.menu = kMenuRoot;
                game_.menuCursor = 0;
            } else {
                running_ = false;
            }
            return;
        }
        if (scancode == SDL_SCANCODE_UP || scancode == SDL_SCANCODE_W) {
            game_.menuMove(-1, static_cast<int>(levelPaths_.size()));
            return;
        }
        if (scancode == SDL_SCANCODE_DOWN || scancode == SDL_SCANCODE_S) {
            game_.menuMove(1, static_cast<int>(levelPaths_.size()));
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            int jump = -1;
            int page = -1;
            bool quit = false;
            bool newGame = false;
            game_.menuChoose(&jump, &page, &quit, &newGame);
            if (quit) {
                running_ = false;
            } else if (newGame) {
                int start = 0;
                for (std::size_t i = 0; i < levelInfos_.size(); ++i) {
                    if (levelInfos_[i].startupLevel) {
                        start = static_cast<int>(i);
                        break;
                    }
                }
                enterLevel(start);
            } else if (jump >= 0) {
                enterLevel(jump);
            } else if (page >= 0) {
                enterComic(page, true);
            }
            return;
        }
        return;
    }
    if (game_.mode == kModeGraphicNovel) {
        if (key == SDLK_ESCAPE) {
            leaveComic();
            return;
        }
        if (scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_A) {
            stepComic(-1);
            return;
        }
        if (scancode == SDL_SCANCODE_RIGHT || scancode == SDL_SCANCODE_D || key == SDLK_RETURN ||
            key == SDLK_SPACE) {
            stepComic(1);
            return;
        }
        return;
    }
    if (scancode == SDL_SCANCODE_LEFTBRACKET) {
        renderer_.cycleRoom(-1, static_cast<int>(level_.rooms.size()));
        return;
    }
    if (scancode == SDL_SCANCODE_RIGHTBRACKET) {
        renderer_.cycleRoom(1, static_cast<int>(level_.rooms.size()));
        return;
    }
    switch (key) {
        case SDLK_ESCAPE:
            if (game_.cine.active || game_.cine.cameraActive) {
                // Esc always aborts the whole cutscene: clip, camera path,
                // fade overlay and letterbox (a held fade-to-black used to
                // keep the screen black after the clip had already ended).
                game_.abortCinematic();
                syncCameraFromPlayer();
                camera_.pitch = 0.0f;
                break;
            }
            game_.mode = kModeMenu;
            game_.menu = kMenuRoot;
            game_.menuCursor = 0;
            mouseCaptured_ = false;
            SDL_SetWindowRelativeMouseMode(window_, false);
            break;
        case SDLK_F1:
            showHelp_ = !showHelp_;
            break;
        case SDLK_F2:
            renderer_.setWireframe(!renderer_.wireframe());
            break;
        case SDLK_F3:
            renderer_.cycleShading();
            break;
        case SDLK_F4:
            renderer_.setShowHelpers(!renderer_.showHelpers());
            break;
        case SDLK_F5:
            renderer_.setShowDynamic(!renderer_.showDynamic());
            break;
        case SDLK_F6:
            renderer_.setShowService(!renderer_.showService());
            break;
        case SDLK_F7:
            audio_.toggleMuted();
            break;
        case SDLK_F8:
            showItemDebug_ = !showItemDebug_;
            break;
        case SDLK_F10:
        case SDLK_N:
            game_.player.noclip = !game_.player.noclip;
            game_.pushLog(game_.player.noclip ? "noclip on" : "noclip off");
            break;
        case SDLK_H:
            renderer_.setShowHud(!renderer_.showHud());
            break;
        case SDLK_R:  // C_Reload
            if (!game_.reloadWeapon()) {
                game_.pushLog(game_.player.clip >= game_.player.clipSize
                                  ? "reload: clip full"
                                  : "reload: no reserve ammo");
            }
            weaponListTimer_ = 1.5f;
            break;
        case SDLK_C:
            startNextCinematic();
            break;
        case SDLK_F9:  // respawn at the starting place
            placeAtSpawn(-1);
            game_.player.position = ldbFromView(camera_.position, game_.player.eyeHeight);
            game_.player.yaw = camera_.yaw;
            game_.player.velocity = Vec3();
            break;
        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
        case SDLK_5:
        case SDLK_6:
        case SDLK_7:
        case SDLK_8:
        case SDLK_9:
        case SDLK_0: {
            // C_SelectWeapon via the 1..0 slot row.
            const int slot = key == SDLK_0 ? 9 : key - SDLK_1;
            if (game_.selectSlot(slot)) {
                weaponListTimer_ = 1.5f;
            }
            break;
        }
        default:
            break;
    }
}

void ViewerApp::update(float dt) {
    if (game_.mode == kModeGraphicNovel) {
        audio_.setEnvPaused(true);
        playComicSound();
        audio_.pump();
        return;
    }
    if (game_.mode != kModePlaying || !levelLoaded_) {
        audio_.setEnvPaused(true);
        audio_.pump();
        return;
    }
    audio_.setEnvPaused(false);
    const bool* keys = SDL_GetKeyboardState(0);
    // GM_ChangeGameSpeed / bullet time: the simulation runs on scaled time
    // (movement, actors, doors, triggers, effects, cinematic), while audio
    // and UI stay on real time.
    game_.tickGameSpeed(dt);
    const float gdt = dt * game_.gameSpeed;
    const bool scriptOwned = game_.cine.active;  // the clip drives the player
    if (!scriptOwned) {
        const bool jump = keys[SDL_SCANCODE_SPACE] != 0;
        const bool sprint = keys[SDL_SCANCODE_LSHIFT] != 0;
        game_.player.yaw = camera_.yaw;
        game_.player.pitch = camera_.pitch;
        game_.tickPlayer(gdt, keys[SDL_SCANCODE_W] != 0, keys[SDL_SCANCODE_S] != 0,
                         keys[SDL_SCANCODE_A] != 0, keys[SDL_SCANCODE_D] != 0, jump, sprint,
                         collision_, keys[SDL_SCANCODE_LCTRL] != 0);
        syncCameraFromPlayer();
        // Only the camera path is left: moving cancels an Abortable path
        // (X_CameraImplementation::userAbortCameraPathIfAbortable), so a long
        // path never traps the player after the clip already ended.
        if (game_.cine.cameraActive && game_.cameraPathAbortable()) {
            const bool moved = keys[SDL_SCANCODE_W] != 0 || keys[SDL_SCANCODE_A] != 0 ||
                               keys[SDL_SCANCODE_S] != 0 || keys[SDL_SCANCODE_D] != 0 ||
                               keys[SDL_SCANCODE_SPACE] != 0;
            if (moved) {
                game_.abortCameraPath(level_, actors_);
                syncCameraFromPlayer();
            }
        }
    }
    // Cutscene timing (clip frame hooks, fades, camera path + [Exit]) always
    // advances, even when the player is controllable again.
    tickCinematicFrame(gdt);
    const Vec3 look = playerLookLdb();
    const Vec3 eye = Vec3(game_.player.position.x, game_.player.position.y + game_.player.eyeHeight,
                          game_.player.position.z);
    audio_.setListener(eye, look);
    static bool useWasDown = false;
    const bool useNow = !scriptOwned && keys[SDL_SCANCODE_E] != 0;
    const bool usePress = useNow && !useWasDown;
    useWasDown = useNow;
    game_.tickDoors(gdt, level_);
    // Door collision refresh: only when a dynamic object crossed the
    // half-open threshold (a full rebuild per frame would be wasteful).
    if (doorSolid_.size() == level_.dynamicMeshes.size()) {
        bool changed = false;
        for (std::size_t i = 0; i < level_.dynamicMeshes.size() && !changed; ++i) {
            if (!level_.dynamicMeshes[i].config.dynamicCollisions) {
                continue;
            }
            const float t = i < game_.doors.size() ? game_.doors[i].t : 0.0f;
            if ((t < 0.5f) != (doorSolid_[i] != 0)) {
                changed = true;
            }
        }
        if (changed) {
            rebuildCollision();
        }
    }
    game_.tickTriggers(gdt, level_, usePress, look, actors_);
    // Runtime-queued one-shots (trigger / door / weapon / impact messages).
    audio_.playRequests(database_, game_.pendingSounds);
    game_.pendingSounds.clear();
    game_.effects.update(gdt);
    if (weaponListTimer_ > 0.0f) {
        weaponListTimer_ -= dt;
    }
    if (game_.mode == kModeGraphicNovel) {
        mouseCaptured_ = false;
        if (window_) {
            SDL_SetWindowRelativeMouseMode(window_, false);
        }
        // playComicSound no-ops while the page is unchanged; resetting
        // lastComicPage_ here used to restart the narration every frame.
        playComicSound();
        audio_.setEnvPaused(true);
        audio_.pump();
        return;
    }
    updateActors(gdt);
    audio_.pump();
}

void ViewerApp::rebuildCollision() {
    collision_.clear();
    collision_.addLevelGeometry(level_);
    // Closed dynamic objects stay solid (X_LevelRuntimeDynamicObject
    // collisions); open ones stop blocking after the half-way point.
    doorSolid_.assign(level_.dynamicMeshes.size(), 0);
    for (std::size_t i = 0; i < level_.dynamicMeshes.size(); ++i) {
        const DynamicMesh& mesh = level_.dynamicMeshes[i];
        if (!mesh.config.dynamicCollisions) {
            continue;
        }
        float t = 0.0f;
        if (i < game_.doors.size()) {
            t = game_.doors[i].t;
        }
        if (t < 0.5f && !mesh.animations.empty()) {
            const Mat4x3 roomX = Renderer::roomMatrix(level_, mesh.properties.roomId);
            const Mat4x3 world = combine(roomX, mesh.properties.objectToRoom);
            collision_.addDynamicMesh(mesh, level_.dynamicTextureVertices, world);
            doorSolid_[i] = 1;
        }
    }
}

void ViewerApp::spawnActors() {
    rebuildCollision();
    actors_.clear();
    for (std::size_t i = 0; i < level_.characters.size(); ++i) {
        const Character& ch = level_.characters[i];
        const Mat4x3 roomX = roomWorldTransform(level_, ch.properties.roomId);
        const Mat4x3 world = combine(roomX, ch.properties.objectToRoom);
        const Vec3 z = world.zAxis();
        const float yaw = std::atan2(z.x, z.z);
        const SkinDef* def = database_.findSkin(ch.characterName);
        CharacterActor actor;
        actor.spawn(world.translation(), yaw, ch.properties.roomId,
                    def != 0 ? &def->character : 0, ch.characterName, ch.sharedName);
        // Skin [OnInit] (e.g. this->C_PickupWeapon("beretta")) runs first,
        // then the LDB startup list can override it (C_RemoveAllWeapons).
        if (def != 0) {
            game_.applyCharacterMessages(actor, def->character.onInitMessages);
        }
        game_.applyCharacterStartup(actor, ch.onStartup);
        actors_.push_back(actor);
    }
}

// X_CRSplineMovementUpdate moves a character from the origin to the clip's
// [Movement] EndPosition over the clip duration, so the locomotion speed is
// |EndPosition| / clipLength. clipWalkSpeed() alone assumes 1-second clips,
// which makes NPCs skate on longer walk cycles. Load the walk (or run) clip
// once per actor and cache the real speed on the actor.
void ViewerApp::fillActorMoveSpeed(CharacterActor& actor) {
    if (actor.moveSpeed != 0.0f || actor.config == 0) {
        return;  // resolved ( > 0), failed before ( < 0), or no config
    }
    const SkinDef* def = database_.findSkin(actor.skinName);
    if (def == 0) {
        actor.moveSpeed = -1.0f;
        return;
    }
    static const int kLocomotion[] = {kCharAnimWalk, kCharAnimRun};
    for (std::size_t c = 0; c < sizeof(kLocomotion) / sizeof(kLocomotion[0]); ++c) {
        const CharacterAnimClip* clip = findAnimClip(def->character, kLocomotion[c]);
        if (clip == 0 || clip->resolvedPath.empty()) {
            continue;
        }
        const Kf2File* anim = database_.loadModel(clip->resolvedPath);
        if (anim == 0 || anim->animations.empty()) {
            continue;
        }
        const float duration = kf2AnimationDuration(*anim);
        const float distance = length(clip->endPosition);
        if (duration > 1.0e-4f && distance > 1.0e-4f) {
            actor.moveSpeed = distance / duration;
            return;
        }
    }
    actor.moveSpeed = -1.0f;  // keep the script-only estimate
}

void ViewerApp::updateActors(float dt) {
    const Vec3 playerLdb(-camera_.position.x, camera_.position.y - 1.6f, camera_.position.z);
    AiUpdateContext ai;
    ai.graph = &game_.aiGraph;
    ai.playerAlive = game_.player.health > 0.0f;
    ai.playerShotRecently = game_.playerShotRecently();
    ai.playerSpeed = length(game_.player.velocity);
    ai.playerPosition = playerLdb;
    std::vector<AiFireEvent> fires;
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        fillActorMoveSpeed(actors_[i]);
        actors_[i].update(dt, playerLdb, collision_, &actors_, &ai, &fires,
                          static_cast<int>(i));
    }
    for (std::size_t i = 0; i < fires.size(); ++i) {
        game_.enemyFire(fires[i], level_, collision_, actors_);
    }
    // X_Characteristic onActivate: runs once when the character's AI
    // activates (perception / combat state machine switch) — this starts
    // the staged fights (e.g. ::teleport::e1 -> ::p5::script FSM_Send).
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        if (actors_[i].aiActive) {
            game_.activateCharacter(static_cast<int>(i), level_, actors_);
        }
    }
    renderer_.beginAnimated();
    const Vec3 camLdb(-camera_.position.x, camera_.position.y, camera_.position.z);
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        CharacterActor& actor = actors_[i];
        const Vec3 toCam = actor.position - camLdb;
        if (toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z > 40.0f * 40.0f) {
            continue;
        }
        const SkinDef* def = database_.findSkin(actor.skinName);
        if (def == 0 || def->lods.empty()) {
            continue;
        }
        const Kf2File* mesh = database_.model(def->lods[0].resolvedExport);
        if (mesh == 0) {
            continue;
        }
        const Kf2File* skin = database_.model(def->lods[0].resolvedSkin);
        const CharacterAnimClip* poseClip = findAnimClip(def->character, kCharAnimPose);
        const CharacterAnimClip* playClip = findAnimClip(def->character, actor.animIndex);
        const Kf2File* bindAnim = poseClip != 0 ? database_.loadModel(poseClip->resolvedPath) : 0;
        const Kf2File* playAnim = playClip != 0 ? database_.loadModel(playClip->resolvedPath) : bindAnim;
        // Cross-fade from the previous clip (crossAnimateObject).
        const Kf2File* crossAnim = 0;
        float crossBlend = 0.0f;
        if (actor.prevAnimIndex >= 0 && actor.blendTime > 0.0f) {
            const CharacterAnimClip* prevClip = findAnimClip(def->character, actor.prevAnimIndex);
            crossAnim = prevClip != 0 ? database_.loadModel(prevClip->resolvedPath) : 0;
            crossBlend = 1.0f - actor.blendTime / 0.25f;
        }
        renderer_.appendAnimatedCharacter(*mesh, skin, bindAnim, playAnim, actor.animTime,
                                          actor.entityTransform(), actor.roomId, true, crossAnim,
                                          actor.prevAnimTime, crossBlend);
    }
    // Dynamic level objects (doors, platforms): the geometry sits in
    // persistent GPU buffers; the animated transform from tickDoors only
    // updates the per-batch uWorld matrix (no per-frame re-upload).
    for (std::size_t i = 0; i < level_.dynamicMeshes.size(); ++i) {
        const DynamicMesh& mesh = level_.dynamicMeshes[i];
        Mat4x3 local = mesh.properties.objectToRoom;
        float t = 0.0f;
        if (i < game_.doors.size()) {
            t = game_.doors[i].t;
        }
        if (t > 0.0f && !mesh.animations.empty()) {
            const MeshAnimation& anim = mesh.animations[0];
            const Mat4x3 pose = game_.dynamicMeshPose(anim, t);
            // pose(t) is authored from startTransform; re-base it onto
            // the bind placement so t=0 is seamless.
            const Mat4x3 rebased =
                combine(pose, combine(inverseRigid(anim.startTransform), local));
            local = rebased;
        }
        const Mat4x3 world = combine(Renderer::roomMatrix(level_, mesh.properties.roomId), local);
        renderer_.setDynamicMeshWorld(i, world);
    }

    // The player acts in cutscenes: render his skin with the cinematic clip
    // (root NOT locked to the bind pose — the movement file carries him).
    if (game_.cine.active) {
        const SkinDef* def = database_.findSkin(playerSkinName_);
        if (def != 0 && !def->lods.empty()) {
            const Kf2File* mesh = database_.model(def->lods[0].resolvedExport);
            if (mesh != 0) {
                const Kf2File* skinKf = database_.model(def->lods[0].resolvedSkin);
                const CharacterAnimClip* poseClip = findAnimClip(def->character, kCharAnimPose);
                const CharacterAnimClip* playClip =
                    findAnimClip(def->character, game_.cine.clipIndex);
                const Kf2File* bindAnim =
                    poseClip != 0 ? database_.loadModel(poseClip->resolvedPath) : 0;
                const Kf2File* playAnim =
                    playClip != 0 ? database_.loadModel(playClip->resolvedPath) : bindAnim;
                renderer_.appendAnimatedCharacter(*mesh, skinKf, bindAnim, playAnim,
                                                  game_.cine.time, cinematicEntity_, -1, false);
            }
        }
    }
}

void ViewerApp::drawWeaponHud() {
    if (!levelLoaded_ || !game_.cine.hudVisible) {
        return;
    }
    const PlayerState& pl = game_.player;
    const WeaponDef* def = game_.currentWeaponDef();
    // hud.txt is authored for 640x480; keep the anchor geometry on other
    // window sizes.
    const float scale = height_ > 0 ? static_cast<float>(height_) / 480.0f : 1.0f;

    if (hud_.valid) {
        // [Health] sprite (and its background) at the bottom-left.
        if (hud_.healthBackground.valid()) {
            const HudSprite& sp = hud_.healthBackground;
            renderer_.drawHudImage(hud_.healthBackground.filename, hud_.healthBackground.alphaFilename,
                                   sp.position[0] * scale, sp.position[1] * scale, sp.width * scale,
                                   sp.height * scale, hudReferencePoint(sp.referencePoint), sp.alpha);
        }
        if (hud_.healthSprite.valid()) {
            const HudSprite& sp = hud_.healthSprite;
            renderer_.drawHudImage(sp.filename, sp.alphaFilename, sp.position[0] * scale,
                                   sp.position[1] * scale, sp.width * scale, sp.height * scale,
                                   hudReferencePoint(sp.referencePoint), sp.alpha);
        }
        // [ActiveWeapon] sprite + ammo counters for the selected weapon.
        if (def != 0) {
            const HudWeaponSprite* ws = findHudWeapon(hud_, def->weaponId);
            if (ws != 0 && ws->sprite.valid()) {
                const HudSprite& sp = ws->sprite;
                renderer_.drawHudImage(sp.filename, sp.alphaFilename, sp.position[0] * scale,
                                       sp.position[1] * scale, sp.width * scale, sp.height * scale,
                                       hudReferencePoint(sp.referencePoint), sp.alpha);
            }
        }
    }

    // Ammo counters: clip / pocket in the lower right (PC hud.txt positions
    // when available, otherwise the fixed viewer spot).
    char line[128];
    std::snprintf(line, sizeof(line), "%d / %d", pl.clip, pl.ammo);
    const float ax = hud_.valid && hud_.ammoInClipsText.valid
                         ? hud_.ammoInClipsText.position[0] * scale
                         : static_cast<float>(width_) - 120.0f;
    const float ay = hud_.valid && hud_.ammoInClipsText.valid
                         ? hud_.ammoInClipsText.position[1] * scale
                         : static_cast<float>(height_) - 60.0f;
    renderer_.drawHudText(static_cast<int>(ax), static_cast<int>(ay), line, 0.95f, 0.92f, 0.6f);
    if (def != 0 && !def->projectileName.empty() && def->clipSize == 0) {
        // Thrown weapons show no clip; just the pocket count.
        std::snprintf(line, sizeof(line), "%d", pl.ammo);
        renderer_.drawHudText(static_cast<int>(ax), static_cast<int>(ay), line, 0.95f, 0.92f, 0.6f);
    }

    // Weapon cycle overlay: the slot row, kept visible a moment after a
    // selection change (like the PC quick-select strip).
    if (weaponListTimer_ > 0.0f && !pl.slots.empty()) {
        const int rowH = 16;
        int y = height_ / 2 - static_cast<int>(pl.slots.size()) * rowH / 2;
        for (std::size_t i = 0; i < pl.slots.size(); ++i) {
            const bool sel = static_cast<int>(i) == pl.selectedSlot;
            std::snprintf(line, sizeof(line), "%s%d  %s  %d+%d", sel ? "> " : "  ",
                          static_cast<int>(i + 1) % 10, pl.slots[i].name.c_str(), pl.slots[i].clip,
                          pl.slots[i].ammo);
            renderer_.drawHudText(width_ / 2 - 90, y, line, sel ? 1.0f : 0.7f, sel ? 0.9f : 0.7f,
                                  sel ? 0.45f : 0.65f);
            y += rowH;
        }
    }
}

void ViewerApp::startNextCinematic() {
    if (!levelLoaded_) {
        return;
    }
    if (game_.cine.active || game_.cine.cameraActive) {
        game_.abortCinematic();
        syncCameraFromPlayer();
    }
    const SkinDef* def = database_.findSkin(playerSkinName_);
    if (def == 0) {
        statusMessage_ = "cinematic: player skin not found";
        return;
    }
    // Scripted clips: any clip with [Message] Frame hooks (the cinematics on
    // the stock skins are CHARANIM_CUSTOM* / cinematic blocks). Clips whose
    // hooks direct the scene (camera paths, fades, game speed, sounds,
    // teleports) come first — real level cinematics are per-level, so on
    // stock skins this falls back to gameplay clips (footstep dust, reload,
    // bullet-time) which still exercise the whole hook machinery.
    std::vector<const CharacterAnimClip*> scripted;
    std::vector<const CharacterAnimClip*> directed;
    for (std::size_t i = 0; i < def->character.animations.size(); ++i) {
        const CharacterAnimClip& clip = def->character.animations[i];
        if (clip.frameMessages.empty()) {
            continue;
        }
        scripted.push_back(&clip);
        for (std::size_t m = 0; m < clip.frameMessages.size(); ++m) {
            const std::string& t = clip.frameMessages[m].text;
            if (t.find("cam_animate") != std::string::npos ||
                t.find("mphm_fadetocolor") != std::string::npos ||
                t.find("mphm_showintroductionsprite") != std::string::npos ||
                t.find("gm_enablewidescreen") != std::string::npos ||
                t.find("gm_changegamespeed") != std::string::npos ||
                t.find("c_enablecinematicmode") != std::string::npos ||
                t.find("c_teleport") != std::string::npos ||
                t.find("a_play3dsound") != std::string::npos) {
                directed.push_back(&clip);
                break;
            }
        }
    }
    if (scripted.empty()) {
        statusMessage_ = "cinematic: no scripted clips on " + playerSkinName_;
        return;
    }
    if (!directed.empty() && directed.size() < scripted.size()) {
        scripted = directed;  // prefer directed clips while any remain
    }
    cinematicClipCursor_ = (cinematicClipCursor_ + 1) % static_cast<int>(scripted.size());
    const CharacterAnimClip* clip = scripted[static_cast<std::size_t>(cinematicClipCursor_)];
    float duration = 0.0f;
    int fps = 30;
    const Kf2File* anim = database_.loadModel(clip->resolvedPath);
    if (anim != 0 && !anim->animations.empty()) {
        duration = kf2AnimationDuration(*anim);
        fps = anim->animations[0].frameRate;
    }
    if (duration <= 0.0f && !clip->frameMessages.empty()) {
        // KF2 missing (stripped database): run to the last frame hook + 1s
        // so the cutscene still terminates instead of hanging forever.
        int last = 0;
        for (std::size_t m = 0; m < clip->frameMessages.size(); ++m) {
            if (clip->frameMessages[m].frame > last) {
                last = clip->frameMessages[m].frame;
            }
        }
        duration = static_cast<float>(last) / static_cast<float>(fps > 0 ? fps : 30) + 1.0f;
    }
    game_.startCinematic(clip->index, duration, fps, clip->frameMessages, clip->resolvedMovement);
    // The player entity when the clip started; the movement KF2 offsets it.
    cinematicStartEntity_ = makeEntity(game_.player.position, -game_.player.yaw);
    cinematicEntity_ = cinematicStartEntity_;
    cameraPathName_.clear();
}

void ViewerApp::tickCinematicFrame(float dt) {
    game_.tickCinematic(dt, level_, actors_);
    // Resolve the active camera path's duration from its KF2 once (the
    // runtime only sees plain data; KF2 loading stays viewer-side). A path
    // whose KF2 is missing ends immediately instead of hanging the view.
    if (game_.cine.cameraActive && game_.cine.cameraDuration <= 0.0f) {
        const CameraPathDef* def = game_.cameraPaths.find(game_.cine.cameraPath);
        const Kf2File* kf =
            def != 0 && !def->resolvedAnimation.empty() ? database_.loadModel(def->resolvedAnimation) : 0;
        const float d = kf != 0 && !kf->animations.empty() ? kf2AnimationDuration(*kf) : 0.0f;
        // 0 would mean "unresolved" forever (tickCinematic skips the end
        // check), so clamp to a minimal fly-by instead.
        game_.cine.cameraDuration = d > 0.01f ? d : 0.05f;
    }
    // Root motion applies ONLY while a cinematic clip is playing. Outside
    // cutscenes the start entity is a stale identity and pinning the player
    // to it every frame froze noclip and normal walking (the player kept
    // being teleported to the world origin).
    if (game_.cine.active && !wasCineActive_) {
        // Rising edge: capture where the cutscene starts from, whichever
        // entry point started it (C key demo or scripted FSM messages).
        cinematicStartEntity_ = makeEntity(game_.player.position, -game_.player.yaw);
        cinematicEntity_ = cinematicStartEntity_;
    }
    if (game_.cine.active) {
        // Movement root motion ("*_mov.kf2"): the clip acts in place while
        // the movement file carries the character.
        cinematicEntity_ = cinematicStartEntity_;
        if (!game_.cine.movementFile.empty()) {
            const Kf2File* mov = database_.loadModel(game_.cine.movementFile);
            if (mov != 0 && !mov->animations.empty()) {
                std::vector<std::string> mn;
                std::vector<Mat4x3> ml;
                kf2BuildSkeletonWorlds(*mov, game_.cine.time, 0, &mn, &ml);
                if (!ml.empty()) {
                    cinematicEntity_ = combine(cinematicStartEntity_, ml[0]);
                }
            }
        }
        game_.player.position = cinematicEntity_.translation();
        const Vec3 ldbFwd = cinematicEntity_.rows[2];  // Z axis = forward (rotationY)
        game_.player.yaw = -std::atan2(ldbFwd.x, ldbFwd.z);  // LDB -> view yaw
    }
    wasCineActive_ = game_.cine.active;

    // Cutscene over: hand the camera back to the player.
    const bool on = game_.cine.active || game_.cine.cameraActive;
    if (wasCineOn_ && !on) {
        syncCameraFromPlayer();
        camera_.pitch = 0.0f;
    }
    wasCineOn_ = on;
}

void ViewerApp::applyCinematicCamera(Camera* cam) {
    if (!game_.cine.cameraActive) {
        return;
    }
    const CameraPathDef* def = game_.cameraPaths.find(game_.cine.cameraPath);
    if (def == 0) {
        return;
    }
    // Capture the base transform when the path activates (parented modes are
    // relative to the camera / player at start).
    if (cameraPathName_ != game_.cine.cameraPath) {
        cameraPathName_ = game_.cine.cameraPath;
        if (game_.cine.cameraMode == 2) {  // in place: relative to the camera
            // Build the base in LDB space (viewer X is the mirror of LDB X).
            const Vec3 fLdb(-camera_.forward().x, camera_.forward().y, camera_.forward().z);
            const Vec3 up(0.0f, 1.0f, 0.0f);
            Vec3 r = normalize(cross(fLdb, up));
            Vec3 u = cross(r, fLdb);
            Mat4x3 m;
            m.rows[0] = Vec3(-r.x, r.y, r.z);
            m.rows[1] = Vec3(-u.x, u.y, u.z);
            m.rows[2] = Vec3(-fLdb.x, fLdb.y, fLdb.z);
            m.rows[3] = Vec3(-camera_.position.x, camera_.position.y, camera_.position.z);
            cameraBase_ = m;
        } else if (game_.cine.cameraMode == 3 || game_.cine.cameraMode == 4) {
            cameraBase_ = cinematicEntity_;  // parented to the character
        } else {
            cameraBase_ = Mat4x3();  // absolute
        }
    }
    const Kf2File* kf =
        !def->resolvedAnimation.empty() ? database_.loadModel(def->resolvedAnimation) : 0;
    if (kf == 0 || kf->animations.empty()) {
        return;
    }
    // Walk the parent chain: the camera node is usually a child of a root
    // node that carries the path's world placement. Sampling only the
    // channel's LOCAL matrix put the camera at the origin (inside geometry,
    // hence a black screen).
    std::vector<std::string> names;
    std::vector<Mat4x3> worlds;
    kf2BuildSkeletonWorlds(*kf, game_.cine.cameraTime, 0, &names, &worlds);
    if (worlds.empty()) {
        return;
    }
    std::size_t channel = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (lowerCopy(names[i]).find("camera") != std::string::npos) {
            channel = i;
            break;
        }
    }
    // Parented paths follow the (possibly moving) player.
    if (game_.cine.cameraMode == 3 || game_.cine.cameraMode == 4) {
        cameraBase_ = cinematicEntity_;
    }
    const Mat4x3 world = combine(cameraBase_, worlds[channel]);
    // LDB -> viewer: points mirror X, directions mirror X (basis rows are
    // direction vectors in LDB space).
    cam->position = Vec3(-world.rows[3].x, world.rows[3].y, world.rows[3].z);
    const Vec3 fwd = normalize(Vec3(-world.rows[2].x, world.rows[2].y, world.rows[2].z));
    yawPitchFromDirection(fwd, &cam->yaw, &cam->pitch);
}

void ViewerApp::drawMenuHud() {
    renderer_.drawHudQuad(0, 0, width_, height_, 0.04f, 0.04f, 0.05f, 0.92f);
    renderer_.drawHudQuad(0, 0, width_, 56, 0.08f, 0.06f, 0.03f, 0.95f);
    renderer_.drawHudText(24, 24, "MAX PAYNE  /  MAX-FX", 0.95f, 0.82f, 0.35f);
    renderer_.drawHudText(24, 44, "PC message table  C_  DO_  T_  GM_  MPGNM_", 0.55f, 0.55f, 0.58f);
    {
        char stamp[96];
        std::snprintf(stamp, sizeof(stamp), "build %s  (%s)", MAXFX_BUILD_HASH, __DATE__);
        renderer_.drawHudText(24, height_ - 20, stamp, 0.45f, 0.45f, 0.5f);
    }
    if (game_.menu == kMenuRoot) {
        renderer_.drawHudQuad(32, 72, 280, 88, 0.10f, 0.09f, 0.07f, 0.85f);
        const char* items[4] = {"New Game", "Jump to Level", "Graphic Novel", "Quit"};
        for (int i = 0; i < 4; ++i) {
            const bool sel = game_.menuCursor == i;
            if (sel) {
                renderer_.drawHudQuad(36, 76 + i * 20, 270, 18, 0.45f, 0.28f, 0.08f, 0.85f);
            }
            renderer_.drawHudText(48, 80 + i * 20, items[i], sel ? 1.0f : 0.72f, sel ? 0.9f : 0.7f,
                                  sel ? 0.4f : 0.7f);
        }
        renderer_.drawHudText(24, height_ - 36, "Up/Down  Enter  Esc", 0.55f, 0.55f, 0.5f);
        return;
    }
    if (game_.menu == kMenuJumpLevel) {
        renderer_.drawHudText(24, 70, "Jump to Level  (levels.txt)", 0.85f, 0.75f, 0.4f);
        const int n = static_cast<int>(levelPaths_.size());
        int start = game_.menuCursor - 8;
        if (start < 0) {
            start = 0;
        }
        renderer_.drawHudQuad(32, 86, 520, 16 * 16 + 8, 0.08f, 0.07f, 0.05f, 0.8f);
        for (int i = start; i < n && i < start + 16; ++i) {
            char line[256];
            const char* name = fileName(levelPaths_[static_cast<std::size_t>(i)]).c_str();
            if (static_cast<std::size_t>(i) < levelInfos_.size() &&
                !levelInfos_[static_cast<std::size_t>(i)].levelName.empty()) {
                name = levelInfos_[static_cast<std::size_t>(i)].levelName.c_str();
            }
            std::snprintf(line, sizeof(line), "%s %s", game_.menuCursor == i ? ">" : " ", name);
            const bool sel = game_.menuCursor == i;
            if (sel) {
                renderer_.drawHudQuad(36, 90 + (i - start) * 16, 510, 16, 0.45f, 0.28f, 0.08f, 0.85f);
            }
            renderer_.drawHudText(40, 92 + (i - start) * 16, line, sel ? 1.0f : 0.7f, sel ? 0.9f : 0.7f,
                                  sel ? 0.4f : 0.7f);
        }
        renderer_.drawHudText(24, height_ - 36, "Enter load   Esc back", 0.55f, 0.55f, 0.5f);
        return;
    }
    if (game_.menu == kMenuComic) {
        renderer_.drawHudText(24, 70, "The Graphic Novel  —  chapters", 0.92f, 0.78f, 0.38f);
        const int n = static_cast<int>(game_.catalog.chapters.size());
        int start = game_.menuCursor - 8;
        if (start < 0) {
            start = 0;
        }
        renderer_.drawHudQuad(32, 86, 560, 16 * 16 + 8, 0.08f, 0.07f, 0.05f, 0.8f);
        for (int i = start; i < n && i < start + 16; ++i) {
            const GraphicNovelChapter& ch = game_.catalog.chapters[static_cast<std::size_t>(i)];
            char line[256];
            std::snprintf(line, sizeof(line), "%s %s   (%zu pages)", game_.menuCursor == i ? ">" : " ",
                          ch.title.c_str(), ch.pageIndices.size());
            const bool sel = game_.menuCursor == i;
            if (sel) {
                renderer_.drawHudQuad(36, 90 + (i - start) * 16, 550, 16, 0.45f, 0.28f, 0.08f, 0.85f);
            }
            renderer_.drawHudText(40, 92 + (i - start) * 16, line, sel ? 1.0f : 0.72f, sel ? 0.88f : 0.7f,
                                  sel ? 0.4f : 0.68f);
        }
        if (n <= 0) {
            renderer_.drawHudText(40, 96, "No graphicnovelpages.txt", 1.0f, 0.4f, 0.4f);
        }
        renderer_.drawHudText(24, height_ - 36, "Enter chapter   Esc back", 0.55f, 0.55f, 0.5f);
        return;
    }
    renderer_.drawHudText(24, 70, "The Graphic Novel  —  pages", 0.92f, 0.78f, 0.38f);
    const GraphicNovelChapter* ch = 0;
    if (game_.comicChapter >= 0 &&
        static_cast<std::size_t>(game_.comicChapter) < game_.catalog.chapters.size()) {
        ch = &game_.catalog.chapters[static_cast<std::size_t>(game_.comicChapter)];
        renderer_.drawHudText(24, 86, ch->title.c_str(), 0.75f, 0.65f, 0.4f);
    }
    const int n = ch ? static_cast<int>(ch->pageIndices.size()) : static_cast<int>(game_.catalog.pages.size());
    int start = game_.menuCursor - 8;
    if (start < 0) {
        start = 0;
    }
    renderer_.drawHudQuad(32, 102, 560, 16 * 16 + 8, 0.08f, 0.07f, 0.05f, 0.8f);
    for (int i = start; i < n && i < start + 16; ++i) {
        int pi = i;
        if (ch && static_cast<std::size_t>(i) < ch->pageIndices.size()) {
            pi = ch->pageIndices[static_cast<std::size_t>(i)];
        }
        const GraphicNovelPageDef& p = game_.catalog.pages[static_cast<std::size_t>(pi)];
        char line[256];
        std::snprintf(line, sizeof(line), "%s Panel %s%s%s", game_.menuCursor == i ? ">" : " ",
                      graphicNovelPageLabel(p.id).c_str(), p.newChapter ? "   chapter card" : "",
                      p.resolvedKf2.empty() ? "   (missing kf2)" : "");
        const bool sel = game_.menuCursor == i;
        if (sel) {
            renderer_.drawHudQuad(36, 106 + (i - start) * 16, 550, 16, 0.45f, 0.28f, 0.08f, 0.85f);
        }
        renderer_.drawHudText(40, 108 + (i - start) * 16, line, sel ? 1.0f : 0.72f, sel ? 0.88f : 0.7f,
                              sel ? 0.4f : 0.68f);
    }
    renderer_.drawHudText(24, height_ - 36, "Enter read   Esc chapters", 0.55f, 0.55f, 0.5f);
}

void ViewerApp::drawComicHud() {
    char line[256];
    const int n = static_cast<int>(game_.catalog.pages.size());
    renderer_.drawHudQuad(0, 0, width_, 40, 0.0f, 0.0f, 0.0f, 0.82f);
    renderer_.drawHudQuad(0, height_ - 44, width_, 44, 0.0f, 0.0f, 0.0f, 0.82f);
    renderer_.drawHudQuad(0, 40, width_, 2, 0.72f, 0.55f, 0.18f, 0.9f);
    renderer_.drawHudQuad(0, height_ - 46, width_, 2, 0.72f, 0.55f, 0.18f, 0.9f);
    renderer_.drawHudText(20, 14, "THE GRAPHIC NOVEL", 0.95f, 0.82f, 0.38f);
    if (n <= 0) {
        renderer_.drawHudText(24, height_ / 2, "No graphicnovelpages.txt", 1.0f, 0.4f, 0.4f);
        renderer_.drawHudText(20, height_ - 28, "Esc back", 0.6f, 0.55f, 0.45f);
        return;
    }
    if (game_.comicIndex < 0) {
        game_.comicIndex = 0;
    }
    if (game_.comicIndex >= n) {
        game_.comicIndex = n - 1;
    }
    const GraphicNovelPageDef& p = game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)];
    const std::string chapter = graphicNovelChapterTitle(graphicNovelChapterKey(p.id));
    renderer_.drawHudText(width_ - 8 * static_cast<int>(chapter.size()) - 20, 14, chapter.c_str(), 0.85f,
                          0.72f, 0.38f);
    if (p.resolvedKf2.empty()) {
        const int pw = width_ * 3 / 5;
        const int ph = height_ * 3 / 5;
        const int px = (width_ - pw) / 2;
        const int py = (height_ - ph) / 2;
        renderer_.drawHudQuad(px - 6, py - 6, pw + 12, ph + 12, 0.72f, 0.55f, 0.18f, 0.95f);
        renderer_.drawHudQuad(px, py, pw, ph, 0.12f, 0.08f, 0.06f, 0.95f);
        const std::string label = graphicNovelPageLabel(p.id);
        renderer_.drawHudText(px + 16, py + 20, "PANEL", 0.55f, 0.48f, 0.35f);
        renderer_.drawHudText(px + 16, py + 40, label.c_str(), 0.95f, 0.85f, 0.45f);
        renderer_.drawHudText(px + 16, py + 64, "KF2 textures not extracted", 0.65f, 0.55f, 0.45f);
    }
    std::snprintf(line, sizeof(line), "Panel %s", graphicNovelPageLabel(p.id).c_str());
    renderer_.drawHudText(20, height_ - 28, line, 0.8f, 0.72f, 0.45f);
    std::snprintf(line, sizeof(line), "%d / %d", game_.comicIndex + 1, n);
    const int mid = width_ / 2 - 4 * static_cast<int>(std::strlen(line));
    renderer_.drawHudText(mid, height_ - 28, line, 0.92f, 0.82f, 0.4f);
    renderer_.drawHudText(20, height_ - 14, "< PREV", game_.comicIndex > 0 ? 0.9f : 0.35f, 0.75f, 0.4f);
    renderer_.drawHudText(width_ - 20 - 8 * 6, height_ - 14, "NEXT >",
                          game_.comicIndex + 1 < n ? 0.9f : 0.35f, 0.75f, 0.4f);
    renderer_.drawHudText(width_ / 2 - 40, height_ - 14, "Esc close  click next", 0.5f, 0.48f, 0.4f);
}

void ViewerApp::drawHud(float dt) {
    char line[512];
    if (game_.mode == kModeMenu) {
        drawMenuHud();
        renderer_.presentHud();
        return;
    }
    if (game_.mode == kModeGraphicNovel) {
        drawComicHud();
        renderer_.presentHud();
        return;
    }
    const char* shade = "lit";
    switch (renderer_.shading()) {
        case kShadeDiffuse:
            shade = "diffuse";
            break;
        case kShadeLightmap:
            shade = "lightmap";
            break;
        case kShadeVertex:
            shade = "vertex";
            break;
        default:
            break;
    }

    if (levelPaths_.size() > 1) {
        std::snprintf(line, sizeof(line), "MAXFX  %s   %d/%zu   Left/Right change level",
                      fileName_.c_str(), levelIndex_ + 1, levelPaths_.size());
    } else {
        std::snprintf(line, sizeof(line), "MAXFX  %s", fileName_.c_str());
    }
    renderer_.drawHudText(12, 12, line, 0.95f, 0.85f, 0.45f);

    std::snprintf(line, sizeof(line),
                  "rooms %zu  meshes %zu/%zu  lights %zu  trig %zu  fsm %zu  tris %u  batches %u",
                  level_.rooms.size(), level_.staticMeshes.size(), level_.dynamicMeshes.size(),
                  level_.pointLights.size(), level_.triggers.size(), level_.fsms.size(),
                  renderer_.triangleCount(), renderer_.batchCount());
    renderer_.drawHudText(12, 28, line, 0.80f, 0.80f, 0.82f);

    const int iso = renderer_.isolatedRoom();
    const char* roomName = "ALL";
    if (iso >= 0 && static_cast<std::size_t>(iso) < level_.rooms.size()) {
        roomName = level_.rooms[static_cast<std::size_t>(iso)].name.c_str();
    }
    const char* spawnName = "";
    if (spawnIndex_ >= 0 && static_cast<std::size_t>(spawnIndex_) < renderer_.spawns().size()) {
        spawnName = renderer_.spawns()[static_cast<std::size_t>(spawnIndex_)].name.c_str();
    }
    std::snprintf(line, sizeof(line), "fps %.0f  shade %s  room %s  spawn %s",
                  dt > 1.0e-4f ? 1.0f / dt : 0.0f, shade, roomName, spawnName);
    renderer_.drawHudText(12, 44, line, 0.70f, 0.75f, 0.80f);

    const char* act = actors_.empty() ? "-" : characterActivityName(actors_[0].activity);
    std::snprintf(line, sizeof(line),
                  "service %s  entities %u  kf2 tris %u  placeholders %u  items %zu  chars %zu  ai %s",
                  renderer_.showService() ? "on" : "off", renderer_.entityMeshCount(),
                  renderer_.entityTriangleCount(), renderer_.entityPlaceholderCount(),
                  level_.items.size(), level_.characters.size(), act);
    renderer_.drawHudText(12, 60, line, 0.65f, 0.70f, 0.75f);

    std::snprintf(line, sizeof(line), "%s%s", audio_.statusLine(), audio_.muted() ? "  MUTE" : "");
    renderer_.drawHudText(12, 76, line, 0.65f, 0.72f, 0.70f);

    if (!statusMessage_.empty()) {
        renderer_.drawHudText(12, 92, statusMessage_.c_str(), 1.0f, 0.35f, 0.35f);
    }

    std::snprintf(line, sizeof(line), "HP %.0f/%.0f  %s  %s%s",
                  game_.player.health, game_.player.maxHealth, game_.player.weaponName.c_str(),
                  game_.player.grounded ? "ground" : "air", game_.player.noclip ? "  NOCLIP" : "");
    renderer_.drawHudText(12, 108, line, 0.95f, 0.45f, 0.35f);
    drawWeaponHud();
    if (!game_.prompt.empty()) {
        renderer_.drawHudText(width_ / 2 - 80, height_ / 2 + 36, game_.prompt.c_str(), 0.95f, 0.9f, 0.4f);
    }
    if (!game_.lastEvent.empty()) {
        renderer_.drawHudText(12, 124, game_.lastEvent.c_str(), 0.7f, 0.75f, 0.55f);
    }
    if (game_.player.crosshair && game_.cine.hudVisible) {
        renderer_.drawHudText(width_ / 2 - 4, height_ / 2 - 4, "+", 0.95f, 0.9f, 0.4f);
    }

    if (showItemDebug_ && !level_.items.empty()) {
        // Item-angle diagnostics: which KF2 each item resolved to (or the
        // placeholder box) plus the LDB objectToRoom rotation rows.
        int y = height_ - 52 - static_cast<int>(std::min<std::size_t>(level_.items.size(), 14)) * 14 - 14;
        renderer_.drawHudText(12, y, "items: name -> model | room | rot rows (objectToRoom)", 0.6f,
                              0.85f, 0.9f);
        y += 14;
        for (std::size_t i = 0; i < level_.items.size() && i < 14; ++i) {
            const LevelItem& it = level_.items[static_cast<std::size_t>(i)];
            const ItemDef* def = database_.findItem(it.itemName);
            std::string model = def != 0 && !def->lods.empty() && !def->lods[0].resolvedExport.empty()
                                    ? fileName(def->lods[0].resolvedExport)
                                    : "PLACEHOLDER";
            const Mat4x3& m = it.properties.objectToRoom;
            std::snprintf(line, sizeof(line),
                          "%-22s %-24s r%u | %4.2f %4.2f %4.2f / %4.2f %4.2f %4.2f / %4.2f %4.2f %4.2f",
                          it.itemName.c_str(), model.c_str(), it.properties.roomId, m.rows[0].x,
                          m.rows[0].y, m.rows[0].z, m.rows[1].x, m.rows[1].y, m.rows[1].z,
                          m.rows[2].x, m.rows[2].y, m.rows[2].z);
            renderer_.drawHudText(12, y, line, 0.75f, 0.8f, 0.85f);
            y += 14;
        }
        if (level_.items.size() > 14) {
            std::snprintf(line, sizeof(line), "... %zu more", level_.items.size() - 14);
            renderer_.drawHudText(12, y, line, 0.6f, 0.65f, 0.7f);
        }
    }

    if (showHelp_) {
        const char* help =
            "WASD walk  Space jump  E use  LMB shoot  Shift run  Esc menu\n"
            "1-0 weapons  wheel cycle  R reload  C cinematic  F9 respawn\n"
            "F8 item debug  N noclip (Ctrl down)  F1 help  F2 wire  F3 shading  F4 helpers  F5 dynamic  F6 service  F7 mute";
        renderer_.drawHudText(12, height_ - 52, help, 0.72f, 0.72f, 0.68f);
    }

    // Cutscene presentation: GM_EnableWideScreen letterbox and the
    // MPHM_FadeToColor overlay (drawn last so they cover the HUD).
    if (game_.cine.widescreen) {
        const int bar = static_cast<int>(static_cast<float>(height_) * 0.12f);
        if (bar > 0) {
            renderer_.drawHudQuad(0, 0, width_, bar, 0.0f, 0.0f, 0.0f, 1.0f);
            renderer_.drawHudQuad(0, height_ - bar, width_, bar, 0.0f, 0.0f, 0.0f, 1.0f);
        }
    }
    {
        float rgba[4];
        game_.cine.fadeColor(rgba);
        if (rgba[3] > 0.003f) {
            renderer_.drawHudQuad(0, 0, width_, height_, rgba[0], rgba[1], rgba[2], rgba[3]);
        }
    }
    renderer_.presentHud();
}

int ViewerApp::run(const char* pathOrNull) {
    char error[1024];
    collectLevels(pathOrNull);

    const std::string dbRoot = DatabaseReader::locateRoot(pathOrNull ? pathOrNull : "");
    if (!dbRoot.empty()) {
        try {
            database_ = DatabaseReader::load(dbRoot);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "database: %s\n", ex.what());
        }
        game_.loadCatalog(dbRoot);
        game_.loadCameraPaths(dbRoot);
        const std::string dataRoot = parentDir(dbRoot);
        hud_ = loadHudDef(dataRoot);
        // decals.txt -> engine DecalSystem materials + viewer texture paths.
        game_.decalMaterials.clear();
        decalFiles_.clear();
        for (std::size_t i = 0; i < database_.decals.size(); ++i) {
            const DecalMaterialDef& def = database_.decals[i];
            DecalMaterialInfo info;
            info.name = def.name;
            info.minRadius = def.minRadius;
            info.maxRadius = def.maxRadius;
            game_.decalMaterials.push_back(info);
            static const char* const kRoots[] = {"database/decals", "database", "hud", ""};
            decalFiles_[def.name] = std::make_pair(
                resolveGameAsset(dataRoot, def.filename, kRoots, 4),
                resolveGameAsset(dataRoot, def.alphaFilename, kRoots, 4));
        }
        game_.effects.decals.setMaterials(game_.decalMaterials);
    }

    if (!createWindow(error, sizeof(error))) {
        std::fprintf(stderr, "%s\n", error);
        return 3;
    }
    if (!renderer_.init(error, sizeof(error))) {
        std::fprintf(stderr, "renderer init: %s\n", error);
        return 4;
    }
    audio_.init();
    renderer_.resize(width_, height_);
    game_.mode = kModeMenu;
    mouseCaptured_ = false;
    SDL_SetWindowRelativeMouseMode(window_, false);

    std::uint64_t last = SDL_GetTicksNS();
    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                handleKeyDown(static_cast<int>(ev.key.scancode), static_cast<int>(ev.key.key));
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (game_.mode == kModePlaying && mouseCaptured_ &&
                    !game_.cine.active && !game_.cine.cameraActive &&
                    ev.button.button == SDL_BUTTON_LEFT) {
                    const Vec3 eye(game_.player.position.x,
                                   game_.player.position.y + game_.player.eyeHeight,
                                   game_.player.position.z);
                    if (game_.tryShoot(level_, collision_, actors_, eye, playerLookLdb())) {
                        game_.notePlayerShot();  // PerceivingGroupOne noise
                    }
                } else if (!mouseCaptured_ && game_.mode == kModePlaying) {
                    mouseCaptured_ = true;
                    SDL_SetWindowRelativeMouseMode(window_, true);
                } else if (game_.mode == kModeMenu || game_.mode == kModeGraphicNovel) {
                    handleMouseButton(ev.button.x, ev.button.y, static_cast<int>(ev.button.button));
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured_ &&
                       game_.mode == kModePlaying) {
                camera_.addLook(static_cast<float>(ev.motion.xrel), static_cast<float>(ev.motion.yrel));
                game_.player.yaw = camera_.yaw;
                game_.player.pitch = camera_.pitch;
            } else if (ev.type == SDL_EVENT_MOUSE_WHEEL && game_.mode == kModePlaying &&
                       mouseCaptured_) {
                if (game_.cycleWeapon(ev.wheel.y > 0 ? 1 : -1)) {
                    weaponListTimer_ = 1.5f;
                }
            } else if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
                width_ = ev.window.data1;
                height_ = ev.window.data2;
                renderer_.resize(width_, height_);
            }
        }

        const std::uint64_t now = SDL_GetTicksNS();
        float dt = static_cast<float>(static_cast<double>(now - last) / 1000000000.0);
        last = now;
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        if (game_.mode == kModePlaying || game_.mode == kModeGraphicNovel) {
            update(dt);
        } else {
            audio_.setEnvPaused(true);
            audio_.pump();
        }
        renderer_.setSkipWorld(game_.mode == kModeGraphicNovel || game_.mode == kModeMenu);
        renderer_.setFovY(game_.mode == kModeGraphicNovel ? 40.0f : 70.0f);
        Camera renderCam = camera_;
        if (game_.mode == kModePlaying) {
            applyCinematicCamera(&renderCam);
        }
        if (game_.mode == kModeGraphicNovel && game_.comicIndex >= 0 &&
            static_cast<std::size_t>(game_.comicIndex) < game_.catalog.pages.size() &&
            !game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)].resolvedKf2.empty()) {
            const GraphicNovelPageDef& page =
                game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)];
            const Kf2File* kf = database_.loadModel(page.resolvedKf2);
            if (kf != 0) {
                renderer_.beginAnimated();
                Mat4x3 identity;
                renderer_.appendOverlayKf2(*kf, identity);
                const float aspect = height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_)
                                                 : 1.777f;
                frameComicCamera(*kf, aspect, &renderCam);
            }
        }
        renderer_.render(renderCam.viewMatrix(), renderCam.position);
        if (game_.mode == kModePlaying) {
            renderer_.renderEffects(renderCam.viewMatrix(), renderCam.position,
                                    game_.effects.decals.list(), game_.effects.effects(),
                                    decalFiles_.empty() ? 0 : &decalFiles_);
        }
        drawHud(dt);
        SDL_GL_SwapWindow(window_);
    }
    return 0;
}
}  // namespace maxfx
