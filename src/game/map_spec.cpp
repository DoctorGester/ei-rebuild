// The zone file's object records: the base record, objects, flames, magic traps and levers
// (map_spec.h), and the quaternion rotation the editor uses. Plain float maths
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "ai_map.h"

#include "base_manager.h"
#include "core_transform.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_spec.h"
#include "sector_map.h"
#include "terrain.h"
#include "world.h"
#include "world_effect.h"
#include "lib/crt.h"

#include <bit>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <string>

namespace mapspec {

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t b;
    b = std::bit_cast<std::uint32_t>(f);
    return b;
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

void Spec::Remove() { Apply(); }
void Spec::Refresh() { Apply(); }

void* Spec::WorldObject() { return g_clientWorld.objectManager->FindObject(id); }

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
    switch (c.id) {
    case 0xB002: id = c.ReadInt(); break;
    case 0xB003: parentId = c.ReadInt(); break;
    case 0xB004: name = c.ReadString(); break;
    case 0xB006: texture = c.ReadString(); break;
    case 0xB007: parentIname = c.ReadString(); break;
    case 0xB008: skinTexture = c.ReadString(); break;
    case 0xB009: position = c.ReadVector3(); break;
    case 0xB00A: rotation = c.ReadQuaternion(); break;
    case 0xB00C:
        scale.y = c.ReadFloat();
        scale.x = c.ReadFloat();
        scale.z = c.ReadFloat();
        break;
    case 0xB00D: parts = c.ReadStringArray(); break;
    case 0xB00E: primaryTexture = c.ReadString(); break;
    case 0xB00F: comment = c.ReadString(); break;
    case 0xB011: side = c.ReadByte(); break;
    case 0xB012: player = c.ReadInt(); break;
    case 0xB013: shadow = ReadFlag(c); break;
    case 0xB014: visible = ReadFlag(c); break;
    case 0xB016: model = c.ReadString(); break;
    default: break;
    }
}

void ObjectSpec::Remove() {
    if (object) {
        g_serverWorld.RemoveObject(id);
        object = nullptr;
    }
    Apply();
}

MapObject* ObjectSpec::MadeMapObject() const { return static_cast<MapObject*>(object); }

void ObjectSpec::Refresh() {
    if (object) MadeMapObject()->MarkChanged(2);
}

void ObjectSpec::Apply() {
    if (!object) {
        g_serverWorld.CreateObject(this);
        return;
    }
    g_serverWorld.aiMap.BeginRefresh();
    g_serverWorld.aiMap.sectorMap.Remove(MadeMapObject());
    MapObject* mapObject = MadeMapObject();
    mapObject->localPosition = position;
    mapObject->localRotation = rotation;
    mapObject->SetFigureScale(&scale.x);
    mapObject->baseScale = scale;
    mapObject->ApplyBaseScale();
    std::uint32_t& flags = mapObject->flags;
    if (!visible) flags |= 8;
    else flags &= ~8u;
    MadeMapObject()->MarkChanged(2);
    MadeMapObject()->MarkChanged(1);
    g_serverWorld.aiMap.sectorMap.Add(MadeMapObject());
    g_serverWorld.aiMap.EndRefresh();
}

FlameSpec::FlameSpec() {
    offset = {0.0f, 0.0f, 0.0f};
    kind = 6;
    volume = 0.7f;
    sound = "nature\\fire\\small.wav";
}

FlameSpec::~FlameSpec() = default;

void FlameSpec::Apply() {
    if (object) {
        g_serverWorld.RemoveObject(id);
        object = nullptr;
    }
    g_serverWorld.CreateObject(this);
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
    switch (c.id) {
    case 0xBBBF0001: volume = c.ReadFloat(); break;
    case 0xBBBF0002: offset = c.ReadVector3(); break;
    case 0xBBBF0003: sound = c.ReadString(); break;
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
    switch (c.id) {
    case 0xBBAB0001: trap.sides = static_cast<std::uint32_t>(c.ReadInt()); break;
    case 0xBBAB0002: trap.spell = c.ReadString(); break;
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
    switch (c.id) {
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

namespace mapspec {

// ---- The zone's diplomacy table ------------------------------------------------------------------

// The diplomacy of the zone's 32 players (made by the zone loader): every pair 1 but a player
// with itself (0), and the player names "Player-<i>".
ZoneDiplomacy::ZoneDiplomacy() {
    for (auto& row : relations) {
        for (std::int32_t& relation : row) relation = 1;
    }
    for (int i = 0; i < 32; ++i) relations[i][i] = 0;
    for (int i = 0; i < 32; ++i) names.push_back(std::format("Player-{}", i));
}

} // namespace mapspec

// --- Effect ------------------------------------------------------------------------------------------

// The zone file's effect records: particle sources (kind 4), lights (kind 2) and sounds (kind 3) on their base
// (map_spec.h). They keep the world object they made and its effect, are kept above the terrain when moved,
// and save as chunks DD01 / AA01 / CC01. See docs/notes/objects.md ("Rewritten").

namespace mapspec {

void EffectNotify(const std::string& kind, const std::string& family);
namespace {


// The notification made before a new effect: (kind, family) strings by value, built in their argument slots
// right to left (the second first, like the original) and destroyed by the callee.
void Notify(const char* kind, const char* family) { EffectNotify(std::string(kind), std::string(family)); }

// A new world object for the record and its effect.
void MakeEffect(EffectSpec* self) {
    g_serverWorld.CreateObject(self);
    self->effect = g_serverWorld.timedObjectManager->FindObject(self->id);
}

} // namespace

// ---- The base ------------------------------------------------------------------------

// The records' destructors are the base record's.
EffectSpec::~EffectSpec() = default;
ParticleSpec::~ParticleSpec() = default;
LightSpec::~LightSpec() = default;

void* EffectSpec::WorldObject() {
    return g_clientWorld.objectManager->FindObject(-3001 - id);
}

TransformObject* EffectSpec::Transform() const { return static_cast<TransformObject*>(object); }

void EffectSpec::SetVisible(std::uint8_t newVisible) {
    visible = newVisible;
    std::uint32_t& flags = object->flags;
    if (newVisible != 0) flags &= ~0x40u;
    else flags |= 0x40;
}

void EffectSpec::Move(float dx, float dy, float dz) {
    Vector3& p = position;
    p.x = dx + p.x;
    p.y = dy + p.y;
    p.z = dz + p.z;
    float ground = g_clientWorld.terrain->HeightAt(p.x, p.y, terrain::Layer::Ground);
    if (ground > p.z) p.z = ground;
    Apply();
}

// Takes the two strings by value and only destroys them (the callee destroys its by-value parameters).
void EffectNotify(const std::string&, const std::string&) {}

// ---- Particle sources ------------------------------------------------------------------

ParticleSpec::ParticleSpec() {
    type = 0;
    kind = 4;
    scale = 1.0f;
}

void ParticleSpec::Save() {
    BeginWrite(0xDD01);
    WriteInt(0xDD02, id);
    WriteVector(*this, 0xDD03, position);
    WriteString(0xDD05, name);
    WriteString(0xDD04, comment);
    WriteInt(0xDD06, type);
    WriteFloat(0xDD07, Bits(scale));
    EndWrite();
}

void ParticleSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xDD02: id = c.ReadInt(); break;
    case 0xDD03: position = c.ReadVector3(); break;
    case 0xDD04: comment = c.ReadString(); break;
    case 0xDD05: name = c.ReadString(); break;
    case 0xDD06: type = c.ReadInt(); break;
    case 0xDD07: scale = c.ReadFloat(); break;
    default: break;
    }
}

void ParticleSpec::Apply() {
    if (!object) {
        Notify("efcu3", "particles");
        if (type) MakeEffect(this);
        return;
    }
    auto* particle = static_cast<EffectParticle*>(effect);
    Transform()->localPosition = position;
    particle->size = scale;
    particle->type = type;
    particle->localPosition = position;
    particle->MarkChanged();
}

LightSpec::LightSpec() {
    colour.x = 1.0f;
    colour.y = 1.0f;
    shadows = 1;
    kind = 2;
    radius = 10.0f;
    colour.z = 1.0f;
}

void LightSpec::Save() {
    BeginWrite(0xAA01);
    WriteInt(0xAA05, id);
    WriteByte(0xAA06, shadows);
    WriteVector(*this, 0xAA04, position);
    WriteVector(*this, 0xAA07, colour);
    WriteFloat(0xAA02, Bits(radius));
    WriteString(0xAA03, name);
    WriteString(0xAA08, comment);
    EndWrite();
}

void LightSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xAA02: radius = c.ReadFloat(); break;
    case 0xAA03: name = c.ReadString(); break;
    case 0xAA04: position = c.ReadVector3(); break;
    case 0xAA05: id = c.ReadInt(); break;
    case 0xAA06: shadows = c.ReadByte() != 0; break;
    case 0xAA07: colour = c.ReadVector3(); break;
    case 0xAA08: comment = c.ReadString(); break;
    default: break;
    }
}

void LightSpec::Apply() {
    if (!object) {
        Notify("efcu0", "light");
        MakeEffect(this);
        return;
    }
    auto* light = static_cast<EffectPointLight*>(effect);
    light->color = colour;
    light->range = radius;
    light->localPosition = position;
    Transform()->localPosition = position;
    light->MarkChanged();
}

SoundSpec::SoundSpec() {
    kind = 3;
    volume = 10;
    radius = 15;
    minInterval = 1500;
    maxInterval = 3000;
    ambient = 0;
}

SoundSpec::~SoundSpec() = default;

void SoundSpec::Save() {
    BeginWrite(0xCC01);
    WriteStringArray(0xCC0A, &sounds);
    WriteInt(0xCC02, id);
    WriteVector(*this, 0xCC03, position);
    WriteInt(0xCC04, volume);
    WriteInt(0xCC0B, radius);
    WriteString(0xCC05, name);
    WriteString(0xCC08, comment);
    WriteInt(0xCC06, minInterval);
    WriteInt(0xCC07, maxInterval);
    WriteByte(0xCC0D, ambient);
    WriteByte(0xCC0E, unreadChunk);
    EndWrite();
}

void SoundSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id) {
    case 0xCC02: id = c.ReadInt(); break;
    case 0xCC03: position = c.ReadVector3(); break;
    case 0xCC04: volume = c.ReadInt(); break;
    case 0xCC05: name = c.ReadString(); break;
    case 0xCC06: minInterval = c.ReadInt(); break;
    case 0xCC07: maxInterval = c.ReadInt(); break;
    case 0xCC08: comment = c.ReadString(); break;
    case 0xCC0A:
        sounds = c.ReadStringArray();
        break;
    case 0xCC0B: radius = c.ReadInt(); break;
    case 0xCC0D: ambient = c.ReadByte() == 1; break;
    case 0xCC0E: unreadChunk = c.ReadByte() == 1; break;
    default: break;
    }
}

void SoundSpec::Apply() {
    if (!object) {
        Notify("efcu2", "sound");
        if (!sounds.empty()) MakeEffect(this);
        return;
    }
    Transform()->localPosition = position;
    if (effect) g_serverWorld.RemoveTimedObject(id);
    if (!sounds.empty()) MakeEffect(this);
    else effect = nullptr;
}

} // namespace mapspec
