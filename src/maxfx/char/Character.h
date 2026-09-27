// Character definition + runtime AI.
//
// Skin scripts (`skins/*.txt`) plus `skeletons/default_skeleton.txt` are the
// official source for capsule, [AI] radii/speeds, and CHARANIM_* clips.
// `X_AIStateMachineStack` exists in the Android decompile as a named FSM
// stack (Idle / combat / hunt / ...) with stripped bodies; this module
// implements that activity set from the documented script fields rather than
// inventing viewer-only behaviour.
#ifndef MAXFX_CHAR_CHARACTER_H
#define MAXFX_CHAR_CHARACTER_H

#include "maxfx/core/Math.h"
#include "maxfx/script/Script.h"

#include <map>
#include <string>
#include <vector>

namespace maxfx {

class CollisionWorld;

// Indices match docs/database/characteranimid.h (CHARANIM_*).
enum CharacterAnimIndex {
    kCharAnimPose = 0,
    kCharAnimWalk = 1,
    kCharAnimRun = 2,
    kCharAnimWalkBackward = 6,
    kCharAnimTurnLeft = 10,
    kCharAnimTurnRight = 11,
    kCharAnimStand = 12,
    kCharAnimCrouch = 13,
    kCharAnimCustomIdle1 = 14,
    kCharAnimGetDamage = 47,
    kCharAnimRandomDeath1 = 49,
    kCharAnimWPose = 70,
    kCharAnimWStand = 82,
    kCharAnimShootBeretta = 313
};

// One scripted clip frame hook: [Animation] [Properties]
// [Message] Frame = N; String = "...". Cinematic clips drive the whole
// cutscene through these (CAM_Animate*, fades, FSM_Send at the last frame).
struct ClipFrameMessage {
    int frame;
    std::string text;

    ClipFrameMessage() : frame(0) {}
};

struct CharacterAnimClip {
    int index;
    std::string filename;
    std::string resolvedPath;
    Vec3 endPosition;
    Vec3 endRotation;
    // [Movement] Filename: a separate root-motion KF2 ("*_mov.kf2") sampled
    // alongside the clip; cinematic clips move the character with it.
    std::string movementFile;
    std::string resolvedMovement;
    std::vector<ClipFrameMessage> frameMessages;

    CharacterAnimClip() : index(-1) {}
};

struct CharacterAiConfig {
    float aimingSpeed;
    float aimingSpeedCone;
    float aimUpDownSpeed;
    float turnLeftRightSpeed;
    float visualPerceivingRadius;
    float generalPerceivingRadius;
    float activationReactionTime;
    float shootingCone;
    float perceivingGroupOneRadius;
    float perceivingGroupTwoRadius;
    float perceivingGroupThreeRadius;
    float enemyInterestTime;
    float activateOtherCharactersRadius;
    float shootingFrequencyMultiplier;

    CharacterAiConfig()
        : aimingSpeed(20.0f),
          aimingSpeedCone(20.0f),
          aimUpDownSpeed(30.0f),
          turnLeftRightSpeed(520.0f),
          visualPerceivingRadius(50.0f),
          generalPerceivingRadius(2.0f),
          activationReactionTime(0.0f),
          shootingCone(120.0f),
          perceivingGroupOneRadius(25.0f),
          perceivingGroupTwoRadius(4.0f),
          perceivingGroupThreeRadius(0.0f),
          enemyInterestTime(30.0f),
          activateOtherCharactersRadius(0.0f),
          shootingFrequencyMultiplier(0.8f) {}
};

struct CharacterCapsule {
    float top;
    float bottom;
    float radius;

    CharacterCapsule() : top(1.95f), bottom(0.0f), radius(0.31f) {}
};

struct CharacterConfig {
    CharacterAiConfig ai;
    CharacterCapsule capsule;
    float maxHealth;
    float airborneSpeed;
    std::string skeleton;
    std::vector<CharacterAnimClip> animations;
    std::map<int, int> reverts;
    std::vector<std::string> onInitMessages;

    CharacterConfig() : maxHealth(15.0f), airborneSpeed(1.5f) {}
};

void fillCharacterConfig(CharacterConfig& cfg, const ScriptBlock& root,
                         const std::string& scriptPath, const std::string& dbRoot);
void mergeSkeletonConfig(CharacterConfig& cfg, const ScriptBlock& skeletonRoot,
                         const std::string& skeletonPath, const std::string& dbRoot);

const CharacterAnimClip* findAnimClip(const CharacterConfig& cfg, int index);
int pickAnimIndex(const CharacterConfig& cfg, int preferred);
// CHARANIM_SHOOT<weapon> (characteranimid.h 310..326): the clip a character
// plays with every trigger pull (RightHandWeapon->WS_AnimateShooting).
int weaponShootAnimIndex(const std::string& weaponName);

enum CharacterActivity {
    kCharIdle = 0,
    kCharAlert,
    kCharHunt,
    kCharCombat,
    kCharPatrol,
    kCharPain,
    kCharDead
};

inline const char* characterActivityName(CharacterActivity a) {
    switch (a) {
        case kCharIdle:
            return "idle";
        case kCharAlert:
            return "alert";
        case kCharHunt:
            return "hunt";
        case kCharCombat:
            return "combat";
        case kCharPatrol:
            return "patrol";
        case kCharPain:
            return "pain";
        case kCharDead:
            return "dead";
        default:
            return "idle";
    }
}

struct AiGraph;

// Per-frame AI inputs (X_Character AI update): what the enemies know about
// the player right now. The host fills this in; actors without it keep the
// legacy idle/patrol behaviour.
struct AiUpdateContext {
    const AiGraph* graph;         // level movement network (may be null)
    bool playerAlive;
    bool playerShotRecently;      // PerceivingGroupOne: player is shooting
    float playerSpeed;            // m/s; running alerts GroupTwo AI
    bool forceActive;             // debug: wake nonreactive enemies
    Vec3 playerPosition;

    AiUpdateContext()
        : graph(0), playerAlive(true), playerShotRecently(false), playerSpeed(0.0f),
          forceActive(false) {}
};

// One enemy shot: the runtime resolves the weapon (sound, muzzle flash,
// projectile, damage) from the actor's inventory.
struct AiFireEvent {
    int actorIndex;
    Vec3 muzzle;  // LDB space
    Vec3 dir;     // normalized, already spread-jittered

    AiFireEvent() : actorIndex(-1) {}
};

// One spawned LDB character. Position / yaw are in MAX-FX (LDB) space.
struct CharacterActor {
    std::string skinName;
    int roomId;
    Vec3 position;
    float yaw;
    float health;
    CharacterActivity activity;
    int animIndex;
    float animTime;
    float interest;
    float idleTimer;
    Vec3 lastSeen;
    bool sawPlayer;
    bool grounded;
    float clipLock;
    // Cross-fade (engine crossAnimateObject): the previous clip keeps
    // sampling for a short blend after a switch instead of hard-popping.
    int prevAnimIndex;
    float blendTime;
    float prevAnimTime;  // the outgoing clip keeps its own clock while blending
    // Locomotion speed in m/s. The engine moves a character along the
    // movement spline (origin -> [Movement] EndPosition) over the clip
    // duration (X_CRSplineMovementUpdate), so speed = |EndPosition| /
    // clipLength. The app fills this once the walk clip has been loaded;
    // 0 = not resolved yet, < 0 = unresolvable (missing clip); both fall
    // back to the script-only estimate in clipWalkSpeed().
    float moveSpeed;
    const CharacterConfig* config;
    // --- LDB character state (startup messages) ---
    std::string entityName;      // sharedName, e.g. "::a5::e1"
    std::string weaponName;      // C_RemoveAllWeapons(keep) / C_PickupWeapon
    bool aiNonReactive;          // c_setstatemachine(nonreactive)
    bool immortal;               // C_SetImmortal / C_SetInvulnerable
    int idleAnimIndex;           // C_SetIdle(n, true)
    // --- combat AI state ---
    bool aiActive;               // activated (perceived the player once)
    float reactionTimer;         // ActivationReactionTime countdown
    float fireCooldown;          // seconds until the next shot
    // Weapon timing resolved from the equipped WeaponDef by the runtime:
    // fireInterval <= 0 means unarmed (never fires), spread is the aim
    // jitter cone in degrees (WeaponDef::spread scaled like the engine's
    // accuracy calculation).
    float fireInterval;
    float fireSpreadDeg;
    // > 0 while the weapon's shoot clip (CHARANIM_SHOOT<weapon>) plays.
    float shootAnimTimer;
    // Pathfinding over the level .ai network (hunt/chase).
    std::vector<Vec3> path;
    std::size_t pathCursor;
    float repathTimer;
    Vec3 pathTarget;
    // Scripted walk (C_GoTo / C_GoToAndShoot waypoint target).
    Vec3 scriptGoal;
    bool hasScriptGoal;
    float scriptSpeed;  // speed factor from the message (0.5 = half speed)

    CharacterActor()
        : roomId(-1),
          yaw(0.0f),
          health(15.0f),
          activity(kCharIdle),
          animIndex(kCharAnimStand),
          animTime(0.0f),
          interest(0.0f),
          idleTimer(0.0f),
          sawPlayer(false),
          grounded(false),
          clipLock(0.0f),
          prevAnimIndex(-1),
          blendTime(0.0f),
          prevAnimTime(0.0f),
          moveSpeed(0.0f),
          config(0),
          aiNonReactive(false),
          immortal(false),
          idleAnimIndex(-1),
          aiActive(false),
          reactionTimer(0.0f),
          fireCooldown(0.0f),
          fireInterval(-1.0f),
          fireSpreadDeg(2.0f),
          shootAnimTimer(0.0f),
          pathCursor(0),
          repathTimer(0.0f),
          hasScriptGoal(false),
          scriptSpeed(1.0f) {}

    void spawn(const Vec3& pos, float yawRadians, int room, const CharacterConfig* cfg,
               const std::string& skin, const std::string& entityName = std::string());
    void applyDamage(float amount);
    // Legacy idle/patrol update (no AI inputs). With `ai` the full combat
    // behaviour runs: perception, reaction, aiming-cone turning, weapon fire
    // (events appended to `fires`, resolved by GameRuntime::enemyFire) and
    // .ai-network pathfinding for the chase.
    void update(float dt, const Vec3& playerPos, CollisionWorld& world,
                std::vector<CharacterActor>* others, const AiUpdateContext* ai = 0,
                std::vector<AiFireEvent>* fires = 0, int selfIndex = -1);
    Mat4x3 entityTransform() const;
    float capsuleRadius() const;
    float capsuleCenterHeight() const;
};

}  // namespace maxfx

#endif  // MAXFX_CHAR_CHARACTER_H
