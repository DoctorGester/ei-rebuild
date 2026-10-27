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

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <memory>
#include <new>
#include "game_data.h"
#include "game_singletons.h"

namespace mapspec {

void __stdcall EffectNotify(mfc::CString kind, mfc::CString family);
namespace {

CBaseManager* Effects() { return TheServerWorld()->manager48_; }
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

// The notification made before a new effect: (kind, family) strings by value, built in their argument slots
// right to left (the second first, like the original) and destroyed by the callee.
void Notify(const char* kind, const char* family) { EffectNotify(mfc::CString(kind), mfc::CString(family)); }

// A new world object for the record and its effect.
void MakeEffect(EffectSpec* self) {
    TheServerWorld()->CreateObject(self);
    self->effect = Effects()->FindObject(self->id);
}

} // namespace

// ---- The base ------------------------------------------------------------------------

// The records' destructors are the base record's.
EffectSpec::~EffectSpec() = default;
EI_REWRITTEN_SPECIAL(0x0046D370, EffectSpec::~EffectSpec);
EI_REWRITTEN_SPECIAL(0x0046D350, EffectSpec::~EffectSpec);
ParticleSpec::~ParticleSpec() = default;
EI_REWRITTEN_SPECIAL(0x0046D5A0, ParticleSpec::~ParticleSpec);
EI_REWRITTEN_SPECIAL(0x0046D580, ParticleSpec::~ParticleSpec);
LightSpec::~LightSpec() = default;
EI_REWRITTEN_SPECIAL(0x0046DA20, LightSpec::~LightSpec);
EI_REWRITTEN_SPECIAL(0x0046DA00, LightSpec::~LightSpec);

void* EffectSpec::WorldObject() {
    return TheObjectManager()->FindObject(-3001 - id);
}
EI_REWRITTEN(0x0046D3D0, EffectSpec::WorldObject);

CTransformObject* EffectSpec::Transform() const { return static_cast<CTransformObject*>(object); }

void EffectSpec::SetVisible(std::uint8_t newVisible) {
    visible = newVisible;
    std::uint32_t& flags = object->flags_;
    if (newVisible != 0) flags &= ~0x40u;
    else flags |= 0x40;
}
EI_REWRITTEN(0x0046D3F0, EffectSpec::SetVisible);
EI_REWRITTEN(0x0046E490, EffectSpec::SetVisible);   // the sound record's SetVisible (the same code)

void EffectSpec::Move(float dx, float dy, float dz) {
    Vector3& p = position;
    p.x = dx + p.x;
    p.y = dy + p.y;
    p.z = dz + p.z;
    double ground = TheTerrain()->HeightAt(p.x, p.y, 0);
    if (ground > p.z) p.z = static_cast<float>(ground);
    Apply();
}
EI_REWRITTEN(0x0046D420, EffectSpec::Move);

// Takes the two strings by value and only destroys them (the callee destroys its by-value parameters).
void __stdcall EffectNotify(mfc::CString, mfc::CString) {}
EI_REWRITTEN(0x0046D4D0, EffectNotify);

// ---- Particle sources ------------------------------------------------------------------

ParticleSpec::ParticleSpec() {
    type = 0;
    kind = 4;
    scale = 1.0f;
}
EI_REWRITTEN_SPECIAL(0x0046D520, ParticleSpec::ParticleSpec);

mfc::CString* ParticleSpec::TypeName(mfc::CString* out) {
    std::construct_at(out, "Particle source");
    return out;
}
EI_REWRITTEN(0x0046D550, ParticleSpec::TypeName);

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
EI_REWRITTEN(0x0046D680, ParticleSpec::Save);

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
EI_REWRITTEN(0x0046D730, ParticleSpec::OnChunk);

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
EI_REWRITTEN(0x0046D870, ParticleSpec::Apply);


LightSpec::LightSpec() {
    colour.x = 1.0f;
    colour.y = 1.0f;
    shadows = 1;
    kind = 2;
    radius = 10.0f;
    colour.z = 1.0f;
}
EI_REWRITTEN_SPECIAL(0x0046D960, LightSpec::LightSpec);

mfc::CString* LightSpec::TypeName(mfc::CString* out) {
    std::construct_at(out, "Light");
    return out;
}
EI_REWRITTEN(0x0046D9D0, LightSpec::TypeName);

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
EI_REWRITTEN(0x0046DB20, LightSpec::Save);

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
EI_REWRITTEN(0x0046DBF0, LightSpec::OnChunk);

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
EI_REWRITTEN(0x0046DD70, LightSpec::Apply);


SoundSpec::SoundSpec() {
    sounds.Construct();
    kind = 3;
    sounds.SetSize(0, -1);
    volume = 10;
    radius = 15;
    unused_70 = 1;
    minInterval = 1500;
    maxInterval = 3000;
    unused_7C = 0;
    unused_80 = 0;
    ambient = 0;
}
EI_REWRITTEN_SPECIAL(0x0046DE70, SoundSpec::SoundSpec);

SoundSpec::~SoundSpec() { sounds.Destruct(); }
EI_REWRITTEN_SPECIAL(0x0046DF70, SoundSpec::~SoundSpec);
EI_REWRITTEN_SPECIAL(0x0046DF50, SoundSpec::~SoundSpec);

mfc::CString* SoundSpec::TypeName(mfc::CString* out) {
    std::construct_at(out, "Sound");
    return out;
}
EI_REWRITTEN(0x0046DF20, SoundSpec::TypeName);

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
EI_REWRITTEN(0x0046E090, SoundSpec::Save);

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
        sounds.SetSize(0, -1);
        c.ReadStringArray(&sounds);
        break;
    case 0xCC0B: radius = c.ReadInt(); break;
    case 0xCC0D: ambient = c.ReadByte() == 1; break;
    case 0xCC0E: field0E = c.ReadByte() == 1; break;
    default: break;
    }
}
EI_REWRITTEN(0x0046E190, SoundSpec::OnChunk);

void SoundSpec::Apply() {
    if (!object) {
        Notify("efcu2", "sound");
        if (sounds.size > 0) MakeEffect(this);
        return;
    }
    std::memcpy(&Transform()->localPosition_, &position, 12);
    if (effect) TheServerWorld()->RemoveTimedObject(id);
    if (sounds.size > 0) MakeEffect(this);
    else effect = nullptr;
}
EI_REWRITTEN(0x0046E380, SoundSpec::Apply);

} // namespace mapspec
