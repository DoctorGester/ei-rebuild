// The client companion's effect visuals (MapObjectClientSpecific, map_object.h / unit.h): the
// particle sources and looping sounds that follow the object's effect list, and the reset of the
// parts' alpha / tint when an invisibility or stun effect ends.

#include "game/effects/effects.h"
#include "engine/scene/light.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/core/base_manager.h"
#include "game/spells/spell.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "game/units/unit.h"

#include "game/console/console.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "game/app/singletons.h"
#include "engine/data/options.h"
#include "game/world/world.h"

#include <list>

namespace {

constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // power / 16

constexpr ParticleType kRingSource = ParticleType::ClayRing;        // Lichdom's ring
constexpr ParticleType kScaleRingSource = ParticleType::Modifier1;   // strengthen / weaken

// The sound distances (10 and 50), read from the exe.
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 50.0f;

// The behaviours whose several effects share one particle source (the strongest power wins).
bool MergesParticles(SpellType type) {
    switch (type) {
    case SpellType::ProtectionFromFire: case SpellType::ProtectionFromLightning: case SpellType::ProtectionFromAcid:
    case SpellType::EagleSight: case SpellType::Infravision: case SpellType::DetectLife: case SpellType::Silence:
    case SpellType::Stench: case SpellType::Stun: case SpellType::Antimagic: case SpellType::Strengthen:
    case SpellType::Weaken: case SpellType::Regeneration: case SpellType::Feeblemind: case SpellType::Speed:
    case SpellType::Slow:
        return true;
    default:
        return false;
    }
}

// The particle source type of a lasting effect's behaviour; none for a behaviour without a look (None).
std::optional<ParticleType> ParticleSourceType(SpellType type) {
    switch (type) {
    case SpellType::ProtectionFromFire: return ParticleType::SphereFire;
    case SpellType::ProtectionFromLightning: return ParticleType::SphereElectricity;
    case SpellType::ProtectionFromAcid: return ParticleType::SphereAcid;
    case SpellType::EagleSight: return ParticleType::VisionStar1;
    case SpellType::Infravision: return ParticleType::VisionStar2;
    case SpellType::DetectLife: return ParticleType::VisionStar3;
    case SpellType::Silence: return ParticleType::VisionStar4;
    case SpellType::Stench: return ParticleType::BlueGas;
    case SpellType::Antimagic: return ParticleType::Antimagic;
    case SpellType::Regeneration: return ParticleType::Silence;
    case SpellType::Feeblemind: return ParticleType::FeebleMind;
    case SpellType::Speed: return ParticleType::FeetCloud2;
    case SpellType::Slow: return ParticleType::FeetCloud1;
    default: return std::nullopt;
    }
}

// The folder of a behaviour's effect sounds ("" for those without).
const char* SoundFolder(SpellType type) {
    switch (type) {
    case SpellType::ProtectionFromFire: return "magic\\Fireshield";
    case SpellType::ProtectionFromLightning: return "magic\\Lightsheild";
    case SpellType::ProtectionFromAcid: return "magic\\PoisonShield";
    case SpellType::EagleSight: return "magic\\Eaglesight";
    case SpellType::Infravision: return "magic\\Infravision";
    case SpellType::DetectLife: return "magic\\Detectlife";
    case SpellType::Invisibility: return "magic\\Invisibility";
    case SpellType::Silence: return "magic\\Silence";
    case SpellType::Lichdom: return "magic\\Lichdom";
    case SpellType::Stun: return "magic\\Stun";
    case SpellType::Antimagic: return "magic\\Antimagic";
    case SpellType::Strengthen: return "magic\\Strengthen";
    case SpellType::Weaken: return "magic\\Weaken";
    case SpellType::Regeneration: return "magic\\Regeneration";
    case SpellType::Speed: return "magic\\Speed";
    case SpellType::Slow: return "magic\\Slow";
    case SpellType::Enlarge: return "magic\\Enlarge";
    case SpellType::Shrink: return "magic\\Shrink";
    default: return "";
    }
}

// The one-shot ring of Lichdom / strengthen / weaken, scaled by `size` (stored as a float).
void SpawnRing(MapObject* owner, SpellType type, float size, bool negate) {
    const bool scales = type == SpellType::Strengthen || type == SpellType::Weaken;
    ParticleSource* ring = g_clientWorld.particleManager->CreateSource(static_cast<int>(scales ? kScaleRingSource : kRingSource));
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float.
float RingSize(SpellType type, int power) {
    const float size = static_cast<float>(power) * kPowerScale;
    return type == SpellType::Weaken ? -size : size;
}

// The feeble mind sequence's chooser: the first name once, then the second.
int FeebleMindChooser(int, int previous) { return previous != 0 ? 1 : 0; }

// Is there a record of `type` other than `index`?
bool OtherRecordOfType(const std::vector<ParticleRecord>& particles, int index, SpellType type) {
    for (int i = 0; i < std::ssize(particles); ++i)
        if (i != index && particles[i].type == type) return true;
    return false;
}


// A new light's flags (ShowLight then clears Hidden).
constexpr LightFlag kLightNew = LightFlag::Hidden | LightFlag::Moving | LightFlag::Off;


// The light goes out for good: no longer held, hidden, its id off the list, released.
void DropLight(MapObjectClientSpecific* self) {
    self->light->flags &= ~LightFlag::Held;
    self->HideLight();
    self->light->flags |= LightFlag::Held;
    int id = self->light->id;
    std::erase(g_clientWorld.lightFactory->litIds, id);
    self->light.Reset();
}

} // namespace

void PlayEffectSound(bool play, EffectSoundRecord* record, const Vector3& position, const char* suffix) {
    SoundManager* sounds = g_clientWorld.sounds.get();
    const SpellType kind = record->kind;
    if (kind == SpellType::Stench) {
        record->timer = 0;
        return;
    }
    if (kind == SpellType::Feeblemind) {
        if (record->handle == -1) {
            std::vector<std::string> names{"magic\\FeebleMind\\start.wav", "magic\\FeebleMind\\circle.wav"};
            record->handle = sounds->AddSequence(names, 0, position,
                                                 kSoundNear, kSoundFar, false, -1, FeebleMindChooser);
            // The sequence plays start.wav once, then circle.wav.
        } else {
            sounds->RemoveSequence(record->handle);
            record->handle = -1;
            g_clientWorld.sounds->Play3D("magic\\FeebleMind\\end.wav", 0, position, kSoundNear, kSoundFar, false);
        }
        return;
    }
    const char* folder = SoundFolder(kind);
    if (*folder == '\0' || !play) return;
    std::string name = folder;
    name += suffix;
    sounds->Play3D(name, 0, position, kSoundNear, kSoundFar, false);
}

void MapObjectClientSpecific::EffectsChanged(bool first) {
    UpdateEffectParticles(first);
    UpdateEffectSounds(first);
}

void MapObjectClientSpecific::UpdateEffectParticles(bool first) {
    MapObject* mapObject = owner;
    // The effects, those of the merging behaviours once each with the highest power.
    std::vector<MapEffect> wanted;
    if (std::ssize(mapObject->effects) > 0) {
        for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
            const MapEffect& effect = mapObject->effects[i];
            bool merged = false;
            if (MergesParticles(effect.Behaviour())) {
                for (int j = 0; j < std::ssize(wanted); ++j) {
                    if (wanted[j].Behaviour() != effect.Behaviour()) continue;
                    if (effect.power > wanted[j].power) wanted[j].power = effect.power;
                    merged = true;
                    break;
                }
            }
            if (!merged) wanted.push_back(effect);
        }
    }

    for (int i = 0; i < std::ssize(particles); ++i) particles[i].present = 0;

    for (int w = 0; w < std::ssize(wanted); ++w) {
        SpellType spellType = wanted[w].Behaviour();
        int power = wanted[w].power;
        // The same effect at the same power keeps its record.
        bool kept = false;
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            ParticleRecord& record = particles[i];
            if (!record.present && record.type == spellType && record.power == power) {
                record.present = 1;
                kept = true;
            }
        }
        if (kept) continue;
        // A source of the same type takes the new power.
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            ParticleRecord& record = particles[i];
            if (!record.present && record.type == spellType && record.source) {
                record.power = power;
                ParticleSource* source = particles[i].source;
                source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
                particles[i].present = 1;
                kept = true;
            }
        }
        if (kept) continue;

        ParticleRecord added = {};
        added.type = spellType;
        added.power = power;
        added.present = 1;
        switch (spellType) {
        case SpellType::Invisibility:
            owner->flags |= MapObjectFlag::Invisible;
            break;
        case SpellType::Stun:
            owner->flags |= MapObjectFlag::Stunned;
            break;
        case SpellType::Lichdom:
        case SpellType::Strengthen:
        case SpellType::Weaken:
            if (!first) SpawnRing(owner, spellType, RingSize(spellType, power), false);
            break;
        case SpellType::Teleport:
        case SpellType::Enlarge:
        case SpellType::Shrink:
            break;
        default: {
            const std::optional<ParticleType> look = ParticleSourceType(spellType);
            if (!look) break;
            ParticleSource* source = g_clientWorld.particleManager->CreateSource(static_cast<int>(*look));
            added.source = source;
            source->Attach(owner);
            source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
            if (first)
                for (int tick = 0; tick < kWarmUpTicks; ++tick) source->RunTick();
            break;
        }
        }
        particles.push_back(std::move(added));
    }

    for (int i = 0; i < std::ssize(particles); ++i) {
        ParticleRecord* record = &particles[i];
        if (record->present) continue;
        switch (record->type) {
        case SpellType::Stun:
            if (OtherRecordOfType(particles, i, SpellType::Stun)) break;
            owner->flags &= ~MapObjectFlag::Stunned;
            owner->ClientSpecific()->ClearPartTints();
            break;
        case SpellType::Invisibility:
            if (OtherRecordOfType(particles, i, SpellType::Invisibility)) break;
            owner->flags &= ~MapObjectFlag::Invisible;
            owner->ClientSpecific()->ResetPartAlpha();
            break;
        case SpellType::Lichdom:
        case SpellType::Strengthen:
        case SpellType::Weaken:
            if (!owner->IsDead())
                SpawnRing(owner, record->type, RingSize(record->type, record->power), true);
            break;
        default:
            break;
        }
        record = &particles[i];
        if (record->source) record->source->Stop();
        particles.erase(particles.begin() + i);
        --i;
    }
}

void MapObjectClientSpecific::UpdateEffectSounds(bool first) {
    MapObject* mapObject = owner;
    // The effects' behaviours, once each.
    SpellType types[256];
    int typeCount = 0;
    for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
        SpellType spellType = mapObject->effects[i].Behaviour();
        bool known = false;
        for (int j = 0; j < typeCount && !known; ++j) known = types[j] == spellType;
        if (!known) types[typeCount++] = spellType;
    }

    for (int i = 0; i < std::ssize(sounds); ++i) sounds[i].present = 0;

    for (int t = 0; t < typeCount; ++t) {
        bool kept = false;
        for (int i = 0; i < std::ssize(sounds) && !kept; ++i) {
            if (!sounds[i].present && sounds[i].kind == types[t]) {
                sounds[i].present = 1;
                kept = true;
            }
        }
        if (kept) continue;
        EffectSoundRecord added = {};
        added.kind = types[t];
        added.spareHandle = -1;
        added.handle = -1;
        added.timer = 0;
        added.present = 1;
        PlayEffectSound(!first, &added, owner->localPosition, "\\start.wav");
        sounds.push_back(added);
    }

    for (int i = 0; i < std::ssize(sounds); ++i) {
        EffectSoundRecord* record = &sounds[i];
        if (record->present) continue;
        MapObject* target = owner;
        bool alive = !target->IsDead();
        PlayEffectSound(alive, record, target->localPosition, "\\end.wav");
        sounds.erase(sounds.begin() + i);
        --i;
    }
}

void Object3DClientSpecific::ResetPartAlpha() {
    for (int i = 0; i < std::ssize(materials); ++i) materials[i]->colors.diffuse.w = 1.0f;
}

void Object3DClientSpecific::ClearPartTints() {
    for (int i = 0; i < std::ssize(materials); ++i) {
        Material* part = materials[i];
        part->colors.specular = {0.0f, 0.0f, 0.0f, part->colors.specular.w};
        materials[i]->flags &= ~MaterialFlag::KeepSpecular;
    }
}

void MapObjectClientSpecific::HideLight() {
    if (!light || (light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags |= LightFlag::Hidden;
    g_clientWorld.terrain->RemoveLight(light);
    MapObject* mapObject = owner;
    if (mapObject && mapObject->attached) mapObject->attached->SetLit(false);
}

void MapObjectClientSpecific::ShowLight() {
    if (!light || !(light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags &= ~LightFlag::Hidden;
    g_clientWorld.terrain->AddLight(light);
    MapObject* mapObject = owner;
    if (mapObject && mapObject->GetClassId() == ClassId::Unit && mapObject->attached) mapObject->attached->SetLit(true);
}

void MapObjectClientSpecific::CreateLight(const float* colour, float radius, bool flag) {
    HideLight();
    light.Assign(g_clientWorld.lightFactory->CreateLight(kLightTypePoint));
    Vector3 rgb = {colour[0], colour[1], colour[2]};
    light->SetPoint(rgb, rgb);
    MapObject* mapObject = owner;
    Light* point = light;
    point->localPosition = {mapObject->localPosition.x, mapObject->localPosition.y,
                             kLightHeight + mapObject->localPosition.z};
    point->range = radius;
    float squared = radius * radius;
    point->rangeSquared = squared;
    point->inverseRangeSquared = 1.0f / squared;
    light->flags |= kLightNew;
    if (flag) light->flags |= LightFlag::Specular;
    ShowLight();
}

void MapObjectClientSpecific::SetLightMode(int mode) {
    if (mode == 1) {
        if (!light) {
            const float* quest = &options::kQuestLight.red;   // red, green, blue
            float colour[4] = {quest[0], quest[1], quest[2], 1.0f};
            CreateLight(colour, options::kQuestLight.radius, true);
        } else {
            light->flags &= ~LightFlag::Held;
            ShowLight();
        }
        light->flags |= LightFlag::Held;
        int lightId = light->id;
        g_clientWorld.lightFactory->litIds.push_back(lightId);
        return;
    }
    if (mode == 2 && light) DropLight(this);
}

void MapObjectClientSpecific::Destroy() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    if (light) DropLight(this);
    owner = nullptr;
}
