// The client companion's effect visuals (CMapObjectClientSpecific, map_object.h / unit.h): the
// particle sources and looping sounds that follow the object's effect list, and the reset of the
// parts' alpha / tint when an invisibility or stun effect ends (A-U3).

#include "effects.h"
#include "map_object.h"
#include "game_arrays.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "base_manager.h"
#include "spell.h"
#include "sound.h"
#include "terrain.h"
#include "unit.h"

#include "console.h"
#include "game_lists.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

#include "ei/hook.h"
#include "lib/stlp_list.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"

#include <cstring>

namespace {

constexpr std::uint32_t kFlagInvisible = 0x10000;     // CEngineObject::flags_ of the object
constexpr std::uint32_t kFlagStunned = 0x40000;
constexpr std::uint32_t kPartTinted = 0x10;           // CFigureMesh::flags_
constexpr int kWarmUpTicks = 0x1E;                    // a source arriving with the object runs ahead
constexpr float kPowerScale = 0.0625f;                // 0073D040: power / 16

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

// The sound distances (0073DBE0 / 0073DBE4: 10 and 50), read from the exe as the original does.
float SoundNear() { return 10.0f /*0073DBE0*/; }
float SoundFar() { return 50.0f /*0073DBE4*/; }

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
    CParticleSource* ring = ParticleManagerObject()->CreateSource(type == kStrengthen || type == kWeaken ? kScaleRingSource : kRingSource);
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float as the original stores it.
float RingSize(int type, int power) {
    float size = static_cast<float>(power) * kPowerScale;
    float value = size;
    if (type == kWeaken) {
        size = -value;
        value = size;
    }
    return value;
}

void AddReference(CParticleSource* source) { ++source->refCount_; }
void ReleaseSource(CParticleSource* source) { source->Release(); }

// The feeble mind sequence's chooser (005117C0): the first name once, then the second.
int __fastcall FeebleMindChooser(int, int previous) { return previous != 0 ? 1 : 0; }

CObject3DClientSpecific* ClientHalf(CMapObject* owner) {
    return static_cast<CObject3DClientSpecific*>(owner->attached);
}

// Is there a record of `type` other than `index`?
bool OtherRecordOfType(const EffectParticleArray& particles, int index, int type) {
    for (int i = 0; i < particles.size; ++i)
        if (i != index && particles.data[i].type == type) return true;
    return false;
}

// --- Lights ---

constexpr std::uint32_t kLightHidden = 0x80;     // CEngineObject::flags_ of a light
constexpr std::uint32_t kLightHeld = 0x200;      // held on by slot 19 (mode 1)
constexpr std::uint32_t kLightNew = 0x580;
constexpr std::uint32_t kLightFlag40 = 0x40;
constexpr int kPointLight = 0x31;
constexpr float kLightHeight = 2.0f;             // 0073DBE8
constexpr int kUnitClassId = 0x50;

// The default light of slot 19's mode 1 (0079BEB0: r, g, b; 0079BEBC: radius).
const float* DefaultLightColour() { return &options::TheOptions()->questLight_.red; }
float DefaultLightRadius() { return options::TheOptions()->questLight_.radius; }

CObject3DSpecific* SideHalf(CMapObject* owner) {
    return owner->Specific();
}

// The light goes out for good: no longer held, hidden, its id off the list, released.
void DropLight(CMapObjectClientSpecific* self) {
    self->light->flags_ &= ~kLightHeld;
    self->HideLight();
    self->light->flags_ |= kLightHeld;
    int id = self->light->id_;
    stlp::ListRemoveInt(&TheLightFactory()->litIds_, nullptr, &id);
    CEngineObject* old = self->light;
    self->light = nullptr;
    if (old) old->Release();
}

} // namespace

// 005114D0 (__fastcall; RET 0xC).
void __fastcall PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, int kind,
                                const char* suffix) {
    CSoundManager* sounds = TheSoundManager();
    if (kind == kStench) {
        record->timer = 0;
        return;
    }
    if (kind == kFeebleMind) {
        if (record->handle == -1) {
            console::CStringArray names;
            names.SetAtGrow(names.GetSize(), "magic\\FeebleMind\\start.wav");
            names.SetAtGrow(names.GetSize(), "magic\\FeebleMind\\circle.wav");
            record->handle = sounds->AddSequence(reinterpret_cast<const SoundNameArray*>(&names), 0, position,
                                                 SoundNear(), SoundFar(), 0, -1, FeebleMindChooser);
            // The folder is empty here: the original plays the bare suffix.
            mfc::CString name;
            name += suffix;
            TheSoundManager()->Play3D(name, 0, position, SoundNear(), SoundFar(), 0, 0);
        } else {
            sounds->RemoveSequence(record->handle);
            record->handle = -1;
            TheSoundManager()->Play3D("magic\\FeebleMind\\end.wav", 0, position, SoundNear(), SoundFar(), 0, 0);
        }
        return;
    }
    const char* folder = SoundFolder(kind);
    if (*folder == '\0' || !play) return;
    mfc::CString name(folder);
    name += suffix;
    sounds->Play3D(name, 0, position, SoundNear(), SoundFar(), 0, 0);
}
EI_REWRITTEN(0x005114D0, PlayEffectSound);

void CMapObjectClientSpecific::OnEffectsChanged(bool first) {
    UpdateEffectParticles(first);
    UpdateEffectSounds(first);
}
EI_REPLACE_METHOD(0x005117D0, CMapObjectClientSpecific::OnEffectsChanged);

void CMapObjectClientSpecific::UpdateEffectParticles(bool first) {
    CMapObject* mapObject = CompanionOwner(this);
    // The effects, those of the merging types once each with the highest power.
    MapEffect* wanted = nullptr;
    int wantedCount = 0;
    if (mapObject->effectCount > 0) {
        wanted = static_cast<MapEffect*>(gamecrt::operator_new(mapObject->effectCount * sizeof(MapEffect)));
        for (int i = 0; i < mapObject->effectCount; ++i) {
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

    for (int i = 0; i < particles.size; ++i) particles.data[i].present = 0;

    for (int w = 0; w < wantedCount; ++w) {
        int type = wanted[w].type;
        int power = wanted[w].power;
        // The same effect at the same power keeps its record.
        bool kept = false;
        for (int i = 0; i < particles.size && !kept; ++i) {
            EffectParticle& record = particles.data[i];
            if (!record.present && record.type == type && record.power == power) {
                record.present = 1;
                kept = true;
            }
        }
        if (kept) continue;
        // A source of the same type takes the new power.
        for (int i = 0; i < particles.size && !kept; ++i) {
            EffectParticle& record = particles.data[i];
            if (!record.present && record.type == type && record.source) {
                record.power = power;
                CParticleSource* source = particles.data[i].source;
                source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
                particles.data[i].present = 1;
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
            CParticleSource* source = ParticleManagerObject()->CreateSource(ParticleSourceType(type));
            if (source) AddReference(source);
            added.source = source;
            source->Attach(CompanionOwner(this));
            source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
            if (first)
                for (int tick = 0; tick < kWarmUpTicks; ++tick) source->RunTick();
            break;
        }
        }
        // SetSize(size + 1), then the element assigned from the local record (00512490: the
        // source referenced), then the local record's reference released.
        int index = particles.size;
        mfc::impl::ArrayAs<gamearrays::Particles>(particles).SetSize(index + 1, -1);
        EffectParticle& record = particles.data[index];
        record.type = added.type;
        record.power = added.power;
        CParticleSource* old = record.source;
        record.source = added.source;
        if (added.source) AddReference(added.source);
        if (old) ReleaseSource(old);
        record.present = added.present;
        if (added.source) ReleaseSource(added.source);
    }

    for (int i = 0; i < particles.size; ++i) {
        EffectParticle* record = &particles.data[i];
        if (record->present) continue;
        switch (record->type) {
        case kStun:
            if (OtherRecordOfType(particles, i, kStun)) break;
            CompanionOwner(this)->flags_ &= ~kFlagStunned;
            ClientHalf(CompanionOwner(this))->ClearPartTints();
            break;
        case kInvisibility:
            if (OtherRecordOfType(particles, i, kInvisibility)) break;
            CompanionOwner(this)->flags_ &= ~kFlagInvisible;
            ClientHalf(CompanionOwner(this))->ResetPartAlpha();
            break;
        case kLichdom:
        case kStrengthen:
        case kWeaken:
            if (!ObjectSlots(CompanionOwner(this))->IsDead())
                SpawnRing(CompanionOwner(this), record->type, RingSize(record->type, record->power), true);
            break;
        default:
            break;
        }
        record = &particles.data[i];
        if (record->source) record->source->Stop();
        // RemoveAt(i): the element's reference released, the tail moved down.
        if (particles.data[i].source) ReleaseSource(particles.data[i].source);
        int tail = particles.size - i - 1;
        if (tail != 0) std::memmove(&particles.data[i], &particles.data[i + 1], tail * sizeof(EffectParticle));
        --particles.size;
        --i;
    }

    if (wanted) gamecrt::operator_delete(wanted);
}
EI_REWRITTEN(0x00510B80, CMapObjectClientSpecific::UpdateEffectParticles);

void CMapObjectClientSpecific::UpdateEffectSounds(bool first) {
    CMapObject* mapObject = CompanionOwner(this);
    // The effect types, once each.
    std::uint8_t types[256];
    int typeCount = 0;
    for (int i = 0; i < mapObject->effectCount; ++i) {
        std::uint8_t type = mapObject->effects[i].type;
        bool known = false;
        for (int j = 0; j < typeCount && !known; ++j) known = types[j] == type;
        if (!known) types[typeCount++] = type;
        mapObject = CompanionOwner(this);
    }

    for (int i = 0; i < sounds.size; ++i) sounds.data[i].present = 0;

    for (int t = 0; t < typeCount; ++t) {
        bool kept = false;
        for (int i = 0; i < sounds.size && !kept; ++i) {
            if (!sounds.data[i].present && sounds.data[i].kind == types[t]) {
                sounds.data[i].present = 1;
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
        PlayEffectSound(!first, &added, &CompanionOwner(this)->position.x, added.kind, "\\start.wav");
        int index = sounds.size;
        mfc::impl::ArrayAs<mfc::impl::SoundRecords>(sounds).SetSize(index + 1, -1);
        std::memcpy(&sounds.data[index], &added, sizeof(added));
    }

    for (int i = 0; i < sounds.size; ++i) {
        EffectSoundRecord* record = &sounds.data[i];
        if (record->present) continue;
        CMapObject* target = CompanionOwner(this);
        bool alive = !ObjectSlots(target)->IsDead();
        PlayEffectSound(alive, record, &target->position.x, record->kind, "\\end.wav");
        int tail = sounds.size - i - 1;
        if (tail != 0) std::memmove(&sounds.data[i], &sounds.data[i + 1], tail * sizeof(EffectSoundRecord));
        --sounds.size;
        --i;
    }
}
EI_REWRITTEN(0x005112A0, CMapObjectClientSpecific::UpdateEffectSounds);

void CObject3DClientSpecific::ResetPartAlpha() {
    for (int i = 0; i < materials.size; ++i) materials[i]->colors.diffuse.w = 1.0f;
}
EI_REPLACE_METHOD(0x0051CCA0, CObject3DClientSpecific::ResetPartAlpha);

void CObject3DClientSpecific::ClearPartTints() {
    for (int i = 0; i < materials.size; ++i) {
        CMaterial* part = materials[i];
        part->colors.specular.x = 0.0f;
        part->colors.specular.y = 0.0f;
        part->colors.specular.z = 0.0f;
        materials[i]->flags_ &= ~kPartTinted;
    }
}
EI_REPLACE_METHOD(0x0051CD60, CObject3DClientSpecific::ClearPartTints);

void CMapObjectClientSpecific::HideLight() {
    if (!light || (light->flags_ & kLightHidden) || (light->flags_ & kLightHeld)) return;
    light->flags_ |= kLightHidden;
    LightManager()->RemoveLight(static_cast<CLight*>(light));
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(0);
}
EI_REPLACE_METHOD(0x005119B0, CMapObjectClientSpecific::HideLight);

void CMapObjectClientSpecific::ShowLight() {
    if (!light || !(light->flags_ & kLightHidden) || (light->flags_ & kLightHeld)) return;
    light->flags_ &= ~kLightHidden;
    LightManager()->AddLight(static_cast<CLight*>(light));
    CMapObject* mapObject = CompanionOwner(this);
    if (mapObject && mapObject->GetClassId() == kUnitClassId && SideHalf(mapObject)) SideHalf(mapObject)->SetLit(1);
}
EI_REPLACE_METHOD(0x00511950, CMapObjectClientSpecific::ShowLight);

void CMapObjectClientSpecific::CreateLight(const float* colour, float radius, bool flag) {
    HideLight();
    ObjectHolderAssign(&light, nullptr, TheLightFactory()->CreateObject(kPointLight, -1));
    Vector3 rgb = {colour[0], colour[1], colour[2]};
    static_cast<CLight*>(light)->SetPoint(&rgb, &rgb);
    CMapObject* mapObject = CompanionOwner(this);
    auto* point = static_cast<CLight*>(light);
    point->position_.x = mapObject->position.x;
    point->position_.y = mapObject->position.y;
    point->position_.z = kLightHeight + mapObject->position.z;
    point->range_ = radius;
    double squared = static_cast<double>(radius) * radius;
    point->rangeSquared_ = static_cast<float>(squared);
    point->inverseRangeSquared_ = static_cast<float>(1.0 / squared);
    light->flags_ |= kLightNew;
    if (flag) light->flags_ |= kLightFlag40;
    ShowLight();
}
EI_REWRITTEN(0x00511830, CMapObjectClientSpecific::CreateLight);

void CMapObjectClientSpecific::ApplyLightMode(int mode) {
    if (mode == 1) {
        if (!light) {
            float colour[4] = {DefaultLightColour()[0], DefaultLightColour()[1], DefaultLightColour()[2], 1.0f};
            CreateLight(colour, DefaultLightRadius(), true);
        } else {
            light->flags_ &= ~kLightHeld;
            ShowLight();
        }
        light->flags_ |= kLightHeld;
        int id = light->id_;
        stlp::list<int>::Insert(TheLightFactory()->litIds_.End(), id);   // 004049D0 end, 00404A40 insert
        return;
    }
    if (mode == 2 && light) DropLight(this);
}
EI_REPLACE_METHOD(0x00511A00, CMapObjectClientSpecific::ApplyLightMode);

void CMapObjectClientSpecific::DestroyClientCompanion() {
    lightStateA = 0;
    lightStateB = 0;
    mfc::impl::ArrayAs<gamearrays::Particles>(particles).SetSize(0, -1);
    if (light) DropLight(this);
    owner = nullptr;
}
EI_REPLACE_METHOD(0x005107B0, CMapObjectClientSpecific::DestroyClientCompanion);
