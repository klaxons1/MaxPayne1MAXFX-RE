// Playable MAX-FX runtime: menu, player capsule, triggers, doors, hitscan.
//
// Message names and trigger kinds come from the PC `MP.exe` decompile
// (C_Jump / C_Use / DO_Animate / T_Activate / GM_Init / MPGNM_PickUpNote).
// Speeds and damage come from skins/max_payne.txt, shooting.h and the
// parsed weapon / projectile scripts — not viewer-only numbers.
#ifndef MAXFX_GAME_RUNTIME_H
#define MAXFX_GAME_RUNTIME_H

#include "maxfx/char/Character.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/core/Math.h"
#include "maxfx/game/Catalog.h"
#include "maxfx/game/Message.h"
#include "maxfx/ldb/Ldb.h"
#include "maxfx/levels/Levels.h"

#include <string>
#include <vector>

namespace maxfx {

enum GameMode {
    kModeMenu = 0,
    kModePlaying,
    kModeGraphicNovel
};

enum MenuScreen {
    kMenuRoot = 0,
    kMenuJumpLevel,
    kMenuComic
};

struct PlayerState {
    Vec3 position;  // LDB space, feet
    float yaw;
    float pitch;
    Vec3 velocity;
    bool grounded;
    bool noclip;
    float health;
    float maxHealth;
    float eyeHeight;
    float radius;
    float walkSpeed;
    float airborneSpeed;
    float jumpSpeed;
    float shootCooldown;
    int clip;
    int ammo;
    int clipSize;
    std::string weaponName;
    bool crosshair;
    bool controlsEnabled;

    PlayerState();
};

struct TriggerState {
    bool inside;
    bool used;
    float cool;

    TriggerState() : inside(false), used(false), cool(0.0f) {}
};

struct DoorState {
    float t;
    int dir;  // -1 closing, 0 idle, +1 opening
    bool open;

    DoorState() : t(0.0f), dir(0), open(false) {}
};

struct HitscanHit {
    bool hit;
    Vec3 point;
    int actorIndex;
    int triggerIndex;
    int doorIndex;

    HitscanHit() : hit(false), actorIndex(-1), triggerIndex(-1), doorIndex(-1) {}
};

struct GameRuntime {
    GameMode mode;
    MenuScreen menu;
    int menuCursor;
    int menuScroll;
    PlayerState player;
    GameCatalog catalog;
    std::vector<TriggerState> triggers;
    std::vector<DoorState> doors;
    std::vector<char> itemTaken;
    int comicIndex;
    std::string prompt;
    std::string lastEvent;
    std::vector<std::string> log;

    GameRuntime();

    void loadCatalog(const std::string& dbRoot);
    void resetLevel(const Level& level, const LevelInfo* info, const Vec3& spawnLdb, float spawnYaw);
    void applyPlayerOnInit(const CharacterConfig* playerCfg);

    void menuMove(int delta, int levelCount, int pageCount);
    bool menuChoose(int* jumpLevel, int* openPage, bool* quit, bool* newGame);

    void setLook(float yawView, float pitch);
    void tickPlayer(float dt, bool forward, bool back, bool left, bool right, bool jump, bool sprint,
                    CollisionWorld& world);
    void tickDoors(float dt, const Level& level);
    void tickTriggers(float dt, const Level& level, bool usePressed, const Vec3& lookDir,
                      std::vector<CharacterActor>& actors);

    HitscanHit fireHitscan(const Level& level, CollisionWorld& world, std::vector<CharacterActor>& actors,
                           const Vec3& origin, const Vec3& dir);

    bool tryShoot(const Level& level, CollisionWorld& world, std::vector<CharacterActor>& actors,
                  const Vec3& origin, const Vec3& dir);

    void dispatch(const GameMessage& msg, const Level& level, std::vector<CharacterActor>& actors,
                  int sourceTrigger);
    void dispatchList(const std::vector<std::string>& lines, const Level& level,
                      std::vector<CharacterActor>& actors, int sourceTrigger);

    Vec3 viewPosition() const;
    float viewYaw() const;

    void pushLog(const std::string& line);
};

// LDB feet from a viewer-space camera that has already applied mirrorX.
Vec3 ldbFromView(const Vec3& viewPos, float eyeHeight);
Vec3 viewFromLdb(const Vec3& ldbFeet, float eyeHeight);

}  // namespace maxfx

#endif  // MAXFX_GAME_RUNTIME_H
