#include "map_object.h"

#include "core_transform.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "spell.h"    // SpellType
#include "unit.h"     // MapObjectServer
#include "unit_create.h"
#include "world.h"    // CWorld

#include <bit>
#include <concepts>
#include <cstring>

// The scale formulas must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr float kOne = 1.0f;
constexpr float kScaleStep = 0.4f;
static_assert(std::bit_cast<std::uint32_t>(kScaleStep) == 0x3ECCCCCDu);

// The original's branch-free max(0, v): 0 against v, as a mask selecting v's bits
// (also a NaN's), else 0.
std::uint32_t ClampToZero(float v) {
    return 0.0f >= v ? 0u : std::bit_cast<std::uint32_t>(v);
}

void StoreBits(float& field, std::uint32_t bits) { field = std::bit_cast<float>(bits); }

// The change stamp: the world's update count.
int UpdateCount(const CWorld* world) { return world->updateCount; }

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


} // namespace

// --- Change tracking -------------------------------------------------------------------------

void CMapObject::MarkChanged(int part) {
    if (part >= 0) netState.mask = static_cast<std::uint16_t>(netState.mask | (1u << (part & 31)));
    flags |= MapObjectFlag::Changed;
    if (refCount == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}

bool CMapObject::HasEffect(SpellType spellType) {
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the whole record is copied
        if (effect.Type() == spellType) return true;
    }
    return false;
}

// --- Per-tick update -------------------------------------------------------------------------

void CMapObject::Update() {
    if (companion) companion->RunTick();
}

void CMapObject::SetEffectCount(int count) {
    effects.resize(count);
}

void CMapObject::AddEffect(const MapEffect* effect) {
    bool rebirth = ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow);
    if (rebirth && std::ssize(effects) >= kRebirthMaxEffects) return;   // addon.dll drops it (and logs)
    int count = lib::Count(effects);
    SetEffectCount(count + 1);
    std::memcpy(&effects[count], effect, sizeof(MapEffect));
    netState.mask = static_cast<std::uint16_t>(netState.mask | 8);       // part 3
    flags |= MapObjectFlag::Changed;
    if (refCount != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    SpellType spellType = effect->Type();
    bool scales = spellType == SpellType::Strengthen || spellType == SpellType::Weaken || spellType == SpellType::Enlarge ||
                  spellType == SpellType::Shrink;
    ScaleEffectChanged(scales);
    if (spellType == SpellType::Stun || spellType == SpellType::Teleport) SetStunned(1);
    EffectsChanged();
    if (scales) RecomputeScale();
    if (rebirth && effect->ticksLeft > 1) RemoveOutdatedEffects(this, effect);
}
// Site #1 of FixSpellEffectsOverflow; #0 (expiring the effects of units the world update skips) is in
// WorldServer::UpdateUnits (original).

void CMapObject::ClearEffects() {
    effects = {};
    netState.mask = static_cast<std::uint16_t>(netState.mask | 8);       // part 3
    flags |= MapObjectFlag::Changed;
    if (refCount != 0) {
        if (CWorld* world = World()) changeStamp = UpdateCount(world);
    }
    ScaleEffectChanged(true);
    SetStunned(0);
    RecomputeScale();
}

float CMapObject::EffectValue(SpellType spellType) {
    std::uint32_t best = 0;
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the record is copied twice
        if (effect.Type() != spellType) continue;   // the record's first dword & 0xFF
        float current = std::bit_cast<float>(best);
        float value = std::bit_cast<float>(effect.value);
        best = current >= value ? best : effect.value;   // the C0 mask: a NaN takes the value
    }
    return std::bit_cast<float>(best);
}

void CMapObject::RecomputeScale() {
    ApplyBaseScale();
    int strength = (HasEffect(SpellType::Strengthen) ? 1 : 0) -
                   (HasEffect(SpellType::Weaken) ? 1 : 0);
    float strengthLevel = static_cast<float>(strength);
    int size = (HasEffect(SpellType::Enlarge) ? 1 : 0) -
               (HasEffect(SpellType::Shrink) ? 1 : 0);
    float total = static_cast<float>(size) + strengthLevel;
    if (size == 0) {
        if (strengthLevel != 0.0f) {
            float step = strengthLevel * kScaleStep;
            StoreBits(scale.x, ClampToZero(step + scale.x));
            if (MapType() != MapObjectType::Hero) StoreBits(scale.z, ClampToZero(step + scale.z));
        }
    } else if (total != 0.0f) {
        float step = total * kScaleStep;
        StoreBits(scale.x, ClampToZero(step + scale.x));
        StoreBits(scale.y, ClampToZero(step + scale.y));
        StoreBits(scale.z, ClampToZero(step + scale.z));
    }
    netState.mask = static_cast<std::uint16_t>(netState.mask | 2);       // part 1
    flags |= MapObjectFlag::Changed;
    if (refCount == 0) return;
    if (CWorld* world = World()) changeStamp = UpdateCount(world);
}

void CMapObject::ResetScale() { scale = baseScale; }

float* CMapObject::GetPosition(float* out) {
    std::memcpy(out, &localPosition.x, 3 * sizeof(float));
    return out;
}

float* CMapObject::GetPosition2(float* out) {
    std::memcpy(out, &localPosition.x, 3 * sizeof(float));
    return out;
}

float* CMapObject::GroundPoint(float* out) {
    float x = localPosition.x + bounds.center.x;    // each stored to a float local
    float y = localPosition.y + bounds.center.y;
    float z = localPosition.z + bounds.center.z;
    double height = World()->GridHeightAt(x, y);
    out[0] = x;
    out[1] = y;
    out[2] = pc24::Add(height, z);
    return out;
}

void CMapObject::PlaceOnGround(float offset) {
    double height = World()->GridHeightAt(localPosition.x, localPosition.y);
    localPosition.z = pc24::Add(height, offset);
}

void CMapObject::SetServerData(const UnitVitals* data) {
    ClassId classId = companion->GetClassId();
    if (classId != ClassId::MapObjectServer) return;
    ServerCompanion()->SetData(data);
}

void CMapObject::AttachCompanion(EngineObject* object) {
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = CompanionSlots(object);
    if (object) {
        CompanionOwner(object) = this;
        companion->AddRef();
    }
}

void CMapObject::AttachObject(EngineObject* object) {
    if (!object || SideTypeOf(object) != SideObjectType::MapObject) {
        CObject3D::AttachSpecific(object);   // the 3D object's attach virtual
        return;
    }
    if (companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    companion = CompanionSlots(object);
    CompanionOwner(object) = this;
    companion->AddRef();
}

int CMapObject::PackVitals() {
    if (companion->GetClassId() != ClassId::MapObjectServer) return 0;
    ServerCompanion()->vitals.Pack(&packedVitals);
    return 1;
}

bool CMapObject::IsInvisible() { return lib::Has(flags, MapObjectFlag::Invisible); }

bool CMapObject::IsStunned() { return lib::Has(flags, MapObjectFlag::Stunned); }

std::uint32_t CMapObject::Flag20000() { return lib::Has(flags, MapObjectFlag::Synchronised); }

// --- Lifetime ------------------------------------------------------------------------------------

CMapObject::CMapObject() {
    companion = nullptr;
    packedVitals.Reset();
    sectorCellX = -1;
    sectorCellY = -1;
    animationSpeed = kOne;
}

CMapObject::~CMapObject() = default;

// --- Small virtuals: the ones a plain object leaves empty (units, levers, traps and torches override them) and
// the network state's write / read wrappers.

namespace {
constexpr int kNoKey = -123;                    // the object asks for no key
constexpr float kNoSpeed = 0.0f;
constexpr float kPlainRadius = 0.75f;
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked
} // namespace

// --- Slots that read the object ------------------------------------------------------------------------

int CMapObject::NotifyCompanion() {
    companion->Unused14();
    return 1;
}

void CMapObject::TakePrototype(std::string_view prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics too), and the
    // original then packs what its stack held; the harness zeroes them there too (a determinism fix,
    // harness_leftovers.h).
    UnitVitals vitals{};
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(&packedVitals);
}

void CMapObject::WriteFullState() {
    DeltaState* state = &netState;
    state->SetWriteMode(0);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

void CMapObject::WriteChangedState() {
    if (netState.mask & kVitalParts) StatsChanged();
    DeltaState* state = &netState;
    state->SetWriteMode(1);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

std::uint32_t CMapObject::ReadState(int first, MemoryStream* reader) {
    DeltaState* state = &netState;
    state->BeginRead(first, reader);
    std::uint32_t result = ReadNetState(first);
    state->Finish();
    return result;
}

std::string* CMapObject::SetSecondName(const char* text) {
    name2 = text;
    return &name2;
}

// --- The virtuals for the compiler's tables ----------------------------------------------------------------
// The bodies above and in map_object*.cpp, called without dispatch.

void CMapObject::Load(ByteStream* stream) { LoadMapObject(stream); }
void CMapObject::Save(ByteStream* stream) { SaveMapObject(stream); }
bool CMapObject::InvisibleSlot() { return IsInvisible() != 0; }
void CMapObject::EffectsChanged() {}
bool CMapObject::IsSynchronised() { return Flag20000() != 0; }
void CMapObject::ApplyBaseScale() { ResetScale(); }
void CMapObject::ReceiveHit(DamageInfo*, CUnit*, CUnit*) {}
int CMapObject::StatsChanged() { return PackVitals(); }
void CMapObject::ScaleEffectChanged(bool) {}
void CMapObject::SetStats(const UnitStats*) {}
void CMapObject::UpdateSizeClasses() {}
bool CMapObject::IsUsable() { return false; }
bool CMapObject::IsDirectUseBlocked() { return false; }
std::uint32_t CMapObject::KeyId() { return static_cast<std::uint32_t>(kNoKey); }
bool CMapObject::AcceptsKey(const LockKey*) { return false; }
bool CMapObject::KeepsMapFlag() { return true; }
std::vector<Vector3> CMapObject::UseSpots(float) { return {}; }
std::uint32_t CMapObject::SetStunned(int) { return 0; }
Vector3* CMapObject::EyePoint(Vector3* out) {
    GroundPoint(&out->x);
    return out;
}
Vector3* CMapObject::PositionAt(Vector3* out, int) {
    GetPosition(&out->x);
    return out;
}
Vector3* CMapObject::AimPoint(Vector3* out) {
    GetPosition2(&out->x);
    return out;
}
bool CMapObject::IsLargeUnit() { return false; }
bool CMapObject::IsDead() { return companion->IsDead(); }
int CMapObject::ForgetObject(EngineObject*) { return 0; }
void CMapObject::UpdateMoveFactors() {}
void CMapObject::RefreshWounds() {}
double CMapObject::MoveSpeed() { return kNoSpeed; }
float CMapObject::Radius() { return kPlainRadius; }
void CMapObject::SetScriptName(std::string_view) {}

// --- Visibility and science -------------------------------------------------------------------------

// Of the unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light); whatever it returns is passed on.
int CMapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}

bool CMapObject::ApplyScience(const LockKey*, EngineObject*) {
    game::DebugLog("Science is applied on wrong object");
    return false;
}
