#include "map_object.h"
#include "unit.h"     // CMapObjectServer
#include "world.h"    // CWorld

#include "lib/crt.h"
#include "lib/mfc_array.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "originals_engine.h"
#include "originals_units.h"
#include "originals_library.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <bit>
#include <cstring>

// The scale formulas must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr float kScaleStep = std::bit_cast<float>(0x3ECCCCCDu);   // 0073B868 (0.4)
constexpr std::uint32_t kFlagDirty = 2;
constexpr int kTypeHero = 0x32;          // CEngineObject::type_ of the player's units
constexpr int kClassMapObjectServer = 0x39;

// The original's branch-free max(0, v): FCOMP of 0 against v, C0 as a mask selecting v's bits
// (also a NaN's), else 0.
std::uint32_t ClampToZero(float v) {
    return 0.0f >= v ? 0u : std::bit_cast<std::uint32_t>(v);
}

void StoreBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(bits)); }

// The world's slot 27 (GridHeightAt): the height at (x, y), in ST0.
double GroundHeight(CWorld* world, float x, float y) { return world->GridHeightAt(x, y); }

// The change stamp: the world's update count.
int UpdateCount(const CWorld* world) { return world->updateCount_; }
} // namespace

CWorld* CMapObject::World() { return ObjectWorld(this); }

// --- Change tracking -------------------------------------------------------------------------

std::uintptr_t CMapObject::MarkChanged(int part) {
    if (part >= 0) changedParts = static_cast<std::uint16_t>(changedParts | (1u << (part & 31)));
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return 0;
    CWorld* world = World();
    if (world) changeStamp = UpdateCount(world);
    return reinterpret_cast<std::uintptr_t>(world);
}
EI_REPLACE_METHOD(0x0050E9F0, CMapObject::MarkChanged);

int CMapObject::HasEffect(std::uint32_t type) {
    for (int i = 0; i < effectCount; ++i) {
        MapEffect effect = effects[i];   // the original copies the whole record
        if (effect.type == type) return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0050F630, CMapObject::HasEffect);

// --- Per-tick update -------------------------------------------------------------------------

int CMapObject::Update() {
    if (companion) companion->RunTick();
    return 1;
}
EI_REPLACE_METHOD(0x0050F1C0, CMapObject::Update);

// --- Effects ---------------------------------------------------------------------------------

namespace {

constexpr int kRebirthMaxEffects = 20;
constexpr double kRebirthSameStrength = 0.001;   // addon.dll's constant (a double)

float FloatBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatBits(T) = delete;

// Rebirth (addon.dll 100181D5): after adding `effect`, older effects (not the new last one) of
// the same type and end flag whose +0x0C is within 0.001 of the new one's, that last no longer and
// whose value is not above it, are removed. The loop steps on after a removal (the next record,
// moved into the gap, is not looked at) and reads the count each time.
void RemoveOutdatedEffects(CMapObject* object, const MapEffect* effect) {
    for (int i = 0; i < object->effectCount - 1; ++i) {
        MapEffect* e = &object->effects[i];
        if (e->type != effect->type || e->ticksLeft > effect->ticksLeft) continue;
        EI_NAN_AUDIT(FloatBits(effect->value), FloatBits(e->value));
        if (FloatBits(effect->value) < FloatBits(e->value)) continue;   // FCOMPP; TEST AH,1
        if (e->ending != effect->ending) continue;
        float difference = FloatBits(e->strength) - FloatBits(effect->strength);
        float distance = difference < 0.0f ? -difference : difference;     // CRT fabs: exact
        if (static_cast<double>(distance) < kRebirthSameStrength) {          // TEST AH,5; JP: IEEE
            std::memmove(e, e + 1, (object->effectCount - i - 1) * sizeof(MapEffect));
            --object->effectCount;
        }
    }
}
} // namespace

void CMapObject::SetEffectCount(int count) {
    mfc::impl::Plain20::SetSize(reinterpret_cast<mfc::impl::Plain20::Array*>(&effectsVtable), nullptr, count, -1);
}

std::uintptr_t CMapObject::AddEffectImpl(const MapEffect* effect) {
    bool rebirth = ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow);
    if (rebirth && effectCount >= kRebirthMaxEffects) return 0;   // addon.dll drops it (and logs)
    int count = effectCount;
    SetEffectCount(count + 1);
    std::memcpy(&effects[count], effect, sizeof(MapEffect));
    changedParts = static_cast<std::uint16_t>(changedParts | 8);   // part 3
    flags_ |= kFlagDirty;
    if (refCount_ != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    // The original keeps the flag in the low byte of its argument's stack slot and passes the whole
    // dword to slot 33.
    int type = effect->type;
    bool scales = type == mapeffect::kStrengthen || type == mapeffect::kWeaken || type == mapeffect::kEnlarge ||
                  type == mapeffect::kShrink;
    std::uint32_t argument = (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(effect)) & ~0xFFu) | (scales ? 1u : 0u);
    ScaleEffectChanged(static_cast<int>(argument));
    if (effect->type == mapeffect::kStun || effect->type == mapeffect::kStun2) SetStunned(1);
    std::uintptr_t result = EffectsChanged();
    if (scales) result = RecomputeScale();
    else result &= ~0xFFu;   // MOV AL,[ESP+0x14] (the flag, 0)
    if (rebirth && effect->ticksLeft > 1) RemoveOutdatedEffects(this, effect);
    return result;
}
EI_REPLACE_METHOD(0x0050F1E0, CMapObject::AddEffectImpl);
// Site #1 of FixSpellEffectsOverflow; #0 (expiring the effects of units the world update skips) is
// in CWorldServer::UpdateUnits (0058D670, original).

std::uintptr_t CMapObject::ClearEffects() {
    if (effects) {
        gamecrt::operator_delete(effects);
        effects = nullptr;
    }
    effectCapacity = 0;
    effectCount = 0;
    changedParts = static_cast<std::uint16_t>(changedParts | 8);   // part 3
    flags_ |= kFlagDirty;
    if (refCount_ != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    ScaleEffectChanged(1);
    SetStunned(0);
    return RecomputeScale();
}
EI_REWRITTEN(0x0050F5B0, CMapObject::ClearEffects);

float CMapObject::EffectValue(std::uint32_t type) {
    std::uint32_t best = 0;   // 0.0f (0073B858)
    for (int i = 0; i < effectCount; ++i) {
        MapEffect effect = effects[i];   // the original copies the record (twice)
        if (effect.type != type) continue;   // the record's first dword & 0xFF
        float current = std::bit_cast<float>(best);
        float value = std::bit_cast<float>(effect.value);
        best = current >= value ? best : effect.value;   // the C0 mask: a NaN takes the value
    }
    return std::bit_cast<float>(best);
}
EI_REPLACE_METHOD(0x0050F690, CMapObject::EffectValue);

// --- Scale -----------------------------------------------------------------------------------

std::uintptr_t CMapObject::RecomputeScale() {
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
    changedParts = static_cast<std::uint16_t>(changedParts | 2);   // part 1
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return 0;
    CWorld* world = World();
    if (world) changeStamp = UpdateCount(world);
    return reinterpret_cast<std::uintptr_t>(world);
}
std::uintptr_t __fastcall RecomputeScaleHook(CMapObject* self) { return self->CMapObject::RecomputeScale(); }
EI_REPLACE(0x0050F2D0, RecomputeScaleHook);

std::uint32_t CMapObject::ResetScale() {
    std::memcpy(&scale, &baseScale, sizeof(scale));
    std::uint32_t z;
    std::memcpy(&z, &baseScale.z, sizeof(z));
    return z;
}
EI_REPLACE_METHOD(0x0050E440, CMapObject::ResetScale);

// --- Position --------------------------------------------------------------------------------

float* CMapObject::GetPosition(float* out, int) {
    std::memcpy(out, &position.x, 3 * sizeof(float));
    return out;
}
EI_REPLACE_METHOD(0x0050E4B0, CMapObject::GetPosition);

float* CMapObject::GetPosition2(float* out) {
    std::memcpy(out, &position.x, 3 * sizeof(float));
    return out;
}
EI_REPLACE_METHOD(0x0050E4D0, CMapObject::GetPosition2);

float* CMapObject::GroundPoint(float* out) {
    float x = position.x + center.x;   // each stored to a float local
    float y = position.y + center.y;
    float z = position.z + center.z;
    double height = GroundHeight(World(), x, y);
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + z);
    return out;
}
EI_REPLACE_METHOD(0x0050F730, CMapObject::GroundPoint);

void CMapObject::PlaceOnGroundImpl(float offset) {
    double height = GroundHeight(World(), position.x, position.y);
    position.z = static_cast<float>(height + offset);
}
EI_REPLACE_METHOD(0x0050F020, CMapObject::PlaceOnGroundImpl);

// --- Companion -------------------------------------------------------------------------------

std::uintptr_t CMapObject::SetServerDataImpl(const void* data) {
    int classId = companion->GetClassId();
    if (classId != kClassMapObjectServer) return static_cast<std::uintptr_t>(classId);
    return static_cast<CMapObjectServer*>(companion)->SetData(data);
}
EI_REPLACE_METHOD(0x0050F050, CMapObject::SetServerDataImpl);

void CMapObject::AttachCompanion(CEngineObject* object) {
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = object;
    if (object) {
        CompanionOwner(object) = this;
        ++companion->refCount_;
    }
}
EI_REPLACE_METHOD(0x0050E8F0, CMapObject::AttachCompanion);

namespace {
constexpr int kTypeMapCompanion = 0x47;   // CEngineObject::type_ of the companions slot 19 accepts

} // namespace

std::uintptr_t CMapObject::AttachObjectImpl(CEngineObject* object) {
    if (!object || object->type_ != kTypeMapCompanion) {
        CObject3D::AttachSpecific(object);   // 00516D50, the 3D object's slot 19 (no result)
        return 0;
    }
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = object;
    CompanionOwner(object) = this;
    return static_cast<std::uintptr_t>(++companion->refCount_);
}
EI_REPLACE_METHOD(0x0050E890, CMapObject::AttachObjectImpl);

int CMapObject::PackVitals() {
    if (companion->GetClassId() != kClassMapObjectServer) return 0;
    static_cast<CMapObjectServer*>(companion)->vitals.Pack(PackedVitals());
    return 1;
}
EI_REPLACE_METHOD(0x0050E970, CMapObject::PackVitals);

// --- Flags -----------------------------------------------------------------------------------

std::uint32_t CMapObject::IsInvisible() { return (flags_ >> 16) & 1; }
EI_REPLACE_METHOD(0x0050FA10, CMapObject::IsInvisible);

std::uint32_t CMapObject::IsStunned() { return (flags_ >> 18) & 1; }
EI_REPLACE_METHOD(0x0050FA20, CMapObject::IsStunned);

std::uint32_t CMapObject::Flag20000() { return (flags_ >> 17) & 1; }
EI_REPLACE_METHOD(0x0050E430, CMapObject::Flag20000);
