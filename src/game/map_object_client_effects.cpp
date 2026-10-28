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

constexpr std::uint32_t kFlagInvisible = 0x10000;     // CEngineObject::flags_ of the object
constexpr std::uint32_t kFlagStunned = 0x40000;
constexpr std::uint32_t kPartTinted = 0x10;           // CFigureMesh::flags_
constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // power / 16

constexpr int kInvisibility = 0x0F;
constexpr int kLichdom = 0x11;
constexpr int kStench = 0x13;
constexpr int kStun = 0x19;
constexpr int kStun2 = 0x1C;
constexpr int kStrengthen = 0x1D;
constexpr int kWeaken = 0x1E;
constexpr int kFeebleMind = 0x20;
constexpr int kEnlarge = 0x25;
constexpr int kShrink = 0x26;

constexpr int kRingSource = 0x2018;        // Lichdom's ring
constexpr int kScaleRingSource = 0x201B;   // strengthen / weaken
constexpr int kFallbackSource = 0x2013;

// The sound distances (10 and 50), read from the exe.
constexpr float kSoundNear = 10.0f;
constexpr float kSoundFar = 50.0f;

// The effect types whose several entries share one particle source (the strongest power wins).
bool MergesParticles(int type) {
    switch (type) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x10: case 0x13: case 0x19:
    case 0x1A: case 0x1D: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22:
        return true;
    default:
        return false;
    }
}

// The particle source type of a lasting effect source; unknown types log and take 0x2013.
int ParticleSourceType(int type) {
    switch (type) {
    case 0x09: return 0x2017;
    case 0x0A: return 0x2016;
    case 0x0B: return 0x2015;
    case 0x0C: return 0x2044;
    case 0x0D: return 0x2045;
    case 0x0E: return 0x2046;
    case 0x10: return 0x204C;
    case 0x13: return 0x2013;
    case 0x1A: return 0x201A;
    case 0x1F: return 0x2048;
    case 0x20: return 0x2049;
    case 0x21: return 0x204B;
    case 0x22: return 0x204A;
    default:
        game::DebugLog("ERROR: Spell effect on map object with unknown particle effect");
        return kFallbackSource;
    }
}

// The folder of an effect type's sounds ("" for types without).
const char* SoundFolder(int type) {
    switch (type) {
    case 0x09: return "magic\\Fireshield";
    case 0x0A: return "magic\\Lightsheild";
    case 0x0B: return "magic\\PoisonShield";
    case 0x0C: return "magic\\Eaglesight";
    case 0x0D: return "magic\\Infravision";
    case 0x0E: return "magic\\Detectlife";
    case 0x0F: return "magic\\Invisibility";
    case 0x10: return "magic\\Silence";
    case 0x11: return "magic\\Lichdom";
    case 0x19: return "magic\\Stun";
    case 0x1A: return "magic\\Antimagic";
    case 0x1D: return "magic\\Strengthen";
    case 0x1E: return "magic\\Weaken";
    case 0x1F: return "magic\\Regeneration";
    case 0x21: return "magic\\Speed";
    case 0x22: return "magic\\Slow";
    case 0x25: return "magic\\Enlarge";
    case 0x26: return "magic\\Shrink";
    default: return "";
    }
}

// The one-shot ring of Lichdom / strengthen / weaken, scaled by `size` (stored as a float).
void SpawnRing(CMapObject* owner, int type, float size, bool negate) {
    CParticleSource* ring = g_clientWorld->particleManager_->CreateSource(type == kStrengthen || type == kWeaken ? kScaleRingSource : kRingSource);
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float.
float RingSize(int type, int power) {
    float size = static_cast<float>(power) * kPowerScale;
    float value = size;
    if (type == kWeaken) {
        size = -value;
        value = size;
    }
    return value;
}

// The feeble mind sequence's chooser: the first name once, then the second.
int FeebleMindChooser(int, int previous) { return previous != 0 ? 1 : 0; }

// Is there a record of `type` other than `index`?
bool OtherRecordOfType(const std::vector<EffectParticle>& particles, int index, int type) {
    for (int i = 0; i < std::ssize(particles); ++i)
        if (i != index && particles[i].type == type) return true;
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
    CEngineObject* old = self->light;
    self->light = nullptr;
    if (old) old->Release();
}

} // namespace

void PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, int kind,
                                const char* suffix) {
    CSoundManager* sounds = g_clientWorld->sounds_;
    if (kind == kStench) {
        record->timer = 0;
        return;
    }
    if (kind == kFeebleMind) {
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
        switch (type) {
        case kInvisibility:
            CompanionOwner(this)->flags_ |= kFlagInvisible;
            break;
        case kStun:
            CompanionOwner(this)->flags_ |= kFlagStunned;
            break;
        case kLichdom:
        case kStrengthen:
        case kWeaken:
            if (!first) SpawnRing(CompanionOwner(this), type, RingSize(type, power), false);
            break;
        case kStun2:
        case kEnlarge:
        case kShrink:
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
        switch (record->type) {
        case kStun:
            if (OtherRecordOfType(particles, i, kStun)) break;
            CompanionOwner(this)->flags_ &= ~kFlagStunned;
            CompanionOwner(this)->ClientSpecific()->ClearPartTints();
            break;
        case kInvisibility:
            if (OtherRecordOfType(particles, i, kInvisibility)) break;
            CompanionOwner(this)->flags_ &= ~kFlagInvisible;
            CompanionOwner(this)->ClientSpecific()->ResetPartAlpha();
            break;
        case kLichdom:
        case kStrengthen:
        case kWeaken:
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
    if (mapObject && mapObject->GetClassId() == kClassUnit && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(1);
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
