// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses (0046A4B0). Plain float maths
// (rule 7). See docs/notes/objects.md ("Sixth round").
#include "map_spec.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <new>
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"

namespace mapspec {

namespace {

constexpr std::uintptr_t kSpecVtable = 0x0073C5C4;
constexpr std::uintptr_t kObjectVtable = 0x0073C5F0;
constexpr std::uintptr_t kFlameVtable = 0x0073C620;
constexpr std::uintptr_t kTrapVtable = 0x0073C650;
constexpr std::uintptr_t kLeverVtable = 0x0073C6B0;
constexpr std::uintptr_t kAreasVtable = 0x0073C68C;      // CArray (12-byte elements)
constexpr std::uintptr_t kAreasDeadVtable = 0x0073C698;  // the same class's vtable once destroyed
constexpr std::uintptr_t kTargetsVtable = 0x0073C680;    // CArray (8-byte elements)
constexpr std::uintptr_t kTargetsDeadVtable = 0x0073C6A4;

constexpr int kSlotWorldObject = 2;
constexpr int kSlotApply = 7;
constexpr int kSlotWriteFields = 11;
constexpr int kFileRead = 13;   // CFile
constexpr int kFileWrite = 14;

// MFC CStringArray (library).
void __fastcall StringArrayAppend(mfc::CStringArray* a0, void*, const mfc::CStringArray* a2) { StringArrayCopyOriginal((void*)a0, nullptr, (const void*)a2); }
// MFC CArray helpers (library): SetSize of the 12-byte array, zeroing 8-byte elements.
void __fastcall AreasSetSize(PlainArray* a0, void*, int a2, int a3) { Array12SetSizeOriginal((void*)a0, nullptr, a2, a3); }

// The objects manager (0x7AF5F8) look-up by id; the world server (007AFF60): an object removed
// by id, an object made from a record; the editor's selection (007B00D0) and object lists
// (007B01F4); the world object's own updates.
__declspec(naked) void __fastcall WorldMakeObject(void* /*world*/, void*, Spec* /*spec*/) EI_ORIGINAL(0x005927C0)
__declspec(naked) void __fastcall SelectionLock(void*) EI_ORIGINAL(0x005B8B30)
__declspec(naked) void __fastcall SelectionUnlock(void*) EI_ORIGINAL(0x005B8B80)
void* World() { return reinterpret_cast<void*>(0x007AFF60); }
void* Selection() { return reinterpret_cast<void*>(0x007B00D0); }
void* Lists() { return reinterpret_cast<void*>(0x007B01F4); }
constexpr int kObjectSlotUpdate = 30;

void SetVtable(void* p, std::uintptr_t vtable) { std::memcpy(p, &vtable, 4); }
template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

// A string chunk into `field` (the read string is a temporary, as in the original).
void ReadInto(Chunk& c, mfc::CString& field) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* read = c.ReadString(reinterpret_cast<mfc::CString*>(raw));
    field = *read;
    read->~CString();
}
bool ReadFlag(Chunk& c) { return c.ReadByte() == 1; }

void FileWrite(Chunk& c, const void* data, unsigned size) { ei::VCall<void>(c.File(), kFileWrite, data, size); }
void FileRead(Chunk& c, void* data, unsigned size) { ei::VCall<unsigned>(c.File(), kFileRead, data, size); }

void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

} // namespace

// ---- The base record (0073C5C4) ---------------------------------------------------------------

// 00469CA0. Returns this.
Spec* __fastcall SpecConstruct(Spec* self) {
    self->chunk.Construct(nullptr);
    self->rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    new (&self->name) mfc::CString();
    new (&self->comment) mfc::CString();
    SetVtable(self, kSpecVtable);
    mfc::CString none(Text(0x007846F0));   // "none"
    self->id = -1;
    self->unknown_44 = -1;
    self->position = {0.0f, 0.0f, 0.0f};
    self->rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    self->kind = 0;
    self->object = nullptr;
    self->name = none;
    self->comment = none;
    return self;
}
EI_REPLACE(0x00469CA0, SpecConstruct);

// 00469E50: the destructor body.
void __fastcall SpecDestruct(Spec* self) {
    SetVtable(self, kSpecVtable);
    self->comment.~CString();
    self->name.~CString();
    self->chunk.Destruct();
}
EI_REPLACE(0x00469E50, SpecDestruct);

// 00469E30 (slot 10).
Spec* __fastcall SpecDelete(Spec* self, void*, unsigned flags) {
    SpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00469E30, SpecDelete);

// 00469DB0 (slot 4): "error".
mfc::CString* __fastcall SpecTypeName(Spec*, void*, mfc::CString* out) {
    new (out) mfc::CString(Text(0x00787DC4));
    return out;
}
EI_REPLACE(0x00469DB0, SpecTypeName);

// 00469DE0 / 00469E00 (slots 6 and 8): slot 7.
void __fastcall SpecRemove(Spec* self) { ei::VCall<void>(self, kSlotApply); }
EI_REPLACE(0x00469DE0, SpecRemove);
void __fastcall SpecRefresh(Spec* self) { ei::VCall<void>(self, kSlotApply); }
EI_REPLACE(0x00469E00, SpecRefresh);

// 00469F20 (slot 2): the object with the record's id.
void* __fastcall SpecWorldObject(Spec* self) { return ManagerFindObject(*reinterpret_cast<void**>(0x007AF5F8), nullptr, self->id); }
EI_REPLACE(0x00469F20, SpecWorldObject);

// 00469F40 (slot 5): moves the record, then slot 7.
void __fastcall SpecMove(Spec* self, void*, float dx, float dy, float dz) {
    self->position.x = dx + self->position.x;
    self->position.y = dy + self->position.y;
    self->position.z = dz + self->position.z;
    ei::VCall<void>(self, kSlotApply);
}
EI_REPLACE(0x00469F40, SpecMove);

// ---- Objects (0073C5F0) ------------------------------------------------------------------------

// 0046A5C0. Returns this.
ObjectSpec* __fastcall ObjectSpecConstruct(ObjectSpec* self) {
    SpecConstruct(&self->base);
    new (&self->model) mfc::CString();
    new (&self->texture) mfc::CString();
    new (&self->primaryTexture) mfc::CString();
    new (&self->parentIname) mfc::CString();
    new (&self->unknown_74) mfc::CString();
    StringArrayConstruct(&self->parts);
    SetVtable(self, kObjectVtable);
    mfc::CString none(Text(0x007846F0));
    self->base.kind = 1;
    self->parentId = 0;
    self->model = game::EmptyText();
    self->texture = none;
    self->primaryTexture = none;
    self->player = -1;
    self->scale.y = 0.5f;
    self->scale.x = 0.5f;
    self->scale.z = 0.5f;
    self->shadow = 0;
    self->visible = 1;
    self->unknown_6E = 0;
    self->parentIname = none;
    self->unknown_74 = none;
    StringArraySetSize(&self->parts, nullptr, 0, -1);
    return self;
}
EI_REPLACE(0x0046A5C0, ObjectSpecConstruct);

// 0046AEB0: the destructor body.
void __fastcall ObjectSpecDestruct(ObjectSpec* self) {
    SetVtable(self, kObjectVtable);
    StringArrayDestruct(&self->parts);
    self->unknown_74.~CString();
    self->parentIname.~CString();
    self->primaryTexture.~CString();
    self->texture.~CString();
    self->model.~CString();
    SpecDestruct(&self->base);
}
EI_REPLACE(0x0046AEB0, ObjectSpecDestruct);
// 0058EDF0: the destructor body of the quick item effect's record (vtable 0073FA58, no members of
// its own): a jump to the one above (A-W14).
EI_REPLACE(0x0058EDF0, ObjectSpecDestruct);

// 0046A6F0 (slot 10).
ObjectSpec* __fastcall ObjectSpecDelete(ObjectSpec* self, void*, unsigned flags) {
    ObjectSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046A6F0, ObjectSpecDelete);
EI_REPLACE(0x0058EDD0, ObjectSpecDelete);   // slot 10 of 0073FA58 (through 0058EDF0; A-W14)

// 0046A710: copies `from` (the base fields but its kind and chunk state, then the object's).
// The original goes through a temporary copy of the base record; CStrings share their data
// either way.
void __fastcall ObjectSpecAssign(ObjectSpec* self, void*, const ObjectSpec* from) {
    Spec& b = self->base;
    const Spec& f = from->base;
    b.object = f.object;
    b.unknown_44 = f.unknown_44;
    b.id = f.id;
    b.position = f.position;
    b.rotation = f.rotation;
    b.name = f.name;
    b.comment = f.comment;
    self->parentId = from->parentId;
    self->model = from->model;
    self->texture = from->texture;
    self->primaryTexture = from->primaryTexture;
    self->player = from->player;
    std::memcpy(&self->scale, &from->scale, sizeof(self->scale));
    self->shadow = from->shadow;
    self->visible = from->visible;
    self->unknown_6E = from->unknown_6E;
    self->parentIname = from->parentIname;
    self->unknown_74 = from->unknown_74;
    StringArraySetSize(&self->parts, nullptr, 0, -1);
    StringArrayAppend(&self->parts, nullptr, &from->parts);
}
EI_REPLACE(0x0046A710, ObjectSpecAssign);

// 0046A940 (slot 11): the common fields.
void __fastcall ObjectSpecWriteFields(ObjectSpec* self) {
    Chunk& c = self->base.chunk;
    c.WriteStringArray(0xB00D, &self->parts);
    c.WriteByte(0xB011, self->unknown_6E);
    c.WriteInt(0xB002, self->base.id);
    c.WriteInt(0xB003, self->parentId);
    c.WriteString(0xB004, self->base.name);
    c.WriteString(0xB006, self->texture);
    c.WriteString(0xB00E, self->primaryTexture);
    c.WriteString(0xB007, self->parentIname);
    c.WriteString(0xB008, self->unknown_74);
    c.WriteString(0xB00F, self->base.comment);
    WriteVector(c, 0xB009, self->base.position);
    Quaternion rotation;
    std::memcpy(&rotation, &self->base.rotation, 16);
    c.WriteQuaternion(0xB00A, rotation);
    c.WriteByte(0xB013, self->shadow);
    c.WriteByte(0xB014, self->visible);
    c.WriteInt(0xB012, self->player);
    c.WriteString(0xB016, self->model);
    c.WriteHeader(0xB00C, 0xC);
    FileWrite(c, &self->scale.y, 4);
    FileWrite(c, &self->scale.x, 4);
    FileWrite(c, &self->scale.z, 4);
}
EI_REPLACE(0x0046A940, ObjectSpecWriteFields);

// 0046A920 (slot 9): the record as chunk B001.
void __fastcall ObjectSpecSave(ObjectSpec* self) {
    self->base.chunk.BeginWrite(0xB001);
    ei::VCall<void>(self, kSlotWriteFields);
    self->base.chunk.EndWrite();
}
EI_REPLACE(0x0046A920, ObjectSpecSave);

// 0046AB20 (slot 0): the chunk just read, by its id.
void __fastcall ObjectSpecOnChunk(ObjectSpec* self) {
    Chunk& c = self->base.chunk;
    switch (c.id_) {
    case 0xB002: self->base.id = c.ReadInt(); break;
    case 0xB003: self->parentId = c.ReadInt(); break;
    case 0xB004: ReadInto(c, self->base.name); break;
    case 0xB006: ReadInto(c, self->texture); break;
    case 0xB007: ReadInto(c, self->parentIname); break;
    case 0xB008: ReadInto(c, self->unknown_74); break;
    case 0xB009: {
        Vector3 v;
        std::memcpy(&self->base.position, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xB00A: {
        float q[4];
        std::memcpy(&self->base.rotation, c.ReadQuaternion(q), sizeof(Quaternion));
        break;
    }
    case 0xB00C:
        self->scale.y = c.ReadFloat();
        self->scale.x = c.ReadFloat();
        self->scale.z = c.ReadFloat();
        break;
    case 0xB00D: c.ReadStringArray(&self->parts); break;
    case 0xB00E: ReadInto(c, self->primaryTexture); break;
    case 0xB00F: ReadInto(c, self->base.comment); break;
    case 0xB011: self->unknown_6E = c.ReadByte(); break;
    case 0xB012: self->player = c.ReadInt(); break;
    case 0xB013: self->shadow = ReadFlag(c); break;
    case 0xB014: self->visible = ReadFlag(c); break;
    case 0xB016: ReadInto(c, self->model); break;
    default: break;
    }
}
EI_REPLACE(0x0046AB20, ObjectSpecOnChunk);

// 0046AF70 (slot 6): removes the world object, then slot 7.
void __fastcall ObjectSpecRemove(ObjectSpec* self) {
    if (self->base.object) {
        WorldRemoveObject(World(), nullptr, self->base.id);
        self->base.object = nullptr;
    }
    ei::VCall<void>(self, kSlotApply);
}
EI_REPLACE(0x0046AF70, ObjectSpecRemove);

// 0046AFA0 (slot 8).
void __fastcall ObjectSpecRefresh(ObjectSpec* self) {
    if (self->base.object) UnitMarkChanged(self->base.object, nullptr, 2);
}
EI_REPLACE(0x0046AFA0, ObjectSpecRefresh);

// 0046AFB0 (slot 7): makes the world object, or updates it from the record (position, rotation,
// scale, visibility) while it is out of the editor's lists.
void __fastcall ObjectSpecApply(ObjectSpec* self) {
    void* object = self->base.object;
    if (!object) {
        WorldMakeObject(World(), nullptr, &self->base);
        return;
    }
    SelectionLock(Selection());
    SectorMapRemove(AsSectorMap(Lists()), nullptr, AsEngineObject(self->base.object));
    std::memcpy(static_cast<std::uint8_t*>(self->base.object) + 0x1C, &self->base.position, 12);
    std::memcpy(static_cast<std::uint8_t*>(self->base.object) + 0x34, &self->base.rotation, 16);
    ObjectSetFigureScaleOriginal(self->base.object, nullptr, &self->scale.x);
    std::memcpy(static_cast<std::uint8_t*>(self->base.object) + 0x1AC, &self->scale, 12);
    ei::VCall<void>(self->base.object, kObjectSlotUpdate);
    std::uint32_t& flags = At<std::uint32_t>(self->base.object, 8);
    if (!self->visible) flags |= 8;
    else flags &= ~8u;
    UnitMarkChanged(self->base.object, nullptr, 2);
    UnitMarkChanged(self->base.object, nullptr, 1);
    SectorMapAdd(AsSectorMap(Lists()), nullptr, AsEngineObject(self->base.object));
    SelectionUnlock(Selection());
}
EI_REPLACE(0x0046AFB0, ObjectSpecApply);

// 0046B090 (slot 4): the player number ("%i").
mfc::CString* __fastcall ObjectSpecTypeName(ObjectSpec* self, void*, mfc::CString* out) {
    mfc::CString text;
    text.Format(Text(0x007842D8), self->player);
    new (out) mfc::CString(text);
    return out;
}
EI_REPLACE(0x0046B090, ObjectSpecTypeName);

// ---- Flames (0073C620) ------------------------------------------------------------------------

// 0046B210. Returns this.
FlameSpec* __fastcall FlameSpecConstruct(FlameSpec* self) {
    ObjectSpecConstruct(&self->object);
    new (&self->sound) mfc::CString();
    self->offset = {0.0f, 0.0f, 0.0f};
    SetVtable(self, kFlameVtable);
    self->object.base.kind = 6;
    std::uint32_t volume = 0x3F333333;   // 0.7f
    std::memcpy(&self->volume, &volume, 4);
    self->sound = Text(0x00787DCC);   // "nature\fire\small.wav"
    return self;
}
EI_REPLACE(0x0046B210, FlameSpecConstruct);

// 0046B120: the destructor body.
void __fastcall FlameSpecDestruct(FlameSpec* self) {
    SetVtable(self, kFlameVtable);
    self->sound.~CString();
    ObjectSpecDestruct(&self->object);
}
EI_REPLACE(0x0046B120, FlameSpecDestruct);

// 0046B1F0 (slot 10).
FlameSpec* __fastcall FlameSpecDelete(FlameSpec* self, void*, unsigned flags) {
    FlameSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046B1F0, FlameSpecDelete);

// 0046B310 (slot 7): the world object is always made again.
void __fastcall FlameSpecApply(FlameSpec* self) {
    Spec& b = self->object.base;
    if (b.object) {
        WorldRemoveObject(World(), nullptr, b.id);
        b.object = nullptr;
    }
    WorldMakeObject(World(), nullptr, &b);
}
EI_REPLACE(0x0046B310, FlameSpecApply);

// 0046B340 (slot 9): chunk BBBF0000 with the flame's fields, then the common ones.
void __fastcall FlameSpecSave(FlameSpec* self) {
    Chunk& c = self->object.base.chunk;
    c.BeginWrite(0xBBBF0000);
    WriteVector(c, 0xBBBF0002, self->offset);
    c.WriteFloat(0xBBBF0001, Bits(self->volume));
    c.WriteString(0xBBBF0003, self->sound);
    ObjectSpecWriteFields(&self->object);
    c.EndWrite();
}
EI_REPLACE(0x0046B340, FlameSpecSave);

// 0046B3C0 (slot 0).
void __fastcall FlameSpecOnChunk(FlameSpec* self) {
    Chunk& c = self->object.base.chunk;
    switch (c.id_) {
    case 0xBBBF0001: self->volume = c.ReadFloat(); break;
    case 0xBBBF0002: {
        Vector3 v;
        std::memcpy(&self->offset, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xBBBF0003: ReadInto(c, self->sound); break;
    default: ObjectSpecOnChunk(&self->object); break;
    }
}
EI_REPLACE(0x0046B3C0, FlameSpecOnChunk);

// ---- Magic traps (0073C650) --------------------------------------------------------------------

// 0046B4A0. Returns this.
TrapSpec* __fastcall TrapSpecConstruct(TrapSpec* self) {
    ObjectSpecConstruct(&self->object);
    self->diplomacy = -1;
    new (&self->spell) mfc::CString();
    self->areas.data = nullptr;
    self->areas.growBy = 0;
    self->areas.maxSize = 0;
    self->areas.size = 0;
    SetVtable(&self->areas, kAreasVtable);
    self->targets.data = nullptr;
    self->targets.growBy = 0;
    self->targets.maxSize = 0;
    self->targets.size = 0;
    SetVtable(&self->targets, kTargetsVtable);
    self->interval = 15;
    SetVtable(self, kTrapVtable);
    self->object.base.kind = 7;
    return self;
}
EI_REPLACE(0x0046B4A0, TrapSpecConstruct);

// 0046B570: the destructor body (the arrays' destructors inlined: their blocks freed).
void __fastcall TrapSpecDestruct(TrapSpec* self) {
    SetVtable(&self->targets, kTargetsDeadVtable);
    if (self->targets.data) gamecrt::operator_delete(self->targets.data);
    SetVtable(&self->areas, kAreasDeadVtable);
    if (self->areas.data) gamecrt::operator_delete(self->areas.data);
    self->spell.~CString();
    ObjectSpecDestruct(&self->object);
}
EI_REPLACE(0x0046B570, TrapSpecDestruct);

// 0046B550 (slot 10).
TrapSpec* __fastcall TrapSpecDelete(TrapSpec* self, void*, unsigned flags) {
    TrapSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046B550, TrapSpecDelete);

// 0046B520 (slot 4): "MagicTrap".
mfc::CString* __fastcall TrapSpecTypeName(TrapSpec*, void*, mfc::CString* out) {
    new (out) mfc::CString(Text(0x00787DE4));
    return out;
}
EI_REPLACE(0x0046B520, TrapSpecTypeName);

// 0046B680 (slot 9): chunk BBAB0000: the trap's fields (the arrays as a count and the raw
// elements), then the common ones.
void __fastcall TrapSpecSave(TrapSpec* self) {
    Chunk& c = self->object.base.chunk;
    c.BeginWrite(0xBBAB0000);
    c.WriteInt(0xBBAB0001, self->diplomacy);
    c.WriteString(0xBBAB0002, self->spell);
    c.WriteInt(0xBBAB0005, self->interval);
    c.WriteByte(0xBBAC0005, self->castOnce);
    std::uint32_t count = static_cast<std::uint32_t>(self->areas.size);
    c.WriteHeader(0xBBAB0003, static_cast<int>(count * 12 + 4));
    FileWrite(c, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(c, self->areas.data + i * 12, 12);
    count = static_cast<std::uint32_t>(self->targets.size);
    c.WriteHeader(0xBBAB0004, static_cast<int>(count * 8 + 4));
    FileWrite(c, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(c, &self->targets.data[i], 8);
    ObjectSpecWriteFields(&self->object);
    c.EndWrite();
}
EI_REPLACE(0x0046B680, TrapSpecSave);

// 0046B7D0 (slot 0).
void __fastcall TrapSpecOnChunk(TrapSpec* self) {
    Chunk& c = self->object.base.chunk;
    switch (c.id_) {
    case 0xBBAB0001: self->diplomacy = c.ReadInt(); break;
    case 0xBBAB0002: ReadInto(c, self->spell); break;
    case 0xBBAB0003: {
        std::uint32_t n = static_cast<std::uint32_t>(c.ReadInt());
        AreasSetSize(&self->areas, nullptr, static_cast<int>(n), -1);
        for (std::uint32_t i = 0; i < n; ++i) {
            FileRead(c, self->areas.data + i * 12, 12);
            c.Advance(12);
        }
        break;
    }
    case 0xBBAB0004: {
        int n = c.ReadInt();
        self->targets.SetSize(n, -1);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            FileRead(c, &self->targets.data[i], 8);
            c.Advance(8);
        }
        break;
    }
    case 0xBBAB0005: self->interval = c.ReadInt(); break;
    case 0xBBAC0005: self->castOnce = ReadFlag(c); break;
    default: ObjectSpecOnChunk(&self->object); break;
    }
}
EI_REPLACE(0x0046B7D0, TrapSpecOnChunk);

// ---- Levers (0073C6B0) -------------------------------------------------------------------------

// 0046BD40. Returns this.
LeverSpec* __fastcall LeverSpecConstruct(LeverSpec* self) {
    ObjectSpecConstruct(&self->object);
    self->stats[0] = 1;
    self->stats[1] = 0;
    self->stats[2] = 0;
    self->state = 0;
    self->states = 2;
    self->cycled = 0;
    self->castOnce = 0;
    self->unknown_A2 = 0;
    SetVtable(self, kLeverVtable);
    self->object.base.kind = 8;
    return self;
}
EI_REPLACE(0x0046BD40, LeverSpecConstruct);

// 0046BDC0: the destructor body (the object record's).
void __fastcall LeverSpecDestruct(LeverSpec* self) { ObjectSpecDestruct(&self->object); }
EI_REPLACE(0x0046BDC0, LeverSpecDestruct);

// 0046BDA0 (slot 10).
LeverSpec* __fastcall LeverSpecDelete(LeverSpec* self, void*, unsigned flags) {
    LeverSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046BDA0, LeverSpecDelete);

// 0046BE80 (slot 9): chunk BBAC0000.
void __fastcall LeverSpecSave(LeverSpec* self) {
    Chunk& c = self->object.base.chunk;
    c.BeginWrite(0xBBAC0000);
    c.WriteBlob(0xBBAC0006, self->stats, 12);
    c.WriteByte(0xBBAC0002, static_cast<std::uint8_t>(self->state));
    c.WriteByte(0xBBAC0003, static_cast<std::uint8_t>(self->states));
    c.WriteByte(0xBBAC0004, self->unknown_A2);
    c.WriteByte(0xBBAC0007, self->cycled);
    c.WriteByte(0xBBAC0008, self->castOnce);
    ObjectSpecWriteFields(&self->object);
    c.EndWrite();
}
EI_REPLACE(0x0046BE80, LeverSpecSave);

// 0046BF20 (slot 0).
void __fastcall LeverSpecOnChunk(LeverSpec* self) {
    Chunk& c = self->object.base.chunk;
    switch (c.id_) {
    case 0xBBAC0002: self->state = c.ReadByte(); break;
    case 0xBBAC0003: self->states = c.ReadByte(); break;
    case 0xBBAC0004: self->unknown_A2 = ReadFlag(c); break;
    case 0xBBAC0006: c.ReadBytes(self->stats, 12); break;
    case 0xBBAC0007: self->cycled = ReadFlag(c); break;
    case 0xBBAC0008: self->castOnce = ReadFlag(c); break;
    default: ObjectSpecOnChunk(&self->object); break;
    }
}
EI_REPLACE(0x0046BF20, LeverSpecOnChunk);

// ---- 0046A4B0: rotating a vector -----------------------------------------------------------------

// `out` = `v` rotated by the quaternion: (w^2 - |q|^2) v + 2w (q x v) + 2 (q . v) q.
Vector3* __fastcall QuaternionRotate(const Quaternion* q, void*, Vector3* out, const Vector3* v) {
    float x = q->x, y = q->y, z = q->z;
    float dot = 2.0f * (x * v->x + z * v->z + y * v->y);
    float ax = dot * x, ay = dot * y, az = dot * z;
    float w2 = q->w + q->w;
    float bx = (y * v->z - z * v->y) * w2;
    float by = (z * v->x - x * v->z) * w2;
    float bz = (x * v->y - y * v->x) * w2;
    float m = q->w * q->w - (z * z + y * y + x * x);
    out->x = (m * v->x + bx) + ax;
    out->y = (m * v->y + by) + ay;
    out->z = (m * v->z + bz) + az;
    return out;
}
EI_REPLACE(0x0046A4B0, QuaternionRotate);

// ---- 0046E5E0: the zone's diplomacy table --------------------------------------------------------

namespace {
void __fastcall StringArraySetAtGrow(mfc::CStringArray* a0, void*, int a2, const char* a3) { StringArraySetAtGrowTextOriginal((void*)a0, nullptr, a2, a3); }
} // namespace

// The diplomacy of the zone's 32 players (0x1014 bytes; made by the zone loader 00481660): every
// pair 1 but a player with itself (0), and the player names "Player-<i>". Returns this.
std::int32_t* __fastcall DiplomacyConstruct(std::int32_t* self) {
    auto* names = reinterpret_cast<mfc::CStringArray*>(self + 0x400);
    StringArrayConstruct(names);
    for (int i = 0; i < 0x400; ++i) self[i] = 1;
    for (int i = 0; i < 32; ++i) self[i * 33] = 0;
    for (int i = 0; i < 32; ++i) {
        StringArraySetAtGrow(names, nullptr, names->size, game::EmptyText());
        names->data[i].Format(Text(0x00787E44), i);   // "Player-%i"
    }
    return self;
}
EI_REPLACE(0x0046E5E0, DiplomacyConstruct);

} // namespace mapspec
