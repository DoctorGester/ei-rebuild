// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses (0046A4B0). Plain float maths
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "base_manager.h"
#include "map_spec.h"
#include "game_strings.h"
#include "map_object.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <new>
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "lib/mfc_slots.h"
#include "ei/vcall.h"

namespace mapspec {

namespace {


// The objects manager (0x7AF5F8) look-up by id; the world server (007AFF60): an object removed
// by id, an object made from a record; the editor's selection (007B00D0) and object lists
// (007B01F4); the world object's own updates.
void* World() { return reinterpret_cast<void*>(0x007AFF60); }
void* Selection() { return reinterpret_cast<void*>(0x007B00D0); }
void* Lists() { return reinterpret_cast<void*>(0x007B01F4); }

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// A string chunk into `field` (the read string is a temporary, as in the original).
void ReadInto(Chunk& c, mfc::CString& field) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    mfc::CString* read = c.ReadString(reinterpret_cast<mfc::CString*>(raw));
    field = *read;
    read->~CString();
}
bool ReadFlag(Chunk& c) { return c.ReadByte() == 1; }

// The chunk's file (an MFC CFile) through its vtable.
mfc::CFileVirtuals* FileOf(Chunk& c) { return static_cast<mfc::CFileVirtuals*>(c.File()); }
void FileWrite(Chunk& c, const void* data, unsigned size) { FileOf(c)->Write(data, size); }
void FileRead(Chunk& c, void* data, unsigned size) { FileOf(c)->Read(data, size); }

void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

} // namespace

// ---- The base record (0073C5C4) ---------------------------------------------------------------

// 00469CA0. Returns this.
Spec* Spec::SpecConstruct() {
    chunk.Construct(nullptr);
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    new (&name) mfc::CString();
    new (&comment) mfc::CString();
    ei::SetVtable(this, kVtableAddress);
    mfc::CString none(game::Text(0x007846F0));   // "none"
    id = -1;
    spareId = -1;
    position = {0.0f, 0.0f, 0.0f};
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    kind = 0;
    object = nullptr;
    name = none;
    comment = none;
    return this;
}
EI_REPLACE_METHOD(0x00469CA0, Spec::SpecConstruct);

// 00469E50: the destructor body.
void Spec::SpecDestruct() {
    ei::SetVtable(this, kVtableAddress);
    comment.~CString();
    name.~CString();
    chunk.Destruct();
}
EI_REPLACE_METHOD(0x00469E50, Spec::SpecDestruct);

// 00469E30 (slot 10).
Spec* Spec::SpecDelete(unsigned flags) {
    SpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00469E30, Spec::SpecDelete);

// 00469DB0 (slot 4): "error".
mfc::CString* Spec::SpecTypeName(mfc::CString* out) {
    new (out) mfc::CString(game::Text(0x00787DC4));
    return out;
}
EI_REPLACE_METHOD(0x00469DB0, Spec::SpecTypeName);

// 00469DE0 / 00469E00 (slots 6 and 8): slot 7.
void Spec::SpecRemove() { Record()->ApplyObject(); }
EI_REPLACE_DISPATCH(0x00469DE0, Spec::SpecRemove);
void Spec::SpecRefresh() { Record()->ApplyObject(); }
EI_REPLACE_DISPATCH(0x00469E00, Spec::SpecRefresh);

// 00469F20 (slot 2): the object with the record's id.
void* Spec::SpecWorldObject() { return static_cast<CBaseManager*>(*reinterpret_cast<void**>(0x007AF5F8))->FindObject(id); }
EI_REPLACE_METHOD(0x00469F20, Spec::SpecWorldObject);

// 00469F40 (slot 5): moves the record, then slot 7.
void Spec::SpecMove(float dx, float dy, float dz) {
    position.x = dx + position.x;
    position.y = dy + position.y;
    position.z = dz + position.z;
    Record()->ApplyObject();
}
EI_REPLACE_METHOD(0x00469F40, Spec::SpecMove);

// ---- Objects (0073C5F0) ------------------------------------------------------------------------

// 0046A5C0. Returns this.
ObjectSpec* ObjectSpec::ObjectSpecConstruct() {
    base.SpecConstruct();
    new (&model) mfc::CString();
    new (&texture) mfc::CString();
    new (&primaryTexture) mfc::CString();
    new (&parentIname) mfc::CString();
    new (&skinTexture) mfc::CString();
    StringArrayConstruct(&parts);
    ei::SetVtable(this, kVtableAddress);
    mfc::CString none(game::Text(0x007846F0));
    base.kind = 1;
    parentId = 0;
    model = game::EmptyText();
    texture = none;
    primaryTexture = none;
    player = -1;
    scale.y = 0.5f;
    scale.x = 0.5f;
    scale.z = 0.5f;
    shadow = 0;
    visible = 1;
    side = 0;
    parentIname = none;
    skinTexture = none;
    StringArraySetSize(&parts, nullptr, 0, -1);
    return this;
}
EI_REPLACE_METHOD(0x0046A5C0, ObjectSpec::ObjectSpecConstruct);

// 0046AEB0: the destructor body.
void ObjectSpec::ObjectSpecDestruct() {
    ei::SetVtable(this, kVtableAddress);
    StringArrayDestruct(&parts);
    skinTexture.~CString();
    parentIname.~CString();
    primaryTexture.~CString();
    texture.~CString();
    model.~CString();
    base.SpecDestruct();
}
EI_REPLACE_METHOD(0x0046AEB0, ObjectSpec::ObjectSpecDestruct);
// 0058EDF0: the destructor body of the quick item effect's record (vtable 0073FA58, no members of
// its own): a jump to the one above (A-W14).
EI_REPLACE_METHOD(0x0058EDF0, ObjectSpec::ObjectSpecDestruct);

// 0046A6F0 (slot 10).
ObjectSpec* ObjectSpec::ObjectSpecDelete(unsigned flags) {
    ObjectSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046A6F0, ObjectSpec::ObjectSpecDelete);
EI_REPLACE_METHOD(0x0058EDD0, ObjectSpec::ObjectSpecDelete);   // slot 10 of 0073FA58 (through 0058EDF0; A-W14)

// 0046A710: copies `from` (the base fields but its kind and chunk state, then the object's).
// The original goes through a temporary copy of the base record; CStrings share their data
// either way.
void ObjectSpec::ObjectSpecAssign(const ObjectSpec* from) {
    Spec& b = base;
    const Spec& f = from->base;
    b.object = f.object;
    b.spareId = f.spareId;
    b.id = f.id;
    b.position = f.position;
    b.rotation = f.rotation;
    b.name = f.name;
    b.comment = f.comment;
    parentId = from->parentId;
    model = from->model;
    texture = from->texture;
    primaryTexture = from->primaryTexture;
    player = from->player;
    std::memcpy(&scale, &from->scale, sizeof(scale));
    shadow = from->shadow;
    visible = from->visible;
    side = from->side;
    parentIname = from->parentIname;
    skinTexture = from->skinTexture;
    StringArraySetSize(&parts, nullptr, 0, -1);
    StringArrayCopyOriginal(&parts, nullptr, &from->parts);
}
EI_REPLACE_METHOD(0x0046A710, ObjectSpec::ObjectSpecAssign);

// 0046A940 (slot 11): the common fields.
void ObjectSpec::ObjectSpecWriteFields() {
    Chunk& c = base.chunk;
    c.WriteStringArray(0xB00D, &parts);
    c.WriteByte(0xB011, side);
    c.WriteInt(0xB002, base.id);
    c.WriteInt(0xB003, parentId);
    c.WriteString(0xB004, base.name);
    c.WriteString(0xB006, texture);
    c.WriteString(0xB00E, primaryTexture);
    c.WriteString(0xB007, parentIname);
    c.WriteString(0xB008, skinTexture);
    c.WriteString(0xB00F, base.comment);
    WriteVector(c, 0xB009, base.position);
    Quaternion rotation;
    std::memcpy(&rotation, &base.rotation, 16);
    c.WriteQuaternion(0xB00A, rotation);
    c.WriteByte(0xB013, shadow);
    c.WriteByte(0xB014, visible);
    c.WriteInt(0xB012, player);
    c.WriteString(0xB016, model);
    c.WriteHeader(0xB00C, 0xC);
    FileWrite(c, &scale.y, 4);
    FileWrite(c, &scale.x, 4);
    FileWrite(c, &scale.z, 4);
}
EI_REPLACE_METHOD(0x0046A940, ObjectSpec::ObjectSpecWriteFields);

// 0046A920 (slot 9): the record as chunk B001.
void ObjectSpec::ObjectSpecSave() {
    base.chunk.BeginWrite(0xB001);
    base.Record()->WriteFields();
    base.chunk.EndWrite();
}
EI_REPLACE_METHOD(0x0046A920, ObjectSpec::ObjectSpecSave);

// 0046AB20 (slot 0): the chunk just read, by its id.
void ObjectSpec::ObjectSpecOnChunk() {
    Chunk& c = base.chunk;
    switch (c.id_) {
    case 0xB002: base.id = c.ReadInt(); break;
    case 0xB003: parentId = c.ReadInt(); break;
    case 0xB004: ReadInto(c, base.name); break;
    case 0xB006: ReadInto(c, texture); break;
    case 0xB007: ReadInto(c, parentIname); break;
    case 0xB008: ReadInto(c, skinTexture); break;
    case 0xB009: {
        Vector3 v;
        std::memcpy(&base.position, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xB00A: {
        Quaternion q;
        std::memcpy(&base.rotation, c.ReadQuaternion(&q.w), sizeof(Quaternion));
        break;
    }
    case 0xB00C:
        scale.y = c.ReadFloat();
        scale.x = c.ReadFloat();
        scale.z = c.ReadFloat();
        break;
    case 0xB00D: c.ReadStringArray(&parts); break;
    case 0xB00E: ReadInto(c, primaryTexture); break;
    case 0xB00F: ReadInto(c, base.comment); break;
    case 0xB011: side = c.ReadByte(); break;
    case 0xB012: player = c.ReadInt(); break;
    case 0xB013: shadow = ReadFlag(c); break;
    case 0xB014: visible = ReadFlag(c); break;
    case 0xB016: ReadInto(c, model); break;
    default: break;
    }
}
EI_REPLACE_METHOD(0x0046AB20, ObjectSpec::ObjectSpecOnChunk);

// 0046AF70 (slot 6): removes the world object, then slot 7.
void ObjectSpec::ObjectSpecRemove() {
    if (base.object) {
        WorldRemoveObject(World(), nullptr, base.id);
        base.object = nullptr;
    }
    base.Record()->ApplyObject();
}
EI_REPLACE_METHOD(0x0046AF70, ObjectSpec::ObjectSpecRemove);

// 0046AFA0 (slot 8).
void ObjectSpec::ObjectSpecRefresh() {
    if (base.object) static_cast<CMapObject*>(base.object)->MarkChanged(2);
}
EI_REPLACE_METHOD(0x0046AFA0, ObjectSpec::ObjectSpecRefresh);

// 0046AFB0 (slot 7): makes the world object, or updates it from the record (position, rotation,
// scale, visibility) while it is out of the editor's lists.
void ObjectSpec::ObjectSpecApply() {
    if (!base.object) {
        WorldMakeObjectOriginal(World(), nullptr, &base);
        return;
    }
    static_cast<CAIMap*>(Selection())->BeginRefresh();
    SectorMapRemove(AsSectorMap(Lists()), nullptr, AsEngineObject(base.object));
    auto* object = static_cast<CMapObject*>(base.object);
    std::memcpy(&object->position, &base.position, 12);
    std::memcpy(&object->orientation, &base.rotation, 16);
    ObjectSetFigureScaleOriginal(base.object, nullptr, &scale.x);
    std::memcpy(&object->baseScale, &scale, 12);
    object->ApplyBaseScale();
    std::uint32_t& flags = object->flags_;
    if (!visible) flags |= 8;
    else flags &= ~8u;
    static_cast<CMapObject*>(base.object)->MarkChanged(2);
    static_cast<CMapObject*>(base.object)->MarkChanged(1);
    SectorMapAdd(AsSectorMap(Lists()), nullptr, AsEngineObject(base.object));
    static_cast<CAIMap*>(Selection())->EndRefresh();
}
EI_REPLACE_METHOD(0x0046AFB0, ObjectSpec::ObjectSpecApply);

// 0046B090 (slot 4): the player number ("%i").
mfc::CString* ObjectSpec::ObjectSpecTypeName(mfc::CString* out) {
    mfc::CString text;
    text.Format(game::Text(0x007842D8), player);
    new (out) mfc::CString(text);
    return out;
}
EI_REPLACE_METHOD(0x0046B090, ObjectSpec::ObjectSpecTypeName);

// ---- Flames (0073C620) ------------------------------------------------------------------------

// 0046B210. Returns this.
FlameSpec* FlameSpec::FlameSpecConstruct() {
    object.ObjectSpecConstruct();
    new (&sound) mfc::CString();
    offset = {0.0f, 0.0f, 0.0f};
    ei::SetVtable(this, kVtableAddress);
    object.base.kind = 6;
    std::uint32_t volumeBits = 0x3F333333;   // 0.7f
    std::memcpy(&volume, &volumeBits, 4);
    sound = game::Text(0x00787DCC);   // "nature\fire\small.wav"
    return this;
}
EI_REPLACE_METHOD(0x0046B210, FlameSpec::FlameSpecConstruct);

// 0046B120: the destructor body.
void FlameSpec::FlameSpecDestruct() {
    ei::SetVtable(this, kVtableAddress);
    sound.~CString();
    object.ObjectSpecDestruct();
}
EI_REPLACE_METHOD(0x0046B120, FlameSpec::FlameSpecDestruct);

// 0046B1F0 (slot 10).
FlameSpec* FlameSpec::FlameSpecDelete(unsigned flags) {
    FlameSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046B1F0, FlameSpec::FlameSpecDelete);

// 0046B310 (slot 7): the world object is always made again.
void FlameSpec::FlameSpecApply() {
    Spec& b = object.base;
    if (b.object) {
        WorldRemoveObject(World(), nullptr, b.id);
        b.object = nullptr;
    }
    WorldMakeObjectOriginal(World(), nullptr, &b);
}
EI_REPLACE_METHOD(0x0046B310, FlameSpec::FlameSpecApply);

// 0046B340 (slot 9): chunk BBBF0000 with the flame's fields, then the common ones.
void FlameSpec::FlameSpecSave() {
    Chunk& c = object.base.chunk;
    c.BeginWrite(0xBBBF0000);
    WriteVector(c, 0xBBBF0002, offset);
    c.WriteFloat(0xBBBF0001, Bits(volume));
    c.WriteString(0xBBBF0003, sound);
    object.ObjectSpecWriteFields();
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046B340, FlameSpec::FlameSpecSave);

// 0046B3C0 (slot 0).
void FlameSpec::FlameSpecOnChunk() {
    Chunk& c = object.base.chunk;
    switch (c.id_) {
    case 0xBBBF0001: volume = c.ReadFloat(); break;
    case 0xBBBF0002: {
        Vector3 v;
        std::memcpy(&offset, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xBBBF0003: ReadInto(c, sound); break;
    default: object.ObjectSpecOnChunk(); break;
    }
}
EI_REPLACE_METHOD(0x0046B3C0, FlameSpec::FlameSpecOnChunk);

// ---- Magic traps (0073C650) --------------------------------------------------------------------

// 0046B4A0. Returns this.
TrapSpec* TrapSpec::TrapSpecConstruct() {
    object.ObjectSpecConstruct();
    diplomacy = -1;
    new (&spell) mfc::CString();
    areas.data = nullptr;
    areas.growBy = 0;
    areas.maxSize = 0;
    areas.size = 0;
    ei::SetVtable(&areas, mfc::impl::TrapAreasArrayClass::Vtable());
    targets.data = nullptr;
    targets.growBy = 0;
    targets.maxSize = 0;
    targets.size = 0;
    ei::SetVtable(&targets, mfc::impl::TrapTargetsArrayClass::Vtable());
    interval = 15;
    ei::SetVtable(this, kVtableAddress);
    object.base.kind = 7;
    return this;
}
EI_REPLACE_METHOD(0x0046B4A0, TrapSpec::TrapSpecConstruct);

// 0046B570: the destructor body (the arrays' destructors inlined: their blocks freed).
void TrapSpec::TrapSpecDestruct() {
    ei::SetVtable(&targets, mfc::impl::TrapPointArrayClass::Vtable());
    if (targets.data) gamecrt::operator_delete(targets.data);
    ei::SetVtable(&areas, mfc::impl::TrapCircleArrayClass::Vtable());
    if (areas.data) gamecrt::operator_delete(areas.data);
    spell.~CString();
    object.ObjectSpecDestruct();
}
EI_REPLACE_METHOD(0x0046B570, TrapSpec::TrapSpecDestruct);

// 0046B550 (slot 10).
TrapSpec* TrapSpec::TrapSpecDelete(unsigned flags) {
    TrapSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046B550, TrapSpec::TrapSpecDelete);

// 0046B520 (slot 4): "MagicTrap".
mfc::CString* TrapSpec::TrapSpecTypeName(mfc::CString* out) {
    new (out) mfc::CString(game::Text(0x00787DE4));
    return out;
}
EI_REPLACE_METHOD(0x0046B520, TrapSpec::TrapSpecTypeName);

// 0046B680 (slot 9): chunk BBAB0000: the trap's fields (the arrays as a count and the raw
// elements), then the common ones.
void TrapSpec::TrapSpecSave() {
    Chunk& c = object.base.chunk;
    c.BeginWrite(0xBBAB0000);
    c.WriteInt(0xBBAB0001, diplomacy);
    c.WriteString(0xBBAB0002, spell);
    c.WriteInt(0xBBAB0005, interval);
    c.WriteByte(0xBBAC0005, castOnce);
    std::uint32_t count = static_cast<std::uint32_t>(areas.size);
    c.WriteHeader(0xBBAB0003, static_cast<int>(count * 12 + 4));
    FileWrite(c, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(c, &areas.data[i], 12);
    count = static_cast<std::uint32_t>(targets.size);
    c.WriteHeader(0xBBAB0004, static_cast<int>(count * 8 + 4));
    FileWrite(c, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(c, &targets.data[i], 8);
    object.ObjectSpecWriteFields();
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046B680, TrapSpec::TrapSpecSave);

// 0046B7D0 (slot 0).
void TrapSpec::TrapSpecOnChunk() {
    Chunk& c = object.base.chunk;
    switch (c.id_) {
    case 0xBBAB0001: diplomacy = c.ReadInt(); break;
    case 0xBBAB0002: ReadInto(c, spell); break;
    case 0xBBAB0003: {
        std::uint32_t n = static_cast<std::uint32_t>(c.ReadInt());
        areas.SetSize(static_cast<int>(n), -1);
        for (std::uint32_t i = 0; i < n; ++i) {
            FileRead(c, &areas.data[i], 12);
            c.Advance(12);
        }
        break;
    }
    case 0xBBAB0004: {
        int n = c.ReadInt();
        targets.SetSize(n, -1);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            FileRead(c, &targets.data[i], 8);
            c.Advance(8);
        }
        break;
    }
    case 0xBBAB0005: interval = c.ReadInt(); break;
    case 0xBBAC0005: castOnce = ReadFlag(c); break;
    default: object.ObjectSpecOnChunk(); break;
    }
}
EI_REPLACE_METHOD(0x0046B7D0, TrapSpec::TrapSpecOnChunk);

// ---- Levers (0073C6B0) -------------------------------------------------------------------------

// 0046BD40. Returns this.
LeverSpec* LeverSpec::LeverSpecConstruct() {
    object.ObjectSpecConstruct();
    stats[0] = 1;
    stats[1] = 0;
    stats[2] = 0;
    state = 0;
    states = 2;
    cycled = 0;
    castOnce = 0;
    field04 = 0;
    ei::SetVtable(this, kVtableAddress);
    object.base.kind = 8;
    return this;
}
EI_REPLACE_METHOD(0x0046BD40, LeverSpec::LeverSpecConstruct);

// 0046BDC0: the destructor body (the object record's).
void LeverSpec::LeverSpecDestruct() { object.ObjectSpecDestruct(); }
EI_REPLACE_METHOD(0x0046BDC0, LeverSpec::LeverSpecDestruct);

// 0046BDA0 (slot 10).
LeverSpec* LeverSpec::LeverSpecDelete(unsigned flags) {
    LeverSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046BDA0, LeverSpec::LeverSpecDelete);

// 0046BE80 (slot 9): chunk BBAC0000.
void LeverSpec::LeverSpecSave() {
    Chunk& c = object.base.chunk;
    c.BeginWrite(0xBBAC0000);
    c.WriteBlob(0xBBAC0006, stats, 12);
    c.WriteByte(0xBBAC0002, static_cast<std::uint8_t>(state));
    c.WriteByte(0xBBAC0003, static_cast<std::uint8_t>(states));
    c.WriteByte(0xBBAC0004, field04);
    c.WriteByte(0xBBAC0007, cycled);
    c.WriteByte(0xBBAC0008, castOnce);
    object.ObjectSpecWriteFields();
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046BE80, LeverSpec::LeverSpecSave);

// 0046BF20 (slot 0).
void LeverSpec::LeverSpecOnChunk() {
    Chunk& c = object.base.chunk;
    switch (c.id_) {
    case 0xBBAC0002: state = c.ReadByte(); break;
    case 0xBBAC0003: states = c.ReadByte(); break;
    case 0xBBAC0004: field04 = ReadFlag(c); break;
    case 0xBBAC0006: c.ReadBytes(stats, 12); break;
    case 0xBBAC0007: cycled = ReadFlag(c); break;
    case 0xBBAC0008: castOnce = ReadFlag(c); break;
    default: object.ObjectSpecOnChunk(); break;
    }
}
EI_REPLACE_METHOD(0x0046BF20, LeverSpec::LeverSpecOnChunk);

// ---- 0046A4B0: rotating a vector -----------------------------------------------------------------

// `out` = `v` rotated by the quaternion: (w^2 - |q|^2) v + 2w (q x v) + 2 (q . v) q.
Vector3* __fastcall QuaternionRotate(const Quaternion* q, void*, Vector3* out, const Vector3* v) {
    float x = q->x, y = q->y, z = q->z;
    float dot = DotUnrounded<Sum3::ZXY>(x * 2.0f, y * 2.0f, z * 2.0f, v->x, v->y, v->z);   // 2(q . v): doubling is exact
    float ax = dot * x, ay = dot * y, az = dot * z;
    float w2 = q->w + q->w;
    float bx = (y * v->z - z * v->y) * w2;
    float by = (z * v->x - x * v->z) * w2;
    float bz = (x * v->y - y * v->x) * w2;
    float m = q->w * q->w - LengthSquaredUnrounded<Sum3::ZYX>(x, y, z);
    out->x = (m * v->x + bx) + ax;
    out->y = (m * v->y + by) + ay;
    out->z = (m * v->z + bz) + az;
    return out;
}
EI_REPLACE(0x0046A4B0, QuaternionRotate);

// ---- 0046E5E0: the zone's diplomacy table --------------------------------------------------------

namespace {
} // namespace

// The diplomacy of the zone's 32 players (0x1014 bytes; made by the zone loader 00481660): every
// pair 1 but a player with itself (0), and the player names "Player-<i>". Returns this.
ZoneDiplomacy* __fastcall DiplomacyConstruct(ZoneDiplomacy* self) {
    mfc::CStringArray* names = &self->names;
    StringArrayConstruct(names);
    for (auto& row : self->relations) {
        for (std::int32_t& relation : row) relation = 1;
    }
    for (int i = 0; i < 32; ++i) self->relations[i][i] = 0;
    for (int i = 0; i < 32; ++i) {
        StringArraySetAtGrowTextOriginal(names, nullptr, names->size, game::EmptyText());
        names->data[i].Format(game::Text(0x00787E44), i);   // "Player-%i"
    }
    return self;
}
EI_REPLACE(0x0046E5E0, DiplomacyConstruct);

} // namespace mapspec
