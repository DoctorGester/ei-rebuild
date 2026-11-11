#pragma once

// The world's effect objects: placed sounds, lights, particle sources, lightning bolts and missiles. They are
// engine objects in the world's object list; the server ticks them (RunTick) and sends their state to the
// clients as a DeltaField (Unpack / Pack), and the client side starts or moves the actual sound, light or
// particle source (Start). See docs/notes/effects.md.

#include "engine/core/core_transform.h"
#include "game/effects/effects.h"
#include "game/effects/lightning.h"
#include "game/net/net_delta.h"
#include "engine/scene/light.h"
#include "game/spells/spell.h"


#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ScriptGroup;

// The common base (abstract). The world's timed objects (World::timedObjects) are reached through Reset,
// Radius and Position.
class Effect : public TransformObject {
public:
    Effect();
    ~Effect() override;

    void Create() override;     // Reset, then a full Pack.
    void Destroy() override;   // Reset (a tail jump).
    bool RunTick() override;    // 1: the effect stays.
    // Position, state field, start tick, velocity and stamp cleared.
    virtual void Reset();
    // How far it reaches: a constant.
    virtual float Radius();
    // Its position as `group` sees it: the local position (the argument is ignored).
    virtual Vector3* Position(const ScriptGroup* group);
    virtual int Unpack(int update, DeltaReader& field) = 0;   // 0 when the field did not end where expected
    virtual void Pack(DeltaWriter& field) = 0;
    virtual void Start(int update) = 0;                      // 0 creates the sound / light / source, else moves it

    // Pack into the state field written in full / as a delta.
    void SendFull();
    void SendDelta();
    // Unpack from `reader`; `update` is 0 for a new object. Returns Unpack's result.
    int Receive(int update, MemoryStream* reader);
    // Flags bit 1 (changed) and the change stamp (global).
    void MarkChanged();
    // Sets the velocity (moving as in UpdateMoving) and marks the effect changed.
    void SetVelocity(const Vector3& velocity);

    int startTick = 0;         // game tick the effect began (particles: warm-up and lifetime)
    Vector3 velocity;          // added to the position every tick while moving
    int moving = 0;            // velocity is not zero
    DeltaField state;          // the replicated state (see net_delta.h)
    int changeStamp = 0;       // the global when last changed, -1 after Reset
};
// Reads a count and that many strings from a save stream, each appended to `array`.
std::vector<std::string> ReadSavedStrings(ByteStream* stream);

// A positional sound (class id 0x48).
class EffectSound final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectSound();
    ~EffectSound() override;
    void Reset() override;
    bool RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    float Radius() override;

    char name[0x100] = {};     // sound file
    float volume = 0.0f;
    float radius = 0.0f;
    int looping = 1;           // 0: the effect ends after its first tick
    std::uint8_t noFade = 0;   // plays with flag 1000
    int sound = -1;            // handle in the sound system, -1: none
    std::uint8_t fresh = 1;    // set by Reset, cleared by the first tick
};

// A sequence of sounds picked at random (class id 99).
class EffectSoundSeq final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectSoundSeq();
    ~EffectSoundSeq() override;
    void Reset() override;
    bool RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    float Radius() override;

    std::string name;          // the first sound
    std::vector<std::string> names;   // the others
    float volume = 0.0f;
    float radius = 0.0f;
    std::uint8_t noFade = 0;
    int sound = -1;            // sequence handle, -1: none
};

// A looping ambient sound (class id 0x49).
class EffectAmbientSound final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectAmbientSound();
    ~EffectAmbientSound() override;
    void Reset() override;
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    float Radius() override;

    std::vector<std::string> names;
    float volume = 0.0f;
    float radius = 0.0f;
    int minInterval = 0;       // ms between plays (the sound emitter's minInterval)
    int maxInterval = 0;       // ms, the upper bound
    int sound = -1;            // handle, -1: none
};

// A point light (class id 0x4B).
class EffectPointLight final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectPointLight();
    ~EffectPointLight() override;
    void Reset() override;
    bool RunTick() override;
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    Vector3 color;
    float range = 0.0f;        // negative: LightFlag::Off on the light
    Ref<Light> light; // reference counted, null: none
    std::uint32_t lightFlags = 0; // LightFlag bits ored into the light's flags (saved)
};

// The particle type of the effect `name` (a game's by its name, a mod's from Res\effects): what EffectParticle::type
// and ParticleManager::CreateSource take; none for an empty name or one no effect has (a warning, once per name). The
// records' effect fields go through it (a spell's cast_look and looks, a model's blood_look and shock_look, a flame's
// and a particle object's effect; docs/modding.md, "Records that name effects").
std::optional<int> EffectTypeNamed(std::string_view name);

// A particle source (class id 0x4A; CreateParticleSource).
class EffectParticle : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectParticle();
    ~EffectParticle() override;
    void Reset() override;
    Vector3* Position(const ScriptGroup* group) override;
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Spawn();                                 // creates or moves the source
    void Pack(DeltaWriter& field) override;
    void SetAttachedTo(int objectId);
    void SetSize(float size);
    void SetColors(float newAlpha, float newTypeParam);
    void SetTarget(const Vector3& target);
    void Attach();                                // attaches the source to attachedTo
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    bool RunTick() override;

    Vector3 worldPosition;     // Position's result when attached
    int type = 0;              // particle type 0x2000..
    float size = 1.0f;
    float alpha = 1.0f;        // copied to the source
    float typeParam = 1.0f;     // copied to the source
    int attachedTo = -1;       // object id, -1: none
    Vector3 target;            // moves towards it
    Ref<ParticleSource> source;    // reference counted
    int unitId = -1;           // a unit whose position is used when unpacking, -1: none
};

// A particle wall (class id 0x5A; fire wall).
class EffectWall final : public EffectParticle {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectWall();
    ~EffectWall() override;
    void Reset() override;
    void SetDirection(const Vector3& direction);
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    Vector3 direction;         // copied to the source
};

// A lightning bolt between two points or objects (class id 0x4D).
class EffectLightning final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    ~EffectLightning() override;
    void Reset() override;
    void UpdateEnds();                            // end points from the objects
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void SetEnds(const Vector3& from, const Vector3& to, float width);
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    void SetObjects(int fromObject, int toObject);
    void SetFlag(std::uint8_t flag);
    bool RunTick() override;

    Vector3 from;
    Vector3 to;
    float width = 1.0f;
    Ref<Lightning> bolt;      // the drawn bolt
    int toObject = -1;         // its position (+1 in z) becomes to
    int fromObject = -1;       // a unit: its point becomes from (server only)
    std::uint8_t flag = 0;
};

// A missile homing on a world object (abstract base). Subclasses: EffectArrow (0x4E) and the class 0x5B
// missile (twice as fast).
class EffectMissile : public Effect {
public:
    ~EffectMissile() override;
    // Aims the velocity at the target's centre at the missile speed (planar). 0 once within one step or
    // when the target is gone (arrived set, velocity zero without a target); on a client the height is
    // then kept above the ground. Returns 1 while flying.
    int Steer();
    // A new target: arrived cleared.
    void SetTarget(int objectId);
    // Clears the target and the flags (the missile is not from the caster).
    void Reset() override;
    // The parts (or the figure) follow the missile.
    virtual void MoveParts() = 0;

    int arrived = 0;
    int targetId = -1;         // world object id, -1: none
    std::uint8_t fromCaster = 0;  // the client starts the missile at the caster's effect point
};

// A spell missile: a particle source, a light and a looping sound flying together (class id 0x4E; created by
// SpellEffect::NewArrow).
class EffectArrow final : public EffectMissile {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectArrow();
    ~EffectArrow() override;
    bool RunTick() override;
    void Reset() override;
    void MoveParts() override;                    // the parts follow, Start(1)
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    // The embedded effects, built before the name and destroyed after it.
    EffectParticle particle;
    EffectPointLight light;
    EffectSound sound;
    std::string soundName;
    Vector3 lightColor;
    float lightRange = 0.0f;
    int particleType = 0;
    float particleSize = 1.0f;
    int casterId = -1;             // unit, -1: none
};

// A bow or crossbow shot (class id 0x5B, "EffectArrowFigure"; made by UnitServer::DeliverAttack with the
// hit it carries). A missile at twice the speed. The client shows it as a figure object of its own (class
// 0x38, model "efar1", texture "arrow01", filed in the client world's maps) that follows it, turned along the
// velocity; on arrival the server hands the hit to the target (ReceiveHit).
class EffectArrowFigure final : public EffectMissile {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectArrowFigure();
    ~EffectArrowFigure() override;
    void Reset() override;                                 // figure out of the maps and released
    bool RunTick() override;
    void MoveParts() override;                    // the figure follows
    void Start(int update) override;                       // 0 makes the figure
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    Ref<MapObject> figure;    // client only: the figure object (a stuff object, class 0x38, counted), null: none
    int casterId = -1;         // the shooter, -1: none
    DamageInfo hit;            // delivered on arrival (multiplier 1 after Reset)
};

// What a unit sees, shown in the fog of war (clairvoyance, class id 0x5E, created by
// SpellEffect::CreateParticle and Start). The server recomputes the seen squares from the seer's sight every
// tick and sends them; the client reveals them in a fog object of its own.
class EffectShowVisibility final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectShowVisibility();
    ~EffectShowVisibility() override;
    void Reset() override;                                 // grid cleared, fog released
    void SetUnits(int observerId, int seerId);
    void Start(int update) override;
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    bool RunTick() override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    int observerId = -1;       // unit (the second unit of the sight computation)
    int seerId = -1;           // unit whose sight is shown; the effect follows it
    Ref<VisibilityFog> fog;   // client only, null: none
    FogGrid grid;
};

// The bird of the client world (class id 100, "EffectMoshka"; WorldClient keeps one in bird, made and told
// whom to follow by the client world). It perches near the followed object (usually the leader), flies to it
// when it comes close, circles it and flies off to a perch again; the client's draw pass moves it every frame
// (Update with the animation time). Its figure object (class 0x38, model "unmobi", texture "bird00") carries
// a particle source (0x203C). Nothing is sent or saved (Load / Save are the base's, Pack / Start do nothing).
class EffectMoshka final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    enum class State : int { Perched = 0, Circling = 1, FlyingOff = 2 };

    EffectMoshka();
    ~EffectMoshka() override;
    void Reset() override;                                 // the figure out of the maps, both released
    bool RunTick() override;                                   // Update(game tick), returns 0
    // A perch seen from 1.5 above `from`: random directions at distance 50 (then 5 less each try, at least 4)
    // and height 30 (3 more each try) until the AI map's line of sight to it is above 0.5.
    Vector3 FindPerch(const Vector3& from);
    // The frame's step: `now` is the animation time (frames).
    void Update(float now);
    // Follows object `objectId`: when it exists, a new figure at a perch near it (filed in the client's
    // maps) with its source, perched for 200 frames.
    void Follow(int objectId);
    int Unpack(int update, DeltaReader& field) override;    // 1
    void Pack(DeltaWriter&) override {}            // nothing is sent
    void Start(int) override {}

    Ref<MapObject> bird;      // the figure object (a stuff object, class 0x38, counted), null: none
    Ref<ParticleSource> source;    // null: none
    int followedId = -1;       // world object id, -1: none
    float lastTime = 0.0f;     // Update's last `now`
    float timer = 0.0f;        // frames left in the state
    float angle = 0.0f;        // circling: the angle at time 0 (radians)
    float height = 0.0f;       // circling: the height above the bird where it looks for a perch
    State state = State::Perched;   // FlyingOff: flying to the position (the perch)
};

// A tornado crossing the map (class id 0x65, "EffectTornado"; the server weather launches it). It moves in a
// straight line for framesLeft ticks, carries a particle source (0x200A) and the looping sound
// "nature\tornado\circle.wav"; on the server every 8th tick (while more than 20 remain) the units within 5
// take a hit of 5 (damage type 6). The source stops emitting for the last 110 ticks.
class EffectTornado final : public Effect {
public:
    const char* ClassName() override;
    ClassId GetClassId() override;
    EffectTornado();
    ~EffectTornado() override;
    void Reset() override;                                 // source cleared and released, sound stopped
    // From `from` towards `to` at a random speed 0.096 .. 0.216 per tick; the ticks to go are the
    // distance over the speed (rounded, at least 0; 0 when the points meet).
    void Launch(const Vector3& from, const Vector3& to);
    bool RunTick() override;                                   // 0 once the ticks are used up
    int Unpack(int update, DeltaReader& field) override;
    void Pack(DeltaWriter& field) override;
    void Start(int update) override;
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;

    Ref<ParticleSource> source;    // client only, null: none
    int sound = -1;            // client: the looping sound's handle, -1: none
    int framesLeft = 0;        // ticks still to move
    int launchTick = 0;        // the world's tick at Launch (Start catches the source up, at most 30)
};


