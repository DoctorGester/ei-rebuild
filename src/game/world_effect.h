#pragma once

// The world's effect objects (00675B40-0067E6D0): placed sounds, lights, particle sources,
// lightning bolts and missiles. They are engine objects in the world's object list; the server
// ticks them (slot 6) and sends their state to the clients as a DeltaField (slots 21 / 22), and the
// client side starts or moves the actual sound, light or particle source (slot 23). Class names are
// the game's GetClassName strings; method names are provisional. See docs/notes/effects.md.
//
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

    int Create() override;     // 2: 00675D10. Reset, then a full Pack; returns 1.
    void Destroy() override;   // 3: 00675D40. Reset (a tail jump).
    int RunTick() override;    // 6: 00676900. 1: the effect stays.
    // 18: 00675BE0. Position, state field, start tick, velocity and stamp cleared.
    virtual void Reset();
    // 19: 006768F0. How far it reaches: the constant at 007433AC, unrounded in ST0.
    virtual double Radius();
    // 20: 00675FA0. Its position as `group` sees it: the local position (the argument is ignored).
    virtual Vector3* Position(void* group);
    virtual int Unpack(int update, DeltaField* field) = 0;   // 21: 0 when the field did not end where expected
    virtual void Pack(DeltaField* field) = 0;                // 22
    virtual void Start(int update) = 0;                      // 23: 0 creates the sound / light / source, else moves it

    // 00675B40 / 00675B70. Pack (slot 22) into the state field written in full / as a delta.
    void SendFull();
    void SendDelta();
    // 00675BA0. Unpack (slot 21) from `reader`; `update` is 0 for a new object. Returns slot 21's
    // result.
    int Receive(int update, BitStream* reader);
    // 00675CA0. Flags bit 1 (changed) and the change stamp (global 007AFFC8).
    void MarkChanged();
    // 00675CC0. moving_ = velocity is not (+-0, +-0, +-0). Not a Ghidra function; no callers found.
    void UpdateMoving();
    // 00675D50. Sets the velocity (moving_ as in UpdateMoving) and marks the effect changed.
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
    CEffectSound();                           // 00675DD0
    ~CEffectSound() override;                              // 006760A0 (deleting: 00676080)
    void Reset() override;                                 // 00676200 (slot 18)
    int RunTick() override;                                   // 006762F0 (slot 6)
    void Start(int update) override;                       // 006763A0 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 00676430 (slot 21)
    void Pack(DeltaField* field) override;                 // 00676550 (slot 22)
    void Load(CStream* stream) override;              // 006765F0 (slot 9)
    void Save(CStream* stream) override;              // 006766D0 (slot 10)
    double Radius() override;                              // 00676050 (slot 19): radius_

    char name_[0x100];         // +0x14C sound file
    float volume_;             // +0x24C
    float radius_;             // +0x250
    int looping_;              // +0x254 0: the effect ends after its first tick
    std::uint8_t noFade_;      // +0x258 plays with flag 1000
    std::uint8_t unused_259[3];
    int sound_;                // +0x25C handle in the sound system (007AF784), -1: none
    std::uint8_t fresh_;       // +0x260 set by Reset, cleared by the first tick
    std::uint8_t unused_261[3];
};

// A sequence of sounds picked at random (vtable 0074344C, class id 99). 0x174 bytes.
class CEffectSoundSeq final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 00676940 "CEffectSoundSeq"
    int GetClassId() override;                    // 12: 00676950 (0x63)
    // As CEffectSound's; the destructor then destroys the name array and the name.
    CEffectSoundSeq();                        // 00676760
    ~CEffectSoundSeq() override;                              // 00676980 (deleting: 00676960)
    void Reset() override;                                 // 00676B10 (slot 18)
    int RunTick() override;                                   // 00677250 (slot 6)
    void Start(int update) override;                       // 00676C00 (slot 23; world_effect_sequence.cpp)
    int Unpack(int update, DeltaField* field) override;    // 00676FD0 (slot 21)
    void Pack(DeltaField* field) override;                 // 00677180 (slot 22)
    void Load(CStream* stream) override;              // 006772D0 (slot 9)
    void Save(CStream* stream) override;              // 00677430 (slot 10)
    double Radius() override;                              // 00676930 (slot 19): radius_

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
    CEffectAmbientSound();                    // 00677510
    ~CEffectAmbientSound() override;                              // 006776E0 (deleting: 006776C0)
    void Reset() override;                                 // 00677870 (slot 18)
    void Start(int update) override;                       // 00677950 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 006779D0 (slot 21)
    void Pack(DeltaField* field) override;                 // 00677B20 (slot 22)
    void Load(CStream* stream) override;              // 00677BF0 (slot 9)
    void Save(CStream* stream) override;              // 00677C60 (slot 10)
    double Radius() override;                              // 00677690 (slot 19): radius_

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
    CEffectPointLight();                      // 00677D30
    ~CEffectPointLight() override;                              // 00677FC0 (deleting: 00677FA0)
    void Reset() override;                                 // 006780C0 (slot 18)
    int RunTick() override;                                   // 006781B0 (slot 6)
    void Start(int update) override;                       // 006782D0 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 00678520 (slot 21)
    void Pack(DeltaField* field) override;                 // 00678610 (slot 22)
    void Load(CStream* stream) override;              // 00678690 (slot 9)
    void Save(CStream* stream) override;              // 00678740 (slot 10)
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
    CEffectParticle();                        // 006787B0
    ~CEffectParticle() override;                              // 00678990 (deleting: 00678970)
    void Reset() override;                                 // 00678A90 (slot 18)
    Vector3* Position(void* group) override;               // 00678BA0 (slot 20)
    void Start(int update) override;                       // 00678CF0 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 00678D30 (slot 21)
    void Spawn();                                 // 00678EC0: creates or moves the source
    void Pack(DeltaField* field) override;                 // 006791A0 (slot 22)
    void SetAttachedTo(int objectId);             // 00679280
    void SetSize(float size);                     // 006792B0
    void SetColors(float a, float b);             // 006792E0
    void SetTarget(const Vector3* target);        // 00679310
    void Attach();                                // 00679350: attaches the source to attachedTo_
    void Load(CStream* stream) override;              // 006793E0 (slot 9)
    void Save(CStream* stream) override;              // 006794E0 (slot 10)
    int RunTick() override;                                   // 006795C0 (slot 6)

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
    // The constructor runs the particle's Reset (00678A90) and then its own; the destructor its
    // own Reset and the particle's, then releases the source.
    CEffectWall();                            // 006797E0
    ~CEffectWall() override;                              // 006799E0 (deleting: 006799C0)
    void Reset() override;                                 // 00679AF0 (slot 18)
    void SetDirection(const Vector3* direction);  // 00679C30
    int Unpack(int update, DeltaField* field) override;    // 00679C70 (slot 21)
    void Pack(DeltaField* field) override;                 // 00679DB0 (slot 22)
    void Load(CStream* stream) override;              // 00679EA0 (slot 9)
    void Save(CStream* stream) override;              // 00679ED0 (slot 10)

    Vector3 direction_;        // +0x180 copied to the source's +0x130
};

// A lightning bolt between two points or objects (vtable 00743690, class id 0x4D). 0x178 bytes.
class CEffectLightning final : public CEffect {
public:
    const char* GetClassName() override;          // 11: 0067A070 "CEffectLightning"
    int GetClassId() override;                    // 12: 0067A080 (0x4D)
    // The constructor does not reset: only the bolt (null), the width (0) and the flag (0) are set.
    // The destructor runs Reset (slot 18) and releases the bolt.
    CEffectLightning();                       // 00679FC0
    ~CEffectLightning() override;                              // 0067A0B0 (deleting: 0067A090)
    void Reset() override;                                 // 0067A1B0 (slot 18)
    void UpdateEnds();                            // 0067A2C0: end points from the objects
    void Start(int update) override;                       // 0067A370 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 0067A490 (slot 21)
    void Pack(DeltaField* field) override;                 // 0067A600 (slot 22)
    void SetEnds(const Vector3* from, const Vector3* to, float width);   // 0067A6B0
    void Load(CStream* stream) override;              // 0067A770 (slot 9)
    void Save(CStream* stream) override;              // 0067A7F0 (slot 10)
    void SetObjects(int fromObject, int toObject);    // 0067A890
    void SetFlag(std::uint8_t flag);              // 0067A8C0
    int RunTick() override;                                   // 0067A8F0 (slot 6)

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
    // 0067AAB0. Aims the velocity at the target's centre at the missile speed (planar). 0 once
    // within one step or when the target is gone (arrived_ set, velocity zero without a target);
    // on a client the height is then kept above the ground. Returns 1 while flying.
    int Steer();
    // 0067AC70. A new target: arrived_ cleared.
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
    CEffectArrow();                           // 0067AE30
    ~CEffectArrow() override;                              // 0067B1C0 (deleting: 0067B1A0)
    int RunTick() override;                                   // 0067AC90 (slot 6)
    void Reset() override;                                 // 0067AD10 (slot 18)
    void MoveParts() override;                    // 0067B450 (slot 24): the parts follow, Start(1)
    void Start(int update) override;                       // 0067B4E0 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 0067B690 (slot 21)
    void Pack(DeltaField* field) override;                 // 0067B800 (slot 22)
    void Load(CStream* stream) override;              // 0067B8E0 (slot 9)
    void Save(CStream* stream) override;              // 0067B9A0 (slot 10)

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
    CEffectArrowFigure();                     // 0067BB90
    ~CEffectArrowFigure() override;                              // 0067BCD0 (deleting: 0067BCB0)
    void Reset() override;                                 // 0067BAA0 (slot 18): figure out of the maps and released
    int RunTick() override;                                   // 0067BE20 (slot 6)
    void MoveParts() override;                    // 0067BEF0 (slot 24): the figure follows
    void Start(int update) override;                       // 0067BF20 (slot 23): 0 makes the figure
    int Unpack(int update, DeltaField* field) override;    // 0067C080 (slot 21)
    void Pack(DeltaField* field) override;                 // 0067C170 (slot 22)
    void Load(CStream* stream) override;              // 0067C200 (slot 9)
    void Save(CStream* stream) override;              // 0067C290 (slot 10)

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
    CEffectShowVisibility();                  // 0067C340
    ~CEffectShowVisibility() override;                              // 0067C490 (deleting: 0067C470)
    void Reset() override;                                 // 0067C5F0 (slot 18): grid cleared, fog released
    void SetUnits(int observerId, int seerId);    // 0067C640
    void Start(int update) override;                       // 0067C660 (slot 23)
    int Unpack(int update, DeltaField* field) override;    // 0067C6B0 (slot 21)
    void Pack(DeltaField* field) override;                 // 0067C7A0 (slot 22)
    int RunTick() override;                                   // 0067C840 (slot 6)
    void Load(CStream* stream) override;              // 0067C8E0 (slot 9)
    void Save(CStream* stream) override;              // 0067C910 (slot 10)

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
    CEffectMoshka();                          // 0067C950
    ~CEffectMoshka() override;                              // 0067CAF0 (deleting: 0067CAD0)
    void Reset() override;                                 // 0067CC90 (slot 18): the figure out of the maps, both released
    int RunTick() override;                                   // 0067CD70 (slot 6): Update(game tick), returns 0
    // 0067CD90 (RET 8; `this` unused). A perch seen from 1.5 above `from`: random directions at
    // distance 50 (then 5 less each try, at least 4) and height 30 (3 more each try) until the
    // AI map's line of sight to it is above 0.5. Returns out.
    Vector3* FindPerch(Vector3* out, const Vector3* from);
    // 0067CEF0 (RET 4). The frame's step: `now` is the animation time (frames).
    void Update(float now);
    // 0067D420 (RET 4). Follows object `objectId`: when it exists, a new figure at a perch near it
    // (filed in the client's maps) with its source, perched (state 0) for 200 frames.
    void Follow(int objectId);
    int Unpack(int update, DeltaField* field) override;    // 0067D640 (slot 21): 1
    void Pack(DeltaField*) override {}            // 0067D650 (slot 22): nothing is sent
    void Start(int) override {}                   // 0067D660 (slot 23)

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
    CEffectTornado();                         // 0067D670
    ~CEffectTornado() override;                              // 0067D800 (deleting: 0067D7E0)
    void Reset() override;                                 // 0067D970 (slot 18): source cleared and released, sound stopped
    // 0067DA50 (RET 8). From `from` towards `to` at a random speed 0.096 .. 0.216 per tick; the
    // ticks to go are the distance over the speed (rounded, at least 0; 0 when the points meet).
    void Launch(const Vector3* from, const Vector3* to);
    int RunTick() override;                                   // 0067DBD0 (slot 6): 0 once the ticks are used up
    int Unpack(int update, DeltaField* field) override;    // 0067DDD0 (slot 21)
    void Pack(DeltaField* field) override;                 // 0067DEB0 (slot 22)
    void Start(int update) override;                       // 0067DF10 (slot 23)
    void Load(CStream* stream) override;              // 0067E020 (slot 9)
    void Save(CStream* stream) override;              // 0067E070 (slot 10)

    CParticleSource* source_;  // +0x14C client: counted, null: none
    int sound_;                // +0x150 client: the looping sound's handle, -1: none
    int framesLeft_;           // +0x154 ticks still to move
    int launchTick_;           // +0x158 the world's tick at Launch (Start catches the source up, at most 30)
};


