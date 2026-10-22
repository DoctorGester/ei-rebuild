#pragma once

// Weather, visibility fog and particles: the per-frame simulation parts.
//
// - CParticleSource (CParticleSource::Vtable() in place of game.exe's 0073CFD0, class name from 004982D0): one emitter. Its tick (slot 6,
//   00496EB0, particle_tick.cpp) runs the per-type callbacks the constructor (slot 2, 00485540) stores
//   at +0x1E0 (emit one particle), +0x1E4 (update one particle; 0 kills it) and +0x1E8 (update one
//   element of the source's own array at +0x154). The callbacks are __thiscall on the source.
// - CRainSnow (the world's rain/snow layer; "RainSnow"): fades its density and the rain sound.
// - CVisibilityFog (vtable 0073D6B4, "CVisibilityFog", class id 0x5F): a hash_map of fog cells,
//   one per visible map square; each cell fades in while visible and out once not, and is dropped
//   when nearly transparent.
//
// Names are provisional. Layouts mirror the original; unknown bytes are unknown_XXX.

#include "base_manager.h"
#include "core_transform.h"
#include "maths.h"
#include "model.h"   // ClipBounds
#include "search_grid.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/mfc_coll.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CMemoryStream;   // core_streams.h
class CSceneRenderer;  // model.h
struct TLVertex;       // clip.h
struct VertexPacket;   // figure_mesh.h

// One particle (0x64 bytes, allocated by the source's pool 00498870). The source's tick saves the
// "prev" fields before calling the update callback and restores them when it returns 0. The
// meaning of +0x20..+0x40 depends on the source type; the comments name the types rewritten here.
struct Particle {
    Vector3 pos;                // +0x00
    float size;                 // +0x0C
    Vector3 prevPos;            // +0x10
    float prevSize;             // +0x1C
    Vector3 velocity;           // +0x20 smoke: initial offset; spheres: orbit offset
    float aux0;                 // +0x2C \ per-type scratch the emitter and update callbacks
    float aux1;                 // +0x30 | share (angles, speeds, growth, steps)
    float aux2;                 // +0x34 /
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
// hold depends on the source type (the views below, by the callbacks of particle_callbacks.cpp and
// particle_emitters.cpp that use them); code indexes element 0 and reads element 1 through it.
struct ParticleElement {
    Vector3 pos;                // +0x00
    union {
        float value[10];        // +0x0C (value[3] = +0x18, value[5] = +0x20, ...)
        std::int32_t word[10];  // the same slots as dwords (the set-ups clear some the type never reads)
        // Lightning (EmitLightning, particle_emitters.cpp): a point of the bolt, on the ground;
        // LightningBlast's heads (TickLightning) move by fork and remember their position in base.
        struct {
            Vector3 fork;       // +0x0C
            Vector3 base;       // +0x18 pos as placed
            std::uint8_t unused_24[0xC];
            int emitted;        // +0x30 point 0: particles emitted along the bolt
        } bolt;
        // Modifier1-12 (EmitModifier): a point of the ring around the carrier.
        struct {
            Vector3 start;      // +0x0C pos as placed (TickModifier: the last position)
            Vector3 offset;     // +0x18 from the emission point
        } ring;
        // The ground plane under the source (TickGroundPlane, PlatformSet::SurfacePlane).
        struct {
            float height;       // +0x0C
            float unused_10[2];
            Vector3 normal;     // +0x18 its z replaced by its reciprocal when not 0
            std::uint8_t unused_24[0xC];
            std::uint32_t color;    // +0x30 the ground dust's colour (0x203A / 0x203B)
        } plane;
        // FeetCloud (TickFeetCloud): a foot; ticks says which (nonzero: the left).
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // +0x18 the carrier's radius (AttachmentOffset)
            float unused_1C[5];
            std::int32_t countdown;   // +0x30 reset every tick; the cloud starts at the foot while >= 0
        } foot;
        // Casting (0x2027-0x202A): element 0 the effect's state (pos.x 1 once set up, ticks the
        // countdown the alpha follows), elements 1.. the swirls.
        struct {
            float growth;       // +0x0C the particles' growth per tick
            float unused_10[2];
            float radius;       // +0x18
            float size;         // +0x1C the particles' size
            float unused_20[4];
            std::int32_t alpha; // +0x30
        } cast;
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 position;    // +0x18 drifting by velocity
            Vector3 velocity;    // +0x24
        } swirl;
        // Protection spheres and AntiMagic (0x2015-0x2017, 0x201A): element 0 (pos.z the height
        // above the carrier); element 1's pos is the carrier's smoothed motion.
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // +0x18 the sphere's
            float unused_1C;
            float speed;        // +0x20 the carrier's (0 below 0.01)
        } sphere;
        struct {
            std::uint8_t unused_0C[0xC];
            float tiltCos;      // +0x18 element 1: the tilt's cosine and sine (0.01 radians)
            float tiltSin;      // +0x1C
        } sphereTilt;
        // Casting (0x200B) and Healing (0x2006): a ring of flames (pos its centre, relative to
        // the source; ticks its countdown).
        struct {
            float radius;       // +0x0C
            float angle;        // +0x10 of its leading flame
            float rise;         // +0x14 per tick
            Vector3 centre;      // +0x18 its starting centre
        } flames;
        // CastingIllusion ... Healing (0x202B-0x202E): an orbiting light (pos its world position).
        struct {
            float turnCos;      // +0x0C the cosine and sine of its turn per tick
            float turnSin;      // +0x10
            float scale;        // +0x14 the carrier's radius (times -1 for 0x202C)
            Vector3 relative;    // +0x18 its position relative to the source (element 0's x: the orbit)
            Vector3 previous;    // +0x24 its last world position
        } orbiter;
        // Link (0x2014): element 0 the beam's length and its inverse (ticks the alpha cap);
        // elements 1-3 the beam's frame (direction, side, their cross product).
        struct {
            std::uint8_t unused_0C[0xC];
            float length;       // +0x18
            float inverseLength;    // +0x1C
        } link;
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 axis;        // +0x18 a unit vector
        } linkAxis;
        // FireBlast (0x2002): a fragment flying out (pos its position).
        struct {
            Vector3 velocity;   // +0x0C
            float radius;       // +0x18 the flame radius
            float growth;       // +0x1C its growth per tick
            float decay;        // +0x20 the growth's factor per tick
            float frame;        // +0x24 a random frame (0..5)
            float sizeFactor;   // +0x28 0 or 1
            float unused_2C;
            std::int32_t age;   // +0x30 in ticks
        } fragment;
        // Geyser / Fireworks (0x2009): a vent (pos the jet's angles and strength, ticks the
        // eruption's remaining ticks).
        struct {
            float unused_0C[9];
            std::int32_t pause; // +0x30 before the next eruption
        } vent;
        // Shower (0x200F, 0x2031-0x2033): a direction (pos); element 0's ticks is the texture frame.
        struct {
            float unused_0C[9];
            std::int32_t age;   // +0x30 in ticks; element 0: negative until the directions are chosen
        } ray;
        // Bag (0x203C): a ball (pos its position, ticks its countdown).
        struct {
            Vector3 step;        // +0x0C its last step
            Vector3 velocity;   // +0x18
            std::uint8_t unused_24[0xC];
            std::int32_t age;   // +0x30 its kind, then its age (indexes the sizes)
        } ball;
        // 0x202F: the ring around the carrier (ticks its age).
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // +0x18
            float unused_1C;
            float height;       // +0x20 the carrier's
            float unused_24[2];
            float foot;         // +0x2C the ring's offset below the carrier's centre
        } column;
        // Mushroom and Transform (0x200E, 0x2052): pos.x the puffs' spread, pos.z the cap's height.
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 stem;        // +0x18 the stem's foot
        } mushroom;
        // BlueGas (0x2013): pos the carrier's figure offset.
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // +0x18 the gas radius
            float unused_1C;
            float height;       // +0x20 the carrier's
        } gas;
        // Tornado and CurseHolder (0x200A, 0x2050): a ring of the funnel (pos its centre); element 0
        // also holds the funnel's own values.
        struct {
            float unused_0C[6];
            float height;       // +0x24 the funnel's
            float bend;         // +0x28 0: the rings sway with the source's motion, else they lean
            float damping;      // +0x2C the particles' spin damping
        } funnel;
        // Cylinder1 / Cylinder2 (0x203F, 0x2040): a star (pos relative to the source, ticks its
        // countdown).
        struct {
            Vector3 step;        // +0x0C its last step
            float rotation[2];  // +0x18 the cosine and sine of its turn per tick
            float unused_20;
            float inverseOrbit; // +0x24
            float orbit;        // +0x28 the orbit radius
            float rise;         // +0x2C per tick
        } star;
        // Banshee (0x200D): pos.z the ring's height, ticks the countdown.
        struct {
            float radius;       // +0x0C easing towards 30 times the source's height
        } banshee;
        // ClayRing (0x2018): a point on the ground (pos; ticks the countdown).
        struct {
            Vector3 previous;    // +0x0C its previous position
            Vector3 direction;   // +0x18 turning a little each tick
        } clay;
        // Falling (0x2034-0x2037): a gust.
        struct {
            float reach;        // +0x0C
            float direction;    // +0x10 radians, truncated
            float unused_14[7];
            std::int32_t age;   // +0x30
        } wind;
        // StartTrans (0x2051): element 0 (ticks the countdown).
        struct {
            std::uint8_t unused_0C[0xC];
            float scale;        // +0x18 the glow's size, growing with the source's
            float count;        // +0x1C the emitted count, rising by 0.4 per tick
        } trans;
        // ShapeChange glow (0x2039): element 0.
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 mark;        // +0x18 the last point glowed at
            std::uint8_t unused_24[0xC];
            std::uint32_t color;    // +0x30 by the alpha field
        } glow;
    };
    int ticks;                  // +0x34 (lightning point 0: bursts; modifier point 0: the fade step)
};
static_assert(sizeof(ParticleElement) == 0x38);
static_assert(offsetof(ParticleElement, bolt.emitted) == 0x30 && offsetof(ParticleElement, ring.offset) == 0x18);
static_assert(offsetof(ParticleElement, plane.normal) == 0x18 && offsetof(ParticleElement, plane.color) == 0x30);
static_assert(offsetof(ParticleElement, foot.radius) == 0x18 && offsetof(ParticleElement, foot.countdown) == 0x30);
static_assert(offsetof(ParticleElement, cast.radius) == 0x18 && offsetof(ParticleElement, cast.alpha) == 0x30);
static_assert(offsetof(ParticleElement, swirl.velocity) == 0x24 && offsetof(ParticleElement, sphere.speed) == 0x20);
static_assert(offsetof(ParticleElement, sphereTilt.tiltSin) == 0x1C && offsetof(ParticleElement, flames.centre) == 0x18);
static_assert(offsetof(ParticleElement, orbiter.previous) == 0x24 && offsetof(ParticleElement, link.inverseLength) == 0x1C);
static_assert(offsetof(ParticleElement, linkAxis.axis) == 0x18 && offsetof(ParticleElement, fragment.sizeFactor) == 0x28);
static_assert(offsetof(ParticleElement, fragment.age) == 0x30 && offsetof(ParticleElement, vent.pause) == 0x30);
static_assert(offsetof(ParticleElement, ray.age) == 0x30 && offsetof(ParticleElement, ball.velocity) == 0x18);
static_assert(offsetof(ParticleElement, ball.age) == 0x30 && offsetof(ParticleElement, column.foot) == 0x2C);
static_assert(offsetof(ParticleElement, column.height) == 0x20 && offsetof(ParticleElement, mushroom.stem) == 0x18);
static_assert(offsetof(ParticleElement, gas.height) == 0x20 && offsetof(ParticleElement, funnel.height) == 0x24);
static_assert(offsetof(ParticleElement, funnel.damping) == 0x2C && offsetof(ParticleElement, star.rotation) == 0x18);
static_assert(offsetof(ParticleElement, star.inverseOrbit) == 0x24 && offsetof(ParticleElement, star.rise) == 0x2C);
static_assert(offsetof(ParticleElement, banshee.radius) == 0x0C && offsetof(ParticleElement, clay.direction) == 0x18);
static_assert(offsetof(ParticleElement, wind.direction) == 0x10 && offsetof(ParticleElement, wind.age) == 0x30);
static_assert(offsetof(ParticleElement, trans.count) == 0x1C && offsetof(ParticleElement, glow.mark) == 0x18);
static_assert(offsetof(ParticleElement, glow.color) == 0x30 && offsetof(ParticleElement, word[9]) == 0x30);

class CParticleSource;
class CObject3D;
class CTexture;   // texture.h

// The particle manager (0xE8 bytes; the client world's +0x19C and *007AF754; a real virtual class:
// the compiler makes its vtable, C19, game.exe's 0073F644 is retired;
// CParticleSource +0x14 points to it): a CBaseManager whose objects are the sources (objectList_).
// It keeps a pool of free particles, an MFC list at +0xB0 (taken from the head, returned to the
// tail), and holds one reference on every source made through CreateSource (`held_`, by id) until
// the source's tick ends it (TickSources) or nobody else holds it (ReleaseUnheld).
// particle_manager.cpp; the pool in particle_tick.cpp, DrawAll in particle_draw.cpp.
// 004981A0 (small_accessors.cpp): the particle manager's own part of its constructor (the client
// world's manager set-up calls it after the base manager's): quietStart cleared.
class CParticleManager;
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager);

class CParticleManager : public CBaseManager {
public:
    using HeldMap = stlp::hash_map<int, CParticleSource*, stlp::IntHash>;

    // CBaseManager's constructor, an empty pool (blocks of 10), an empty held table (Construct(100),
    // 005725E0), maxHeldId_ 0, then 004981A0 (quietStart cleared). Inlined in the client world's
    // manager set-up (world_client_maps.cpp); world_destructors.cpp.
    CParticleManager();
    // 005627C0 (S13a). Destroy, the members last to first (`held_`'s nodes and buckets, the free
    // particles' list emptied), then CBaseManager's destructor.
    ~CParticleManager() override;
    const char* GetClassName() override;           // 11: 00562780 "CParticleManager"
    int GetClassId() override;                     // 12: 00562790 (0x2C)
    // 14: 004981B0 (particle_create.cpp, NewParticleSource). A new source of `type`.
    CEngineObject* NewObject(int type) override;

    // 2: 00497FC0. quietStart cleared, then CBaseManager::Create; returns 1 / 0.
    int Create() override;
    // 3: 00497FE0. Releases the held sources, empties `held_`, deletes the pooled particles, then
    // CBaseManager::Destroy; quietStart cleared.
    void Destroy() override;

    Particle* AcquireParticle();                    // 00498870: from the pool, else new (0x64 bytes)
    void ReleaseParticle(Particle* p);              // 004987C0: back to the pool's tail
    // particle_draw.cpp: draws every source back to front; 0 when a source failed to draw.
    int DrawAll(void* renderer);                    // 00498940
    // 00498400 (RET 4). A new source of `type` (CreateObject(type, -1)), referenced and held under
    // its id; with quietStart set, types 0x2001 / 0x2003 / 0x2004 start without emitting.
    CParticleSource* CreateSource(int type);
    // 00498480 (RET 4). Every source's tick (slot 6; with `paused` only the ones with flag 8); a
    // held source whose tick returned 0 is released and leaves `held_`. Returns 0.
    int TickSources(char paused);
    // 00498630. Every held source nobody else references (refCount_ 1) is released and leaves
    // `held_` (the client's zone load).
    void ReleaseUnheld();
    // The registrations (C20 retires them): 005627C0 the destructor, 005627A0 (slot 0) the
    // destructor, then operator delete when bit 0 of `flags` is set (returns this).
    void Destruct();
    CParticleManager* DeletingDestruct(unsigned flags);

    mfc::CTypedPtrList<Particle> freeParticles;     // +0x0B0
    HeldMap held_;                                  // +0x0CC id -> source holding a reference
    int maxHeldId_;                                 // +0x0E0 highest key of held_
    int quietStart;                                 // +0x0E4 see CreateSource; never set non-zero in src
};
static_assert(offsetof(CParticleManager, freeParticles) == 0xB0);
static_assert(offsetof(CParticleManager, held_) == 0xCC);
static_assert(offsetof(CParticleManager, maxHeldId_) == 0xE0);
static_assert(sizeof(CParticleManager) == 0xE8);

// One entry of the draw order (particle_draw.cpp): the source's depth along the camera's view axis.
struct ParticleDepth {
    float depth;
    CParticleSource* source;
};

// 007A9C50: the draw order, an MFC CArray<ParticleDepth> (vtable, data, size, max, grow by).
using ParticleDepthArray = mfc::CArray<ParticleDepth>;   // SetAtGrow 00499200 is the template's

// The draw order's sort: STLport's introsort on [first, last) with "greater depth first"
// (far sources first), as instantiated in game.exe. All __fastcall (first, last in registers).
void __fastcall IntroSortByDepth(ParticleDepth* first, ParticleDepth* last, void* tag, int depthLimit,
                                 int compare);                                          // 00499380
void __fastcall FinalSortByDepth(ParticleDepth* first, ParticleDepth* last, int compare);  // 004994E0
void __fastcall MakeHeapByDepth(ParticleDepth* first, ParticleDepth* last, void* tag, int compare,
                                void* distanceTag);                                     // 00499CD0
void __fastcall PopHeapByDepth(ParticleDepth* first, ParticleDepth* last, ParticleDepth* result,
                               ParticleDepth value, int compare, void* distanceTag);    // 00499C20

// One texture frame's UV rectangle (a particle source's frames, 16 bytes each).
struct SourceFrame {
    float u0, v0, u1, v1;
};
static_assert(sizeof(SourceFrame) == 16);

// A particle source: a transform object (CTransformObject, whose vtable the factory stores on the
// way): localPosition_ (+0x1C) is where it emits relative to what it is attached to, localRotation_
// (+0x34) the identity after Reset.
class CParticleSource : public CTransformObject {
public:
    // The compiled table (C19, particle_create.cpp's ParticleSourceClass; game.exe's 0073CFD0).
    static void* Vtable();
    // particle_render.cpp: fills the shared vertex / sprite arrays and draws through the renderer
    // (007AF060, vtable slot 17). 1 when there is nothing to draw or it is off screen.
    int Draw(void* renderer);                       // 004976B0

    // --- set-up (particle_create.cpp) ---
    int CreateForType();                            // 00485540 (vtable slot 2): per-type parameters
    void Reset();                                   // 004847E0: defaults, texture and elements dropped
    void Clear();                                   // 00484710 (vtable slot 3): particles back to the pool, detach, Reset
    void Destruct();                                // 00498310: destructor body (Clear, free, base)
    CParticleSource* DeletingDestruct(int flags);   // 004982F0 (slot 0): Destruct, delete on bit 0

    // --- per-frame tick (vtable slot 6; particle_tick.cpp) ---
    int Tick();                                     // 00496EB0: 1 while the source stays alive
    void AddParticle(Particle* p);                  // 00496A20: prev = current, bounds, list tail

    // The callbacks Create stores (+0x1E0 emit, +0x1E4 update, +0x1E8 element): __thiscall members
    // below. Emit fills a particle from the pool (0 returns it), update moves one (0 drops it).
    using EmitCallback = int (CParticleSource::*)(Particle* p, int index);
    using UpdateCallback = int (CParticleSource::*)(Particle* p);
    using ElementCallback = void (CParticleSource::*)(ParticleElement* e);

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
    int EmitZoneExit(Particle* p, int index);       // 00492D30 (ZoneExit)
    int EmitFeebleMind(Particle* p, int index);     // 004957E0 (FeebleMind)
    int EmitFeetCloud(Particle* p, int index);      // 00495A30 (FeetCloud1/2)
    int UpdateGlow(Particle* p);                    // 00493280 (0x2039)
    int UpdateGroundDust(Particle* p);              // 004933F0 (0x203A, 0x203B)
    int UpdatePortal(Particle* p);                  // 00494770 (Portal)
    int UpdateSparks(Particle* p);                  // 00494CD0 (Sparks)
    void TickGroundPlane(ParticleElement* e);       // 004934F0 (0x203A, 0x203B)
    void TickPortal(ParticleElement* e);            // 00494930 (Portal)
    void TickStartTrans(ParticleElement* e);        // 00496990 (StartTrans)

    // particle_draw.cpp: the emission point in the world (attachment point plus the local position).
    Vector3* WorldPosition(Vector3* out);           // 00484AE0

    // particle_callbacks.cpp
    int EmitCasting(Particle* p, int index);        // 004918E0 (0x2027-0x202A)
    int UpdateCasting(Particle* p);                 // 00492030
    void TickCasting(ParticleElement* e);           // 004921D0
    int EmitOrbit(Particle* p, int index);          // 0048E070 (0x2015-0x2017, 0x201A)
    int EmitCastRing(Particle* p, int index);       // 00489320 (Casting, 0x200B)
    int UpdateCastRing(Particle* p);                // 00489A50
    void TickCastRing(ParticleElement* e);          // 00489B40
    int EmitHealing(Particle* p, int index);        // 0048B630 (Healing, 0x2006)
    int UpdateHealing(Particle* p);                 // 0048BBE0
    void TickHealing(ParticleElement* e);           // 0048BCE0
    int EmitCastSwirl(Particle* p, int index);      // 00490C10 (0x202B-0x202E)
    int UpdateCastSwirl(Particle* p);               // 00491230
    void TickCastSwirl(ParticleElement* e);         // 00491390
    int EmitLink(Particle* p, int index);           // 0048D8D0 (Link, 0x2014)
    int UpdateLink(Particle* p);                    // 0048DE30
    void TickLink(ParticleElement* e);              // 0048E040
    int EmitBlast(Particle* p, int index);          // 0048C090 (FireBlast, 0x2002)
    int UpdateBlast(Particle* p);                   // 0048C590
    void TickBlast(ParticleElement* e);             // 0048C760
    int EmitPoisonFog(Particle* p, int index);      // 0048B080 (PoisonFog, 0x2007 / 0x2008)
    int EmitGeyser(Particle* p, int index);         // 0048A8F0 (Geyser, 0x2009)
    int UpdateGeyser(Particle* p);                  // 0048ADA0
    void TickGeyser(ParticleElement* e);            // 0048AEC0
    int EmitShower(Particle* p, int index);         // 0048EEA0 (0x200F, 0x2031-0x2033)
    int UpdateShower(Particle* p);                  // 0048F300
    void TickShower(ParticleElement* e);            // 0048F380
    int EmitBag(Particle* p, int index);            // 00493560 (Bag, 0x203C)
    int UpdateBag(Particle* p);                     // 00493770
    void TickBag(ParticleElement* e);               // 004938C0
    int EmitColumn(Particle* p, int index);         // 00491420 (0x202F)
    int UpdateColumn(Particle* p);                  // 00491830
    void TickColumn(ParticleElement* e);            // 004918D0
    int EmitMushroom(Particle* p, int index);       // 004889D0 (Mushroom, Transform)
    int UpdateMushroom(Particle* p);                // 00488DB0
    int EmitStench(Particle* p, int index);         // 0048D420 (BlueGas, 0x2013)
    int UpdateStench(Particle* p);                  // 0048D7F0
    int EmitTornado(Particle* p, int index);        // 00489F70 (Tornado, CurseHolder)
    int UpdateTornado(Particle* p);                 // 0048A310
    void TickTornado(ParticleElement* e);           // 0048A560
    int EmitPortalStar(Particle* p, int index);     // 00493D10 (PortalStar, 0x203D)
    int UpdatePortalStar(Particle* p);              // 00493D50
    int EmitCylinder(Particle* p, int index);       // 00493DE0 (Cylinder1/2, 0x203F / 0x2040)
    int UpdateCylinder(Particle* p);                // 00494170
    void TickCylinder(ParticleElement* e);          // 00494340
    int EmitFireBall(Particle* p, int index);       // 0048C800 (FireBall, 0x2000)
    int UpdateFireBall(Particle* p);                // 0048CAD0
    int EmitArrowSmoke(Particle* p, int index);     // 0048CBA0 (FireArrow 0x2011, 0x204E)
    int UpdateArrowSmoke(Particle* p);              // 0048CED0
    int EmitBanshee(Particle* p, int index);        // 00489BE0 (Banshee, 0x200D)
    int UpdateBanshee(Particle* p);                 // 00489EF0
    void TickBanshee(ParticleElement* e);           // 00489F30
    int EmitPortal(Particle* p, int index);         // 00494470 (Portal, 0x203E)
    int EmitAcidRay(Particle* p, int index);        // 0048D050 (AcidRay, 0x2012)
    int UpdateAcidRay(Particle* p);                 // 0048D330
    int EmitSparks(Particle* p, int index);         // 00494A10 (Sparks, 0x2043)
    int EmitClayRing(Particle* p, int index);       // 0048F7F0 (ClayRing, 0x2018)
    int UpdateClayRing(Particle* p);                // 0048FAB0
    void TickClayRing(ParticleElement* e);          // 0048FBE0
    int EmitFalling(Particle* p, int index);        // 0048F3B0 (0x2034-0x2037)
    void TickFalling(ParticleElement* e);           // 0048F6F0
    int EmitVulkan(Particle* p, int index);         // 0048BD50 (VulcanSmoke, 0x2005)
    int UpdateVulkan(Particle* p);                  // 0048C010
    int EmitFireStar(Particle* p, int index);       // 00494950 (FireStar, AcidStar)
    int UpdateFireStar(Particle* p);                // 004949B0
    int EmitRegeneration(Particle* p, int index);   // 004950B0 (Regeneration)
    int UpdateRegeneration(Particle* p);            // 00495120
    int EmitVisionStar(Particle* p, int index);     // 00494E00 (VisionStar1-3)
    int UpdateVisionStar(Particle* p);              // 00494F30
    int UpdateZoneExit(Particle* p);                // 00492F40 (ZoneExit)
    int UpdateFeebleMind(Particle* p);              // 00495950 (FeebleMind, 0x2049)
    int UpdateFeetCloud(Particle* p);               // 00495C80 (FeetCloud, 0x2047)
    void TickFeetCloud(ParticleElement* e);         // 00495E00
    int EmitSilence(Particle* p, int index);        // 00495320 (Silence, 0x2048)
    int UpdateSilence(Particle* p);                 // 004955D0
    int UpdateModifier(Particle* p);                // 00490990 (Modifier1-12)
    void TickModifier(ParticleElement* e);          // 00490B00
    int UpdateLightning(Particle* p);               // 00492B80 (LightningBlast, 0x2030)
    void TickLightning(ParticleElement* e);         // 00492CC0
    int EmitTeleport(Particle* p, int index);       // 0048FDB0 (Teleport, 0x2019)
    int UpdateTeleport(Particle* p);                // 00490060
    void TickTeleport(ParticleElement* e);          // 00490110
    int EmitCurseStars(Particle* p, int index);     // 00496270 (CurseStars)
    int UpdateCurseStars(Particle* p);              // 004964B0
    int EmitStartTrans(Particle* p, int index);     // 004965E0 (StartTrans)
    int UpdateStartTrans(Particle* p);              // 00496840
    int EmitNuke(Particle* p, int index);           // 00488F90 (Nuke)
    int EmitTendril(Particle* p, int index);        // 0048ED60
    int EmitGroundDust(Particle* p, int index);     // 00493300 (0x203A, 0x203B)
    int EmitGlow(Particle* p, int index);           // 00492FC0 (ShapeChange, 0x2039)
    void TickGlow(ParticleElement* e);              // 004932F0
    int EmitStun(Particle* p, int index);           // 00495FB0 (Stun, 0x204D)
    int UpdateStun(Particle* p);                    // 004961B0

    // particle_attach.cpp. Offset (0, 0, the bounds centre's z) and radius of the object followed
    // (its bounds recomputed); (0, 0, 0) and 0.1 when there is none. Returns `out`.
    Vector3* AttachmentOffset(Vector3* out, float* radius);   // 00485280

    // --- per-element callbacks (+0x1E8) ---
    void TickNuke(ParticleElement* e);              // 004892E0
    void TickTendril(ParticleElement* e);           // 0048EE20 (0x2010)
    void TickMushroom(ParticleElement* e);          // 00488F00 (Mushroom, Transform)
    void TickNone(ParticleElement* e);              // 00485530 and ten more: does nothing (particle_create.cpp)

    // --- small setters the game calls on sources ---
    void ScaleSize(float factor);                   // 00485480
    void StopEmitting();                            // 00485440: flags bit 0 off, bit 2 on
    void StartEmitting();                           // 00485450: flags bits 0 and 2 on
    void Stop();                                    // 00485360: bits 0 and 2 off, detach

    // particle_attach.cpp. Follow `object` (a CObject3D, or null to let go): the old object
    // drops the source (its point is folded into the local position), the new one adds it, and
    // the emission point becomes the new attachment point (or the local position).
    void Attach(CObject3D* object);                 // 004849F0
    // Where the source sits on `object` (a CObject3D): with flag 0x10 a unit's part chosen by
    // attachPoint (0 the effect point, 1-7 head / hands / legs / body, 8-12 body00-04), else the
    // top of its bounds; without flag 0x10 its position. Returns `out`.
    Vector3* AttachmentPoint(Vector3* out, CObject3D* object);   // 00484C00
    // The world position of `object`'s first part named `prefix...`, else of its "bd" part, else
    // its position raised by the bounds' upper z extent. Returns `out` (`this` is not used).
    Vector3* PartPoint(Vector3* out, CObject3D* object, const char* prefix);   // 00484B40
    // Every particle back to the pool, emission off (flags 0 and 2), detached.
    void StopAndClear();                            // 00485380
    // The loop Clear and StopAndClear share: every particle unlinked and returned to the pool.
    void ReleaseAllParticles();

    // CEngineObject's flags_: bit 0 emitting, bit 1 attached-only, bit 2 keep alive; type_ 0x2000..
    // (see the name table at 0078B120); owner_ the particle manager.
    std::uint8_t unused_054[0x94 - 0x54];
    CObject3D* attachedTo;          // +0x094 the object followed, null: none
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
    int frameColumns;               // +0x0E0 columns of the texture's frame grid
    int frameDelay;                 // +0x0E4 (Tornado, CurseHolder: the particle colour)
    int hiddenTicks;                // +0x0E8 draws skipped off screen (the tick idles past 0x38 with flag 0x20)
    int lifetime;                   // +0x0EC
    Vector3 spreadMin;              // +0x0F0
    Vector3 spreadMax;              // +0x0FC
    float unused_108;
    float size;                     // +0x10C
    float height;                   // +0x110
    float growth;                   // +0x114
    float alpha;                    // +0x118
    float typeParam;                // +0x11C per type: rate, count or size (1.0 after Reset)
    float turnCos;                  // +0x120 \ cos / sin of the emitter's turn (a zone exit's
    float turnSin;                  // +0x124 / angle)
    float baseSize;                 // +0x128
    float emitState;                // +0x12C int bits: emitted count or a one-shot flag, per type
    Vector3 extent;                 // +0x130
    int attachPoint;                // +0x13C
    Vector3 windDirection;          // +0x140 copied from the world every tick
    float windStrength;             // +0x14C
    float additiveBlend;            // +0x150 int bits: 1 draws with additive blending
    ParticleElement* elements;      // +0x154
    int elementCount;               // +0x158
    int elementCapacity;            // +0x15C (elements is an MFC-style array: data, size, max, grow by)
    int elementGrowBy;              // +0x160
    Vector3 boundsMin;              // +0x164 over the particles, rebuilt every tick
    Vector3 boundsMax;              // +0x170
    // +0x17C the bounds the renderer clips by: flags bit 1 set by the tick, the box (boundsMin and
    // boundsMax less the centre) relative to the centre; the radius is not written.
    ClipBounds clip;
    std::uint8_t unused_1A8[0x1B4 - 0x1A8];
    mfc::CTypedPtrList<Particle> particles;     // +0x1B4
    // +0x1D0 the frame UV rectangles (32-byte aligned); the original inlines the array's SetSize.
    lib::AlignedArray<SourceFrame> frames;
    EmitCallback emitFn;            // +0x1E0
    UpdateCallback updateFn;        // +0x1E4
    ElementCallback elementFn;      // +0x1E8
    HeldTexture texture;            // +0x1EC texture holder (bound by 00497CE0)
};
static_assert(offsetof(CParticleSource, attachedTo) == 0x94);
static_assert(offsetof(CParticleSource, emitChance) == 0xCC);
static_assert(offsetof(CParticleSource, size) == 0x10C);
static_assert(offsetof(CParticleSource, extent) == 0x130);
static_assert(offsetof(CParticleSource, elements) == 0x154);
static_assert(offsetof(CParticleSource, distance) == 0xC8);
static_assert(offsetof(CParticleSource, clip) == 0x17C);
static_assert(offsetof(CParticleSource, particles) == 0x1B4);
static_assert(offsetof(CParticleSource, emitFn) == 0x1E0);
static_assert(sizeof(CParticleSource::EmitCallback) == 4);   // single inheritance: a plain code address
static_assert(sizeof(CParticleSource) == 0x1F0);

// 004981B0. The class factory's creator for particle sources (particle_create.cpp): a new source of
// `type` (the register arguments are unused), or null.
CParticleSource* __stdcall NewParticleSource(int type);
// 004982D0 / 004982E0 (CParticleSource slots 11 / 12, particle_create.cpp): "CParticle", 12.
const char* __fastcall ParticleSourceClassName(CParticleSource* source);
int __fastcall ParticleSourceClassId(CParticleSource* source);

// 00488940. `out` = v / d, all three quotients computed before the first store. __fastcall (out in
// ECX, v in EDX, d on the stack); returns out.
Vector3* __fastcall DivideVector(Vector3* out, const Vector3& v, float d);

// The particle renderer's shared arrays (particle_render.cpp). One vertex (position and size,
// interpolated between the last two ticks) and one sprite (colour, texture frame) per particle.
struct ParticleVertex {
    float x, y, z, size;
};
struct ParticleSprite {
    std::uint32_t color;            // ARGB
    const SourceFrame* frame;       // the particle's frame among its source's frames
};
// 007A9BD0: data 32-byte aligned (00468F70), the block address below it.
using ParticleVertexArray = lib::AlignedArray<ParticleVertex>;
// 007A9BE0: an MFC-style array with a vtable (0073C584, constructor 00468D60).
using ParticleSpriteArray = mfc::CArray<ParticleSprite>;   // SetSize 00497D50 is the template's

// (00497D40, the list position's particle with the position advanced, is the list's GetNext.)

// (00497CE0, the renderer's bind of a held texture, is CSceneRenderer::BindHeldTexture, model.h.)

// 00497EB0 (particle_emitters.cpp). Room for `count` elements, 32-byte aligned, the allocated block
// stored just below; __fastcall.
ParticleElement* __fastcall AllocateElements(int count);

// The world's rain/snow layer (CWorldClient + 0x6F4, CWorldClient::RainSnow(); an engine object
// whose slot 3 is the thunk 005617B0 -> 004E3B80). Only the fields its rewritten members use.
class CRainSnow : public CEngineObject {
public:
    // Body of the slot at 004E5680 (registered by member pointer).
    int DrawImpl(CSceneRenderer* renderer);
    // The compiled table (C19, effects_weather.cpp's RainSnowClass; game.exe's 0073F4C8 with its tail 0073F4E8).
    static void* Vtable();

    // 005616C0. The destructor's body: Clear, the work arrays freed (last member first), the texture
    // released, then the engine object's body.
    void DestructBody();
    // 005617E0 (slot 0, RET 4): DestructBody, then operator delete when flags bit 0 is set.
    CRainSnow* DeletingDestruct(unsigned flags);
    void UpdateFade(float now);                     // 004E40B0
    void FinishFade();                              // 004E4050
    void SetWind(const Vector3& direction, float strength);    // 004E41F0
    // 004E3B80 (slot 3 through 005617B0). The rain loop stopped, the texture released, wind, mode,
    // fade and world cleared, density 64.
    void Clear();
    // 004E3C20 (effects_weather_draw.cpp; the client world's setup 00561E50). The drawing code's
    // work arrays sized and their fixed parts filled.
    void PrepareBuffers();
    // 004E3FC0 (RET 8; the weather messages 00669798). The texture "RainSnow" (kind 6) made sure of;
    // a running loop stopped unless `newMode` is 0; rain starts the looping "nature\rain\circle.wav";
    // the fade towards `newMode` starts now (the world's tick) over `duration` (0: at the next step).
    void Start(int newMode, float duration);
    // weather_particles.cpp (A-S13). 004E4240 (rain) / 004E4C80 (snow), RET 4, EAX 1; the layer's
    // draw 004E5680 (weather_draw.cpp) calls the one of the mode. The particles of the 81 x 81 grid
    // cells around the point the wind carries them to the camera from, each cell's place and time
    // hashed, `density` of 64 of them shown: a rain drop is a thin triangle along the wind, a
    // snowflake a point swayed by the five sway tables that becomes a small triangle on screen.
    // Projected with the pipeline's stack 2 and drawn as pre-transformed triangles through
    // `indices` with the device's current state.
    int DrawRain(CSceneRenderer* renderer);
    int DrawSnow(CSceneRenderer* renderer);
    // Their two halves (the original has them in one piece): the vertices of the particles on
    // screen built (returns their count, three per particle), and those drawn (the distance fog
    // when the device has it on, DrawIndexedPrimitive under the device lock, the frame's counts).
    int BuildRain(CSceneRenderer* renderer);
    int BuildSnow(CSceneRenderer* renderer);
    void DrawParticles(CSceneRenderer* renderer, int vertexCount);

    HeldTexture texture;            // +0x01C counted texture ("RainSnow")
    class CWorldClient* world;      // +0x020 the client world (set after construction)
    int sound;                      // +0x024 rain loop handle in the sound manager (0: none)
    // The draws' work arrays.
    lib::AlignedArray<Vector4> projected;       // +0x028 the positions projected (four per packet)
    lib::AlignedArray<VertexPacket> positions;  // +0x038 the particles' corners (w not written)
    mfc::impl::CByteArray outcodes;             // +0x048 a clip outcode per position
    mfc::impl::CByteArray frames;               // +0x05C a texture frame (0..3) per particle
    lib::AlignedArray<TLVertex> vertices;       // +0x070 what is drawn (colours never written)
    mfc::impl::CWordArray indices;              // +0x080 0, 1, 2, ... (filled elsewhere)
    lib::AlignedArray<int> offsets;             // +0x094 the grid's 81 cell offsets (filled elsewhere)
    int particleCount;              // +0x0A4 positions built by the last draw
    Vector3 wind;                   // +0x0A8
    int mode;                       // +0x0B4 0 off, 1 rain, 2 snow
    int targetMode;                 // +0x0B8
    int density;                    // +0x0BC 0..64
    float fadeStart;                // +0x0C0 world time
    float fadeDuration;             // +0x0C4 0: no fade running
};
static_assert(offsetof(CRainSnow, texture) == 0x1C);
static_assert(offsetof(CRainSnow, particleCount) == 0xA4 && offsetof(CRainSnow, projected) == 0x28);
static_assert(offsetof(CRainSnow, vertices) == 0x70 && offsetof(CRainSnow, offsets) == 0x94);
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
    std::uint8_t unused_16[2];
};
static_assert(sizeof(FogCell) == 0x18);

// The texture coordinates of the fog texture's 4 x 4 tiles (007ABA58, 16 records; the fog manager's
// Create fills them, the fog's draw hands them to the renderer): a tile's u range, and its v range
// counted down from 1 a row.
struct FogTile {
    float u0;   // +0x00 column / 4
    float v1;   // +0x04 v0 + 1/4
    float u1;   // +0x08 u0 + 1/4
    float v0;   // +0x0C 1 - (row + 1) / 4
};
inline FogTile* FogTiles() { return ei::GamePointer<FogTile>(0x007ABA58); }

// The squares currently seen: rows of bytes, nonzero = visible.
struct FogGrid {
    // World-side members (ai_fog_grid.cpp).
    void Clear();                         // 005C8B60: no origin (0x7FFFFFFF), 1 x 1, zeroed
    // 005C8BF0 / 005C8CC0 (RET 4): x0, y0, width, height (dwords), then the cells; reading makes
    // the cells again when the size changed.
    void Read(CMemoryStream* stream);
    void Write(CMemoryStream* stream);
    // 005C8D40 (ai_movement.cpp; CAIMap::MarkVisibleCells). Grows the grid to cover (x - n .. x + n,
    // y - n .. y + n); the first call (x0 = 0x7FFFFFFF) sizes and clears it.
    void Grow(int x, int y, int n);

    int x0;                         // +0x00 map cell of cells[0][0]; 0x7FFFFFFF before the first Grow
    int y0;                         // +0x04
    SearchGrid<std::uint8_t> cells; // +0x08 width * height bytes and the row table (operator new)
};
static_assert(sizeof(FogGrid) == 0x18);

// The engine object's virtuals are game.exe's (vtable 0073D6B4, set by the manager; never constructed here).
class CVisibilityFog : public CEngineObject {
public:
    using CellMap = stlp::hash_map<std::uint32_t, FogCell, stlp::IntHash>;

    void ComputeBounds();                           // 004E1770
    int UpdateCells();                              // 004E1AB0 (vtable slot 6; no C++ virtuals here)
    void Reveal(const FogGrid* grid);               // 004E1C60
    // A-S10 (effects_weather.cpp). Every cell freed (nodes back to the pool), the bounds invalid:
    // the body of slots 2 (004E23D0, returns 1) and 3 (004E2440).
    void ClearCells();
    // 004E24E0. The destructor body: the vtable, ClearCells, the table freed, the engine object's
    // base destructor. 004E24C0 (slot 0, RET 4): it, then operator delete on bit 0; returns this.
    void Destruct();
    CVisibilityFog* DeletingDestruct(int flags);
    // 004E1EB0 (RET 4; effects_weather_draw.cpp). The cells as sprites through the scene renderer
    // (its result); 1 without cells or when the bounds are out of view.
    int Draw(class CSceneRenderer* renderer);

    // Not rewritten (forwarders).
    CWorld* World();                                // 00455950 (CEngineObject)

    ClipBounds bounds;              // +0x1C flags 3 once computed; the box relative to the centre
    Vector3 boundsSpare;            // +0x48 zeroed with the bounds, never read
    CellMap cells;                  // +0x54
    std::uint8_t boundsValid;       // +0x68
    std::uint8_t unused_69[3];
};
static_assert(offsetof(CVisibilityFog, bounds) == 0x1C);
static_assert(offsetof(CVisibilityFog, cells) == 0x54);
// The fog object's slots 2 / 3 (004E23D0: ClearCells, returns 1; 004E2440: ClearCells) and 11 / 12
// (004E24A0 "CVisibilityFog", 004E24B0 0x5F); effects_weather.cpp.
int __fastcall FogCreate(CVisibilityFog* self);
void __fastcall FogDestroy(CVisibilityFog* self);
const char* __fastcall FogClassName(CVisibilityFog* self);
int __fastcall FogClassId(CVisibilityFog* self);
static_assert(sizeof(CVisibilityFog) == 0x6C);

// The size of an orbiting sphere's particle by the ticks it has left (the fire, electricity and air spheres'
// 16 entries; the antimagic sphere's 6): effects.cpp, particle_callbacks.cpp.
inline constexpr float kSphereSizeByLife[16] = /*0073CB38*/ {
    0.01f, 0.02f, 0.03f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.03f,
    0.02f, 0.01f};
inline constexpr float kAntimagicSizeByLife[6] = /*0073CB78*/ {0.01f, 0.015f, 0.02f, 0.03f, 0.015f, 0.01f};
