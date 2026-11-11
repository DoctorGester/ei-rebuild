// The zone file's unit records (SpecKind::Unit; map_spec.h) with their five behaviour records (0x4C bytes: patrols of
// waypoints and the guard settings), the small chunk classes that read those (waypoint, patrol,
// behaviour) and the patrol array's copy. See docs/notes/objects.md, "Zone files and unit records".
#include "game/ai/ai_map.h"
#include "game/objects/map_object.h"
#include "game/combat/combat.h"
#include "game/objects/object3d.h"
#include "game/map/map_spec.h"
#include "engine/core/core_streams.h"
#include "game/units/unit.h"

#include "lib/bytes.h"
#include "lib/containers.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include <string>
#include "game/app/singletons.h"
#include "game/world/world.h"
#include "game/saves/save_versions.h"

namespace mapspec {


// The small chunk classes: a Chunk and the value it reads (the target is set after the constructor).
template <typename T>
class TargetChunk : public Chunk {
public:
    explicit TargetChunk(Chunk* parent) : Chunk(parent) {}
    T* target = nullptr;
};
// A waypoint, a patrol and a behaviour record.
class WaypointChunk : public TargetChunk<Waypoint> {
public:
    using TargetChunk::TargetChunk;
    void OnChunk() override;
};
class PatrolChunk : public TargetChunk<Patrol> {
public:
    using TargetChunk::TargetChunk;
    void OnChunk() override;
};
class BehaviourChunk : public TargetChunk<Behaviour> {
public:
    using TargetChunk::TargetChunk;
    void OnChunk() override;
};

namespace {

void StreamRead(ByteStream* stream, void* data, unsigned size) { stream->Read(data, size); }
void StreamWrite(ByteStream* stream, const void* data, unsigned size) { stream->Write(data, size); }

} // namespace

// ---- Behaviour records -------------------------------------------------------------------------




// ---- The chunk classes ---------------------------------------------------------------------------

// A waypoint's values (OnChunk).
void WaypointChunk::OnChunk() {
    Waypoint* w = target;
    Chunk& c = *this;
    switch (c.id) {
    case 0xBBBE0001: target->position = c.ReadVector3(); break;
    case 0xBBBE0002: w->lookWait = static_cast<std::int16_t>(c.ReadInt()); break;
    case 0xBBBE0003: target->time = c.ReadFloat(); break;
    case 0xBBBE0004: target->stopFirst = c.ReadByte(); break;
    default: break;
    }
}

// A patrol's start, or a waypoint appended and read (OnChunk).
void PatrolChunk::OnChunk() {
    Chunk& c = *this;
    if (c.id == 0xBBBD0001) {
        target->position = c.ReadVector3();
        return;
    }
    if (c.id != 0xBBBE0000) return;
    WaypointChunk child(this);
    Patrol* patrol = target;
    patrol->waypoints.resize(std::ssize(patrol->waypoints) + 1);
    patrol->waypoints[std::ssize(patrol->waypoints) - 1].position = {0.0f, 0.0f, 0.0f};
    Patrol* again = target;
    child.target = &again->waypoints[std::ssize(patrol->waypoints) - 1];
    child.ForEachChild();
}

// A behaviour record's values, or a patrol appended and read (OnChunk). The three alarm flags only lower the
// level (0, 1, 2) when set.
void BehaviourChunk::OnChunk() {
    Chunk& c = *this;
    Behaviour* b = target;
    switch (c.id) {
    case 0xBBBC0001:
        if (c.ReadByte() != 0 && b->alarm > Reaction::Fight) b->alarm = Reaction::Fight;
        break;
    case 0xBBBC0002: b->loop = c.ReadByte(); break;
    case 0xBBBC0003: b->type = static_cast<BehaviourType>(c.ReadInt()); break;
    case 0xBBBC0004: b->guardRadius = c.ReadFloat(); break;
    case 0xBBBC0005: b->guard = c.ReadVector3(); break;
    case 0xBBBC0006: b->alarmKind = c.ReadByte(); break;
    case 0xBBBC0007: b->enabled = c.ReadByte(); break;
    case 0xBBBC0008:
        if (c.ReadByte() != 0 && b->alarm > Reaction::Revenge) b->alarm = Reaction::Revenge;
        break;
    case 0xBBBC0009:
        if (c.ReadByte() != 0 && b->alarm > Reaction::Watch) b->alarm = Reaction::Watch;
        break;
    case 0xBBBC000A: b->unreadChunk = c.ReadFloat(); break;
    case 0xBBBC000B: b->alertMode = static_cast<AlertMode>(c.ReadByte()); break;
    case 0xBBBC000C: {
        b->callRadius = c.ReadFloat();
        break;
    }
    case 0xBBBC000D:
        if (c.ReadByte() != 0) b->flags |= BehaviourFlag::AlwaysUpdated;
        else b->flags &= ~BehaviourFlag::AlwaysUpdated;
        break;
    case 0xBBBC000E: b->alarm = static_cast<Reaction>(c.ReadByte()); break;
    case 0xBBBD0000: {
        PatrolChunk child(this);
        Behaviour* behaviour = target;
        child.target = &behaviour->patrols.emplace_back();
        child.ForEachChild();
        break;
    }
    default: break;
    }
}

UnitSpec::UnitSpec() {
    record.applyStats = 0;
    kind = SpecKind::Unit;
    prototype = "none";
    behaviour[0].enabled = 1;
    std::memset(&record.stats, 0, sizeof(record.stats));
}

UnitSpec::~UnitSpec() = default;

// A behaviour record chunk fills the next record (not bounded, as in the original); an alarm level
// left unset (4) becomes 0.
void UnitSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xBBBB0002: prototype = c.ReadString(); break;
    case 0xBBBB0004: c.ReadBytes(&record.stats, sizeof(record.stats)); break;
    case 0xBBBB0005:
    case 0xBBBB0006:
    case 0xBBBB0007:
    case 0xBBBB0008:
    case 0xBBBB0009: record.lists[c.id - 0xBBBB0005] = c.ReadStringArray(); break;
    case 0xBBBB000A: record.applyStats = c.ReadByte(); break;
    case 0xBBBC0000: {
        BehaviourChunk reader(this);
        int index = behaviours;
        behaviours = index + 1;
        Behaviour* b = &behaviour[index];
        reader.target = b;
        b->flags = 0;
        b->alarm = Reaction::Unset;
        reader.ForEachChild();
        if (b->alarm == Reaction::Unset) b->alarm = Reaction::Fight;
        break;
    }
    default: ObjectSpec::OnChunk(); break;
    }
}

// ---- Behaviour records in a stream (saved games) -----------------------------------------------

// The count, the array sized to it, then the raw waypoints.
void WaypointsRead(std::vector<Waypoint>* waypoints, ByteStream* stream) {
    int n;
    StreamRead(stream, &n, 4);
    waypoints->resize(n);
    for (Waypoint& w : *waypoints) StreamRead(stream, &w, sizeof(Waypoint));
}

// The count, then the raw waypoints.
void WaypointsWrite(const std::vector<Waypoint>& waypoints, ByteStream* stream) {
    int n = static_cast<int>(waypoints.size());
    StreamWrite(stream, &n, 4);
    for (const Waypoint& w : waypoints) StreamWrite(stream, &w, sizeof(Waypoint));
}

// The record from a stream. Files before version 0x53 have no alarm level: it follows
// from the flags (bit 0 -> 0, bit 1 -> 1, bit 2 -> 2, bit 4 -> 3, else 2). unreadChunk is not stored.
void Behaviour::Read(ByteStream* stream) {
    StreamRead(stream, &type, 4);
    int n;
    StreamRead(stream, &n, 4);
    patrols.resize(n);
    for (int i = 0; i < std::ssize(patrols); ++i) {
        Patrol* p = &patrols[i];
        StreamRead(stream, &p->position, 12);
        WaypointsRead(&p->waypoints, stream);
    }
    StreamRead(stream, &guard, 12);
    StreamRead(stream, &loop, 4);
    StreamRead(stream, &flags, 4);
    StreamRead(stream, &guardRadius, 4);
    StreamRead(stream, &alarmKind, 4);
    StreamRead(stream, &enabled, 4);
    StreamRead(stream, &callRadius, 4);
    StreamRead(stream, &alertMode, 4);
    StreamRead(stream, &alarmDelay, 4);
    if (stream->version >= kVersionUnitAlarm) {
        StreamRead(stream, &alarm, 4);
        return;
    }
    alarm = Reaction::Watch;
    if (flags & BehaviourFlag::OldFight) alarm = Reaction::Fight;
    else if (flags & BehaviourFlag::OldRevenge) alarm = Reaction::Revenge;
    else if (flags & BehaviourFlag::OldWatch) alarm = Reaction::Watch;
    else if (flags & BehaviourFlag::OldWatchFearful) alarm = Reaction::WatchFearful;
}

// The record to a stream (the same fields).
void Behaviour::Write(ByteStream* stream) const {
    StreamWrite(stream, &type, 4);
    int n = lib::Count(patrols);
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < std::ssize(patrols); ++i) {
        const Patrol* p = &patrols[i];
        StreamWrite(stream, &p->position, 12);
        WaypointsWrite(p->waypoints, stream);
    }
    StreamWrite(stream, &guard, 12);
    std::uint32_t value;
    for (const void* field : std::initializer_list<const void*>{&loop, &flags, &guardRadius, &alarmKind, &enabled,
                                                                 &callRadius}) {
        value = lib::Load<std::uint32_t>(field);
        StreamWrite(stream, &value, 4);
    }
    StreamWrite(stream, &alertMode, 4);
    value = alarmDelay;
    StreamWrite(stream, &value, 4);
    StreamWrite(stream, &alarm, 4);
}

} // namespace mapspec
