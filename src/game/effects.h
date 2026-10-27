#pragma once

// Weather, visibility fog and particles: the per-frame simulation parts.
// - CParticleSource ("CParticle"): one emitter. Its tick runs the per-type callbacks the
// constructor stores (emit one particle, update one particle: 0 kills it, update
// one element of the source's own array).
// - CRainSnow (the world's rain/snow layer; "RainSnow"): fades its density and the rain sound.
// - CVisibilityFog ("CVisibilityFog", class id 0x5F): a hash_map of fog cells, one per visible map square; each
// cell fades in while visible and out once not, and is dropped when nearly transparent.

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

#undef GetClassName

class CMemoryStream;
class CSceneRenderer;
struct TLVertex;
struct VertexPacket;

// One particle (0x64 bytes, allocated by the source's pool). The source's tick saves the "prev" fields before
// calling the update callback and restores them when it returns 0. The meaning of +0x20..+0x40 depends on the
// source type; the comments name the types written here.
struct Particle {
    Vector3 pos;
    float size;
    Vector3 prevPos;
    float prevSize;
    Vector3 velocity;           // smoke: initial offset; spheres: orbit offset
    float aux0;                 // per-type scratch the emitter and update callbacks share (angles, speeds, growth, steps)
    float aux1;
    float aux2;
    Vector3 drift;              // smoke: rise in z; debris: speed; spheres: x = size factor
    int frame;                  // texture frame
    int prevFrame;
    int frameTimer;             // ticks until the next frame
    int life;                   // ticks left
    int state;                  // debris: landed
    std::uint32_t color;        // ARGB
    std::uint32_t prevColor;
    int dead;                   // set by the tick when the update returned 0

    // The emitters' Vector4::FromPoint on the particle's first 16 bytes: the position `at` and the size 1.
    void PlaceAt(const Vector3& at) {
        pos = at;
        size = 1.0f;
    }
};
static_assert(sizeof(Particle) == 0x64);

// One element of a source's own array (0x38 bytes, 32-byte aligned). What the slots hold depends on the
// source type (the views below); code indexes element 0 and reads element 1 through it.
struct ParticleElement {
    Vector3 pos;
    union {
        float value[10];
        std::int32_t word[10];  // the same slots as dwords (the set-ups clear some the type never reads)
        // Lightning (EmitLightning, particle_emitters.cpp): a point of the bolt, on the ground;
        // LightningBlast's heads (TickLightning) move by fork and remember their position in base.
        struct {
            Vector3 fork;
            Vector3 base;       // pos as placed
            std::uint8_t unused_24[0xC];
            int emitted;        // point 0: particles emitted along the bolt
        } bolt;
        // Modifier1-12 (EmitModifier): a point of the ring around the carrier.
        struct {
            Vector3 start;      // pos as placed (TickModifier: the last position)
            Vector3 offset;     // from the emission point
        } ring;
        // The ground plane under the source (TickGroundPlane, PlatformSet::SurfacePlane).
        struct {
            float height;
            float unused_10[2];
            Vector3 normal;     // its z replaced by its reciprocal when not 0
            std::uint8_t unused_24[0xC];
            std::uint32_t color;    // the ground dust's colour (0x203A / 0x203B)
        } plane;
        // FeetCloud (TickFeetCloud): a foot; ticks says which (nonzero: the left).
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // the carrier's radius (AttachmentOffset)
            float unused_1C[5];
            std::int32_t countdown;   // reset every tick; the cloud starts at the foot while >= 0
        } foot;
        // Casting (0x2027-0x202A): element 0 the effect's state (pos.x 1 once set up, ticks the
        // countdown the alpha follows), elements 1.. the swirls.
        struct {
            float growth;       // the particles' growth per tick
            float unused_10[2];
            float radius;
            float size;         // the particles' size
            float unused_20[4];
            std::int32_t alpha;
        } cast;
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 position;    // drifting by velocity
            Vector3 velocity;
        } swirl;
        // Protection spheres and AntiMagic (0x2015-0x2017, 0x201A): element 0 (pos.z the height
        // above the carrier); element 1's pos is the carrier's smoothed motion.
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // the sphere's
            float unused_1C;
            float speed;        // the carrier's (0 below 0.01)
        } sphere;
        struct {
            std::uint8_t unused_0C[0xC];
            float tiltCos;      // element 1: the tilt's cosine and sine (0.01 radians)
            float tiltSin;
        } sphereTilt;
        // Casting (0x200B) and Healing (0x2006): a ring of flames (pos its centre, relative to
        // the source; ticks its countdown).
        struct {
            float radius;
            float angle;        // of its leading flame
            float rise;         // per tick
            Vector3 centre;      // its starting centre
        } flames;
        // CastingIllusion ... Healing (0x202B-0x202E): an orbiting light (pos its world position).
        struct {
            float turnCos;      // the cosine and sine of its turn per tick
            float turnSin;
            float scale;        // the carrier's radius (times -1 for 0x202C)
            Vector3 relative;    // its position relative to the source (element 0's x: the orbit)
            Vector3 previous;    // its last world position
        } orbiter;
        // Link (0x2014): element 0 the beam's length and its inverse (ticks the alpha cap);
        // elements 1-3 the beam's frame (direction, side, their cross product).
        struct {
            std::uint8_t unused_0C[0xC];
            float length;
            float inverseLength;
        } link;
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 axis;        // a unit vector
        } linkAxis;
        // FireBlast (0x2002): a fragment flying out (pos its position).
        struct {
            Vector3 velocity;
            float radius;       // the flame radius
            float growth;       // its growth per tick
            float decay;        // the growth's factor per tick
            float frame;        // a random frame (0..5)
            float sizeFactor;   // 0 or 1
            float unused_2C;
            std::int32_t age;   // in ticks
        } fragment;
        // Geyser / Fireworks (0x2009): a vent (pos the jet's angles and strength, ticks the
        // eruption's remaining ticks).
        struct {
            float unused_0C[9];
            std::int32_t pause; // before the next eruption
        } vent;
        // Shower (0x200F, 0x2031-0x2033): a direction (pos); element 0's ticks is the texture frame.
        struct {
            float unused_0C[9];
            std::int32_t age;   // in ticks; element 0: negative until the directions are chosen
        } ray;
        // Bag (0x203C): a ball (pos its position, ticks its countdown).
        struct {
            Vector3 step;        // its last step
            Vector3 velocity;
            std::uint8_t unused_24[0xC];
            std::int32_t age;   // its kind, then its age (indexes the sizes)
        } ball;
        // 0x202F: the ring around the carrier (ticks its age).
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;
            float unused_1C;
            float height;       // the carrier's
            float unused_24[2];
            float foot;         // the ring's offset below the carrier's centre
        } column;
        // Mushroom and Transform (0x200E, 0x2052): pos.x the puffs' spread, pos.z the cap's height.
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 stem;        // the stem's foot
        } mushroom;
        // BlueGas (0x2013): pos the carrier's figure offset.
        struct {
            std::uint8_t unused_0C[0xC];
            float radius;       // the gas radius
            float unused_1C;
            float height;       // the carrier's
        } gas;
        // Tornado and CurseHolder (0x200A, 0x2050): a ring of the funnel (pos its centre); element 0
        // also holds the funnel's own values.
        struct {
            float unused_0C[6];
            float height;       // the funnel's
            float bend;         // 0: the rings sway with the source's motion, else they lean
            float damping;      // the particles' spin damping
        } funnel;
        // Cylinder1 / Cylinder2 (0x203F, 0x2040): a star (pos relative to the source, ticks its countdown).
        struct {
            Vector3 step;        // its last step
            float rotation[2];  // the cosine and sine of its turn per tick
            float unused_20;
            float inverseOrbit;
            float orbit;        // the orbit radius
            float rise;         // per tick
        } star;
        // Banshee (0x200D): pos.z the ring's height, ticks the countdown.
        struct {
            float radius;       // easing towards 30 times the source's height
        } banshee;
        // ClayRing (0x2018): a point on the ground (pos; ticks the countdown).
        struct {
            Vector3 previous;    // its previous position
            Vector3 direction;   // turning a little each tick
        } clay;
        // Falling (0x2034-0x2037): a gust.
        struct {
            float reach;
            float direction;    // radians, truncated
            float unused_14[7];
            std::int32_t age;
        } wind;
        // StartTrans (0x2051): element 0 (ticks the countdown).
        struct {
            std::uint8_t unused_0C[0xC];
            float scale;        // the glow's size, growing with the source's
            float count;        // the emitted count, rising by 0.4 per tick
        } trans;
        // ShapeChange glow (0x2039): element 0.
        struct {
            std::uint8_t unused_0C[0xC];
            Vector3 mark;        // the last point glowed at
            std::uint8_t unused_24[0xC];
            std::uint32_t color;    // by the alpha field
        } glow;
    };
    int ticks;                  // (lightning point 0: bursts; modifier point 0: the fade step)
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
class CTexture;

// The particle manager (a real virtual class; each CParticleSource points to it): a CBaseManager whose
// objects are the sources (objectList_). It keeps a pool of free particles, an MFC list (taken from the head,
// returned to the tail), and holds one reference on every source made through CreateSource (`held_`, by id)
// until the source's tick ends it (TickSources) or nobody else holds it (ReleaseUnheld). The particle
// manager's own part of its constructor (small_accessors.cpp; the client world's manager set-up calls it
// after the base manager's): quietStart cleared.
class CParticleManager;
void __fastcall ParticleManagerClearQuietStart(CParticleManager* manager);

class CParticleManager : public CBaseManager {
public:
    using HeldMap = stlp::hash_map<int, CParticleSource*, stlp::IntHash>;

    CParticleManager();
    ~CParticleManager() override;
    const char* GetClassName() override;
    int GetClassId() override;                     // class id 0x2C
    CEngineObject* NewObject(int type) override;

    // QuietStart cleared, then CBaseManager::Create; returns 1 / 0.
    int Create() override;
    // Releases the held sources, empties `held_`, deletes the pooled particles, then
    // CBaseManager::Destroy; quietStart cleared.
    void Destroy() override;

    Particle* AcquireParticle();                    // from the pool, else new (0x64 bytes)
    void ReleaseParticle(Particle* p);              // back to the pool's tail
    int DrawAll(void* renderer);
    // A new source of `type` (CreateObject(type, -1)), referenced and held under
    // its id; with quietStart set, types 0x2001 / 0x2003 / 0x2004 start without emitting.
    CParticleSource* CreateSource(int type);
    // Every source's tick (with `paused` only the ones with flag 8); a held source whose tick returned 0 is released and
    // leaves `held_`. Returns 0.
    int TickSources(char paused);
    // Every held source nobody else references (refCount_ 1) is released and leaves
    // `held_` (the client's zone load).
    void ReleaseUnheld();

    mfc::CTypedPtrList<Particle> freeParticles;
    HeldMap held_;                                  // id -> source holding a reference
    int maxHeldId_;                                 // highest key of held_
    int quietStart;                                 // see CreateSource; never set non-zero in src
};
static_assert(offsetof(CParticleManager, freeParticles) == 0xB0);
static_assert(offsetof(CParticleManager, held_) == 0xCC);
static_assert(offsetof(CParticleManager, maxHeldId_) == 0xE0);
static_assert(sizeof(CParticleManager) == 0xE8);

// One entry of the draw order: the source's depth along the camera's view axis.
struct ParticleDepth {
    float depth;
    CParticleSource* source;
};

// The draw order, an MFC CArray<ParticleDepth> (vtable, data, size, max, grow by).
using ParticleDepthArray = mfc::CArray<ParticleDepth>;

// The draw order's sort: STLport's introsort on [first, last) with "greater depth first" (far sources first).
void __fastcall IntroSortByDepth(ParticleDepth* first, ParticleDepth* last, void* tag, int depthLimit,
                                 int compare);
void __fastcall FinalSortByDepth(ParticleDepth* first, ParticleDepth* last, int compare);
void __fastcall MakeHeapByDepth(ParticleDepth* first, ParticleDepth* last, void* tag, int compare,
                                void* distanceTag);
void __fastcall PopHeapByDepth(ParticleDepth* first, ParticleDepth* last, ParticleDepth* result,
                               ParticleDepth value, int compare, void* distanceTag);

// One texture frame's UV rectangle (a particle source's frames, 16 bytes each).
struct SourceFrame {
    float u0, v0, u1, v1;
};
static_assert(sizeof(SourceFrame) == 16);

// A particle source: a transform object (CTransformObject, whose vtable the factory stores on the
// way): localPosition_ is where it emits relative to what it is attached to, localRotation_
// the identity after Reset.
class CParticleSource final : public CTransformObject {
public:
    // The construction (particle_create.cpp; NewParticleSource, the class factory's creator): the engine object and
    // transform bases (identity rotations), no elements, no bounds, an empty particle list (blocks of 10), no frames or
    // texture; then Reset and `type`.
    explicit CParticleSource(int type);
    // Deleting destructor: Clear, the texture released, the frames, the particle list and the elements freed.
    ~CParticleSource() override;
    // Sources start zeroed: nothing here or in Reset sets the emission point (origin) and the first Tick takes the
    // motion from it. The harness zeroes the game's sources alike.
    static void* operator new(std::size_t size) noexcept;

    int Create() override;                          // per-type parameters
    void Destroy() override { Clear(); }
    int RunTick() override;                         // 1 while the source stays alive
    const char* GetClassName() override;
    int GetClassId() override;                      // class id 12

    // The particle manager that owns the source (owner_), the pool its particles come from.
    CParticleManager* Manager() const { return static_cast<CParticleManager*>(owner_); }
    // Fills the shared vertex / sprite arrays and draws through the renderer. 1 when there is
    // nothing to draw or it is off screen.
    int Draw(void* renderer);

    // --- set-up ---
    void Reset();                                   // defaults, texture and elements dropped
    void Clear();                                   // particles back to the pool, detach, Reset

    // --- per-frame tick ---
    void AddParticle(Particle* p);                  // prev = current, bounds, list tail

    // The callbacks Create stores (emit, update, element): members below. Emit fills a
    // particle from the pool (0 returns it), update moves one (0 drops it).
    using EmitCallback = int (CParticleSource::*)(Particle* p, int index);
    using UpdateCallback = int (CParticleSource::*)(Particle* p);
    using ElementCallback = void (CParticleSource::*)(ParticleElement* e);

    // --- per-particle callbacks, return 0 to drop ---
    int UpdateFalling(Particle* p);                 // types 0x2034-0x2037
    int UpdateCloud(Particle* p);                   // PoisonFog and similar
    int UpdateSmoke(Particle* p);                   // CampFire, Fire, Smoke, 0x2010
    int EmitSmoke(Particle* p, int index);
    int UpdateDebris(Particle* p);                  // Nuke
    int UpdateOrbit(Particle* p);                   // Antimagic, spheres, 0x2014

    int EmitLightning(Particle* p, int index);      // LightningBlast
    int EmitModifier(Particle* p, int index);       // Modifier1-12
    int EmitZoneExit(Particle* p, int index);       // ZoneExit
    int EmitFeebleMind(Particle* p, int index);     // FeebleMind
    int EmitFeetCloud(Particle* p, int index);      // FeetCloud1/2
    int UpdateGlow(Particle* p);                    // 0x2039
    int UpdateGroundDust(Particle* p);              // 0x203A, 0x203B
    int UpdatePortal(Particle* p);                  // Portal
    int UpdateSparks(Particle* p);                  // Sparks
    void TickGroundPlane(ParticleElement* e);       // 0x203A, 0x203B
    void TickPortal(ParticleElement* e);            // Portal
    void TickStartTrans(ParticleElement* e);        // StartTrans

    // The emission point in the world (attachment point plus the local position).
    Vector3* WorldPosition(Vector3* out);

    int EmitCasting(Particle* p, int index);        // 0x2027-0x202A
    int UpdateCasting(Particle* p);
    void TickCasting(ParticleElement* e);
    int EmitOrbit(Particle* p, int index);          // 0x2015-0x2017, 0x201A
    int EmitCastRing(Particle* p, int index);       // Casting, 0x200B
    int UpdateCastRing(Particle* p);
    void TickCastRing(ParticleElement* e);
    int EmitHealing(Particle* p, int index);        // Healing, 0x2006
    int UpdateHealing(Particle* p);
    void TickHealing(ParticleElement* e);
    int EmitCastSwirl(Particle* p, int index);      // 0x202B-0x202E
    int UpdateCastSwirl(Particle* p);
    void TickCastSwirl(ParticleElement* e);
    int EmitLink(Particle* p, int index);           // Link, 0x2014
    int UpdateLink(Particle* p);
    void TickLink(ParticleElement* e);
    int EmitBlast(Particle* p, int index);          // FireBlast, 0x2002
    int UpdateBlast(Particle* p);
    void TickBlast(ParticleElement* e);
    int EmitPoisonFog(Particle* p, int index);      // PoisonFog, 0x2007 / 0x2008
    int EmitGeyser(Particle* p, int index);         // Geyser, 0x2009
    int UpdateGeyser(Particle* p);
    void TickGeyser(ParticleElement* e);
    int EmitShower(Particle* p, int index);         // 0x200F, 0x2031-0x2033
    int UpdateShower(Particle* p);
    void TickShower(ParticleElement* e);
    int EmitBag(Particle* p, int index);            // Bag, 0x203C
    int UpdateBag(Particle* p);
    void TickBag(ParticleElement* e);
    int EmitColumn(Particle* p, int index);         // 0x202F
    int UpdateColumn(Particle* p);
    void TickColumn(ParticleElement* e);
    int EmitMushroom(Particle* p, int index);       // Mushroom, Transform
    int UpdateMushroom(Particle* p);
    int EmitStench(Particle* p, int index);         // BlueGas, 0x2013
    int UpdateStench(Particle* p);
    int EmitTornado(Particle* p, int index);        // Tornado, CurseHolder
    int UpdateTornado(Particle* p);
    void TickTornado(ParticleElement* e);
    int EmitPortalStar(Particle* p, int index);     // PortalStar, 0x203D
    int UpdatePortalStar(Particle* p);
    int EmitCylinder(Particle* p, int index);       // Cylinder1/2, 0x203F / 0x2040
    int UpdateCylinder(Particle* p);
    void TickCylinder(ParticleElement* e);
    int EmitFireBall(Particle* p, int index);       // FireBall, 0x2000
    int UpdateFireBall(Particle* p);
    int EmitArrowSmoke(Particle* p, int index);     // FireArrow 0x2011, 0x204E
    int UpdateArrowSmoke(Particle* p);
    int EmitBanshee(Particle* p, int index);        // Banshee, 0x200D
    int UpdateBanshee(Particle* p);
    void TickBanshee(ParticleElement* e);
    int EmitPortal(Particle* p, int index);         // Portal, 0x203E
    int EmitAcidRay(Particle* p, int index);        // AcidRay, 0x2012
    int UpdateAcidRay(Particle* p);
    int EmitSparks(Particle* p, int index);         // Sparks, 0x2043
    int EmitClayRing(Particle* p, int index);       // ClayRing, 0x2018
    int UpdateClayRing(Particle* p);
    void TickClayRing(ParticleElement* e);
    int EmitFalling(Particle* p, int index);        // 0x2034-0x2037
    void TickFalling(ParticleElement* e);
    int EmitVulkan(Particle* p, int index);         // VulcanSmoke, 0x2005
    int UpdateVulkan(Particle* p);
    int EmitFireStar(Particle* p, int index);       // FireStar, AcidStar
    int UpdateFireStar(Particle* p);
    int EmitRegeneration(Particle* p, int index);   // Regeneration
    int UpdateRegeneration(Particle* p);
    int EmitVisionStar(Particle* p, int index);     // VisionStar1-3
    int UpdateVisionStar(Particle* p);
    int UpdateZoneExit(Particle* p);                // ZoneExit
    int UpdateFeebleMind(Particle* p);              // FeebleMind, 0x2049
    int UpdateFeetCloud(Particle* p);               // FeetCloud, 0x2047
    void TickFeetCloud(ParticleElement* e);
    int EmitSilence(Particle* p, int index);        // Silence, 0x2048
    int UpdateSilence(Particle* p);
    int UpdateModifier(Particle* p);                // Modifier1-12
    void TickModifier(ParticleElement* e);
    int UpdateLightning(Particle* p);               // LightningBlast, 0x2030
    void TickLightning(ParticleElement* e);
    int EmitTeleport(Particle* p, int index);       // Teleport, 0x2019
    int UpdateTeleport(Particle* p);
    void TickTeleport(ParticleElement* e);
    int EmitCurseStars(Particle* p, int index);     // CurseStars
    int UpdateCurseStars(Particle* p);
    int EmitStartTrans(Particle* p, int index);     // StartTrans
    int UpdateStartTrans(Particle* p);
    int EmitNuke(Particle* p, int index);           // Nuke
    int EmitTendril(Particle* p, int index);
    int EmitGroundDust(Particle* p, int index);     // 0x203A, 0x203B
    int EmitGlow(Particle* p, int index);           // ShapeChange, 0x2039
    void TickGlow(ParticleElement* e);
    int EmitStun(Particle* p, int index);           // Stun, 0x204D
    int UpdateStun(Particle* p);

    // Offset (0, 0, the bounds centre's z) and radius of the object followed
    // (its bounds recomputed); (0, 0, 0) and 0.1 when there is none. Returns `out`.
    Vector3* AttachmentOffset(Vector3* out, float* radius);

    // --- per-element callbacks ---
    void TickNuke(ParticleElement* e);
    void TickTendril(ParticleElement* e);           // 0x2010
    void TickMushroom(ParticleElement* e);          // Mushroom, Transform
    void TickNone(ParticleElement* e);              // does nothing; ten more callbacks are as empty

    // --- small setters the game calls on sources ---
    void ScaleSize(float factor);
    void StopEmitting();                            // flags bit 0 off, bit 2 on
    void StartEmitting();                           // flags bits 0 and 2 on
    void Stop();                                    // bits 0 and 2 off, detach

    // Follow `object` (a CObject3D, or null to let go): the old object
    // drops the source (its point is folded into the local position), the new one adds it, and
    // the emission point becomes the new attachment point (or the local position).
    void Attach(CObject3D* object);
    // Where the source sits on `object` (a CObject3D): with flag 0x10 a unit's part chosen by
    // attachPoint (0 the effect point, 1-7 head / hands / legs / body, 8-12 body00-04), else the
    // top of its bounds; without flag 0x10 its position. Returns `out`.
    Vector3* AttachmentPoint(Vector3* out, CObject3D* object);
    // The world position of `object`'s first part named `prefix...`, else of its "bd" part, else
    // its position raised by the bounds' upper z extent. Returns `out` (`this` is not used).
    Vector3* PartPoint(Vector3* out, CObject3D* object, const char* prefix);
    // Every particle back to the pool, emission off (flags 0 and 2), detached.
    void StopAndClear();
    // The loop Clear and StopAndClear share: every particle unlinked and returned to the pool.
    void ReleaseAllParticles();

    // CEngineObject's flags_: bit 0 emitting, bit 1 attached-only, bit 2 keep alive; type_ 0x2000.. (the effect names in
    // the type name table); owner_ the particle manager.
    std::uint8_t unused_054[0x94 - 0x54];
    CObject3D* attachedTo;          // the object followed, null: none
    Vector3 origin;                 // world emission point
    Vector3 motion;                 // movement since the last tick
    Vector3 side;                   // unit, motion x (0, 0, 1) (zero while still)
    Vector3 up;                     // unit, motion x side
    float distance;                 // length of motion
    int emitChance;                 // percent per try
    int emitCount;                  // tries per tick
    float emitSpacing;
    int maxParticles;
    int frameCount;
    int frameColumns;               // columns of the texture's frame grid
    int frameDelay;                 // (Tornado, CurseHolder: the particle colour)
    int hiddenTicks;                // draws skipped off screen (the tick idles past 0x38 with flag 0x20)
    int lifetime;
    Vector3 spreadMin;
    Vector3 spreadMax;
    float unused_108;
    float size;
    float height;
    float growth;
    float alpha;
    float typeParam;                // per type: rate, count or size (1.0 after Reset)
    float turnCos;                  // \ cos / sin of the emitter's turn (a zone exit's
    float turnSin;                  // / angle)
    float baseSize;
    float emitState;                // int bits: emitted count or a one-shot flag, per type
    Vector3 extent;
    int attachPoint;
    Vector3 windDirection;          // copied from the world every tick
    float windStrength;
    float additiveBlend;            // int bits: 1 draws with additive blending
    ParticleElement* elements;
    int elementCount;
    int elementCapacity;            // (elements is an MFC-style array: data, size, max, grow by)
    int elementGrowBy;
    Vector3 boundsMin;              // over the particles, rebuilt every tick
    Vector3 boundsMax;
    // the bounds the renderer clips by: flags bit 1 set by the tick, the box (boundsMin and
    // boundsMax less the centre) relative to the centre; the radius is not written.
    ClipBounds clip;
    std::uint8_t unused_1A8[0x1B4 - 0x1A8];
    mfc::CTypedPtrList<Particle> particles;
    // the frame UV rectangles (32-byte aligned).
    lib::AlignedArray<SourceFrame> frames;
    EmitCallback emitFn;
    UpdateCallback updateFn;
    ElementCallback elementFn;
    HeldTexture texture;            // texture holder
};

// The class factory's creator for particle sources: a new source of
// `type` (the register arguments are unused), or null.
CParticleSource* __stdcall NewParticleSource(int type);

// `out` = v / d, all three quotients computed before the first store; returns out.
Vector3* __fastcall DivideVector(Vector3* out, const Vector3& v, float d);

// The particle renderer's shared arrays. One vertex (position and size,
// interpolated between the last two ticks) and one sprite (colour, texture frame) per particle.
struct ParticleVertex {
    float x, y, z, size;
};
struct ParticleSprite {
    std::uint32_t color;            // ARGB
    const SourceFrame* frame;       // the particle's frame among its source's frames
};
using ParticleVertexArray = lib::AlignedArray<ParticleVertex>;
using ParticleSpriteArray = mfc::CArray<ParticleSprite>;

// (The list position's particle with the position advanced is the list's GetNext.)

// (The renderer's bind of a held texture is CSceneRenderer::BindHeldTexture, model.h.)

// Room for `count` elements, 32-byte aligned, the allocated block stored just below;
ParticleElement* __fastcall AllocateElements(int count);

// The world's rain/snow layer (an engine object). Only the fields its rewritten members use.
class CRainSnow final : public CEngineObject {
public:
    CRainSnow();
    // Deleting destructor: Clear, the work arrays freed (last member first), the texture released.
    ~CRainSnow() override;
    int Create() override { return 0; }
    void Destroy() override { Clear(); }
    const char* GetClassName() override;            // "CWeather" (nothing asks)
    int GetClassId() override;                      // class id 0x1D

    int Draw(CSceneRenderer* renderer);
    void UpdateFade(float now);
    void FinishFade();
    void SetWind(const Vector3& direction, float strength);
    // The rain loop stopped, the texture released, wind, mode, fade and world cleared, density 64.
    void Clear();
    // The drawing code's work arrays sized and their fixed parts filled (effects_weather_draw.cpp; the client world's
    // setup).
    void PrepareBuffers();
    // The texture "RainSnow" (kind 6) made sure of; a running loop stopped unless `newMode` is 0; rain starts the looping
    // "nature\rain\circle.wav"; the fade towards `newMode` starts now (the world's tick) over `duration` (0: at the next
    // step). Called by the weather messages.
    void Start(int newMode, float duration);
    // Rain and snow; the layer's draw calls the one of the mode. The particles
    // of the 81 x 81 grid cells around the point the wind carries them to the camera from, each cell's place and time
    // hashed, `density` of 64 of them shown: a rain drop is a thin triangle along the wind, a snowflake a point swayed
    // by the five sway tables that becomes a small triangle on screen. Projected with the pipeline's stack 2 and drawn
    // as pre-transformed triangles through `indices` with the device's current state.
    int DrawRain(CSceneRenderer* renderer);
    int DrawSnow(CSceneRenderer* renderer);
    // Their two halves: the vertices of the particles on screen built (returns their count, three per particle), and
    // those drawn (the distance fog when the device has it on, DrawIndexedPrimitive under the device lock, the frame's
    // counts).
    int BuildRain(CSceneRenderer* renderer);
    int BuildSnow(CSceneRenderer* renderer);
    void DrawParticles(CSceneRenderer* renderer, int vertexCount);

    HeldTexture texture;            // counted texture ("RainSnow")
    class CWorldClient* world;      // the client world (set after construction)
    int sound;                      // rain loop handle in the sound manager (0: none)
    // The draws' work arrays.
    lib::AlignedArray<Vector4> projected;       // the positions projected (four per packet)
    lib::AlignedArray<VertexPacket> positions;  // the particles' corners (w not written)
    mfc::impl::CByteArray outcodes;             // a clip outcode per position
    mfc::impl::CByteArray frames;               // a texture frame (0..3) per particle
    lib::AlignedArray<TLVertex> vertices;       // what is drawn (colours never written)
    mfc::impl::CWordArray indices;              // 0, 1, 2, ... (filled elsewhere)
    lib::AlignedArray<int> offsets;             // the grid's 81 cell offsets (filled elsewhere)
    int particleCount;              // positions built by the last draw
    Vector3 wind;
    int mode;                       // 0 off, 1 rain, 2 snow
    int targetMode;
    int density;                    // 0..64
    float fadeStart;                // world time
    float fadeDuration;             // 0: no fade running
};

// A fog cell (the hash_map value; the key is (y << 16) + x of the map square).
struct FogCell {
    std::int16_t x;                 // map square
    std::int16_t y;
    Vector3 pos;                    // jittered centre, z = ground + 0.5
    float alpha;
    std::uint8_t visible;
    std::int8_t frame;              // 0..14, picks the texture tile
    std::uint8_t unused_16[2];
};
static_assert(sizeof(FogCell) == 0x18);

// The texture coordinates of the fog texture's 4 x 4 tiles (16 records; the fog manager's Create fills them,
// the fog's draw hands them to the renderer): a tile's u range, and its v range counted down from 1 a row.
struct FogTile {
    float u0;   // column / 4
    float v1;   // v0 + 1/4
    float u1;   // u0 + 1/4
    float v0;   // 1 - (row + 1) / 4
};
inline FogTile g_fogTiles[16] = /*007ABA58*/ {};

// The squares currently seen: rows of bytes, nonzero = visible.
struct FogGrid {
    // World-side members.
    void Clear();                         // no origin (0x7FFFFFFF), 1 x 1, zeroed
    // x0, y0, width, height (dwords), then the cells; reading makes the cells again when the size changed.
    void Read(CMemoryStream* stream);
    void Write(CMemoryStream* stream);
    // CAIMap::MarkVisibleCells. Grows the grid to cover (x - n .. x + n, y - n .. y + n); the first
    // call (x0 = 0x7FFFFFFF) sizes and clears it.
    void Grow(int x, int y, int n);

    int x0;                         // map cell of cells[0][0]; 0x7FFFFFFF before the first Grow
    int y0;
    SearchGrid<std::uint8_t> cells; // width * height bytes and the row table (operator new)
};
static_assert(sizeof(FogGrid) == 0x18);

// The fog object (made by the fog manager's NewObject, vis_fog_manager.cpp).
class CVisibilityFog final : public CEngineObject {
public:
    using CellMap = stlp::hash_map<std::uint32_t, FogCell, stlp::IntHash>;

    CVisibilityFog();
    ~CVisibilityFog() override;
    int Create() override;                          // ClearCells, 1
    void Destroy() override { ClearCells(); }
    int RunTick() override { return UpdateCells(); }
    const char* GetClassName() override;
    int GetClassId() override;                      // class id 0x5F

    void ComputeBounds();
    int UpdateCells();
    void Reveal(const FogGrid* grid);
    //. Every cell freed (nodes back to the pool), the bounds invalid: also the body of two of the
    // object's virtuals (one returns 1).
    void ClearCells();
    // The cells as sprites through the scene renderer (effects_weather_draw.cpp; its result); 1 without cells or when
    // the bounds are out of view.
    int Draw(class CSceneRenderer* renderer);

    CWorld* World();

    ClipBounds bounds;              // flags 3 once computed; the box relative to the centre
    Vector3 boundsSpare;            // zeroed with the bounds, never read
    CellMap cells;
    std::uint8_t boundsValid;
    std::uint8_t unused_69[3];
};

// The size of an orbiting sphere's particle by the ticks it has left (the fire, electricity and air spheres'
// 16 entries; the antimagic sphere's 6): effects.cpp, particle_callbacks.cpp.
inline constexpr float kSphereSizeByLife[16] = /*0073CB38*/ {
    0.01f, 0.02f, 0.03f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.03f,
    0.02f, 0.01f};
inline constexpr float kAntimagicSizeByLife[6] = /*0073CB78*/ {0.01f, 0.015f, 0.02f, 0.03f, 0.015f, 0.01f};
