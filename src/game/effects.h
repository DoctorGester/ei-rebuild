#pragma once

// Weather, visibility fog and particles: the per-frame simulation parts.
//
// - CParticleSource (vtable 0073CFD0, class name from 004982D0): one emitter. Its tick (slot 6,
//   00496EB0, particle_tick.cpp) runs the per-type callbacks the constructor (slot 2, 00485540) stores
//   at +0x1E0 (emit one particle), +0x1E4 (update one particle; 0 kills it) and +0x1E8 (update one
//   element of the source's own array at +0x154). The callbacks are __thiscall on the source.
// - CRainSnow (the world's rain/snow layer; "RainSnow"): fades its density and the rain sound.
// - CVisibilityFog (vtable 0073D6B4, "CVisibilityFog", class id 0x5F): a hash_map of fog cells,
//   one per visible map square; each cell fades in while visible and out once not, and is dropped
//   when nearly transparent.
//
// Names are provisional. Layouts mirror the original; unknown bytes are unknown_XXX.

#include "maths.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// One particle (0x64 bytes, allocated by the source's pool 00498870). The source's tick saves the
// "prev" fields before calling the update callback and restores them when it returns 0. The
// meaning of +0x20..+0x40 depends on the source type; the comments name the types rewritten here.
struct Particle {
    Vector3 pos;                // +0x00
    float size;                 // +0x0C
    Vector3 prevPos;            // +0x10
    float prevSize;             // +0x1C
    Vector3 velocity;           // +0x20 smoke: initial offset; spheres: orbit offset
    float unknown_2C;           // +0x2C
    float unknown_30;           // +0x30
    int unknown_34;             // +0x34
    Vector3 drift;              // +0x38 smoke: rise in z; debris: speed; spheres: x = size factor
    int frame;                  // +0x44 texture frame
    int prevFrame;              // +0x48
    int frameTimer;             // +0x4C ticks until the next frame
    int life;                   // +0x50 ticks left
    int state;                  // +0x54 debris: landed
    std::uint32_t color;        // +0x58 ARGB
    std::uint32_t prevColor;    // +0x5C
    int dead;                   // +0x60 set by the tick when the update returned 0
};
static_assert(sizeof(Particle) == 0x64);

// One element of a source's own array (+0x154, 0x38 bytes each, 32-byte aligned). What the slots
// hold depends on the source type; code indexes element 0 and reads element 1 through it.
struct ParticleElement {
    Vector3 pos;                // +0x00
    float value[10];            // +0x0C (value[3] = +0x18, value[5] = +0x20, ...)
    int ticks;                  // +0x34
};
static_assert(sizeof(ParticleElement) == 0x38);

// The object a source's particles come from (CParticleSource +0x14): a pool of free particles,
// an MFC list at +0xB0 (taken from the head, returned to the tail).
struct ParticlePool {
    Particle* Acquire();                            // 00498870: from the pool, else new (0x64 bytes)
    void Release(Particle* p);                      // 004987C0: back to the pool's tail

    std::uint8_t unknown_000[0xB0];
    mfc::CTypedPtrList<Particle> freeParticles;     // +0x0B0
};
static_assert(offsetof(ParticlePool, freeParticles) == 0xB0);

class CParticleSource {
public:
    // --- per-frame tick (vtable slot 6; particle_tick.cpp) ---
    int Tick();                                     // 00496EB0: 1 while the source stays alive
    void AddParticle(Particle* p);                  // 00496A20: prev = current, bounds, list tail

    // --- per-particle callbacks (+0x1E4 update, +0x1E0 emit), __thiscall, return 0 to drop ---
    int UpdateFalling(Particle* p);                 // 0048F670 (types 0x2034-0x2037)
    int UpdateCloud(Particle* p);                   // 0048B570 (PoisonFog and similar)
    int UpdateSmoke(Particle* p);                   // 0048EC20 (CampFire, Fire, Smoke, 0x2010)
    int EmitSmoke(Particle* p, int index);          // 0048E9E0
    int UpdateDebris(Particle* p);                  // 00489190 (Nuke)
    int UpdateOrbit(Particle* p);                   // 0048E7C0 (Antimagic, spheres, 0x2014)

    // particle_emitters.cpp
    int EmitLightning(Particle* p, int index);      // 004923B0 (LightningBlast)
    int EmitModifier(Particle* p, int index);       // 00490130 (Modifier1-12)

    // Not rewritten (forwarder): offset and radius of the attachment's figure.
    Vector3* AttachmentOffset(Vector3* out, float* radius);   // 00485280

    // --- per-element callbacks (+0x1E8) ---
    void TickNuke(ParticleElement* e);              // 004892E0
    void TickTendril(ParticleElement* e);           // 0048EE20 (0x2010)
    void TickMushroom(ParticleElement* e);          // 00488F00 (Mushroom, Transform)

    // --- small setters the game calls on sources ---
    void ScaleSize(float factor);                   // 00485480
    void StopEmitting();                            // 00485440: flags bit 0 off, bit 2 on
    void StartEmitting();                           // 00485450: flags bits 0 and 2 on
    void Stop();                                    // 00485360: bits 0 and 2 off, detach

    // Not rewritten (forwarders).
    void Attach(void* object);                      // 004849F0
    Vector3* AttachmentPoint(Vector3* out, void* object);   // 00484C00

    void* vtable_;                  // +0x000
    int refCount_;                  // +0x004
    std::uint32_t flags_;           // +0x008 bit 0 emitting, bit 1 attached-only, bit 2 keep alive
    int id_;                        // +0x00C
    int type_;                      // +0x010 0x2000.. (see the name table at 0078B120)
    void* owner_;                   // +0x014
    std::uint32_t unknown_018;
    Vector3 position;               // +0x01C local position
    std::uint8_t unknown_028[0x94 - 0x28];
    void* attachedTo;               // +0x094
    Vector3 origin;                 // +0x098 world emission point
    Vector3 motion;                 // +0x0A4 movement since the last tick
    Vector3 side;                   // +0x0B0 unit, motion x (0, 0, 1) (zero while still)
    Vector3 up;                     // +0x0BC unit, motion x side
    float distance;                 // +0x0C8 length of motion
    int emitChance;                 // +0x0CC percent per try
    int emitCount;                  // +0x0D0 tries per tick
    float emitSpacing;              // +0x0D4
    int maxParticles;               // +0x0D8
    int frameCount;                 // +0x0DC
    int unknown_0E0;
    int frameDelay;                 // +0x0E4
    int unknown_0E8;
    int lifetime;                   // +0x0EC
    Vector3 spreadMin;              // +0x0F0
    Vector3 spreadMax;              // +0x0FC
    float unknown_108;
    float size;                     // +0x10C
    float height;                   // +0x110
    float growth;                   // +0x114
    float alpha;                    // +0x118
    float unknown_11C;
    float unknown_120;
    float unknown_124;
    float baseSize;                 // +0x128
    float unknown_12C;
    Vector3 extent;                 // +0x130
    int attachPoint;                // +0x13C
    Vector3 windDirection;          // +0x140 copied from the world every tick
    float windStrength;             // +0x14C
    float unknown_150;
    ParticleElement* elements;      // +0x154
    int elementCount;               // +0x158
    int elementCapacity;            // +0x15C (elements is an MFC-style array: data, size, max, grow by)
    int elementGrowBy;              // +0x160
    Vector3 boundsMin;              // +0x164 over the particles, rebuilt every tick
    Vector3 boundsMax;              // +0x170
    std::uint32_t boundsFlags;      // +0x17C bit 1 set by the tick
    Vector3 boundsCenter;           // +0x180
    std::uint32_t unknown_18C;
    Vector3 boundsLow;              // +0x190 boundsMin - boundsCenter
    Vector3 boundsHigh;             // +0x19C boundsMax - boundsCenter
    std::uint8_t unknown_1A8[0x1B4 - 0x1A8];
    mfc::CTypedPtrList<Particle> particles;     // +0x1B4
    std::uint8_t unknown_1D0[0x1E0 - 0x1D0];
    void* emitFn;                   // +0x1E0
    void* updateFn;                 // +0x1E4
    void* elementFn;                // +0x1E8
    void* unknown_1EC;
};
static_assert(offsetof(CParticleSource, attachedTo) == 0x94);
static_assert(offsetof(CParticleSource, emitChance) == 0xCC);
static_assert(offsetof(CParticleSource, size) == 0x10C);
static_assert(offsetof(CParticleSource, extent) == 0x130);
static_assert(offsetof(CParticleSource, elements) == 0x154);
static_assert(offsetof(CParticleSource, distance) == 0xC8);
static_assert(offsetof(CParticleSource, boundsFlags) == 0x17C);
static_assert(offsetof(CParticleSource, boundsHigh) == 0x19C);
static_assert(offsetof(CParticleSource, particles) == 0x1B4);
static_assert(offsetof(CParticleSource, emitFn) == 0x1E0);
static_assert(sizeof(CParticleSource) == 0x1F0);

// 00488940. `out` = v / d, all three quotients computed before the first store. __fastcall (out in
// ECX, v in EDX, d on the stack); returns out.
Vector3* __fastcall DivideVector(Vector3* out, const Vector3& v, float d);

// 00497EB0 (particle_emitters.cpp). Room for `count` elements, 32-byte aligned, the allocated block
// stored just below; __fastcall.
ParticleElement* __fastcall AllocateElements(int count);

// The world's rain/snow layer. Only the fields the fade uses.
class CRainSnow {
public:
    void UpdateFade(float now);                     // 004E40B0
    void FinishFade();                              // 004E4050
    void SetWind(const Vector3& direction, float strength);    // 004E41F0

    std::uint8_t unknown_000[0x24];
    int sound;                      // +0x024 rain loop handle in the sound manager (0: none)
    std::uint8_t unknown_028[0xA8 - 0x28];
    Vector3 wind;                   // +0x0A8
    int mode;                       // +0x0B4 0 off, 1 rain, 2 snow
    int targetMode;                 // +0x0B8
    int density;                    // +0x0BC 0..64
    float fadeStart;                // +0x0C0 world time
    float fadeDuration;             // +0x0C4 0: no fade running
};
static_assert(offsetof(CRainSnow, wind) == 0xA8);
static_assert(sizeof(CRainSnow) == 0xC8);

// A fog cell (the hash_map value; the key is (y << 16) + x of the map square).
struct FogCell {
    std::int16_t x;                 // +0x00 map square
    std::int16_t y;                 // +0x02
    Vector3 pos;                    // +0x04 jittered centre, z = ground + 0.5
    float alpha;                    // +0x10
    std::uint8_t visible;           // +0x14
    std::int8_t frame;              // +0x15 0..14, picks the texture tile
    std::uint8_t unknown_16[2];
};
static_assert(sizeof(FogCell) == 0x18);

// The squares currently seen: rows of bytes, nonzero = visible.
struct FogGrid {
    int x0;                         // +0x00
    int y0;                         // +0x04
    int unknown_08;
    std::uint8_t** rows;            // +0x0C
    int width;                      // +0x10
    int height;                     // +0x14
};

class CVisibilityFog {
public:
    using CellMap = stlp::hash_map<std::uint32_t, FogCell, stlp::IntHash>;

    void ComputeBounds();                           // 004E1770
    int UpdateCells();                              // 004E1AB0 (vtable slot 6; no C++ virtuals here)
    void Reveal(const FogGrid* grid);               // 004E1C60

    // Not rewritten (forwarders).
    void* World();                                  // 00455950 (CEngineObject)

    void* vtable_;                  // +0x00
    int refCount_;                  // +0x04
    std::uint8_t unknown_08[0x1C - 0x08];
    int boundsKind;                 // +0x1C 3 once computed
    Vector3 center;                 // +0x20
    float radius;                   // +0x2C
    Vector3 boundsMin;              // +0x30 relative to center
    Vector3 boundsMax;              // +0x3C
    Vector3 unknown_48;             // +0x48 zeroed with the bounds
    CellMap cells;                  // +0x54
    std::uint8_t boundsValid;       // +0x68
    std::uint8_t unknown_69[3];
};
static_assert(offsetof(CVisibilityFog, cells) == 0x54);
static_assert(sizeof(CVisibilityFog) == 0x6C);
