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

}  // namespace

ViewerApp::ViewerApp()
    : window_(0),
      glContext_(0),
      running_(true),
      levelLoaded_(false),
      mouseCaptured_(false),
      showHelp_(true),
      spawnIndex_(-1),
      width_(1280),
      height_(720),
      levelIndex_(-1) {}

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

    window_ = SDL_CreateWindow("MAXFX Level Viewer", width_, height_,
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
    } else {
        audio_.playLevel(database_, std::string());
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
    const SkinDef* playerSkin = database_.findSkin(
        (index >= 0 && static_cast<std::size_t>(index) < levelInfos_.size() &&
         !levelInfos_[static_cast<std::size_t>(index)].playerSkinName.empty())
            ? levelInfos_[static_cast<std::size_t>(index)].playerSkinName
            : std::string("max_payne"));
    game_.applyPlayerOnInit(playerSkin != 0 ? &playerSkin->character : 0);
    game_.mode = kModePlaying;
    mouseCaptured_ = true;
    if (window_) {
        SDL_SetWindowRelativeMouseMode(window_, true);
    }
    syncCameraFromPlayer();

    char title[256];
    std::snprintf(title, sizeof(title), "MAXFX Level Viewer  -  %s  [%d/%zu]", fileName_.c_str(),
                  levelIndex_ + 1, levelPaths_.size());
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

void ViewerApp::handleKeyDown(int scancode, int key) {
    if (game_.mode == kModeMenu) {
        if (key == SDLK_ESCAPE) {
            if (game_.menu != kMenuRoot) {
                game_.menu = kMenuRoot;
                game_.menuCursor = 0;
            } else {
                running_ = false;
            }
            return;
        }
        if (scancode == SDL_SCANCODE_UP || scancode == SDL_SCANCODE_W) {
            game_.menuMove(-1, static_cast<int>(levelPaths_.size()),
                           static_cast<int>(game_.catalog.pages.size()));
            return;
        }
        if (scancode == SDL_SCANCODE_DOWN || scancode == SDL_SCANCODE_S) {
            game_.menuMove(1, static_cast<int>(levelPaths_.size()),
                           static_cast<int>(game_.catalog.pages.size()));
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
            }
            return;
        }
        return;
    }
    if (game_.mode == kModeGraphicNovel) {
        if (key == SDLK_ESCAPE) {
            game_.mode = levelLoaded_ ? kModePlaying : kModeMenu;
            mouseCaptured_ = levelLoaded_;
            SDL_SetWindowRelativeMouseMode(window_, mouseCaptured_);
            return;
        }
        if (scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_A) {
            if (game_.comicIndex > 0) {
                --game_.comicIndex;
            }
            return;
        }
        if (scancode == SDL_SCANCODE_RIGHT || scancode == SDL_SCANCODE_D || key == SDLK_RETURN) {
            if (game_.comicIndex + 1 < static_cast<int>(game_.catalog.pages.size())) {
                ++game_.comicIndex;
            }
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
        case SDLK_F10:
            game_.player.noclip = !game_.player.noclip;
            game_.pushLog(game_.player.noclip ? "noclip on" : "noclip off");
            break;
        case SDLK_H:
            renderer_.setShowHud(!renderer_.showHud());
            break;
        case SDLK_R:
            placeAtSpawn(-1);
            game_.player.position = ldbFromView(camera_.position, game_.player.eyeHeight);
            game_.player.yaw = camera_.yaw;
            game_.player.velocity = Vec3();
            break;
        default:
            break;
    }
}

void ViewerApp::update(float dt) {
    if (game_.mode != kModePlaying || !levelLoaded_) {
        audio_.pump();
        return;
    }
    const bool* keys = SDL_GetKeyboardState(0);
    const bool jump = keys[SDL_SCANCODE_SPACE] != 0;
    const bool use = keys[SDL_SCANCODE_E] != 0;
    const bool sprint = keys[SDL_SCANCODE_LSHIFT] != 0;
    game_.player.yaw = camera_.yaw;
    game_.player.pitch = camera_.pitch;
    game_.tickPlayer(dt, keys[SDL_SCANCODE_W] != 0, keys[SDL_SCANCODE_S] != 0,
                     keys[SDL_SCANCODE_A] != 0, keys[SDL_SCANCODE_D] != 0, jump, sprint, collision_);
    syncCameraFromPlayer();
    const Vec3 look = playerLookLdb();
    const Vec3 eye = Vec3(game_.player.position.x, game_.player.position.y + game_.player.eyeHeight,
                          game_.player.position.z);
    static bool useWasDown = false;
    const bool usePress = use && !useWasDown;
    useWasDown = use;
    game_.tickDoors(dt, level_);
    game_.tickTriggers(dt, level_, usePress, look, actors_);
    updateActors(dt);
    audio_.pump();
}

void ViewerApp::spawnActors() {
    collision_.clear();
    collision_.addLevelGeometry(level_);
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
                    def != 0 ? &def->character : 0, ch.characterName);
        actors_.push_back(actor);
    }
}

void ViewerApp::updateActors(float dt) {
    const Vec3 playerLdb(-camera_.position.x, camera_.position.y - 1.6f, camera_.position.z);
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        actors_[i].update(dt, playerLdb, collision_, &actors_);
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
        renderer_.appendAnimatedCharacter(*mesh, skin, bindAnim, playAnim, actor.animTime,
                                          actor.entityTransform(), actor.roomId);
    }
}

void ViewerApp::drawMenuHud() {
    renderer_.drawHudText(24, 24, "MAX PAYNE  /  MAX-FX", 0.95f, 0.82f, 0.35f);
    renderer_.drawHudText(24, 44, "PC message table  C_  DO_  T_  GM_  MPGNM_", 0.55f, 0.55f, 0.58f);
    if (game_.menu == kMenuRoot) {
        const char* items[4] = {"New Game", "Jump to Level", "Graphic Novel", "Quit"};
        for (int i = 0; i < 4; ++i) {
            const bool sel = game_.menuCursor == i;
            renderer_.drawHudText(40, 80 + i * 18, items[i], sel ? 1.0f : 0.7f, sel ? 0.9f : 0.7f,
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
        for (int i = start; i < n && i < start + 16; ++i) {
            char line[256];
            const char* name = fileName(levelPaths_[static_cast<std::size_t>(i)]).c_str();
            if (static_cast<std::size_t>(i) < levelInfos_.size() &&
                !levelInfos_[static_cast<std::size_t>(i)].levelName.empty()) {
                name = levelInfos_[static_cast<std::size_t>(i)].levelName.c_str();
            }
            std::snprintf(line, sizeof(line), "%s %s", game_.menuCursor == i ? ">" : " ", name);
            const bool sel = game_.menuCursor == i;
            renderer_.drawHudText(40, 92 + (i - start) * 16, line, sel ? 1.0f : 0.7f, sel ? 0.9f : 0.7f,
                                  sel ? 0.4f : 0.7f);
        }
        return;
    }
    renderer_.drawHudText(24, 70, "Graphic Novel pages", 0.85f, 0.75f, 0.4f);
    const int n = static_cast<int>(game_.catalog.pages.size());
    int start = game_.menuCursor - 8;
    if (start < 0) {
        start = 0;
    }
    for (int i = start; i < n && i < start + 16; ++i) {
        char line[256];
        std::snprintf(line, sizeof(line), "%s %s%s", game_.menuCursor == i ? ">" : " ",
                      game_.catalog.pages[static_cast<std::size_t>(i)].id.c_str(),
                      game_.catalog.pages[static_cast<std::size_t>(i)].resolvedKf2.empty() ? "  (kf2 missing)"
                                                                                          : "");
        const bool sel = game_.menuCursor == i;
        renderer_.drawHudText(40, 92 + (i - start) * 16, line, sel ? 1.0f : 0.7f, sel ? 0.9f : 0.7f,
                              sel ? 0.4f : 0.7f);
    }
}

void ViewerApp::drawComicHud() {
    char line[256];
    const int n = static_cast<int>(game_.catalog.pages.size());
    if (n <= 0) {
        renderer_.drawHudText(24, 24, "No graphicnovelpages.txt", 1, 0.4f, 0.4f);
        return;
    }
    if (game_.comicIndex < 0) {
        game_.comicIndex = 0;
    }
    if (game_.comicIndex >= n) {
        game_.comicIndex = n - 1;
    }
    const GraphicNovelPageDef& p = game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)];
    std::snprintf(line, sizeof(line), "Graphic Novel  %d/%d   %s", game_.comicIndex + 1, n, p.id.c_str());
    renderer_.drawHudText(24, 24, line, 0.95f, 0.85f, 0.4f);
    renderer_.drawHudText(24, 44, p.resolvedKf2.empty() ? "KF2 not extracted" : p.resolvedKf2.c_str(),
                          0.7f, 0.7f, 0.7f);
    renderer_.drawHudText(24, height_ - 36, "Left/Right page   Esc back", 0.55f, 0.55f, 0.5f);
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

    std::snprintf(line, sizeof(line), "HP %.0f/%.0f  %s  clip %d/%d  ammo %d  %s%s",
                  game_.player.health, game_.player.maxHealth, game_.player.weaponName.c_str(),
                  game_.player.clip, game_.player.clipSize, game_.player.ammo,
                  game_.player.grounded ? "ground" : "air", game_.player.noclip ? "  NOCLIP" : "");
    renderer_.drawHudText(12, 108, line, 0.95f, 0.45f, 0.35f);
    if (!game_.prompt.empty()) {
        renderer_.drawHudText(width_ / 2 - 80, height_ / 2 + 36, game_.prompt.c_str(), 0.95f, 0.9f, 0.4f);
    }
    if (!game_.lastEvent.empty()) {
        renderer_.drawHudText(12, 124, game_.lastEvent.c_str(), 0.7f, 0.75f, 0.55f);
    }
    if (game_.player.crosshair) {
        renderer_.drawHudText(width_ / 2 - 4, height_ / 2 - 4, "+", 0.95f, 0.9f, 0.4f);
    }

    if (showHelp_) {
        const char* help =
            "WASD walk  Space jump  E use  LMB shoot  Shift run  Esc menu\n"
            "F10 noclip  F1 help  F2 wire  F3 shading  F4 helpers  F5 dynamic  F6 service  F7 mute";
        renderer_.drawHudText(12, height_ - 52, help, 0.72f, 0.72f, 0.68f);
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
                    ev.button.button == SDL_BUTTON_LEFT) {
                    const Vec3 eye(game_.player.position.x,
                                   game_.player.position.y + game_.player.eyeHeight,
                                   game_.player.position.z);
                    game_.tryShoot(level_, collision_, actors_, eye, playerLookLdb());
                } else if (!mouseCaptured_ && game_.mode == kModePlaying) {
                    mouseCaptured_ = true;
                    SDL_SetWindowRelativeMouseMode(window_, true);
                }
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured_ &&
                       game_.mode == kModePlaying) {
                camera_.addLook(static_cast<float>(ev.motion.xrel), static_cast<float>(ev.motion.yrel));
                game_.player.yaw = camera_.yaw;
                game_.player.pitch = camera_.pitch;
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
        if (game_.mode == kModePlaying) {
            update(dt);
        }
        if (game_.mode == kModeGraphicNovel && game_.comicIndex >= 0 &&
            static_cast<std::size_t>(game_.comicIndex) < game_.catalog.pages.size() &&
            !game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)].resolvedKf2.empty()) {
            const GraphicNovelPageDef& page =
                game_.catalog.pages[static_cast<std::size_t>(game_.comicIndex)];
            const Kf2File* kf = database_.loadModel(page.resolvedKf2);
            if (kf != 0) {
                renderer_.beginAnimated();
                const Mat4x3 billboard = makeEntity(
                    ldbFromView(camera_.position, 0.0f) + playerLookLdb() * 2.0f, game_.player.yaw);
                renderer_.appendOverlayKf2(*kf, billboard);
            }
        }
        renderer_.render(camera_.viewMatrix(), camera_.position);
        drawHud(dt);
        SDL_GL_SwapWindow(window_);
    }
    return 0;
}
}  // namespace maxfx
