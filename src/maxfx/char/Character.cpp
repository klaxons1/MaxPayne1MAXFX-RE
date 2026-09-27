#include "maxfx/char/Character.h"

#include "maxfx/script/Script.h"

#include "maxfx/collision/Collision.h"
#include "maxfx/core/Fs.h"

#include <cmath>
#include <cstdlib>

namespace maxfx {
namespace {

std::string assignmentOf(const ScriptBlock& block, const char* lvalue) {
    const std::string key = lowerCopy(lvalue);
    for (std::size_t i = 0; i < block.assignments.size(); ++i) {
        if (block.assignments[i].lvalue == key) {
            return block.assignments[i].rvalue;
        }
    }
    return std::string();
}

float parseFloatCatch(const std::string& v, float fallback) {
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptFloat(v, "", 0, "");
    } catch (...) {
        return fallback;
    }
}

int parseIntCatch(const std::string& v, int fallback) {
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptInt(v, "", 0, "");
    } catch (...) {
        return fallback;
    }
}

float floatField(const ScriptBlock& block, const char* name, float fallback) {
    return parseFloatCatch(assignmentOf(block, name), fallback);
}

int intField(const ScriptBlock& block, const char* name, int fallback) {
    return parseIntCatch(assignmentOf(block, name), fallback);
}

Vec3 vecField(const ScriptBlock& block, const char* name, const Vec3& fallback) {
    const std::string v = assignmentOf(block, name);
    if (v.empty()) {
        return fallback;
    }
    float out[3] = {fallback.x, fallback.y, fallback.z};
    try {
        parseScriptFloat3(v, out, name, block.line, "");
        return Vec3(out[0], out[1], out[2]);
    } catch (...) {
        return fallback;
    }
}

std::string nativeFromScript(const std::string& p) {
    std::string s = p;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            s[i] = '/';
        }
    }
    return nativeSeparators(s);
}

std::string firstExisting(const std::vector<std::string>& cands) {
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const std::string hit = existingPathIgnoreCase(cands[i]);
        if (!hit.empty() && isFile(hit)) {
            return hit;
        }
    }
    return cands.empty() ? std::string() : cands[0];
}

std::string resolveClipPath(const std::string& filename, const std::string& scriptPath,
                            const std::string& skeleton, const std::string& dbRoot) {
    if (filename.empty()) {
        return std::string();
    }
    const std::string rel = nativeFromScript(filename);
    const std::string scriptDir = parentDir(scriptPath);
    const std::string stem = fileStem(scriptPath);
    std::vector<std::string> cands;
    cands.push_back(joinPath(scriptDir, rel));
    cands.push_back(joinPath(joinPath(scriptDir, stem), rel));
    if (!skeleton.empty() && !dbRoot.empty()) {
        const std::string skelDir = joinPath(joinPath(dbRoot, "skeletons"), skeleton);
        cands.push_back(joinPath(skelDir, rel));
        cands.push_back(joinPath(joinPath(skelDir, stem), rel));
    }
    if (!dbRoot.empty()) {
        cands.push_back(joinPath(joinPath(dbRoot, "skeletons"), rel));
        cands.push_back(joinPath(dbRoot, rel));
    }
    return firstExisting(cands);
}

const ScriptBlock* findChild(const ScriptBlock& block, const char* name) {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        if (block.children[i].name == key) {
            return &block.children[i];
        }
    }
    return 0;
}

void collectFrameMessages(const ScriptBlock& block, std::vector<ClipFrameMessage>* out) {
    if (block.name == "message") {
        ClipFrameMessage m;
        m.frame = intField(block, "frame", 0);
        m.text = parseScriptString(assignmentOf(block, "string"));
        if (!m.text.empty()) {
            out->push_back(m);
        }
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectFrameMessages(block.children[i], out);
    }
}

void collectClips(const ScriptBlock& block, const std::string& scriptPath, const std::string& skeleton,
                  const std::string& dbRoot, std::vector<CharacterAnimClip>* out) {
    if (block.name == "animation") {
        CharacterAnimClip clip;
        clip.index = intField(block, "index", -1);
        clip.filename = assignmentOf(block, "filename");
        clip.resolvedPath = resolveClipPath(clip.filename, scriptPath, skeleton, dbRoot);
        const ScriptBlock* props = findChild(block, "properties");
        if (props != 0) {
            const ScriptBlock* mov = findChild(*props, "movement");
            if (mov != 0) {
                clip.endPosition = vecField(*mov, "endposition", Vec3());
                clip.endRotation = vecField(*mov, "rotation", Vec3());
                const std::string mf = parseScriptString(assignmentOf(*mov, "filename"));
                if (!mf.empty()) {
                    clip.movementFile = mf;
                    clip.resolvedMovement = resolveClipPath(mf, scriptPath, skeleton, dbRoot);
                }
            }
        }
        // [Message] Frame hooks anywhere under the clip (authored inside
        // [Properties]) — cinematic scripts are made of these.
        collectFrameMessages(block, &clip.frameMessages);
        if (clip.index >= 0 && !clip.filename.empty()) {
            out->push_back(clip);
        }
    }
    if (block.name == "revert") {
        return;
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectClips(block.children[i], scriptPath, skeleton, dbRoot, out);
    }
}

void collectReverts(const ScriptBlock& block, std::map<int, int>* out) {
    if (block.name == "revert") {
        const int from = intField(block, "from", -1);
        const int to = intField(block, "to", -1);
        if (from >= 0 && to >= 0) {
            (*out)[from] = to;
        }
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectReverts(block.children[i], out);
    }
}

bool hasClipIndex(const std::vector<CharacterAnimClip>& clips, int index) {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].index == index) {
            return true;
        }
    }
    return false;
}

void applyAiBlock(CharacterAiConfig& ai, const ScriptBlock& block) {
    ai.aimingSpeed = floatField(block, "aimingspeed", ai.aimingSpeed);
    ai.aimingSpeedCone = floatField(block, "aimingspeedcone", ai.aimingSpeedCone);
    ai.aimUpDownSpeed = floatField(block, "aimupdownspeed", ai.aimUpDownSpeed);
    ai.turnLeftRightSpeed = floatField(block, "turnleftrightspeed", ai.turnLeftRightSpeed);
    ai.visualPerceivingRadius = floatField(block, "visualperceivingradius", ai.visualPerceivingRadius);
    ai.generalPerceivingRadius =
        floatField(block, "generalperceivingradius", ai.generalPerceivingRadius);
    ai.activationReactionTime =
        floatField(block, "activationreactiontime", ai.activationReactionTime);
    ai.shootingCone = floatField(block, "shootingcone", ai.shootingCone);
    ai.perceivingGroupOneRadius =
        floatField(block, "perceivinggrouponeradius", ai.perceivingGroupOneRadius);
    ai.perceivingGroupTwoRadius =
        floatField(block, "perceivinggrouptworadius", ai.perceivingGroupTwoRadius);
    ai.perceivingGroupThreeRadius =
        floatField(block, "perceivinggroupthreeradius", ai.perceivingGroupThreeRadius);
    ai.enemyInterestTime = floatField(block, "enemyinteresttime", ai.enemyInterestTime);
    ai.activateOtherCharactersRadius =
        floatField(block, "activateothercharactersradius", ai.activateOtherCharactersRadius);
}

void applyProperties(CharacterConfig& cfg, const ScriptBlock& block) {
    const std::string sk = assignmentOf(block, "skeleton");
    if (!sk.empty()) {
        cfg.skeleton = sk;
    }
    cfg.capsule.top = floatField(block, "capsuletop", cfg.capsule.top);
    cfg.capsule.bottom = floatField(block, "capsulebottom", cfg.capsule.bottom);
    cfg.capsule.radius = floatField(block, "capsuleradius", cfg.capsule.radius);
    cfg.maxHealth = floatField(block, "maximumhealth", cfg.maxHealth);
    cfg.airborneSpeed = floatField(block, "airbornespeed", cfg.airborneSpeed);
}

// Script-only estimate used until the app has loaded the walk clip KF2:
// |EndPosition| assumes a 1-second clip. The engine's real speed is
// |EndPosition| / clipLength (see CharacterActor::moveSpeed).
float clipWalkSpeed(const CharacterConfig& cfg) {
    const CharacterAnimClip* walk = findAnimClip(cfg, kCharAnimWalk);
    if (walk != 0 && std::fabs(walk->endPosition.z) > 0.01f) {
        return std::fabs(walk->endPosition.z);
    }
    const CharacterAnimClip* run = findAnimClip(cfg, kCharAnimRun);
    if (run != 0 && std::fabs(run->endPosition.z) > 0.01f) {
        return std::fabs(run->endPosition.z);
    }
    return 1.5f;
}

void turnToward(float* yaw, float desired, float speedDeg, float dt) {
    const float diff = wrapAngle(desired - *yaw);
    const float maxTurn = toRadians(speedDeg) * dt;
    if (diff > maxTurn) {
        *yaw = wrapAngle(*yaw + maxTurn);
    } else if (diff < -maxTurn) {
        *yaw = wrapAngle(*yaw - maxTurn);
    } else {
        *yaw = desired;
    }
}

int activityAnim(CharacterActivity a, bool armed) {
    switch (a) {
        case kCharHunt:
        case kCharPatrol:
            return kCharAnimWalk;
        case kCharCombat:
            return armed ? kCharAnimWStand : kCharAnimStand;
        case kCharPain:
            return kCharAnimGetDamage;
        case kCharDead:
            return kCharAnimRandomDeath1;
        case kCharAlert:
            return armed ? kCharAnimWStand : kCharAnimStand;
        case kCharIdle:
        default:
            return armed ? kCharAnimWStand : kCharAnimStand;
    }
}

}  // namespace

void fillCharacterConfig(CharacterConfig& cfg, const ScriptBlock& root,
                         const std::string& scriptPath, const std::string& dbRoot) {
    cfg.skeleton = assignmentOf(root, "skeleton");
    for (std::size_t i = 0; i < root.children.size(); ++i) {
        const ScriptBlock& ch = root.children[i];
        if (ch.name == "ai") {
            applyAiBlock(cfg.ai, ch);
        } else if (ch.name == "properties") {
            applyProperties(cfg, ch);
        } else if (ch.name == "oninit") {
            for (std::size_t m = 0; m < ch.children.size(); ++m) {
                if (ch.children[m].name == "message") {
                    const std::string s = assignmentOf(ch.children[m], "string");
                    if (!s.empty()) {
                        cfg.onInitMessages.push_back(s);
                    }
                }
            }
        }
    }
    collectClips(root, scriptPath, cfg.skeleton, dbRoot, &cfg.animations);
    collectReverts(root, &cfg.reverts);
}

void mergeSkeletonConfig(CharacterConfig& cfg, const ScriptBlock& skeletonRoot,
                         const std::string& skeletonPath, const std::string& dbRoot) {
    std::vector<CharacterAnimClip> extra;
    collectClips(skeletonRoot, skeletonPath, cfg.skeleton, dbRoot, &extra);
    for (std::size_t i = 0; i < extra.size(); ++i) {
        if (!hasClipIndex(cfg.animations, extra[i].index)) {
            cfg.animations.push_back(extra[i]);
        }
    }
    collectReverts(skeletonRoot, &cfg.reverts);
}

const CharacterAnimClip* findAnimClip(const CharacterConfig& cfg, int index) {
    for (std::size_t i = 0; i < cfg.animations.size(); ++i) {
        if (cfg.animations[i].index == index) {
            return &cfg.animations[i];
        }
    }
    return 0;
}

int pickAnimIndex(const CharacterConfig& cfg, int preferred) {
    int cur = preferred;
    for (int guard = 0; guard < 8; ++guard) {
        if (findAnimClip(cfg, cur) != 0) {
            return cur;
        }
        std::map<int, int>::const_iterator it = cfg.reverts.find(cur);
        if (it == cfg.reverts.end()) {
            break;
        }
        cur = it->second;
    }
    static const int kFall[] = {kCharAnimStand, kCharAnimWStand, kCharAnimCustomIdle1,
                                kCharAnimPose,  kCharAnimWalk,   kCharAnimWPose};
    for (std::size_t i = 0; i < sizeof(kFall) / sizeof(kFall[0]); ++i) {
        if (findAnimClip(cfg, kFall[i]) != 0) {
            return kFall[i];
        }
    }
    if (!cfg.animations.empty()) {
        return cfg.animations[0].index;
    }
    return preferred;
}

void CharacterActor::applyDamage(float amount) {
    health -= amount;
    if (health <= 0.0f) {
        health = 0.0f;
        activity = kCharDead;
        animIndex = config != 0 ? pickAnimIndex(*config, kCharAnimRandomDeath1) : kCharAnimRandomDeath1;
        clipLock = 2.0f;
    } else {
        activity = kCharPain;
        animIndex = config != 0 ? pickAnimIndex(*config, kCharAnimGetDamage) : kCharAnimGetDamage;
        clipLock = 0.4f;
    }
}

void CharacterActor::spawn(const Vec3& pos, float yawRadians, int room, const CharacterConfig* cfg,
                           const std::string& skin) {
    position = pos;
    yaw = yawRadians;
    roomId = room;
    config = cfg;
    skinName = skin;
    health = cfg != 0 ? cfg->maxHealth : 15.0f;
    activity = kCharIdle;
    animIndex = cfg != 0 ? pickAnimIndex(*cfg, kCharAnimStand) : kCharAnimStand;
    animTime = 0.0f;
    interest = 0.0f;
    idleTimer = 0.0f;
    sawPlayer = false;
    grounded = false;
    clipLock = 0.0f;
    moveSpeed = 0.0f;
    lastSeen = pos;
}

float CharacterActor::capsuleRadius() const {
    return config != 0 ? config->capsule.radius : 0.31f;
}

float CharacterActor::capsuleCenterHeight() const {
    if (config == 0) {
        return 0.95f;
    }
    return 0.5f * (config->capsule.top + config->capsule.bottom);
}

Mat4x3 CharacterActor::entityTransform() const { return makeEntity(position, yaw); }

void CharacterActor::update(float dt, const Vec3& playerPos, CollisionWorld& world,
                            std::vector<CharacterActor>* others) {
    if (config == 0) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (activity == kCharDead) {
        animTime += dt;
        return;
    }

    const CharacterAiConfig& ai = config->ai;
    Vec3 toPlayer = playerPos - position;
    toPlayer.y = 0.0f;
    const float dist = length(toPlayer);
    const Vec3 eye = position + Vec3(0.0f, config->capsule.top * 0.85f, 0.0f);
    const Vec3 playerEye = playerPos + Vec3(0.0f, 1.6f, 0.0f);
    Vec3 los = playerEye - eye;
    const float losLen = length(los);
    bool visible = false;
    if (dist < ai.visualPerceivingRadius && losLen > 0.05f && dist < 35.0f) {
        const CollisionHit hit = world.raycast(eye, los, 1.0f);
        if (!hit.hit || hit.t > losLen * 0.95f) {
            visible = true;
        }
    }
    if (!visible && dist < ai.generalPerceivingRadius) {
        visible = true;
    }

    if (visible) {
        lastSeen = playerPos;
        sawPlayer = true;
        interest = ai.enemyInterestTime;
        if (others != 0 && ai.activateOtherCharactersRadius > 0.01f) {
            for (std::size_t i = 0; i < others->size(); ++i) {
                CharacterActor& o = (*others)[i];
                if (&o == this || o.activity == kCharDead) {
                    continue;
                }
                const float d = length(Vec3(o.position.x - position.x, 0.0f, o.position.z - position.z));
                if (d <= ai.activateOtherCharactersRadius) {
                    o.sawPlayer = true;
                    o.lastSeen = lastSeen;
                    o.interest = ai.enemyInterestTime;
                }
            }
        }
    } else if (interest > 0.0f) {
        interest -= dt;
        if (interest < 0.0f) {
            interest = 0.0f;
            sawPlayer = false;
        }
    }

    Vec3 toTarget = lastSeen - position;
    toTarget.y = 0.0f;
    const float targetDist = length(toTarget);
    const float desiredYaw =
        targetDist > 0.05f ? std::atan2(toTarget.x, toTarget.z) : yaw;
    const float facing = wrapAngle(desiredYaw - yaw);
    const float cone = toRadians(ai.shootingCone * 0.5f);

    if (health <= 0.0f) {
        activity = kCharDead;
    } else {
        // Sticky combat/hunt so facing-cone flicker does not restart clips
        // every few frames (walk/stand popping).
        bool sticky = false;
        if (sawPlayer) {
            if (activity == kCharCombat && targetDist < 14.0f) {
                sticky = true;
            } else if (activity == kCharHunt && targetDist > 0.8f && targetDist < 28.0f) {
                sticky = true;
            }
        }
        if (!sticky) {
            if (sawPlayer && targetDist < 10.0f && std::fabs(facing) < cone) {
                activity = kCharCombat;
            } else if (sawPlayer && targetDist < 4.0f) {
                activity = kCharAlert;
            } else if (sawPlayer && targetDist > 1.2f) {
                activity = kCharHunt;
            } else if (interest > 0.0f) {
                activity = kCharAlert;
            } else {
                idleTimer += dt;
                if (idleTimer > 6.0f) {
                    activity = kCharPatrol;
                    if (idleTimer > 10.0f) {
                        idleTimer = 0.0f;
                    }
                } else {
                    activity = kCharIdle;
                }
            }
        }
    }

    const bool armed = findAnimClip(*config, kCharAnimWStand) != 0;
    const int want = pickAnimIndex(*config, activityAnim(activity, armed));
    if (clipLock > 0.0f) {
        clipLock -= dt;
    }
    if (want != animIndex && clipLock <= 0.0f) {
        prevAnimIndex = animIndex;
        prevAnimTime = animTime;  // outgoing clip continues where it was
        blendTime = 0.25f;        // crossAnimateObject blend window
        animIndex = want;
        animTime = 0.0f;
        clipLock = 0.4f;
    } else {
        animTime += dt;
        if (blendTime > 0.0f) {
            prevAnimTime += dt;
            blendTime -= dt;
            if (blendTime < 0.0f) {
                blendTime = 0.0f;
                prevAnimIndex = -1;
            }
        }
    }

    if (activity == kCharCombat || activity == kCharAlert || activity == kCharHunt) {
        turnToward(&yaw, desiredYaw, ai.turnLeftRightSpeed, dt);
    } else if (activity == kCharPatrol) {
        turnToward(&yaw, yaw + toRadians(40.0f) * dt, ai.turnLeftRightSpeed, dt);
    }

    Vec3 delta(0.0f, 0.0f, 0.0f);
    if (activity == kCharHunt || activity == kCharPatrol) {
        const float speed = moveSpeed > 0.0f ? moveSpeed : clipWalkSpeed(*config);
        const Vec3 fwd = Vec3(std::sin(yaw), 0.0f, std::cos(yaw));
        if (activity == kCharHunt && targetDist > 1.0f) {
            delta = fwd * (speed * dt);
        } else if (activity == kCharPatrol) {
            delta = fwd * (speed * 0.5f * dt);
        }
    }

    // Horizontal slide against room meshes, then snap feet to the floor.
    // Never drop Y when the downward ray misses — missing collision used
    // to send every NPC through the map.
    const float centerY = capsuleCenterHeight();
    const bool moving = length(delta) > 1.0e-5f;
    if (moving || !grounded) {
        Vec3 center = position + Vec3(0.0f, centerY, 0.0f);
        if (moving) {
            center = world.moveSphere(center, delta, capsuleRadius());
        }
        position.x = center.x;
        position.z = center.z;
        const CollisionHit floor =
            world.raycast(center + Vec3(0.0f, 1.2f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 4.0f);
        if (floor.hit) {
            position.y = floor.point.y - config->capsule.bottom;
            grounded = true;
        }
    }

    if (others != 0) {
        for (std::size_t i = 0; i < others->size(); ++i) {
            CharacterActor& o = (*others)[i];
            if (&o == this || o.activity == kCharDead) {
                continue;
            }
            Vec3 d = position - o.position;
            d.y = 0.0f;
            const float sep = length(d);
            const float need = capsuleRadius() + o.capsuleRadius();
            if (sep > 0.001f && sep < need) {
                const Vec3 n = d * (1.0f / sep);
                const float push = 0.5f * (need - sep);
                position += n * push;
                o.position = o.position - n * push;
            }
        }
    }
}

}  // namespace maxfx
