#include "maxfx/game/Runtime.h"

#include "maxfx/core/Fs.h"
#include "maxfx/script/Script.h"

#include <algorithm>
#include <cmath>
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
      controlsEnabled(true) {}

GameRuntime::GameRuntime()
    : mode(kModeMenu),
      menu(kMenuRoot),
      menuCursor(0),
      menuScroll(0),
      comicIndex(0),
      comicChapter(0),
      comicFromMenu(false) {}

void GameRuntime::loadCatalog(const std::string& dbRoot) { catalog = loadGameCatalog(dbRoot); }

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
    doors.assign(level.dynamicMeshes.size(), DoorState());
    itemTaken.assign(level.items.size(), 0);
    prompt.clear();
    mode = kModePlaying;
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
                             bool sprint, CollisionWorld& world) {
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

void GameRuntime::tickDoors(float dt, const Level& level) {
    for (std::size_t i = 0; i < doors.size() && i < level.dynamicMeshes.size(); ++i) {
        DoorState& d = doors[i];
        if (d.dir == 0) {
            continue;
        }
        float len = 1.0f;
        if (!level.dynamicMeshes[i].animations.empty()) {
            len = std::max(0.15f, level.dynamicMeshes[i].animations[0].lengthSeconds);
        }
        d.t += dt * static_cast<float>(d.dir) / len;
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
        for (std::size_t i = 0; i < doors.size(); ++i) {
            if (!doors[i].open && doors[i].dir == 0) {
                doors[i].dir = 1;
                pushLog("DO_Animate");
                break;
            }
        }
        return;
    }
    if (methodIs(msg, "do_invertanimation")) {
        for (std::size_t i = 0; i < doors.size(); ++i) {
            if (doors[i].open || doors[i].dir != 0) {
                doors[i].dir = doors[i].open ? -1 : -doors[i].dir;
                pushLog("DO_InvertAnimation");
                break;
            }
        }
        return;
    }
    if (methodIs(msg, "do_stopanimation") || methodIs(msg, "do_pauseanimation")) {
        for (std::size_t i = 0; i < doors.size(); ++i) {
            doors[i].dir = 0;
        }
        return;
    }
    if (methodIs(msg, "t_activate")) {
        pushLog("T_Activate");
        return;
    }
    if (methodIs(msg, "t_enable")) {
        return;
    }
    if (methodIs(msg, "mpgnm_pickupnote")) {
        const std::string id = lowerArg(msg, 0);
        for (std::size_t i = 0; i < catalog.pages.size(); ++i) {
            if (lowerCopy(catalog.pages[i].id) == id) {
                comicIndex = static_cast<int>(i);
                comicFromMenu = false;
                mode = kModeGraphicNovel;
                pushLog(std::string("MPGNM_PickUpNote ") + id);
                return;
            }
        }
        pushLog(std::string("MPGNM_PickUpNote ") + id);
        return;
    }
    if (methodIs(msg, "li_activatelevelitem") || methodIs(msg, "li_removelevelitem")) {
        return;
    }
    if (methodIs(msg, "c_teleport") && !msg.args.empty()) {
        pushLog(std::string("C_Teleport ") + msg.args[0]);
        return;
    }
    (void)level;
    (void)actors;
}

void GameRuntime::tickTriggers(float dt, const Level& level, bool usePressed, const Vec3& lookDir,
                               std::vector<CharacterActor>& actors) {
    prompt.clear();
    const Vec3 feet = player.position;
    int nearestUse = -1;
    float nearestUseD = 1.0e9f;
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        if (i >= triggers.size()) {
            break;
        }
        TriggerState& st = triggers[i];
        if (st.cool > 0.0f) {
            st.cool -= dt;
        }
        const Trigger& tr = level.triggers[i];
        const Vec3 w = triggerWorld(level, tr);
        const float rad = tr.radius > 0.05f ? tr.radius : 1.0f;
        const float d = horizDist(feet, w);
        const bool nowInside = d <= rad && std::fabs(feet.y - w.y) < 2.5f;
        const bool entered = nowInside && !st.inside;
        st.inside = nowInside;
        if (nowInside && (tr.type == kTriggerActionButton || tr.type == kTriggerLookAt)) {
            if (d < nearestUseD) {
                nearestUseD = d;
                nearestUse = static_cast<int>(i);
            }
        }
        bool fire = false;
        if (tr.type == kTriggerPlayerCollide && entered && st.cool <= 0.0f) {
            fire = true;
        }
        if (tr.type == kTriggerLookAt && nowInside && st.cool <= 0.0f) {
            const Vec3 to = w - Vec3(feet.x, feet.y + player.eyeHeight, feet.z);
            const float ln = length(to);
            if (ln > 0.05f && dot(normalize(to), lookDir) > 0.85f) {
                fire = true;
            }
        }
        if (tr.type == kTriggerActionButton && usePressed && nowInside && st.cool <= 0.0f) {
            fire = true;
        }
        if (!fire) {
            continue;
        }
        st.cool = 0.4f;
        st.used = true;
        pushLog(std::string("T_Activate ") + tr.sharedName + " (" + triggerTypeName(tr.type) + ")");
        // Fire matching FSM entity events / custom strings.
        for (std::size_t f = 0; f < level.fsms.size(); ++f) {
            const Fsm& fsm = level.fsms[f];
            const bool nameHit = lowerCopy(fsm.sharedName).find(lowerCopy(tr.sharedName)) != std::string::npos ||
                                 lowerCopy(tr.sharedName).find(lowerCopy(fsm.sharedName)) != std::string::npos;
            for (std::size_t e = 0; e < fsm.entityEvents.size(); ++e) {
                const std::string en = lowerCopy(fsm.entityEvents[e].name);
                const bool kind = en.find("activ") != std::string::npos || en.find("use") != std::string::npos ||
                                  en.find("player") != std::string::npos || en.find("collide") != std::string::npos;
                if (nameHit || kind) {
                    dispatchList(fsm.entityEvents[e].before.messages, level, actors, static_cast<int>(i));
                    dispatchList(fsm.entityEvents[e].after.messages, level, actors, static_cast<int>(i));
                }
            }
            for (std::size_t e = 0; e < fsm.customString.size(); ++e) {
                const std::string en = lowerCopy(fsm.customString[e].name);
                if (en.find("activ") != std::string::npos || nameHit) {
                    dispatchList(fsm.customString[e].before.messages, level, actors, static_cast<int>(i));
                    dispatchList(fsm.customString[e].after.messages, level, actors, static_cast<int>(i));
                }
            }
        }
        // Use / collide also opens the nearest door (DO_Animate).
        int bestDoor = -1;
        float bestD = 3.0f;
        for (std::size_t d = 0; d < level.dynamicMeshes.size(); ++d) {
            const Vec3 dw = entityWorld(level, level.dynamicMeshes[d].properties);
            const float dd = horizDist(w, dw);
            if (dd < bestD) {
                bestD = dd;
                bestDoor = static_cast<int>(d);
            }
        }
        if (bestDoor >= 0 && static_cast<std::size_t>(bestDoor) < doors.size()) {
            if (doors[static_cast<std::size_t>(bestDoor)].dir == 0) {
                doors[static_cast<std::size_t>(bestDoor)].dir =
                    doors[static_cast<std::size_t>(bestDoor)].open ? -1 : 1;
                pushLog("DO_Animate");
            }
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
    }
    if (worldHit.hit && worldHit.t < t) {
        t = worldHit.t;
        h.hit = true;
        h.actorIndex = -1;
        h.point = worldHit.point;
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
    if (player.clip <= 0) {
        if (player.ammo > 0) {
            GameMessage r;
            r.method = "c_reload";
            dispatch(r, level, actors, -1);
            player.shootCooldown = 0.6f;
            pushLog("C_Reload");
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
    player.clip -= 1;
    player.shootCooldown = 1.0f / hz;
    HitscanHit hit = fireHitscan(level, world, actors, origin, dir);
    if (hit.actorIndex >= 0 && static_cast<std::size_t>(hit.actorIndex) < actors.size()) {
        CharacterActor& a = actors[static_cast<std::size_t>(hit.actorIndex)];
        a.applyDamage(dmg);
        pushLog(std::string("hitscan ") + a.skinName);
    } else if (hit.hit) {
        pushLog("hitscan world");
    }
    if (hit.triggerIndex >= 0 && static_cast<std::size_t>(hit.triggerIndex) < triggers.size()) {
        triggers[static_cast<std::size_t>(hit.triggerIndex)].cool = 0.0f;
        triggers[static_cast<std::size_t>(hit.triggerIndex)].inside = true;
        // Reuse collide path by faking a player collide on that trigger.
        Trigger dummy = level.triggers[static_cast<std::size_t>(hit.triggerIndex)];
        (void)dummy;
        GameMessage act;
        act.method = "t_activate";
        dispatch(act, level, actors, hit.triggerIndex);
    }
    return true;
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
