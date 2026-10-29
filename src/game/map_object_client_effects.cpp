// The client companion's effect visuals (CMapObjectClientSpecific, map_object.h / unit.h): the
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

constexpr std::uint32_t kPartTinted = 0x10;           // CFigureMesh::flags_
constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // power / 16

constexpr int kRingSource = static_cast<int>(ParticleType::ClayRing);        // Lichdom's ring
constexpr int kScaleRingSource = static_cast<int>(ParticleType::Modifier1);   // strengthen / weaken
constexpr int kFallbackSource = static_cast<int>(ParticleType::BlueGas);

// The sound distances (10 and 50), read from the exe.
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 50.0f;

// The effect types whose several entries share one particle source (the strongest power wins).
bool MergesParticles(int type) {
    switch (SpellTypeOf(type)) {
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
int ParticleSourceType(int type) {
    switch (SpellTypeOf(type)) {
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
const char* SoundFolder(int type) {
    switch (SpellTypeOf(type)) {
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
void SpawnRing(CMapObject* owner, int type, float size, bool negate) {
    CParticleSource* ring = g_clientWorld->particleManager_->CreateSource(SpellTypeOf(type) == SpellType::Strengthen || SpellTypeOf(type) == SpellType::Weaken ? kScaleRingSource : kRingSource);
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float.
float RingSize(int type, int power) {
    float size = static_cast<float>(power) * kPowerScale;
    float value = size;
    if (SpellTypeOf(type) == SpellType::Weaken) {
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
        if (i != index && SpellTypeOf(particles[i].type) == type) return true;
    return false;
}


constexpr std::uint32_t kLightHidden = 0x80;     // CEngineObject::flags_ of a light
constexpr std::uint32_t kLightHeld = 0x200;      // held on by the light effect (mode 1)
constexpr std::uint32_t kLightNew = 0x580;
constexpr std::uint32_t kLightFlag40 = 0x40;
constexpr int kPointLight = 0x31;
constexpr float kLightHeight = 2.0f;

// The default light of mode 1 (r, g, b, radius).
const float* DefaultLightColour() { return &options::g_options->questLight_.red; }

CObject3DSpecific* SideHalf(CMapObject* owner) {
    return owner->attached;
}

// The light goes out for good: no longer held, hidden, its id off the list, released.
void DropLight(CMapObjectClientSpecific* self) {
    self->light->flags_ &= ~kLightHeld;
    self->HideLight();
    self->light->flags_ |= kLightHeld;
    int id = self->light->id_;
    g_clientWorld->lightFactory_->litIds_.remove(id);
    self->light.Reset();
}

} // namespace

void PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, int kind,
                                const char* suffix) {
    CSoundManager* sounds = g_clientWorld->sounds_;
    if (SpellTypeOf(kind) == SpellType::Stench) {
        record->timer = 0;
        return;
    }
    if (SpellTypeOf(kind) == SpellType::Feeblemind) {
        if (record->handle == -1) {
            std::vector<std::string> names{"magic\\FeebleMind\\start.wav", "magic\\FeebleMind\\circle.wav"};
            record->handle = sounds->AddSequence(&names, 0, position,
                                                 kSoundNear, kSoundFar, 0, -1, FeebleMindChooser);
            // The folder is empty here: the original plays the bare suffix.
            std::string name;
            name += suffix;
            g_clientWorld->sounds_->Play3D(name, 0, position, kSoundNear, kSoundFar, 0, 0);
        } else {
            sounds->RemoveSequence(record->handle);
            record->handle = -1;
            g_clientWorld->sounds_->Play3D("magic\\FeebleMind\\end.wav", 0, position, kSoundNear, kSoundFar, 0, 0);
        }
        return;
    }
    const char* folder = SoundFolder(kind);
    if (*folder == '\0' || !play) return;
    std::string name = folder;
    name += suffix;
    sounds->Play3D(name, 0, position, kSoundNear, kSoundFar, 0, 0);
}

void CMapObjectClientSpecific::EffectsChanged(bool first) {
    UpdateEffectParticles(first);
    UpdateEffectSounds(first);
}

void CMapObjectClientSpecific::UpdateEffectParticles(bool first) {
    CMapObject* mapObject = CompanionOwner(this);
    // The effects, those of the merging types once each with the highest power.
    MapEffect* wanted = nullptr;
    int wantedCount = 0;
    if (std::ssize(mapObject->effects) > 0) {
        wanted = new MapEffect[std::ssize(mapObject->effects)];
        for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
            const MapEffect& effect = mapObject->effects[i];
            bool merged = false;
            if (MergesParticles(effect.type)) {
                for (int j = 0; j < wantedCount; ++j) {
                    if (wanted[j].type != effect.type) continue;
                    if (effect.power > wanted[j].power) wanted[j].power = effect.power;
                    merged = true;
                    break;
                }
            }
            if (!merged) std::memcpy(&wanted[wantedCount++], &effect, sizeof(MapEffect));
            mapObject = CompanionOwner(this);
        }
    }

    for (int i = 0; i < std::ssize(particles); ++i) particles[i].present = 0;

    for (int w = 0; w < wantedCount; ++w) {
        int type = wanted[w].type;
        int power = wanted[w].power;
        // The same effect at the same power keeps its record.
        bool kept = false;
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            EffectParticle& record = particles[i];
            if (!record.present && record.type == type && record.power == power) {
                record.present = 1;
                kept = true;
            }
        }
        if (kept) continue;
        // A source of the same type takes the new power.
        for (int i = 0; i < std::ssize(particles) && !kept; ++i) {
            EffectParticle& record = particles[i];
            if (!record.present && record.type == type && record.source) {
                record.power = power;
                CParticleSource* source = particles[i].source;
                source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
                particles[i].present = 1;
                kept = true;
            }
        }
        if (kept) continue;

        EffectParticle added = {};
        added.type = type;
        added.power = power;
        added.present = 1;
        switch (SpellTypeOf(type)) {
        case SpellType::Invisibility:
            CompanionOwner(this)->flags_ |= MapObjectFlag::Invisible;
            break;
        case SpellType::Stun:
            CompanionOwner(this)->flags_ |= MapObjectFlag::Stunned;
            break;
        case SpellType::Lichdom:
        case SpellType::Strengthen:
        case SpellType::Weaken:
            if (!first) SpawnRing(CompanionOwner(this), type, RingSize(type, power), false);
            break;
        case SpellType::Teleport:
        case SpellType::Enlarge:
        case SpellType::Shrink:
            break;
        default: {
            CParticleSource* source = g_clientWorld->particleManager_->CreateSource(ParticleSourceType(type));
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
        switch (SpellTypeOf(record->type)) {
        case SpellType::Stun:
            if (OtherRecordOfType(particles, i, SpellType::Stun)) break;
            CompanionOwner(this)->flags_ &= ~MapObjectFlag::Stunned;
            CompanionOwner(this)->ClientSpecific()->ClearPartTints();
            break;
        case SpellType::Invisibility:
            if (OtherRecordOfType(particles, i, SpellType::Invisibility)) break;
            CompanionOwner(this)->flags_ &= ~MapObjectFlag::Invisible;
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

    if (wanted) delete[] wanted;
}

void CMapObjectClientSpecific::UpdateEffectSounds(bool first) {
    CMapObject* mapObject = CompanionOwner(this);
    // The effect types, once each.
    std::uint8_t types[256];
    int typeCount = 0;
    for (int i = 0; i < std::ssize(mapObject->effects); ++i) {
        std::uint8_t type = mapObject->effects[i].type;
        bool known = false;
        for (int j = 0; j < typeCount && !known; ++j) known = types[j] == type;
        if (!known) types[typeCount++] = type;
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
        PlayEffectSound(!first, &added, &CompanionOwner(this)->localPosition_.x, added.kind, "\\start.wav");
        sounds.push_back(added);
    }

    for (int i = 0; i < std::ssize(sounds); ++i) {
        EffectSoundRecord* record = &sounds[i];
        if (record->present) continue;
        CMapObject* target = CompanionOwner(this);
        bool alive = !target->IsDead();
        PlayEffectSound(alive, record, &target->localPosition_.x, record->kind, "\\end.wav");
        sounds.erase(sounds.begin() + i);
        --i;
    }
}

void CObject3DClientSpecific::ResetPartAlpha() {
    for (int i = 0; i < std::ssize(materials); ++i) materials[i]->colors.diffuse.w = 1.0f;
}

void CObject3DClientSpecific::ClearPartTints() {
    for (int i = 0; i < std::ssize(materials); ++i) {
        CMaterial* part = materials[i];
        part->colors.specular.x = 0.0f;
        part->colors.specular.y = 0.0f;
        part->colors.specular.z = 0.0f;
        materials[i]->flags_ &= ~kPartTinted;
    }
}

void CMapObjectClientSpecific::HideLight() {
    if (!light || (light->flags_ & kLightHidden) || (light->flags_ & kLightHeld)) return;
    light->flags_ |= kLightHidden;
    g_clientWorld->terrain_->RemoveLight(light);
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(0);
}

void CMapObjectClientSpecific::ShowLight() {
    if (!light || !(light->flags_ & kLightHidden) || (light->flags_ & kLightHeld)) return;
    light->flags_ &= ~kLightHidden;
    g_clientWorld->terrain_->AddLight(light);
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && mapObject->GetClassId() == ClassId::Unit && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(1);
}

void CMapObjectClientSpecific::CreateLight(const float* colour, float radius, bool flag) {
    HideLight();
    light.Assign(g_clientWorld->lightFactory_->CreateLight(kPointLight));
    Vector3 rgb = {colour[0], colour[1], colour[2]};
    light->SetPoint(&rgb, &rgb);
    CMapObject* mapObject = CompanionOwner(this);
    CLight* point = light;
    point->localPosition_.x = mapObject->localPosition_.x;
    point->localPosition_.y = mapObject->localPosition_.y;
    point->localPosition_.z = kLightHeight + mapObject->localPosition_.z;
    point->range_ = radius;
    double squared = static_cast<double>(radius) * radius;
    point->rangeSquared_ = static_cast<float>(squared);
    point->inverseRangeSquared_ = static_cast<float>(1.0 / squared);
    light->flags_ |= kLightNew;
    if (flag) light->flags_ |= kLightFlag40;
    ShowLight();
}

void CMapObjectClientSpecific::SetLightMode(int mode) {
    if (mode == 1) {
        if (!light) {
            float colour[4] = {DefaultLightColour()[0], DefaultLightColour()[1], DefaultLightColour()[2], 1.0f};
            CreateLight(colour, options::g_options->questLight_.radius, true);
        } else {
            light->flags_ &= ~kLightHeld;
            ShowLight();
        }
        light->flags_ |= kLightHeld;
        int id = light->id_;
        g_clientWorld->lightFactory_->litIds_.insert(g_clientWorld->lightFactory_->litIds_.end(), id);
        return;
    }
    if (mode == 2 && light) DropLight(this);
}

void CMapObjectClientSpecific::Destroy() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    if (light) DropLight(this);
    owner = nullptr;
}
