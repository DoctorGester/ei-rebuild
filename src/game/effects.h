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

#include "base_manager.h"
#include "maths.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

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
    float aux1;                 // +0x30 | share (angles, speeds, growth; aux2 often holds
    int aux2;                   // +0x34 / float bits)
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

class CParticleSource;
class CObject3D;

// The particle manager (vtable 0073F644, 0xE8 bytes; the client world's +0x19C and *007AF754;
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
    // 005627C0 (S13a, world_destructors.cpp). The destructor's body: the vtable, Destroy, the
    // members last to first (`held_`'s nodes and buckets, the free particles' list emptied), then
    // CBaseManager's body.
    void Destruct();
    // 005627A0 (slot 0): Destruct, then operator delete when bit 0 of `flags` is set. Returns this.
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

class CParticleSource {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073CFD0);
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
    void Attach(void* object);                      // 004849F0
    // Where the source sits on `object` (a CObject3D): with flag 0x10 a unit's part chosen by
    // attachPoint (0 the effect point, 1-7 head / hands / legs / body, 8-12 body00-04), else the
    // top of its bounds; without flag 0x10 its position. Returns `out`.
    Vector3* AttachmentPoint(Vector3* out, void* object);   // 00484C00
    // The world position of `object`'s first part named `prefix...`, else of its "bd" part, else
    // its position raised by the bounds' upper z extent. Returns `out` (`this` is not used).
    Vector3* PartPoint(Vector3* out, CObject3D* object, const char* prefix);   // 00484B40
    // The source as the engine object it is (its first 0x1C bytes; slots 0-12 are CEngineObject's).
    CEngineObject* AsObject() { return reinterpret_cast<CEngineObject*>(this); }

    // Every particle back to the pool, emission off (flags 0 and 2), detached.
    void StopAndClear();                            // 00485380
    // The loop Clear and StopAndClear share: every particle unlinked and returned to the pool.
    void ReleaseAllParticles();

    void* vtable_;                  // +0x000
    int refCount_;                  // +0x004
    std::uint32_t flags_;           // +0x008 bit 0 emitting, bit 1 attached-only, bit 2 keep alive
    int id_;                        // +0x00C
    int type_;                      // +0x010 0x2000.. (see the name table at 0078B120)
    void* owner_;                   // +0x014
    std::uint32_t unused_018;
    Vector3 position;               // +0x01C local position
    std::uint8_t unused_028[0x34 - 0x28];
    Quaternion orientation;         // +0x034 the identity after Reset (the transform's local rotation)
    std::uint8_t unused_044[0x94 - 0x44];
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
    std::uint32_t boundsFlags;      // +0x17C bit 1 set by the tick
    Vector3 boundsCenter;           // +0x180
    std::uint32_t unused_18C;
    Vector3 boundsLow;              // +0x190 boundsMin - boundsCenter
    Vector3 boundsHigh;             // +0x19C boundsMax - boundsCenter
    std::uint8_t unused_1A8[0x1B4 - 0x1A8];
    mfc::CTypedPtrList<Particle> particles;     // +0x1B4
    int frameBase;                  // +0x1D0 address of the frame UV rectangles (16 bytes each, 32-byte aligned)
    int frameRectCount;             // +0x1D4 (an MFC-style array: data, size, max, grow by)
    int frameRectCapacity;          // +0x1D8
    int frameRectGrowBy;            // +0x1DC
    EmitCallback emitFn;            // +0x1E0
    UpdateCallback updateFn;        // +0x1E4
    ElementCallback elementFn;      // +0x1E8
    void* texture;                  // +0x1EC texture holder (bound by 00497CE0)
};
static_assert(offsetof(CParticleSource, attachedTo) == 0x94);
static_assert(offsetof(CParticleSource, orientation) == 0x34);
static_assert(offsetof(CParticleSource, emitChance) == 0xCC);
static_assert(offsetof(CParticleSource, size) == 0x10C);
static_assert(offsetof(CParticleSource, extent) == 0x130);
static_assert(offsetof(CParticleSource, elements) == 0x154);
static_assert(offsetof(CParticleSource, distance) == 0xC8);
static_assert(offsetof(CParticleSource, boundsFlags) == 0x17C);
static_assert(offsetof(CParticleSource, boundsHigh) == 0x19C);
static_assert(offsetof(CParticleSource, particles) == 0x1B4);
static_assert(offsetof(CParticleSource, emitFn) == 0x1E0);
static_assert(sizeof(CParticleSource::EmitCallback) == 4);   // single inheritance: a plain code address
static_assert(sizeof(CParticleSource) == 0x1F0);

// 004981B0. The class factory's creator for particle sources (particle_create.cpp): a new source of
// `type` (the register arguments are unused), or null.
CParticleSource* __fastcall NewParticleSource(void*, void*, int type);
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
    int frame;                      // texture frame index (+ the source's base +0x1D0)
};
// 007A9BD0: data 32-byte aligned (00468F70), the block address below it.
using ParticleVertexArray = lib::AlignedArray<ParticleVertex>;
// 007A9BE0: an MFC-style array with a vtable (0073C584, constructor 00468D60).
using ParticleSpriteArray = mfc::CArray<ParticleSprite>;   // SetSize 00497D50 is the template's

// 00497D40. The list position's particle, advancing the position (MFC GetNext); __thiscall on the
// list, which it does not use.
Particle** __fastcall NextParticle(void* list, void*, mfc::CTypedPtrList<Particle>::Node** position);

// 00497CE0. Makes sure the texture in `holder` is loaded (00500970) and binds it to `stage`
// through the renderer (slot 11). __thiscall on the renderer.
int __fastcall BindParticleTexture(void* renderer, void*, int stage, void** holder);

// 00497EB0 (particle_emitters.cpp). Room for `count` elements, 32-byte aligned, the allocated block
// stored just below; __fastcall.
ParticleElement* __fastcall AllocateElements(int count);

// The world's rain/snow layer (CWorldClient + 0x6F4, CWorldClient::RainSnow(); an engine object
// whose slot 3 is the thunk 005617B0 -> 004E3B80). Only the fields its rewritten members use.
class CRainSnow : public CEngineObject {
public:
    // Body of the slot at 004E5680 (registered by member pointer).
    int DrawImpl(CSceneRenderer* renderer);
    static constexpr std::uintptr_t kRainSnowVtable = std::uintptr_t(0x0073F4C8);

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

    CEngineObject* texture;         // +0x01C counted texture ("RainSnow")
    class CWorld* world;            // +0x020 the client world (set after construction)
    int sound;                      // +0x024 rain loop handle in the sound manager (0: none)
    // The draws' work arrays.
    lib::AlignedArray<Vector4> projected;       // +0x028 the positions projected (four per packet)
    lib::AlignedArray<VertexPacket> positions;  // +0x038 the particles' corners (w not written)
    mfc::CArray<std::uint8_t> outcodes;         // +0x048 CByteArray: a clip outcode per position
    mfc::CArray<std::uint8_t> frames;           // +0x05C CByteArray: a texture frame (0..3) per particle
    lib::AlignedArray<TLVertex> vertices;       // +0x070 what is drawn (colours never written)
    mfc::CArray<std::uint16_t> indices;         // +0x080 CWordArray: 0, 1, 2, ... (filled elsewhere)
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
inline FogTile* FogTiles() { return reinterpret_cast<FogTile*>(0x007ABA58); }

// The squares currently seen: rows of bytes, nonzero = visible.
struct FogGrid {
    // World-side members (ai_fog_grid.cpp).
    void Clear();                         // 005C8B60: no origin (0x7FFFFFFF), 1 x 1, zeroed
    // 005C8BF0 / 005C8CC0 (RET 4): x0, y0, width, height (dwords), then the cells; reading makes
    // the cells again when the size changed.
    void Read(CMemoryStream* stream);
    void Write(CMemoryStream* stream);

    int x0;                         // +0x00
    int y0;                         // +0x04
    std::uint8_t* cells;            // +0x08 width * height bytes (operator new)
    std::uint8_t** rows;            // +0x0C one pointer per row into cells (operator new)
    int width;                      // +0x10
    int height;                     // +0x14
};
static_assert(sizeof(FogGrid) == 0x18);

class CVisibilityFog {
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
    void* World();                                  // 00455950 (CEngineObject)

    void* vtable_;                  // +0x00
    int refCount_;                  // +0x04
    std::uint8_t unused_08[0x1C - 0x08];
    int boundsKind;                 // +0x1C 3 once computed
    Vector3 center;                 // +0x20
    float radius;                   // +0x2C
    Vector3 boundsMin;              // +0x30 relative to center
    Vector3 boundsMax;              // +0x3C
    Vector3 boundsSpare;            // +0x48 zeroed with the bounds, never read
    CellMap cells;                  // +0x54
    std::uint8_t boundsValid;       // +0x68
    std::uint8_t unused_69[3];
};
static_assert(offsetof(CVisibilityFog, cells) == 0x54);
// The fog object's slots 2 / 3 (004E23D0: ClearCells, returns 1; 004E2440: ClearCells) and 11 / 12
// (004E24A0 "CVisibilityFog", 004E24B0 0x5F); effects_weather.cpp.
int __fastcall FogCreate(CVisibilityFog* self);
void __fastcall FogDestroy(CVisibilityFog* self);
const char* __fastcall FogClassName(CVisibilityFog* self);
int __fastcall FogClassId(CVisibilityFog* self);
static_assert(sizeof(CVisibilityFog) == 0x6C);
