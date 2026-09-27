#include "maxfx/game/Effects.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace maxfx {

// Simple xorshift keep the module free of <random> and reproducible enough.
float effectsRandom() {
    static unsigned int state = 0x9e3779b9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// ---------------------------------------------------------------------------
// DecalSystem

DecalSystem::DecalSystem() : dropped_(0) {}

void DecalSystem::setMaterials(const std::vector<DecalMaterialInfo>& materials) {
    materials_ = materials;
}

const DecalMaterialInfo* DecalSystem::findMaterial(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < materials_.size(); ++i) {
        if (lowerCopy(materials_[i].name) == key) {
            return &materials_[i];
        }
    }
    return 0;
}

void DecalSystem::spawn(const std::string& materialName, const Vec3& position, const Vec3& normal) {
    Decal d;
    d.material = lowerCopy(materialName);
    d.position = position;
    Vec3 n = normal;
    const float ln = length(n);
    if (ln > 1.0e-5f) {
        n = Vec3(n.x / ln, n.y / ln, n.z / ln);
    } else {
        n = Vec3(0.0f, 1.0f, 0.0f);
    }
    d.normal = n;
    const DecalMaterialInfo* m = findMaterial(materialName);
    float lo = 0.05f;
    float hi = 0.15f;
    if (m != 0) {
        lo = m->minRadius > 0.001f ? m->minRadius : lo;
        hi = m->maxRadius > lo ? m->maxRadius : lo + 0.05f;
    }
    d.radius = lo + (hi - lo) * effectsRandom();
    d.rotation = effectsRandom() * 6.2831853f;
    if (static_cast<int>(decals_.size()) >= kMaxDecals) {
        // FIFO: the oldest decal rolls out (the engine keeps a per-level cap).
        decals_.erase(decals_.begin());
        ++dropped_;
    }
    decals_.push_back(d);
}

void DecalSystem::update(float dt) {
    for (std::size_t i = 0; i < decals_.size(); ++i) {
        decals_[i].age += dt;
    }
}

void DecalSystem::clear() {
    decals_.clear();
}

// ---------------------------------------------------------------------------
// Particles

void GameEffects::addDefinition(const ParticleEffectDef& def) {
    const std::string key = lowerCopy(def.name);
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (lowerCopy(defs_[i].name) == key) {
            defs_[i] = def;
            return;
        }
    }
    defs_.push_back(def);
}

const ParticleEffectDef* GameEffects::findDefinition(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < defs_.size(); ++i) {
        if (lowerCopy(defs_[i].name) == key) {
            return &defs_[i];
        }
    }
    return 0;
}

namespace {

Vec3 randomUnitVector() {
    // Uniform point on the unit sphere.
    const float z = effectsRandom() * 2.0f - 1.0f;
    const float a = effectsRandom() * 6.2831853f;
    const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return Vec3(r * std::cos(a), r * std::sin(a), z);
}

}  // namespace

void GameEffects::installBuiltins() {
    // Emitter profiles follow the look of the shipped effects (ParticleFX
    // exports): short additive muzzle bursts, brief surface sparks with a
    // smoke puff, spraying blood droplets, rising smoke, expanding fireballs.
    ParticleEffectDef e;

    e = ParticleEffectDef();
    e.name = "muzzleflash";
    e.texture = "muzzleflash";
    e.additive = true;
    e.duration = 0.12f;
    {
        ParticleEmitterProfile p;
        p.burst = 5;
        p.lifetimeMin = 0.05f;
        p.lifetimeMax = 0.10f;
        p.speedMin = 0.2f;
        p.speedMax = 0.6f;
        p.coneCos = 0.9f;
        p.sizeStart = 0.30f;
        p.sizeEnd = 0.05f;
        p.colorStart = Vec3(1.0f, 0.85f, 0.4f);
        p.colorEnd = Vec3(1.0f, 0.4f, 0.1f);
        p.alphaStart = 1.0f;
        p.alphaEnd = 0.0f;
        e.emitters.push_back(p);
    }
    addDefinition(e);

    e = ParticleEffectDef();
    e.name = "impact";
    e.texture = "impact";
    e.additive = true;
    e.duration = 0.25f;
    {
        ParticleEmitterProfile p;
        p.burst = 9;
        p.lifetimeMin = 0.12f;
        p.lifetimeMax = 0.30f;
        p.speedMin = 2.5f;
        p.speedMax = 6.0f;
        p.coneCos = 0.3f;
        p.sizeStart = 0.06f;
        p.sizeEnd = 0.02f;
        p.colorStart = Vec3(1.0f, 0.9f, 0.6f);
        p.colorEnd = Vec3(1.0f, 0.5f, 0.2f);
        p.alphaStart = 1.0f;
        p.alphaEnd = 0.0f;
        p.gravity = -4.0f;
        e.emitters.push_back(p);
    }
    {
        ParticleEmitterProfile p;
        p.burst = 4;
        p.lifetimeMin = 0.35f;
        p.lifetimeMax = 0.6f;
        p.speedMin = 0.3f;
        p.speedMax = 0.8f;
        p.coneCos = 0.7f;
        p.sizeStart = 0.10f;
        p.sizeEnd = 0.35f;
        p.colorStart = Vec3(0.55f, 0.55f, 0.55f);
        p.colorEnd = Vec3(0.3f, 0.3f, 0.3f);
        p.alphaStart = 0.5f;
        p.alphaEnd = 0.0f;
        p.drag = 2.0f;
        e.emitters.push_back(p);
    }
    addDefinition(e);

    e = ParticleEffectDef();
    e.name = "blood";
    e.texture = "blood";
    e.additive = false;
    e.duration = 0.2f;
    {
        ParticleEmitterProfile p;
        p.burst = 10;
        p.lifetimeMin = 0.25f;
        p.lifetimeMax = 0.5f;
        p.speedMin = 1.5f;
        p.speedMax = 4.0f;
        p.coneCos = 0.4f;
        p.sizeStart = 0.07f;
        p.sizeEnd = 0.03f;
        p.colorStart = Vec3(0.7f, 0.05f, 0.05f);
        p.colorEnd = Vec3(0.45f, 0.02f, 0.02f);
        p.alphaStart = 0.95f;
        p.alphaEnd = 0.4f;
        p.gravity = -9.0f;
        e.emitters.push_back(p);
    }
    addDefinition(e);

    e = ParticleEffectDef();
    e.name = "smoke";
    e.texture = "smoke";
    e.additive = false;
    e.looping = true;
    e.duration = 1.0f;
    {
        ParticleEmitterProfile p;
        p.rate = 8.0f;
        p.lifetimeMin = 1.2f;
        p.lifetimeMax = 2.0f;
        p.speedMin = 0.15f;
        p.speedMax = 0.4f;
        p.coneCos = 0.8f;
        p.sizeStart = 0.08f;
        p.sizeEnd = 0.5f;
        p.colorStart = Vec3(0.6f, 0.6f, 0.6f);
        p.colorEnd = Vec3(0.35f, 0.35f, 0.35f);
        p.alphaStart = 0.35f;
        p.alphaEnd = 0.0f;
        p.drag = 0.5f;
        e.emitters.push_back(p);
    }
    addDefinition(e);

    e = ParticleEffectDef();
    e.name = "explosion";
    e.texture = "explosion";
    e.additive = true;
    e.duration = 0.5f;
    {
        ParticleEmitterProfile p;
        p.burst = 14;
        p.lifetimeMin = 0.25f;
        p.lifetimeMax = 0.5f;
        p.speedMin = 2.0f;
        p.speedMax = 5.0f;
        p.sizeStart = 0.45f;
        p.sizeEnd = 0.1f;
        p.colorStart = Vec3(1.0f, 0.75f, 0.3f);
        p.colorEnd = Vec3(0.9f, 0.25f, 0.05f);
        p.alphaStart = 1.0f;
        p.alphaEnd = 0.0f;
        e.emitters.push_back(p);
    }
    {
        ParticleEmitterProfile p;
        p.burst = 8;
        p.lifetimeMin = 0.8f;
        p.lifetimeMax = 1.4f;
        p.speedMin = 0.5f;
        p.speedMax = 1.5f;
        p.sizeStart = 0.2f;
        p.sizeEnd = 0.9f;
        p.colorStart = Vec3(0.35f, 0.32f, 0.3f);
        p.colorEnd = Vec3(0.2f, 0.2f, 0.2f);
        p.alphaStart = 0.6f;
        p.alphaEnd = 0.0f;
        p.drag = 1.2f;
        e.emitters.push_back(p);
    }
    addDefinition(e);
}

void GameEffects::startEffect(const std::string& name, const Vec3& origin, const Vec3& direction) {
    const ParticleEffectDef* def = findDefinition(name);
    if (def == 0) {
        // Name heuristics for the shipped effect names whose .psc graphs are
        // not decoded yet: pick the closest built-in profile.
        const std::string n = lowerCopy(name);
        if (n.find("blood") != std::string::npos) {
            def = findDefinition("blood");
        } else if (n.find("muzzle") != std::string::npos || n.find("fire") != std::string::npos) {
            def = findDefinition("muzzleflash");
        } else if (n.find("explosion") != std::string::npos || n.find("grenade") != std::string::npos ||
                   n.find("bomb") != std::string::npos) {
            def = findDefinition("explosion");
        } else if (n.find("smoke") != std::string::npos || n.find("steam") != std::string::npos ||
                   n.find("dust") != std::string::npos) {
            def = findDefinition("smoke");
        } else if (n.find("spark") != std::string::npos || n.find("impact") != std::string::npos ||
                   n.find("hit") != std::string::npos || n.find("wall") != std::string::npos ||
                   n.find("surface") != std::string::npos) {
            def = findDefinition("impact");
        } else {
            def = findDefinition("impact");
        }
        if (def == 0) {
            return;
        }
        // Keep the requested name on the instance so the viewer can still
        // resolve the material textures of the real effect.
        ParticleEffectDef renamed = *def;
        renamed.name = lowerCopy(name);
        addDefinition(renamed);
        def = findDefinition(renamed.name);
    }

    // Reuse a live looping instance of the same effect at the same origin.
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].def == def && effects_[i].emitting && def->looping &&
            length(effects_[i].origin - origin) < 0.25f) {
            return;
        }
    }
    if (static_cast<int>(effects_.size()) >= kMaxEffects) {
        // Drop the oldest finished (or oldest overall) effect.
        for (std::size_t i = 0; i < effects_.size(); ++i) {
            if (effects_[i].particles.empty()) {
                effects_.erase(effects_.begin() + static_cast<long>(i));
                break;
            }
        }
        if (static_cast<int>(effects_.size()) >= kMaxEffects) {
            effects_.erase(effects_.begin());
        }
    }
    ParticleEffectInstance inst;
    inst.def = def;
    inst.origin = origin;
    Vec3 dir = direction;
    const float dl = length(dir);
    inst.direction = dl > 1.0e-5f ? Vec3(dir.x / dl, dir.y / dl, dir.z / dl) : Vec3(0, 1, 0);
    inst.age = 0.0f;
    inst.emitting = true;
    inst.accum.resize(def->emitters.size(), 0.0f);
    // One-shot emitters fire their burst immediately.
    for (std::size_t e = 0; e < def->emitters.size(); ++e) {
        const ParticleEmitterProfile& p = def->emitters[e];
        for (int b = 0; b < p.burst; ++b) {
            Particle pt;
            pt.lifetime = p.lifetimeMin + (p.lifetimeMax - p.lifetimeMin) * effectsRandom();
            pt.sizeStart = p.sizeStart;
            pt.sizeEnd = p.sizeEnd;
            pt.colorStart = p.colorStart;
            pt.colorEnd = p.colorEnd;
            pt.alphaStart = p.alphaStart;
            pt.alphaEnd = p.alphaEnd;
            pt.gravity = p.gravity;
            pt.drag = p.drag;
            const float speed = p.speedMin + (p.speedMax - p.speedMin) * effectsRandom();
            Vec3 v;
            if (length(inst.direction) < 0.5f || p.coneCos <= -0.999f) {
                v = randomUnitVector();
            } else {
                // Rejection-sample a cone around the emitter direction.
                for (int guard = 0; guard < 16; ++guard) {
                    v = randomUnitVector();
                    if (dot(v, inst.direction) >= p.coneCos) {
                        break;
                    }
                }
            }
            pt.position = inst.origin;
            pt.velocity = Vec3(v.x * speed, v.y * speed, v.z * speed);
            inst.particles.push_back(pt);
        }
    }
    effects_.push_back(inst);
}

void GameEffects::stopAll() {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        effects_[i].emitting = false;
    }
}

void GameEffects::update(float dt) {
    decals.update(dt);
    int totalParticles = 0;
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        ParticleEffectInstance& fx = effects_[i];
        fx.age += dt;
        if (fx.def == 0) {
            fx.emitting = false;
        } else {
            if (fx.emitting && !fx.def->looping && fx.age >= fx.def->duration) {
                fx.emitting = false;
            }
            if (fx.emitting) {
                for (std::size_t e = 0; e < fx.def->emitters.size(); ++e) {
                    const ParticleEmitterProfile& p = fx.def->emitters[e];
                    if (p.burst > 0 || p.rate <= 0.0f) {
                        continue;
                    }
                    if (fx.accum.size() <= e) {
                        fx.accum.resize(e + 1, 0.0f);
                    }
                    fx.accum[e] += p.rate * dt;
                    while (fx.accum[e] >= 1.0f && totalParticles < kMaxParticles) {
                        fx.accum[e] -= 1.0f;
                        Particle pt;
                        pt.lifetime = p.lifetimeMin + (p.lifetimeMax - p.lifetimeMin) * effectsRandom();
                        pt.sizeStart = p.sizeStart;
                        pt.sizeEnd = p.sizeEnd;
                        pt.colorStart = p.colorStart;
                        pt.colorEnd = p.colorEnd;
                        pt.alphaStart = p.alphaStart;
                        pt.alphaEnd = p.alphaEnd;
                        pt.gravity = p.gravity;
                        pt.drag = p.drag;
                        const float speed = p.speedMin + (p.speedMax - p.speedMin) * effectsRandom();
                        Vec3 v;
                        if (length(fx.direction) < 0.5f || p.coneCos <= -0.999f) {
                            v = randomUnitVector();
                        } else {
                            for (int guard = 0; guard < 16; ++guard) {
                                v = randomUnitVector();
                                if (dot(v, fx.direction) >= p.coneCos) {
                                    break;
                                }
                            }
                        }
                        pt.position = fx.origin;
                        pt.velocity = Vec3(v.x * speed, v.y * speed, v.z * speed);
                        fx.particles.push_back(pt);
                        ++totalParticles;
                    }
                }
            }
        }
        // Integrate particles.
        for (std::size_t p = 0; p < fx.particles.size();) {
            Particle& pt = fx.particles[p];
            pt.age += dt;
            if (pt.age >= pt.lifetime) {
                fx.particles.erase(fx.particles.begin() + static_cast<long>(p));
                continue;
            }
            pt.velocity.y += pt.gravity * dt;
            const float damp = pt.drag > 0.0f ? std::max(0.0f, 1.0f - pt.drag * dt) : 1.0f;
            pt.velocity = Vec3(pt.velocity.x * damp, pt.velocity.y * damp, pt.velocity.z * damp);
            pt.position = Vec3(pt.position.x + pt.velocity.x * dt, pt.position.y + pt.velocity.y * dt,
                               pt.position.z + pt.velocity.z * dt);
            ++p;
        }
    }
    // Reap finished effects.
    for (std::size_t i = 0; i < effects_.size();) {
        if (!effects_[i].emitting && effects_[i].particles.empty()) {
            effects_.erase(effects_.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
}

void GameEffects::clear() {
    effects_.clear();
    decals.clear();
}

}  // namespace maxfx
