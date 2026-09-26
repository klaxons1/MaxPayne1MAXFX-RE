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

struct CharacterAnimClip {
    int index;
    std::string filename;
    std::string resolvedPath;
    Vec3 endPosition;
    Vec3 endRotation;

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
          activateOtherCharactersRadius(0.0f) {}
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
    const CharacterConfig* config;

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
          config(0) {}

    void spawn(const Vec3& pos, float yawRadians, int room, const CharacterConfig* cfg,
               const std::string& skin);
    void applyDamage(float amount);
    void update(float dt, const Vec3& playerPos, CollisionWorld& world,
                std::vector<CharacterActor>* others);
    Mat4x3 entityTransform() const;
    float capsuleRadius() const;
    float capsuleCenterHeight() const;
};

}  // namespace maxfx

#endif  // MAXFX_CHAR_CHARACTER_H
