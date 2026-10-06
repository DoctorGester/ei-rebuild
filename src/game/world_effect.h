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
#include "net_delta.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

// The common base (vtable 007434AC; slots 11, 12, 21-23 pure). 0x14C bytes.
class CEffect : public CTransformObject {
public:
    // 00675B40 / 00675B70. Pack (slot 22) into the state field written in full / as a delta.
    void SendFull();
    void SendDelta();
    // 00675BA0. Unpack (slot 21) from `reader`; `update` is 0 for a new object. Returns slot 21's
    // result.
    int Receive(int update, BitStream* reader);
    // 00675BE0 (slot 18 of the base). Position, state field, start tick, velocity and stamp cleared.
    void ResetBase();
    // 00675CA0. Flags bit 1 (changed) and the change stamp (global 007AFFC8).
    void MarkChanged();
    // 00675CC0. moving_ = velocity is not (+-0, +-0, +-0). Not a Ghidra function; no callers found.
    void UpdateMoving();
    // 00675D10 (slot 2). Reset (slot 18), then a full Pack; returns 1.
    int CreateEffect();
    // 00675D40 (slot 3). Reset (slot 18; a tail jump).
    void DestroyEffect();
    // 00675D50. Sets the velocity (moving_ as in UpdateMoving) and marks the effect changed.
    void SetVelocity(const Vector3* velocity);
    // 00675FA0 (slot 20 of the base). The local position; the argument is ignored.
    Vector3* Position(void* group);
    // 006768F0 (slot 19 of the base). The constant at 007433AC, unrounded in ST0.
    double DefaultRadius();
    // The base constructor every effect constructor inlines (world_effect_lifetime.cpp): the
    // transform object (00453400), no velocity, the state field, start tick and moving flag 0.
    void ConstructEffect();
    // The base destructor every effect destructor inlines: the state field's streams, then the
    // transform object's vtable and CEngineObject's destructor body (00455930).
    void DestructEffect();

    std::uint8_t unknown_54[0x94 - 0x54];
    int startTick_;            // +0x94 game tick the effect began (particles: warm-up and lifetime)
    Vector3 velocity_;         // +0x98 added to the position every tick while moving_
    int moving_;               // +0xA4 velocity is not zero
    DeltaField state_;         // +0xA8 the replicated state (see net_delta.h)
    int changeStamp_;          // +0x148 global 007AFFC8 when last changed, -1 after Reset
};
static_assert(offsetof(CEffect, startTick_) == 0x94);
static_assert(offsetof(CEffect, state_) == 0xA8);
static_assert(sizeof(CEffect) == 0x14C);

// A positional sound (vtable 007433EC, class id 0x48). 0x264 bytes.
class CEffectSound : public CEffect {
public:
    // Constructor body, destructor body and slot 0 (world_effect_lifetime.cpp). The constructor
    // ends with Reset; the destructor runs Reset twice (inline, then slot 18).
    CEffectSound* Construct();                    // 00675DD0
    void Destruct();                              // 006760A0
    CEffectSound* DeletingDestruct(unsigned flags);   // 00676080
    static std::uintptr_t Vtable();               // 007433EC
    void Reset();                                 // 00676200 (slot 18)
    int Tick();                                   // 006762F0 (slot 6)
    void Start(int update);                       // 006763A0 (slot 23)
    int Unpack(int update, DeltaField* field);    // 00676430 (slot 21)
    void Pack(DeltaField* field);                 // 00676550 (slot 22)
    void LoadState(void* stream);                 // 006765F0 (slot 9)
    void SaveState(void* stream);                 // 006766D0 (slot 10)
    double Radius();                              // 00676050 (slot 19): radius_

    char name_[0x100];         // +0x14C sound file
    float volume_;             // +0x24C
    float radius_;             // +0x250
    int looping_;              // +0x254 0: the effect ends after its first tick
    std::uint8_t noFade_;      // +0x258 plays with flag 1000
    std::uint8_t unknown_259[3];
    int sound_;                // +0x25C handle in the sound system (007AF784), -1: none
    std::uint8_t fresh_;       // +0x260 set by Reset, cleared by the first tick
    std::uint8_t unknown_261[3];
};
static_assert(offsetof(CEffectSound, sound_) == 0x25C);
static_assert(sizeof(CEffectSound) == 0x264);

// MFC CStringArray (vtable, data, size, max, grow by), only for the layouts below.
struct EffectStringArray {
    void* vtable;
    char** data;
    int size;
    int maxSize;
    int growBy;
};
static_assert(sizeof(EffectStringArray) == 0x14);

// A sequence of sounds picked at random (vtable 0074344C, class id 99). 0x174 bytes.
class CEffectSoundSeq : public CEffect {
public:
    // As CEffectSound's; the destructor then destroys the name array and the name.
    CEffectSoundSeq* Construct();                 // 00676760
    void Destruct();                              // 00676980
    CEffectSoundSeq* DeletingDestruct(unsigned flags);    // 00676960
    static std::uintptr_t Vtable();               // 0074344C
    void Reset();                                 // 00676B10 (slot 18)
    int Tick();                                   // 00677250 (slot 6)
    void Pack(DeltaField* field);                 // 00677180 (slot 22)
    void LoadState(void* stream);                 // 006772D0 (slot 9)
    void SaveState(void* stream);                 // 00677430 (slot 10)
    double Radius();                              // 00676930 (slot 19): radius_

    char* name_;               // +0x14C CString: the first sound
    EffectStringArray names_;  // +0x150 the others
    float volume_;             // +0x164
    float radius_;             // +0x168
    std::uint8_t noFade_;      // +0x16C
    std::uint8_t unknown_16D[3];
    int sound_;                // +0x170 sequence handle, -1: none
};
static_assert(offsetof(CEffectSoundSeq, volume_) == 0x164);
static_assert(sizeof(CEffectSoundSeq) == 0x174);

// A looping ambient sound (vtable 0074350C, class id 0x49). 0x174 bytes.
class CEffectAmbientSound : public CEffect {
public:
    void Reset();                                 // 00677870 (slot 18)
    void Start(int update);                       // 00677950 (slot 23)
    int Unpack(int update, DeltaField* field);    // 006779D0 (slot 21)
    void Pack(DeltaField* field);                 // 00677B20 (slot 22)
    void LoadState(void* stream);                 // 00677BF0 (slot 9)
    void SaveState(void* stream);                 // 00677C60 (slot 10)
    double Radius();                              // 00677690 (slot 19): radius_

    EffectStringArray names_;  // +0x14C
    float volume_;             // +0x160
    float radius_;             // +0x164
    int unknown_168;           // +0x168
    int unknown_16C;           // +0x16C
    int sound_;                // +0x170 handle, -1: none
};
static_assert(offsetof(CEffectAmbientSound, volume_) == 0x160);
static_assert(sizeof(CEffectAmbientSound) == 0x174);

// A point light (vtable 0074356C, class id 0x4B). 0x164 bytes.
class CEffectPointLight : public CEffect {
public:
    // The constructor ends with Reset; the destructor runs Reset (slot 18) and releases the light.
    CEffectPointLight* Construct();               // 00677D30
    void Destruct();                              // 00677FC0
    CEffectPointLight* DeletingDestruct(unsigned flags);  // 00677FA0
    static std::uintptr_t Vtable();               // 0074356C
    void Reset();                                 // 006780C0 (slot 18)
    int Tick();                                   // 006781B0 (slot 6)
    void Start(int update);                       // 006782D0 (slot 23)
    int Unpack(int update, DeltaField* field);    // 00678520 (slot 21)
    void Pack(DeltaField* field);                 // 00678610 (slot 22)
    void LoadState(void* stream);                 // 00678690 (slot 9)
    void SaveState(void* stream);                 // 00678740 (slot 10)
    double Radius();                              // 00677F70 (slot 19): DefaultRadius + range_

    Vector3 color_;            // +0x14C
    float range_;              // +0x158 negative: flag 0x400 on the light
    CEngineObject* light_;     // +0x15C the CLight (reference counted), null: none
    std::uint32_t lightFlags_; // +0x160 ored into the light's flags
};
static_assert(offsetof(CEffectPointLight, light_) == 0x15C);
static_assert(sizeof(CEffectPointLight) == 0x164);

// A particle source (vtable 007435D0, class id 0x4A; CreateParticleSource). 0x180 bytes.
class CEffectParticle : public CEffect {
public:
    // The constructor ends with Reset; the destructor runs Reset (slot 18) and releases the source.
    CEffectParticle* Construct();                 // 006787B0
    void Destruct();                              // 00678990
    CEffectParticle* DeletingDestruct(unsigned flags);    // 00678970
    static std::uintptr_t Vtable();               // 007435D0
    void Reset();                                 // 00678A90 (slot 18)
    Vector3* Position(void* group);               // 00678BA0 (slot 20)
    void Start(int update);                       // 00678CF0 (slot 23)
    int Unpack(int update, DeltaField* field);    // 00678D30 (slot 21)
    void Spawn();                                 // 00678EC0: creates or moves the source
    void Pack(DeltaField* field);                 // 006791A0 (slot 22)
    void SetAttachedTo(int objectId);             // 00679280
    void SetSize(float size);                     // 006792B0
    void SetColors(float a, float b);             // 006792E0
    void SetTarget(const Vector3* target);        // 00679310
    void Attach();                                // 00679350: attaches the source to attachedTo_
    void LoadState(void* stream);                 // 006793E0 (slot 9)
    void SaveState(void* stream);                 // 006794E0 (slot 10)
    int Tick();                                   // 006795C0 (slot 6)

    Vector3 worldPosition_;    // +0x14C Position's result when attached
    int type_;                 // +0x158 particle type 0x2000..
    float size_;               // +0x15C
    float alpha_;              // +0x160 copied to the source's +0x118
    float param164_;           // +0x164 copied to the source's +0x11C
    int attachedTo_;           // +0x168 object id, -1: none
    Vector3 target_;           // +0x16C moves towards it
    void* source_;             // +0x178 the CParticleSource (reference counted)
    int unitId_;               // +0x17C a unit whose position is used when unpacking, -1: none
};
static_assert(offsetof(CEffectParticle, source_) == 0x178);
static_assert(sizeof(CEffectParticle) == 0x180);

// A particle wall (vtable 00743630, class id 0x5A; fire wall). 0x18C bytes.
class CEffectWall : public CEffectParticle {
public:
    // The constructor runs the particle's Reset (00678A90) and then its own; the destructor its
    // own Reset and the particle's, then releases the source.
    CEffectWall* Construct();                     // 006797E0
    void Destruct();                              // 006799E0
    CEffectWall* DeletingDestruct(unsigned flags);    // 006799C0
    static std::uintptr_t Vtable();               // 00743630
    void Reset();                                 // 00679AF0 (slot 18)
    void SetDirection(const Vector3* direction);  // 00679C30
    int Unpack(int update, DeltaField* field);    // 00679C70 (slot 21)
    void Pack(DeltaField* field);                 // 00679DB0 (slot 22)
    void LoadState(void* stream);                 // 00679EA0 (slot 9)
    void SaveState(void* stream);                 // 00679ED0 (slot 10)

    Vector3 direction_;        // +0x180 copied to the source's +0x130
};
static_assert(sizeof(CEffectWall) == 0x18C);

// A lightning bolt between two points or objects (vtable 00743690, class id 0x4D). 0x178 bytes.
class CEffectLightning : public CEffect {
public:
    // The constructor does not reset: only the bolt (null), the width (0) and the flag (0) are set.
    // The destructor runs Reset (slot 18) and releases the bolt.
    CEffectLightning* Construct();                // 00679FC0
    void Destruct();                              // 0067A0B0
    CEffectLightning* DeletingDestruct(unsigned flags);   // 0067A090
    static std::uintptr_t Vtable();               // 00743690
    void Reset();                                 // 0067A1B0 (slot 18)
    void UpdateEnds();                            // 0067A2C0: end points from the objects
    void Start(int update);                       // 0067A370 (slot 23)
    int Unpack(int update, DeltaField* field);    // 0067A490 (slot 21)
    void Pack(DeltaField* field);                 // 0067A600 (slot 22)
    void SetEnds(const Vector3* from, const Vector3* to, float width);   // 0067A6B0
    void LoadState(void* stream);                 // 0067A770 (slot 9)
    void SaveState(void* stream);                 // 0067A7F0 (slot 10)
    void SetObjects(int fromObject, int toObject);    // 0067A890
    void SetFlag(std::uint8_t flag);              // 0067A8C0
    int Tick();                                   // 0067A8F0 (slot 6)

    Vector3 from_;             // +0x14C
    Vector3 to_;               // +0x158
    float width_;              // +0x164
    CEngineObject* bolt_;      // +0x168 the drawn bolt (reference counted)
    int toObject_;             // +0x16C its position (+1 in z) becomes to_
    int fromObject_;           // +0x170 a unit: its 00532860 point becomes from_ (server only)
    std::uint8_t flag_;        // +0x174
    std::uint8_t unknown_175[3];
};
static_assert(offsetof(CEffectLightning, bolt_) == 0x168);
static_assert(sizeof(CEffectLightning) == 0x178);

// A missile homing on a world object (abstract base, vtable 00743754: slots 21-24 pure). 0x158
// bytes. Subclasses: CEffectArrow (0x4E) and the class 0x5B missile (twice as fast).
class CEffectMissile : public CEffect {
public:
    // 0067AAB0. Aims the velocity at the target's centre at the missile speed (planar). 0 once
    // within one step or when the target is gone (arrived_ set, velocity zero without a target);
    // on a client the height is then kept above the ground. Returns 1 while flying.
    int Steer();
    // 0067AC70. A new target: arrived_ cleared.
    void SetTarget(int objectId);

    int arrived_;              // +0x14C
    int targetId_;             // +0x150 world object id, -1: none
    std::uint8_t fromCaster_;  // +0x154 the client starts the missile at the caster's effect point
    std::uint8_t unknown_155[3];
};
static_assert(offsetof(CEffectMissile, arrived_) == 0x14C);
static_assert(sizeof(CEffectMissile) == 0x158);

// A spell missile: a particle source, a light and a looping sound flying together (vtable
// 007436F0, class id 0x4E; created by CSpellEffect::CreateArrow). 0x6C0 bytes.
class CEffectArrow : public CEffectMissile {
public:
    // world_effect_missile.cpp. The three embedded effects are built and destroyed with their own
    // constructor and destructor bodies; the constructor ends with Reset.
    CEffectArrow* Construct();                    // 0067AE30
    void Destruct();                              // 0067B1C0
    CEffectArrow* DeletingDestruct(unsigned flags);   // 0067B1A0 (slot 0)
    static std::uintptr_t Vtable();               // 007436F0
    int Tick();                                   // 0067AC90 (slot 6)
    void Reset();                                 // 0067AD10 (slot 18)
    void MoveParts();                             // 0067B450 (slot 24): the parts follow, Start(1)
    void Start(int update);                       // 0067B4E0 (slot 23)
    int Unpack(int update, DeltaField* field);    // 0067B690 (slot 21)
    void Pack(DeltaField* field);                 // 0067B800 (slot 22)
    void LoadState(void* stream);                 // 0067B8E0 (slot 9)
    void SaveState(void* stream);                 // 0067B9A0 (slot 10)

    // The embedded effects (the classes are abstract in C++, so the members are their bytes).
    CEffectParticle& Particle() { return *reinterpret_cast<CEffectParticle*>(particle_); }
    CEffectPointLight& Light() { return *reinterpret_cast<CEffectPointLight*>(light_); }
    CEffectSound& Sound() { return *reinterpret_cast<CEffectSound*>(sound_); }
    const CEffectParticle& Particle() const { return *reinterpret_cast<const CEffectParticle*>(particle_); }
    const CEffectPointLight& Light() const { return *reinterpret_cast<const CEffectPointLight*>(light_); }
    const CEffectSound& Sound() const { return *reinterpret_cast<const CEffectSound*>(sound_); }

    alignas(4) std::uint8_t particle_[sizeof(CEffectParticle)];   // +0x158
    alignas(4) std::uint8_t light_[sizeof(CEffectPointLight)];    // +0x2D8
    alignas(4) std::uint8_t sound_[sizeof(CEffectSound)];         // +0x43C
    mfc::CString soundName_;       // +0x6A0
    Vector3 lightColor_;           // +0x6A4
    float lightRange_;             // +0x6B0
    int particleType_;             // +0x6B4
    float particleSize_;           // +0x6B8
    int casterId_;                 // +0x6BC unit, -1: none
};
static_assert(offsetof(CEffectArrow, light_) == 0x2D8);
static_assert(offsetof(CEffectArrow, sound_) == 0x43C);
static_assert(offsetof(CEffectArrow, soundName_) == 0x6A0);
static_assert(sizeof(CEffectArrow) == 0x6C0);
