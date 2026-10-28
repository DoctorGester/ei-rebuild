// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses. Plain float maths
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"
#include "base_manager.h"
#include "map_object_figures.h"
#include "map_spec.h"
#include "game_strings.h"
#include "map_object.h"

#include "lib/crt.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "game_singletons.h"
#include "sector_map.h"
#include "world.h"
#include "lib/str.h"

namespace mapspec {

namespace {


// The objects manager (0x7AF5F8) look-up by id; the world server: an object removed by id, an object made
// from a record; the editor's selection and object lists the world object's own updates.

std::uint32_t Bits(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// A string chunk into `field` (through a temporary).
void ReadInto(Chunk& c, std::string& field) {
    std::string read;
    field = c.ReadString(&read)->c_str();
}
bool ReadFlag(Chunk& c) { return c.ReadByte() == 1; }

// The chunk's file.
void FileWrite(Chunk& c, const void* data, unsigned size) { c.File()->Write(data, size); }
void FileRead(Chunk& c, void* data, unsigned size) { c.File()->Read(data, size); }

void WriteVector(Chunk& c, std::uint32_t id, const Vector3& v) { c.WriteVector3(id, Bits(v.x), Bits(v.y), Bits(v.z)); }

} // namespace

// ---- The base record ---------------------------------------------------------------


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

Spec::~Spec() = default;

std::string* Spec::TypeName(std::string* out) {
    *out = "error";
    return out;
}

void Spec::Remove() { Apply(); }
void Spec::Refresh() { Apply(); }

void* Spec::WorldObject() { return g_clientWorld->objectManager_->FindObject(id); }

void Spec::Move(float dx, float dy, float dz) {
    position.x = dx + position.x;
    position.y = dy + position.y;
    position.z = dz + position.z;
    Apply();
}

ObjectSpec::ObjectSpec() {
    std::string none("none");
    kind = 1;
    parentId = 0;
    model = game::kEmptyText;
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
}

ObjectSpec::~ObjectSpec() = default;

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
    turn = rotation;
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

void ObjectSpec::Save() {
    BeginWrite(0xB001);
    WriteFields();
    EndWrite();
}

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
        position = *c.ReadVector3(&v);
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

void ObjectSpec::Remove() {
    if (object) {
        g_serverWorld->RemoveObject(id);
        object = nullptr;
    }
    Apply();
}

CMapObject* ObjectSpec::MapObject() const { return static_cast<CMapObject*>(object); }

void ObjectSpec::Refresh() {
    if (object) MapObject()->MarkChanged(2);
}

void ObjectSpec::Apply() {
    if (!object) {
        g_serverWorld->CreateObject(this);
        return;
    }
    g_serverWorld->aiMap_.BeginRefresh();
    g_serverWorld->aiMap_.sectorMap.Remove(MapObject());
    CMapObject* mapObject = MapObject();
    mapObject->localPosition_ = position;
    mapObject->localRotation_ = rotation;
    mapObject->SetFigureScale(&scale.x);
    mapObject->baseScale = scale;
    mapObject->ApplyBaseScale();
    std::uint32_t& flags = mapObject->flags_;
    if (!visible) flags |= 8;
    else flags &= ~8u;
    MapObject()->MarkChanged(2);
    MapObject()->MarkChanged(1);
    g_serverWorld->aiMap_.sectorMap.Add(MapObject());
    g_serverWorld->aiMap_.EndRefresh();
}

std::string* ObjectSpec::TypeName(std::string* out) {
    std::string text;
    text = str::Format("%i", player);
    *out = text;
    return out;
}

FlameSpec::FlameSpec() {
    offset = {0.0f, 0.0f, 0.0f};
    kind = 6;
    std::uint32_t volumeBits = 0x3F333333;   // 0.7f
    std::memcpy(&volume, &volumeBits, 4);
    sound = "nature\\fire\\small.wav";
}

FlameSpec::~FlameSpec() = default;

void FlameSpec::Apply() {
    if (object) {
        g_serverWorld->RemoveObject(id);
        object = nullptr;
    }
    g_serverWorld->CreateObject(this);
}

void FlameSpec::Save() {
    BeginWrite(0xBBBF0000);
    WriteVector(*this, 0xBBBF0002, offset);
    WriteFloat(0xBBBF0001, Bits(volume));
    WriteString(0xBBBF0003, sound);
    ObjectSpec::WriteFields();
    EndWrite();
}

void FlameSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBBF0001: volume = c.ReadFloat(); break;
    case 0xBBBF0002: {
        Vector3 v;
        offset = *c.ReadVector3(&v);
        break;
    }
    case 0xBBBF0003: ReadInto(c, sound); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

// ---- Magic traps --------------------------------------------------------------------

TrapSpec::TrapSpec() {
    trap.sides = ~0u;
    trap.period = 15;
    kind = 7;
}

TrapSpec::~TrapSpec() = default;

std::string* TrapSpec::TypeName(std::string* out) {
    *out = "MagicTrap";
    return out;
}

void TrapSpec::Save() {
    BeginWrite(0xBBAB0000);
    WriteInt(0xBBAB0001, static_cast<int>(trap.sides));
    WriteString(0xBBAB0002, trap.spell);
    WriteInt(0xBBAB0005, trap.period);
    WriteByte(0xBBAC0005, trap.singleTarget);
    std::uint32_t count = static_cast<std::uint32_t>(trap.area.size());
    WriteHeader(0xBBAB0003, static_cast<int>(count * 12 + 4));
    FileWrite(*this, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(*this, &trap.area[i], 12);
    count = static_cast<std::uint32_t>(trap.points.size());
    WriteHeader(0xBBAB0004, static_cast<int>(count * 8 + 4));
    FileWrite(*this, &count, 4);
    for (std::uint32_t i = 0; i < count; ++i) FileWrite(*this, &trap.points[i], 8);
    ObjectSpec::WriteFields();
    EndWrite();
}

void TrapSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBAB0001: trap.sides = static_cast<std::uint32_t>(c.ReadInt()); break;
    case 0xBBAB0002: ReadInto(c, trap.spell); break;
    case 0xBBAB0003: {
        std::uint32_t n = static_cast<std::uint32_t>(c.ReadInt());
        trap.area.resize(static_cast<int>(n));
        for (std::uint32_t i = 0; i < n; ++i) {
            FileRead(c, &trap.area[i], 12);
            c.Advance(12);
        }
        break;
    }
    case 0xBBAB0004: {
        int n = c.ReadInt();
        trap.points.resize(n);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) {
            FileRead(c, &trap.points[i], 8);
            c.Advance(8);
        }
        break;
    }
    case 0xBBAB0005: trap.period = c.ReadInt(); break;
    case 0xBBAC0005: trap.singleTarget = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

LeverSpec::LeverSpec() {
    lever.lock.mask = 1;
    lever.lock.id = 0;
    lever.lock.level = 0;
    lever.state = 0;
    lever.stateCount = 2;
    lever.directUseBlocked = 0;
    lever.keepsMapFlag = 0;
    lever.cycling = 0;
    kind = 8;
}

LeverSpec::~LeverSpec() = default;

void LeverSpec::Save() {
    BeginWrite(0xBBAC0000);
    WriteBlob(0xBBAC0006, &lever.lock, 12);
    WriteByte(0xBBAC0002, static_cast<std::uint8_t>(lever.state));
    WriteByte(0xBBAC0003, static_cast<std::uint8_t>(lever.stateCount));
    WriteByte(0xBBAC0004, lever.cycling);
    WriteByte(0xBBAC0007, lever.directUseBlocked);
    WriteByte(0xBBAC0008, lever.keepsMapFlag);
    ObjectSpec::WriteFields();
    EndWrite();
}

void LeverSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xBBAC0002: lever.state = c.ReadByte(); break;
    case 0xBBAC0003: lever.stateCount = c.ReadByte(); break;
    case 0xBBAC0004: lever.cycling = ReadFlag(c); break;
    case 0xBBAC0006: c.ReadBytes(&lever.lock, 12); break;
    case 0xBBAC0007: lever.directUseBlocked = ReadFlag(c); break;
    case 0xBBAC0008: lever.keepsMapFlag = ReadFlag(c); break;
    default: ObjectSpec::OnChunk(); break;
    }
}

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

namespace mapspec {

// ---- The zone's diplomacy table ------------------------------------------------------------------

// The diplomacy of the zone's 32 players (made by the zone loader): every pair 1 but a player
// with itself (0), and the player names "Player-<i>".
ZoneDiplomacy::ZoneDiplomacy() {
    for (auto& row : relations) {
        for (std::int32_t& relation : row) relation = 1;
    }
    for (int i = 0; i < 32; ++i) relations[i][i] = 0;
    for (int i = 0; i < 32; ++i) names.push_back(str::Format("Player-%i", i));
}

} // namespace mapspec
