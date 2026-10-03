#include "map_object.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <bit>
#include <cstring>

// The scale formulas must keep the original's order of operations and its NaN behaviour.
#pragma float_control(precise, on)

namespace {
constexpr float kScaleStep = std::bit_cast<float>(0x3ECCCCCDu);   // 0073B868 (0.4)
constexpr std::uint32_t kFlagDirty = 2;
constexpr int kTypeHero = 0x32;          // CEngineObject::type_ of the player's units
constexpr int kClassMapObjectServer = 0x39;
constexpr int kWorldGroundHeight = 0x6C / 4;   // world slot 27: height(x, y), result in ST0

// The original's branch-free max(0, v): FCOMP of 0 against v, C0 (0 < v, or unordered) as a mask
// selecting v's bits, else 0.
std::uint32_t ClampToZero(float v) {
    return !(0.0f >= v) ? std::bit_cast<std::uint32_t>(v) : 0u;
}

void StoreBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(bits)); }

bool HasEffectType(const CMapObject* object, int type) {
    for (int i = 0; i < object->effectCount; ++i)
        if (object->effects[i].type == type) return true;
    return false;
}

double GroundHeight(void* world, float x, float y) { return ei::VCall<double>(world, kWorldGroundHeight, x, y); }
} // namespace

__declspec(naked) void* CMapObject::World() EI_ORIGINAL(0x00455950)
__declspec(naked) std::uintptr_t CMapObject::MarkChanged(int) EI_ORIGINAL(0x0050E9F0)
__declspec(naked) int CMapObject::HasEffect(std::uint32_t) EI_ORIGINAL(0x0050F630)

// --- Per-tick update -------------------------------------------------------------------------

int CMapObject::Update() {
    if (companion) ei::VCall<int>(companion, mapslot::kUpdate);
    return 1;
}
EI_REPLACE_METHOD(0x0050F1C0, CMapObject::Update);

// --- Effects ---------------------------------------------------------------------------------

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
        if (void* world = World()) changeStamp = *reinterpret_cast<const int*>(static_cast<const char*>(world) + 0x68);
    }
    ei::VCall<int>(this, mapslot::kScaleEffectChanged, 1);
    ei::VCall<int>(this, mapslot::kSetStunned, 0);
    return ei::VCall<std::uintptr_t>(this, mapslot::kRecomputeScale);
}
EI_REPLACE_METHOD(0x0050F5B0, CMapObject::ClearEffects);

float CMapObject::EffectValue(std::uint32_t type) {
    std::uint32_t best = 0;   // 0.0f (0073B858)
    for (int i = 0; i < effectCount; ++i) {
        MapEffect effect = effects[i];   // the original copies the record (twice)
        if (effect.type != type) continue;   // the record's first dword & 0xFF
        float current = std::bit_cast<float>(best);
        float value = std::bit_cast<float>(effect.value);
        if (!(current >= value)) best = effect.value;   // C0: current < value, or unordered
    }
    return std::bit_cast<float>(best);
}
EI_REPLACE_METHOD(0x0050F690, CMapObject::EffectValue);

// --- Scale -----------------------------------------------------------------------------------

std::uintptr_t CMapObject::RecomputeScale() {
    ei::VCall<void>(this, mapslot::kResetScale);
    int strength = (HasEffectType(this, mapeffect::kStrengthen) ? 1 : 0) - (HasEffectType(this, mapeffect::kWeaken) ? 1 : 0);
    float strengthLevel = static_cast<float>(strength);   // FILD, FSTP float
    int size = (HasEffectType(this, mapeffect::kEnlarge) ? 1 : 0) - (HasEffectType(this, mapeffect::kShrink) ? 1 : 0);
    double sizeLevel = size;                               // FILD, kept on the stack
    float total = static_cast<float>(sizeLevel + strengthLevel);
    if (size == 0) {
        if (strengthLevel != 0.0f) {
            double step = strengthLevel * static_cast<double>(kScaleStep);   // FMUL, FST to a float
            float stepRounded = static_cast<float>(step);
            StoreBits(scale[0], ClampToZero(static_cast<float>(step + scale[0])));
            if (type_ != kTypeHero)
                StoreBits(scale[2], ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale[2])));
        }
    } else if (total != 0.0f) {
        double step = total * static_cast<double>(kScaleStep);
        float stepRounded = static_cast<float>(step);
        StoreBits(scale[0], ClampToZero(static_cast<float>(step + scale[0])));
        StoreBits(scale[1], ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale[1])));
        StoreBits(scale[2], ClampToZero(static_cast<float>(static_cast<double>(stepRounded) + scale[2])));
    }
    changedParts = static_cast<std::uint16_t>(changedParts | 2);   // part 1
    flags_ |= kFlagDirty;
    if (refCount_ == 0) return 0;
    void* world = World();
    if (world) changeStamp = *reinterpret_cast<const int*>(static_cast<const char*>(world) + 0x68);
    return reinterpret_cast<std::uintptr_t>(world);
}
EI_REPLACE_METHOD(0x0050F2D0, CMapObject::RecomputeScale);

std::uint32_t CMapObject::ResetScale() {
    std::memcpy(scale, baseScale, sizeof(scale));
    std::uint32_t z;
    std::memcpy(&z, &baseScale[2], sizeof(z));
    return z;
}
EI_REPLACE_METHOD(0x0050E440, CMapObject::ResetScale);

// --- Position --------------------------------------------------------------------------------

float* CMapObject::GetPosition(float* out, int) {
    std::memcpy(out, &positionX, 3 * sizeof(float));
    return out;
}
EI_REPLACE_METHOD(0x0050E4B0, CMapObject::GetPosition);

float* CMapObject::GetPosition2(float* out) {
    std::memcpy(out, &positionX, 3 * sizeof(float));
    return out;
}
EI_REPLACE_METHOD(0x0050E4D0, CMapObject::GetPosition2);

float* CMapObject::GroundPoint(float* out) {
    float x = positionX + pointOffset[0];   // each stored to a float local
    float y = positionY + pointOffset[1];
    float z = positionZ + pointOffset[2];
    double height = GroundHeight(World(), x, y);
    out[0] = x;
    out[1] = y;
    out[2] = static_cast<float>(height + z);
    return out;
}
EI_REPLACE_METHOD(0x0050F730, CMapObject::GroundPoint);

void CMapObject::PlaceOnGround(float offset) {
    double height = GroundHeight(World(), positionX, positionY);
    positionZ = static_cast<float>(height + offset);
}
EI_REPLACE_METHOD(0x0050F020, CMapObject::PlaceOnGround);

// --- Companion -------------------------------------------------------------------------------

namespace {
// 00515950 (map_object_companion.cpp), through its game address.
__declspec(naked) std::uintptr_t __fastcall ServerSetData(CEngineObject*, void*, const void*) EI_ORIGINAL(0x00515950)
} // namespace

std::uintptr_t CMapObject::SetServerData(const void* data) {
    int classId = companion->GetClassId();
    if (classId != kClassMapObjectServer) return static_cast<std::uintptr_t>(classId);
    return ServerSetData(companion, nullptr, data);
}
EI_REPLACE_METHOD(0x0050F050, CMapObject::SetServerData);

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

// 00516D50. The 3D object's slot 19, which attaches type 0x46 objects at +0x94. Not rewritten.
__declspec(naked) std::uintptr_t __fastcall Object3DAttach(CMapObject*, void*, CEngineObject*) EI_ORIGINAL(0x00516D50)
// 00522C50. UnitVitals::Pack of unit_stats_pack.cpp, through its game address.
__declspec(naked) void* __fastcall PackVitalsAt(void*, void*, void*) EI_ORIGINAL(0x00522C50)
} // namespace

std::uintptr_t CMapObject::AttachObject(CEngineObject* object) {
    if (!object || object->type_ != kTypeMapCompanion) return Object3DAttach(this, nullptr, object);
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = object;
    CompanionOwner(object) = this;
    return static_cast<std::uintptr_t>(++companion->refCount_);
}
EI_REPLACE_METHOD(0x0050E890, CMapObject::AttachObject);

int CMapObject::PackVitals() {
    if (companion->GetClassId() != kClassMapObjectServer) return 0;
    PackVitalsAt(reinterpret_cast<char*>(companion) + 0x20, nullptr, reinterpret_cast<char*>(this) + 0x17C);
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
