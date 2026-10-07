// The zone file's effect records: particle sources (vtable 0073C758, kind 4), lights (0073C788,
// kind 2) and sounds (0073C7B8, kind 3) on their base 0073C728 (map_spec.h). They keep the world
// object they made at +0x1C and its effect at +0x4C, are kept above the terrain when moved, and
// save as chunks DD01 / AA01 / CC01. See docs/notes/objects.md ("Sixth round").
#include "map_spec.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <new>
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_effects.h"

namespace mapspec {

// EffectSpec, ParticleSpec, LightSpec and SoundSpec: map_spec.h.

// Rewritten in map_spec.cpp.
Spec* __fastcall SpecConstruct(Spec* self);
void __fastcall SpecDestruct(Spec* self);

namespace {

constexpr std::uintptr_t kEffectVtable = 0x0073C728;
constexpr std::uintptr_t kParticleVtable = 0x0073C758;
constexpr std::uintptr_t kLightVtable = 0x0073C788;
constexpr std::uintptr_t kSoundVtable = 0x0073C7B8;
constexpr int kSlotApply = 7;

__declspec(naked) void __fastcall WorldMakeObject(void*, void*, Spec*) EI_ORIGINAL(0x005927C0)
__declspec(naked) double __fastcall TerrainHeight(void*, void*, float /*x*/, float /*y*/, int /*zero*/) EI_ORIGINAL(0x006C8680)
void __fastcall EffectRestart(void* a0) { ObjectMarkNewOriginal(a0); }
// 0046D4D0 (below), through its address: the two strings are passed by value (destroyed there).
__declspec(naked) void __fastcall EffectNotifyAt(void*, void*, std::uintptr_t, std::uintptr_t) EI_ORIGINAL(0x0046D4D0)

void* World() { return reinterpret_cast<void*>(0x007AFF60); }
void* Effects() { return *reinterpret_cast<void**>(0x007AFFA8); }
void SetVtable(void* p, std::uintptr_t vtable) { std::memcpy(p, &vtable, 4); }
const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }
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
    new (second) mfc::CString(Text(family));
    new (first) mfc::CString(Text(kind));
    std::uintptr_t a, b;
    std::memcpy(&a, first, 4);
    std::memcpy(&b, second, 4);
    EffectNotifyAt(self, nullptr, a, b);
}

// A new world object for the record and its effect.
void MakeEffect(EffectSpec* self) {
    WorldMakeObject(World(), nullptr, &self->base);
    self->effect = ManagerFindObject(Effects(), nullptr, self->base.id);
}

} // namespace

// ---- The base (0073C728) ------------------------------------------------------------------------

// 0046D370 / 0046D5A0 / 0046DA20: the destructor bodies (the base record's).
void __fastcall EffectSpecDestruct(EffectSpec* self) { SpecDestruct(&self->base); }
EI_REPLACE(0x0046D370, EffectSpecDestruct);
void __fastcall ParticleSpecDestruct(ParticleSpec* self) { SpecDestruct(&self->effect.base); }
EI_REPLACE(0x0046D5A0, ParticleSpecDestruct);
void __fastcall LightSpecDestruct(LightSpec* self) { SpecDestruct(&self->effect.base); }
EI_REPLACE(0x0046DA20, LightSpecDestruct);

// 0046D350 / 0046D580 / 0046DA00 (slot 10).
EffectSpec* __fastcall EffectSpecDelete(EffectSpec* self, void*, unsigned flags) {
    EffectSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046D350, EffectSpecDelete);
ParticleSpec* __fastcall ParticleSpecDelete(ParticleSpec* self, void*, unsigned flags) {
    ParticleSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046D580, ParticleSpecDelete);
LightSpec* __fastcall LightSpecDelete(LightSpec* self, void*, unsigned flags) {
    LightSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046DA00, LightSpecDelete);

// 0046D3D0 (slot 2): effects are managed under -3001 - id.
void* __fastcall EffectSpecWorldObject(EffectSpec* self) {
    return ManagerFindObject(*reinterpret_cast<void**>(0x007AF5F8), nullptr, -3001 - self->base.id);
}
EI_REPLACE(0x0046D3D0, EffectSpecWorldObject);

// 0046D3F0 / 0046E490 (slot 11): shows or hides the world object (flag 0x40 hides).
void __fastcall EffectSpecSetVisible(EffectSpec* self, void*, std::uint8_t visible) {
    self->visible = visible;
    std::uint32_t& flags = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(self->base.object) + 8);
    if (visible != 0) flags &= ~0x40u;
    else flags |= 0x40;
}
EI_REPLACE(0x0046D3F0, EffectSpecSetVisible);
void __fastcall SoundSpecSetVisible(EffectSpec* self, void*, std::uint8_t visible) { EffectSpecSetVisible(self, nullptr, visible); }
EI_REPLACE(0x0046E490, SoundSpecSetVisible);

// 0046D420 (slot 5): moves the record, not below the terrain, then slot 7.
void __fastcall EffectSpecMove(EffectSpec* self, void*, float dx, float dy, float dz) {
    Vector3& p = self->base.position;
    p.x = dx + p.x;
    p.y = dy + p.y;
    p.z = dz + p.z;
    double ground = TerrainHeight(*reinterpret_cast<void**>(0x007AF738), nullptr, p.x, p.y, 0);
    if (ground > p.z) p.z = static_cast<float>(ground);
    ei::VCall<void>(self, kSlotApply);
}
EI_REPLACE(0x0046D420, EffectSpecMove);

// 0046D4D0: takes the two strings (by value) and only destroys them.
void __fastcall EffectNotify(void*, void*, std::uintptr_t kind, std::uintptr_t family) {
    reinterpret_cast<mfc::CString*>(&kind)->~CString();
    reinterpret_cast<mfc::CString*>(&family)->~CString();
}
EI_REPLACE(0x0046D4D0, EffectNotify);

// ---- Particle sources (0073C758) ------------------------------------------------------------------

// 0046D520. Returns this.
ParticleSpec* __fastcall ParticleSpecConstruct(ParticleSpec* self) {
    SpecConstruct(&self->effect.base);
    self->effect.visible = 1;
    self->effect.base.object = nullptr;
    self->effect.effect = nullptr;
    self->type = 0;
    SetVtable(self, kParticleVtable);
    self->effect.base.kind = 4;
    self->scale = 1.0f;
    return self;
}
EI_REPLACE(0x0046D520, ParticleSpecConstruct);

// 0046D550 (slot 4): "Particle source".
mfc::CString* __fastcall ParticleSpecTypeName(ParticleSpec*, void*, mfc::CString* out) {
    new (out) mfc::CString(Text(0x00787DF0));
    return out;
}
EI_REPLACE(0x0046D550, ParticleSpecTypeName);

// 0046D680 (slot 9): chunk DD01.
void __fastcall ParticleSpecSave(ParticleSpec* self) {
    Chunk& c = self->effect.base.chunk;
    c.BeginWrite(0xDD01);
    c.WriteInt(0xDD02, self->effect.base.id);
    WriteVector(c, 0xDD03, self->effect.base.position);
    c.WriteString(0xDD05, self->effect.base.name);
    c.WriteString(0xDD04, self->effect.base.comment);
    c.WriteInt(0xDD06, self->type);
    c.WriteFloat(0xDD07, Bits(self->scale));
    c.EndWrite();
}
EI_REPLACE(0x0046D680, ParticleSpecSave);

// 0046D730 (slot 0).
void __fastcall ParticleSpecOnChunk(ParticleSpec* self) {
    Chunk& c = self->effect.base.chunk;
    switch (c.id_) {
    case 0xDD02: self->effect.base.id = c.ReadInt(); break;
    case 0xDD03: ReadVector(c, self->effect.base.position); break;
    case 0xDD04: ReadInto(c, self->effect.base.comment); break;
    case 0xDD05: ReadInto(c, self->effect.base.name); break;
    case 0xDD06: self->type = c.ReadInt(); break;
    case 0xDD07: self->scale = c.ReadFloat(); break;
    default: break;
    }
}
EI_REPLACE(0x0046D730, ParticleSpecOnChunk);

// 0046D870 (slot 7): makes the source (when it has a type) or updates its effect.
void __fastcall ParticleSpecApply(ParticleSpec* self) {
    Spec& b = self->effect.base;
    if (!b.object) {
        Notify(self, 0x00787E00, 0x00787E08);   // "efcu3", "particles"
        if (self->type) MakeEffect(&self->effect);
        return;
    }
    auto* effect = static_cast<std::uint8_t*>(self->effect.effect);
    std::memcpy(static_cast<std::uint8_t*>(b.object) + 0x1C, &b.position, 12);
    std::memcpy(effect + 0x15C, &self->scale, 4);
    std::memcpy(effect + 0x158, &self->type, 4);
    std::memcpy(effect + 0x1C, &b.position, 12);
    EffectRestart(effect);
}
EI_REPLACE(0x0046D870, ParticleSpecApply);

// ---- Lights (0073C788) -----------------------------------------------------------------------------

// 0046D960. Returns this.
LightSpec* __fastcall LightSpecConstruct(LightSpec* self) {
    SpecConstruct(&self->effect.base);
    self->effect.base.object = nullptr;
    self->colour.x = 1.0f;
    self->effect.effect = nullptr;
    self->colour.y = 1.0f;
    self->effect.visible = 1;
    self->shadows = 1;
    SetVtable(self, kLightVtable);
    self->effect.base.kind = 2;
    self->radius = 10.0f;
    self->colour.z = 1.0f;
    return self;
}
EI_REPLACE(0x0046D960, LightSpecConstruct);

// 0046D9D0 (slot 4): "Light".
mfc::CString* __fastcall LightSpecTypeName(LightSpec*, void*, mfc::CString* out) {
    new (out) mfc::CString(Text(0x00787E14));
    return out;
}
EI_REPLACE(0x0046D9D0, LightSpecTypeName);

// 0046DB20 (slot 9): chunk AA01.
void __fastcall LightSpecSave(LightSpec* self) {
    Chunk& c = self->effect.base.chunk;
    c.BeginWrite(0xAA01);
    c.WriteInt(0xAA05, self->effect.base.id);
    c.WriteByte(0xAA06, self->shadows);
    WriteVector(c, 0xAA04, self->effect.base.position);
    WriteVector(c, 0xAA07, self->colour);
    c.WriteFloat(0xAA02, Bits(self->radius));
    c.WriteString(0xAA03, self->effect.base.name);
    c.WriteString(0xAA08, self->effect.base.comment);
    c.EndWrite();
}
EI_REPLACE(0x0046DB20, LightSpecSave);

// 0046DBF0 (slot 0).
void __fastcall LightSpecOnChunk(LightSpec* self) {
    Chunk& c = self->effect.base.chunk;
    switch (c.id_) {
    case 0xAA02: self->radius = c.ReadFloat(); break;
    case 0xAA03: ReadInto(c, self->effect.base.name); break;
    case 0xAA04: ReadVector(c, self->effect.base.position); break;
    case 0xAA05: self->effect.base.id = c.ReadInt(); break;
    case 0xAA06: self->shadows = c.ReadByte() != 0; break;
    case 0xAA07: ReadVector(c, self->colour); break;
    case 0xAA08: ReadInto(c, self->effect.base.comment); break;
    default: break;
    }
}
EI_REPLACE(0x0046DBF0, LightSpecOnChunk);

// 0046DD70 (slot 7): makes the light or updates its effect (colour, radius, position).
void __fastcall LightSpecApply(LightSpec* self) {
    Spec& b = self->effect.base;
    if (!b.object) {
        Notify(self, 0x00787E1C, 0x00787E24);   // "efcu0", "light"
        MakeEffect(&self->effect);
        return;
    }
    auto* effect = static_cast<std::uint8_t*>(self->effect.effect);
    std::memcpy(effect + 0x14C, &self->colour, 12);
    std::memcpy(effect + 0x158, &self->radius, 4);
    std::memcpy(effect + 0x1C, &b.position, 12);
    std::memcpy(static_cast<std::uint8_t*>(b.object) + 0x1C, &b.position, 12);
    EffectRestart(effect);
}
EI_REPLACE(0x0046DD70, LightSpecApply);

// ---- Sounds (0073C7B8) -------------------------------------------------------------------------------

// 0046DE70. Returns this.
SoundSpec* __fastcall SoundSpecConstruct(SoundSpec* self) {
    SpecConstruct(&self->effect.base);
    SetVtable(self, kEffectVtable);
    self->effect.base.object = nullptr;
    self->effect.effect = nullptr;
    self->effect.visible = 1;
    StringArrayConstruct(&self->sounds);
    SetVtable(self, kSoundVtable);
    self->effect.base.kind = 3;
    StringArraySetSize(&self->sounds, nullptr, 0, -1);
    self->unknown_68 = 10;
    self->unknown_6C = 15;
    self->unknown_70 = 1;
    self->unknown_74 = 1500;
    self->unknown_78 = 3000;
    self->unknown_7C = 0;
    self->unknown_80 = 0;
    self->unknown_84 = 0;
    return self;
}
EI_REPLACE(0x0046DE70, SoundSpecConstruct);

// 0046DF70: the destructor body.
void __fastcall SoundSpecDestruct(SoundSpec* self) {
    SetVtable(self, kSoundVtable);
    StringArrayDestruct(&self->sounds);
    SpecDestruct(&self->effect.base);
}
EI_REPLACE(0x0046DF70, SoundSpecDestruct);

// 0046DF50 (slot 10).
SoundSpec* __fastcall SoundSpecDelete(SoundSpec* self, void*, unsigned flags) {
    SoundSpecDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0046DF50, SoundSpecDelete);

// 0046DF20 (slot 4): "Sound".
mfc::CString* __fastcall SoundSpecTypeName(SoundSpec*, void*, mfc::CString* out) {
    new (out) mfc::CString(Text(0x00787E2C));
    return out;
}
EI_REPLACE(0x0046DF20, SoundSpecTypeName);

// 0046E090 (slot 9): chunk CC01.
void __fastcall SoundSpecSave(SoundSpec* self) {
    Chunk& c = self->effect.base.chunk;
    c.BeginWrite(0xCC01);
    c.WriteStringArray(0xCC0A, &self->sounds);
    c.WriteInt(0xCC02, self->effect.base.id);
    WriteVector(c, 0xCC03, self->effect.base.position);
    c.WriteInt(0xCC04, self->unknown_68);
    c.WriteInt(0xCC0B, self->unknown_6C);
    c.WriteString(0xCC05, self->effect.base.name);
    c.WriteString(0xCC08, self->effect.base.comment);
    c.WriteInt(0xCC06, self->unknown_74);
    c.WriteInt(0xCC07, self->unknown_78);
    c.WriteByte(0xCC0D, self->unknown_84);
    c.WriteByte(0xCC0E, self->unknown_85);
    c.EndWrite();
}
EI_REPLACE(0x0046E090, SoundSpecSave);

// 0046E190 (slot 0).
void __fastcall SoundSpecOnChunk(SoundSpec* self) {
    Chunk& c = self->effect.base.chunk;
    switch (c.id_) {
    case 0xCC02: self->effect.base.id = c.ReadInt(); break;
    case 0xCC03: ReadVector(c, self->effect.base.position); break;
    case 0xCC04: self->unknown_68 = c.ReadInt(); break;
    case 0xCC05: ReadInto(c, self->effect.base.name); break;
    case 0xCC06: self->unknown_74 = c.ReadInt(); break;
    case 0xCC07: self->unknown_78 = c.ReadInt(); break;
    case 0xCC08: ReadInto(c, self->effect.base.comment); break;
    case 0xCC0A:
        StringArraySetSize(&self->sounds, nullptr, 0, -1);
        c.ReadStringArray(&self->sounds);
        break;
    case 0xCC0B: self->unknown_6C = c.ReadInt(); break;
    case 0xCC0D: self->unknown_84 = c.ReadByte() == 1; break;
    case 0xCC0E: self->unknown_85 = c.ReadByte() == 1; break;
    default: break;
    }
}
EI_REPLACE(0x0046E190, SoundSpecOnChunk);

// 0046E380 (slot 7): makes the sound (when it has sounds); otherwise moves the world object and
// makes its effect again.
void __fastcall SoundSpecApply(SoundSpec* self) {
    Spec& b = self->effect.base;
    if (!b.object) {
        Notify(self, 0x00787E34, 0x00787E3C);   // "efcu2", "sound"
        if (self->sounds.size > 0) MakeEffect(&self->effect);
        return;
    }
    std::memcpy(static_cast<std::uint8_t*>(b.object) + 0x1C, &b.position, 12);
    if (self->effect.effect) WorldRemoveTimed(World(), nullptr, b.id);
    if (self->sounds.size > 0) MakeEffect(&self->effect);
    else self->effect.effect = nullptr;
}
EI_REPLACE(0x0046E380, SoundSpecApply);

} // namespace mapspec
