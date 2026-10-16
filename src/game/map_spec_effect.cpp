// The zone file's effect records: particle sources (vtable 0073C758, kind 4), lights (0073C788,
// kind 2) and sounds (0073C7B8, kind 3) on their base 0073C728 (map_spec.h). They keep the world
// object they made at +0x1C and its effect at +0x4C, are kept above the terrain when moved, and
// save as chunks DD01 / AA01 / CC01. See docs/notes/objects.md ("Rewritten").
#include "map_spec.h"
#include "game_strings.h"
#include "core_transform.h"
#include "terrain.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ei/vcall.h"

#include <cstring>
#include <new>
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_effects.h"

namespace mapspec {

// EffectSpec, ParticleSpec, LightSpec and SoundSpec: map_spec.h.

namespace {

void __fastcall EffectRestart(void* a0) { static_cast<CEffect*>(a0)->MarkChanged(); }
// 0046D4D0 (below), through its address: the two strings are passed by value (destroyed there).
__declspec(naked) void __fastcall EffectNotifyAt(void*, void*, std::uintptr_t, std::uintptr_t) EI_ORIGINAL(0x0046D4D0)

void* World() { return reinterpret_cast<void*>(0x007AFF60); }
void* Effects() { return *reinterpret_cast<void**>(0x007AFFA8); }
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

// The notification made before a new effect: (kind, family) strings by value, the second made
// first.
void Notify(void* self, std::uintptr_t kind, std::uintptr_t family) {
    alignas(mfc::CString) unsigned char second[sizeof(mfc::CString)];
    alignas(mfc::CString) unsigned char first[sizeof(mfc::CString)];
    new (second) mfc::CString(game::Text(family));
    new (first) mfc::CString(game::Text(kind));
    std::uintptr_t a, b;
    std::memcpy(&a, first, 4);
    std::memcpy(&b, second, 4);
    EffectNotifyAt(self, nullptr, a, b);
}

// A new world object for the record and its effect.
void MakeEffect(EffectSpec* self) {
    WorldMakeObjectOriginal(World(), nullptr, &self->base);
    self->effect = ManagerFindObject(Effects(), nullptr, self->base.id);
}

} // namespace

// ---- The base (0073C728) ------------------------------------------------------------------------

// 0046D370 / 0046D5A0 / 0046DA20: the destructor bodies (the base record's).
void EffectSpec::EffectSpecDestruct() { base.SpecDestruct(); }
EI_REPLACE_METHOD(0x0046D370, EffectSpec::EffectSpecDestruct);
void ParticleSpec::ParticleSpecDestruct() { effect.base.SpecDestruct(); }
EI_REPLACE_METHOD(0x0046D5A0, ParticleSpec::ParticleSpecDestruct);
void LightSpec::LightSpecDestruct() { effect.base.SpecDestruct(); }
EI_REPLACE_METHOD(0x0046DA20, LightSpec::LightSpecDestruct);

// 0046D350 / 0046D580 / 0046DA00 (slot 10).
EffectSpec* EffectSpec::EffectSpecDelete(unsigned flags) {
    EffectSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046D350, EffectSpec::EffectSpecDelete);
ParticleSpec* ParticleSpec::ParticleSpecDelete(unsigned flags) {
    ParticleSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046D580, ParticleSpec::ParticleSpecDelete);
LightSpec* LightSpec::LightSpecDelete(unsigned flags) {
    LightSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046DA00, LightSpec::LightSpecDelete);

// 0046D3D0 (slot 2): effects are managed under -3001 - id.
void* EffectSpec::EffectSpecWorldObject() {
    return ManagerFindObject(*reinterpret_cast<void**>(0x007AF5F8), nullptr, -3001 - base.id);
}
EI_REPLACE_METHOD(0x0046D3D0, EffectSpec::EffectSpecWorldObject);

// 0046D3F0 / 0046E490 (slot 11): shows or hides the world object (flag 0x40 hides).
void EffectSpec::EffectSpecSetVisible(std::uint8_t newVisible) {
    visible = newVisible;
    std::uint32_t& flags = static_cast<CEngineObject*>(base.object)->flags_;
    if (newVisible != 0) flags &= ~0x40u;
    else flags |= 0x40;
}
EI_REPLACE_METHOD(0x0046D3F0, EffectSpec::EffectSpecSetVisible);
EI_REPLACE_METHOD(0x0046E490, EffectSpec::EffectSpecSetVisible);   // the sound record's slot 11 (the same code)

// 0046D420 (slot 5): moves the record, not below the terrain, then slot 7.
void EffectSpec::EffectSpecMove(float dx, float dy, float dz) {
    Vector3& p = base.position;
    p.x = dx + p.x;
    p.y = dy + p.y;
    p.z = dz + p.z;
    double ground = (*reinterpret_cast<Terrain**>(0x007AF738))->HeightAt(p.x, p.y, 0);
    if (ground > p.z) p.z = static_cast<float>(ground);
    base.Record()->ApplyObject();
}
EI_REPLACE_METHOD(0x0046D420, EffectSpec::EffectSpecMove);

// 0046D4D0: takes the two strings (by value) and only destroys them.
void __fastcall EffectNotify(void*, void*, std::uintptr_t kind, std::uintptr_t family) {
    reinterpret_cast<mfc::CString*>(&kind)->~CString();
    reinterpret_cast<mfc::CString*>(&family)->~CString();
}
EI_REPLACE(0x0046D4D0, EffectNotify);

// ---- Particle sources (0073C758) ------------------------------------------------------------------

// 0046D520. Returns this.
ParticleSpec* ParticleSpec::ParticleSpecConstruct() {
    effect.base.SpecConstruct();
    effect.visible = 1;
    effect.base.object = nullptr;
    effect.effect = nullptr;
    type = 0;
    ei::SetVtable(this, kVtableAddress);
    effect.base.kind = 4;
    scale = 1.0f;
    return this;
}
EI_REPLACE_METHOD(0x0046D520, ParticleSpec::ParticleSpecConstruct);

// 0046D550 (slot 4): "Particle source".
mfc::CString* ParticleSpec::ParticleSpecTypeName(mfc::CString* out) {
    new (out) mfc::CString(game::Text(0x00787DF0));
    return out;
}
EI_REPLACE_METHOD(0x0046D550, ParticleSpec::ParticleSpecTypeName);

// 0046D680 (slot 9): chunk DD01.
void ParticleSpec::ParticleSpecSave() {
    Chunk& c = effect.base.chunk;
    c.BeginWrite(0xDD01);
    c.WriteInt(0xDD02, effect.base.id);
    WriteVector(c, 0xDD03, effect.base.position);
    c.WriteString(0xDD05, effect.base.name);
    c.WriteString(0xDD04, effect.base.comment);
    c.WriteInt(0xDD06, type);
    c.WriteFloat(0xDD07, Bits(scale));
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046D680, ParticleSpec::ParticleSpecSave);

// 0046D730 (slot 0).
void ParticleSpec::ParticleSpecOnChunk() {
    Chunk& c = effect.base.chunk;
    switch (c.id_) {
    case 0xDD02: effect.base.id = c.ReadInt(); break;
    case 0xDD03: ReadVector(c, effect.base.position); break;
    case 0xDD04: ReadInto(c, effect.base.comment); break;
    case 0xDD05: ReadInto(c, effect.base.name); break;
    case 0xDD06: type = c.ReadInt(); break;
    case 0xDD07: scale = c.ReadFloat(); break;
    default: break;
    }
}
EI_REPLACE_METHOD(0x0046D730, ParticleSpec::ParticleSpecOnChunk);

// 0046D870 (slot 7): makes the source (when it has a type) or updates its effect.
void ParticleSpec::ParticleSpecApply() {
    Spec& b = effect.base;
    if (!b.object) {
        Notify(this, 0x00787E00, 0x00787E08);   // "efcu3", "particles"
        if (type) MakeEffect(&effect);
        return;
    }
    auto* particle = static_cast<CEffectParticle*>(effect.effect);
    std::memcpy(&static_cast<CTransformObject*>(b.object)->localPosition_, &b.position, 12);
    std::memcpy(&particle->size_, &scale, 4);
    std::memcpy(&particle->type_, &type, 4);
    std::memcpy(&particle->localPosition_, &b.position, 12);
    EffectRestart(particle);
}
EI_REPLACE_METHOD(0x0046D870, ParticleSpec::ParticleSpecApply);

// ---- Lights (0073C788) -----------------------------------------------------------------------------

// 0046D960. Returns this.
LightSpec* LightSpec::LightSpecConstruct() {
    effect.base.SpecConstruct();
    effect.base.object = nullptr;
    colour.x = 1.0f;
    effect.effect = nullptr;
    colour.y = 1.0f;
    effect.visible = 1;
    shadows = 1;
    ei::SetVtable(this, kVtableAddress);
    effect.base.kind = 2;
    radius = 10.0f;
    colour.z = 1.0f;
    return this;
}
EI_REPLACE_METHOD(0x0046D960, LightSpec::LightSpecConstruct);

// 0046D9D0 (slot 4): "Light".
mfc::CString* LightSpec::LightSpecTypeName(mfc::CString* out) {
    new (out) mfc::CString(game::Text(0x00787E14));
    return out;
}
EI_REPLACE_METHOD(0x0046D9D0, LightSpec::LightSpecTypeName);

// 0046DB20 (slot 9): chunk AA01.
void LightSpec::LightSpecSave() {
    Chunk& c = effect.base.chunk;
    c.BeginWrite(0xAA01);
    c.WriteInt(0xAA05, effect.base.id);
    c.WriteByte(0xAA06, shadows);
    WriteVector(c, 0xAA04, effect.base.position);
    WriteVector(c, 0xAA07, colour);
    c.WriteFloat(0xAA02, Bits(radius));
    c.WriteString(0xAA03, effect.base.name);
    c.WriteString(0xAA08, effect.base.comment);
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046DB20, LightSpec::LightSpecSave);

// 0046DBF0 (slot 0).
void LightSpec::LightSpecOnChunk() {
    Chunk& c = effect.base.chunk;
    switch (c.id_) {
    case 0xAA02: radius = c.ReadFloat(); break;
    case 0xAA03: ReadInto(c, effect.base.name); break;
    case 0xAA04: ReadVector(c, effect.base.position); break;
    case 0xAA05: effect.base.id = c.ReadInt(); break;
    case 0xAA06: shadows = c.ReadByte() != 0; break;
    case 0xAA07: ReadVector(c, colour); break;
    case 0xAA08: ReadInto(c, effect.base.comment); break;
    default: break;
    }
}
EI_REPLACE_METHOD(0x0046DBF0, LightSpec::LightSpecOnChunk);

// 0046DD70 (slot 7): makes the light or updates its effect (colour, radius, position).
void LightSpec::LightSpecApply() {
    Spec& b = effect.base;
    if (!b.object) {
        Notify(this, 0x00787E1C, 0x00787E24);   // "efcu0", "light"
        MakeEffect(&effect);
        return;
    }
    auto* light = static_cast<CEffectPointLight*>(effect.effect);
    std::memcpy(&light->color_, &colour, 12);
    std::memcpy(&light->range_, &radius, 4);
    std::memcpy(&light->localPosition_, &b.position, 12);
    std::memcpy(&static_cast<CTransformObject*>(b.object)->localPosition_, &b.position, 12);
    EffectRestart(light);
}
EI_REPLACE_METHOD(0x0046DD70, LightSpec::LightSpecApply);

// ---- Sounds (0073C7B8) -------------------------------------------------------------------------------

// 0046DE70. Returns this.
SoundSpec* SoundSpec::SoundSpecConstruct() {
    effect.base.SpecConstruct();
    ei::SetVtable(this, EffectSpec::kVtableAddress);
    effect.base.object = nullptr;
    effect.effect = nullptr;
    effect.visible = 1;
    StringArrayConstruct(&sounds);
    ei::SetVtable(this, kVtableAddress);
    effect.base.kind = 3;
    StringArraySetSize(&sounds, nullptr, 0, -1);
    volume = 10;
    radius = 15;
    unused_70 = 1;
    minInterval = 1500;
    maxInterval = 3000;
    unused_7C = 0;
    unused_80 = 0;
    ambient = 0;
    return this;
}
EI_REPLACE_METHOD(0x0046DE70, SoundSpec::SoundSpecConstruct);

// 0046DF70: the destructor body.
void SoundSpec::SoundSpecDestruct() {
    ei::SetVtable(this, kVtableAddress);
    StringArrayDestruct(&sounds);
    effect.base.SpecDestruct();
}
EI_REPLACE_METHOD(0x0046DF70, SoundSpec::SoundSpecDestruct);

// 0046DF50 (slot 10).
SoundSpec* SoundSpec::SoundSpecDelete(unsigned flags) {
    SoundSpecDestruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0046DF50, SoundSpec::SoundSpecDelete);

// 0046DF20 (slot 4): "Sound".
mfc::CString* SoundSpec::SoundSpecTypeName(mfc::CString* out) {
    new (out) mfc::CString(game::Text(0x00787E2C));
    return out;
}
EI_REPLACE_METHOD(0x0046DF20, SoundSpec::SoundSpecTypeName);

// 0046E090 (slot 9): chunk CC01.
void SoundSpec::SoundSpecSave() {
    Chunk& c = effect.base.chunk;
    c.BeginWrite(0xCC01);
    c.WriteStringArray(0xCC0A, &sounds);
    c.WriteInt(0xCC02, effect.base.id);
    WriteVector(c, 0xCC03, effect.base.position);
    c.WriteInt(0xCC04, volume);
    c.WriteInt(0xCC0B, radius);
    c.WriteString(0xCC05, effect.base.name);
    c.WriteString(0xCC08, effect.base.comment);
    c.WriteInt(0xCC06, minInterval);
    c.WriteInt(0xCC07, maxInterval);
    c.WriteByte(0xCC0D, ambient);
    c.WriteByte(0xCC0E, field0E);
    c.EndWrite();
}
EI_REPLACE_METHOD(0x0046E090, SoundSpec::SoundSpecSave);

// 0046E190 (slot 0).
void SoundSpec::SoundSpecOnChunk() {
    Chunk& c = effect.base.chunk;
    switch (c.id_) {
    case 0xCC02: effect.base.id = c.ReadInt(); break;
    case 0xCC03: ReadVector(c, effect.base.position); break;
    case 0xCC04: volume = c.ReadInt(); break;
    case 0xCC05: ReadInto(c, effect.base.name); break;
    case 0xCC06: minInterval = c.ReadInt(); break;
    case 0xCC07: maxInterval = c.ReadInt(); break;
    case 0xCC08: ReadInto(c, effect.base.comment); break;
    case 0xCC0A:
        StringArraySetSize(&sounds, nullptr, 0, -1);
        c.ReadStringArray(&sounds);
        break;
    case 0xCC0B: radius = c.ReadInt(); break;
    case 0xCC0D: ambient = c.ReadByte() == 1; break;
    case 0xCC0E: field0E = c.ReadByte() == 1; break;
    default: break;
    }
}
EI_REPLACE_METHOD(0x0046E190, SoundSpec::SoundSpecOnChunk);

// 0046E380 (slot 7): makes the sound (when it has sounds); otherwise moves the world object and
// makes its effect again.
void SoundSpec::SoundSpecApply() {
    Spec& b = effect.base;
    if (!b.object) {
        Notify(this, 0x00787E34, 0x00787E3C);   // "efcu2", "sound"
        if (sounds.size > 0) MakeEffect(&effect);
        return;
    }
    std::memcpy(&static_cast<CTransformObject*>(b.object)->localPosition_, &b.position, 12);
    if (effect.effect) WorldRemoveTimed(World(), nullptr, b.id);
    if (sounds.size > 0) MakeEffect(&effect);
    else effect.effect = nullptr;
}
EI_REPLACE_METHOD(0x0046E380, SoundSpec::SoundSpecApply);

} // namespace mapspec
