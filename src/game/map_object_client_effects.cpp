// The client companion's effect visuals (MapObjectClientSpecific, map_object.h / unit.h): the
// particle sources and looping sounds that follow the object's effect list, and the reset of the
// parts' alpha / tint when an invisibility or stun effect ends.

#include "effects.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "base_manager.h"
#include "spell.h"
#include "sound.h"
#include "terrain.h"
#include "unit.h"

#include "console.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "game_singletons.h"
#include "options.h"
#include "world.h"

#include <list>

namespace {

constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // power / 16

constexpr int kRingSource = static_cast<int>(ParticleType::ClayRing);        // Lichdom's ring
constexpr int kScaleRingSource = static_cast<int>(ParticleType::Modifier1);   // strengthen / weaken
constexpr int kFallbackSource = static_cast<int>(ParticleType::BlueGas);

// The sound distances (10 and 50), read from the exe.
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 50.0f;

// The effect types whose several entries share one particle source (the strongest power wins).
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

// The particle source type of a lasting effect source; unknown types log and take 0x2013.
int ParticleSourceType(SpellType type) {
    switch (type) {
    case SpellType::ProtectionFromFire: return static_cast<int>(ParticleType::SphereFire);
    case SpellType::ProtectionFromLightning: return static_cast<int>(ParticleType::SphereElectricity);
    case SpellType::ProtectionFromAcid: return static_cast<int>(ParticleType::SphereAcid);
    case SpellType::EagleSight: return static_cast<int>(ParticleType::VisionStar1);
    case SpellType::Infravision: return static_cast<int>(ParticleType::VisionStar2);
    case SpellType::DetectLife: return static_cast<int>(ParticleType::VisionStar3);
    case SpellType::Silence: return static_cast<int>(ParticleType::VisionStar4);
    case SpellType::Stench: return static_cast<int>(ParticleType::BlueGas);
    case SpellType::Antimagic: return static_cast<int>(ParticleType::Antimagic);
    case SpellType::Regeneration: return static_cast<int>(ParticleType::Silence);
    case SpellType::Feeblemind: return static_cast<int>(ParticleType::FeebleMind);
    case SpellType::Speed: return static_cast<int>(ParticleType::FeetCloud2);
    case SpellType::Slow: return static_cast<int>(ParticleType::FeetCloud1);
    default:
        game::DebugLog("ERROR: Spell effect on map object with unknown particle effect");
        return kFallbackSource;
    }
}

// The folder of an effect type's sounds ("" for types without).
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
void SpawnRing(CMapObject* owner, SpellType type, float size, bool negate) {
    ParticleSource* ring = g_clientWorld.particleManager->CreateSource(type == SpellType::Strengthen || type == SpellType::Weaken ? kScaleRingSource : kRingSource);
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float.
float RingSize(SpellType type, int power) {
    float size = static_cast<float>(power) * kPowerScale;
    float value = size;
    if (type == SpellType::Weaken) {
        size = -value;
        value = size;
    }
    return value;
}

// The feeble mind sequence's chooser: the first name once, then the second.
int FeebleMindChooser(int, int previous) { return previous != 0 ? 1 : 0; }

// Is there a record of `type` other than `index`?
bool OtherRecordOfType(const std::vector<EffectParticle>& particles, int index, SpellType type) {
    for (int i = 0; i < std::ssize(particles); ++i)
        if (i != index && particles[i].type == type) return true;
    return false;
}


// A new light's flags (ShowLight then clears Hidden).
constexpr LightFlag kLightNew = LightFlag::Hidden | LightFlag::Moving | LightFlag::Off;
constexpr int kPointLight = 0x31;
constexpr float kLightHeight = 2.0f;

// The default light of mode 1 (r, g, b, radius).
const float* DefaultLightColour() { return &options::g_options.questLight.red; }

Object3DSpecific* SideHalf(CMapObject* owner) {
    return owner->attached;
}

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

void PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, SpellType kind,
                     const char* suffix) {
    SoundManager* sounds = g_clientWorld.sounds;
    if (kind == SpellType::Stench) {
        record->timer = 0;
        return;
    }
    if (kind == SpellType::Feeblemind) {
        if (record->handle == -1) {
            std::vector<std::string> names{"magic\\FeebleMind\\start.wav", "magic\\FeebleMind\\circle.wav"};
            record->handle = sounds->AddSequence(&names, 0, position,
                                                 kSoundNear, kSoundFar, 0, -1, FeebleMindChooser);
            // The folder is empty here: the original plays the bare suffix.
            std::string name;
            name += suffix;
            g_clientWorld.sounds->Play3D(name, 0, position, kSoundNear, kSoundFar, 0);
        } else {
            sounds->RemoveSequence(record->handle);
            record->handle = -1;
            g_clientWorld.sounds->Play3D("magic\\FeebleMind\\end.wav", 0, position, kSoundNear, kSoundFar, 0);
        }
        return;
    }
    const char* folder = SoundFolder(kind);
    if (*folder == '\0' || !play) return;
    std::string name = folder;
    name += suffix;
    sounds->Play3D(name, 0, position, kSoundNear, kSoundFar, 0);
}

void MapObjectClientSpecific::EffectsChanged(bool first) {
    UpdateEffectParticles(first);
    UpdateEffectSounds(first);
}

void MapObjectClientSpecific::UpdateEffectParticles(bool first) {
    CMapObject* mapObject = CompanionOwner(this);
    // The effects, those of the merging types once each with the highest power.
    std::vector<MapEffect> wanted;
    if (std::ssize(mapObject->effects) > 0) {
        for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
            const MapEffect& effect = mapObject->effects[i];
            bool merged = false;
            if (MergesParticles(effect.Type())) {
                for (int j = 0; j < std::ssize(wanted); ++j) {
                    if (wanted[j].type != effect.type) continue;
                    if (effect.power > wanted[j].power) wanted[j].power = effect.power;
                    merged = true;
                    break;
                }
            }
            if (!merged) wanted.push_back(effect);
            mapObject = CompanionOwner(this);
        }
    }

    for (int i = 0; i < std::ssize(particles); ++i) particles[i].present = 0;

    for (int w = 0; w < std::ssize(wanted); ++w) {
        SpellType spellType = wanted[w].Type();
        int power = wanted[w].power;
        // The same effect at the same power keeps its record.
        bool kept = false;
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            EffectParticle& record = particles[i];
            if (!record.present && record.type == spellType && record.power == power) {
                record.present = 1;
                kept = true;
            }
        }
        if (kept) continue;
        // A source of the same type takes the new power.
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            EffectParticle& record = particles[i];
            if (!record.present && record.type == spellType && record.source) {
                record.power = power;
                ParticleSource* source = particles[i].source;
                source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
                particles[i].present = 1;
                kept = true;
            }
        }
        if (kept) continue;

        EffectParticle added = {};
        added.type = spellType;
        added.power = power;
        added.present = 1;
        switch (spellType) {
        case SpellType::Invisibility:
            CompanionOwner(this)->flags |= MapObjectFlag::Invisible;
            break;
        case SpellType::Stun:
            CompanionOwner(this)->flags |= MapObjectFlag::Stunned;
            break;
        case SpellType::Lichdom:
        case SpellType::Strengthen:
        case SpellType::Weaken:
            if (!first) SpawnRing(CompanionOwner(this), spellType, RingSize(spellType, power), false);
            break;
        case SpellType::Teleport:
        case SpellType::Enlarge:
        case SpellType::Shrink:
            break;
        default: {
            ParticleSource* source = g_clientWorld.particleManager->CreateSource(ParticleSourceType(spellType));
            added.source = source;
            source->Attach(CompanionOwner(this));
            source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
            if (first)
                for (int tick = 0; tick < kWarmUpTicks; ++tick) source->RunTick();
            break;
        }
        }
        particles.push_back(std::move(added));
    }

    for (int i = 0; i < std::ssize(particles); ++i) {
        EffectParticle* record = &particles[i];
        if (record->present) continue;
        switch (record->type) {
        case SpellType::Stun:
            if (OtherRecordOfType(particles, i, SpellType::Stun)) break;
            CompanionOwner(this)->flags &= ~MapObjectFlag::Stunned;
            CompanionOwner(this)->ClientSpecific()->ClearPartTints();
            break;
        case SpellType::Invisibility:
            if (OtherRecordOfType(particles, i, SpellType::Invisibility)) break;
            CompanionOwner(this)->flags &= ~MapObjectFlag::Invisible;
            CompanionOwner(this)->ClientSpecific()->ResetPartAlpha();
            break;
        case SpellType::Lichdom:
        case SpellType::Strengthen:
        case SpellType::Weaken:
            if (!CompanionOwner(this)->IsDead())
                SpawnRing(CompanionOwner(this), record->type, RingSize(record->type, record->power), true);
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
    CMapObject* mapObject = CompanionOwner(this);
    // The effect types, once each.
    SpellType types[256];
    int typeCount = 0;
    for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
        SpellType spellType = mapObject->effects[i].Type();
        bool known = false;
        for (int j = 0; j < typeCount && !known; ++j) known = types[j] == spellType;
        if (!known) types[typeCount++] = spellType;
        mapObject = CompanionOwner(this);
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
        PlayEffectSound(!first, &added, &CompanionOwner(this)->localPosition.x, added.kind, "\\start.wav");
        sounds.push_back(added);
    }

    for (int i = 0; i < std::ssize(sounds); ++i) {
        EffectSoundRecord* record = &sounds[i];
        if (record->present) continue;
        CMapObject* target = CompanionOwner(this);
        bool alive = !target->IsDead();
        PlayEffectSound(alive, record, &target->localPosition.x, record->kind, "\\end.wav");
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
        part->colors.specular.x = 0.0f;
        part->colors.specular.y = 0.0f;
        part->colors.specular.z = 0.0f;
        materials[i]->flags &= ~MaterialFlag::KeepSpecular;
    }
}

void MapObjectClientSpecific::HideLight() {
    if (!light || (light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags |= LightFlag::Hidden;
    g_clientWorld.terrain->RemoveLight(light);
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(false);
}

void MapObjectClientSpecific::ShowLight() {
    if (!light || !(light->flags & LightFlag::Hidden) || (light->flags & LightFlag::Held)) return;
    light->flags &= ~LightFlag::Hidden;
    g_clientWorld.terrain->AddLight(light);
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && mapObject->GetClassId() == ClassId::Unit && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(true);
}

void MapObjectClientSpecific::CreateLight(const float* colour, float radius, bool flag) {
    HideLight();
    light.Assign(g_clientWorld.lightFactory->CreateLight(kPointLight));
    Vector3 rgb = {colour[0], colour[1], colour[2]};
    light->SetPoint(rgb, rgb);
    CMapObject* mapObject = CompanionOwner(this);
    CLight* point = light;
    point->localPosition.x = mapObject->localPosition.x;
    point->localPosition.y = mapObject->localPosition.y;
    point->localPosition.z = kLightHeight + mapObject->localPosition.z;
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
            float colour[4] = {DefaultLightColour()[0], DefaultLightColour()[1], DefaultLightColour()[2], 1.0f};
            CreateLight(colour, options::g_options.questLight.radius, true);
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
