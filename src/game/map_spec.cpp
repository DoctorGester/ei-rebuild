// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses. Plain float maths
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "base_manager.h"
#include "map_object_figures.h"
#include "map_spec.h"
#include "game_strings.h"
#include "map_object.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "lib/mfc_slots.h"
#include "ei/vcall.h"
#include "game_data.h"
#include "game_singletons.h"
#include "sector_map.h"
#include "world.h"
#include "lib/str.h"

namespace mapspec {

namespace {


// The objects manager (0x7AF5F8) look-up by id; the world server: an object removed by id, an object made
// from a record; the editor's selection and object lists the world object's own updates.
CAIMap* Selection() { return TheServerWorld()->AIMap(); }

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// A string chunk into `field` (through a temporary).
void ReadInto(Chunk& c, std::string& field) {
    mfc::CString read(mfc::CString::kUnset);
    field = c.ReadString(&read)->GetString();
}
bool ReadFlag(Chunk& c) { return c.ReadByte() == 1; }

// The chunk's file (an MFC CFile) through its vtable.
void FileWrite(Chunk& c, const void* data, unsigned size) { c.File()->Write(data, size); }
void FileRead(Chunk& c, void* data, unsigned size) { c.File()->Read(data, size); }

void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

} // namespace

// ---- The base record ---------------------------------------------------------------

void* Spec::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void Spec::operator delete(void* block) { gamecrt::operator_delete(block); }

Spec::Spec() : Chunk(nullptr) {
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    std::string none("none");
    id = -1;
    spareId = -1;
    position = {0.0f, 0.0f, 0.0f};
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    kind = 0;
    object = nullptr;
    name = none;
    comment = none;
}
EI_REWRITTEN_SPECIAL(0x00469CA0, Spec::Spec);

Spec::~Spec() = default;
EI_REWRITTEN_SPECIAL(0x00469E50, Spec::~Spec);
EI_REWRITTEN_SPECIAL(0x00469E30, Spec::~Spec);

mfc::CString* Spec::TypeName(mfc::CString* out) {
    std::construct_at(out, "error");
    return out;
}
EI_REWRITTEN(0x00469DB0, Spec::TypeName);

void Spec::Remove() { Apply(); }
EI_REWRITTEN(0x00469DE0, Spec::Remove);
void Spec::Refresh() { Apply(); }
EI_REWRITTEN(0x00469E00, Spec::Refresh);

void* Spec::WorldObject() { return TheObjectManager()->FindObject(id); }
EI_REWRITTEN(0x00469F20, Spec::WorldObject);

void Spec::Move(float dx, float dy, float dz) {
    position.x = dx + position.x;
    position.y = dy + position.y;
    position.z = dz + position.z;
    Apply();
}
EI_REWRITTEN(0x00469F40, Spec::Move);


ObjectSpec::ObjectSpec() {
    parts.Construct();
    std::string none("none");
    kind = 1;
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
    parts.SetSize(0, -1);
}
EI_REWRITTEN_SPECIAL(0x0046A5C0, ObjectSpec::ObjectSpec);

ObjectSpec::~ObjectSpec() { parts.Destruct(); }
EI_REWRITTEN_SPECIAL(0x0046AEB0, ObjectSpec::~ObjectSpec);
EI_REWRITTEN_SPECIAL(0x0046A6F0, ObjectSpec::~ObjectSpec);
EI_REWRITTEN_SPECIAL(0x0058EDF0, ObjectSpec::~ObjectSpec);   // the quick item effect record's twin (UseQuickItem)
EI_REWRITTEN_SPECIAL(0x0058EDD0, ObjectSpec::~ObjectSpec);

// The original goes through a temporary copy of the base record; CStrings share their data either way.
void ObjectSpec::Assign(const ObjectSpec* from) {
    object = from->object;
    spareId = from->spareId;
    id = from->id;
    position = from->position;
    rotation = from->rotation;
    name = from->name;
    comment = from->comment;
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
    parts.SetSize(0, -1);
    parts.Copy(from->parts);
}
EI_REWRITTEN(0x0046A710, ObjectSpec::Assign);

void ObjectSpec::WriteFields() {
    WriteStringArray(0xB00D, &parts);
    WriteByte(0xB011, side);
    WriteInt(0xB002, id);
    WriteInt(0xB003, parentId);
    WriteString(0xB004, name);
    WriteString(0xB006, texture);
    WriteString(0xB00E, primaryTexture);
    WriteString(0xB007, parentIname);
    WriteString(0xB008, skinTexture);
    WriteString(0xB00F, comment);
    WriteVector(*this, 0xB009, position);
    Quaternion turn;
    std::memcpy(&turn, &rotation, 16);
    WriteQuaternion(0xB00A, turn);
    WriteByte(0xB013, shadow);
    WriteByte(0xB014, visible);
    WriteInt(0xB012, player);
    WriteString(0xB016, model);
    WriteHeader(0xB00C, 0xC);
    FileWrite(*this, &scale.y, 4);
    FileWrite(*this, &scale.x, 4);
    FileWrite(*this, &scale.z, 4);
}
EI_REWRITTEN(0x0046A940, ObjectSpec::WriteFields);

void ObjectSpec::Save() {
    BeginWrite(0xB001);
    WriteFields();
    EndWrite();
}
EI_REWRITTEN(0x0046A920, ObjectSpec::Save);

void ObjectSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xB002: id = c.ReadInt(); break;
    case 0xB003: parentId = c.ReadInt(); break;
    case 0xB004: ReadInto(c, name); break;
    case 0xB006: ReadInto(c, texture); break;
    case 0xB007: ReadInto(c, parentIname); break;
    case 0xB008: ReadInto(c, skinTexture); break;
    case 0xB009: {
        Vector3 v;
        std::memcpy(&position, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xB00A: {
        Quaternion q;
        std::memcpy(&rotation, c.ReadQuaternion(&q.w), sizeof(Quaternion));
        break;
    }
    case 0xB00C:
        scale.y = c.ReadFloat();
        scale.x = c.ReadFloat();
        scale.z = c.ReadFloat();
        break;
    case 0xB00D: c.ReadStringArray(&parts); break;
    case 0xB00E: ReadInto(c, primaryTexture); break;
    case 0xB00F: ReadInto(c, comment); break;
    case 0xB011: side = c.ReadByte(); break;
    case 0xB012: player = c.ReadInt(); break;
    case 0xB013: shadow = ReadFlag(c); break;
    case 0xB014: visible = ReadFlag(c); break;
    case 0xB016: ReadInto(c, model); break;
    default: break;
    }
}
EI_REWRITTEN(0x0046AB20, ObjectSpec::OnChunk);

void ObjectSpec::Remove() {
    if (object) {
        static_cast<CWorldServer*>(TheServerWorld())->RemoveObject(id);
        object = nullptr;
    }
    Apply();
}
EI_REWRITTEN(0x0046AF70, ObjectSpec::Remove);

CMapObject* ObjectSpec::MapObject() const { return static_cast<CMapObject*>(object); }

void ObjectSpec::Refresh() {
    if (object) MapObject()->MarkChanged(2);
}
EI_REWRITTEN(0x0046AFA0, ObjectSpec::Refresh);

void ObjectSpec::Apply() {
    if (!object) {
        static_cast<CWorldServer*>(TheServerWorld())->CreateObject(this);
        return;
    }
    Selection()->BeginRefresh();
    TheSectorMap()->Remove(MapObject());
    CMapObject* mapObject = MapObject();
    std::memcpy(&mapObject->localPosition_, &position, 12);
    std::memcpy(&mapObject->localRotation_, &rotation, 16);
    static_cast<CObject3D*>(object)->SetFigureScale(&scale.x);
    std::memcpy(&mapObject->baseScale, &scale, 12);
    mapObject->ApplyBaseScale();
    std::uint32_t& flags = mapObject->flags_;
    if (!visible) flags |= 8;
    else flags &= ~8u;
    MapObject()->MarkChanged(2);
    MapObject()->MarkChanged(1);
    TheSectorMap()->Add(MapObject());
    Selection()->EndRefresh();
}
EI_REWRITTEN(0x0046AFB0, ObjectSpec::Apply);

mfc::CString* ObjectSpec::TypeName(mfc::CString* out) {
    std::string text;
    text = str::Format("%i", player);
    std::construct_at(out, text);
    return out;
}
EI_REWRITTEN(0x0046B090, ObjectSpec::TypeName);


FlameSpec::FlameSpec() {
    offset = {0.0f, 0.0f, 0.0f};
    kind = 6;
    std::uint32_t volumeBits = 0x3F333333;   // 0.7f
    std::memcpy(&volume, &volumeBits, 4);
    sound = "nature\\fire\\small.wav";
}
EI_REWRITTEN_SPECIAL(0x0046B210, FlameSpec::FlameSpec);

FlameSpec::~FlameSpec() = default;
EI_REWRITTEN_SPECIAL(0x0046B120, FlameSpec::~FlameSpec);
EI_REWRITTEN_SPECIAL(0x0046B1F0, FlameSpec::~FlameSpec);

void FlameSpec::Apply() {
    if (object) {
        static_cast<CWorldServer*>(TheServerWorld())->RemoveObject(id);
        object = nullptr;
    }
    static_cast<CWorldServer*>(TheServerWorld())->CreateObject(this);
}
EI_REWRITTEN(0x0046B310, FlameSpec::Apply);

void FlameSpec::Save() {
    BeginWrite(0xBBBF0000);
    WriteVector(*this, 0xBBBF0002, offset);
    WriteFloat(0xBBBF0001, Bits(volume));
    WriteString(0xBBBF0003, sound);
    ObjectSpec::WriteFields();
    EndWrite();
}
EI_REWRITTEN(0x0046B340, FlameSpec::Save);

void FlameSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBBF0001: volume = c.ReadFloat(); break;
    case 0xBBBF0002: {
        Vector3 v;
        std::memcpy(&offset, c.ReadVector3(&v), sizeof(Vector3));
        break;
    }
    case 0xBBBF0003: ReadInto(c, sound); break;
    default: ObjectSpec::OnChunk(); break;
    }
}
EI_REWRITTEN(0x0046B3C0, FlameSpec::OnChunk);

// ---- Magic traps --------------------------------------------------------------------

TrapSpec::TrapSpec() {
    trap.sides = ~0u;
    trap.area.data = nullptr;
    trap.area.growBy = 0;
    trap.area.maxSize = 0;
    trap.area.size = 0;
    ei::SetVtable(&trap.area, mfc::impl::TrapAreasArrayClass::Vtable());
    trap.points.data = nullptr;
    trap.points.growBy = 0;
    trap.points.maxSize = 0;
    trap.points.size = 0;
    ei::SetVtable(&trap.points, mfc::impl::TrapTargetsArrayClass::Vtable());
    trap.period = 15;
    kind = 7;
}
EI_REWRITTEN_SPECIAL(0x0046B4A0, TrapSpec::TrapSpec);

// The spell's text goes with the trap's members, after this.
TrapSpec::~TrapSpec() {
    ei::SetVtable(&trap.points, mfc::impl::TrapPointArrayClass::Vtable());
    if (trap.points.data) gamecrt::operator_delete(trap.points.data);
    ei::SetVtable(&trap.area, mfc::impl::TrapCircleArrayClass::Vtable());
    if (trap.area.data) gamecrt::operator_delete(trap.area.data);
}
EI_REWRITTEN_SPECIAL(0x0046B570, TrapSpec::~TrapSpec);
EI_REWRITTEN_SPECIAL(0x0046B550, TrapSpec::~TrapSpec);

mfc::CString* TrapSpec::TypeName(mfc::CString* out) {
    std::construct_at(out, "MagicTrap");
    return out;
}
EI_REWRITTEN(0x0046B520, TrapSpec::TypeName);

void TrapSpec::Save() {
    BeginWrite(0xBBAB0000);
    WriteInt(0xBBAB0001, static_cast<int>(trap.sides));
    WriteString(0xBBAB0002, trap.spell);
    WriteInt(0xBBAB0005, trap.period);
    WriteByte(0xBBAC0005, trap.singleTarget);
    std::uint32_t count = static_cast<std::uint32_t>(trap.area.size);
    WriteHeader(0xBBAB0003, static_cast<int>(count * 12 + 4));
    FileWrite(*this, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(*this, &trap.area.data[i], 12);
    count = static_cast<std::uint32_t>(trap.points.size);
    WriteHeader(0xBBAB0004, static_cast<int>(count * 8 + 4));
    FileWrite(*this, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(*this, &trap.points.data[i], 8);
    ObjectSpec::WriteFields();
    EndWrite();
}
EI_REWRITTEN(0x0046B680, TrapSpec::Save);

void TrapSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBAB0001: trap.sides = static_cast<std::uint32_t>(c.ReadInt()); break;
    case 0xBBAB0002: ReadInto(c, trap.spell); break;
    case 0xBBAB0003: {
        std::uint32_t n = static_cast<std::uint32_t>(c.ReadInt());
        trap.area.SetSize(static_cast<int>(n), -1);
        for (std::uint32_t i = 0; i < n; ++i) {
            FileRead(c, &trap.area.data[i], 12);
            c.Advance(12);
        }
        break;
    }
    case 0xBBAB0004: {
        int n = c.ReadInt();
        trap.points.SetSize(n, -1);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            FileRead(c, &trap.points.data[i], 8);
            c.Advance(8);
        }
        break;
    }
    case 0xBBAB0005: trap.period = c.ReadInt(); break;
    case 0xBBAC0005: trap.singleTarget = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}
EI_REWRITTEN(0x0046B7D0, TrapSpec::OnChunk);


LeverSpec::LeverSpec() {
    lever.lock.mask = 1;
    lever.lock.id = 0;
    lever.lock.level = 0;
    lever.state = 0;
    lever.stateCount = 2;
    lever.flag260 = 0;
    lever.keepsMapFlag = 0;
    lever.cycling = 0;
    kind = 8;
}
EI_REWRITTEN_SPECIAL(0x0046BD40, LeverSpec::LeverSpec);

LeverSpec::~LeverSpec() = default;
EI_REWRITTEN_SPECIAL(0x0046BDC0, LeverSpec::~LeverSpec);
EI_REWRITTEN_SPECIAL(0x0046BDA0, LeverSpec::~LeverSpec);

void LeverSpec::Save() {
    BeginWrite(0xBBAC0000);
    WriteBlob(0xBBAC0006, &lever.lock, 12);
    WriteByte(0xBBAC0002, static_cast<std::uint8_t>(lever.state));
    WriteByte(0xBBAC0003, static_cast<std::uint8_t>(lever.stateCount));
    WriteByte(0xBBAC0004, lever.cycling);
    WriteByte(0xBBAC0007, lever.flag260);
    WriteByte(0xBBAC0008, lever.keepsMapFlag);
    ObjectSpec::WriteFields();
    EndWrite();
}
EI_REWRITTEN(0x0046BE80, LeverSpec::Save);

void LeverSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBAC0002: lever.state = c.ReadByte(); break;
    case 0xBBAC0003: lever.stateCount = c.ReadByte(); break;
    case 0xBBAC0004: lever.cycling = ReadFlag(c); break;
    case 0xBBAC0006: c.ReadBytes(&lever.lock, 12); break;
    case 0xBBAC0007: lever.flag260 = ReadFlag(c); break;
    case 0xBBAC0008: lever.keepsMapFlag = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}
EI_REWRITTEN(0x0046BF20, LeverSpec::OnChunk);

// ---- Rotating a vector ---------------------------------------------------------------------------

} // namespace mapspec

// `out` = `v` rotated by the quaternion: (w^2 - |q|^2) v + 2w (q x v) + 2 (q . v) q.
Vector3* Quaternion::Rotate(Vector3* out, const Vector3* v) const {
    float dot = DotUnrounded<Sum3::ZXY>(x * 2.0f, y * 2.0f, z * 2.0f, v->x, v->y, v->z);   // 2(q . v): doubling is exact
    float ax = dot * x, ay = dot * y, az = dot * z;
    float w2 = w + w;
    float bx = (y * v->z - z * v->y) * w2;
    float by = (z * v->x - x * v->z) * w2;
    float bz = (x * v->y - y * v->x) * w2;
    float m = w * w - LengthSquaredUnrounded<Sum3::ZYX>(x, y, z);
    out->x = (m * v->x + bx) + ax;
    out->y = (m * v->y + by) + ay;
    out->z = (m * v->z + bz) + az;
    return out;
}
EI_REWRITTEN(0x0046A4B0, Quaternion::Rotate);

namespace mapspec {

// ---- The zone's diplomacy table ------------------------------------------------------------------

// The diplomacy of the zone's 32 players (made by the zone loader): every pair 1 but a player
// with itself (0), and the player names "Player-<i>".
ZoneDiplomacy* __fastcall DiplomacyConstruct(ZoneDiplomacy* self) {
    mfc::CStringArray* names = &self->names;
    names->Construct();
    for (auto& row : self->relations) {
        for (std::int32_t& relation : row) relation = 1;
    }
    for (int i = 0; i < 32; ++i) self->relations[i][i] = 0;
    for (int i = 0; i < 32; ++i) {
        names->SetAtGrow(names->size, game::EmptyText());
        names->data[i].Format("Player-%i", i);
    }
    return self;
}
EI_REWRITTEN(0x0046E5E0, DiplomacyConstruct);

} // namespace mapspec
