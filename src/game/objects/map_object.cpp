#include "game/objects/map_object.h"

#include "engine/core/core_streams.h"
#include "engine/core/core_transform.h"
#include "game/net/net_delta.h"
#include "game/objects/object3d.h"
#include "game/objects/torch.h"
#include "game/saves/save_versions.h"
#include "game/spells/spell.h"   // SpellType
#include "game/units/unit.h"   // MapObjectServer
#include "game/objects/map_object_manager.h"
#include "game/world/world.h"   // World

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>

// The scale formulas must keep the original's order of operations and its NaN behaviour.

namespace {
constexpr float kScaleStep = 0.4f;

// The most effects an object holds: the network sends the count as a byte (WriteNetState).
constexpr int kMaxEffects = 255;
constexpr float kSameStrength = 0.001f;   // Rebirth's constant

// After adding `effect`: the older effects (not the new last one) it renews are removed, those of the same type
// and end flag whose strength is within 0.001 of the new one's, that last no longer and whose value is not above it.
// The original kept every copy, so a buff cast again and again piled up (Rebirth's FixSpellEffectsOverflow, whose
// loop also skipped the record after each removal).
void RemoveOutdatedEffects(MapObject* object, const MapEffect* effect) {
    for (int i = 0; i < std::ssize(object->effects) - 1;) {
        MapEffect* e = &object->effects[i];
        bool outdated = e->type == effect->type && e->ticksLeft <= effect->ticksLeft && e->ending == effect->ending;
        if (outdated) {
            outdated = !(effect->value < e->value);
        }
        if (outdated) {
            float difference = e->strength - effect->strength;
            outdated = std::fabs(difference) < kSameStrength;
        }
        if (outdated) {
            EffectEnded(*object, *e, EffectEnd::Renewed);
            object->effects.erase(object->effects.begin() + i);
        } else {
            ++i;
        }
    }
}

constexpr int kNoKey = -123;                    // the object asks for no key
constexpr float kNoSpeed = 0.0f;
constexpr float kPlainRadius = 0.75f;
constexpr std::uint16_t kVitalParts = 0x11;     // changed parts 0 and 4: the statistics are repacked

constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;

constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // MapObject::updateCountdown

template <ClassId Id> constexpr const char* kName = nullptr;
template <> constexpr const char* kName<ClassId::Tree> = "CTree";
template <> constexpr const char* kName<ClassId::Stone> = "CStone";
template <> constexpr const char* kName<ClassId::Building> = "CBuilding";
template <> constexpr const char* kName<ClassId::StuffObject> = "CStuffObject";

} // namespace

// --- Change tracking -------------------------------------------------------------------------

void MapObject::MarkChanged(NetField part) {
    if (part != NetField::None) netState.mask = static_cast<std::uint16_t>(netState.mask | (1u << (static_cast<int>(part) & 31)));
    flags |= MapObjectFlag::Changed;
    if (refCount == 0) return;
    if (World* world = OwningWorld()) changeStamp = world->updateCount;
}

bool MapObject::HasEffect(SpellType spellType) {
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect effect = effects[i];   // the whole record is copied
        if (effect.Behaviour() == spellType) return true;
    }
    return false;
}

bool MapObject::HasEffectKind(bool (*test)(const itemdb::SpellRecord& kind)) const {
    for (const MapEffect& effect : effects)
        if (const itemdb::SpellRecord* kind = effect.KindRecord(); kind && test(*kind)) return true;
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
    if (std::ssize(effects) >= kMaxEffects) return;   // RemoveOutdatedEffects keeps the list far below this
    effects.push_back(*effect);
    effects.back().serial = MapEffect::NewSerial();
    const MapEffect added = effects.back();
    MarkChanged(NetField::Effects);
    const bool scales = ScalesWith(effects.back());
    const bool stuns = EffectTotals()[EffectStat::Stunned] > 0.0f;
    ScaleEffectChanged(scales);
    if (stuns) SetStunned(1);
    EffectsChanged();
    if (scales) RecomputeScale();
    if (effect->ticksLeft > 1) RemoveOutdatedEffects(this, effect);
    EffectApplied(*this, added);
}

void MapObject::EffectRemoved(bool scales) {
    if (scales) RecomputeScale();
    MarkChanged(NetField::Effects);
    ScaleEffectChanged(scales);
    EffectsChanged();
    if (!(EffectTotals()[EffectStat::Stunned] > 0.0f)) SetStunned(0);
}

int MapObject::RemoveEffects(std::uint8_t record) {
    int removed = 0;
    for (std::size_t i = 0; i < effects.size();) {
        if (effects[i].type != record) {
            ++i;
            continue;
        }
        const bool scales = ScalesWith(effects[i]);
        const MapEffect gone = effects[i];
        effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(i));
        EffectRemoved(scales);
        EffectEnded(*this, gone, EffectEnd::Removed);
        ++removed;
    }
    return removed;
}

void MapObject::ClearEffects() {
    for (const MapEffect& effect : effects) EffectEnded(*this, effect, EffectEnd::Died);
    effects = {};
    MarkChanged(NetField::Effects);
    ScaleEffectChanged(true);
    SetStunned(0);
    RecomputeScale();
}

const EffectStats& MapObject::EffectTotals() {
    MapObjectServer* server = ServerCompanion();
    return server ? server->effectStats.Of(*this) : kNoEffectStats;
}

bool MapObject::ScalesWith(const MapEffect& effect) {
    MapObjectServer* server = ServerCompanion();
    if (!server) return false;
    server->effectStats.Of(*this);
    return server->effectStats.Touches(effect.Kind(),
                                       {EffectStat::Might, EffectStat::Frailty, EffectStat::Girth, EffectStat::Size});
}

void MapObject::RecomputeScale() {
    ApplyBaseScale();
    // The effects' steps: the original's levels of strengthen less weaken and enlarge less shrink.
    const EffectStats& totals = EffectTotals();
    const float strengthLevel = totals[EffectStat::Girth];
    const float size = totals[EffectStat::Size];
    float total = size + strengthLevel;
    if (size == 0.0f) {
        if (strengthLevel != 0.0f) {
            float step = strengthLevel * kScaleStep;
            scale.x = MaxKeepB(0.0f, step + scale.x);
            if (MapType() != MapObjectType::Hero) scale.z = MaxKeepB(0.0f, step + scale.z);
        }
    } else if (total != 0.0f) {
        float step = total * kScaleStep;
        scale.x = MaxKeepB(0.0f, step + scale.x);
        scale.y = MaxKeepB(0.0f, step + scale.y);
        scale.z = MaxKeepB(0.0f, step + scale.z);
    }
    MarkChanged(NetField::Scale);
}

Vector3 MapObject::GroundPoint() {
    const Vector3 centre = localPosition + bounds.center;
    return {centre.x, centre.y, OwningWorld()->GridHeightAt(centre.x, centre.y) + centre.z};
}

void MapObject::PlaceOnGround(float offset) {
    localPosition.z = OwningWorld()->GridHeightAt(localPosition.x, localPosition.y) + offset;
}

void MapObject::SetServerData(const UnitVitals* data) {
    ClassId classId = companion->GetClassId();
    if (classId != ClassId::MapObjectServer) return;
    ServerCompanion()->SetData(data);
}

void MapObject::AttachObject(EngineObject* object) {
    if (!object || SideTypeOf(object) != SideObjectType::MapObject) {
        Object3D::AttachObject(object);
        return;
    }
    SetCompanion(static_cast<MapObjectCompanion*>(object));
}

void MapObject::SetCompanion(MapObjectCompanion* newCompanion) {
    if (companion) companion->owner = nullptr;
    companion.Assign(newCompanion);
    if (newCompanion) newCompanion->owner = this;
}

int MapObject::StatsChanged() {
    if (companion->GetClassId() != ClassId::MapObjectServer) return 0;
    Vitals().Pack(&packedVitals);
    return 1;
}

bool MapObject::IsStunned() { return lib::Has(flags, MapObjectFlag::Stunned); }

// --- Lifetime ------------------------------------------------------------------------------------

MapObject::MapObject() {
    packedVitals.Reset();
}

MapObject::~MapObject() = default;

// --- Small virtuals: the ones a plain object leaves empty (units, levers, traps and torches override them) and
// the network state's write / read wrappers.

// --- Slots that read the object ------------------------------------------------------------------------

void MapObject::TakePrototype(std::string_view prototype) {
    // Zeroed: the builder leaves the vitals alone when the prototype exists (it wants statistics too), and the
    // original then packed what its stack held (a determinism fix).
    UnitVitals vitals{};
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, nullptr, prototype);
    vitals.Pack(&packedVitals);
}

void MapObject::WriteFullState() {
    DeltaState* state = &netState;
    state->SetWriteMode(DeltaWrite::Full);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

void MapObject::WriteChangedState() {
    if (netState.mask & kVitalParts) StatsChanged();
    DeltaState* state = &netState;
    state->SetWriteMode(DeltaWrite::Changes);
    WriteNetState();
    state->Finish();
    flags &= ~MapObjectFlag::Changed;
}

bool MapObject::ReadState(bool update, MemoryStream* reader) {
    netState.BeginRead(update, reader);
    bool complete = ReadNetState(update);
    netState.Finish();
    return complete;
}

// --- The virtuals a plain object answers -------------------------------------------------------------------

bool MapObject::InvisibleSlot() { return lib::Has(flags, MapObjectFlag::Invisible); }
void MapObject::EffectsChanged() {}
bool MapObject::IsSynchronised() { return lib::Has(flags, MapObjectFlag::Synchronised); }
void MapObject::ApplyBaseScale() { scale = baseScale; }
void MapObject::ReceiveHit(DamageInfo*, Unit*, Unit*) {}
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
Vector3 MapObject::EyePoint() { return GroundPoint(); }
Vector3 MapObject::PositionAt(int) { return localPosition; }
Vector3 MapObject::AimPoint() { return localPosition; }
bool MapObject::IsLargeUnit() { return false; }
bool MapObject::IsDead() { return companion->IsDead(); }
int MapObject::ForgetObject(EngineObject*) { return 0; }
void MapObject::UpdateMoveFactors() {}
void MapObject::RefreshWounds() {}
float MapObject::MoveSpeed() { return kNoSpeed; }
float MapObject::Radius() { return kPlainRadius; }
void MapObject::SetScriptName(std::string_view) {}

// --- Visibility and science -------------------------------------------------------------------------

// Of the unit and nine other map object classes: the visibility mode is handed to the companion
// (the client's shows or hides the object's light).
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

void MapObject::ClearProtectionBases() {
    if (companion->GetClassId() != ClassId::MapObjectServer) return;
    // The original reloads the companion pointer for each store (two stores per pair, both +0).
    for (UnitStatPair& pair : Vitals().protection) pair.base = 0.0f;
}

// --- Create ------------------------------------------------------------------------------------------

// Create of map objects, units and torches: the map object's Create, then the object's number of network
// fields. See unit.h (Unit::Create), torch.h (TorchObject::Create) and docs/notes/units.md
// ("Creation, databases, save").

// --- MapObject's Create / Destroy (A-U) ---------------------------------------------------------

void MapObject::Create() {
    Object3D::Create();
    SetCompanion(nullptr);
    packedVitals.Reset();
    animationSpeed = 1.0f;
    netState.SetFieldCount(kMapObjectNetFields);
    netState.Reset();
    changeStamp = -1;
    effects = {};
    updateCountdown = kInitialCountdown;
    name2.clear();
    baseScale.x = baseScale.y = baseScale.z = 0.0f;
}

void MapObject::Destroy() {
    SetEffectCount(0);
    SetCompanion(nullptr);
    Object3D::Destroy();
}

// --- Units and torches -----------------------------------------------------------------------------

void Unit::Create() {
    MapObject::Create();
    Reset();
    AsMapObject()->netState.SetFieldCount(kUnitNetFields);
}

void TorchObject::Create() {
    MapObject::Create();
    spec.Reset();
    DeltaState* state = &netState;
    state->SetFieldCount(kTorchNetFields);
    state->Reset();
}

// --- Save --------------------------------------------------------------------------------------------

// Saving and loading the map object part of every map object (MapObject Load / Save) and its
// server companion's (MapObjectServer Load / Save). See map_object.h and docs/notes/map_objects.md.

void MapObject::Load(ByteStream* stream) {
    companion->Load(stream);
    stream->ReadValue(animationSpeed);
    stream->Read(&localPosition.x, 3 * sizeof(float));
    stream->ReadValue(localRotation);
    name = stream->ReadString();
    stream->ReadValue(scale);
    SetEffectCount(stream->ReadCount());
    for (int i = 0; i < std::ssize(effects); ++i) effects[i].Load(stream);
    std::uint32_t currentFlags = flags;    // the original's local starts as stack garbage
    stream->ReadValue(currentFlags);
    flags = currentFlags;
    name2 = stream->ReadString();
    if (stream->version >= kVersionBaseScale) stream->ReadValue(baseScale);
}

void MapObject::Save(ByteStream* stream) {
    companion->Save(stream);
    float speed = animationSpeed;
    stream->WriteValue(speed);
    stream->Write(&localPosition.x, 3 * sizeof(float));
    stream->WriteValue(localRotation);
    stream->WriteString(name);
    stream->WriteValue(scale);
    stream->WriteCount(effects.size());
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
    for (int count = stream->ReadCount(); count > 0; --count) strings.push_back(stream->ReadString());
}

void MapObjectServer::Save(ByteStream* stream) {
    stream->WriteValue(vitals);
    stream->WriteString(parentIname);
    stream->WriteString(skinTexture);
    stream->WriteCount(strings.size());
    for (const std::string& text : strings) stream->WriteString(text);
}

// --- The plain classes -------------------------------------------------------------------------

// The member-less map object classes (objects/map_object.h, A-U6): CTree, CBuilding, CStone and CStuffObject.

template <ClassId Id>
PlainMapObject<Id>::PlainMapObject() {
    MapObject::Create();
}

template <ClassId Id>
PlainMapObject<Id>::~PlainMapObject() {
    MapObject::Destroy();
}

template <ClassId Id>
const char* PlainMapObject<Id>::ClassName() {
    return kName<Id>;
}

template <ClassId Id>
ClassId PlainMapObject<Id>::GetClassId() {
    return Id;
}

template class PlainMapObject<ClassId::Tree>;
template class PlainMapObject<ClassId::Stone>;
template class PlainMapObject<ClassId::Building>;
template class PlainMapObject<ClassId::StuffObject>;
