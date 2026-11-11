// The lever map object's own functions: lifetime, update, save / load, network state, the zone record set-up
// and the small slots; the lever's state change and use spots; the global lever table and its load
// (docs/notes/units.md, "Levers").
#include "game/objects/lever.h"

#include "game/ai/ai_map.h"
#include "game/ai/sector_map.h"
#include "game/app/singletons.h"
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "engine/data/chunk_archive.h"
#include "engine/data/database_file.h"
#include "engine/data/database_toml.h"
#include "engine/data/resource.h"
#include "game/net/net_delta.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/sound/sound.h"
#include "game/world/world.h"

#include "lib/crt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <string>

namespace {
constexpr int kLeverNetFields = 6;
constexpr std::uint32_t kKeyById = 8;           // lock mask bit: the id names the key
constexpr float kNextStateDuration = -10.0f;   // SetState(-1, -10): the model's default
constexpr float kSoundNear = 8.0f;
constexpr float kSoundFar = 40.0f;

const char* const kLeverClassName = "CLeverObject";

// The tail the constructor and Create share (after the map object's Create).
void ResetLever(Lever* self) {
    self->rate = 0.0f;
    self->endTick = 0.0f;
    self->to = 0.0f;
    self->from = 0.0f;
    self->netState.SetFieldCount(kLeverNetFields);
    self->lock = LockKey{1, 0, 0};
    self->state = 0;
    self->stateCount = 1;
    self->cycling = 0;
    self->directUseBlocked = 0;
    self->keepsMapFlag = 0;
}
} // namespace

Lever::Lever() {
    lock = LockKey{1, 0, 0};
    MapObject::Create();
    ResetLever(this);
}

Lever::~Lever() { MapObject::Destroy(); }

void Lever::Create() {
    MapObject::Create();
    ResetLever(this);
}

bool Lever::RunTick() {
    Update();
    if (OwningWorld()->IsServer()) return false;
    Vector3 shown = scale;
    float t = (endTick - static_cast<float>(g_clientWorld.tick)) * rate;
    if (t > 0.0f) {
        shown.y = to - (to - from) * t;
        AsObject3D()->SetFigureScale(shown);
        g_clientWorld.RefreshPlatforms(this);
        return true;
    }
    if (shown.y != to) {
        shown.y = to;
        AsObject3D()->SetFigureScale(shown);
        g_clientWorld.RefreshPlatforms(this);
    }
    return false;
}

void Lever::Load(ByteStream* stream) {
    MapObject::Load(stream);
    stream->ReadValue(state);
    stream->ReadValue(stateCount);
    stream->ReadValue(cycling);
    lock.Load(stream);
    stream->ReadValue(directUseBlocked);
    stream->ReadValue(keepsMapFlag);
}

void Lever::Save(ByteStream* stream) {
    MapObject::Save(stream);
    std::uint32_t value = static_cast<std::uint32_t>(state);
    stream->WriteValue(value);
    value = static_cast<std::uint32_t>(stateCount);
    stream->WriteValue(value);
    value = static_cast<std::uint32_t>(cycling);
    stream->WriteValue(value);
    lock.Save(stream);
    std::uint8_t byte = directUseBlocked;
    stream->WriteValue(byte);
    byte = keepsMapFlag;
    stream->WriteValue(byte);
}

bool Lever::ApplyScience(const LockKey* key, EngineObject*) { return ApplyKey(key); }

void LockKey::Load(ByteStream* stream) {
    stream->ReadValue(mask);
    stream->ReadValue(id);
    stream->ReadValue(level);
}

void LockKey::Save(ByteStream* stream) const {
    for (std::uint32_t word : {mask, id, level}) stream->WriteValue(word);
}

void Lever::WriteNetState() {
    MapObject::WriteNetState();
    std::optional<DeltaWriter> field = netState.WriteField(NetField::LeverState);
    if (!field) return;
    if (to != scale.y) {
        rate = 0.0f;
        to = scale.y;
    }
    field->WriteDelta(&lock, sizeof(LockKey));
    for (float value : {from, to, endTick, rate}) field->WriteDelta(&value, 4);
    std::uint8_t disabled = lib::Has(flags, MapObjectFlag::Disabled);
    field->WriteDelta(&disabled, 1);
    field->End();
}

bool Lever::ReadNetState(bool update) {
    bool complete = MapObject::ReadNetState(update);
    std::optional<DeltaReader> field = netState.ReadField(NetField::LeverState);
    if (!field) return complete;
    field->ReadDelta(&lock, sizeof(LockKey));
    field->ReadDelta(&from, 4);
    field->ReadDelta(&to, 4);
    field->ReadDelta(&endTick, 4);
    field->ReadDelta(&rate, 4);
    std::uint8_t disabled;
    field->ReadDelta(&disabled, 1);
    complete &= field->SnapshotConsumed();
    field->End();
    if (disabled) flags |= MapObjectFlag::Disabled;
    else flags &= ~MapObjectFlag::Disabled;
    g_clientWorld.QueueSynchronised(this);
    if (update) {
        const LeverRecord* record = g_leverTable.Named(name);
        if (record && from != to)
            g_clientWorld.sounds->Play3D(record->sound, 0, localPosition, kSoundNear, kSoundFar, false);
    }
    return complete;
}

void Lever::SetUp(const LeverSetup* setup) {
    state = setup->state;
    stateCount = setup->stateCount;
    cycling = setup->cycling != 0 ? 1 : 0;
    lock = setup->lock;
    directUseBlocked = setup->directUseBlocked;
    keepsMapFlag = setup->keepsMapFlag;
    if (stateCount > 1) scale.y = static_cast<float>(state) / static_cast<float>(stateCount - 1);   // states < 256
    else scale.y = 0.0f;
    MarkChanged(NetField::Scale);
}

void Lever::Enable(bool on) {
    if (on) flags &= ~MapObjectFlag::Disabled;
    else flags |= MapObjectFlag::Disabled;
    MarkChanged(NetField::LeverState);
}

void Lever::SetLock(const LockKey* key) {
    lock = *key;
    MarkChanged(NetField::LeverState);
}

bool Lever::NeedsKey() {
    if (flags & MapObjectFlag::Disabled) return false;
    return lock.mask != 0;
}

std::uint32_t Lever::KeyId() { return (lock.mask & kKeyById) ? lock.id : 0; }

bool Lever::ApplyKey(const LockKey* key) {
    if (!key->Opens(&lock)) return false;
    SetState(-1, kNextStateDuration);
    flags |= MapObjectFlag::Disabled;
    MarkChanged(NetField::LeverState);
    return true;
}

bool Lever::KeyOpens(const LockKey* key) { return key->Opens(&lock); }

const char* Lever::ClassName() { return kLeverClassName; }
ClassId Lever::GetClassId() { return ClassId::Lever; }

Lever* LeverOrNull(EngineObject* object) {
    return object && object->GetClassId() == ClassId::Lever ? static_cast<Lever*>(object) : nullptr;
}

// --- State change and use spots ----------------------------------------------------------------

// A lever's state change and the spots it is used from. See
// docs/notes/units.md ("Levers").

void Lever::SetState(int wanted, float duration) {
    // A negative duration takes the model's default.
    if (duration < 0.0f) {
        const LeverRecord* record = g_leverTable.Named(name);
        duration = record ? static_cast<float>(static_cast<std::int64_t>(record->duration)) : 10.0f;
    }
    int& current = state;
    int states = stateCount;
    if (wanted < 0) {
        if (cycling != 0) {
            current = (current + 1) % states;
        } else {
            int next = current + 1;
            current = next < states - 1 ? next : states - 1;
        }
    } else {
        int chosen = wanted < states - 1 ? wanted : states - 1;
        current = chosen <= 0 ? 0 : chosen;
    }

    auto* world = ServerWorld();
    if (!world) return;   // on the client
    SectorMap* sectors = &world->aiMap.sectorMap;
    float reach = bounds.radius + 0.5f;
    std::vector<Unit*> around = sectors->FindUnitsInRadius(localPosition, reach);
    for (Unit* unit : around) sectors->Remove(unit);
    AIMap* map = &world->aiMap;
    map->refreshUnits = keepsMapFlag;   // the byte as it is
    map->BeginRefresh();
    sectors->Remove(this);

    states = stateCount;
    float travel = states > 1 ? static_cast<float>(state) / static_cast<float>(states - 1) : 0.0f;
    if (duration > 0.0f) {
        to = travel;
        from = scale.y;   // bit for bit
        endTick = (static_cast<float>(world->tick) + duration) + 1.0f;
        rate = 1.0f / duration;
    } else {
        to = travel;
        from = travel;
        endTick = 0.0f;
        rate = 0.0f;
    }
    scale.y = travel;   // the shown position
    SetUpAnimatedFigure(name);
    MarkChanged(NetField::LeverState);
    sectors->Add(this);
    map->EndRefresh();
    map->refreshUnits = true;

    for (auto node = around.begin(); node != around.end();) {
        auto next = std::next(node);
        Unit* unit = *node;
        if (!map->CanLeaveCell(unit)) {
            Vector3* at = &unit->localPosition;
            Vector3 free = {at->x, at->y, at->z};
            map->FindFreeCellNear(unit, *at, &free);
            *at = free;
            unit->ServerController()->ResyncWithUnit();
        }
        sectors->Add(unit);
        node = next;
    }
    around.clear();
}

std::vector<Vector3> Lever::UseSpots(float userRadius) {
    std::vector<Vector3> spots;
    const LeverRecord* record = g_leverTable.Named(name);
    if (!record || record->spotCount == 0) return spots;
    const Quaternion& facing = localRotation;
    // The record holds two spots; a larger count uses those.
    const std::uint32_t count = std::min<std::uint32_t>(record->spotCount, std::size(record->spots));
    for (std::uint32_t i = 0; i < count; ++i) {
        const float* spot = record->spots[i];
        Vector3 at = localPosition;
        float along = spot[0];
        float across = spot[1];
        float length = std::sqrt(across * across + along * along);
        if (length > 0.0f) {
            along /= length;
            across /= length;
        }
        float phase = scale.z;
        float distance = ((1.0f - phase) + phase * record->farScale) * length + userRadius;
        Vector3 axis = facing.AxisX();
        at += axis * along * distance;
        axis = facing.AxisY();
        at += axis * across * distance;
        spots.push_back(at);
    }
    return spots;
}

// --- The lever table ---------------------------------------------------------------------------

// The global lever table that levers look up (loaded from WinMain by the Rebirth-patched loader): the records
// and their index by name. See docs/notes/app.md.

LeverRecordTable g_leverTable;

// The record's fields 0 .. 7 (strings 0, 5, 6, 7).
void LeverRecord::Archive(ChunkArchive* archive) {
    archive->StringField(0, &name);
    archive->Field(1, spots, 0x10);
    archive->Field(2, &spotCount);
    archive->Field(3, &farScale);
    archive->Field(4, &duration);
    archive->StringField(5, &unreadTextFirst);
    archive->StringField(6, &sound);
    archive->StringField(7, &unreadTextLast);
}

void ChunkArchive::SerializeLevers(int id, std::vector<LeverRecord>* list) {
    RecordChunks(this, id, list, &LeverRecord::Archive);
}

// --- Load --------------------------------------------------------------------------------------------

// The lever table's load from its archive at start-up (the application's start-up calls it). Main thread. See docs/notes/units.md ("Levers").

int LeverRecordTable::Load(const std::string& folder) {
    MappedResource resource;
    dbfile::OpenDatabase(resource, folder);
    const RecordFile file = resource.FindRecord("Levers.ldb");
    if (!file.Any()) game::FatalExit("Can't open Levers database\n");
    resource.CloseResource();
    Read(file);
    return 1;
}

void LeverRecordTable::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream local;
        local.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        ChunkArchive archive;
        archive.Open(&local, 1, 1);
        archive.SerializeLevers(1, &records);
        archive.Close(&local);
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    for (LeverRecord& record : records) byName[record.name] = &record;
}

// --- As TOML ---------------------------------------------------------------------------------------

// A record's spots as TOML: the list of its spotCount spots, each [along, across] (the bytes of the others are not
// written: Lever::UseSpots reads only the first spotCount); read back, the count is the list's length.
struct LeverSpots {
    float (&spots)[2][2];
    std::uint32_t& count;
};

std::string ValueText(LeverSpots field) {
    std::vector<std::string> spots;
    for (std::uint32_t i = 0; i < std::min<std::uint32_t>(field.count, std::size(field.spots)); ++i)
        spots.push_back(dbtoml::ValueText(field.spots[i]));
    return toml::Array(spots);
}

bool ReadValue(const toml::Value& value, LeverSpots field) {
    float spots[2][2] = {};
    if (!dbtoml::ReadValue(value, spots)) return false;
    std::copy(&spots[0][0], &spots[0][0] + 4, &field.spots[0][0]);
    field.count = static_cast<std::uint32_t>(value.Size());
    return true;
}

template <class Visit>
void Fields(LeverRecord& r, Visit& v) {
    v("name", r.name);
    v("spots", LeverSpots{r.spots, r.spotCount});
    v("far_scale", r.farScale);
    v("duration", r.duration);
    v("sound", r.sound);
    v.Unread("unread_text_first", r.unreadTextFirst);
    v.Unread("unread_text_last", r.unreadTextLast);
}

namespace {

constexpr const char* kLevers = "Levers.ldb";

} // namespace

void LeverRecordTable::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["lever"], records, "name", [](const LeverRecord& record) { return record.name; }, kLevers,
                         path);
    dbtoml::CheckTables(root, {"lever"}, kLevers, path);
}

std::string LeverRecordTable::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "lever", records, "name");
    return writer.text;
}
