// The zone file's effect records: particle sources (kind 4), lights (kind 2) and sounds (kind 3) on their base
// (map_spec.h). They keep the world object they made and its effect, are kept above the terrain when moved,
// and save as chunks DD01 / AA01 / CC01. See docs/notes/objects.md ("Rewritten").
#include "base_manager.h"
#include "map_spec.h"
#include "game_strings.h"
#include "core_transform.h"
#include "terrain.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include "game_data.h"
#include "game_singletons.h"

namespace mapspec {

void __stdcall EffectNotify(const std::string& kind, const std::string& family);
namespace {

CBaseManager* Effects() { return TheServerWorld()->manager48_; }
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

// The notification made before a new effect: (kind, family) strings by value, built in their argument slots
// right to left (the second first, like the original) and destroyed by the callee.
void Notify(const char* kind, const char* family) { EffectNotify(std::string(kind), std::string(family)); }

// A new world object for the record and its effect.
void MakeEffect(EffectSpec* self) {
    TheServerWorld()->CreateObject(self);
    self->effect = Effects()->FindObject(self->id);
}

} // namespace

// ---- The base ------------------------------------------------------------------------

// The records' destructors are the base record's.
EffectSpec::~EffectSpec() = default;
ParticleSpec::~ParticleSpec() = default;
LightSpec::~LightSpec() = default;

void* EffectSpec::WorldObject() {
    return TheObjectManager()->FindObject(-3001 - id);
}

CTransformObject* EffectSpec::Transform() const { return static_cast<CTransformObject*>(object); }

void EffectSpec::SetVisible(std::uint8_t newVisible) {
    visible = newVisible;
    std::uint32_t& flags = object->flags_;
    if (newVisible != 0) flags &= ~0x40u;
    else flags |= 0x40;
}

void EffectSpec::Move(float dx, float dy, float dz) {
    Vector3& p = position;
    p.x = dx + p.x;
    p.y = dy + p.y;
    p.z = dz + p.z;
    double ground = TheTerrain()->HeightAt(p.x, p.y, 0);
    if (ground > p.z) p.z = static_cast<float>(ground);
    Apply();
}

// Takes the two strings by value and only destroys them (the callee destroys its by-value parameters).
void __stdcall EffectNotify(const std::string&, const std::string&) {}

// ---- Particle sources ------------------------------------------------------------------

ParticleSpec::ParticleSpec() {
    type = 0;
    kind = 4;
    scale = 1.0f;
}

std::string* ParticleSpec::TypeName(std::string* out) {
    *out = "Particle source";
    return out;
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
    switch (c.id_) {
    case 0xDD02: id = c.ReadInt(); break;
    case 0xDD03: ReadVector(c, position); break;
    case 0xDD04: ReadInto(c, comment); break;
    case 0xDD05: ReadInto(c, name); break;
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
    auto* particle = static_cast<CEffectParticle*>(effect);
    std::memcpy(&Transform()->localPosition_, &position, 12);
    std::memcpy(&particle->size_, &scale, 4);
    std::memcpy(&particle->type_, &type, 4);
    std::memcpy(&particle->localPosition_, &position, 12);
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

std::string* LightSpec::TypeName(std::string* out) {
    *out = "Light";
    return out;
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
    switch (c.id_) {
    case 0xAA02: radius = c.ReadFloat(); break;
    case 0xAA03: ReadInto(c, name); break;
    case 0xAA04: ReadVector(c, position); break;
    case 0xAA05: id = c.ReadInt(); break;
    case 0xAA06: shadows = c.ReadByte() != 0; break;
    case 0xAA07: ReadVector(c, colour); break;
    case 0xAA08: ReadInto(c, comment); break;
    default: break;
    }
}

void LightSpec::Apply() {
    if (!object) {
        Notify("efcu0", "light");
        MakeEffect(this);
        return;
    }
    auto* light = static_cast<CEffectPointLight*>(effect);
    std::memcpy(&light->color_, &colour, 12);
    std::memcpy(&light->range_, &radius, 4);
    std::memcpy(&light->localPosition_, &position, 12);
    std::memcpy(&Transform()->localPosition_, &position, 12);
    light->MarkChanged();
}

SoundSpec::SoundSpec() {
    kind = 3;
    volume = 10;
    radius = 15;
    unused_70 = 1;
    minInterval = 1500;
    maxInterval = 3000;
    unused_7C = 0;
    unused_80 = 0;
    ambient = 0;
}

SoundSpec::~SoundSpec() = default;

std::string* SoundSpec::TypeName(std::string* out) {
    *out = "Sound";
    return out;
}

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
    WriteByte(0xCC0E, field0E);
    EndWrite();
}

void SoundSpec::OnChunk() {
    Chunk& c = *this;
    switch (c.id_) {
    case 0xCC02: id = c.ReadInt(); break;
    case 0xCC03: ReadVector(c, position); break;
    case 0xCC04: volume = c.ReadInt(); break;
    case 0xCC05: ReadInto(c, name); break;
    case 0xCC06: minInterval = c.ReadInt(); break;
    case 0xCC07: maxInterval = c.ReadInt(); break;
    case 0xCC08: ReadInto(c, comment); break;
    case 0xCC0A:
        c.ReadStringArray(&sounds);
        break;
    case 0xCC0B: radius = c.ReadInt(); break;
    case 0xCC0D: ambient = c.ReadByte() == 1; break;
    case 0xCC0E: field0E = c.ReadByte() == 1; break;
    default: break;
    }
}

void SoundSpec::Apply() {
    if (!object) {
        Notify("efcu2", "sound");
        if (!sounds.empty()) MakeEffect(this);
        return;
    }
    std::memcpy(&Transform()->localPosition_, &position, 12);
    if (effect) TheServerWorld()->RemoveTimedObject(id);
    if (!sounds.empty()) MakeEffect(this);
    else effect = nullptr;
}

} // namespace mapspec
