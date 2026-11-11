#pragma once

// The particle core's sources (docs/notes/effects.md, "Particle sources"): the particle types, a particle, the particle
// manager and its sources (ParticleSource, "CParticle": one emitter; its tick runs its effect's record, particle_data.h:
// the original types and the mods' effects alike). Part of the engine (docs/notes/file-layout.md, "The engine
// library"): what a source needs of the world it is in goes through particle_host.h.

#include "engine/core/base_manager.h"
#include "engine/core/core_transform.h"
#include "engine/scene/pipeline.h"   // ClipBounds, SpritePoint

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string_view>
#include <vector>

namespace effectdata {
class State;       // particle_record.h
}

// The particle source / effect types (EngineObject::type of a ParticleSource and a EffectParticle). The names
// are the original script table's and the texture its effect uses (assets/effects); a type neither names is
// described: Shower1-4 (the RainSnow texture, texture frames 12-15), Falling1-4 (a gust of RainSnow), GroundDust1 / 2,
// TeleportRing (the Teleport texture), VisionStar4 (the fourth vision star, lifetime 3), Stun (the Stun texture),
// PoisonCloud (the second PoisonFog type, the poison spell's cloud). type stays a plain int in the objects;
// ParticleTypeOf reads it.
enum class ParticleType : int {
    FireBall = 0x2000,
    CampFire = 0x2001,
    FireBlast = 0x2002,
    Fire = 0x2003,
    Smoke = 0x2004,
    VulcanSmoke = 0x2005,
    Healing = 0x2006,
    PoisonFog = 0x2007,
    PoisonCloud = 0x2008,
    Geyser = 0x2009,
    Tornado = 0x200A,
    Casting = 0x200B,
    Nuke = 0x200C,
    Banshee = 0x200D,
    Mushroom = 0x200E,
    Shower1 = 0x200F,
    Tendril = 0x2010,
    FireArrow = 0x2011,
    AcidRay = 0x2012,
    BlueGas = 0x2013,
    Link = 0x2014,
    SphereAcid = 0x2015,
    SphereElectricity = 0x2016,
    SphereFire = 0x2017,
    ClayRing = 0x2018,
    Teleport = 0x2019,
    Antimagic = 0x201A,
    Modifier1 = 0x201B,
    Modifier2 = 0x201C,
    Modifier3 = 0x201D,
    Modifier4 = 0x201E,
    Modifier5 = 0x201F,
    Modifier6 = 0x2020,
    Modifier7 = 0x2021,
    Modifier8 = 0x2022,
    Modifier9 = 0x2023,
    Modifier10 = 0x2024,
    Modifier11 = 0x2025,
    Modifier12 = 0x2026,
    CastingFire = 0x2027,
    CastingElectricity = 0x2028,
    CastingAcid = 0x2029,
    CastingDivination = 0x202A,
    CastingIllusion = 0x202B,
    CastingDomination = 0x202C,
    CastingEnchantment = 0x202D,
    CastingHealing = 0x202E,
    TeleportRing = 0x202F,
    LightningBlast = 0x2030,
    Shower2 = 0x2031,
    Shower3 = 0x2032,
    Shower4 = 0x2033,
    Falling1 = 0x2034,
    Falling2 = 0x2035,
    Falling3 = 0x2036,
    Falling4 = 0x2037,
    ZoneExit = 0x2038,
    ShapeChange = 0x2039,
    GroundDust1 = 0x203A,
    GroundDust2 = 0x203B,
    Bag = 0x203C,
    PortalStar = 0x203D,
    Portal = 0x203E,
    Cylinder1 = 0x203F,
    Cylinder2 = 0x2040,
    FireStar = 0x2041,
    AcidStar = 0x2042,
    Sparks = 0x2043,
    VisionStar1 = 0x2044,
    VisionStar2 = 0x2045,
    VisionStar3 = 0x2046,
    Regeneration = 0x2047,
    Silence = 0x2048,
    FeebleMind = 0x2049,
    FeetCloud1 = 0x204A,
    FeetCloud2 = 0x204B,
    VisionStar4 = 0x204C,
    Stun = 0x204D,
    RikArrowSmoke = 0x204E,
    CurseStars = 0x204F,
    CurseHolder = 0x2050,
    StartTrans = 0x2051,
    Transform = 0x2052,
};

inline ParticleType ParticleTypeOf(int type) { return static_cast<ParticleType>(type); }

// One particle (from the source's pool). The source's tick saves the "prev" fields before calling its update and
// restores them when it returns 0. A data effect's particle (particle_data.cpp) keeps its velocity, in `drift` its
// offset in source or anchor space, in `state` its anchor.
struct Particle {
    Vector3 pos;
    float size = 0.0f;
    Vector3 prevPos;
    float prevSize = 0.0f;
    Vector3 velocity;
    Vector3 drift;
    int frame = 0;              // texture frame
    int prevFrame = 0;
    int frameTimer = 0;         // ticks until the next frame
    int life = 0;               // ticks left
    int state = 0;              // its anchor (-1: none)
    std::uint32_t color = 0;    // ARGB
    std::uint32_t prevColor = 0;
    int dead = 0;               // set by the tick when the update returned 0
    // Its ticks since emission, its kind (the record's [[particle]]) and its vars.
    int age = 0;
    int kind = 0;
    float vars[8] = {};
};


class FigureInstance;
class ParticleSource;
class SceneRenderer;
class Texture;

// The particle manager: a BaseManager whose objects are the sources. It keeps a pool of free particles (taken
// from the head, returned to the tail), and holds one reference on every source made through CreateSource
// (`held`, by id) until the source's tick ends it (TickSources) or nobody else holds it (ReleaseUnheld).
class ParticleManager : public BaseManager {
public:
    // Kept order: Destroy releases the sources in it, which sends them to the spares and their textures' releases
    // to the texture manager's spare list in that order.
    using HeldMap = lib::HashMap<int, ParticleSource*>;

    ParticleManager() = default;
    ~ParticleManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    EngineObject* NewObject(int type) override;

    void Create() override;
    void Destroy() override;

    Particle* AcquireParticle();                    // from the pool, else new
    void ReleaseParticle(Particle* p);              // back to the pool's tail
    int DrawAll(SceneRenderer* renderer);
    // CreateObject(ShownParticleType(type), -1), referenced and held under its id; null when the type shows none. With
    // quietStart set, CampFire, Fire and Smoke start without emitting.
    ParticleSource* CreateSource(int type);
    // Every source's tick (with `paused` only the ones with ParticleSourceFlag::TicksWhilePaused; never one with
    // OwnClock); a held source whose tick returned 0 is released and leaves `held`. Returns 0.
    int TickSources(char paused);
    // Every held source nobody else references (refCount 1) is released and leaves `held` (the client's zone
    // load).
    void ReleaseUnheld();

    std::deque<Particle*> freeParticles;   // taken from the front, returned to the back
    HeldMap held;                                   // id -> source holding a reference
    int maxHeldId = 0;                              // highest key of held
    int quietStart = 0;                             // see CreateSource; never set non-zero in src
};

// One entry of the draw order: the source's depth along the camera's view axis.
struct ParticleDepth {
    float depth = 0.0f;
    ParticleSource* source = nullptr;
};

// One texture frame's UV rectangle.
struct SourceFrame {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

// The bits of EngineObject::flags on a particle source (the types' setups in particle_create.cpp set them).
enum class ParticleSourceFlag : std::uint32_t {
    Emitting = 0x1,
    AttachedOnly = 0x2,     // emits only while attached
    KeepAlive = 0x4,        // a detached source stays while it has it (else while it has particles)
    TicksWhilePaused = 0x8, // ticked while the game is paused
    OnPart = 0x10,          // sits on a unit's part chosen by attachPoint (else on the top of the object's bounds)
    IdlesOffScreen = 0x20,  // the tick idles once the draws were skipped off screen kIdleAfterHidden times
    NotDrawn = 0x40,        // the draw skips it; nothing sets it
    OwnClock = 0x80,        // ticked by what made it (the effect viewer, src/tools), never by TickSources
};
template <> inline constexpr bool lib::kBitFlags<ParticleSourceFlag> = true;

// Where a source with ParticleSourceFlag::OnPart sits on a unit (ParticleSource::AttachmentPoint): the unit's effect
// point or a part of its figure (the part's name; a figure without it gives "bd", then the top of its bounds).
enum class AttachPoint : std::uint8_t {
    EffectPoint,    // Unit::EffectPoint
    Head,           // "hd"
    AboveHead,      // "hd", 1 higher
    LeftHand,       // "lh2"
    RightHand,      // "rh2"
    LeftLeg,        // "ll2"
    RightLeg,       // "rl2"
    Body,           // "bd"
    Body00,         // "body00" ... "body04"
    Body01,
    Body02,
    Body03,
    Body04,
};
// The part a source on `point` sits on ("hd", "lh2" ...: the first part whose name starts so); null for EffectPoint.
const char* AttachPointPart(AttachPoint point);

// The bounds a tick starts from: empty (each minimum above its maximum).
inline constexpr float kBoundsFar = 100000.0f;

// The draws a source with ParticleSourceFlag::IdlesOffScreen skips off screen before its tick idles.
inline constexpr int kIdleAfterHidden = 57;

// A particle source: localPosition is where it emits relative to what it is attached to, localRotation the
// identity after Reset.
class ParticleSource final : public TransformObject {
public:
    explicit ParticleSource(int type);
    ~ParticleSource() override;
    void Create() override;                          // set up from its effect's record
    void Destroy() override { Clear(); }
    bool RunTick() override;                         // 1 while the source stays alive
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The particle manager that made the source (the client world's): its own teardown still reaches it there.
    ParticleManager* Manager() const;
    // Fills the shared vertex / sprite arrays and draws through the renderer. 1 when there is
    // nothing to draw or it is off screen.
    int Draw(SceneRenderer* renderer);

    // --- set-up ---
    void Reset();                                   // defaults, texture dropped
    void Clear();                                   // particles back to the pool, detach, Reset

    // --- per-frame tick ---
    void AddParticle(Particle* p);                  // prev = current, bounds, list tail

    // The emission point in the world (attachment point plus the local position).
    Vector3 WorldPosition();

    // --- small setters the game calls on sources ---
    void ScaleSize(float factor);
    void StopEmitting();                            // Emitting off, KeepAlive on
    void StartEmitting();                           // Emitting and KeepAlive on
    void Stop();                                    // Emitting and KeepAlive off, detach

    // Follow `object` (a figure: the game's map objects, the viewer's figure; null to let go): the old object
    // drops the source (its point is folded into the local position), the new one adds it, and
    // the emission point becomes the new attachment point (CarrierPoint).
    void Attach(FigureInstance* object);
    // Where the source sits on its carrier (not null): with ParticleSourceFlag::OnPart the host's point of the part
    // attachPoint names, else the carrier's position.
    Vector3 CarrierPoint() const;
    // Every particle back to the pool, emission off (Emitting and KeepAlive cleared), detached.
    void StopAndClear();
    void ReleaseAllParticles();

    // EngineObject's flags hold the ParticleSourceFlag bits, its type a ParticleType.
    FigureInstance* attachedTo = nullptr;  // the object followed, null: none
    Vector3 origin;                 // world emission point
    Vector3 motion;                 // movement since the last tick
    Vector3 side;                   // unit, motion x (0, 0, 1) (zero while still)
    Vector3 up;                     // unit, motion x side
    float distance = 0.0f;          // length of motion
    int emitChance = 0;             // percent per try
    int emitCount = 0;              // tries per tick
    float emitSpacing = 1000.0f;
    int maxParticles = 0;
    int frameCount = 1;
    int frameColumns = 1;           // columns of the texture's frame grid
    int hiddenTicks = 0;            // draws skipped off screen in a row (IdlesOffScreen)
    int lifetime = -1;
    Vector3 spreadMin;
    Vector3 spreadMax;
    float size = 1.0f;
    float height = 0.0f;
    float growth = 0.0f;
    float alpha = 1.0f;
    float typeParam = 1.0f;         // per type: rate, count or size (1.0 after Reset)
    float turnCos = 0.0f;           // \ cos / sin of the emitter's turn (a zone exit's
    float turnSin = 0.0f;           // / angle)
    float baseSize = 1.0f;
    Vector3 extent;
    AttachPoint attachPoint = AttachPoint::EffectPoint;
    Vector3 windDirection;          // copied from the world every tick
    float windStrength = 0.0f;
    bool additiveBlend = false;     // drawn with additive blending
    // Its effect's state (particle_data.h; its type is the effect's id): its inputs, vars, stage and anchors.
    std::unique_ptr<effectdata::State> data;
    Vector3 boundsMin;              // over the particles, rebuilt every tick
    Vector3 boundsMax;
    // The bounds the renderer clips by: BoundsFlag::Box set by the tick, the box (boundsMin and boundsMax less the
    // centre) relative to the centre; the radius is not written.
    ClipBounds clip = {};
    std::vector<Particle*> particles;
    std::vector<SourceFrame> frames;
    Ref<Texture> texture;
};

// The class factory's creator; the register arguments are unused.
ParticleSource* NewParticleSource(int type);

// ShownParticleType's answer for a type that shows nothing.
constexpr int kNoParticleType = -1;
// The type a source of `type` is made as: `type` itself when this build knows it, else kNoParticleType, logged once per
// type (a peer's or a save's effect from a mod or a newer build: the effect's world object stays, showing nothing).
int ShownParticleType(int type);
// The original type named `name` (ParticleType's names, any case), or kNoParticleType.
int ParticleTypeByName(std::string_view name);
// The name of the original type `type` (empty for another value).
std::string_view ParticleTypeName(int type);

// The particle renderer's shared arrays: one SpritePoint (position and size, interpolated between the last two
// ticks) and one sprite (colour, texture frame) per particle.
struct ParticleSprite {
    std::uint32_t color = 0;        // ARGB
    const SourceFrame* frame = nullptr;  // the particle's frame among its source's frames
};
