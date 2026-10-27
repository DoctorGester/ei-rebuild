#include "map_object.h"
#include "unit.h"     // CMapObjectServer
#include "world.h"    // CWorld

#include "lib/crt.h"
#include "lib/mfc_array.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <concepts>
#include <bit>
#include <cstring>

// The scale formulas must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr float kScaleStep = std::bit_cast<float>(0x3ECCCCCDu);   // 0.4
constexpr std::uint32_t kFlagDirty = 2;
constexpr int kTypeHero = 0x32;          // CEngineObject::type_ of the player's units
constexpr int kClassMapObjectServer = 0x39;

// The original's branch-free max(0, v): FCOMP of 0 against v, C0 as a mask selecting v's bits
// (also a NaN's), else 0.
std::uint32_t ClampToZero(float v) {
    return 0.0f >= v ? 0u : std::bit_cast<std::uint32_t>(v);
}

void StoreBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(bits)); }

// The world's GridHeightAt: the height at (x, y), in ST0.
double GroundHeight(CWorld* world, float x, float y) { return world->GridHeightAt(x, y); }

// The change stamp: the world's update count.
int UpdateCount(const CWorld* world) { return world->updateCount_; }
} // namespace

CWorld* CMapObject::World() { return ObjectWorld(this); }

// --- Change tracking -------------------------------------------------------------------------

void CMapObject::MarkChanged(int part) {
    if (part >= 0) ChangedParts() = static_cast<std::uint16_t>(ChangedParts() | (1u << (part & 31)));
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}
EI_REWRITTEN(0x0050E9F0, CMapObject::MarkChanged);

int CMapObject::HasEffect(std::uint32_t type) {
    for (int i = 0; i < effects.size; ++i) {
        MapEffect effect = effects[i];   // the whole record is copied
        if (effect.type == type) return 1;
    }
    return 0;
}
EI_REWRITTEN(0x0050F630, CMapObject::HasEffect);

// --- Per-tick update -------------------------------------------------------------------------

int CMapObject::Update() {
    if (companion) companion->RunTick();
    return 1;
}
EI_REWRITTEN(0x0050F1C0, CMapObject::Update);


namespace {

constexpr int kRebirthMaxEffects = 20;
constexpr double kRebirthSameStrength = 0.001;   // addon.dll's constant (a double)

float FloatBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatBits(T) = delete;

// Rebirth fix (FixSpellEffectsOverflow): after adding `effect`, older effects (not the new last one) of the
// same type and end flag whose second float is within 0.001 of the new one's, that last no longer and whose
// value is not above it, are removed. The loop steps on after a removal (the next record, moved into the gap,
// is not looked at) and reads the count each time.
void RemoveOutdatedEffects(CMapObject* object, const MapEffect* effect) {
    for (int i = 0; i < object->effects.size - 1; ++i) {
        MapEffect* e = &object->effects[i];
        if (e->type != effect->type || e->ticksLeft > effect->ticksLeft) continue;
        EI_NAN_AUDIT(FloatBits(effect->value), FloatBits(e->value));
        if (FloatBits(effect->value) < FloatBits(e->value)) continue;   // FCOMPP; TEST AH,1
        if (e->ending != effect->ending) continue;
        float difference = FloatBits(e->strength) - FloatBits(effect->strength);
        float distance = difference < 0.0f ? -difference : difference;     // CRT fabs: exact
        if (static_cast<double>(distance) < kRebirthSameStrength) {          // TEST AH,5; JP: IEEE
            std::memmove(e, e + 1, (object->effects.size - i - 1) * sizeof(MapEffect));
            --object->effects.size;
        }
    }
}
} // namespace

void CMapObject::SetEffectCount(int count) {
    effects.SetSize(count, -1);
}

void CMapObject::AddEffect(const MapEffect* effect) {
    bool rebirth = ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow);
    if (rebirth && effects.size >= kRebirthMaxEffects) return;   // addon.dll drops it (and logs)
    int count = effects.size;
    SetEffectCount(count + 1);
    std::memcpy(&effects[count], effect, sizeof(MapEffect));
    ChangedParts() = static_cast<std::uint16_t>(ChangedParts() | 8);   // part 3
    flags_ |= kFlagDirty;
    if (refCount_ != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    // The original keeps the flag in the low byte of its argument's stack slot and passes the whole dword on.
    int type = effect->type;
    bool scales = type == mapeffect::kStrengthen || type == mapeffect::kWeaken || type == mapeffect::kEnlarge ||
                  type == mapeffect::kShrink;
    std::uint32_t argument = (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(effect)) & ~0xFFu) | (scales ? 1u : 0u);
    ScaleEffectChanged(static_cast<int>(argument));
    if (effect->type == mapeffect::kStun || effect->type == mapeffect::kStun2) SetStunned(1);
    EffectsChanged();
    if (scales) RecomputeScale();
    if (rebirth && effect->ticksLeft > 1) RemoveOutdatedEffects(this, effect);
}
EI_REWRITTEN(0x0050F1E0, CMapObject::AddEffect);
// Site #1 of FixSpellEffectsOverflow; #0 (expiring the effects of units the world update skips) is in
// CWorldServer::UpdateUnits (original).

void CMapObject::ClearEffects() {
    if (effects.data) {
        gamecrt::operator_delete(effects.data);
        effects.data = nullptr;
    }
    effects.maxSize = 0;
    effects.size = 0;
    ChangedParts() = static_cast<std::uint16_t>(ChangedParts() | 8);   // part 3
    flags_ |= kFlagDirty;
    if (refCount_ != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    ScaleEffectChanged(1);
    SetStunned(0);
    RecomputeScale();
}
EI_REWRITTEN(0x0050F5B0, CMapObject::ClearEffects);

float CMapObject::EffectValue(std::uint32_t type) {
    std::uint32_t best = 0;
    for (int i = 0; i < effects.size; ++i) {
        MapEffect effect = effects[i];   // the record is copied twice
        if (effect.type != type) continue;   // the record's first dword & 0xFF
        float current = std::bit_cast<float>(best);
        float value = std::bit_cast<float>(effect.value);
        best = current >= value ? best : effect.value;   // the C0 mask: a NaN takes the value
    }
    return std::bit_cast<float>(best);
}
EI_REWRITTEN(0x0050F690, CMapObject::EffectValue);


void CMapObject::RecomputeScale() {
    ApplyBaseScale();
    int strength = (HasEffect(mapeffect::kStrengthen) ? 1 : 0) - (HasEffect(mapeffect::kWeaken) ? 1 : 0);
    float strengthLevel = static_cast<float>(strength);   // FILD, FSTP float
    int size = (HasEffect(mapeffect::kEnlarge) ? 1 : 0) - (HasEffect(mapeffect::kShrink) ? 1 : 0);
    double sizeLevel = size;                               // FILD, kept on the stack
    float total = static_cast<float>(sizeLevel + strengthLevel);
    if (size == 0) {
        if (strengthLevel != 0.0f) {
            double step = strengthLevel * static_cast<double>(kScaleStep);   // FMUL, FST to a float
            float stepRounded = static_cast<float>(step);
            StoreBits(scale.x, ClampToZero(static_cast<float>(step + scale.x)));
            if (type_ != kTypeHero)
                StoreBits(scale.z, ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale.z)));
        }
    } else if (total != 0.0f) {
        double step = total * static_cast<double>(kScaleStep);
        float stepRounded = static_cast<float>(step);
        StoreBits(scale.x, ClampToZero(static_cast<float>(step + scale.x)));
        StoreBits(scale.y, ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale.y)));
        StoreBits(scale.z, ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale.z)));
    }
    ChangedParts() = static_cast<std::uint16_t>(ChangedParts() | 2);   // part 1
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}
EI_REWRITTEN(0x0050F2D0, CMapObject::RecomputeScale);

void CMapObject::ResetScale() { scale = baseScale; }
EI_REWRITTEN(0x0050E440, CMapObject::ResetScale);


float* CMapObject::GetPosition(float* out, int) {
    std::memcpy(out, &localPosition_.x, 3 * sizeof(float));
    return out;
}
EI_REWRITTEN(0x0050E4B0, CMapObject::GetPosition);

float* CMapObject::GetPosition2(float* out) {
    std::memcpy(out, &localPosition_.x, 3 * sizeof(float));
    return out;
}
EI_REWRITTEN(0x0050E4D0, CMapObject::GetPosition2);

float* CMapObject::GroundPoint(float* out) {
    float x = localPosition_.x + center.x;   // each stored to a float local
    float y = localPosition_.y + center.y;
    float z = localPosition_.z + center.z;
    double height = GroundHeight(World(), x, y);
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + z);
    return out;
}
EI_REWRITTEN(0x0050F730, CMapObject::GroundPoint);

void CMapObject::PlaceOnGround(float offset) {
    double height = GroundHeight(World(), localPosition_.x, localPosition_.y);
    localPosition_.z = static_cast<float>(height + offset);
}
EI_REWRITTEN(0x0050F020, CMapObject::PlaceOnGround);


void CMapObject::SetServerData(const UnitVitals* data) {
    int classId = companion->GetClassId();
    if (classId != kClassMapObjectServer) return;
    ServerCompanion()->SetData(data);
}
EI_REWRITTEN(0x0050F050, CMapObject::SetServerData);

void CMapObject::AttachCompanion(CEngineObject* object) {
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = CompanionSlots(object);
    if (object) {
        CompanionOwner(object) = this;
        ++companion->refCount_;
    }
}
EI_REWRITTEN(0x0050E8F0, CMapObject::AttachCompanion);

namespace {
constexpr int kTypeMapCompanion = 0x47;   // CEngineObject::type_ of the companions AttachCompanion accepts

} // namespace

void CMapObject::AttachObject(CEngineObject* object) {
    if (!object || object->type_ != kTypeMapCompanion) {
        CObject3D::AttachSpecific(object);   // the 3D object's attach virtual
        return;
    }
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = CompanionSlots(object);
    CompanionOwner(object) = this;
    ++companion->refCount_;
}
EI_REWRITTEN(0x0050E890, CMapObject::AttachObject);

int CMapObject::PackVitals() {
    if (companion->GetClassId() != kClassMapObjectServer) return 0;
    ServerCompanion()->vitals.Pack(PackedVitals());
    return 1;
}
EI_REWRITTEN(0x0050E970, CMapObject::PackVitals);


std::uint32_t CMapObject::IsInvisible() { return (flags_ >> 16) & 1; }
EI_REWRITTEN(0x0050FA10, CMapObject::IsInvisible);

std::uint32_t CMapObject::IsStunned() { return (flags_ >> 18) & 1; }
EI_REWRITTEN(0x0050FA20, CMapObject::IsStunned);

std::uint32_t CMapObject::Flag20000() { return (flags_ >> 17) & 1; }
EI_REWRITTEN(0x0050E430, CMapObject::Flag20000);
