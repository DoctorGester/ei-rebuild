#include "map_object.h"
#include "unit.h"     // CMapObjectServer
#include "world.h"    // CWorld

#include "lib/crt.h"

#include "ei/rebirth.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <bit>
#include <cstring>

// The scale formulas must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr float kScaleStep = std::bit_cast<float>(0x3ECCCCCDu);   // 0.4
constexpr std::uint32_t kFlagDirty = 2;
constexpr int kTypeHero = 0x32;          // CEngineObject::type_ of the player's units
constexpr int kClassMapObjectServer = 0x39;

// The original's branch-free max(0, v): 0 against v, as a mask selecting v's bits
// (also a NaN's), else 0.
std::uint32_t ClampToZero(float v) {
    return 0.0f >= v ? 0u : std::bit_cast<std::uint32_t>(v);
}

void StoreBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(bits)); }

// The world's GridHeightAt: the height at (x, y), unrounded.
double GroundHeight(CWorld* world, float x, float y) { return world->GridHeightAt(x, y); }

// The change stamp: the world's update count.
int UpdateCount(const CWorld* world) { return world->updateCount_; }

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
    for (int i = 0; i < std::ssize(object->effects) - 1; ++i) {
        MapEffect* e = &object->effects[i];
        if (e->type != effect->type || e->ticksLeft > effect->ticksLeft) continue;
        EI_NAN_AUDIT(FloatBits(effect->value), FloatBits(e->value));
        if (FloatBits(effect->value) < FloatBits(e->value)) continue;
        if (e->ending != effect->ending) continue;
        float difference = FloatBits(e->strength) - FloatBits(effect->strength);
        float distance = difference < 0.0f ? -difference : difference;     // CRT fabs: exact
        if (static_cast<double>(distance) < kRebirthSameStrength) {          // plain IEEE compare
            object->effects.erase(object->effects.begin() + i);
        }
    }
}

constexpr int kTypeMapCompanion = 0x47;   // CEngineObject::type_ of the companions AttachCompanion accepts

} // namespace

CWorld* CMapObject::World() { return ObjectWorld(this); }

// --- Change tracking -------------------------------------------------------------------------

void CMapObject::MarkChanged(int part) {
    if (part >= 0) netState_.mask_ = static_cast<std::uint16_t>(netState_.mask_ | (1u << (part & 31)));
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}

int CMapObject::HasEffect(std::uint32_t type) {
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the whole record is copied
        if (effect.type == type) return 1;
    }
    return 0;
}

// --- Per-tick update -------------------------------------------------------------------------

int CMapObject::Update() {
    if (companion) companion->RunTick();
    return 1;
}

void CMapObject::SetEffectCount(int count) {
    effects.resize(count);
}

void CMapObject::AddEffect(const MapEffect* effect) {
    bool rebirth = ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow);
    if (rebirth && std::ssize(effects) >= kRebirthMaxEffects) return;   // addon.dll drops it (and logs)
    int count = std::ssize(effects);
    SetEffectCount(count + 1);
    std::memcpy(&effects[count], effect, sizeof(MapEffect));
    netState_.mask_ = static_cast<std::uint16_t>(netState_.mask_ | 8);   // part 3
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
// Site #1 of FixSpellEffectsOverflow; #0 (expiring the effects of units the world update skips) is in
// CWorldServer::UpdateUnits (original).

void CMapObject::ClearEffects() {
    effects = {};
    netState_.mask_ = static_cast<std::uint16_t>(netState_.mask_ | 8);   // part 3
    flags_ |= kFlagDirty;
    if (refCount_ != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    ScaleEffectChanged(1);
    SetStunned(0);
    RecomputeScale();
}

float CMapObject::EffectValue(std::uint32_t type) {
    std::uint32_t best = 0;
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the record is copied twice
        if (effect.type != type) continue;   // the record's first dword & 0xFF
        float current = std::bit_cast<float>(best);
        float value = std::bit_cast<float>(effect.value);
        best = current >= value ? best : effect.value;   // the C0 mask: a NaN takes the value
    }
    return std::bit_cast<float>(best);
}

void CMapObject::RecomputeScale() {
    ApplyBaseScale();
    int strength = (HasEffect(mapeffect::kStrengthen) ? 1 : 0) - (HasEffect(mapeffect::kWeaken) ? 1 : 0);
    float strengthLevel = static_cast<float>(strength);   // stored as a float
    int size = (HasEffect(mapeffect::kEnlarge) ? 1 : 0) - (HasEffect(mapeffect::kShrink) ? 1 : 0);
    double sizeLevel = size;                               // kept unrounded
    float total = static_cast<float>(sizeLevel + strengthLevel);
    if (size == 0) {
        if (strengthLevel != 0.0f) {
            double step = strengthLevel * static_cast<double>(kScaleStep);   // rounded to a float
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
    netState_.mask_ = static_cast<std::uint16_t>(netState_.mask_ | 2);   // part 1
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}

void CMapObject::ResetScale() { scale = baseScale; }

float* CMapObject::GetPosition(float* out, int) {
    std::memcpy(out, &localPosition_.x, 3 * sizeof(float));
    return out;
}

float* CMapObject::GetPosition2(float* out) {
    std::memcpy(out, &localPosition_.x, 3 * sizeof(float));
    return out;
}

float* CMapObject::GroundPoint(float* out) {
    float x = localPosition_.x + bounds.center.x;   // each stored to a float local
    float y = localPosition_.y + bounds.center.y;
    float z = localPosition_.z + bounds.center.z;
    double height = GroundHeight(World(), x, y);
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + z);
    return out;
}

void CMapObject::PlaceOnGround(float offset) {
    double height = GroundHeight(World(), localPosition_.x, localPosition_.y);
    localPosition_.z = static_cast<float>(height + offset);
}

void CMapObject::SetServerData(const UnitVitals* data) {
    int classId = companion->GetClassId();
    if (classId != kClassMapObjectServer) return;
    ServerCompanion()->SetData(data);
}

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

int CMapObject::PackVitals() {
    if (companion->GetClassId() != kClassMapObjectServer) return 0;
    ServerCompanion()->vitals.Pack(&packedVitals);
    return 1;
}

std::uint32_t CMapObject::IsInvisible() { return (flags_ >> 16) & 1; }

std::uint32_t CMapObject::IsStunned() { return (flags_ >> 18) & 1; }

std::uint32_t CMapObject::Flag20000() { return (flags_ >> 17) & 1; }
