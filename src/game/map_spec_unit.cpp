// The zone file's unit records (vtable 0073C6E0, kind 5; map_spec.h) with their five behaviour
// records (0x4C bytes: patrols of waypoints and the guard settings), the small chunk classes that
// read and write those (vtables 0073C710 waypoint, 0073C718 patrol, 0073C720 behaviour) and the
// patrol array's copy. See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "map_object.h"
#include "combat.h"
#include "map_spec.h"
#include "core_streams.h"
#include "record_arrays.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "lib/mfc_slots.h"
#include "lib/mfc_array.h"
#include "world_data.h"
#include "ei/vcall.h"

namespace mapspec {

template <typename T>
using Array = mfc::CArray<T>;


// UnitSpec (vtable 0073C6E0, 0x324 bytes): map_spec.h.

// The small chunk classes: a Chunk and the value it reads or writes.
struct TargetChunk {
    Chunk chunk;
    void* target;                // +0x18
};
static_assert(sizeof(TargetChunk) == 0x1C);
// The three chunk classes on it, by vtable: a waypoint, a patrol and a behaviour record.
struct WaypointChunk : TargetChunk {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073C710);
};
struct PatrolChunk : TargetChunk {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073C718);
};
struct BehaviourChunk : TargetChunk {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073C720);
};
static_assert(sizeof(WaypointChunk) == sizeof(TargetChunk) && sizeof(PatrolChunk) == sizeof(TargetChunk) &&
              sizeof(BehaviourChunk) == sizeof(TargetChunk));

namespace {

// CRT eh vector constructor / destructor iterators.
template <typename Constructor, typename Destructor>
void VectorConstruct(void* a0, unsigned a1, int a2, Constructor a3, Destructor a4) { libcrt::ConstructElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3), reinterpret_cast<libcrt::ElementFunction>(a4)); }
template <typename Destructor>
void VectorDestruct(void* a0, unsigned a1, int a2, Destructor a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
// MFC (library): CStringArray, the CArray instantiations of patrols and waypoints.
// SetSize 0045DAE0, ConstructElements 0045E180, DestructElements 0046F0B0.
void PatrolsSetSize(Array<Patrol>* patrols, int size) { mfc::impl::ArrayAs<mfc::impl::OwnedPatrols>(*patrols).SetSize(size, -1); }
constexpr auto ConstructPatrols = mfc::impl::ConstructElementsAt<mfc::impl::Patrols>;
constexpr auto DestructPatrols = mfc::impl::DestructElementsAt<mfc::impl::OwnedPatrols>;
// A patrol's destructor (0045E1E0, also 0046F160 with flags 0): its waypoint array destroyed by the
// array base class's destructor.
void DestroyPatrol(Patrol& p) {
    ei::SetVtable(&p.waypoints, mfc::impl::ArrayBaseClass::Vtable());
    if (p.waypoints.data) gamecrt::operator_delete(p.waypoints.data);
}
// The world server's sector map (007B01F4, world + 0x170 + 0x124) and the selection (007B00D0) move and turn an
// object; the world object's updates.

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

void ReadInto(Chunk& c, mfc::CString& field) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* read = c.ReadString(reinterpret_cast<mfc::CString*>(raw));
    field = *read;
    read->~CString();
}
void ReadVector(Chunk& c, Vector3& to) {
    Vector3 v;
    std::memcpy(&to, c.ReadVector3(&v), sizeof(Vector3));
}
void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

// A child chunk object of its class for `parent`.
template <typename ChunkClass>
void StartChild(ChunkClass& child, Chunk* parent) {
    child.chunk.Construct(nullptr);
    ei::SetVtable(&child, ChunkClass::kVtableAddress);
    child.chunk.parent_ = parent;
}

} // namespace

// ---- Behaviour records -------------------------------------------------------------------------

// 0045D5B0. Returns this.
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
    self->callRadius = *reinterpret_cast<const std::uint32_t*>(0x0073C378);
    self->alarmDelay = 75;
    self->alarm = 0;
    return self;
}
EI_REPLACE(0x0045D5B0, BehaviourConstruct);

// 0046C290: the destructor body (the patrol array's destructor inlined: each patrol's waypoint
// block freed, then the patrols' block).
void __fastcall BehaviourDestruct(Behaviour* self) {
    Patrol* patrols = self->patrols.data;
    ei::SetVtable(&self->patrols, mfc::impl::PatrolArrayClass::Vtable());
    if (!patrols) return;
    for (int i = 0, n = self->patrols.size; i < n; ++i) DestroyPatrol(patrols[i]);
    gamecrt::operator_delete(self->patrols.data);
}
EI_REPLACE(0x0046C290, BehaviourDestruct);

// 0046E680: the patrol array becomes a copy of `from` (each patrol copied through a temporary
// returned by value, as the original does).
void __fastcall PatrolsCopy(Array<Patrol>* self, void*, const Array<Patrol>* from) {
    int n = from->size;
    if (n == 0) {
        if (self->data) {
            for (int i = 0, count = self->size; i < count; ++i) DestroyPatrol(self->data[i]);
            gamecrt::operator_delete(self->data);
            self->data = nullptr;
        }
        self->maxSize = 0;
        self->size = 0;
    } else if (!self->data) {
        self->data = static_cast<Patrol*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(Patrol)));
        ConstructPatrols(self->data, n);
        self->maxSize = n;
        self->size = n;
    } else if (n <= self->maxSize) {
        if (self->size < n) ConstructPatrols(self->data + self->size, n - self->size);
        else if (self->size > n) DestructPatrols(self->data + n, self->size - n);
        self->size = n;
    } else {
        int grow = self->growBy;
        if (grow == 0) {
            grow = self->size / 8;
            grow = grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
        }
        int capacity = self->maxSize + grow;
        if (n >= capacity) capacity = n;
        auto* data = static_cast<Patrol*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * sizeof(Patrol)));
        std::memcpy(data, self->data, static_cast<std::size_t>(self->size) * sizeof(Patrol));
        ConstructPatrols(data + self->size, n - self->size);
        gamecrt::operator_delete(self->data);
        self->data = data;
        self->size = n;
        self->maxSize = capacity;
    }
    for (int i = 0; i < from->size; ++i) {
        alignas(4) std::uint8_t raw[sizeof(Patrol)];
        auto* copy = reinterpret_cast<Patrol*>(raw);
        recordarrays::PatrolGetAt(from, nullptr, copy, i);
        Patrol& to = self->data[i];
        to.position = copy->position;
        to.waypoints.SetSize(copy->waypoints.size, -1);
        for (int j = 0; j < copy->waypoints.size; ++j) {
            Waypoint w = copy->waypoints.data[j];   // GetAt by value (0046EFA0)
            self->data[i].waypoints.data[j] = w;
        }
        ei::SetVtable(&copy->waypoints, mfc::impl::ArrayBaseClass::Vtable());
        if (copy->waypoints.data) gamecrt::operator_delete(copy->waypoints.data);
    }
}
EI_REPLACE(0x0046E680, PatrolsCopy);

// ---- The chunk classes ---------------------------------------------------------------------------

// 0046C610 (0073C710 slot 0): a waypoint's values.
void __fastcall WaypointChunkOnChunk(TargetChunk* self) {
    auto* w = static_cast<Waypoint*>(self->target);
    Chunk& c = self->chunk;
    switch (c.id_) {
    case 0xBBBE0001: ReadVector(c, static_cast<Waypoint*>(self->target)->position); break;
    case 0xBBBE0002: w->lookWait = static_cast<std::int16_t>(c.ReadInt()); break;
    case 0xBBBE0003: static_cast<Waypoint*>(self->target)->time = c.ReadFloat(); break;
    case 0xBBBE0004: static_cast<Waypoint*>(self->target)->stopFirst = c.ReadByte(); break;
    default: break;
    }
}
EI_REPLACE(0x0046C610, WaypointChunkOnChunk);

// 0046C7F0 (0073C718 slot 0): a patrol's start, or a waypoint appended and read.
void __fastcall PatrolChunkOnChunk(TargetChunk* self) {
    Chunk& c = self->chunk;
    if (c.id_ == 0xBBBD0001) {
        ReadVector(c, static_cast<Patrol*>(self->target)->position);
        return;
    }
    if (c.id_ != 0xBBBE0000) return;
    WaypointChunk child;
    StartChild(child, &self->chunk);
    auto* patrol = static_cast<Patrol*>(self->target);
    patrol->waypoints.SetSize(patrol->waypoints.size + 1, -1);
    patrol->waypoints.data[patrol->waypoints.size - 1].position = {0.0f, 0.0f, 0.0f};
    auto* again = static_cast<Patrol*>(self->target);
    child.target = &again->waypoints.data[patrol->waypoints.size - 1];
    child.chunk.ForEachChild();
    child.chunk.Destruct();
}
EI_REPLACE(0x0046C7F0, PatrolChunkOnChunk);

// 0046CBC0 (0073C720 slot 0): a behaviour record's values, or a patrol appended and read. The
// three alarm flags only lower the level (0, 1, 2) when set.
void __fastcall BehaviourChunkOnChunk(TargetChunk* self) {
    Chunk& c = self->chunk;
    auto* b = static_cast<Behaviour*>(self->target);
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
        PatrolChunk child;
        StartChild(child, &self->chunk);
        auto* behaviour = static_cast<Behaviour*>(self->target);
        PatrolsSetSize(&behaviour->patrols, behaviour->patrols.size + 1);
        behaviour->patrols.data[behaviour->patrols.size - 1].position = {0.0f, 0.0f, 0.0f};
        child.target = &static_cast<Behaviour*>(self->target)->patrols.data[behaviour->patrols.size - 1];
        child.chunk.ForEachChild();
        child.chunk.Destruct();
        break;
    }
    default: break;
    }
}
EI_REPLACE(0x0046CBC0, BehaviourChunkOnChunk);

// 0046C900: writes the behaviour record (chunk BBBC0000), each patrol a BBBD0000 chunk with its
// waypoints as BBBE0000 chunks.
void __fastcall BehaviourWrite(TargetChunk* self) {
    Chunk& c = self->chunk;
    auto* b = static_cast<Behaviour*>(self->target);
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
    PatrolChunk patrolChunk;
    patrolChunk.chunk.Construct(nullptr);
    ei::SetVtable(&patrolChunk, PatrolChunk::kVtableAddress);
    patrolChunk.chunk.parent_ = &self->chunk;
    for (int i = 0; i < static_cast<Behaviour*>(self->target)->patrols.size; ++i) {
        Patrol* p = &static_cast<Behaviour*>(self->target)->patrols.data[i];
        patrolChunk.target = p;
        patrolChunk.chunk.BeginWrite(0xBBBD0000);
        WriteVector(patrolChunk.chunk, 0xBBBD0001, p->position);
        WaypointChunk waypointChunk;
        waypointChunk.chunk.Construct(nullptr);
        ei::SetVtable(&waypointChunk, WaypointChunk::kVtableAddress);
        waypointChunk.chunk.parent_ = &patrolChunk.chunk;
        for (int j = 0; j < p->waypoints.size; ++j) {
            Waypoint* w = &p->waypoints.data[j];
            waypointChunk.target = w;
            waypointChunk.chunk.BeginWrite(0xBBBE0000);
            WriteVector(waypointChunk.chunk, 0xBBBE0001, w->position);
            waypointChunk.chunk.WriteInt(0xBBBE0002, w->lookWait);
            waypointChunk.chunk.WriteFloat(0xBBBE0003, Bits(w->time));
            waypointChunk.chunk.WriteByte(0xBBBE0004, static_cast<std::uint8_t>(w->stopFirst));
            waypointChunk.chunk.EndWrite();
        }
        patrolChunk.chunk.EndWrite();
        waypointChunk.chunk.Destruct();
    }
    c.EndWrite();
    patrolChunk.chunk.Destruct();
}
EI_REPLACE(0x0046C900, BehaviourWrite);

// ---- Units (0073C6E0) ------------------------------------------------------------------------------

// 0046C010. Returns this.
UnitSpec* __fastcall UnitSpecConstruct(UnitSpec* self) {
    self->object.ObjectSpecConstruct();
    VectorConstruct(self->behaviour, sizeof(Behaviour), 5, BehaviourConstruct, BehaviourDestruct);
    new (&self->prototype) mfc::CString();
    for (mfc::CStringArray& list : self->lists) list.Construct();
    self->field0A = 0;
    ei::SetVtable(self, worlddata::kUnitSpecVtable);
    self->object.base.kind = 5;
    self->prototype = "none";
    self->behaviours = 0;
    self->behaviour[0].enabled = 1;
    std::memset(self->stats, 0, sizeof(self->stats));
    return self;
}
EI_REPLACE(0x0046C010, UnitSpecConstruct);

const void* UnitSpecBehaviours(const UnitSpec* spec) { return spec->behaviour; }

// 0046C140: the destructor body.
void __fastcall UnitSpecDestruct(UnitSpec* self) {
    ei::SetVtable(self, worlddata::kUnitSpecVtable);
    for (int i = 4; i >= 0; --i) StringArrayDestruct(&self->lists[i]);
    self->prototype.~CString();
    VectorDestruct(self->behaviour, sizeof(Behaviour), 5, BehaviourDestruct);
    self->object.ObjectSpecDestruct();
}
EI_REPLACE(0x0046C140, UnitSpecDestruct);

// 0046C120 (slot 10).
UnitSpec* UnitSpec::DeleteScalar(unsigned flags) {
    UnitSpecDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046C120, UnitSpec::DeleteScalar);

// 0046C2F0: the destructor of the block at +0x210 (the five string lists; reached only when an
// exception unwinds the constructor).
void __fastcall UnitStatsDestruct(std::uint8_t* self) {
    for (int i = 4; i >= 0; --i) StringArrayDestruct(reinterpret_cast<mfc::CStringArray*>(self + 0xAC + i * 0x14));
}
EI_REPLACE(0x0046C2F0, UnitStatsDestruct);

// 0046C4D0 (slot 7): makes the unit, or moves, turns and rescales it.
void __fastcall UnitSpecApply(UnitSpec* self) {
    Spec& b = self->object.base;
    if (!b.object) {
        WorldMakeObjectOriginal(reinterpret_cast<void*>(0x007AFF60), nullptr, &b);
        return;
    }
    SectorMapMove(reinterpret_cast<void*>(0x007B01F4), nullptr, AsEngineObject(b.object), &b.position);
    reinterpret_cast<CAIMap*>(0x007B00D0)->TurnObject(static_cast<CEngineObject*>(b.object), &b.rotation);
    auto* unit = static_cast<CUnit*>(b.object);
    std::memcpy(&unit->orientation, &b.rotation, 16);
    ObjectSetFigureScaleOriginal(b.object, nullptr, &self->object.scale.x);
    std::memcpy(&unit->baseScale, &self->object.scale, 12);
    unit->ApplyBaseScale();
    std::uint32_t& flags = unit->flags_;
    if (!self->object.visible) flags |= 8;
    else flags &= ~8u;
    static_cast<CUnitServer*>(unit->controller)->ResyncWithUnit();
    static_cast<CMapObject*>(b.object)->MarkChanged(1);
}
EI_REPLACE(0x0046C4D0, UnitSpecApply);

// 0046CF10 (slot 9): chunk BBBB0000: the unit's fields, the common ones, then the five behaviour
// records (each given the unit's position first).
void __fastcall UnitSpecSave(UnitSpec* self) {
    Chunk& c = self->object.base.chunk;
    c.BeginWrite(0xBBBB0000);
    c.WriteByte(0xBBBB000A, static_cast<std::uint8_t>(self->field0A));
    c.WriteString(0xBBBB0002, self->prototype);
    for (int i = 4; i >= 0; --i) c.WriteStringArray(0xBBBB0005 + i, &self->lists[i]);
    c.WriteBlob(0xBBBB0004, self->stats, sizeof(self->stats));
    self->object.ObjectSpecWriteFields();
    BehaviourChunk writer;
    writer.chunk.Construct(nullptr);
    ei::SetVtable(&writer, BehaviourChunk::kVtableAddress);
    writer.chunk.parent_ = &c;
    for (Behaviour& behaviour : self->behaviour) {
        behaviour.guard = self->object.base.position;
        writer.target = &behaviour;
        BehaviourWrite(&writer);
    }
    c.EndWrite();
    writer.chunk.Destruct();
}
EI_REPLACE(0x0046CF10, UnitSpecSave);

// 0046D080 (slot 0). A behaviour record chunk fills the next record (not bounded, as in the
// original); an alarm level left unset (4) becomes 0.
void __fastcall UnitSpecOnChunk(UnitSpec* self) {
    Chunk& c = self->object.base.chunk;
    switch (c.id_) {
    case 0xBBBB0002: ReadInto(c, self->prototype); break;
    case 0xBBBB0004: c.ReadBytes(self->stats, sizeof(self->stats)); break;
    case 0xBBBB0005:
    case 0xBBBB0006:
    case 0xBBBB0007:
    case 0xBBBB0008:
    case 0xBBBB0009: c.ReadStringArray(&self->lists[c.id_ - 0xBBBB0005]); break;
    case 0xBBBB000A: self->field0A = c.ReadByte(); break;
    case 0xBBBC0000: {
        BehaviourChunk reader;
        reader.chunk.Construct(nullptr);
        ei::SetVtable(&reader, BehaviourChunk::kVtableAddress);
        reader.chunk.parent_ = &c;
        int index = self->behaviours;
        self->behaviours = index + 1;
        Behaviour* b = &self->behaviour[index];
        reader.target = b;
        b->flags = 0;
        b->alarm = 4;
        reader.chunk.ForEachChild();
        if (b->alarm == 4) b->alarm = 0;
        reader.chunk.Destruct();
        break;
    }
    default: self->object.ObjectSpecOnChunk(); break;
    }
}
EI_REPLACE(0x0046D080, UnitSpecOnChunk);

// ---- Behaviour records in a stream (saved games) -----------------------------------------------
// The stream (CStream, core_streams.h) reads and writes through its vtable (slots 1 and 2).

namespace {
void StreamRead(void* stream, void* data, unsigned size) { static_cast<CStream*>(stream)->Read(data, size); }
void StreamWrite(void* stream, const void* data, unsigned size) { static_cast<CStream*>(stream)->Write(data, size); }
} // namespace

// 0045DE30 (ECX array, EDX stream): the count, the array sized to it (MFC SetSize inlined, new
// waypoints cleared as 0045E130 does), then the raw waypoints.
void __fastcall WaypointsRead(Array<Waypoint>* a, void* stream) {
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
        a->data = static_cast<Waypoint*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(Waypoint)));
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
        auto* data = static_cast<Waypoint*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * sizeof(Waypoint)));
        std::memcpy(data, a->data, static_cast<std::size_t>(a->size) * sizeof(Waypoint));
        mfc::PlainElements<Waypoint>::Construct(data + a->size, n - a->size);
        gamecrt::operator_delete(a->data);
        a->size = n;
        a->data = data;
        a->maxSize = capacity;
    }
    for (int i = 0; i < a->size; ++i) StreamRead(stream, &a->data[i], sizeof(Waypoint));
}
EI_REPLACE(0x0045DE30, WaypointsRead);

// 0045DFB0 (ECX array, EDX stream): the count, then the raw waypoints.
void __fastcall WaypointsWrite(const Array<Waypoint>* a, void* stream) {
    int n = a->size;
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < a->size; ++i) StreamWrite(stream, &a->data[i], sizeof(Waypoint));
}
EI_REPLACE(0x0045DFB0, WaypointsWrite);

// 0045D610: the record from a stream. Files before version 0x53 have no alarm level: it follows
// from the flags (bit 0 -> 0, bit 1 -> 1, bit 2 -> 2, bit 4 -> 3, else 2). +0x30 is not stored.
void Behaviour::Read(void* stream) {
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
    if (static_cast<std::int32_t>(static_cast<CStream*>(stream)->version_) >= 0x53) {
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
EI_REPLACE_METHOD(0x0045D610, Behaviour::Read);

// 0045D760: the record to a stream (the same fields).
void Behaviour::Write(void* stream) const {
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
    for (const void* field : {static_cast<const void*>(&loop), static_cast<const void*>(&flags),
                              static_cast<const void*>(&guardRadius), static_cast<const void*>(&alarmKind),
                              static_cast<const void*>(&enabled), static_cast<const void*>(&callRadius)}) {
        std::memcpy(&value, field, 4);
        StreamWrite(stream, &value, 4);
    }
    StreamWrite(stream, &alertMode, 4);
    value = alarmDelay;
    StreamWrite(stream, &value, 4);
    StreamWrite(stream, &alarm, 4);
}
EI_REPLACE_METHOD(0x0045D760, Behaviour::Write);

} // namespace mapspec
