// The zone file's unit records (kind 5; map_spec.h) with their five behaviour records (0x4C bytes: patrols of
// waypoints and the guard settings), the small chunk classes that read and write those (waypoint, patrol,
// behaviour) and the patrol array's copy. See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "map_object.h"
#include "combat.h"
#include "map_object_figures.h"
#include "map_spec.h"
#include "core_streams.h"
#include "record_arrays.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/static_object.h"
#include "lib/crt.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include "lib/mfc_slots.h"
#include "lib/mfc_array.h"
#include "world_data.h"
#include "ei/vcall.h"
#include "game_data.h"
#include "game_singletons.h"
#include "world.h"

namespace mapspec {

template <typename T>
using Array = mfc::CArray<T>;


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

// CRT eh vector constructor / destructor iterators.
template <typename Constructor, typename Destructor>
void VectorConstruct(void* a0, unsigned a1, int a2, Constructor a3, Destructor a4) { libcrt::ConstructElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3), reinterpret_cast<libcrt::ElementFunction>(a4)); }
template <typename Destructor>
void VectorDestruct(void* a0, unsigned a1, int a2, Destructor a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
void PatrolsSetSize(Array<Patrol>* patrols, int size) { mfc::impl::ArrayAs<mfc::impl::OwnedPatrols>(*patrols).SetSize(size, -1); }
constexpr auto ConstructPatrols = mfc::impl::ConstructElementsAt<mfc::impl::Patrols>;
constexpr auto DestructPatrols = mfc::impl::DestructElementsAt<mfc::impl::OwnedPatrols>;
// A patrol's destructor (also with flags 0): its waypoint array destroyed by the array base class's
// destructor.
void DestroyPatrol(Patrol& p) {
    ei::SetVtable(&p.waypoints, mfc::impl::ArrayBaseClass::Vtable());
    if (p.waypoints.data) gamecrt::operator_delete(p.waypoints.data);
}
std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void ReadInto(Chunk& c, std::string& field) {
    mfc::CString read(mfc::CString::kUnset);
    field = c.ReadString(&read)->GetString();
}
void ReadVector(Chunk& c, Vector3& to) {
    Vector3 v;
    std::memcpy(&to, c.ReadVector3(&v), sizeof(Vector3));
}
void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

} // namespace

// ---- Behaviour records -------------------------------------------------------------------------

Behaviour* __fastcall BehaviourConstruct(Behaviour* self) {
    self->patrols.data = nullptr;
    self->patrols.growBy = 0;
    self->patrols.maxSize = 0;
    self->patrols.size = 0;
    ei::SetVtable(&self->patrols, mfc::impl::BehaviourPatrolsArrayClass::Vtable());
    self->type = 3;
    self->loop = 1;
    self->flags = 0;
    self->guardRadius = 10.0f;
    self->field0A = -1.0f;
    self->alarmKind = 0;
    self->enabled = 0;
    self->alertMode = 0;
    self->callRadius = 0x41200000u /*0073C378*/;
    self->alarmDelay = 75;
    self->alarm = 0;
    return self;
}
EI_REWRITTEN(0x0045D5B0, BehaviourConstruct);

// The destructor body (the patrol array's destructor inlined: each patrol's waypoint
// block freed, then the patrols' block).
void __fastcall BehaviourDestruct(Behaviour* self) {
    Patrol* patrols = self->patrols.data;
    ei::SetVtable(&self->patrols, mfc::impl::PatrolArrayClass::Vtable());
    if (!patrols) return;
    for (int i = 0, n = self->patrols.size; i < n; ++i) DestroyPatrol(patrols[i]);
    gamecrt::operator_delete(self->patrols.data);
}
EI_REWRITTEN(0x0046C290, BehaviourDestruct);

void Patrols::CopyFrom(const Patrols& from) {
    int n = from.size;
    if (n == 0) {
        if (data) {
            for (int i = 0, count = size; i < count; ++i) DestroyPatrol(data[i]);
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        maxSize = 0;
        size = 0;
    } else if (!data) {
        data = gamecrt::operator_new_array<Patrol>(static_cast<std::size_t>(n));
        ConstructPatrols(data, n);
        maxSize = n;
        size = n;
    } else if (n <= maxSize) {
        if (size < n) ConstructPatrols(data + size, n - size);
        else if (size > n) DestructPatrols(data + n, size - n);
        size = n;
    } else {
        int grow = growBy;
        if (grow == 0) {
            grow = size / 8;
            grow = grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
        }
        int capacity = maxSize + grow;
        if (n >= capacity) capacity = n;
        auto* grown = gamecrt::operator_new_array<Patrol>(static_cast<std::size_t>(capacity));
        std::memcpy(grown, data, static_cast<std::size_t>(size) * sizeof(Patrol));
        ConstructPatrols(grown + size, n - size);
        gamecrt::operator_delete(data);
        data = grown;
        size = n;
        maxSize = capacity;
    }
    for (int i = 0; i < from.size; ++i) {
        lib::InPlace<Patrol> raw;
        auto* copy = raw.Get();
        from.CopyAt(copy, i);
        Patrol& to = data[i];
        to.position = copy->position;
        to.waypoints.SetSize(copy->waypoints.size, -1);
        for (int j = 0; j < copy->waypoints.size; ++j) {
            Waypoint w = copy->waypoints.data[j];   // GetAt by value
            data[i].waypoints.data[j] = w;
        }
        ei::SetVtable(&copy->waypoints, mfc::impl::ArrayBaseClass::Vtable());
        if (copy->waypoints.data) gamecrt::operator_delete(copy->waypoints.data);
    }
}
EI_REWRITTEN(0x0046E680, Patrols::CopyFrom);

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
EI_REWRITTEN(0x0046C610, WaypointChunk::OnChunk);

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
    patrol->waypoints.SetSize(patrol->waypoints.size + 1, -1);
    patrol->waypoints.data[patrol->waypoints.size - 1].position = {0.0f, 0.0f, 0.0f};
    Patrol* again = target;
    child.target = &again->waypoints.data[patrol->waypoints.size - 1];
    child.ForEachChild();
}
EI_REWRITTEN(0x0046C7F0, PatrolChunk::OnChunk);

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
        PatrolsSetSize(&behaviour->patrols, behaviour->patrols.size + 1);
        behaviour->patrols.data[behaviour->patrols.size - 1].position = {0.0f, 0.0f, 0.0f};
        child.target = &target->patrols.data[behaviour->patrols.size - 1];
        child.ForEachChild();
        break;
    }
    default: break;
    }
}
EI_REWRITTEN(0x0046CBC0, BehaviourChunk::OnChunk);

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
    for (int i = 0; i < target->patrols.size; ++i) {
        Patrol* p = &target->patrols.data[i];
        patrolChunk.target = p;
        patrolChunk.BeginWrite(0xBBBD0000);
        WriteVector(patrolChunk, 0xBBBD0001, p->position);
        WaypointChunk waypointChunk(&patrolChunk);
        for (int j = 0; j < p->waypoints.size; ++j) {
            Waypoint* w = &p->waypoints.data[j];
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
EI_REWRITTEN(0x0046C900, BehaviourChunk::Write);


UnitSpec::UnitSpec() {
    VectorConstruct(behaviour, sizeof(Behaviour), 5, BehaviourConstruct, BehaviourDestruct);
    for (mfc::CStringArray& list : record.lists) list.Construct();
    record.applyStats = 0;
    kind = 5;
    prototype = "none";
    behaviours = 0;
    behaviour[0].enabled = 1;
    std::memset(&record.stats, 0, sizeof(record.stats));
}
EI_REWRITTEN_SPECIAL(0x0046C010, UnitSpec::UnitSpec);

// The prototype's name goes with the members, after the behaviour records (the original frees it
// before them).
UnitSpec::~UnitSpec() {
    for (int i = 4; i >= 0; --i) record.lists[i].Destruct();
    VectorDestruct(behaviour, sizeof(Behaviour), 5, BehaviourDestruct);
}
EI_REWRITTEN_SPECIAL(0x0046C140, UnitSpec::~UnitSpec);
EI_REWRITTEN_SPECIAL(0x0046C120, UnitSpec::~UnitSpec);

// The destructor of the block (the five string lists; reached only when an
// exception unwinds the constructor).
void __fastcall UnitRecordDestruct(unitrecord::UnitRecord* self) {
    for (int i = 4; i >= 0; --i) self->lists[i].Destruct();
}
EI_REWRITTEN(0x0046C2F0, UnitRecordDestruct);

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
EI_REWRITTEN(0x0046C4D0, UnitSpec::Apply);

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
EI_REWRITTEN(0x0046CF10, UnitSpec::Save);

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
EI_REWRITTEN(0x0046D080, UnitSpec::OnChunk);

// ---- Behaviour records in a stream (saved games) -----------------------------------------------

namespace {
void StreamRead(CStream* stream, void* data, unsigned size) { stream->Read(data, size); }
void StreamWrite(CStream* stream, const void* data, unsigned size) { stream->Write(data, size); }
} // namespace

// The count, the array sized to it (MFC SetSize inlined, new waypoints cleared), then the raw waypoints.
void __fastcall WaypointsRead(Array<Waypoint>* a, CStream* stream) {
    int n;
    StreamRead(stream, &n, 4);
    if (n == 0) {
        if (a->data) {
            gamecrt::operator_delete(a->data);
            a->data = nullptr;
        }
        a->maxSize = 0;
        a->size = 0;
    } else if (!a->data) {
        a->data = gamecrt::operator_new_array<Waypoint>(static_cast<std::size_t>(n));
        mfc::PlainElements<Waypoint>::Construct(a->data, n);
        a->maxSize = n;
        a->size = n;
    } else if (n <= a->maxSize) {
        if (n > a->size) mfc::PlainElements<Waypoint>::Construct(a->data + a->size, n - a->size);
        a->size = n;
    } else {
        int grow = a->growBy;
        if (grow == 0) {
            grow = a->size / 8;
            grow = grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
        }
        int capacity = a->maxSize + grow;
        if (n >= capacity) capacity = n;
        auto* data = gamecrt::operator_new_array<Waypoint>(static_cast<std::size_t>(capacity));
        std::memcpy(data, a->data, static_cast<std::size_t>(a->size) * sizeof(Waypoint));
        mfc::PlainElements<Waypoint>::Construct(data + a->size, n - a->size);
        gamecrt::operator_delete(a->data);
        a->size = n;
        a->data = data;
        a->maxSize = capacity;
    }
    for (int i = 0; i < a->size; ++i) StreamRead(stream, &a->data[i], sizeof(Waypoint));
}
EI_REWRITTEN(0x0045DE30, WaypointsRead);

// The count, then the raw waypoints.
void __fastcall WaypointsWrite(const Array<Waypoint>* a, CStream* stream) {
    int n = a->size;
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < a->size; ++i) StreamWrite(stream, &a->data[i], sizeof(Waypoint));
}
EI_REWRITTEN(0x0045DFB0, WaypointsWrite);

// The record from a stream. Files before version 0x53 have no alarm level: it follows
// from the flags (bit 0 -> 0, bit 1 -> 1, bit 2 -> 2, bit 4 -> 3, else 2). field0A is not stored.
void Behaviour::Read(CStream* stream) {
    StreamRead(stream, &type, 4);
    int n;
    StreamRead(stream, &n, 4);
    PatrolsSetSize(&patrols, n);
    for (int i = 0; i < patrols.size; ++i) {
        Patrol* p = &patrols.data[i];
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
EI_REWRITTEN(0x0045D610, Behaviour::Read);

// The record to a stream (the same fields).
void Behaviour::Write(CStream* stream) const {
    StreamWrite(stream, &type, 4);
    int n = patrols.size;
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < patrols.size; ++i) {
        const Patrol* p = &patrols.data[i];
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
EI_REWRITTEN(0x0045D760, Behaviour::Write);

} // namespace mapspec
