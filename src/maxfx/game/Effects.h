// Runtime visual effects: bullet decals (D_CreateDecal) and particle effects
// (PS_StartEffect).
//
// Decals follow X_DecalMessage_CreateDecal: a textured quad projected onto the
// hit surface with a radius randomized between the decal material's
// MinimumRadius / MaximumRadius (decals.txt). They persist (FIFO-capped).
//
// Particle effects: the shipped data stores effects as ParticleFX .psc graphs
// (PS_* binary chunks — ids verified in the Android decompile: PS_Element 32/3,
// PS_Emitter 33/5, PS_Particle 48/4, PS_Configuration 96/5; graph decoding is
// future work). This module drives effects with engine-style emitter profiles:
// particles live, move under gravity/drag, and are drawn as camera-facing
// billboards whose color/alpha/size ramp over lifetime. Profiles are picked
// per effect name (muzzle / impact / blood / smoke / explosion ...) and the
// texture comes from the particles.txt material bitmaps when resolvable.
#ifndef MAXFX_GAME_EFFECTS_H
#define MAXFX_GAME_EFFECTS_H

#include "maxfx/core/Fs.h"
#include "maxfx/core/Math.h"

#include <string>
#include <vector>
#include <deque>

namespace maxfx {

// ---------------------------------------------------------------------------
// Decals

// One decals.txt entry (viewer fills this from Database::decals).
struct DecalMaterialInfo {
    std::string name;
    float minRadius;
    float maxRadius;

    DecalMaterialInfo() : minRadius(0.05f), maxRadius(0.15f) {}
};

struct Decal {
    Vec3 position;
    Vec3 normal;    // surface normal, unit length
    float radius;
    float rotation;  // random roll around the normal
    std::string material;
    float age;       // seconds since spawn

    Decal() : radius(0.1f), rotation(0.0f), age(0.0f) {}
};

class DecalSystem {
public:
    DecalSystem();

    void setMaterials(const std::vector<DecalMaterialInfo>& materials);
    const DecalMaterialInfo* findMaterial(const std::string& name) const;

    // D_CreateDecal(name): project onto the surface, random radius in
    // [min, max] of the material, random roll. Older decals roll out.
    void spawn(const std::string& materialName, const Vec3& position, const Vec3& normal);
    void update(float dt);
    void clear();

    const std::vector<Decal>& list() const { return decals_; }
    int dropped() const { return dropped_; }  // spawns rejected by the cap

private:
    std::vector<DecalMaterialInfo> materials_;
    std::vector<Decal> decals_;
    int dropped_;
    static const int kMaxDecals = 256;
};

// ---------------------------------------------------------------------------
// Particles

struct Particle {
    Vec3 position;
    Vec3 velocity;
    float age;
    float lifetime;
    float sizeStart;
    float sizeEnd;
    Vec3 colorStart;
    Vec3 colorEnd;
    float alphaStart;
    float alphaEnd;
    float gravity;
    float drag;

    Particle()
        : age(0.0f),
          lifetime(1.0f),
          sizeStart(0.1f),
          sizeEnd(0.1f),
          colorStart(1, 1, 1),
          colorEnd(1, 1, 1),
          alphaStart(1.0f),
          alphaEnd(0.0f),
          gravity(0.0f),
          drag(0.0f) {}
};

// One emitter of an effect (PS_Emitter equivalent, profile-based).
struct ParticleEmitterProfile {
    float rate;      // continuous particles per second
    int burst;       // one-shot count (> 0 disables the continuous rate)
    float lifetimeMin;
    float lifetimeMax;
    float speedMin;
    float speedMax;
    Vec3 direction;  // emission axis (normalized on use); (0,0,0) = sphere
    float coneCos;   // 1 = beam, -1 = full sphere
    float sizeStart;
    float sizeEnd;
    Vec3 colorStart;
    Vec3 colorEnd;
    float alphaStart;
    float alphaEnd;
    float gravity;
    float drag;

    ParticleEmitterProfile()
        : rate(20.0f),
          burst(0),
          lifetimeMin(0.4f),
          lifetimeMax(0.8f),
          speedMin(1.0f),
          speedMax(2.0f),
          coneCos(-1.0f),
          sizeStart(0.1f),
          sizeEnd(0.1f),
          colorStart(1, 1, 1),
          colorEnd(1, 1, 1),
          alphaStart(1.0f),
          alphaEnd(0.0f),
          gravity(0.0f),
          drag(0.0f) {}
};

struct ParticleEffectDef {
    std::string name;      // PS_StartEffect argument (lower-case)
    std::string texture;   // particles.txt material / bitmap key for the viewer
    bool additive;         // BlendingMode = additive vs alpha
    bool looping;
    float duration;        // emitter lifetime for non-looping effects
    std::vector<ParticleEmitterProfile> emitters;

    ParticleEffectDef() : additive(false), looping(false), duration(1.0f) {}
};

struct ParticleEffectInstance {
    const ParticleEffectDef* def;
    Vec3 origin;
    Vec3 direction;  // emitter axis (muzzle flash forward, impact normal)
    float age;
    bool emitting;
    std::vector<Particle> particles;
    std::vector<float> accum;  // per-emitter emission accumulator

    ParticleEffectInstance() : def(0), age(0.0f), emitting(false) {}
};

// Owns the effect definitions and the live instances.
class GameEffects {
public:
    // Registers a definition (idempotent by name). Viewer-owned textures are
    // keyed by def->texture.
    void addDefinition(const ParticleEffectDef& def);
    const ParticleEffectDef* findDefinition(const std::string& name) const;

    // Installs the built-in effect library (muzzle flash, impacts, blood,
    // smoke, explosion) — called once at startup; user definitions added via
    // addDefinition override same-named built-ins.
    void installBuiltins();

    // PS_StartEffect(name) at `origin` oriented along `direction`.
    void startEffect(const std::string& name, const Vec3& origin, const Vec3& direction);
    // PS_StopAllEmissions(): stop emitting, live particles fade out.
    void stopAll();

    void update(float dt);
    void clear();

    const std::vector<ParticleEffectInstance>& effects() const { return effects_; }
    DecalSystem decals;

private:
    // Stable storage: ParticleEffectInstance stores a const def pointer, and
    // addDefinition() (PS_StartEffect name fallback appends renamed copies at
    // runtime) must never invalidate those pointers — a vector realloc here
    // caused heap-use-after-free reads in update() (freed def garbage also
    // killed live particle rendering before the crash).
    std::deque<ParticleEffectDef> defs_;
    std::vector<ParticleEffectInstance> effects_;
    static const int kMaxParticles = 2048;
    static const int kMaxEffects = 64;
};

// Shared random helpers (deterministic per call, no global state).
float effectsRandom();

}  // namespace maxfx

#endif  // MAXFX_GAME_EFFECTS_H
