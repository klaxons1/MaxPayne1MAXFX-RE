#include "viewer/App.h"

#include "maxfx/core/Fs.h"
#include "maxfx/ldb/LdbReader.h"
#include "viewer/GL.h"

#include <SDL3/SDL.h>

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
      mouseCaptured_(true),
      showHelp_(true),
      spawnIndex_(-1),
      width_(1280),
      height_(720),
      levelIndex_(-1) {}

ViewerApp::~ViewerApp() { destroyWindow(); }

void ViewerApp::destroyWindow() {
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::snprintf(error, errorSize, "SDL_Init: %s", SDL_GetError());
        return false;
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
    SDL_SetWindowRelativeMouseMode(window_, true);
    return true;
}

void ViewerApp::collectLevels(const char* pathOrNull) {
    levelPaths_.clear();
    levelsDir_.clear();
    levelIndex_ = -1;

    std::string explicitFile;
    std::vector<std::string> searchDirs;

    if (pathOrNull && pathOrNull[0] != 0) {
        const std::string given = pathOrNull;
        if (isDirectory(given)) {
            searchDirs.push_back(given);
            searchDirs.push_back(joinPath(given, "database"));
            searchDirs.push_back(joinPath(joinPath(given, "database"), "levels"));
            searchDirs.push_back(joinPath(given, "levels"));
        } else if (isFile(given)) {
            explicitFile = given;
            searchDirs.push_back(parentDir(given));
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
    }

    for (std::size_t i = 0; i < searchDirs.size(); ++i) {
        const std::vector<std::string> found = listFilesWithExtension(searchDirs[i], ".ldb");
        if (found.empty()) {
            continue;
        }
        levelsDir_ = searchDirs[i];
        levelPaths_ = found;
        break;
    }

    if (!explicitFile.empty()) {
        const int existing = findPathIndex(levelPaths_, explicitFile);
        if (existing >= 0) {
            levelIndex_ = existing;
        } else {
            levelPaths_.insert(levelPaths_.begin(), explicitFile);
            levelIndex_ = 0;
            if (levelsDir_.empty()) {
                levelsDir_ = parentDir(explicitFile);
            }
        }
    } else if (!levelPaths_.empty()) {
        int prefer = findPathIndex(levelPaths_, "Part1_Level1.ldb");
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

    char error[1024];
    if (!renderer_.loadLevel(loaded, error, sizeof(error))) {
        statusMessage_ = std::string("gpu upload failed: ") + error;
        std::fprintf(stderr, "%s\n", statusMessage_.c_str());
        return false;
    }

    level_ = loaded;
    levelIndex_ = index;
    statusMessage_.clear();
    renderer_.resize(width_, height_);
    placeAtSpawn(-1);

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
        for (std::size_t i = 0; i < spawns.size(); ++i) {
            if (spawns[i].name.find("startroom") != std::string::npos &&
                spawns[i].name.find("Jumppoint") != std::string::npos) {
                index = static_cast<int>(i);
                break;
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

void ViewerApp::handleKeyDown(int scancode, int key) {
    if (scancode == SDL_SCANCODE_LEFT) {
        cycleLevel(-1);
        return;
    }
    if (scancode == SDL_SCANCODE_RIGHT) {
        cycleLevel(1);
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
            if (mouseCaptured_) {
                mouseCaptured_ = false;
                SDL_SetWindowRelativeMouseMode(window_, false);
            } else {
                running_ = false;
            }
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
        case SDLK_H:
            renderer_.setShowHud(!renderer_.showHud());
            break;
        case SDLK_PAGEUP:
            if (!renderer_.spawns().empty()) {
                int next = spawnIndex_ - 1;
                if (next < 0) {
                    next = static_cast<int>(renderer_.spawns().size()) - 1;
                }
                placeAtSpawn(next);
            }
            break;
        case SDLK_PAGEDOWN:
            if (!renderer_.spawns().empty()) {
                int next = spawnIndex_ + 1;
                if (next >= static_cast<int>(renderer_.spawns().size())) {
                    next = 0;
                }
                placeAtSpawn(next);
            }
            break;
        case SDLK_R:
            placeAtSpawn(-1);
            break;
        default:
            break;
    }
}

void ViewerApp::update(float dt) {
    const bool* keys = SDL_GetKeyboardState(0);
    float f = 0, r = 0, u = 0;
    if (keys[SDL_SCANCODE_W]) {
        f += 1;
    }
    if (keys[SDL_SCANCODE_S]) {
        f -= 1;
    }
    if (keys[SDL_SCANCODE_D]) {
        r += 1;
    }
    if (keys[SDL_SCANCODE_A]) {
        r -= 1;
    }
    if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE]) {
        u += 1;
    }
    if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_LCTRL]) {
        u -= 1;
    }
    const bool sprint = keys[SDL_SCANCODE_LSHIFT] != 0;
    camera_.fly(f, r, u, dt, sprint);
}

void ViewerApp::drawHud(float dt) {
    char line[512];
    const char* shade = "lit";
    switch (renderer_.shading()) {
        case kShadeDiffuse:
            shade = "diffuse";
            break;
        case kShadeLightmap:
            shade = "lightmap";
            break;
        case kShadeVertex:
            shade = "flat";
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

    if (!statusMessage_.empty()) {
        renderer_.drawHudText(12, 60, statusMessage_.c_str(), 1.0f, 0.35f, 0.35f);
    }

    if (showHelp_) {
        const char* help =
            "WASD fly   Q/E up/down   Shift sprint   mouse look\n"
            "Left/Right levels   [ ] isolate room   PgUp/PgDn jumppoints\n"
            "F1 help  F2 wire  F3 shading  F4 helpers  F5 dynamic   Esc grab/quit";
        renderer_.drawHudText(12, height_ - 52, help, 0.72f, 0.72f, 0.68f);
    }
    renderer_.presentHud();
}

int ViewerApp::run(const char* pathOrNull) {
    char error[1024];
    collectLevels(pathOrNull);
    if (levelPaths_.empty()) {
        std::fprintf(stderr,
                     "no .ldb files found.\n"
                     "Put ldb-viewer next to the game data folder:\n"
                     "  ldb-viewer.exe\n"
                     "  data\\database\\levels\\*.ldb\n"
                     "or pass a path: ldb-viewer path\\to\\level.ldb\n");
        return 1;
    }

    if (!createWindow(error, sizeof(error))) {
        std::fprintf(stderr, "%s\n", error);
        return 3;
    }
    if (!renderer_.init(error, sizeof(error))) {
        std::fprintf(stderr, "renderer init: %s\n", error);
        return 4;
    }
    renderer_.resize(width_, height_);

    int start = levelIndex_ >= 0 ? levelIndex_ : 0;
    if (!loadLevelIndex(start, true)) {
        bool any = false;
        for (std::size_t i = 0; i < levelPaths_.size(); ++i) {
            if (static_cast<int>(i) == start) {
                continue;
            }
            if (loadLevelIndex(static_cast<int>(i), true)) {
                any = true;
                break;
            }
        }
        if (!any) {
            std::fprintf(stderr, "could not load any LDB in the playlist\n");
            return 5;
        }
    }

    std::uint64_t last = SDL_GetTicksNS();
    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running_ = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                handleKeyDown(static_cast<int>(ev.key.scancode), static_cast<int>(ev.key.key));
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured_) {
                mouseCaptured_ = true;
                SDL_SetWindowRelativeMouseMode(window_, true);
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION && mouseCaptured_) {
                camera_.addLook(static_cast<float>(ev.motion.xrel), static_cast<float>(ev.motion.yrel));
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
        if (mouseCaptured_) {
            update(dt);
        }
        renderer_.render(camera_.viewMatrix(), camera_.position);
        drawHud(dt);
        SDL_GL_SwapWindow(window_);
    }
    return 0;
}

}  // namespace maxfx
