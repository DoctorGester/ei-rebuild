// The zone file's unit records (kind 5; map_spec.h) with their five behaviour records (0x4C bytes: patrols of
// waypoints and the guard settings), the small chunk classes that read and write those (waypoint, patrol,
// behaviour) and the patrol array's copy. See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "map_object.h"
#include "combat.h"
#include "map_object_figures.h"
#include "map_spec.h"
#include "core_streams.h"
#include "unit.h"

#include "lib/static_object.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include <string>
#include "world_data.h"
#include "game_singletons.h"
#include "world.h"

namespace mapspec {


// The small chunk classes: a Chunk and the value it reads or writes (the target is set after the
// constructor). OnChunk is the first virtual; the behaviour record's writer is called directly.
template <typename T>
class TargetChunk : public Chunk {
public:
    explicit TargetChunk(Chunk* parent) : Chunk(parent) {}
    T* target;
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
    void Write();
};

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void ReadInto(Chunk& c, std::string& field) {
    std::string read;
    field = c.ReadString(&read)->c_str();
}
void ReadVector(Chunk& c, Vector3& to) {
    Vector3 v;
    std::memcpy(&to, c.ReadVector3(&v), sizeof(Vector3));
}
void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

void StreamRead(CStream* stream, void* data, unsigned size) { stream->Read(data, size); }
void StreamWrite(CStream* stream, const void* data, unsigned size) { stream->Write(data, size); }

} // namespace

// ---- Behaviour records -------------------------------------------------------------------------




// ---- The chunk classes ---------------------------------------------------------------------------

// A waypoint's values (OnChunk).
void WaypointChunk::OnChunk() {
    Waypoint* w = target;
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBBE0001: ReadVector(c, target->position); break;
    case 0xBBBE0002: w->lookWait = static_cast<std::int16_t>(c.ReadInt()); break;
    case 0xBBBE0003: target->time = c.ReadFloat(); break;
    case 0xBBBE0004: target->stopFirst = c.ReadByte(); break;
    default: break;
    }
}

// A patrol's start, or a waypoint appended and read (OnChunk).
void PatrolChunk::OnChunk() {
    Chunk& c = *this;
    if (c.id_ == 0xBBBD0001) {
        ReadVector(c, target->position);
        return;
    }
    if (c.id_ != 0xBBBE0000) return;
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
    switch (c.id_) {
    case 0xBBBC0001:
        if (c.ReadByte() != 0 && static_cast<std::int32_t>(b->alarm) > 0) b->alarm = 0;
        break;
    case 0xBBBC0002: b->loop = c.ReadByte(); break;
    case 0xBBBC0003: b->type = c.ReadInt(); break;
    case 0xBBBC0004: b->guardRadius = c.ReadFloat(); break;
    case 0xBBBC0005: ReadVector(c, b->guard); break;
    case 0xBBBC0006: b->alarmKind = c.ReadByte(); break;
    case 0xBBBC0007: b->enabled = c.ReadByte(); break;
    case 0xBBBC0008:
        if (c.ReadByte() != 0 && static_cast<std::int32_t>(b->alarm) > 1) b->alarm = 1;
        break;
    case 0xBBBC0009:
        if (c.ReadByte() != 0 && static_cast<std::int32_t>(b->alarm) > 2) b->alarm = 2;
        break;
    case 0xBBBC000A: b->field0A = c.ReadFloat(); break;
    case 0xBBBC000B: b->alertMode = c.ReadByte(); break;
    case 0xBBBC000C: {
        float f = c.ReadFloat();
        std::memcpy(&b->callRadius, &f, 4);
        break;
    }
    case 0xBBBC000D:
        if (c.ReadByte() != 0) b->flags |= 8;
        else b->flags &= ~8u;
        break;
    case 0xBBBC000E: b->alarm = c.ReadByte(); break;
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

// Writes the behaviour record (chunk BBBC0000), each patrol a BBBD0000 chunk with its
// waypoints as BBBE0000 chunks.
void BehaviourChunk::Write() {
    Chunk& c = *this;
    Behaviour* b = target;
    c.BeginWrite(0xBBBC0000);
    c.WriteByte(0xBBBC000B, static_cast<std::uint8_t>(b->alertMode));
    c.WriteFloat(0xBBBC000C, b->callRadius);
    c.WriteByte(0xBBBC0002, static_cast<std::uint8_t>(b->loop));
    c.WriteByte(0xBBBC000E, static_cast<std::uint8_t>(b->alarm));
    c.WriteByte(0xBBBC000D, static_cast<std::uint8_t>((b->flags >> 3) & 1));
    c.WriteInt(0xBBBC0003, b->type);
    c.WriteFloat(0xBBBC0004, Bits(b->guardRadius));
    c.WriteFloat(0xBBBC000A, Bits(b->field0A));
    WriteVector(c, 0xBBBC0005, b->guard);
    c.WriteByte(0xBBBC0007, static_cast<std::uint8_t>(b->enabled));
    c.WriteByte(0xBBBC0006, static_cast<std::uint8_t>(b->alarmKind));
    PatrolChunk patrolChunk(this);
    for (int i = 0; i < std::ssize(target->patrols); ++i) {
        Patrol* p = &target->patrols[i];
        patrolChunk.target = p;
        patrolChunk.BeginWrite(0xBBBD0000);
        WriteVector(patrolChunk, 0xBBBD0001, p->position);
        WaypointChunk waypointChunk(&patrolChunk);
        for (int j = 0; j < std::ssize(p->waypoints); ++j) {
            Waypoint* w = &p->waypoints[j];
            waypointChunk.target = w;
            waypointChunk.BeginWrite(0xBBBE0000);
            WriteVector(waypointChunk, 0xBBBE0001, w->position);
            waypointChunk.WriteInt(0xBBBE0002, w->lookWait);
            waypointChunk.WriteFloat(0xBBBE0003, Bits(w->time));
            waypointChunk.WriteByte(0xBBBE0004, static_cast<std::uint8_t>(w->stopFirst));
            waypointChunk.EndWrite();
        }
        patrolChunk.EndWrite();
    }
    c.EndWrite();
}

UnitSpec::UnitSpec() {
    record.applyStats = 0;
    kind = 5;
    prototype = "none";
    behaviours = 0;
    behaviour[0].enabled = 1;
    std::memset(&record.stats, 0, sizeof(record.stats));
}

UnitSpec::~UnitSpec() = default;

CUnit* ObjectSpec::Unit() const { return static_cast<CUnit*>(object); }

void UnitSpec::Apply() {
    if (!object) {
        TheServerWorld()->CreateObject(this);
        return;
    }
    CUnit* unit = Unit();
    TheSectorMap()->Move(unit, &position);
    TheServerWorld()->AIMap()->TurnObject(unit, &rotation);
    std::memcpy(&unit->localRotation_, &rotation, 16);
    unit->SetFigureScale(&scale.x);
    std::memcpy(&unit->baseScale, &scale, 12);
    unit->ApplyBaseScale();
    std::uint32_t& flags = unit->flags_;
    if (!visible) flags |= 8;
    else flags &= ~8u;
    unit->ServerController()->ResyncWithUnit();
    unit->MarkChanged(1);
}

// The unit's fields, the common ones, then the five behaviour records (each given the unit's position first).
void UnitSpec::Save() {
    BeginWrite(0xBBBB0000);
    WriteByte(0xBBBB000A, static_cast<std::uint8_t>(record.applyStats));
    WriteString(0xBBBB0002, prototype);
    for (int i = 4; i >= 0; --i) WriteStringArray(0xBBBB0005 + i, &record.lists[i]);
    WriteBlob(0xBBBB0004, &record.stats, sizeof(record.stats));
    ObjectSpec::WriteFields();
    BehaviourChunk writer(this);
    for (Behaviour& each : behaviour) {
        each.guard = position;
        writer.target = &each;
        writer.Write();
    }
    EndWrite();
}

// A behaviour record chunk fills the next record (not bounded, as in the original); an alarm level
// left unset (4) becomes 0.
void UnitSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBBB0002: ReadInto(c, prototype); break;
    case 0xBBBB0004: c.ReadBytes(&record.stats, sizeof(record.stats)); break;
    case 0xBBBB0005:
    case 0xBBBB0006:
    case 0xBBBB0007:
    case 0xBBBB0008:
    case 0xBBBB0009: c.ReadStringArray(&record.lists[c.id_ - 0xBBBB0005]); break;
    case 0xBBBB000A: record.applyStats = c.ReadByte(); break;
    case 0xBBBC0000: {
        BehaviourChunk reader(this);
        int index = behaviours;
        behaviours = index + 1;
        Behaviour* b = &behaviour[index];
        reader.target = b;
        b->flags = 0;
        b->alarm = 4;
        reader.ForEachChild();
        if (b->alarm == 4) b->alarm = 0;
        break;
    }
    default: ObjectSpec::OnChunk(); break;
    }
}

// ---- Behaviour records in a stream (saved games) -----------------------------------------------

// The count, the array sized to it, then the raw waypoints.
void WaypointsRead(std::vector<Waypoint>* a, CStream* stream) {
    int n;
    StreamRead(stream, &n, 4);
    a->resize(n);
    for (Waypoint& w : *a) StreamRead(stream, &w, sizeof(Waypoint));
}

// The count, then the raw waypoints.
void WaypointsWrite(const std::vector<Waypoint>* a, CStream* stream) {
    int n = static_cast<int>(a->size());
    StreamWrite(stream, &n, 4);
    for (const Waypoint& w : *a) StreamWrite(stream, &w, sizeof(Waypoint));
}

// The record from a stream. Files before version 0x53 have no alarm level: it follows
// from the flags (bit 0 -> 0, bit 1 -> 1, bit 2 -> 2, bit 4 -> 3, else 2). field0A is not stored.
void Behaviour::Read(CStream* stream) {
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
    if (static_cast<std::int32_t>(stream->version_) >= 0x53) {
        StreamRead(stream, &alarm, 4);
        return;
    }
    std::uint32_t oldFlags = flags;
    alarm = 2;
    if (oldFlags & 1) alarm = 0;
    else if (oldFlags & 2) alarm = 1;
    else if (oldFlags & 4) alarm = 2;
    else if (oldFlags & 0x10) alarm = 3;
}

// The record to a stream (the same fields).
void Behaviour::Write(CStream* stream) const {
    StreamWrite(stream, &type, 4);
    int n = std::ssize(patrols);
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < std::ssize(patrols); ++i) {
        const Patrol* p = &patrols[i];
        StreamWrite(stream, &p->position, 12);
        WaypointsWrite(&p->waypoints, stream);
    }
    StreamWrite(stream, &guard, 12);
    std::uint32_t value;
    for (const void* field : std::initializer_list<const void*>{&loop, &flags, &guardRadius, &alarmKind, &enabled,
                                                                 &callRadius}) {
        std::memcpy(&value, field, 4);
        StreamWrite(stream, &value, 4);
    }
    StreamWrite(stream, &alertMode, 4);
    value = alarmDelay;
    StreamWrite(stream, &value, 4);
    StreamWrite(stream, &alarm, 4);
}

} // namespace mapspec
