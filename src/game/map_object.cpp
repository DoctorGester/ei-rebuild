#include "map_object.h"

#include "core_streams.h"
#include "core_transform.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "save_versions.h"
#include "server_specific_manager.h"   // TorchObjectSpec
#include "spell.h"    // SpellType
#include "torch.h"
#include "unit.h"     // MapObjectServer
#include "unit_create.h"
#include "world.h"    // World
#include "ei/rebirth.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

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
int UpdateCount(const World* world) { return world->updateCount; }

constexpr int kRebirthMaxEffects = 20;
constexpr double kRebirthSameStrength = 0.001;   // addon.dll's constant (a double)

// Rebirth fix (FixSpellEffectsOverflow): after adding `effect`, older effects (not the new last one) of the
// same type and end flag whose second float is within 0.001 of the new one's, that last no longer and whose
// value is not above it, are removed. The loop steps on after a removal (the next record, moved into the gap,
// is not looked at) and reads the count each time.
void RemoveOutdatedEffects(MapObject* object, const MapEffect* effect) {
    for (int i = 0; i < std::ssize(object->effects) - 1; ++i) {
        MapEffect* e = &object->effects[i];
        if (e->type != effect->type || e->ticksLeft > effect->ticksLeft) continue;
        EI_NAN_AUDIT(lib::FloatFromBits(effect->value), lib::FloatFromBits(e->value));
        if (lib::FloatFromBits(effect->value) < lib::FloatFromBits(e->value)) continue;
        if (e->ending != effect->ending) continue;
        float difference = lib::FloatFromBits(e->strength) - lib::FloatFromBits(effect->strength);
        float distance = difference < 0.0f ? -difference : difference;     // CRT fabs: exact
        if (static_cast<double>(distance) < kRebirthSameStrength) {          // plain IEEE compare
            object->effects.erase(object->effects.begin() + i);
        }
    }
}

} // namespace

// --- Change tracking -------------------------------------------------------------------------

void MapObject::MarkChanged(int part) {
    if (part >= 0) netState.mask = static_cast<std::uint16_t>(netState.mask | (1u << (part & 31)));
    flags |= MapObjectFlag::Changed;
    if (refCount == 0) return;
    if (World* world = OwningWorld()) changeStamp = UpdateCount(world);
}

bool MapObject::HasEffect(SpellType spellType) {
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the whole record is copied
        if (effect.Type() == spellType) return true;
    }
    return false;
}

// --- Per-tick update -------------------------------------------------------------------------

void MapObject::Update() {
    if (companion) companion->RunTick();
}

void MapObject::SetEffectCount(int count) {
    effects.resize(count);
}

void MapObject::AddEffect(const MapEffect* effect) {
    bool rebirth = ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellEffectsOverflow);
    if (rebirth && std::ssize(effects) >= kRebirthMaxEffects) return;   // addon.dll drops it (and logs)
    int count = lib::Count(effects);
    SetEffectCount(count + 1);
    std::memcpy(&effects[count], effect, sizeof(MapEffect));
    netState.mask = static_cast<std::uint16_t>(netState.mask | 8);       // part 3
    flags |= MapObjectFlag::Changed;
    if (refCount != 0) {
        if (World* world = OwningWorld()) changeStamp = UpdateCount(world);
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

void MapObject::ClearEffects() {
    effects = {};
    netState.mask = static_cast<std::uint16_t>(netState.mask | 8);       // part 3
    flags |= MapObjectFlag::Changed;
    if (refCount != 0) {
        if (World* world = OwningWorld()) changeStamp = UpdateCount(world);
    }
    ScaleEffectChanged(true);
    SetStunned(0);
    RecomputeScale();
}

float MapObject::EffectValue(SpellType spellType) {
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

void MapObject::RecomputeScale() {
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
    if (World* world = OwningWorld()) changeStamp = UpdateCount(world);
}

void MapObject::ResetScale() { scale = baseScale; }

float* MapObject::GetPosition(float* out) {
    std::memcpy(out, &localPosition.x, 3 * sizeof(float));
    return out;
}

float* MapObject::GetPosition2(float* out) {
    std::memcpy(out, &localPosition.x, 3 * sizeof(float));
    return out;
}

float* MapObject::GroundPoint(float* out) {
    float x = localPosition.x + bounds.center.x;    // each stored to a float local
    float y = localPosition.y + bounds.center.y;
    float z = localPosition.z + bounds.center.z;
    double height = OwningWorld()->GridHeightAt(x, y);
    out[0] = x;
    out[1] = y;
    out[2] = pc24::Add(height, z);
    return out;
}

void MapObject::PlaceOnGround(float offset) {
    double height = OwningWorld()->GridHeightAt(localPosition.x, localPosition.y);
    localPosition.z = pc24::Add(height, offset);
}

void MapObject::SetServerData(const UnitVitals* data) {
    ClassId classId = companion->GetClassId();
    if (classId != ClassId::MapObjectServer) return;
    ServerCompanion()->SetData(data);
}

void MapObject::AttachCompanion(EngineObject* object) {
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

void MapObject::AttachObject(EngineObject* object) {
    if (!object || SideTypeOf(object) != SideObjectType::MapObject) {
        Object3D::AttachSpecific(object);   // the 3D object's attach virtual
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

int MapObject::PackVitals() {
    if (companion->GetClassId() != ClassId::MapObjectServer) return 0;
    ServerCompanion()->vitals.Pack(&packedVitals);
    return 1;
}

bool MapObject::IsInvisible() { return lib::Has(flags, MapObjectFlag::Invisible); }

bool MapObject::IsStunned() { return lib::Has(flags, MapObjectFlag::Stunned); }

std::uint32_t MapObject::Flag20000() { return lib::Has(flags, MapObjectFlag::Synchronised); }

// --- Lifetime ------------------------------------------------------------------------------------

MapObject::MapObject() {
    companion = nullptr;
    packedVitals.Reset();
    sectorCellX = -1;
    sectorCellY = -1;
    animationSpeed = kOne;
}

MapObject::~MapObject() = default;

// --- Small virtuals: the ones a plain object leaves empty (units, levers, traps and torches override them) and
// the network state's write / read wrappers.

namespace {
constexpr int kNoKey = -123;                    // the object asks for no key
constexpr float kNoSpeed = 0.0f;
constexpr float kPlainRadius = 0.75f;
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked
} // namespace

// --- Slots that read the object ------------------------------------------------------------------------

int MapObject::NotifyCompanion() {
    companion->Unused14();
    return 1;
}

void MapObject::TakePrototype(std::string_view prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics too), and the
    // original then packs what its stack held; the harness zeroes them there too (a determinism fix,
    // harness_leftovers.h).
    UnitVitals vitals{};
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(&packedVitals);
}

void MapObject::WriteFullState() {
    DeltaState* state = &netState;
    state->SetWriteMode(0);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

void MapObject::WriteChangedState() {
    if (netState.mask & kVitalParts) StatsChanged();
    DeltaState* state = &netState;
    state->SetWriteMode(1);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

std::uint32_t MapObject::ReadState(int first, MemoryStream* reader) {
    DeltaState* state = &netState;
    state->BeginRead(first, reader);
    std::uint32_t result = ReadNetState(first);
    state->Finish();
    return result;
}

std::string* MapObject::SetSecondName(const char* text) {
    name2 = text;
    return &name2;
}

// --- The virtuals for the compiler's tables ----------------------------------------------------------------
// The bodies above and in map_object*.cpp, called without dispatch.

void MapObject::Load(ByteStream* stream) { LoadMapObject(stream); }
void MapObject::Save(ByteStream* stream) { SaveMapObject(stream); }
bool MapObject::InvisibleSlot() { return IsInvisible() != 0; }
void MapObject::EffectsChanged() {}
bool MapObject::IsSynchronised() { return Flag20000() != 0; }
void MapObject::ApplyBaseScale() { ResetScale(); }
void MapObject::ReceiveHit(DamageInfo*, Unit*, Unit*) {}
int MapObject::StatsChanged() { return PackVitals(); }
void MapObject::ScaleEffectChanged(bool) {}
void MapObject::SetStats(const UnitStats*) {}
void MapObject::UpdateSizeClasses() {}
bool MapObject::IsUsable() { return false; }
bool MapObject::IsDirectUseBlocked() { return false; }
std::uint32_t MapObject::KeyId() { return static_cast<std::uint32_t>(kNoKey); }
bool MapObject::AcceptsKey(const LockKey*) { return false; }
bool MapObject::KeepsMapFlag() { return true; }
std::vector<Vector3> MapObject::UseSpots(float) { return {}; }
std::uint32_t MapObject::SetStunned(int) { return 0; }
Vector3 MapObject::EyePoint() {
    Vector3 out;
    GroundPoint(&out.x);
    return out;
}
Vector3 MapObject::PositionAt(int) {
    Vector3 out;
    GetPosition(&out.x);
    return out;
}
Vector3 MapObject::AimPoint() {
    Vector3 out;
    GetPosition2(&out.x);
    return out;
}
bool MapObject::IsLargeUnit() { return false; }
bool MapObject::IsDead() { return companion->IsDead(); }
int MapObject::ForgetObject(EngineObject*) { return 0; }
void MapObject::UpdateMoveFactors() {}
void MapObject::RefreshWounds() {}
double MapObject::MoveSpeed() { return kNoSpeed; }
float MapObject::Radius() { return kPlainRadius; }
void MapObject::SetScriptName(std::string_view) {}

// --- Visibility and science -------------------------------------------------------------------------

// Of the unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light); whatever it returns is passed on.
int MapObject::SetVisibilityMode(int mode) {
    companion->SetLightMode(mode);
    return 0;
}

bool MapObject::ApplyScience(const LockKey*, EngineObject*) {
    game::DebugLog("Science is applied on wrong object");
    return false;
}

// --- Vitals ------------------------------------------------------------------------------------------

// MapObject: the server companion's vitals. See map_object.h and docs/notes/map_objects.md.

namespace {
} // namespace

void MapObject::ClearProtectionBases() {
    if (companion->GetClassId() != ClassId::MapObjectServer) return;
    // The original reloads the companion pointer for each store (two stores per pair, both +0).
    for (UnitStatPair& pair : ServerCompanion()->vitals.protection) pair.base = 0.0f;
}

// --- Create ------------------------------------------------------------------------------------------

// Create of map objects, units and torches: the map object's Create, then the object's number of network
// fields. See unit.h (Unit::Create), torch.h (TorchObject::Create) and docs/notes/units.md
// ("Creation, databases, save").

namespace {
constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;

constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // MapObject::updateCountdown

void DetachCompanion(MapObject* object) {
    if (EngineObject* companion = object->companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    object->companion = nullptr;
}

} // namespace

// --- MapObject's Create / Destroy (A-U) ---------------------------------------------------------

void MapObject::CreateMapObject() {
    Create3D();
    DetachCompanion(this);
    packedVitals.Reset();
    animationSpeed = 1.0f;
    packedVitals.Reset();
    netState.SetFieldCount(kMapObjectNetFields);
    netState.Reset();
    changeStamp = -1;
    effects = {};
    updateCountdown = kInitialCountdown;
    name2 = game::kEmptyText;
    baseScale.x = baseScale.y = baseScale.z = 0.0f;
}

void MapObject::DestroyMapObject() {
    SetEffectCount(0);
    DetachCompanion(this);
    Destroy3D();
}

// --- Units and torches -----------------------------------------------------------------------------

void Unit::Create() {
    CreateMapObject();
    Reset();
    AsMapObject()->netState.SetFieldCount(kUnitNetFields);
}

void TorchObject::Create() {
    CreateMapObject();
    TorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    DeltaState* state = &netState;
    state->SetFieldCount(kTorchNetFields);
    state->Reset();
}

// --- Save --------------------------------------------------------------------------------------------

// Saving and loading the map object part of every map object (MapObject Load / Save) and its
// server companion's (MapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.

namespace {

} // namespace

void MapObject::LoadMapObject(ByteStream* stream) {
    companion->Load(stream);
    stream->ReadValue(animationSpeed);
    stream->Read(&localPosition.x, 3 * sizeof(float));
    stream->ReadValue(localRotation);
    name = stream->ReadString();
    stream->ReadValue(scale);
    int count = 0;
    stream->ReadValue(count);
    SetEffectCount(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Load(stream);
    std::uint32_t currentFlags = flags;    // the original's local starts as stack garbage
    stream->ReadValue(currentFlags);
    flags = currentFlags;
    name2 = stream->ReadString();
    if (stream->version > kVersionBaseScale) stream->ReadValue(baseScale);
}

void MapObject::SaveMapObject(ByteStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->WriteValue(speed);
    stream->Write(&localPosition.x, 3 * sizeof(float));
    stream->WriteValue(localRotation);
    stream->WriteString(name);
    stream->WriteValue(scale);
    int count = lib::Count(effects);
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Save(stream);
    std::uint32_t currentFlags = flags;
    stream->WriteValue(currentFlags);
    stream->WriteString(name2);
    stream->WriteValue(baseScale);
}

void MapObjectServer::Load(ByteStream* stream) {
    stream->ReadValue(vitals);
    parentIname = stream->ReadString();
    skinTexture = stream->ReadString();
    int count = 0;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) strings.push_back(stream->ReadString());
}

void MapObjectServer::Save(ByteStream* stream) {
    stream->WriteValue(vitals);
    stream->WriteString(parentIname);
    stream->WriteString(skinTexture);
    int count = static_cast<int>(strings.size());
    stream->WriteValue(count);
    for (const std::string& text : strings) stream->WriteString(text);
}
