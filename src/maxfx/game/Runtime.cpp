#include "maxfx/game/Runtime.h"

#include "maxfx/core/Fs.h"
#include "maxfx/script/Script.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <cstdio>

namespace maxfx {
namespace {

std::string lowerArg(const GameMessage& m, std::size_t i) {
    if (i >= m.args.size()) {
        return std::string();
    }
    return lowerCopy(m.args[i]);
}

float floatArg(const GameMessage& m, std::size_t i, float fallback) {
    if (i >= m.args.size()) {
        return fallback;
    }
    try {
        return parseScriptFloat(m.args[i], "", 0, "");
    } catch (...) {
        return fallback;
    }
}

int intArg(const GameMessage& m, std::size_t i, int fallback) {
    if (i >= m.args.size()) {
        return fallback;
    }
    std::istringstream ss(m.args[i]);
    int v = fallback;
    ss >> v;
    return v;
}

// MPHM_FadeToColor colours: 0xRRGGBBAA written as one integer.
unsigned int colorArg(const GameMessage& m, std::size_t i, unsigned int fallback) {
    if (i >= m.args.size()) {
        return fallback;
    }
    return static_cast<unsigned int>(std::strtoul(m.args[i].c_str(), 0, 0));
}

void unpackColor(unsigned int c, float* rgba) {
    rgba[0] = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
    rgba[1] = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
    rgba[2] = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
    rgba[3] = static_cast<float>(c & 0xFF) / 255.0f;
}

bool boolArg(const GameMessage& m, std::size_t i, bool fallback) {
    if (i >= m.args.size()) {
        return fallback;
    }
    const std::string s = lowerCopy(m.args[i]);
    if (s == "1" || s == "true" || s == "yes") {
        return true;
    }
    if (s == "0" || s == "false" || s == "no") {
        return false;
    }
    return fallback;
}

Vec3 triggerWorld(const Level& level, const Trigger& tr) {
    const Mat4x3 roomX = roomWorldTransform(level, tr.properties.roomId);
    return transformPoint(combine(roomX, tr.properties.objectToRoom), Vec3(0, 0, 0));
}

Vec3 entityWorld(const Level& level, const EntityProperties& p) {
    const Mat4x3 roomX = roomWorldTransform(level, p.roomId);
    return transformPoint(combine(roomX, p.objectToRoom), Vec3(0, 0, 0));
}

float horizDist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool endsWithLower(const std::string& s, const char* suffixLower) {
    const std::string n = std::string(suffixLower);
    if (s.size() < n.size()) {
        return false;
    }
    const std::string tail = lowerCopy(s.substr(s.size() - n.size()));
    return tail == n;
}

// A trigger "X.TRIGGER" is wired to the level FSM named "X"
// (X_LevelDBTrigger::getFSM; verified on Part1_Level1: 131/131 triggers match).
std::string triggerFsmName(const Trigger& tr) {
    if (endsWithLower(tr.sharedName, ".trigger")) {
        return tr.sharedName.substr(0, tr.sharedName.size() - 8);
    }
    return tr.sharedName;
}

int findFsmIndexByName(const Level& level, const std::string& name) {
    if (name.empty()) {
        return -1;
    }
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < level.fsms.size(); ++i) {
        if (lowerCopy(level.fsms[i].sharedName) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int findTriggerIndexByName(const Level& level, const std::string& name) {
    if (name.empty()) {
        return -1;
    }
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        if (lowerCopy(level.triggers[i].sharedName) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int findDynamicMeshIndexByName(const Level& level, const std::string& name) {
    if (name.empty()) {
        return -1;
    }
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < level.dynamicMeshes.size(); ++i) {
        if (lowerCopy(level.dynamicMeshes[i].name) == key) {
            return static_cast<int>(i);
        }
    }
    // ".DO" objects are dynamic meshes; be forgiving about the suffix.
    if (endsWithLower(name, ".do")) {
        return findDynamicMeshIndexByName(level, name.substr(0, name.size() - 3));
    }
    return -1;
}


int findActorCapsule(const std::vector<CharacterActor>& actors, const Vec3& origin, const Vec3& dir,
                     float maxDist, float* tOut) {
    int best = -1;
    float bestT = maxDist;
    const Vec3 nd = length(dir) > 1.0e-6f ? normalize(dir) : Vec3(0, 0, 1);
    for (std::size_t i = 0; i < actors.size(); ++i) {
        const CharacterActor& a = actors[i];
        if (a.activity == kCharDead) {
            continue;
        }
        const float rad = a.capsuleRadius();
        const float h = a.capsuleCenterHeight();
        const Vec3 c(a.position.x, a.position.y + h, a.position.z);
        const Vec3 w = c - origin;
        const float t = dot(w, nd);
        if (t < 0.0f || t > bestT) {
            continue;
        }
        const Vec3 closest = origin + nd * t;
        const Vec3 d = closest - c;
        // Vertical capsule: ignore Y beyond the half-height.
        const float half = std::max(0.4f, h);
        Vec3 dFlat = d;
        if (dFlat.y > half) {
            dFlat.y -= half;
        } else if (dFlat.y < -half) {
            dFlat.y += half;
        } else {
            dFlat.y = 0.0f;
        }
        if (dot(dFlat, dFlat) <= rad * rad) {
            best = static_cast<int>(i);
            bestT = t;
        }
    }
    if (tOut) {
        *tOut = bestT;
    }
    return best;
}

}  // namespace

PlayerState::PlayerState()
    : yaw(0.0f),
      pitch(0.0f),
      grounded(false),
      noclip(false),
      health(60.0f),
      maxHealth(60.0f),
      eyeHeight(1.6f),
      radius(0.48f),
      walkSpeed(4.2f),
      airborneSpeed(2.5f),
      jumpSpeed(7.5f),
      shootCooldown(0.0f),
      clip(18),
      ammo(20),
      clipSize(18),
      weaponName("beretta"),
      crosshair(true),
      controlsEnabled(true),
      selectedSlot(-1) {}

GameRuntime::GameRuntime()
    : mode(kModeMenu),
      menu(kMenuRoot),
      menuCursor(0),
      menuScroll(0),
      comicIndex(0),
      comicChapter(0),
      comicFromMenu(false),
      soundOrigin(0.0f, 0.0f, 0.0f),
      impactNormal(0.0f, 1.0f, 0.0f) {}

void GameRuntime::loadCatalog(const std::string& dbRoot) { catalog = loadGameCatalog(dbRoot); }

void GameRuntime::loadCameraPaths(const std::string& dbRoot) { cameraPaths.load(dbRoot); }

void CinematicState::fadeColor(float* rgba) const {
    if (!fading()) {
        for (int i = 0; i < 4; ++i) {
            rgba[i] = fadeTo[i];
        }
        // A finished fade holds its target colour at full strength.
        rgba[3] = fadeDuration > 0.0f ? fadeTo[3] : 0.0f;
        return;
    }
    const float t = fadeElapsed / fadeDuration;
    for (int i = 0; i < 4; ++i) {
        rgba[i] = fadeFrom[i] + (fadeTo[i] - fadeFrom[i]) * t;
    }
}

void GameRuntime::startCinematic(int clipIndex, float duration, int frameRate,
                                 const std::vector<ClipFrameMessage>& messages,
                                 const std::string& movementFile) {
    if (duration <= 0.0f) {
        // No clip KF2 (stripped database): run to the last frame hook + 1s
        // so the cutscene always terminates; hookless clips get 2s.
        int last = 0;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            if (messages[i].frame > last) {
                last = messages[i].frame;
            }
        }
        duration = last > 0
                       ? static_cast<float>(last) / static_cast<float>(frameRate > 0 ? frameRate : 30) + 1.0f
                       : 2.0f;
    }
    cine.active = true;
    cine.clipIndex = clipIndex;
    cine.time = 0.0f;
    cine.duration = duration;
    cine.frameRate = frameRate > 0 ? frameRate : 30;
    cine.lastFrame = -1;
    cine.messages = messages;
    cine.movementFile = movementFile;
    // Frame hooks must fire in frame order even when authored out of order.
    for (std::size_t i = 1; i < cine.messages.size(); ++i) {
        const ClipFrameMessage key = cine.messages[i];
        std::size_t j = i;
        while (j > 0 && cine.messages[j - 1].frame > key.frame) {
            cine.messages[j] = cine.messages[j - 1];
            --j;
        }
        cine.messages[j] = key;
    }
    pushLog("cinematic clip " + (clipIndex >= 0 ? std::to_string(clipIndex) : std::string("?")));
}

void GameRuntime::stopCinematic() {
    cine.active = false;
    cine.cinematicMode = false;
    // Widescreen / HUD / the fade hold while the camera path may still be
    // flying; tickCinematic clears them once the whole cutscene is over and
    // abortCinematic clears them on Esc.
    pushLog("cinematic end");
}

void GameRuntime::abortCinematic() {
    stopCinematic();
    cine.cameraActive = false;
    cine.fadeDuration = 0.0f;
    cine.widescreen = false;
    cine.hudVisible = true;
    pushLog("cinematic aborted");
}

bool GameRuntime::cameraPathAbortable() const {
    if (!cine.cameraActive) {
        return false;
    }
    const CameraPathDef* def = cameraPaths.find(cine.cameraPath);
    return def != 0 && def->abortable;
}

void GameRuntime::abortCameraPath(const Level& level, std::vector<CharacterActor>& actors) {
    if (!cine.cameraActive) {
        return;
    }
    cine.cameraActive = false;
    // User abort still runs the [Exit] messages (the engine returns the
    // camera behind the player and the scripted transition continues).
    const Vec3 keep = soundOrigin;
    soundOrigin = player.position;
    for (std::size_t i = 0; i < cine.cameraExitMessages.size(); ++i) {
        const std::vector<GameMessage> msgs = parseGameMessages(cine.cameraExitMessages[i]);
        for (std::size_t m = 0; m < msgs.size(); ++m) {
            dispatch(msgs[m], level, actors, -1);
        }
    }
    soundOrigin = keep;
}

void GameRuntime::startCameraPath(const std::string& name, int mode, const std::string& room) {
    const CameraPathDef* def = cameraPaths.find(name);
    if (def == 0) {
        pushLog("camera path not found: " + name);
        return;
    }
    cine.cameraActive = true;
    cine.cameraPath = lowerCopy(name);
    cine.cameraTime = 0.0f;
    cine.cameraDuration = 0.0f;  // the host resolves this from the path KF2
    cine.cameraMode = mode;
    cine.cameraRoom = room;
    cine.cameraExitMessages = def->exitMessages;
    pushLog(std::string("camera path ") + (def->resolvedAnimation.empty()
                                               ? name + " (kf2 missing)"
                                               : def->resolvedAnimation));
}

void GameRuntime::tickCinematic(float dt, const Level& level, std::vector<CharacterActor>& actors) {
    if (cine.active) {
        cine.time += dt;
        // Frame-message edges: every [Message] Frame hook between the last
        // dispatched frame and the current one runs, in frame order.
        const int fps = cine.frameRate > 0 ? cine.frameRate : 30;
        const int frame = static_cast<int>(cine.time * static_cast<float>(fps));
        const Vec3 keep = soundOrigin;
        soundOrigin = player.position;
        for (std::size_t i = 0; i < cine.messages.size(); ++i) {
            const int f = cine.messages[i].frame;
            if (f > cine.lastFrame && f <= frame && !cine.messages[i].text.empty()) {
                const std::vector<GameMessage> msgs = parseGameMessages(cine.messages[i].text);
                for (std::size_t m = 0; m < msgs.size(); ++m) {
                    dispatch(msgs[m], level, actors, -1);
                }
            }
        }
        cine.lastFrame = frame;
        soundOrigin = keep;
        if (cine.duration > 0.0f && cine.time >= cine.duration) {
            stopCinematic();
        }
    }
    // Fades advance after the dispatch so a fade started by a frame hook
    // already gets this frame's dt.
    if (cine.fading()) {
        cine.fadeElapsed += dt;
    }
    if (cine.cameraActive) {
        cine.cameraTime += dt;
        if (cine.cameraDuration > 0.0f && cine.cameraTime >= cine.cameraDuration) {
            // X_CameraPath [Exit] messages run when the path finishes.
            cine.cameraActive = false;
            const Vec3 keep = soundOrigin;
            soundOrigin = player.position;
            for (std::size_t i = 0; i < cine.cameraExitMessages.size(); ++i) {
                const std::vector<GameMessage> msgs = parseGameMessages(cine.cameraExitMessages[i]);
                for (std::size_t m = 0; m < msgs.size(); ++m) {
                    dispatch(msgs[m], level, actors, -1);
                }
            }
            soundOrigin = keep;
        }
    }
    // Whole cutscene over (clip done, path done): nothing of the
    // presentation survives - a finished fade-to-black used to hold the
    // black overlay forever, which looked like a cutscene that never ends.
    if (!cine.active && !cine.cameraActive) {
        cine.fadeDuration = 0.0f;
        cine.widescreen = false;
        cine.hudVisible = true;
    }
}

void GameRuntime::pushLog(const std::string& line) {
    lastEvent = line;
    log.push_back(line);
    if (log.size() > 12) {
        log.erase(log.begin());
    }
}

void GameRuntime::resetLevel(const Level& level, const LevelInfo* info, const Vec3& spawnLdb,
                             float spawnYaw) {
    player = PlayerState();
    player.position = spawnLdb;
    player.yaw = spawnYaw;
    player.grounded = true;
    cine = CinematicState();  // no cutscene survives a level change
    if (info && info->playerSkinName.size()) {
        // max_payne.txt: MaximumHealth 60, CapsuleRadius 0.48, AirborneSpeed 2.5,
        // PLAYER_MOVEMENT 4.2, C_Jump(7.5).
        player.maxHealth = 60.0f;
        player.health = 60.0f;
        player.radius = 0.48f;
        player.airborneSpeed = 2.5f;
        player.walkSpeed = 4.2f;
        player.jumpSpeed = 7.5f;
    }
    const WeaponDef* w = catalog.findWeapon(player.weaponName);
    if (w) {
        player.clipSize = w->clipSize;
        player.clip = w->clipSize;
        player.ammo = 20;
    }
    triggers.assign(level.triggers.size(), TriggerState());
    fsmStates.assign(level.fsms.size(), FsmRuntime());
    for (std::size_t i = 0; i < level.fsms.size() && i < fsmStates.size(); ++i) {
        fsmStates[i].state = level.fsms[i].defaultState;
    }
    doors.assign(level.dynamicMeshes.size(), DoorState());
    itemTaken.assign(level.items.size(), 0);
    pendingNotes.clear();
    prompt.clear();
    pendingSounds.clear();
    effects.clear();
    effects.installBuiltins();
    soundOrigin = Vec3(0.0f, 0.0f, 0.0f);
    mode = kModePlaying;
    giveAllWeapons();
}

// ---------------------------------------------------------------------------
// Weapons (X_CharacterWeapons)

void GameRuntime::giveAllWeapons() {
    player.slots.clear();
    // weaponpriority.txt [CycleWeapons] order first, then anything else by
    // WeaponID (slot order).
    std::vector<std::string> order = catalog.cycleOrder;
    for (std::map<std::string, WeaponDef>::const_iterator it = catalog.weapons.begin();
         it != catalog.weapons.end(); ++it) {
        bool listed = false;
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == it->first) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            order.push_back(it->first);
        }
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        const WeaponDef* w = catalog.findWeapon(order[i]);
        if (w == 0) {
            continue;  // cycle order referencing an unknown weapon
        }
        PlayerState::WeaponSlot slot;
        slot.name = order[i];
        slot.clip = w->clipSize;
        slot.ammo = w->pocketSize;
        player.slots.push_back(slot);
    }
    if (player.slots.empty()) {
        // No parsed catalog: keep the single default weapon.
        PlayerState::WeaponSlot slot;
        slot.name = player.weaponName;
        slot.clip = player.clip;
        slot.ammo = player.ammo;
        player.slots.push_back(slot);
    }
    selectSlot(0);
}

int GameRuntime::findSlotOfWeapon(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < player.slots.size(); ++i) {
        if (player.slots[i].name == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool GameRuntime::selectSlot(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= player.slots.size()) {
        return false;
    }
    player.selectedSlot = index;
    const std::string& name = player.slots[static_cast<std::size_t>(index)].name;
    const WeaponDef* w = catalog.findWeapon(name);
    player.weaponName = name;
    if (w) {
        player.clipSize = w->clipSize;
    }
    player.clip = player.slots[static_cast<std::size_t>(index)].clip;
    player.ammo = player.slots[static_cast<std::size_t>(index)].ammo;
    pushLog(std::string("C_SelectWeapon ") + name);
    return true;
}

bool GameRuntime::cycleWeapon(int delta) {
    if (player.slots.empty()) {
        return false;
    }
    int cur = player.selectedSlot < 0 ? 0 : player.selectedSlot;
    const int n = static_cast<int>(player.slots.size());
    cur = ((cur + delta) % n + n) % n;
    return selectSlot(cur);
}

bool GameRuntime::reloadWeapon() {
    if (player.selectedSlot < 0 ||
        static_cast<std::size_t>(player.selectedSlot) >= player.slots.size()) {
        return false;
    }
    PlayerState::WeaponSlot& slot = player.slots[static_cast<std::size_t>(player.selectedSlot)];
    const WeaponDef* w = catalog.findWeapon(slot.name);
    const int clipSize = w ? w->clipSize : player.clipSize;
    if (slot.clip >= clipSize || slot.ammo <= 0) {
        return false;
    }
    const int need = clipSize - slot.clip;
    const int take = slot.ammo < need ? slot.ammo : need;
    slot.clip += take;
    slot.ammo -= take;
    player.clip = slot.clip;
    player.ammo = slot.ammo;
    pushLog("C_Reload");
    return true;
}

const WeaponDef* GameRuntime::currentWeaponDef() const {
    return catalog.findWeapon(player.weaponName);
}

void GameRuntime::applyPlayerOnInit(const CharacterConfig* playerCfg) {
    if (playerCfg == 0) {
        return;
    }
    player.maxHealth = playerCfg->maxHealth > 1.0f ? playerCfg->maxHealth : player.maxHealth;
    player.radius = playerCfg->capsule.radius > 0.05f ? playerCfg->capsule.radius : player.radius;
    player.airborneSpeed =
        playerCfg->airborneSpeed > 0.0f ? playerCfg->airborneSpeed : player.airborneSpeed;
    Level empty;
    std::vector<CharacterActor> none;
    dispatchList(playerCfg->onInitMessages, empty, none, -1);
}

Vec3 ldbFromView(const Vec3& viewPos, float eyeHeight) {
    return Vec3(-viewPos.x, viewPos.y - eyeHeight, viewPos.z);
}

Vec3 viewFromLdb(const Vec3& ldbFeet, float eyeHeight) {
    return Vec3(-ldbFeet.x, ldbFeet.y + eyeHeight, ldbFeet.z);
}

Vec3 GameRuntime::viewPosition() const { return viewFromLdb(player.position, player.eyeHeight); }

float GameRuntime::viewYaw() const {
    // Spawn yaw is already in view space (mirrorX of LDB +Z). Player.yaw is
    // kept in that same view-space convention so the camera can use it as-is.
    return player.yaw;
}

void GameRuntime::setLook(float yawView, float pitch) {
    player.yaw = yawView;
    player.pitch = pitch;
}

void GameRuntime::menuMove(int delta, int levelCount) {
    int n = 4;
    if (menu == kMenuJumpLevel) {
        n = std::max(1, levelCount);
    } else if (menu == kMenuComic) {
        n = std::max(1, static_cast<int>(catalog.chapters.size()));
    } else if (menu == kMenuComicPages) {
        if (comicChapter >= 0 && static_cast<std::size_t>(comicChapter) < catalog.chapters.size()) {
            n = std::max(1, static_cast<int>(catalog.chapters[static_cast<std::size_t>(comicChapter)].pageIndices.size()));
        } else {
            n = std::max(1, static_cast<int>(catalog.pages.size()));
        }
    }
    menuCursor += delta;
    while (menuCursor < 0) {
        menuCursor += n;
    }
    while (menuCursor >= n) {
        menuCursor -= n;
    }
}

bool GameRuntime::menuChoose(int* jumpLevel, int* openPage, bool* quit, bool* newGame) {
    *jumpLevel = -1;
    *openPage = -1;
    *quit = false;
    *newGame = false;
    if (menu == kMenuRoot) {
        if (menuCursor == 0) {
            *newGame = true;
            return true;
        }
        if (menuCursor == 1) {
            menu = kMenuJumpLevel;
            menuCursor = 0;
            return false;
        }
        if (menuCursor == 2) {
            menu = kMenuComic;
            menuCursor = 0;
            return false;
        }
        *quit = true;
        return true;
    }
    if (menu == kMenuJumpLevel) {
        *jumpLevel = menuCursor;
        menu = kMenuRoot;
        menuCursor = 0;
        return true;
    }
    if (menu == kMenuComic) {
        if (catalog.chapters.empty()) {
            *openPage = menuCursor;
            comicIndex = menuCursor;
            comicFromMenu = true;
            mode = kModeGraphicNovel;
            return true;
        }
        comicChapter = menuCursor;
        menu = kMenuComicPages;
        menuCursor = 0;
        return false;
    }
    if (menu == kMenuComicPages) {
        int page = menuCursor;
        if (comicChapter >= 0 && static_cast<std::size_t>(comicChapter) < catalog.chapters.size()) {
            const GraphicNovelChapter& ch = catalog.chapters[static_cast<std::size_t>(comicChapter)];
            if (menuCursor >= 0 && static_cast<std::size_t>(menuCursor) < ch.pageIndices.size()) {
                page = ch.pageIndices[static_cast<std::size_t>(menuCursor)];
            }
        }
        *openPage = page;
        comicIndex = page;
        comicFromMenu = true;
        mode = kModeGraphicNovel;
        return true;
    }
    return false;
}

void GameRuntime::tickPlayer(float dt, bool forward, bool back, bool left, bool right, bool jump,
                             bool sprint, CollisionWorld& world, bool descend) {
    if (!player.controlsEnabled && !player.noclip) {
        return;
    }
    if (player.shootCooldown > 0.0f) {
        player.shootCooldown -= dt;
    }
    float f = 0.0f;
    float s = 0.0f;
    if (forward) {
        f += 1.0f;
    }
    if (back) {
        f -= 1.0f;
    }
    if (right) {
        s += 1.0f;
    }
    if (left) {
        s -= 1.0f;
    }
    const float speed = player.noclip ? (sprint ? 18.0f : 6.0f)
                                      : ((sprint ? player.walkSpeed * 1.35f : player.walkSpeed) *
                                         (player.grounded ? 1.0f : 0.0f) +
                                         (player.grounded ? 0.0f : player.airborneSpeed));
    // View-space wish on XZ, converted to LDB (X mirrored).
    const float cy = std::cos(player.yaw);
    const float sy = std::sin(player.yaw);
    Vec3 wishView(sy * f + cy * s, 0.0f, -cy * f + sy * s);
    if (player.noclip) {
        wishView.y = 0.0f;
    }
    const float wl = length(wishView);
    if (wl > 1.0e-4f) {
        wishView = wishView * (1.0f / wl);
    }
    Vec3 wishLdb(-wishView.x, wishView.y, wishView.z);
    if (player.noclip) {
        player.position += wishLdb * (speed * dt);
        if (jump) {
            player.position.y += speed * dt;
        }
        if (descend) {
            player.position.y -= speed * dt;
        }
        player.velocity = Vec3();
        player.grounded = true;
        return;
    }
    if (player.grounded && jump) {
        player.velocity.y = player.jumpSpeed;
        player.grounded = false;
        pushLog("C_Jump");
    }
    // Gravity: levels.txt GRAVITY_VALUE -981 is cm/s^2; world is metres.
    if (!player.grounded) {
        player.velocity.y += -9.81f * dt;
    }
    Vec3 delta = wishLdb * (speed * dt);
    delta.y += player.velocity.y * dt;
    const float half = std::max(0.4f, player.eyeHeight * 0.5f);
    const Vec3 start(player.position.x, player.position.y + half, player.position.z);
    const Vec3 end = world.moveSphere(start, delta, player.radius);
    player.position.x = end.x;
    player.position.z = end.z;
    const float appliedY = end.y - start.y;
    player.position.y = end.y - half;
    if (delta.y < -0.001f && appliedY > delta.y + 0.01f) {
        player.grounded = true;
        player.velocity.y = 0.0f;
    } else if (delta.y > 0.02f && appliedY < delta.y - 0.01f) {
        player.velocity.y = 0.0f;
        player.grounded = false;
    } else if (player.velocity.y > 0.1f) {
        player.grounded = false;
    }
    if (player.position.y < -50.0f) {
        player.position.y = 0.0f;
        player.velocity.y = 0.0f;
        player.grounded = true;
    }
    // Floor snap.
    const CollisionHit floor = world.raycast(
        Vec3(player.position.x, player.position.y + 1.2f, player.position.z), Vec3(0, -1, 0), 2.5f);
    if (floor.hit && player.velocity.y <= 0.1f) {
        const float feet = floor.point.y;
        if (player.position.y - feet < 0.35f && player.position.y - feet > -0.2f) {
            player.position.y = feet;
            player.grounded = true;
            player.velocity.y = 0.0f;
        }
    }
}

namespace {

// AnimationGraph easing: flat float samples read as the eased parameter at
// uniform intervals over [0, 1]. Only trust the graph when every sample is a
// plausible parameter (0..1); otherwise the block is some other layout and
// linear time is the safe fallback.
float graphEase(const AnimationGraph& g, float t) {
    if (g.samples.size() < 2) {
        return t;
    }
    for (std::size_t i = 0; i < g.samples.size(); ++i) {
        if (g.samples[i] < -0.01f || g.samples[i] > 1.01f) {
            return t;
        }
    }
    const float u = std::max(0.0f, std::min(1.0f, t)) * static_cast<float>(g.samples.size() - 1);
    const std::size_t i = static_cast<std::size_t>(u);
    if (i + 1 >= g.samples.size()) {
        return g.samples.back();
    }
    const float f = u - static_cast<float>(i);
    return g.samples[i] + (g.samples[i + 1] - g.samples[i]) * f;
}

}  // namespace

Mat4x3 GameRuntime::dynamicMeshPose(const MeshAnimation& anim, float t) const {
    const float st = graphEase(anim.translation, t);
    const float sr = graphEase(anim.rotation, t);
    Mat4x3 m;
    for (int r = 0; r < 4; ++r) {
        m.rows[r].x = anim.startTransform.rows[r].x +
                      (anim.endTransform.rows[r].x - anim.startTransform.rows[r].x) * (r < 3 ? sr : st);
        m.rows[r].y = anim.startTransform.rows[r].y +
                      (anim.endTransform.rows[r].y - anim.startTransform.rows[r].y) * (r < 3 ? sr : st);
        m.rows[r].z = anim.startTransform.rows[r].z +
                      (anim.endTransform.rows[r].z - anim.startTransform.rows[r].z) * (r < 3 ? sr : st);
    }
    orthonormalizeMat3(m);
    return m;
}

void GameRuntime::tickDoors(float dt, const Level& level) {
    for (std::size_t i = 0; i < doors.size() && i < level.dynamicMeshes.size(); ++i) {
        DoorState& d = doors[i];
        if (d.dir == 0) {
            continue;
        }
        const DynamicMesh& mesh = level.dynamicMeshes[i];
        float len = 1.0f;
        if (!mesh.animations.empty()) {
            len = std::max(0.15f, mesh.animations[0].lengthSeconds);
        }
        const float before = d.t;
        d.t += dt * static_cast<float>(d.dir) / len;
        // X_LevelRuntimeDynamicObject runs the MeshAnimation message lists at
        // the keyframe boundaries: leavingFirstKeyframe when the animation
        // starts moving away from keyframe 0, reachingSecondKeyframe at the
        // open end, returningFirstKeyframe when back at the start.
        const MeshAnimation* anim = mesh.animations.empty() ? 0 : &mesh.animations[0];
        if (anim != 0) {
            std::vector<CharacterActor> noActors;  // DO messages never target actors here
            const Vec3 keep = soundOrigin;
            soundOrigin = entityWorld(level, mesh.properties);
            if (d.dir > 0) {
                if (before <= 0.0f && d.t > 0.0f) {
                    dispatchList(anim->leavingFirstKeyframe.messages, level, noActors, -1);
                }
                if (before < 1.0f && d.t >= 1.0f) {
                    dispatchList(anim->reachingSecondKeyframe.messages, level, noActors, -1);
                }
            } else {
                if (before >= 1.0f && d.t < 1.0f) {
                    dispatchList(anim->returningFirstKeyframe.messages, level, noActors, -1);
                }
            }
            soundOrigin = keep;
        }
        if (d.t >= 1.0f) {
            d.t = 1.0f;
            d.dir = 0;
            d.open = true;
        } else if (d.t <= 0.0f) {
            d.t = 0.0f;
            d.dir = 0;
            d.open = false;
        }
    }
}

void GameRuntime::dispatchList(const std::vector<std::string>& lines, const Level& level,
                               std::vector<CharacterActor>& actors, int sourceTrigger) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::vector<GameMessage> msgs = parseGameMessages(lines[i]);
        for (std::size_t m = 0; m < msgs.size(); ++m) {
            dispatch(msgs[m], level, actors, sourceTrigger);
        }
    }
}

void GameRuntime::dispatch(const GameMessage& msg, const Level& level, std::vector<CharacterActor>& actors,
                           int sourceTrigger) {
    (void)sourceTrigger;
    if (methodIs(msg, "c_jump")) {
        player.velocity.y = floatArg(msg, 0, player.jumpSpeed);
        player.grounded = false;
        return;
    }
    if (methodIs(msg, "c_sethealth")) {
        const float v = floatArg(msg, 0, 1.0f);
        player.health = (v <= 1.001f) ? v * player.maxHealth : v;
        return;
    }
    if (methodIs(msg, "c_addhealth")) {
        player.health = std::min(player.maxHealth, player.health + floatArg(msg, 0, 0.0f));
        return;
    }
    if (methodIs(msg, "c_causedamage")) {
        player.health -= floatArg(msg, 0, 0.0f);
        return;
    }
    if (methodIs(msg, "c_displaycrosshair")) {
        player.crosshair = boolArg(msg, 0, true);
        return;
    }
    if (methodIs(msg, "c_pickupweapon")) {
        const std::string n = lowerArg(msg, 0);
        if (!n.empty()) {
            player.weaponName = n;
            const WeaponDef* w = catalog.findWeapon(n);
            if (w) {
                player.clipSize = w->clipSize;
                player.clip = w->clipSize;
            }
            pushLog(std::string("C_PickupWeapon ") + n);
        }
        return;
    }
    if (methodIs(msg, "c_pickupammo")) {
        player.ammo += static_cast<int>(floatArg(msg, 1, 0.0f));
        pushLog("C_PickupAmmo");
        return;
    }
    if (methodIs(msg, "c_reload")) {
        const int need = player.clipSize - player.clip;
        const int take = std::min(need, player.ammo);
        player.clip += take;
        player.ammo -= take;
        return;
    }
    if (methodIs(msg, "c_use")) {
        prompt = "C_Use";
        return;
    }
    if (methodIs(msg, "c_kill")) {
        player.health = 0.0f;
        return;
    }
    if (methodIs(msg, "c_setimmortal") || methodIs(msg, "c_setinvulnerable")) {
        return;
    }
    if (methodIs(msg, "gm_setplayercontrols") || methodIs(msg, "gm_setplayercontrolledslowmotion")) {
        player.controlsEnabled = boolArg(msg, 0, true);
        return;
    }
    if (methodIs(msg, "gm_enablebullettime")) {
        pushLog("GM_EnableBulletTime");
        return;
    }
    if (methodIs(msg, "gm_init") || methodIs(msg, "gm_setgamelevel")) {
        pushLog(std::string("GM_Init ") + (msg.args.empty() ? std::string() : msg.args[0]));
        return;
    }
    if (methodIs(msg, "do_animate") || methodIs(msg, "do_resumeanimation")) {
        // DO_Animate targets a specific dynamic mesh ("X.DO->DO_Animate(...)").
        // Only fall back to the first idle door when the target is unknown.
        int di = findDynamicMeshIndexByName(level, msg.target);
        if (di < 0) {
            for (std::size_t i = 0; i < doors.size(); ++i) {
                if (!doors[i].open && doors[i].dir == 0) {
                    di = static_cast<int>(i);
                    break;
                }
            }
        }
        if (di >= 0 && static_cast<std::size_t>(di) < doors.size()) {
            DoorState& ds = doors[static_cast<std::size_t>(di)];
            if (ds.dir == 0) {
                ds.dir = ds.open ? -1 : 1;
                pushLog(std::string("DO_Animate ") +
                        (di < static_cast<int>(level.dynamicMeshes.size())
                             ? level.dynamicMeshes[static_cast<std::size_t>(di)].name
                             : std::string()));
            }
        }
        return;
    }
    if (methodIs(msg, "do_invertanimation")) {
        int di = findDynamicMeshIndexByName(level, msg.target);
        if (di < 0) {
            for (std::size_t i = 0; i < doors.size(); ++i) {
                if (doors[i].open || doors[i].dir != 0) {
                    di = static_cast<int>(i);
                    break;
                }
            }
        }
        if (di >= 0 && static_cast<std::size_t>(di) < doors.size()) {
            DoorState& ds = doors[static_cast<std::size_t>(di)];
            if (ds.open || ds.dir != 0) {
                ds.dir = ds.open ? -1 : -ds.dir;
                pushLog("DO_InvertAnimation");
            }
        }
        return;
    }
    if (methodIs(msg, "do_stopanimation") || methodIs(msg, "do_pauseanimation")) {
        int di = findDynamicMeshIndexByName(level, msg.target);
        if (di >= 0 && static_cast<std::size_t>(di) < doors.size()) {
            doors[static_cast<std::size_t>(di)].dir = 0;
        } else {
            for (std::size_t i = 0; i < doors.size(); ++i) {
                doors[i].dir = 0;
            }
        }
        return;
    }
    if (methodIs(msg, "t_activate")) {
        // Chained activation: "X.TRIGGER->T_Activate()" targets one trigger.
        const int ti = findTriggerIndexByName(level, msg.target);
        if (ti >= 0) {
            activateTrigger(ti, level, actors);
        } else if (sourceTrigger >= 0) {
            activateTrigger(sourceTrigger, level, actors);
        } else {
            pushLog("T_Activate");
        }
        return;
    }
    if (methodIs(msg, "t_enable")) {
        // X_LevelRuntimeTrigger::receive(T_Enable): sets `enabled` and always
        // re-arms the activation latch (byte +1613 = 0).
        const int ti = findTriggerIndexByName(level, msg.target);
        if (ti >= 0 && static_cast<std::size_t>(ti) < triggers.size()) {
            const bool on = boolArg(msg, 0, true);
            triggers[static_cast<std::size_t>(ti)].enabled = on;
            triggers[static_cast<std::size_t>(ti)].activated = false;
            if (!on) {
                triggers[static_cast<std::size_t>(ti)].inside = false;
            }
            pushLog(std::string("T_Enable(") + (on ? "true" : "false") + ") " +
                    level.triggers[static_cast<std::size_t>(ti)].sharedName);
        }
        return;
    }
    if (methodIs(msg, "fsm_switch")) {
        // "X->FSM_Switch(state)" — switch the targeted FSM's current state.
        const int fi = findFsmIndexByName(level, msg.target);
        if (fi >= 0 && static_cast<std::size_t>(fi) < fsmStates.size()) {
            fsmStates[static_cast<std::size_t>(fi)].state = msg.args.empty() ? std::string() : msg.args[0];
        }
        return;
    }
    if (methodIs(msg, "fsm_send")) {
        // "X->FSM_Send(string)" — run the targeted FSM's custom-string event.
        const int fi = findFsmIndexByName(level, msg.target);
        if (fi >= 0 && !msg.args.empty()) {
            dispatchFsmEvent(fi, msg.args[0], level, actors, sourceTrigger);
        }
        return;
    }
    if (methodIs(msg, "s_modeswitch")) {
        // x_modeswitch->s_modeswitch(graphicnovel|game): enter the reader at
        // the first picked note (the MPGNM queue head).
        const std::string to = lowerArg(msg, 0);
        if (to == "graphicnovel") {
            if (mode != kModeGraphicNovel) {
                const int page = nextPendingNote();
                if (page >= 0) {
                    comicIndex = page;
                    comicFromMenu = false;
                    mode = kModeGraphicNovel;
                }
            }
        } else if (to == "game") {
            mode = kModePlaying;
        }
        return;
    }
    if (methodIs(msg, "mpgnm_pickupnote")) {
        // MaxPayne_GraphicNovelMode->MPGNM_PickUpNote(id): queue the page and
        // open the reader on it (in-game pickups enter MPGNM mode right away).
        // leaveComic pops the rest one page at a time (intro sequences pick
        // several notes before s_modeswitch).
        const std::string id = lowerArg(msg, 0);
        for (std::size_t i = 0; i < catalog.pages.size(); ++i) {
            if (lowerCopy(catalog.pages[i].id) == id) {
                pendingNotes.push_back(static_cast<int>(i));
                if (mode != kModeGraphicNovel) {
                    comicIndex = nextPendingNote();
                    comicFromMenu = false;
                    mode = kModeGraphicNovel;
                }
                pushLog(std::string("MPGNM_PickUpNote ") + id);
                return;
            }
        }
        pushLog(std::string("MPGNM_PickUpNote ") + id);
        return;
    }
    if (methodIs(msg, "mpgnm_markallnotesread")) {
        pushLog("MPGNM_MarkAllNotesRead");
        return;
    }
    if (methodIs(msg, "a_play3dsound") || methodIs(msg, "a_playfloating3dsound")) {
        // X_AudioMessage_Play3DSound(category, name): positioned at the
        // sending object (soundOrigin) — the trigger/FSM/door/projectile.
        SoundRequest q;
        q.is3d = true;
        if (msg.args.size() >= 2) {
            q.category = lowerCopy(msg.args[0]);
            q.name = lowerCopy(msg.args[1]);
        } else if (!msg.args.empty()) {
            q.name = lowerCopy(msg.args[0]);
        }
        if (!q.name.empty() && q.name != "empty") {
            q.position = soundOrigin;
            pendingSounds.push_back(q);
        }
        return;
    }
    if (methodIs(msg, "a_playsound")) {
        // X_AudioMessage_PlaySound(category, name): 2D (HUD / graphic novel).
        SoundRequest q;
        if (msg.args.size() >= 2) {
            q.category = lowerCopy(msg.args[0]);
            q.name = lowerCopy(msg.args[1]);
        } else if (!msg.args.empty()) {
            q.name = lowerCopy(msg.args[0]);
        }
        if (!q.name.empty() && q.name != "empty") {
            pendingSounds.push_back(q);
        }
        return;
    }
    if (methodIs(msg, "a_playmusic")) {
        // X_AudioMessage_PlayMusic(name): switch the level music track.
        SoundRequest q;
        q.music = true;
        q.name = msg.args.empty() ? std::string() : lowerCopy(msg.args[0]);
        if (!q.name.empty() && q.name != "empty") {
            pendingSounds.push_back(q);
        }
        return;
    }
    if (methodIs(msg, "a_stopmusic")) {
        SoundRequest q;
        q.stopMusic = true;
        pendingSounds.push_back(q);
        return;
    }
    if (methodIs(msg, "cam_animateabsolute") || methodIs(msg, "cam_animateinplace") ||
        methodIs(msg, "cam_animateparented") || methodIs(msg, "cam_animateplayerparented")) {
        // X_CameraMessage_Animate*: mode is per message variant; the args are
        // (blockName[, roomName]) for Absolute and (blockName) for the rest.
        int mode = 1;
        if (methodIs(msg, "cam_animateinplace")) {
            mode = 2;
        } else if (methodIs(msg, "cam_animateparented")) {
            mode = 3;
        } else if (methodIs(msg, "cam_animateplayerparented")) {
            mode = 4;
        }
        const std::string name = msg.args.empty() ? std::string() : lowerArg(msg, 0);
        std::string room;
        if (msg.args.size() >= 2) {
            room = lowerArg(msg, 1);
            // ::roomName marker -> plain name
            if (room.size() >= 2 && room[0] == ':' && room[1] == ':') {
                room = room.substr(2);
            }
        }
        if (!name.empty() && name != "empty") {
            startCameraPath(name, mode, room);
        }
        return;
    }
    if (methodIs(msg, "c_enablecinematicmode")) {
        cine.cinematicMode = boolArg(msg, 0, true);
        if (cine.cinematicMode) {
            player.controlsEnabled = false;
        }
        return;
    }
    if (methodIs(msg, "gm_enablewidescreen")) {
        cine.widescreen = boolArg(msg, 0, true);
        return;
    }
    if (methodIs(msg, "mphm_enablehud")) {
        cine.hudVisible = boolArg(msg, 0, true);
        return;
    }
    if (methodIs(msg, "mphm_fadetocolor")) {
        // MPHM_FadeToColor(from, to, time) with 0xRRGGBBAA colours.
        unpackColor(colorArg(msg, 0, 0u), cine.fadeFrom);
        unpackColor(colorArg(msg, 1, 0xFFu), cine.fadeTo);
        cine.fadeDuration = floatArg(msg, 2, 0.0f);
        cine.fadeElapsed = 0.0f;
        return;
    }
    if (methodIs(msg, "c_teleportxyz")) {
        // C_TeleportXYZ(x, y, z, yaw[, room]) — LDB space.
        player.position = Vec3(floatArg(msg, 0, player.position.x),
                               floatArg(msg, 1, player.position.y),
                               floatArg(msg, 2, player.position.z));
        player.yaw = floatArg(msg, 3, player.yaw) * 0.0174532925f;
        player.velocity = Vec3();
        return;
    }
    if (methodIs(msg, "ps_starteffect")) {
        // X_ParticleSystemMessage_PlayAnimation(name, anim): start a particle
        // effect at the sending object, oriented along the impact surface.
        if (!msg.args.empty()) {
            effects.startEffect(msg.args[0], soundOrigin, impactNormal);
        }
        return;
    }
    if (methodIs(msg, "ps_stopallemissions")) {
        effects.stopAll();
        return;
    }
    if (methodIs(msg, "d_createdecal")) {
        // X_DecalMessage_CreateDecal(name): project a decal on the impacted
        // surface (the normal comes from the projectile hit context; without
        // it wall decals lie flat in the floor plane and vanish edge-on).
        if (!msg.args.empty()) {
            effects.decals.spawn(msg.args[0], soundOrigin, impactNormal);
        }
        return;
    }
    if (methodIs(msg, "c_pickupweapon")) {
        if (!msg.args.empty()) {
            const int slot = findSlotOfWeapon(msg.args[0]);
            if (slot >= 0) {
                selectSlot(slot);
            } else {
                pushLog(std::string("C_PickupWeapon ") + msg.args[0]);
            }
        }
        return;
    }
    if (methodIs(msg, "c_pickupammo")) {
        if (!msg.args.empty()) {
            const int slot = findSlotOfWeapon(msg.args[0]);
            const int amount = msg.args.size() > 1 ? intArg(msg, 1, 10) : 10;
            if (slot >= 0) {
                player.slots[static_cast<std::size_t>(slot)].ammo += amount;
                if (slot == player.selectedSlot) {
                    player.ammo = player.slots[static_cast<std::size_t>(slot)].ammo;
                }
            }
            pushLog(std::string("C_PickupAmmo ") + msg.args[0]);
        }
        return;
    }
    if (methodIs(msg, "c_reload")) {
        reloadWeapon();
        return;
    }
    if (methodIs(msg, "ws_animate") || methodIs(msg, "ws_hide") ||
        methodIs(msg, "ws_animateshooting")) {
        // Weapon-model animation (RightHandWeapon->WS_Animate): the view
        // model is not rendered yet; keep it bookkept only.
        return;
    }
    if (methodIs(msg, "li_activatelevelitem") || methodIs(msg, "li_removelevelitem")) {
        return;
    }
    if (methodIs(msg, "c_teleport") && !msg.args.empty()) {
        pushLog(std::string("C_Teleport ") + msg.args[0]);
        return;
    }
    (void)actors;
}

int GameRuntime::nextPendingNote() {
    while (!pendingNotes.empty()) {
        const int page = pendingNotes.front();
        pendingNotes.erase(pendingNotes.begin());
        if (page >= 0 && static_cast<std::size_t>(page) < catalog.pages.size()) {
            return page;
        }
    }
    return -1;
}

bool GameRuntime::activateTrigger(int index, const Level& level, std::vector<CharacterActor>& actors) {
    if (index < 0 || static_cast<std::size_t>(index) >= level.triggers.size() ||
        static_cast<std::size_t>(index) >= triggers.size()) {
        return false;
    }
    TriggerState& st = triggers[static_cast<std::size_t>(index)];
    if (!st.enabled || st.activated) {
        return false;
    }
    st.activated = true;
    const Trigger& tr = level.triggers[static_cast<std::size_t>(index)];
    pushLog(std::string("T_Activate ") + tr.sharedName + " (" + triggerTypeName(tr.type) + ")");
    // X_LevelRuntimeTrigger::activate sends T_Activate to the trigger's own
    // FSM (X_LevelDBTrigger::getFSM) — never to unrelated level FSMs.
    const int fi = findFsmIndexByName(level, triggerFsmName(tr));
    if (fi >= 0) {
        const Vec3 keep = soundOrigin;
        soundOrigin = triggerWorld(level, tr);
        dispatchFsmEvent(fi, "T_Activate", level, actors, index);
        soundOrigin = keep;
    }
    return true;
}

void GameRuntime::dispatchFsmEvent(int fsmIndex, const std::string& eventName, const Level& level,
                                   std::vector<CharacterActor>& actors, int sourceTrigger) {
    if (fsmIndex < 0 || static_cast<std::size_t>(fsmIndex) >= level.fsms.size()) {
        return;
    }
    const Fsm& fsm = level.fsms[static_cast<std::size_t>(fsmIndex)];
    const std::string state =
        static_cast<std::size_t>(fsmIndex) < fsmStates.size()
            ? fsmStates[static_cast<std::size_t>(fsmIndex)].state
            : std::string();
    const std::string key = lowerCopy(eventName);
    // Entity events (T_Activate, DO_*) and custom strings (FSM_Send) share the
    // lookup: X_LevelRuntimeFSM::getOtherMessages keys both by name.
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<FsmEvent>& events =
            pass == 0 ? fsm.entityEvents : fsm.customString;
        for (std::size_t e = 0; e < events.size(); ++e) {
            if (lowerCopy(events[e].name) != key) {
                continue;
            }
            dispatchList(events[e].before.messages, level, actors, sourceTrigger);
            for (std::size_t s = 0; s < events[e].stateSpecific.size(); ++s) {
                if (lowerCopy(events[e].stateSpecific[s].stateName) == lowerCopy(state)) {
                    dispatchList(events[e].stateSpecific[s].messages.messages, level, actors,
                                 sourceTrigger);
                }
            }
            dispatchList(events[e].after.messages, level, actors, sourceTrigger);
        }
    }
}

void GameRuntime::startLevel(const Level& level, std::vector<CharacterActor>& actors) {
    // X_LevelRuntimeFSM runs every FSM's startup messages when the level loads
    // (the same lists collectLevelSoundCues reads for A_Play3DSound). The PC
    // v32 "state specific" startup block stays raw (no sample level writes
    // it), so only the before/after lists run.
    for (std::size_t i = 0; i < level.fsms.size(); ++i) {
        const Fsm& fsm = level.fsms[i];
        (void)fsm;
        dispatchList(level.fsms[i].startupBefore.messages, level, actors, -1);
        dispatchList(level.fsms[i].startupAfter.messages, level, actors, -1);
    }
}


void GameRuntime::tickTriggers(float dt, const Level& level, bool usePressed, const Vec3& lookDir,
                               std::vector<CharacterActor>& actors) {
    // X_LevelRuntimeTrigger semantics (Android decompile):
    //   isAutoActivate: types 1 (player collide), 3 (character collide) and
    //   4 (look-at) fire on their own; type 0 needs the use button; type 2 is
    //   projectiles (tryShoot). X_Character::canActivate: types 1/4 are
    //   player-only, 0/3 any character. activate() latches — one fire per arm.
    (void)dt;
    prompt.clear();
    const Vec3 feet = player.position;
    const Vec3 eye = Vec3(feet.x, feet.y + player.eyeHeight, feet.z);
    int nearestUse = -1;
    float nearestUseD = 1.0e9f;
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        if (i >= triggers.size()) {
            break;
        }
        TriggerState& st = triggers[i];
        const Trigger& tr = level.triggers[i];
        if (!st.enabled) {
            st.inside = false;
            continue;
        }
        const Vec3 w = triggerWorld(level, tr);
        const float rad = tr.radius > 0.05f ? tr.radius : 1.0f;
        const float d = horizDist(feet, w);
        const bool nowInside = d <= rad && std::fabs(feet.y - w.y) < 2.5f;
        const bool entered = nowInside && !st.inside;
        st.inside = nowInside;
        if (nowInside && tr.type == kTriggerActionButton && d < nearestUseD) {
            nearestUseD = d;
            nearestUse = static_cast<int>(i);
        }
        switch (tr.type) {
            case kTriggerActionButton:
                if (usePressed && nowInside) {
                    activateTrigger(static_cast<int>(i), level, actors);
                }
                break;
            case kTriggerPlayerCollide:
                if (entered) {
                    activateTrigger(static_cast<int>(i), level, actors);
                }
                break;
            case kTriggerCharacterCollide: {
                // Any character (player or NPC) entering the sphere.
                bool actorInside = false;
                for (std::size_t a = 0; a < actors.size(); ++a) {
                    const CharacterActor& act = actors[a];
                    if (act.activity == kCharDead) {
                        continue;
                    }
                    const Vec3 c(act.position.x, act.position.y + act.capsuleCenterHeight(),
                                act.position.z);
                    const Vec3 dv = c - w;
                    const float reach = act.capsuleRadius() + rad;
                    if (dot(dv, dv) <= reach * reach) {
                        actorInside = true;
                        break;
                    }
                }
                if (entered || actorInside) {
                    activateTrigger(static_cast<int>(i), level, actors);
                }
                break;
            }
            case kTriggerLookAt: {
                const Vec3 to = w - eye;
                const float ln = length(to);
                if (nowInside && ln > 0.05f && dot(normalize(to), lookDir) > 0.85f) {
                    activateTrigger(static_cast<int>(i), level, actors);
                }
                break;
            }
            default:
                break;
        }
    }
    if (usePressed) {
        // Pickup nearest item.
        for (std::size_t i = 0; i < level.items.size(); ++i) {
            if (i < itemTaken.size() && itemTaken[i]) {
                continue;
            }
            const Vec3 w = entityWorld(level, level.items[i].properties);
            if (horizDist(feet, w) < 1.4f) {
                if (i < itemTaken.size()) {
                    itemTaken[i] = 1;
                }
                pushLog(std::string("LI_ActivateLevelItem ") + level.items[i].itemName);
                const std::string n = lowerCopy(level.items[i].itemName);
                if (n.find("ammo") != std::string::npos) {
                    player.ammo += 18;
                } else if (n.find("medi") != std::string::npos) {
                    player.health = std::min(player.maxHealth, player.health + 20.0f);
                } else {
                    const WeaponDef* wpn = catalog.findWeapon(n);
                    if (wpn) {
                        player.weaponName = n;
                        player.clipSize = wpn->clipSize;
                        player.clip = wpn->clipSize;
                    }
                }
                break;
            }
        }
        // Use nearest door if no trigger claimed it.
        if (nearestUse < 0) {
            int bestDoor = -1;
            float bestD = 1.8f;
            for (std::size_t d = 0; d < level.dynamicMeshes.size(); ++d) {
                const Vec3 dw = entityWorld(level, level.dynamicMeshes[d].properties);
                const float dd = horizDist(feet, dw);
                if (dd < bestD) {
                    bestD = dd;
                    bestDoor = static_cast<int>(d);
                }
            }
            if (bestDoor >= 0) {
                DoorState& ds = doors[static_cast<std::size_t>(bestDoor)];
                ds.dir = ds.open ? -1 : 1;
                pushLog("C_Use / DO_Animate");
            }
        }
    }
    if (nearestUse >= 0) {
        prompt = std::string("E  Use  ") + level.triggers[static_cast<std::size_t>(nearestUse)].sharedName;
    }
}

HitscanHit GameRuntime::fireHitscan(const Level& level, CollisionWorld& world,
                                    std::vector<CharacterActor>& actors, const Vec3& origin,
                                    const Vec3& dir) {
    HitscanHit h;
    const WeaponDef* w = catalog.findWeapon(player.weaponName);
    float cast = w ? w->castLength : 100.0f;
    if (cast < 1.0f) {
        cast = 100.0f;
    }
    const Vec3 nd = length(dir) > 1.0e-6f ? normalize(dir) : Vec3(0, 0, 1);
    float actorT = cast;
    const int ai = findActorCapsule(actors, origin, nd, cast, &actorT);
    const CollisionHit worldHit = world.raycast(origin, nd, cast);
    float t = cast;
    if (ai >= 0) {
        t = actorT;
        h.hit = true;
        h.actorIndex = ai;
        h.point = origin + nd * actorT;
        h.normal = Vec3(-nd.x, -nd.y, -nd.z);
    }
    if (worldHit.hit && worldHit.t < t) {
        t = worldHit.t;
        h.hit = true;
        h.actorIndex = -1;
        h.point = worldHit.point;
        h.normal = worldHit.normal;
    }
    if (!h.hit) {
        h.point = origin + nd * cast;
    }
    // Projectile-collide triggers.
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        if (level.triggers[i].type != kTriggerProjectileCollide) {
            continue;
        }
        const Vec3 wpos = triggerWorld(level, level.triggers[i]);
        const float rad = std::max(0.3f, level.triggers[i].radius);
        // Closest point on the ray.
        const Vec3 wvec = wpos - origin;
        const float pt = dot(wvec, nd);
        if (pt < 0.0f || pt > t) {
            continue;
        }
        const Vec3 closest = origin + nd * pt;
        const Vec3 d = closest - wpos;
        if (dot(d, d) <= rad * rad) {
            h.triggerIndex = static_cast<int>(i);
            break;
        }
    }
    return h;
}

bool GameRuntime::tryShoot(const Level& level, CollisionWorld& world, std::vector<CharacterActor>& actors,
                           const Vec3& origin, const Vec3& dir) {
    if (player.shootCooldown > 0.0f) {
        return false;
    }
    // Sync the selected slot (the inventory owns clip/ammo).
    PlayerState::WeaponSlot* slot = 0;
    if (player.selectedSlot >= 0 &&
        static_cast<std::size_t>(player.selectedSlot) < player.slots.size()) {
        slot = &player.slots[static_cast<std::size_t>(player.selectedSlot)];
    }
    if (slot != 0 && slot->clip <= 0) {
        if (slot->ammo > 0) {
            reloadWeapon();
            player.shootCooldown = 0.6f;
        }
        return false;
    }
    const WeaponDef* w = catalog.findWeapon(player.weaponName);
    const float hz = w && w->shootHz > 0.1f ? w->shootHz : 4.0f;
    float dmg = w ? w->damage : 5.0f;
    if (w) {
        const ProjectileDef* pr = catalog.findProjectile(w->projectileName);
        if (pr) {
            dmg = pr->damage;
        }
    }
    if (slot != 0) {
        slot->clip -= 1;
        player.clip = slot->clip;
    } else {
        player.clip -= 1;
    }
    player.shootCooldown = 1.0f / hz;

    // The shooting character plays the weapon's fire sound and muzzle flash
    // (WEAPONANIM_SHOOT message list).
    if (w && !w->shootSoundName.empty()) {
        SoundRequest q;
        q.category = w->shootSoundCategory;
        q.name = w->shootSoundName;
        q.position = origin;
        q.is3d = true;
        pendingSounds.push_back(q);
    }
    effects.startEffect(w && !w->muzzleEffect.empty() ? w->muzzleEffect : "muzzleflash", origin,
                        Vec3(dir.x, dir.y, dir.z));

    HitscanHit hit = fireHitscan(level, world, actors, origin, dir);
    if (hit.actorIndex >= 0 && static_cast<std::size_t>(hit.actorIndex) < actors.size()) {
        CharacterActor& a = actors[static_cast<std::size_t>(hit.actorIndex)];
        a.applyDamage(dmg);
        pushLog(std::string("hitscan ") + a.skinName);
        // X_Projectile runs its message list when it damages a character.
        if (w) {
            const ProjectileDef* pr = catalog.findProjectile(w->projectileName);
            if (pr && !pr->messages.empty()) {
                runImpactMessages(*pr, hit.point, hit.normal, level, actors);
                effects.startEffect("blood", hit.point, Vec3(-dir.x, -dir.y, -dir.z));
            }
        }
    } else if (hit.hit) {
        pushLog("hitscan world");
        // X_Projectile message list on world impact: D_CreateDecal,
        // PS_StartEffect, A_Play3DSound ...
        if (w) {
            const ProjectileDef* pr = catalog.findProjectile(w->projectileName);
            if (pr && !pr->messages.empty()) {
                runImpactMessages(*pr, hit.point, hit.normal, level, actors);
            } else {
                effects.startEffect("impact", hit.point, hit.normal);
            }
        } else {
            effects.startEffect("impact", hit.point, hit.normal);
        }
    }
    if (hit.triggerIndex >= 0 && static_cast<std::size_t>(hit.triggerIndex) < triggers.size()) {
        // Type 2 (projectile) triggers: X_Projectile activates when the shot
        // path crosses the sphere (see fireHitscan) — same latch as the rest.
        activateTrigger(hit.triggerIndex, level, actors);
    }
    return true;
}

// X_Projectile hit handling: run the projectile script's [Message] list at
// the impact point (decals, particle effects, 3D sounds).
void GameRuntime::runImpactMessages(const ProjectileDef& pr, const Vec3& position, const Vec3& normal,
                                    const Level& level, std::vector<CharacterActor>& actors) {
    const Vec3 keep = soundOrigin;
    const Vec3 keepN = impactNormal;
    soundOrigin = position;
    Vec3 n = normal;
    const float ln = length(n);
    impactNormal = ln > 1.0e-5f ? Vec3(n.x / ln, n.y / ln, n.z / ln) : Vec3(0.0f, 1.0f, 0.0f);
    for (std::size_t i = 0; i < pr.messages.size(); ++i) {
        const std::vector<GameMessage> msgs = parseGameMessages(pr.messages[i]);
        for (std::size_t m = 0; m < msgs.size(); ++m) {
            dispatch(msgs[m], level, actors, -1);
        }
    }
    soundOrigin = keep;
    impactNormal = keepN;
}

void collectCuesFromText(const std::string& text, const Vec3& origin, std::vector<SoundCueRequest>* out) {
    const std::vector<GameMessage> msgs = parseGameMessages(text);
    for (std::size_t i = 0; i < msgs.size(); ++i) {
        const GameMessage& m = msgs[i];
        SoundCueRequest q;
        q.origin = origin;
        if (methodIs(m, "a_play3dsound")) {
            q.is3d = true;
            q.floating = false;
        } else if (methodIs(m, "a_playfloating3dsound")) {
            q.is3d = true;
            q.floating = true;
        } else if (methodIs(m, "a_playsound")) {
            q.is3d = false;
            q.floating = false;
        } else {
            continue;
        }
        if (m.args.size() >= 2) {
            q.category = lowerCopy(m.args[0]);
            q.name = lowerCopy(m.args[1]);
        } else if (!m.args.empty()) {
            q.name = lowerCopy(m.args[0]);
        } else {
            continue;
        }
        out->push_back(q);
    }
}

Vec3 fsmOrigin(const Level& level, const EntityProperties& p) {
    const Mat4x3 roomX = roomWorldTransform(level, p.roomId);
    return transformPoint(combine(roomX, p.objectToRoom), Vec3(0, 0, 0));
}

void collectLevelSoundCues(const Level& level, std::vector<SoundCueRequest>* out) {
    if (out == 0) {
        return;
    }
    for (std::size_t i = 0; i < level.fsms.size(); ++i) {
        const Fsm& fsm = level.fsms[i];
        const Vec3 origin = fsmOrigin(level, fsm.properties);
        for (std::size_t k = 0; k < fsm.startupBefore.messages.size(); ++k) {
            collectCuesFromText(fsm.startupBefore.messages[k], origin, out);
        }
        for (std::size_t k = 0; k < fsm.startupAfter.messages.size(); ++k) {
            collectCuesFromText(fsm.startupAfter.messages[k], origin, out);
        }
        for (std::size_t k = 0; k < fsm.startupStateSpecific.size(); ++k) {
            collectCuesFromText(fsm.startupStateSpecific[k], origin, out);
        }
    }
    for (std::size_t i = 0; i < level.characters.size(); ++i) {
        const Character& ch = level.characters[i];
        const Vec3 origin = fsmOrigin(level, ch.properties);
        for (std::size_t k = 0; k < ch.onStartup.messages.size(); ++k) {
            collectCuesFromText(ch.onStartup.messages[k], origin, out);
        }
    }
}

}  // namespace maxfx
