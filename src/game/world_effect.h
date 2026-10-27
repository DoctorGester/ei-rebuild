#pragma once

// The world's effect objects: placed sounds, lights, particle sources, lightning bolts and missiles. They are
// engine objects in the world's object list; the server ticks them (RunTick) and sends their state to the
// clients as a DeltaField (Unpack / Pack), and the client side starts or moves the actual sound, light or
// particle source (Start). See docs/notes/effects.md.

#include "core_transform.h"
#include "effects.h"
#include "lightning.h"
#include "net_delta.h"
#include "spell.h"
#include "lib/mfc.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>

// The common base (abstract). The world's timed objects (CWorld::timedObjects_) are reached through Reset,
// Radius and Position.
class CEffect : public CTransformObject {
public:
    CEffect();
    ~CEffect() override;

    int Create() override;     // Reset, then a full Pack; returns 1.
    void Destroy() override;   // Reset (a tail jump).
    int RunTick() override;    // 1: the effect stays.
    // Position, state field, start tick, velocity and stamp cleared.
    virtual void Reset();
    // How far it reaches: a constant.
    virtual double Radius();
    // Its position as `group` sees it: the local position (the argument is ignored).
    virtual Vector3* Position(void* group);
    virtual int Unpack(int update, DeltaField* field) = 0;   // 0 when the field did not end where expected
    virtual void Pack(DeltaField* field) = 0;
    virtual void Start(int update) = 0;                      // 0 creates the sound / light / source, else moves it

    // Pack into the state field written in full / as a delta.
    void SendFull();
    void SendDelta();
    // Unpack from `reader`; `update` is 0 for a new object. Returns Unpack's result.
    int Receive(int update, BitStream* reader);
    // Flags bit 1 (changed) and the change stamp (global).
    void MarkChanged();
    // moving_ = the velocity is not zero. No callers.
    void UpdateMoving();
    // Sets the velocity (moving_ as in UpdateMoving) and marks the effect changed.
    void SetVelocity(const Vector3* velocity);

    std::uint8_t unused_54[0x94 - 0x54];
    int startTick_;            // game tick the effect began (particles: warm-up and lifetime)
    Vector3 velocity_;         // added to the position every tick while moving_
    int moving_;               // velocity is not zero
    DeltaField state_;         // the replicated state (see net_delta.h)
    int changeStamp_;          // the global when last changed, -1 after Reset
};
// Reads a count and that many strings from a save stream, each appended to `array`.
void __fastcall ReadSavedStrings(mfc::CStringArray* array, CStream* stream);

// A positional sound (class id 0x48).
class CEffectSound final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectSound();
    ~CEffectSound() override;
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;

    char name_[0x100];         // sound file
    float volume_;
    float radius_;
    int looping_;              // 0: the effect ends after its first tick
    std::uint8_t noFade_;      // plays with flag 1000
    std::uint8_t unused_259[3];
    int sound_;                // handle in the sound system, -1: none
    std::uint8_t fresh_;       // set by Reset, cleared by the first tick
    std::uint8_t unused_261[3];
};

// A sequence of sounds picked at random (class id 99).
class CEffectSoundSeq final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectSoundSeq();
    ~CEffectSoundSeq() override;
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;

    mfc::CString name_;        // the first sound
    mfc::CStringArray names_;  // the others
    float volume_;
    float radius_;
    std::uint8_t noFade_;
    std::uint8_t unused_16D[3];
    int sound_;                // sequence handle, -1: none
};

// A looping ambient sound (class id 0x49).
class CEffectAmbientSound final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectAmbientSound();
    ~CEffectAmbientSound() override;
    void Reset() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;

    mfc::CStringArray names_;
    float volume_;
    float radius_;
    int minInterval_;          // ms between plays (the sound emitter's minInterval)
    int maxInterval_;          // ms, the upper bound
    int sound_;                // handle, -1: none
};

// A point light (class id 0x4B).
class CEffectPointLight final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectPointLight();
    ~CEffectPointLight() override;
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    virtual double LightRadius();

    Vector3 color_;
    float range_;              // negative: flag 0x400 on the light
    HeldObject<CLight> light_; // reference counted, null: none
    std::uint32_t lightFlags_; // ored into the light's flags
};

// A particle source (class id 0x4A; CreateParticleSource).
class CEffectParticle : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectParticle();
    ~CEffectParticle() override;
    void Reset() override;
    Vector3* Position(void* group) override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Spawn();                                 // creates or moves the source
    void Pack(DeltaField* field) override;
    void SetAttachedTo(int objectId);
    void SetSize(float size);
    void SetColors(float a, float b);
    void SetTarget(const Vector3* target);
    void Attach();                                // attaches the source to attachedTo_
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    int RunTick() override;

    Vector3 worldPosition_;    // Position's result when attached
    int type_;                 // particle type 0x2000..
    float size_;
    float alpha_;              // copied to the source
    float param164_;           // copied to the source
    int attachedTo_;           // object id, -1: none
    Vector3 target_;           // moves towards it
    HeldObject<CParticleSource> source_;   // reference counted
    int unitId_;               // a unit whose position is used when unpacking, -1: none
};

// A particle wall (class id 0x5A; fire wall).
class CEffectWall final : public CEffectParticle {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectWall();
    ~CEffectWall() override;
    void Reset() override;
    void SetDirection(const Vector3* direction);
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 direction_;        // copied to the source
};

// A lightning bolt between two points or objects (class id 0x4D).
class CEffectLightning final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectLightning();
    ~CEffectLightning() override;
    void Reset() override;
    void UpdateEnds();                            // end points from the objects
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void SetEnds(const Vector3* from, const Vector3* to, float width);
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    void SetObjects(int fromObject, int toObject);
    void SetFlag(std::uint8_t flag);
    int RunTick() override;

    Vector3 from_;
    Vector3 to_;
    float width_;
    CLightning* bolt_;         // the drawn bolt (reference counted)
    int toObject_;             // its position (+1 in z) becomes to_
    int fromObject_;           // a unit: its point becomes from_ (server only)
    std::uint8_t flag_;
    std::uint8_t unused_175[3];
};

// A missile homing on a world object (abstract base). Subclasses: CEffectArrow (0x4E) and the class 0x5B
// missile (twice as fast).
class CEffectMissile : public CEffect {
public:
    ~CEffectMissile() override;
    // Aims the velocity at the target's centre at the missile speed (planar). 0 once within one step or
    // when the target is gone (arrived_ set, velocity zero without a target); on a client the height is
    // then kept above the ground. Returns 1 while flying.
    int Steer();
    // A new target: arrived_ cleared.
    void SetTarget(int objectId);
    // Clears the target and the flags (the missile is not from the caster).
    void Reset() override;
    // The parts (or the figure) follow the missile.
    virtual void MoveParts() = 0;

    int arrived_;
    int targetId_;             // world object id, -1: none
    std::uint8_t fromCaster_;  // the client starts the missile at the caster's effect point
    std::uint8_t unused_155[3];
};

// A spell missile: a particle source, a light and a looping sound flying together (class id 0x4E; created by
// CSpellEffect::CreateArrow).
class CEffectArrow final : public CEffectMissile {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectArrow();
    ~CEffectArrow() override;
    int RunTick() override;
    void Reset() override;
    void MoveParts() override;                    // the parts follow, Start(1)
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // The embedded effects, built before the name and destroyed after it.
    CEffectParticle particle_;
    CEffectPointLight light_;
    CEffectSound sound_;
    mfc::CString soundName_;
    Vector3 lightColor_;
    float lightRange_;
    int particleType_;
    float particleSize_;
    int casterId_;                 // unit, -1: none
};

// A bow or crossbow shot (class id 0x5B, "CEffectArrowFigure"; made by CUnitServer::DeliverAttack with the
// hit it carries). A missile at twice the speed. The client shows it as a figure object of its own (class
// 0x38, model "efar1", texture "arrow01", filed in the client world's maps) that follows it, turned along the
// velocity; on arrival the server hands the hit to the target (ReceiveHit).
class CEffectArrowFigure final : public CEffectMissile {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectArrowFigure();
    ~CEffectArrowFigure() override;
    void Reset() override;                                 // figure out of the maps and released
    int RunTick() override;
    void MoveParts() override;                    // the figure follows
    void Start(int update) override;                       // 0 makes the figure
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> figure_;   // client only: the figure object (a stuff object, class 0x38, counted), null: none
    int casterId_;             // the shooter, -1: none
    DamageInfo hit_;           // delivered on arrival (multiplier 1 after Reset)
};

// What a unit sees, shown in the fog of war (clairvoyance, class id 0x5E, created by
// CSpellEffect::CreateParticle and Start). The server recomputes the seen squares from the seer's sight every
// tick and sends them; the client reveals them in a fog object of its own.
class CEffectShowVisibility final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectShowVisibility();
    ~CEffectShowVisibility() override;
    void Reset() override;                                 // grid cleared, fog released
    void SetUnits(int observerId, int seerId);
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    int RunTick() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    int observerId_;           // unit (the second unit of the sight computation)
    int seerId_;               // unit whose sight is shown; the effect follows it
    CVisibilityFog* fog_;      // client only (counted), null: none
    FogGrid grid_;
};

// The bird of the client world (class id 100, "CEffectMoshka"; CWorldClient keeps one in bird_, made and told
// whom to follow by the client world). It perches near the followed object (usually the leader), flies to it
// when it comes close, circles it and flies off to a perch again; the client's draw pass moves it every frame
// (Update with the animation time). Its figure object (class 0x38, model "unmobi", texture "bird00") carries
// a particle source (0x203C). Nothing is sent or saved (Load / Save are the base's, Pack / Start do nothing).
class CEffectMoshka final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectMoshka();
    ~CEffectMoshka() override;
    void Reset() override;                                 // the figure out of the maps, both released
    int RunTick() override;                                   // Update(game tick), returns 0
    // A perch seen from 1.5 above `from`: random directions at distance 50 (then 5 less each try, at least 4)
    // and height 30 (3 more each try) until the AI map's line of sight to it is above 0.5.
    Vector3* FindPerch(Vector3* out, const Vector3* from);
    // The frame's step: `now` is the animation time (frames).
    void Update(float now);
    // Follows object `objectId`: when it exists, a new figure at a perch near it (filed in the client's
    // maps) with its source, perched (state 0) for 200 frames.
    void Follow(int objectId);
    int Unpack(int update, DeltaField* field) override;    // 1
    void Pack(DeltaField*) override {}            // nothing is sent
    void Start(int) override {}

    AIRefTo<CMapObject> bird_;     // the figure object (a stuff object, class 0x38, counted), null: none
    CParticleSource* source_;  // counted, null: none
    int followedId_;           // world object id, -1: none
    float lastTime_;           // Update's last `now`
    float timer_;              // frames left in the state
    float angle_;              // circling: the angle at time 0 (radians)
    float height_;             // circling: the height above the bird where it looks for a perch
    int state_;                // 0 perched, 1 circling, 2 flying to position_ (the perch)
};

// A tornado crossing the map (class id 0x65, "CEffectTornado"; the server weather launches it). It moves in a
// straight line for framesLeft_ ticks, carries a particle source (0x200A) and the looping sound
// "nature\tornado\circle.wav"; on the server every 8th tick (while more than 20 remain) the units within 5
// take a hit of 5 (damage type 6). The source stops emitting for the last 110 ticks.
class CEffectTornado final : public CEffect {
public:
    const char* GetClassName() override;
    int GetClassId() override;
    CEffectTornado();
    ~CEffectTornado() override;
    void Reset() override;                                 // source cleared and released, sound stopped
    // From `from` towards `to` at a random speed 0.096 .. 0.216 per tick; the ticks to go are the
    // distance over the speed (rounded, at least 0; 0 when the points meet).
    void Launch(const Vector3* from, const Vector3* to);
    int RunTick() override;                                   // 0 once the ticks are used up
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Start(int update) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    CParticleSource* source_;  // client: counted, null: none
    int sound_;                // client: the looping sound's handle, -1: none
    int framesLeft_;           // ticks still to move
    int launchTick_;           // the world's tick at Launch (Start catches the source up, at most 30)
};


