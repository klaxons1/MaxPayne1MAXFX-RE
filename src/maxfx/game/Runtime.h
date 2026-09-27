// Playable MAX-FX runtime: menu, player capsule, triggers, doors, hitscan.
//
// Message names and trigger kinds come from the PC `MP.exe` decompile
// (C_Jump / C_Use / DO_Animate / T_Activate / GM_Init / MPGNM_PickUpNote).
// Speeds and damage come from skins/max_payne.txt, shooting.h and the
// parsed weapon / projectile scripts — not viewer-only numbers.
#ifndef MAXFX_GAME_RUNTIME_H
#define MAXFX_GAME_RUNTIME_H

#include "maxfx/ai/AiGraph.h"
#include "maxfx/char/Character.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/core/Math.h"
#include "maxfx/game/CameraPaths.h"
#include "maxfx/game/Catalog.h"
#include "maxfx/game/Effects.h"
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
    kMenuComic,
    kMenuComicPages
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
    // Player inventory: one slot per carried weapon (X_CharacterWeapons).
    struct WeaponSlot {
        std::string name;
        int clip;
        int ammo;  // rounds left in the pocket
        WeaponSlot() : clip(0), ammo(0) {}
    };
    std::vector<WeaponSlot> slots;
    int selectedSlot;

    PlayerState();
};

// X_LevelRuntimeTrigger runtime state (offsets from the Android decompile):
//   +1612 `enabled`   — T_Enable(bool) sets it and re-arms the latch
//   +1613 `activated` — latch set by activate()/T_Activate; a trigger fires
//                       exactly once per arm (per level load)
struct TriggerState {
    bool inside;     // player capsule overlap, for enter-edge detection
    bool enabled;
    bool activated;

    TriggerState() : inside(false), enabled(true), activated(false) {}
};

// X_LevelRuntimeFSM current state ("" = default state of the shared DB FSM).
struct FsmRuntime {
    std::string state;
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
    Vec3 normal;  // surface normal at the hit (world hits)
    int actorIndex;
    int triggerIndex;
    int doorIndex;

    HitscanHit() : hit(false), actorIndex(-1), triggerIndex(-1), doorIndex(-1) {}
};

// One queued audio playback (A_Play3DSound / A_PlaySound / A_PlayMusic,
// weapon fire, ...). The viewer drains the queue every frame; the runtime
// itself never touches an audio device.
struct SoundRequest {
    std::string category;  // sounds/ subfolder stem ("" = search all)
    std::string name;
    Vec3 position;    // world position for 3D
    bool is3d;
    bool music;       // A_PlayMusic: (re)start a music track
    bool stopMusic;

    SoundRequest() : is3d(false), music(false), stopMusic(false) {}
};

// In-engine cutscene state: a scripted clip acting the player while a camera
// path flies the view (CAM_AnimateAbsolute and friends). The host application
// samples the KF2 transforms for presentation; the runtime owns timing,
// frame-message dispatch and the [Exit] messages of the path.
struct CinematicState {
    bool active;
    int clipIndex;        // CHARANIM_* index playing on the player
    float time;           // seconds into the clip
    float duration;       // clip length in seconds (0 = unknown / hold)
    int frameRate;        // clip FPS for frame-message edges
    int lastFrame;        // last dispatched frame number
    std::vector<ClipFrameMessage> messages;  // frame hooks of the clip
    std::string movementFile;   // optional root-motion KF2 ("" = in place)
    // Camera path (CAM_Animate*).
    bool cameraActive;
    std::string cameraPath;
    float cameraTime;
    float cameraDuration;  // resolved by the host from the path KF2 (0 = ask)
    int cameraMode;        // 1 absolute, 2 in place, 3 parented, 4 player parented
    std::string cameraRoom;
    std::vector<std::string> cameraExitMessages;
    // Presentation flags driven by messages.
    bool widescreen;   // GM_EnableWideScreen
    bool hudVisible;   // MPHM_EnableHUD
    bool cinematicMode;  // C_EnableCinematicMode (player is script-driven)
    // MPHM_FadeToColor(from, to, time).
    float fadeFrom[4];
    float fadeTo[4];
    float fadeDuration;
    float fadeElapsed;

    CinematicState()
        : active(false),
          clipIndex(-1),
          time(0.0f),
          duration(0.0f),
          frameRate(30),
          lastFrame(-1),
          cameraActive(false),
          cameraTime(0.0f),
          cameraDuration(0.0f),
          cameraMode(0),
          widescreen(false),
          hudVisible(true),
          cinematicMode(false),
          fadeDuration(0.0f),
          fadeElapsed(0.0f) {
        for (int i = 0; i < 4; ++i) {
            fadeFrom[i] = 0.0f;
            fadeTo[i] = 0.0f;
        }
    }

    // Current fade colour (rgba 0..1); alpha 0 when no fade is running.
    void fadeColor(float* rgba) const;
    bool fading() const { return fadeDuration > 0.0f && fadeElapsed < fadeDuration; }
};

struct GameRuntime {
    GameMode mode;
    MenuScreen menu;
    int menuCursor;
    int menuScroll;
    PlayerState player;
    GameCatalog catalog;
    std::vector<TriggerState> triggers;
    std::vector<FsmRuntime> fsmStates;  // parallel to level FSMs
    std::vector<DoorState> doors;
    std::vector<char> itemTaken;
    int comicIndex;
    int comicChapter;
    bool comicFromMenu;
    // MPGNM_PickUpNote pages not shown yet (the graphic-novel reader pops
    // them one per leave, like the engine's picked-notes list).
    std::vector<int> pendingNotes;
    std::string prompt;
    std::string lastEvent;
    std::vector<std::string> log;
    // Audio / effects produced this frame; drained by the host application.
    std::vector<SoundRequest> pendingSounds;
    GameEffects effects;
    // Position 3D sounds are attached to while a message list runs (the
    // sending object, like X_AudioMessage_Play3DSound does).
    Vec3 soundOrigin;
    // Surface normal at the impact being dispatched (D_CreateDecal projects
    // the decal on this plane; PS_StartEffect orients the burst along it).
    Vec3 impactNormal;
    // decals.txt materials (filled by the viewer from Database::decals).
    std::vector<DecalMaterialInfo> decalMaterials;
    // In-engine cutscene state + the camera-path catalog.
    CinematicState cine;
    CameraPathCatalog cameraPaths;
    // Global game speed (GM_ChangeGameSpeed / GM_EnableBulletTime): the
    // simulation advances on scaled time; cinematic scripts drive it to
    // 0.01..0.3 for slow motion and back to 1.0.
    float gameSpeed;
    float gameSpeedTarget;
    float gameSpeedStart;    // ramp origin (GM_ChangeGameSpeed lerp)
    float gameSpeedElapsed;  // ramp progress in real seconds
    float gameSpeedSeconds;  // transition time to the target, 0 = snap
    bool bulletTime;
    // Level movement network (.ai) + player-noise window for AI perception.
    AiGraph aiGraph;
    float playerShotTimer;  // > 0 while the player's shots still alert AI
    // Per-character latches (parallel to the actor array): onActivate runs
    // once when the character's AI activates; the trigger activator names
    // the character that touched a type-3 trigger this dispatch.
    std::vector<char> charActivated;
    int currentActivatorActor;

    GameRuntime();

    void loadCatalog(const std::string& dbRoot);
    // Also loads data/database/camerapaths/camerapaths.txt.
    void loadCameraPaths(const std::string& dbRoot);
    void resetLevel(const Level& level, const LevelInfo* info, const Vec3& spawnLdb, float spawnYaw);
    void applyPlayerOnInit(const CharacterConfig* playerCfg);

    void menuMove(int delta, int levelCount);
    bool menuChoose(int* jumpLevel, int* openPage, bool* quit, bool* newGame);

    void setLook(float yawView, float pitch);
    void tickPlayer(float dt, bool forward, bool back, bool left, bool right, bool jump, bool sprint,
                    CollisionWorld& world, bool descend = false);
    // Move gameSpeed toward gameSpeedTarget (GM_ChangeGameSpeed ramp) on
    // REAL time, not scaled time.
    void tickGameSpeed(float realDt);
    void tickDoors(float dt, const Level& level);

    // X_LevelRuntimeDynamicObject pose: the MeshAnimation interpolates the
    // mesh transform from startTransform to endTransform over its length
    // (translation / rotation graphs are easing curves when present).
    Mat4x3 dynamicMeshPose(const MeshAnimation& anim, float t) const;
    void tickTriggers(float dt, const Level& level, bool usePressed, const Vec3& lookDir,
                      std::vector<CharacterActor>& actors);

    // X_LevelRuntimeFSM: every level FSM runs its startup messages at level
    // load (this is what plays the level intro comic via ::startroom::fsm_start).
    void startLevel(const Level& level, std::vector<CharacterActor>& actors);

    // --- Weapons (X_CharacterWeapons / MaxPayne_PlayerInformation) ---
    // Cheat-style arsenal: every catalog weapon, ordered by
    // weaponpriority.txt [CycleWeapons] (falls back to slot order).
    void giveAllWeapons();
    // Slot selection: 1..9 / 0 keys or mouse wheel. Returns false when the
    // slot does not exist.
    bool selectSlot(int index);
    bool cycleWeapon(int delta);
    bool reloadWeapon();  // C_Reload: pocket -> clip
    const WeaponDef* currentWeaponDef() const;
    int findSlotOfWeapon(const std::string& name) const;

    // X_LevelRuntimeTrigger::activate — latch + T_Activate to the trigger's
    // own FSM (the level FSM named like the trigger without ".TRIGGER").
    bool activateTrigger(int index, const Level& level, std::vector<CharacterActor>& actors);

    // Next un-shown MPGNM_PickUpNote page, or -1 (graphic-novel reader).
    int nextPendingNote();

    HitscanHit fireHitscan(const Level& level, CollisionWorld& world, std::vector<CharacterActor>& actors,
                           const Vec3& origin, const Vec3& dir);

    // X_Projectile impact: run the projectile script's message list at the
    // hit point (D_CreateDecal / PS_StartEffect / A_Play3DSound ...).
    void runImpactMessages(const ProjectileDef& pr, const Vec3& position, const Vec3& normal,
                           const Level& level, std::vector<CharacterActor>& actors);

    // --- Cinematics (in-engine cutscenes) ---
    // Start playing a scripted clip on the player. `messages` are the clip's
    // [Message] Frame hooks; the host fills `duration` / `frameRate` from the
    // clip KF2 and may patch cine.duration later once the KF2 is loaded.
    void startCinematic(int clipIndex, float duration, int frameRate,
                        const std::vector<ClipFrameMessage>& messages,
                        const std::string& movementFile);
    void stopCinematic();
    // Advance the cinematic: frame-message edges, fades, camera-path timing
    // and its [Exit] messages. Call once per frame while cine.active.
    void tickCinematic(float dt, const Level& level, std::vector<CharacterActor>& actors);
    // CAM_AnimateAbsolute / InPlace / Parented / PlayerParented.
    void startCameraPath(const std::string& name, int mode, const std::string& room);
    // Full abort (Esc): stop the clip AND the camera path, clear the fade /
    // letterbox / HUD state so nothing of the cutscene survives.
    void abortCinematic();
    // X_CameraImplementation::userAbortCameraPathIfAbortable: moving cancels
    // an Abortable camera path (its [Exit] messages still run).
    bool cameraPathAbortable() const;
    void abortCameraPath(const Level& level, std::vector<CharacterActor>& actors);

    bool tryShoot(const Level& level, CollisionWorld& world, std::vector<CharacterActor>& actors,
                  const Vec3& origin, const Vec3& dir);

    // --- enemy combat (X_Character AI) ---
    // Load <level>.ai next to the LDB; missing/incompatible files simply
    // leave the graph empty (the engine plays on without a network too).
    void loadLevelAi(const std::string& aiPath, const Level& level);
    // Register the player's shot for PerceivingGroupOne (heard through
    // walls at the skin's GroupOne radius for a short window).
    void notePlayerShot();
    bool playerShotRecently() const { return playerShotTimer > 0.0f; }
    // LDB character startup messages (c_kill, c_setstatemachine,
    // C_RemoveAllWeapons, C_PickupWeapon, C_SetHealth, C_SetIdle, ...).
    void applyCharacterStartup(CharacterActor& actor, const FsmMessages& startup);
    // Skin [OnInit] message list (C_PickupWeapon from the skin) or any other
    // raw message list applied to a character.
    void applyCharacterMessages(CharacterActor& actor, const std::vector<std::string>& lines);
    // X_Characteristic activation: the onActivate list runs once when the
    // character's AI activates (it perceives the player / a script switches
    // it to a combat state machine).
    void activateCharacter(int actorIndex, const Level& level,
                           std::vector<CharacterActor>& actors);
    // C_SendSpecial: run the character's onSpecial list.
    void characterSendSpecial(int actorIndex, const Level& level,
                              std::vector<CharacterActor>& actors);
    // Waypoint entity position in LDB space (C_Teleport / C_GoTo targets).
    Vec3 waypointWorld(const Level& level, const std::string& name) const;
    // Route one message to a character entity; false = not a character
    // method (global handlers take it).
    bool handleCharacterMessage(const GameMessage& msg, int actorIndex, const Level& level,
                                std::vector<CharacterActor>& actors);
    // Equip an NPC (fire interval / spread from the WeaponDef).
    void setCharacterWeapon(CharacterActor& actor, const std::string& weaponNameIn);
    // Resolve one enemy shot: weapon sound + muzzle flash, hitscan against
    // the player capsule, other characters and the world.
    void enemyFire(const AiFireEvent& ev, const Level& level, CollisionWorld& world,
                   std::vector<CharacterActor>& actors);
    void damagePlayer(float amount);

    void dispatch(const GameMessage& msg, const Level& level, std::vector<CharacterActor>& actors,
                  int sourceTrigger);
    void dispatchList(const std::vector<std::string>& lines, const Level& level,
                      std::vector<CharacterActor>& actors, int sourceTrigger);
    // Entity event by name, state-filtered (before/current-state/after).
    void dispatchFsmEvent(int fsmIndex, const std::string& eventName, const Level& level,
                          std::vector<CharacterActor>& actors, int sourceTrigger);

    Vec3 viewPosition() const;
    float viewYaw() const;

    void pushLog(const std::string& line);
};

// A_Play3DSound / A_PlaySound / A_PlayFloating3DSound from FSM startup
// (X_LevelRuntimeFSM receive X_AudioMessage_Play3DSound in MP.exe).
struct SoundCueRequest {
    std::string category;
    std::string name;
    Vec3 origin;
    bool is3d;
    bool floating;

    SoundCueRequest() : is3d(true), floating(false) {}
};

void collectLevelSoundCues(const Level& level, std::vector<SoundCueRequest>* out);

// LDB feet from a viewer-space camera that has already applied mirrorX.
Vec3 ldbFromView(const Vec3& viewPos, float eyeHeight);
Vec3 viewFromLdb(const Vec3& ldbFeet, float eyeHeight);

}  // namespace maxfx

#endif  // MAXFX_GAME_RUNTIME_H
