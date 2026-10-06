// The client companion's effect visuals (CMapObjectClientSpecific, map_object.h / unit.h): the
// particle sources and looping sounds that follow the object's effect list, and the reset of the
// parts' alpha / tint when an invisibility or stun effect ends (A-U3).

#include "effects.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "sound.h"
#include "unit.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_units.h"

#include "ei/hook.h"

#include <cstring>

namespace {

constexpr std::uint32_t kFlagInvisible = 0x10000;     // CEngineObject::flags_ of the object
constexpr std::uint32_t kFlagStunned = 0x40000;
constexpr std::uint32_t kPartTinted = 0x10;           // FigurePart::flags_
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
float SoundNear() { return *reinterpret_cast<const float*>(0x0073DBE0); }
float SoundFar() { return *reinterpret_cast<const float*>(0x0073DBE4); }

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
        DebugMessageOriginal("ERROR: Spell effect on map object with unknown particle effect");
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
    CParticleSource* ring = static_cast<CParticleSource*>(ParticleCreateSourceOriginal(
        ParticleManagerObject(), nullptr, type == kStrengthen || type == kWeaken ? kScaleRingSource : kRingSource));
    ring->Attach(owner);
    float factor = size / ring->baseSize;
    ring->ScaleSize(negate ? -factor : factor);
}

// The ring's size: power / 16, negative for weaken, rounded to a float as the original stores it.
float RingSize(int type, int power) {
    volatile float size = static_cast<float>(power) * kPowerScale;
    float value = size;
    if (type == kWeaken) {
        size = -value;
        value = size;
    }
    return value;
}

void AddReference(CParticleSource* source) { ++reinterpret_cast<CEngineObject*>(source)->refCount_; }
void ReleaseSource(CParticleSource* source) { reinterpret_cast<CEngineObject*>(source)->Release(); }

// The feeble mind sequence's chooser (005117C0): the first name once, then the second.
int __fastcall FeebleMindChooser(int, int previous) { return previous != 0 ? 1 : 0; }

// 00510B80 / 005112A0 (rewritten below; through their game addresses so their wrappers see the
// calls). 005114D0 likewise.
__declspec(naked) void __fastcall CallUpdateEffectParticles(CMapObjectClientSpecific*, void*, bool)
    EI_ORIGINAL(0x00510B80)
__declspec(naked) void __fastcall CallUpdateEffectSounds(CMapObjectClientSpecific*, void*, bool)
    EI_ORIGINAL(0x005112A0)
__declspec(naked) void __fastcall CallPlayEffectSound(bool, EffectSoundRecord*, const float*, int, const char*)
    EI_ORIGINAL(0x005114D0)
// 0051CCA0 / 0051CD60 (rewritten below).
__declspec(naked) void __fastcall CallResetPartAlpha(CObject3DClientSpecific*) EI_ORIGINAL(0x0051CCA0)
__declspec(naked) void __fastcall CallClearPartTints(CObject3DClientSpecific*) EI_ORIGINAL(0x0051CD60)

CObject3DClientSpecific* ClientHalf(CMapObject* owner) {
    return static_cast<CObject3DClientSpecific*>(owner->Object3D()->attached);
}

// Is there a record of `type` other than `index`?
bool OtherRecordOfType(const EffectParticleArray& particles, int index, int type) {
    for (int i = 0; i < particles.size; ++i)
        if (i != index && particles.data[i].type == type) return true;
    return false;
}

} // namespace

// 005114D0 (__fastcall; RET 0xC).
void __fastcall PlayEffectSound(bool play, EffectSoundRecord* record, const float* position, int kind,
                                const char* suffix) {
    CSoundManager* sounds = sound::Manager();
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
            sound::Manager()->Play3D(name, 0, position, SoundNear(), SoundFar(), 0, 0);
        } else {
            sounds->RemoveSequence(record->handle);
            record->handle = -1;
            sound::Manager()->Play3D("magic\\FeebleMind\\end.wav", 0, position, SoundNear(), SoundFar(), 0, 0);
        }
        return;
    }
    const char* folder = SoundFolder(kind);
    if (*folder == '\0' || !play) return;
    mfc::CString name(folder);
    name += suffix;
    sounds->Play3D(name, 0, position, SoundNear(), SoundFar(), 0, 0);
}
EI_REPLACE(0x005114D0, PlayEffectSound);

void CMapObjectClientSpecific::EffectsChanged(bool first) {
    CallUpdateEffectParticles(this, nullptr, first);
    CallUpdateEffectSounds(this, nullptr, first);
}
EI_REPLACE_METHOD(0x005117D0, CMapObjectClientSpecific::EffectsChanged);

void CMapObjectClientSpecific::UpdateEffectParticles(bool first) {
    CMapObject* owner = CompanionOwner(this);
    // The effects, those of the merging types once each with the highest power.
    MapEffect* wanted = nullptr;
    int wantedCount = 0;
    if (owner->effectCount > 0) {
        wanted = static_cast<MapEffect*>(gamecrt::operator_new(owner->effectCount * sizeof(MapEffect)));
        for (int i = 0; i < owner->effectCount; ++i) {
            const MapEffect& effect = owner->effects[i];
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
            owner = CompanionOwner(this);
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
            CParticleSource* source = static_cast<CParticleSource*>(
                ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, ParticleSourceType(type)));
            if (source) AddReference(source);
            added.source = source;
            source->Attach(CompanionOwner(this));
            source->ScaleSize(static_cast<float>(power) * kPowerScale / source->baseSize);
            if (first)
                for (int tick = 0; tick < kWarmUpTicks; ++tick) reinterpret_cast<CEngineObject*>(source)->Slot6();
            break;
        }
        }
        // SetSize(size + 1), then the element assigned from the local record (00512490: the
        // source referenced), then the local record's reference released.
        int index = particles.size;
        CompanionParticlesSetSizeOriginal(&particles, nullptr, index + 1, -1);
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
            CallClearPartTints(ClientHalf(CompanionOwner(this)));
            break;
        case kInvisibility:
            if (OtherRecordOfType(particles, i, kInvisibility)) break;
            CompanionOwner(this)->flags_ &= ~kFlagInvisible;
            CallResetPartAlpha(ClientHalf(CompanionOwner(this)));
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
EI_REPLACE_METHOD(0x00510B80, CMapObjectClientSpecific::UpdateEffectParticles);

void CMapObjectClientSpecific::UpdateEffectSounds(bool first) {
    CMapObject* owner = CompanionOwner(this);
    // The effect types, once each.
    std::uint8_t types[256];
    int typeCount = 0;
    for (int i = 0; i < owner->effectCount; ++i) {
        std::uint8_t type = owner->effects[i].type;
        bool known = false;
        for (int j = 0; j < typeCount && !known; ++j) known = types[j] == type;
        if (!known) types[typeCount++] = type;
        owner = CompanionOwner(this);
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
        added.unknown_04 = -1;
        added.handle = -1;
        added.timer = 0;
        added.present = 1;
        CallPlayEffectSound(!first, &added, &CompanionOwner(this)->positionX, added.kind, "\\start.wav");
        int index = sounds.size;
        CompanionSoundsSetSizeOriginal(&sounds, nullptr, index + 1, -1);
        std::memcpy(&sounds.data[index], &added, sizeof(added));
    }

    for (int i = 0; i < sounds.size; ++i) {
        EffectSoundRecord* record = &sounds.data[i];
        if (record->present) continue;
        CMapObject* target = CompanionOwner(this);
        bool alive = !ObjectSlots(target)->IsDead();
        CallPlayEffectSound(alive, record, &target->positionX, record->kind, "\\end.wav");
        int tail = sounds.size - i - 1;
        if (tail != 0) std::memmove(&sounds.data[i], &sounds.data[i + 1], tail * sizeof(EffectSoundRecord));
        --sounds.size;
        --i;
    }
}
EI_REPLACE_METHOD(0x005112A0, CMapObjectClientSpecific::UpdateEffectSounds);

void CObject3DClientSpecific::ResetPartAlpha() {
    for (int i = 0; i < drawnPartCount; ++i) drawnParts[i]->alpha = 1.0f;
}
EI_REPLACE_METHOD(0x0051CCA0, CObject3DClientSpecific::ResetPartAlpha);

void CObject3DClientSpecific::ClearPartTints() {
    for (int i = 0; i < drawnPartCount; ++i) {
        FigurePart* part = drawnParts[i];
        part->tint[0] = 0.0f;
        part->tint[1] = 0.0f;
        part->tint[2] = 0.0f;
        drawnParts[i]->flags_ &= ~kPartTinted;
    }
}
EI_REPLACE_METHOD(0x0051CD60, CObject3DClientSpecific::ClearPartTints);
