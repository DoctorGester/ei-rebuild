// The zone file's unit records (vtable 0073C6E0, kind 5; map_spec.h) with their five behaviour
// records (0x4C bytes: patrols of waypoints and the guard settings), the small chunk classes that
// read and write those (vtables 0073C710 waypoint, 0073C718 patrol, 0073C720 behaviour) and the
// patrol array's copy. See docs/notes/objects.md ("Sixth round").
#include "map_spec.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include "originals_library.h"
#include "originals_units.h"

namespace mapspec {

// A waypoint (0x14 bytes).
struct Waypoint {
    Vector3 position;            // +0x00
    float wait;                  // +0x0C
    std::int16_t unknown_10;     // +0x10
    std::uint16_t unknown_12;    // +0x12
};
static_assert(sizeof(Waypoint) == 0x14);

template <typename T>
struct Array {                   // MFC CArray (0x14 bytes)
    void* vtable;
    T* data;
    int size;
    int max;
    int grow;
};

// A patrol (0x20 bytes): where it starts and its waypoints (CArray, vtable 0073C3A0).
struct Patrol {
    Vector3 position;
    Array<Waypoint> waypoints;   // +0x0C
};
static_assert(sizeof(Patrol) == 0x20);

// A unit's behaviour record (0x4C bytes; constructor 0045D5B0, destructor 0046C290).
struct Behaviour {
    std::int32_t type;           // +0x00 (3)
    Array<Patrol> patrols;       // +0x04 (vtable 0073C37C)
    Vector3 guard;               // +0x18 the unit's position when saved
    std::uint32_t unknown_24;    // +0x24 (1)
    std::uint32_t flags;         // +0x28 bit 3
    float unknown_2C;            // +0x2C (10)
    float unknown_30;            // +0x30 (-1)
    std::uint32_t unknown_34;    // +0x34
    std::uint32_t unknown_38;    // +0x38
    std::uint32_t unknown_3C;    // +0x3C float (from 0073C378)
    std::uint32_t unknown_40;    // +0x40
    std::uint32_t unknown_44;    // +0x44 (75)
    std::uint32_t alarm;         // +0x48 0..2 (4 while reading: unset)
};
static_assert(offsetof(Behaviour, guard) == 0x18);
static_assert(sizeof(Behaviour) == 0x4C);

// A unit record (vtable 0073C6E0, 0x324 bytes).
struct UnitSpec {
    ObjectSpec object;           // +0x000
    std::int32_t behaviours;     // +0x08C records read so far
    Behaviour behaviour[5];      // +0x090
    mfc::CString prototype;      // +0x20C ("none")
    std::uint8_t stats[0xAC];    // +0x210
    mfc::CStringArray lists[5];   // +0x2BC ... +0x30C (chunks BBBB0005 ... BBBB0009)
    std::uint32_t unknown_320;   // +0x320
};
static_assert(offsetof(UnitSpec, behaviour) == 0x90);
static_assert(offsetof(UnitSpec, prototype) == 0x20C);
static_assert(offsetof(UnitSpec, lists) == 0x2BC);
static_assert(sizeof(UnitSpec) == 0x324);

// The small chunk classes: a Chunk and the value it reads or writes.
struct TargetChunk {
    Chunk chunk;
    void* target;                // +0x18
};
static_assert(sizeof(TargetChunk) == 0x1C);

// Rewritten in map_spec.cpp.
ObjectSpec* __fastcall ObjectSpecConstruct(ObjectSpec* self);
void __fastcall ObjectSpecDestruct(ObjectSpec* self);
void __fastcall ObjectSpecWriteFields(ObjectSpec* self);
void __fastcall ObjectSpecOnChunk(ObjectSpec* self);

namespace {

constexpr std::uintptr_t kUnitVtable = 0x0073C6E0;
constexpr std::uintptr_t kWaypointChunkVtable = 0x0073C710;
constexpr std::uintptr_t kPatrolChunkVtable = 0x0073C718;
constexpr std::uintptr_t kBehaviourChunkVtable = 0x0073C720;
constexpr std::uintptr_t kPatrolsVtable = 0x0073C37C;
constexpr std::uintptr_t kPatrolsDeadVtable = 0x0073C394;
constexpr std::uintptr_t kWaypointsDeadVtable = 0x0073C388;
constexpr std::uintptr_t kBehaviourConstruct = 0x0045D5B0;
constexpr std::uintptr_t kBehaviourDestruct = 0x0046C290;
constexpr int kObjectSlotUpdate = 30;

// CRT eh vector constructor / destructor iterators.
void __stdcall VectorConstruct(void* a0, unsigned a1, int a2, std::uintptr_t a3, std::uintptr_t a4) { EhVectorConstructorIterator(a0, a1, a2, (void*)a3, (void*)a4); }
void __stdcall VectorDestruct(void* a0, unsigned a1, int a2, std::uintptr_t a3) { EhVectorDestructorIterator(a0, a1, a2, (void*)a3); }
// MFC (library): CStringArray, the CArray instantiations of patrols and waypoints.
__declspec(naked) void __fastcall WaypointsSetSize(Array<Waypoint>*, void*, int, int) EI_ORIGINAL(0x0046E970)
__declspec(naked) void __fastcall PatrolsSetSize(Array<Patrol>*, void*, int, int) EI_ORIGINAL(0x0045DAE0)
__declspec(naked) void __stdcall ConstructPatrols(Patrol*, int) EI_ORIGINAL(0x0045E180)
__declspec(naked) void __stdcall DestructPatrols(Patrol*, int) EI_ORIGINAL(0x0046F0B0)
__declspec(naked) Patrol* __fastcall PatrolDelete(Patrol*, void*, unsigned) EI_ORIGINAL(0x0046F160)
__declspec(naked) Patrol* __fastcall PatrolAt(const Array<Patrol>*, void*, Patrol* /*out*/, int) EI_ORIGINAL(0x0046EAB0)
__declspec(naked) Waypoint* __fastcall WaypointAt(const Array<Waypoint>*, void*, Waypoint* /*out*/, int) EI_ORIGINAL(0x0046EFA0)
// The world server (007AFF60) makes an object; the editor's object lists (007B01F4) and selection
// (007B00D0) move it; the world object's updates.
__declspec(naked) void __fastcall WorldMakeObject(void*, void*, Spec*) EI_ORIGINAL(0x005927C0)
__declspec(naked) void __fastcall ListsMoveObject(void*, void*, void* /*object*/, const Vector3*) EI_ORIGINAL(0x005C11D0)
__declspec(naked) void __fastcall SelectionTurnObject(void*, void*, void* /*object*/, const Quaternion*) EI_ORIGINAL(0x005BC180)
__declspec(naked) void __fastcall ObjectSetScale(void*, void*, const float*) EI_ORIGINAL(0x00517700)
void __fastcall UnitResetMotion(void* a0) { ControllerRepositionOriginal(a0); }
// The behaviour writer, through its address (rewritten below).
__declspec(naked) void __fastcall BehaviourWriteAt(TargetChunk*) EI_ORIGINAL(0x0046C900)

void SetVtable(void* p, std::uintptr_t vtable) { std::memcpy(p, &vtable, 4); }
template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
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

// A child chunk object of `vtable` for `parent`.
void StartChild(TargetChunk& child, std::uintptr_t vtable, Chunk* parent) {
    child.chunk.Construct(nullptr);
    SetVtable(&child, vtable);
    child.chunk.parent_ = parent;
}

} // namespace

// ---- Behaviour records -------------------------------------------------------------------------

// 0045D5B0. Returns this.
Behaviour* __fastcall BehaviourConstruct(Behaviour* self) {
    self->patrols.data = nullptr;
    self->patrols.grow = 0;
    self->patrols.max = 0;
    self->patrols.size = 0;
    SetVtable(&self->patrols, kPatrolsVtable);
    self->type = 3;
    self->unknown_24 = 1;
    self->flags = 0;
    self->unknown_2C = 10.0f;
    self->unknown_30 = -1.0f;
    self->unknown_34 = 0;
    self->unknown_38 = 0;
    self->unknown_40 = 0;
    self->unknown_3C = *reinterpret_cast<const std::uint32_t*>(0x0073C378);
    self->unknown_44 = 75;
    self->alarm = 0;
    return self;
}
EI_REPLACE(0x0045D5B0, BehaviourConstruct);

// 0046C290: the destructor body (the patrol array's destructor inlined: each patrol's waypoint
// block freed, then the patrols' block).
void __fastcall BehaviourDestruct(Behaviour* self) {
    Patrol* patrols = self->patrols.data;
    SetVtable(&self->patrols, kPatrolsDeadVtable);
    if (!patrols) return;
    for (int i = 0, n = self->patrols.size; i < n; ++i) {
        Patrol& p = patrols[i];
        SetVtable(&p.waypoints, kWaypointsDeadVtable);
        if (p.waypoints.data) gamecrt::operator_delete(p.waypoints.data);
    }
    gamecrt::operator_delete(self->patrols.data);
}
EI_REPLACE(0x0046C290, BehaviourDestruct);

// 0046E680: the patrol array becomes a copy of `from` (each patrol copied through a temporary
// returned by value, as the original does).
void __fastcall PatrolsCopy(Array<Patrol>* self, void*, const Array<Patrol>* from) {
    int n = from->size;
    if (n == 0) {
        if (self->data) {
            for (int i = 0, count = self->size; i < count; ++i) PatrolDelete(self->data + i, nullptr, 0);
            gamecrt::operator_delete(self->data);
            self->data = nullptr;
        }
        self->max = 0;
        self->size = 0;
    } else if (!self->data) {
        self->data = static_cast<Patrol*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(Patrol)));
        ConstructPatrols(self->data, n);
        self->max = n;
        self->size = n;
    } else if (n <= self->max) {
        if (self->size < n) ConstructPatrols(self->data + self->size, n - self->size);
        else if (self->size > n) DestructPatrols(self->data + n, self->size - n);
        self->size = n;
    } else {
        int grow = self->grow;
        if (grow == 0) {
            grow = self->size / 8;
            grow = grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
        }
        int capacity = self->max + grow;
        if (n >= capacity) capacity = n;
        auto* data = static_cast<Patrol*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * sizeof(Patrol)));
        std::memcpy(data, self->data, static_cast<std::size_t>(self->size) * sizeof(Patrol));
        ConstructPatrols(data + self->size, n - self->size);
        gamecrt::operator_delete(self->data);
        self->data = data;
        self->size = n;
        self->max = capacity;
    }
    for (int i = 0; i < from->size; ++i) {
        alignas(4) std::uint8_t raw[sizeof(Patrol)];
        auto* copy = reinterpret_cast<Patrol*>(raw);
        PatrolAt(from, nullptr, copy, i);
        Patrol& to = self->data[i];
        to.position = copy->position;
        WaypointsSetSize(&to.waypoints, nullptr, copy->waypoints.size, -1);
        for (int j = 0; j < copy->waypoints.size; ++j) {
            Waypoint w;
            WaypointAt(&copy->waypoints, nullptr, &w, j);
            self->data[i].waypoints.data[j] = w;
        }
        SetVtable(&copy->waypoints, kWaypointsDeadVtable);
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
    case 0xBBBE0002: w->unknown_10 = static_cast<std::int16_t>(c.ReadInt()); break;
    case 0xBBBE0003: static_cast<Waypoint*>(self->target)->wait = c.ReadFloat(); break;
    case 0xBBBE0004: static_cast<Waypoint*>(self->target)->unknown_12 = c.ReadByte(); break;
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
    TargetChunk child;
    StartChild(child, kWaypointChunkVtable, &self->chunk);
    auto* patrol = static_cast<Patrol*>(self->target);
    WaypointsSetSize(&patrol->waypoints, nullptr, patrol->waypoints.size + 1, -1);
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
    case 0xBBBC0002: b->unknown_24 = c.ReadByte(); break;
    case 0xBBBC0003: b->type = c.ReadInt(); break;
    case 0xBBBC0004: b->unknown_2C = c.ReadFloat(); break;
    case 0xBBBC0005: ReadVector(c, b->guard); break;
    case 0xBBBC0006: b->unknown_34 = c.ReadByte(); break;
    case 0xBBBC0007: b->unknown_38 = c.ReadByte(); break;
    case 0xBBBC0008:
        if (c.ReadByte() != 0 && static_cast<std::int32_t>(b->alarm) > 1) b->alarm = 1;
        break;
    case 0xBBBC0009:
        if (c.ReadByte() != 0 && static_cast<std::int32_t>(b->alarm) > 2) b->alarm = 2;
        break;
    case 0xBBBC000A: b->unknown_30 = c.ReadFloat(); break;
    case 0xBBBC000B: b->unknown_40 = c.ReadByte(); break;
    case 0xBBBC000C: {
        float f = c.ReadFloat();
        std::memcpy(&b->unknown_3C, &f, 4);
        break;
    }
    case 0xBBBC000D:
        if (c.ReadByte() != 0) b->flags |= 8;
        else b->flags &= ~8u;
        break;
    case 0xBBBC000E: b->alarm = c.ReadByte(); break;
    case 0xBBBD0000: {
        TargetChunk child;
        StartChild(child, kPatrolChunkVtable, &self->chunk);
        auto* behaviour = static_cast<Behaviour*>(self->target);
        PatrolsSetSize(&behaviour->patrols, nullptr, behaviour->patrols.size + 1, -1);
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
    c.WriteByte(0xBBBC000B, static_cast<std::uint8_t>(b->unknown_40));
    c.WriteFloat(0xBBBC000C, b->unknown_3C);
    c.WriteByte(0xBBBC0002, static_cast<std::uint8_t>(b->unknown_24));
    c.WriteByte(0xBBBC000E, static_cast<std::uint8_t>(b->alarm));
    c.WriteByte(0xBBBC000D, static_cast<std::uint8_t>((b->flags >> 3) & 1));
    c.WriteInt(0xBBBC0003, b->type);
    c.WriteFloat(0xBBBC0004, Bits(b->unknown_2C));
    c.WriteFloat(0xBBBC000A, Bits(b->unknown_30));
    WriteVector(c, 0xBBBC0005, b->guard);
    c.WriteByte(0xBBBC0007, static_cast<std::uint8_t>(b->unknown_38));
    c.WriteByte(0xBBBC0006, static_cast<std::uint8_t>(b->unknown_34));
    TargetChunk patrolChunk;
    patrolChunk.chunk.Construct(nullptr);
    SetVtable(&patrolChunk, kPatrolChunkVtable);
    patrolChunk.chunk.parent_ = &self->chunk;
    for (int i = 0; i < static_cast<Behaviour*>(self->target)->patrols.size; ++i) {
        Patrol* p = &static_cast<Behaviour*>(self->target)->patrols.data[i];
        patrolChunk.target = p;
        patrolChunk.chunk.BeginWrite(0xBBBD0000);
        WriteVector(patrolChunk.chunk, 0xBBBD0001, p->position);
        TargetChunk waypointChunk;
        waypointChunk.chunk.Construct(nullptr);
        SetVtable(&waypointChunk, kWaypointChunkVtable);
        waypointChunk.chunk.parent_ = &patrolChunk.chunk;
        for (int j = 0; j < p->waypoints.size; ++j) {
            Waypoint* w = &p->waypoints.data[j];
            waypointChunk.target = w;
            waypointChunk.chunk.BeginWrite(0xBBBE0000);
            WriteVector(waypointChunk.chunk, 0xBBBE0001, w->position);
            waypointChunk.chunk.WriteInt(0xBBBE0002, w->unknown_10);
            waypointChunk.chunk.WriteFloat(0xBBBE0003, Bits(w->wait));
            waypointChunk.chunk.WriteByte(0xBBBE0004, static_cast<std::uint8_t>(w->unknown_12));
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
    ObjectSpecConstruct(&self->object);
    VectorConstruct(self->behaviour, sizeof(Behaviour), 5, kBehaviourConstruct, kBehaviourDestruct);
    new (&self->prototype) mfc::CString();
    for (mfc::CStringArray& list : self->lists) list.Construct();
    self->unknown_320 = 0;
    SetVtable(self, kUnitVtable);
    self->object.base.kind = 5;
    self->prototype = reinterpret_cast<const char*>(0x007846F0);   // "none"
    self->behaviours = 0;
    self->behaviour[0].unknown_38 = 1;
    std::memset(self->stats, 0, sizeof(self->stats));
    return self;
}
EI_REPLACE(0x0046C010, UnitSpecConstruct);

const void* UnitSpecBehaviours(const UnitSpec* spec) { return spec->behaviour; }

// 0046C140: the destructor body.
void __fastcall UnitSpecDestruct(UnitSpec* self) {
    SetVtable(self, kUnitVtable);
    for (int i = 4; i >= 0; --i) StringArrayDestruct(&self->lists[i]);
    self->prototype.~CString();
    VectorDestruct(self->behaviour, sizeof(Behaviour), 5, kBehaviourDestruct);
    ObjectSpecDestruct(&self->object);
}
EI_REPLACE(0x0046C140, UnitSpecDestruct);

// 0046C120 (slot 10).
UnitSpec* __fastcall UnitSpecDelete(UnitSpec* self, void*, unsigned flags) {
    UnitSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046C120, UnitSpecDelete);

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
        WorldMakeObject(reinterpret_cast<void*>(0x007AFF60), nullptr, &b);
        return;
    }
    ListsMoveObject(reinterpret_cast<void*>(0x007B01F4), nullptr, b.object, &b.position);
    SelectionTurnObject(reinterpret_cast<void*>(0x007B00D0), nullptr, b.object, &b.rotation);
    std::memcpy(static_cast<std::uint8_t*>(b.object) + 0x34, &b.rotation, 16);
    ObjectSetScale(b.object, nullptr, self->object.scale);
    std::memcpy(static_cast<std::uint8_t*>(b.object) + 0x1AC, self->object.scale, 12);
    ei::VCall<void>(b.object, kObjectSlotUpdate);
    std::uint32_t& flags = At<std::uint32_t>(b.object, 8);
    if (!self->object.visible) flags |= 8;
    else flags &= ~8u;
    UnitResetMotion(At<void*>(b.object, 0x240));
    UnitMarkChanged(b.object, nullptr, 1);
}
EI_REPLACE(0x0046C4D0, UnitSpecApply);

// 0046CF10 (slot 9): chunk BBBB0000: the unit's fields, the common ones, then the five behaviour
// records (each given the unit's position first).
void __fastcall UnitSpecSave(UnitSpec* self) {
    Chunk& c = self->object.base.chunk;
    c.BeginWrite(0xBBBB0000);
    c.WriteByte(0xBBBB000A, static_cast<std::uint8_t>(self->unknown_320));
    c.WriteString(0xBBBB0002, self->prototype);
    for (int i = 4; i >= 0; --i) c.WriteStringArray(0xBBBB0005 + i, &self->lists[i]);
    c.WriteBlob(0xBBBB0004, self->stats, sizeof(self->stats));
    ObjectSpecWriteFields(&self->object);
    TargetChunk writer;
    writer.chunk.Construct(nullptr);
    SetVtable(&writer, kBehaviourChunkVtable);
    writer.chunk.parent_ = &c;
    for (Behaviour& behaviour : self->behaviour) {
        behaviour.guard = self->object.base.position;
        writer.target = &behaviour;
        BehaviourWriteAt(&writer);
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
    case 0xBBBB000A: self->unknown_320 = c.ReadByte(); break;
    case 0xBBBC0000: {
        TargetChunk reader;
        reader.chunk.Construct(nullptr);
        SetVtable(&reader, kBehaviourChunkVtable);
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
    default: ObjectSpecOnChunk(&self->object); break;
    }
}
EI_REPLACE(0x0046D080, UnitSpecOnChunk);

// ---- Behaviour records in a stream (saved games) -----------------------------------------------
// The stream's slot 1 reads (data, size), slot 2 writes; its +8 is the file version.

namespace {
constexpr int kStreamRead = 1;
constexpr int kStreamWrite = 2;
void StreamRead(void* stream, void* data, unsigned size) { ei::VCall<unsigned>(stream, kStreamRead, data, size); }
void StreamWrite(void* stream, const void* data, unsigned size) { ei::VCall<unsigned>(stream, kStreamWrite, data, size); }
__declspec(naked) void __stdcall ConstructWaypoints(Waypoint*, int) EI_ORIGINAL(0x0045E130)
__declspec(naked) void __fastcall WaypointsReadAt(Array<Waypoint>*, void* /*stream*/) EI_ORIGINAL(0x0045DE30)
__declspec(naked) void __fastcall WaypointsWriteAt(const Array<Waypoint>*, void* /*stream*/) EI_ORIGINAL(0x0045DFB0)
} // namespace

// 0045DE30 (ECX array, EDX stream): the count, the array sized to it (MFC SetSize inlined, new
// waypoints cleared by 0045E130), then the raw waypoints.
void __fastcall WaypointsRead(Array<Waypoint>* a, void* stream) {
    int n;
    StreamRead(stream, &n, 4);
    if (n == 0) {
        if (a->data) {
            gamecrt::operator_delete(a->data);
            a->data = nullptr;
        }
        a->max = 0;
        a->size = 0;
    } else if (!a->data) {
        a->data = static_cast<Waypoint*>(gamecrt::operator_new(static_cast<std::size_t>(n) * sizeof(Waypoint)));
        ConstructWaypoints(a->data, n);
        a->max = n;
        a->size = n;
    } else if (n <= a->max) {
        if (n > a->size) ConstructWaypoints(a->data + a->size, n - a->size);
        a->size = n;
    } else {
        int grow = a->grow;
        if (grow == 0) {
            grow = a->size / 8;
            grow = grow < 4 ? 4 : grow > 0x400 ? 0x400 : grow;
        }
        int capacity = a->max + grow;
        if (n >= capacity) capacity = n;
        auto* data = static_cast<Waypoint*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * sizeof(Waypoint)));
        std::memcpy(data, a->data, static_cast<std::size_t>(a->size) * sizeof(Waypoint));
        ConstructWaypoints(data + a->size, n - a->size);
        gamecrt::operator_delete(a->data);
        a->size = n;
        a->data = data;
        a->max = capacity;
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
void __fastcall BehaviourRead(Behaviour* self, void*, void* stream) {
    StreamRead(stream, &self->type, 4);
    int n;
    StreamRead(stream, &n, 4);
    PatrolsSetSize(&self->patrols, nullptr, n, -1);
    for (int i = 0; i < self->patrols.size; ++i) {
        Patrol* p = &self->patrols.data[i];
        StreamRead(stream, &p->position, 12);
        WaypointsReadAt(&p->waypoints, stream);
    }
    StreamRead(stream, &self->guard, 12);
    StreamRead(stream, &self->unknown_24, 4);
    StreamRead(stream, &self->flags, 4);
    StreamRead(stream, &self->unknown_2C, 4);
    StreamRead(stream, &self->unknown_34, 4);
    StreamRead(stream, &self->unknown_38, 4);
    StreamRead(stream, &self->unknown_3C, 4);
    StreamRead(stream, &self->unknown_40, 4);
    StreamRead(stream, &self->unknown_44, 4);
    if (*reinterpret_cast<int*>(static_cast<std::uint8_t*>(stream) + 8) >= 0x53) {
        StreamRead(stream, &self->alarm, 4);
        return;
    }
    std::uint32_t flags = self->flags;
    self->alarm = 2;
    if (flags & 1) self->alarm = 0;
    else if (flags & 2) self->alarm = 1;
    else if (flags & 4) self->alarm = 2;
    else if (flags & 0x10) self->alarm = 3;
}
EI_REPLACE(0x0045D610, BehaviourRead);

// 0045D760: the record to a stream (the same fields).
void __fastcall BehaviourStreamWrite(Behaviour* self, void*, void* stream) {
    StreamWrite(stream, &self->type, 4);
    int n = self->patrols.size;
    StreamWrite(stream, &n, 4);
    for (int i = 0; i < self->patrols.size; ++i) {
        Patrol* p = &self->patrols.data[i];
        StreamWrite(stream, &p->position, 12);
        WaypointsWriteAt(&p->waypoints, stream);
    }
    StreamWrite(stream, &self->guard, 12);
    std::uint32_t value;
    for (std::size_t offset : {0x24u, 0x28u, 0x2Cu, 0x34u, 0x38u, 0x3Cu}) {
        std::memcpy(&value, reinterpret_cast<std::uint8_t*>(self) + offset, 4);
        StreamWrite(stream, &value, 4);
    }
    StreamWrite(stream, &self->unknown_40, 4);
    value = self->unknown_44;
    StreamWrite(stream, &value, 4);
    StreamWrite(stream, &self->alarm, 4);
}
EI_REPLACE(0x0045D760, BehaviourStreamWrite);

} // namespace mapspec
