#pragma once

// The world's effect objects (00675B40-0067E6D0): placed sounds, lights, particle sources,
// lightning bolts and missiles. They are engine objects in the world's object list; the server
// ticks them (slot 6) and sends their state to the clients as a DeltaField (slots 21 / 22), and the
// client side starts or moves the actual sound, light or particle source (slot 23). Class names are
// the game's GetClassName strings; method names are provisional. See docs/notes/effects.md.
// Virtual slots shared by every effect (CEngineObject 0-12, CTransformObject 13-17):
//   2 Create (00675D10: Reset, then a full state write), 3 Destroy (00675D40: Reset),
//   6 Tick (0 = remove the effect), 9 / 10 Load / Save (save game; stream slot 1 Read, 2 Write,
//   +8 the save version), 18 Reset, 19 Radius (float), 20 Position(group),
//   21 Unpack(update, field) (returns 0 when the field did not end where expected),
//   22 Pack(field), 23 Start(update) (0: create the sound / light / source, else move it).
// Non-virtual helpers of the base: SendFull / SendDelta / Receive wrap Pack and Unpack in the
// DeltaField's begin / end.

#include "core_transform.h"
#include "effects.h"
#include "lightning.h"
#include "net_delta.h"
#include "spell.h"
#include "lib/mfc.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>

// The common base (game.exe's table 007434AC; abstract: GetClassName / GetClassId and Unpack /
// Pack / Start are pure). 0x14C bytes. The world's timed objects (CWorld::timedObjects_) are
// reached through its slots 18-20.
class CEffect : public CTransformObject {
public:
    // 0067B050 (out of line for CEffectArrow's parts; every effect's constructor inlines it): the
    // transform object's fields (00453400's), no velocity, the state field, start tick and moving
    // flag 0.
    CEffect();
    // 00675FB0 / 0067B390 (every effect's destructor inlines it; slot 0 of the abstract tables:
    // 00676910 / 0067B430): the state field's streams, then the transform object's.
    ~CEffect() override;

    int Create() override;     // Reset, then a full Pack; returns 1.
    void Destroy() override;   // Reset (a tail jump).
    int RunTick() override;    // 1: the effect stays.
    // Position, state field, start tick, velocity and stamp cleared.
    virtual void Reset();
    // How far it reaches: the constant at 007433AC, unrounded in ST0.
    virtual double Radius();
    // Its position as `group` sees it: the local position (the argument is ignored).
    virtual Vector3* Position(void* group);
    virtual int Unpack(int update, DeltaField* field) = 0;   // 21: 0 when the field did not end where expected
    virtual void Pack(DeltaField* field) = 0;                // 22
    virtual void Start(int update) = 0;                      // 23: 0 creates the sound / light / source, else moves it

    // 00675B40 / 00675B70. Pack (slot 22) into the state field written in full / as a delta.
    void SendFull();
    void SendDelta();
    // Unpack (slot 21) from `reader`; `update` is 0 for a new object. Returns slot 21's
    // result.
    int Receive(int update, BitStream* reader);
    // Flags bit 1 (changed) and the change stamp (global 007AFFC8).
    void MarkChanged();
    // Moving_ = velocity is not (+-0, +-0, +-0). Not a Ghidra function; no callers found.
    void UpdateMoving();
    // Sets the velocity (moving_ as in UpdateMoving) and marks the effect changed.
    void SetVelocity(const Vector3* velocity);

    std::uint8_t unused_54[0x94 - 0x54];
    int startTick_;            // +0x94 game tick the effect began (particles: warm-up and lifetime)
    Vector3 velocity_;         // +0x98 added to the position every tick while moving_
    int moving_;               // +0xA4 velocity is not zero
    DeltaField state_;         // +0xA8 the replicated state (see net_delta.h)
    int changeStamp_;          // +0x148 global 007AFFC8 when last changed, -1 after Reset
};
// 006773A0 (ECX array, EDX stream; world_effect.cpp). Reads a count and that many strings
// (CStream::ReadString) from a save stream, each appended to `array`.
void __fastcall ReadSavedStrings(mfc::CStringArray* array, CStream* stream);

// A positional sound (vtable 007433EC, class id 0x48). 0x264 bytes.
class CEffectSound final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 00676060 "CEffectSound"
    int GetClassId() override;                    // 12: 00676070 (0x48)
    // world_effect_lifetime.cpp. The constructor ends with Reset; the destructor runs Reset twice
    // (inline, then slot 18).
    CEffectSound();
    ~CEffectSound() override;                              // 006760A0 (deleting: 00676080)
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;                              // radius_

    char name_[0x100];         // +0x14C sound file
    float volume_;             // +0x24C
    float radius_;             // +0x250
    int looping_;              // +0x254 0: the effect ends after its first tick
    std::uint8_t noFade_;      // +0x258 plays with flag 1000
    std::uint8_t unused_259[3];
    int sound_;                // +0x25C handle in the sound system, -1: none
    std::uint8_t fresh_;       // +0x260 set by Reset, cleared by the first tick
    std::uint8_t unused_261[3];
};

// A sequence of sounds picked at random (vtable 0074344C, class id 99). 0x174 bytes.
class CEffectSoundSeq final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 00676940 "CEffectSoundSeq"
    int GetClassId() override;                    // 12: 00676950 (0x63)
    // As CEffectSound's; the destructor then destroys the name array and the name.
    CEffectSoundSeq();
    ~CEffectSoundSeq() override;                              // 00676980 (deleting: 00676960)
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;                       // 00676C00 (slot 23; world_effect_sequence.cpp)
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;                              // radius_

    mfc::CString name_;        // +0x14C the first sound
    mfc::CStringArray names_;  // +0x150 the others
    float volume_;             // +0x164
    float radius_;             // +0x168
    std::uint8_t noFade_;      // +0x16C
    std::uint8_t unused_16D[3];
    int sound_;                // +0x170 sequence handle, -1: none
};

// A looping ambient sound (vtable 0074350C, class id 0x49). 0x174 bytes.
class CEffectAmbientSound final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 006776A0 "CEffectAmbientSound"
    int GetClassId() override;                    // 12: 006776B0 (0x49)
    // The constructor ends with Reset; the destructor runs Reset twice (inline, then slot 18).
    CEffectAmbientSound();
    ~CEffectAmbientSound() override;                              // 006776E0 (deleting: 006776C0)
    void Reset() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    double Radius() override;                              // radius_

    mfc::CStringArray names_;  // +0x14C
    float volume_;             // +0x160
    float radius_;             // +0x164
    int minInterval_;          // +0x168 ms between plays (the sound emitter's minInterval)
    int maxInterval_;          // +0x16C ms, the upper bound
    int sound_;                // +0x170 handle, -1: none
};

// A point light (vtable 0074356C, class id 0x4B). 0x164 bytes.
class CEffectPointLight final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 00677F80 "CEffectPointLight"
    int GetClassId() override;                    // 12: 00677F90 (0x4B)
    // The constructor ends with Reset; the destructor runs Reset (slot 18) and releases the light.
    CEffectPointLight();
    ~CEffectPointLight() override;                              // 00677FC0 (deleting: 00677FA0)
    void Reset() override;
    int RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    // 24: 00677F70 (slot 19 is the base's). The base's radius plus range_.
    virtual double LightRadius();

    Vector3 color_;            // +0x14C
    float range_;              // +0x158 negative: flag 0x400 on the light
    HeldObject<CLight> light_; // +0x15C reference counted, null: none
    std::uint32_t lightFlags_; // +0x160 ored into the light's flags
};

// A particle source (vtable 007435D0, class id 0x4A; CreateParticleSource). 0x180 bytes.
class CEffectParticle : public CEffect {
public:
    const char* GetClassName() override;          // 11: 00678950 "CEffectParticle"
    int GetClassId() override;                    // 12: 00678960 (0x4A)
    // The constructor ends with Reset; the destructor runs Reset (slot 18) and releases the source.
    CEffectParticle();
    ~CEffectParticle() override;                              // 00678990 (deleting: 00678970)
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

    Vector3 worldPosition_;    // +0x14C Position's result when attached
    int type_;                 // +0x158 particle type 0x2000..
    float size_;               // +0x15C
    float alpha_;              // +0x160 copied to the source's +0x118
    float param164_;           // +0x164 copied to the source's +0x11C
    int attachedTo_;           // +0x168 object id, -1: none
    Vector3 target_;           // +0x16C moves towards it
    HeldObject<CParticleSource> source_;   // +0x178 reference counted
    int unitId_;               // +0x17C a unit whose position is used when unpacking, -1: none
};

// A particle wall (vtable 00743630, class id 0x5A; fire wall). 0x18C bytes.
class CEffectWall final : public CEffectParticle {
public:
    const char* GetClassName() override;          // 11: 006799A0 "CEffectWall"
    int GetClassId() override;                    // 12: 006799B0 (0x5A)
    // The constructor runs the particle's Reset and then its own; the destructor its
    // own Reset and the particle's, then releases the source.
    CEffectWall();
    ~CEffectWall() override;                              // 006799E0 (deleting: 006799C0)
    void Reset() override;
    void SetDirection(const Vector3* direction);
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    Vector3 direction_;        // +0x180 copied to the source's +0x130
};

// A lightning bolt between two points or objects (vtable 00743690, class id 0x4D). 0x178 bytes.
class CEffectLightning final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 0067A070 "CEffectLightning"
    int GetClassId() override;                    // 12: 0067A080 (0x4D)
    // The constructor does not reset: only the bolt (null), the width (0) and the flag (0) are set.
    // The destructor runs Reset (slot 18) and releases the bolt.
    CEffectLightning();
    ~CEffectLightning() override;                              // 0067A0B0 (deleting: 0067A090)
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

    Vector3 from_;             // +0x14C
    Vector3 to_;               // +0x158
    float width_;              // +0x164
    CLightning* bolt_;         // +0x168 the drawn bolt (reference counted)
    int toObject_;             // +0x16C its position (+1 in z) becomes to_
    int fromObject_;           // +0x170 a unit: its 00532860 point becomes from_ (server only)
    std::uint8_t flag_;        // +0x174
    std::uint8_t unused_175[3];
};

// A missile homing on a world object (abstract base, vtable 00743754: slots 21-24 pure). 0x158
// bytes. Subclasses: CEffectArrow (0x4E) and the class 0x5B missile (twice as fast).
class CEffectMissile : public CEffect {
public:
    // Slot 0 of the abstract table (0067B430 is the effect base's; no live object has it).
    ~CEffectMissile() override;
    // Aims the velocity at the target's centre at the missile speed (planar). 0 once
    // within one step or when the target is gone (arrived_ set, velocity zero without a target);
    // on a client the height is then kept above the ground. Returns 1 while flying.
    int Steer();
    // A new target: arrived_ cleared.
    void SetTarget(int objectId);
    // 18: 0067AA00 (the subclasses' Reset inline it). The base's Reset, not arrived, no target,
    // not from the caster.
    void Reset() override;
    // 24: the parts (or the figure) follow the missile.
    virtual void MoveParts() = 0;

    int arrived_;              // +0x14C
    int targetId_;             // +0x150 world object id, -1: none
    std::uint8_t fromCaster_;  // +0x154 the client starts the missile at the caster's effect point
    std::uint8_t unused_155[3];
};

// A spell missile: a particle source, a light and a looping sound flying together (vtable
// 007436F0, class id 0x4E; created by CSpellEffect::CreateArrow). 0x6C0 bytes.
class CEffectArrow final : public CEffectMissile {
public:
    const char* GetClassName() override;          // 11: 0067B180 "CEffectArrow"
    int GetClassId() override;                    // 12: 0067B190 (0x4E)
    // world_effect_missile.cpp. The three embedded effects are built and destroyed with their own
    // constructor and destructor bodies; the constructor ends with Reset.
    CEffectArrow();
    ~CEffectArrow() override;                              // 0067B1C0 (deleting: 0067B1A0)
    int RunTick() override;
    void Reset() override;
    void MoveParts() override;                    // the parts follow, Start(1)
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    // The embedded effects, built before the name and destroyed after it.
    CEffectParticle particle_;     // +0x158
    CEffectPointLight light_;      // +0x2D8
    CEffectSound sound_;           // +0x43C
    mfc::CString soundName_;       // +0x6A0
    Vector3 lightColor_;           // +0x6A4
    float lightRange_;             // +0x6B0
    int particleType_;             // +0x6B4
    float particleSize_;           // +0x6B8
    int casterId_;                 // +0x6BC unit, -1: none
};

// A bow or crossbow shot (vtable 007437B8, class id 0x5B, "CEffectArrowFigure"; made by
// CUnitServer::DeliverAttack 0054F450 with the hit it carries). A missile at twice the speed. The
// client shows it as a figure object of its own (class 0x38, model "efar1", texture "arrow01",
// filed in the client world's maps) that follows it, turned along the velocity; on arrival the
// server hands the hit to the target (its slot 31). 0x19C bytes. world_effect_missile.cpp.
class CEffectArrowFigure final : public CEffectMissile {
public:
    const char* GetClassName() override;          // 11: 0067BC90 "CEffectArrowFigure"
    int GetClassId() override;                    // 12: 0067BCA0 (0x5B)
    // The constructor resets (Reset); the destructor resets twice (inline, then slot 18).
    CEffectArrowFigure();
    ~CEffectArrowFigure() override;                              // 0067BCD0 (deleting: 0067BCB0)
    void Reset() override;                                 // figure out of the maps and released
    int RunTick() override;
    void MoveParts() override;                    // the figure follows
    void Start(int update) override;                       // 0 makes the figure
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    AIRefTo<CMapObject> figure_;   // +0x158 client only: the figure object (a stuff object, class 0x38, counted), null: none
    int casterId_;             // +0x15C the shooter, -1: none
    DamageInfo hit_;           // +0x160 delivered on arrival (multiplier 1 after Reset)
};

// What a unit sees, shown in the fog of war (clairvoyance; vtable 0074381C, class id 0x5E, created
// by CSpellEffect::CreateParticle and Start). The server recomputes the seen squares from the
// seer's sight every tick and sends them; the client reveals them in a fog object of its own.
// 0x170 bytes. world_effect_visibility.cpp.
class CEffectShowVisibility final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 0067C450 "CEffectShowVisibility"
    int GetClassId() override;                    // 12: 0067C460 (0x5E)
    // The constructor gives the grid one square (cells and rows allocated) and does not Reset.
    CEffectShowVisibility();
    ~CEffectShowVisibility() override;                              // 0067C490 (deleting: 0067C470)
    void Reset() override;                                 // grid cleared, fog released
    void SetUnits(int observerId, int seerId);
    void Start(int update) override;
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    int RunTick() override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    int observerId_;           // +0x14C unit (the second unit of the sight computation)
    int seerId_;               // +0x150 unit whose sight is shown; the effect follows it
    CVisibilityFog* fog_;      // +0x154 client only (counted), null: none
    FogGrid grid_;             // +0x158
};

// The bird of the client world (class id 100, vtable 0074387C, "CEffectMoshka"; CWorldClient keeps
// one at +0x1B0, made and told whom to follow by 00565380 / 00566C20). It perches near the followed
// object (+0x154, usually the leader), flies to it when it comes close, circles it and flies off
// to a perch again; the client's draw pass moves it every frame (Update with the animation time).
// Its figure object (class 0x38, model "unmobi", texture "bird00") carries a particle source
// (0x203C). Nothing is sent or saved (Load / Save are the base's, Pack / Start do nothing).
// 0x16C bytes. world_effect_moshka.cpp.
class CEffectMoshka final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 0067CAB0 "CEffectMoshka"
    int GetClassId() override;                    // 12: 0067CAC0 (0x64)
    // The constructor and destructor reset (the destructor twice: inline, then slot 18).
    CEffectMoshka();
    ~CEffectMoshka() override;                              // 0067CAF0 (deleting: 0067CAD0)
    void Reset() override;                                 // the figure out of the maps, both released
    int RunTick() override;                                   // Update(game tick), returns 0
    // 0067CD90 (RET 8; `this` unused). A perch seen from 1.5 above `from`: random directions at
    // distance 50 (then 5 less each try, at least 4) and height 30 (3 more each try) until the
    // AI map's line of sight to it is above 0.5. Returns out.
    Vector3* FindPerch(Vector3* out, const Vector3* from);
    // The frame's step: `now` is the animation time (frames).
    void Update(float now);
    // Follows object `objectId`: when it exists, a new figure at a perch near it
    // (filed in the client's maps) with its source, perched (state 0) for 200 frames.
    void Follow(int objectId);
    int Unpack(int update, DeltaField* field) override;    // 1
    void Pack(DeltaField*) override {}            // nothing is sent
    void Start(int) override {}

    AIRefTo<CMapObject> bird_;     // +0x14C the figure object (a stuff object, class 0x38, counted), null: none
    CParticleSource* source_;  // +0x150 counted, null: none
    int followedId_;           // +0x154 world object id, -1: none
    float lastTime_;           // +0x158 Update's last `now`
    float timer_;              // +0x15C frames left in the state
    float angle_;              // +0x160 circling: the angle at time 0 (radians)
    float height_;             // +0x164 circling: the height above the bird where it looks for a perch
    int state_;                // +0x168 0 perched, 1 circling, 2 flying to position_ (the perch)
};

// A tornado crossing the map (class id 0x65, vtable 007438F8, "CEffectTornado"; the server weather
// 0058CCD0 launches it). It moves in a straight line for framesLeft_ ticks, carries a particle
// source (0x200A) and the looping sound "nature\tornado\circle.wav"; on the server every 8th tick
// (while more than 20 remain) the units within 5 take a hit of 5 (damage type 6). The source stops
// emitting for the last 110 ticks. 0x15C bytes. world_effect_tornado.cpp.
class CEffectTornado final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 0067D7C0 "CEffectTornado"
    int GetClassId() override;                    // 12: 0067D7D0 (0x65)
    // The constructor resets inline (as Reset); the destructor resets twice (inline, slot 18).
    CEffectTornado();
    ~CEffectTornado() override;                              // 0067D800 (deleting: 0067D7E0)
    void Reset() override;                                 // source cleared and released, sound stopped
    // From `from` towards `to` at a random speed 0.096 .. 0.216 per tick; the
    // ticks to go are the distance over the speed (rounded, at least 0; 0 when the points meet).
    void Launch(const Vector3* from, const Vector3* to);
    int RunTick() override;                                   // 0 once the ticks are used up
    int Unpack(int update, DeltaField* field) override;
    void Pack(DeltaField* field) override;
    void Start(int update) override;
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;

    CParticleSource* source_;  // +0x14C client: counted, null: none
    int sound_;                // +0x150 client: the looping sound's handle, -1: none
    int framesLeft_;           // +0x154 ticks still to move
    int launchTick_;           // +0x158 the world's tick at Launch (Start catches the source up, at most 30)
};


