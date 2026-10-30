#pragma once

// The 3D object base under MapObject: the object's figure (a model resource shared through the figure
// manager), its parts (scene nodes, one per figure piece) and their bounding box, and the scale the figure
// was built for. See docs/notes/map_objects.md ("Figures").

#include "core_transform.h"
#include "engine_object.h"
#include "figure_mesh.h"            // FigureMesh (an object's figure parts)
#include "map_object_animation.h"   // AnimationTrack (a bone's animator)
#include "maths.h"
#include "ref.h"
#include "unit_create.h"   // MapObjectType

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/nan_audit.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class Object3D;
class ParticleSource;
class SceneRenderer;

// The figures' default scale (zeros).
inline constexpr Vector3 kDefaultFigureScale = /*007AF1D0*/ {};
class LinkMesh;

// FigureAnimation::key, as the figures' animation databases (database.res, "<figure>.adb") give it, and the
// keys the units ask for (docs/notes/combat.md, "Animation keys"):
//   bits 0..7    the weapon types the animation is for (AnimationWeaponBit; 0x7F: any)
//   bits 15..17  the movement mode's bits (UnitServer::MoveModeMask; the data does not name them)
//   bits 18..21  the kind (AnimationKind)
//   bits 22..29  the number (AnimationNumber): a Special's SkillAnimation, a walk's or run's DamageLevel bits,
//                an attack's height band, a death's BlowSide and fall bits
//   bit 30       played once (AnimationKeyFlag::Once: the "s..." names, "scross13")
//   bit 31       looped (AnimationKeyFlag::Looped: the "c..." names, "cwalk01", "cidle"); "u..." names have neither

// The kind of an animation, bits 0x3C0000 of its key. Names from the databases' animation names (1.06's
// database.res: "uattack01", "crun02", "udeath12", ...); a few unnamed entries have kind None.
enum class AnimationKind : std::uint32_t {
    None = 0,
    Special = 0x40000,   // "uspecial", "ubriefing": the skills (SkillAnimation) and the briefings' gestures
    Attack = 0x80000,
    Cast = 0xC0000,
    Run = 0x100000,    // "crun", the once-played "scross" into a run, "cflight"
    Walk = 0x140000,   // "cwalk", "ccrawl", the once-played "scross" into a walk, "cflight"
    Idle = 0x180000,   // "cidle", "crest"
    Death = 0x1C0000,
    Hit = 0x200000,
    Cross = 0x240000,   // "ucross": the movement mode changes (LogicSettings::humanCrossAnimationSpeed)
};
inline constexpr std::uint32_t kAnimationKindMask = 0x3C0000;
constexpr AnimationKind AnimationKindOf(std::uint32_t key) {
    return static_cast<AnimationKind>(key & kAnimationKindMask);
}
constexpr std::uint32_t AnimationKeyOf(AnimationKind kind) { return static_cast<std::uint32_t>(kind); }

// The key's low bits (0..14): the variant bits (the weapon, movement and attack options an entry is made for);
// the bits above them must match a request exactly.
inline constexpr std::uint32_t kAnimationVariantBits = 0x7FFF;

// Which generator a random animation pick draws from: the game's rand() (the server's steps and idles), or the client
// effects' (g_clientRandom, random.h: the client's idle animations, picked as often as its draw time restarts them).
enum class AnimationRandom { Game, Client };

// The key's number field (bits 22..29).
inline constexpr int kAnimationNumberShift = 22;
inline constexpr std::uint32_t kAnimationNumberMask = 0x3FC00000;
constexpr int AnimationNumber(std::uint32_t key) { return static_cast<int>((key >> kAnimationNumberShift) & 0xFF); }
constexpr std::uint32_t AnimationNumberBits(int number) {
    return static_cast<std::uint32_t>(number) << kAnimationNumberShift;
}

// The weapon bit of weapon type `type` (the original's shift takes the count's low 5 bits).
constexpr std::uint32_t AnimationWeaponBit(int type) { return 1u << (type & 0x1F); }

// Flag bits of the key. DamageLevel1 / DamageLevel2 pick a hero's tired walk or run (LogicSettings'
// DamageLevel1Value / DamageLevel2Value against its load factor; "cwalk08" / "cwalk06"); they are the number
// field's low bits.
enum class AnimationKeyFlag : std::uint32_t {
    DamageLevel1 = 0x400000,
    DamageLevel2 = 0x800000,
    Once = 0x40000000,
    Looped = 0x80000000,
};
template <>
inline constexpr bool lib::kBitFlags<AnimationKeyFlag> = true;
constexpr std::uint32_t AnimationKeyOf(AnimationKeyFlag flag) { return static_cast<std::uint32_t>(flag); }

// The movement mode's bits of the key (mask 0x38000; UnitServer::MoveModeMask and the transitions' bits), named by
// the MoveModeState::mode they stand for. A hero's own walk (mode 2 without onGuard) has none.
enum class AnimationMoveBits : std::uint32_t {
    None = 0,
    Mode4 = 0x8000,
    Mode2 = 0x10000,
    Mode1 = 0x20000,
    Mode0 = 0x30000,    // standing still
};
inline constexpr std::uint32_t kAnimationMoveMask = 0x38000;
constexpr std::uint32_t AnimationKeyOf(AnimationMoveBits bits) { return static_cast<std::uint32_t>(bits); }

// An attack's height band (bits 24..25 of its key): where on the target's body the blow lands. The key's bit
// kAnimationAttackBlow is set on a blow with a target, on the casts and on a non-hero's attack.
enum class AttackHeight : std::uint32_t {
    High = 0x1000000,
    Middle = 0x2000000,
    Low = 0x3000000,
};
constexpr std::uint32_t AnimationKeyOf(AttackHeight height) { return static_cast<std::uint32_t>(height); }
inline constexpr std::uint32_t kAnimationAttackBlow = 0x800000;

// The idle a unit asks for while its mode is "special idle" (looped, kind Idle, number 1).
inline constexpr std::uint32_t kAnimationSpecialIdleKey =
    AnimationKeyOf(AnimationKeyFlag::Looped) | AnimationKeyOf(AnimationKind::Idle) | AnimationNumberBits(1);

// The numbers of the Special animations: the briefing gestures the interface plays and the skills (UnitServer's
// use orders, the skill sounds). Briefing and Special names are the animation databases'
// ("ubriefing01", "uspecial14"; the interface plays 1 to 13 and 0x21, a briefing script any number); the skills' names are from the use sources (UseSource) and the sounds (heroes\human\skills\*.wav); the
// four SearchBody animations ("loot.wav") differ by the movement mode (2 or another) and whether the weapon is
// ranged.
enum class SkillAnimation : int {
    Briefing01 = 0x1,
    Briefing02 = 0x2,
    Briefing03 = 0x3,
    Briefing04 = 0x4,
    Briefing05 = 0x5,
    Briefing06 = 0x6,
    Briefing07 = 0x7,
    Briefing08 = 0x8,
    Briefing09 = 0x9,
    Briefing10 = 0xA,
    Briefing11 = 0xB,
    Briefing12 = 0xC,
    Briefing13 = 0xD,
    Special14 = 0x21,   // the heroes' pose in the camp and the hero screen ("cspecial14" of the creatures)
    Tame = 0x13,    // "tame.wav"
    Steal = 0x14,   // "steal.wav"
    Use = 0x15,     // "science.wav"
    SearchBodyRanged = 0x16,
    SearchBodyMode2 = 0x30,
    SearchBodyMode2Ranged = 0x34,
    SearchBody = 0x35,
};

// The side a blow came from (UnitServer::HitDirection), bits 0x1C00000 of a death animation's flags (and of the
// key a unit asks for). In the unit's axes, whose Y points backwards; the death animation's point (Unit's action
// point) lies away from the blow: +Y for Front, -Y for Back, +X for MinusX, -X for PlusX. Which of X's sides is
// the unit's left the code does not say.
enum class BlowSide : std::uint32_t {
    None = 0,
    Back = 0x400000,
    Front = 0x800000,
    MinusX = 0xC00000,   // the attacker on the unit's -X side
    PlusX = 0x1000000,
};
inline constexpr std::uint32_t kBlowSideMask = 0x1C00000;
constexpr std::uint32_t AnimationKeyOf(BlowSide side) { return static_cast<std::uint32_t>(side); }

// One animation of a figure's animation database.
struct FigureAnimation {
    AnimationKind Kind() const { return AnimationKindOf(key); }
    int Number() const { return AnimationNumber(key); }
    BlowSide Side() const { return static_cast<BlowSide>(flags & kBlowSideMask); }

    char name[0x10];                // (the figure manager takes the entry as an animation's name)
    std::uint8_t index;             // its index in the database (a unit keeps it)
    std::uint8_t unused_11[3];
    std::uint32_t key;              // the animation key (above)
    std::uint32_t flags;            // bit 0: a bow's arrow shows from frame arrowFrom to arrowTo; a death's BlowSide
    std::uint32_t chance;           // percent (a random pick drops entries below a random percent)
    std::uint32_t frameCount;       // the animation's length in frames
    // The distance one play of the animation moves the unit (0: the speed comes from the unit instead).
    float stride;
    std::uint32_t arrowFrom;
    std::uint32_t arrowTo;
    std::uint32_t stepFrames[4];    // the footsteps' frames + 1 (0 ends the list)
    std::uint32_t moment;           // the action's moment (frame)
    int soundFrame;                 // the frame of the action's sound (0: none)
    std::uint32_t sounds[4];        // sound numbers to pick from (0 ends the list)
};
static_assert(offsetof(FigureAnimation, arrowFrom) == 0x28);
static_assert(offsetof(FigureAnimation, soundFrame) == 0x44);
static_assert(sizeof(FigureAnimation) == 0x58);

// A figure's animation database: the data of "<figure name>.adb" as the object's manager returns it
// (BaseManager::GetFile), kept in Object3D::animations.
struct AnimationDatabase {
    std::uint8_t unused_00[4];
    std::uint32_t count;            // animations
    std::uint8_t unused_08[0x20 - 0x08];
    float start;                    // \ the animation time factor maps t to
    float length;                   // | (start + (end - start) * t) / length
    float end;                      // /
    FigureAnimation animations[1];  // `count` of them

    // The database `animation` is an entry of (entries know their index).
    static const AnimationDatabase* Of(const FigureAnimation* animation) {
        const FigureAnimation* first = animation - animation->index;
        return reinterpret_cast<const AnimationDatabase*>(reinterpret_cast<const char*>(first) -
                                                          offsetof(AnimationDatabase, animations));
    }
    // (start + (end - start) * t) / length; 1 for a length of 0.
    float TimeFactor(float t) const {
        EI_NAN_AUDIT(length);
        if (length == 0.0f) return 1.0f;
        return ((end - start) * t + start) / length;
    }
};
static_assert(offsetof(AnimationDatabase, start) == 0x20);
static_assert(offsetof(AnimationDatabase, animations) == 0x2C);

// The side-specific half of a 3D object (Object3D::attached): Object3DClientSpecific (class id 0x37) on
// the client, Object3DServer (0x38, stubs) on the server.
class Object3DSpecific;
class Object3DClientSpecific;
class Object3DSpecific : public EngineObject {
public:
    Object3DSpecific() {
        object = nullptr;
    }
    ~Object3DSpecific() override;

    virtual int OnPartAdded(FigureMesh* part) = 0;   // server: 0
    // (name): the client takes the part named (FindPart) out of the drawn part sets and releases it; returns it
    // or null. The server returns null.
    virtual FigureMesh* RemoveFromPartSets(const char* name) = 0;
    virtual int OnPartRemoved(FigureMesh* part) = 0;      // server: 0
    virtual FigureMesh* FindPart(std::string_view name) = 0;   // a part by name (client); server: null
    virtual FigureMesh* FindPartOfType(int type) = 0;     // a part by type (client); server: null
    // The figure's extra scale (the base's: 1, 1, 1).
    virtual Vector3 GetScale();
    // The client draws the parts / their shadow; the base returns 0.
    virtual int DrawParts(SceneRenderer* context, Rect* dirty);
    virtual int DrawPartsShadow(SceneRenderer* context);
    // After LoadFigures: the client gives the parts the world's materials, then the skin texture. The base does
    // nothing.
    virtual void ApplyMaterials() {}
    // Screen picking: whether the point (x, y) is in the object's dirtyRect (PickAt).
    virtual int BoundsContainPoint(const std::int32_t* point);
    // Whether the screen rectangle (a RECT) meets a drawn part's rectangle.
    virtual int PartsIntersectRect(const Rect* rect);
    // Whether the point is in the screen rectangle of a part with flag 0x40 (PickAt2).
    virtual int PartsContainPoint(const std::int32_t* point);
    virtual void Unused25(int) {}                   // empty in the base and the client
    virtual void SetLit(bool) {}                     // the object's light is on / off (client); base: nothing

    Object3D* object;              // the object it belongs to
};

// The server's half: no parts, nothing drawn.
class Object3DServer : public Object3DSpecific {
public:
    Object3DServer();             // The factory allocates it zeroed.
    ~Object3DServer() override;
    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    int OnPartAdded(FigureMesh* part) override;
    FigureMesh* RemoveFromPartSets(const char* name) override;
    int OnPartRemoved(FigureMesh* part) override;
    FigureMesh* FindPart(std::string_view name) override;
    FigureMesh* FindPartOfType(int type) override;
};

// A bone of a figure's skeleton (made from the skeleton links). The animation state's bone list holds them,
// one of its members the root. Only the fields the pose code uses.
struct FigureBone : EngineObject {
    std::string name;                      // the piece's name (children are linked by it)
    std::string name2;                     // its parent's name (empty for the root)
    std::string name3;                     // the figure's model name (LinkBone)
    FigureBone* parent;                     // null for the root
    std::vector<FigureBone*> children;
    FigureMesh* mesh;                      // the part the bone places, or null
    AnimationTrack* pose;                   // the animator whose values place the bone
    AnimationTrack* animator;               // counted, made on first use; Play copies it to `pose`
    Vector3 offset;                         // the place relative to the parent
    float morphS;                           // the figure's morph coefficients
    float morphT;
    float morphU;                           // (also the time factor's t)
    Vector3 position;                       // in figure space
    Quaternion rotation;                    // in figure space
    std::vector<Matrix4> matrices;    // (emptied without freeing; freed by the destructor)

    FigureBone();
    ~FigureBone() override;

    void Create() override;
    void Destroy() override;
    const char* GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void CreateBone();
    void DestroyBone();

    // Places this bone and, recursively, its children: a child turns its offset by the parent's
    // rotation ((w^2 - u.u) o + 2w (u x o) + 2 (u.o) u, the scaled terms rounded) onto the parent's position and
    // takes rotation = pose * parent (Hamilton); the root takes the pose's position (times the animation's time
    // factor at morphU while one plays) minus `origin`, and its rotation; a root without a pose keeps both. A bone
    // with a mesh gives it the position and rotation and the pose's morphed vertices as offsets.
    void Place(const Vector3& origin);
    // The bone's cage (map_object_cage.cpp): `offset` becomes the point at (morphS, morphT, morphU) in the cube of
    // the next 8 points of *stream (0x60 bytes, consumed; edges 0-1 and 2-3 at morphS, between them at morphT;
    // edges 4-5 and 6-7 likewise; then between the two at morphU) or in the prism of the next 6 (0x48 bytes; edge
    // 0-1 at morphS, towards point 2 at morphT; edge 3-4 likewise towards point 5; then between the two at morphU).
    void InterpolateCube(const float** stream);
    void InterpolatePrism(const float** stream);
};

// The figure's animation state (Object3D's animation member): the skeleton's bones, the animation playing
// and the queue behind it, the frame reached.
class FigureAnimationState : public EngineObject {
public:
    // Plays `animation` from its start: set through the world's figure manager by the owner's model name, or
    // else on each bone's animator (AnimateBone).
    void Play(FigureAnimation* animation);
    // Queues `animation` after the one playing.
    void Queue(FigureAnimation* animation);
    // The frame reached becomes `frame` (`changed` set when it differs): with nothing playing the next queued
    // animation starts at 0; while the frame lies beyond the animation's frame count the count is taken off,
    // the callback runs, and the next queued animation plays, or else the animation holds its last frame
    // unless its key's top bits are 10 (loops). When the animation changed, it is set as in Play.
    void SetTime(float frame);
    void Advance(float frames);

    // Each bone's animator plays `animation`: the bone's manager is told and the animator's time cleared.
    static void AnimateBone(FigureBone* bone, const FigureAnimation* animation);

    FigureAnimationState();
    ~FigureAnimationState() override;

    void Create() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void DestroyState();
    void ReleaseBones();
    void Reset();
    // The first bone named `boneName` (_mbscmp), or null.
    FigureBone* FindBone(std::string_view boneName);
    // A new bone (the figure manager's class 0x300C) named `child` with parent `parentName` joins through
    // LinkBone. Returns 1.
    int LinkPiece(FigureModel* model, std::string_view child, std::string_view parentName);
    // `bone` becomes a child of the first bone named as its parent (name2) unless that one has a child of its
    // name; it joins the list (referenced), takes the model name (name3) and morph, and its manager loads its
    // key frames from `model`. `changed` set. Returns 1.
    int LinkBone(FigureModel* model, FigureBone* bone);
    // The bone named as `part` carries it as its mesh (referenced, the old one released): the part takes the
    // bone's position and rotation. 0 when no bone has the name, else 1. `changed` set.
    int AttachMesh(EngineObject* part);
    // The root becomes the first bone without a parent name. 1, or 0 when none.
    int FindRoot();
    // The figure manager links the skeleton of `model` into this state; `changed` set. Returns its result.
    int SetModel(std::string_view model);
    // The first bone carrying `part` lets it go (released). 1, or 0 when none.
    int DetachMesh(EngineObject* part);
    // Every bone lets its mesh go (released). `changed` set. Returns 1.
    int DetachMeshes();
    // When `changed`: cleared, and the root (if any) places the skeleton at the owner's time factor (at its scale
    // z) times its default scale.
    void PlaceIfChanged();
    // When `changed`: each bone's placing animator is evaluated at the frame reached then PlaceIfChanged.
    void UpdatePose();
    // The morph becomes (s, t, u) on every bone, whose manager reloads its key frames from the owner's model,
    // and on the state (`changed` cleared); then the root places the skeleton as PlaceIfChanged does.
    void Rescale(float s, float t, float u);
    // Every bone's name.
    std::vector<std::string> BoneNames();
    // The figure manager's SetAnimation without an animation. Every bone's placing track forgets its animation and
    // key frames (DestroyTrack's reset, the bone kept). `changed` set. Returns 1.
    int ClearBoneTracks();
    // The figure manager's SetAnimation with an animation. Every bone's placing track loads the entry of `part`
    // named as the bone (LoadKeyFrames), or without one is cleared as above, and takes `animation`. `changed` set.
    // Returns 1.
    int LoadBoneTracks(MappedResource* part, const FigureAnimation* animation);

    Object3D* owner;
    FigureBone* root;               // the skeleton's root bone (FindRoot)
    std::vector<FigureBone*> bones;   // LinkBone adds
    FigureAnimation* current;       // the animation playing, or null
    // The animations queued after it (the client update keeps fewer than 3).
    std::deque<FigureAnimation*> queue;
    float time;                     // the frame reached
    // Called each time an animation's end is passed, or null.
    void(*onEnd)(Object3D* owner);
    float morphS;                   // the figure's morph coefficients (Rescale; bones copy them)
    float morphT;
    float morphU;
    std::uint8_t changed;           // set when the frame, the animation or the bones changed
};

// The 3D object: a transform object with a figure, its parts and their animation (abstract: class name and
// id are pure). WorldClient's CollectVisible sets its world position to the position in view space.
class Object3D : public TransformObject {
public:
    ~Object3D() override;
    // type as the map object factory's type.
    MapObjectType MapType() const { return static_cast<MapObjectType>(type); }
    void Create() override { Create3D(); }
    void Destroy() override { Destroy3D(); }
    void ClearState() override { RemoveAllEffectSources(); }
    // worldRotation = parent * localRotation.
    void CombineRotation(const Quaternion& parent) override;
    void SaveTransform(platform::File* file) override;
    void LoadTransform(platform::File* file) override;
    // Whether the object may play the animation entry (FigureAnimation); any here, Unit overrides it.
    virtual bool AcceptsAnimation(const FigureAnimation* animation);
    // AttachSpecific. MapObject and units override it.
    virtual void AttachObject(EngineObject* object);
    // Takes `other`'s parts (the array copied, no references taken) and its transform
    // (TransformObject::CopyTransform).
    virtual void CopyFigure(const Object3D* other);
    // Whether the object is invisible: false here; map objects override it.
    virtual bool InvisibleSlot();
    // Draw the figure / draw it and collect its screen rectangle / draw its shadow with the scene context (the
    // side half's draw virtuals, the second first emptying dirtyRect to have it filled; 1 without a half).
    virtual int DrawFigure(SceneRenderer* context);
    virtual int DrawCollectDirty(SceneRenderer* context);
    virtual int DrawFigureShadow(SceneRenderer* context);
    // Empty; units override it.
    virtual void AdvanceTime(float time);
    // Loads the model named by the object's name into the figure manager; non-units mark every part and themselves
    // (flags 8 / 0x200). Then the half's LoadFigures-done virtual and flags 0x80. Nothing when already loaded.
    virtual void LoadFigures();

    // The bounding box of the parts (only those within 1e9 squared of the object) into the bounds: centre, box
    // corners relative to it, radius; sets two flag bits. Nothing changes without parts.
    void ComputeBounds();
    // The bounds, the bytes after them and the eye height zeroed.
    void ClearBounds();

    // Sets the scale and, when it changed, rebuilds the figure at that scale through the figure manager, copies
    // each part's piece record from it, rescales the animation and recomputes the bounds.
    void SetFigureScale(const float* newScale);

    // The first part whose name starts with `prefix` (case-sensitive), or null.
    FigureMesh* FindPartByPrefix(std::string_view prefix);
    // The first part named exactly `partName`, or null.
    FigureMesh* FindPart(std::string_view partName);
    // The first part whose engine object type is `type`, or null.
    FigureMesh* FindPartOfType(int type);
    // Every part whose name starts with `prefix`.
    std::vector<FigureMesh*> CollectPartsByPrefix(std::string_view prefix);
    // Takes `part` out of the parts (it forgets its owner), tells the animation state and releases it. The side
    // half hears of it first, found or not. Returns 1 when it was there, else 0.
    int RemovePart(FigureMesh* part);
    // FindPart(`partName`), removed by RemovePart. Returns the part (released; the pointer may be dangling)
    // or null.
    FigureMesh* RemovePartNamed(std::string_view partName);
    // `part`'s position turned by the object's rotation, scaled by the side half's GetScale and moved to the
    // object's position.
    float* PartWorldPosition(float* out, const FigureMesh* part);

    // The animation database, loaded on first use through the owner's GetFile ("<name>.adb"; nothing without
    // an owner). Null when there is none.
    AnimationDatabase* Animations();
    // Animation `key & 0xFF` of the database, or null (no database, or beyond its count).
    FigureAnimation* AnimationAt(std::uint32_t key);
    // The first animation of kind Special whose number (key bits 22-29) is `number`, or null.
    FigureAnimation* FindSkillAnimation(SkillAnimation number);
    // A random animation for `key`: the database's entries whose key has the same bits above kAnimationVariantBits and all
    // of key's low 15 bits (none when key has none) and that AcceptsAnimation accepts; one is returned as it
    // is, several are narrowed to those whose chance is at least a random percent and one of them is picked
    // (the pick made before narrowing when none is left); the draws from `random`'s generator. Null without a
    // database or a match.
    FigureAnimation* RandomAnimation(std::uint32_t key, AnimationRandom random = AnimationRandom::Game);
    // As RandomAnimation for the entries whose key's bits above kAnimationVariantBits equal key's, one picked from
    // `random`'s generator; no chance narrowing.
    FigureAnimation* RandomAnimationOfKind(std::uint32_t key, AnimationRandom random = AnimationRandom::Game);
    // For units (class id 0x50) with an animation database whose length is neither 0 nor NaN:
    // (start + (end - start) * time) / length, else 1. Unrounded.
    float AnimationFactor(float time);
    // Sets the figure up for the animated model `model`: one part box and part name per animated part (the
    // figure manager's bounds of the part, its pivot, flags 4 | 0x400 "BASE", 0x90 "CROWN", 0x890 "EMPTY" by
    // name prefix); the bounds span the boxes (heroes, Hero: x and y -0.5..0.5).
    void SetUpAnimatedFigure(std::string_view model);
    // A part object for the figure part `partName` (the figure manager's LoadPart; "Can't load figure" logged
    // and null when missing). With a figure the part must be one of its pieces (else null, the part leaked)
    // and takes its box; box flags by name prefix as SetUpAnimatedFigure. It joins the parts.
    FigureMesh* LoadPart(std::string_view partName);
    // The object's figure becomes `model` and each name of `partList` is loaded by LoadPart
    // (failures logged). Without a figure manager: FatalError. Returns 1.
    int LoadParts(std::string_view model, const std::vector<std::string>* partList);
    // When a unit becomes the controlled one (world client): with the figure loaded (flags 0x80) the parts'
    // world transforms are updated and the bounds recomputed from their world boxes.
    void UpdatePartBounds();
    // Builds the figure of the model for the part names `partList` (none: the animation
    // state's parts and the whole figure), then LoadParts and the bounds; flags 0x80 set unless both `arg`
    // and the figure are 0. Returns 1.
    int MakeFigure(std::string_view model, std::vector<std::string>* partList, std::uint32_t arg);
    // The index of the part name equal to `partName` (_mbscmp), or -1.
    int FindPartIndex(std::string_view partName);
    // The box (FigureMesh::bounds) of part `index`.
    FootprintPart* PartBox(int index);

    // Create (named apart so it is no override): back to an empty object, the figure released.
    void Create3D();
    // Destroy: the figure, the effect sources, the animation state, the parts and the side half let go (the
    // parts lose a reference without a release).
    void Destroy3D();
    // Adds `source` (a particle source; it calls this from its Attach) to the effect sources unless it is there:
    // referenced and attached to this object.
    void AddEffectSource(ParticleSource* source);
    // Takes `source` out of the effect sources, detaches it and releases it. Nothing when not there.
    void RemoveEffectSource(ParticleSource* source);

    // A side half (SideObjectType::Object3D) replaces the attached one as SetSpecific does; other objects (and null) are
    // ignored.
    void AttachSpecific(EngineObject* half);
    // The release-effects virtual: every effect source is taken out of the list, detached and released.
    void RemoveAllEffectSources();

    // `half` (of any type, or null) replaces the attached half: the old one is detached and released, the new
    // one points back here and gains a reference.
    void SetSpecific(Object3DSpecific* half);
    // Every part's flags gains / loses the bits of `mask`.
    void SetPartFlags(std::uint32_t mask);
    void ClearPartFlags(std::uint32_t mask);
    // Every part's flatColour / flatSpecular becomes `value`.
    void SetPartsFlatColour(std::uint32_t value);
    void SetPartsFlatSpecular(std::uint32_t value);
    // `state` (or null) replaces the animation state: the old one loses its owner and is released, the new
    // one gains a reference and this owner.
    void SetAnimationState(FigureAnimationState* state);
    // The animation state (if any) plays `animation`.
    void PlayAnimation(FigureAnimation* animation);
    // The animation state's bone named as `part` carries it (AttachMesh); 0 without a state.
    int AttachPartMesh(EngineObject* part);
    // The animation state (if any) goes to frame 0 and places the skeleton.
    void RestartPose();

    // The same half on the client (defined in map_object_shadow.h).
    Object3DClientSpecific* ClientSpecific() const;

    Matrix4 worldMatrix;            // object -> world: the rotation, the position as column 3
    Object3DSpecific* attached;    // the side half: AttachSpecific accepts (SideObjectType::Object3D)
    FigureAnimationState* animation;   // the figure's animation state
    AnimationDatabase* animations;  // loaded on first use
    std::string name;               // the figure's name
    std::vector<HeldRef<ParticleSource>> effectSources;   // the particle sources attached to the object
    Vector3 scale;
    // the figure's parts (counted; CopyFigure shares them uncounted, Destroy3D drops them without a release)
    std::vector<HeldRef<FigureMesh>> parts;
    // the figure (counted; deleted at its last reference), made by the figure cache: the parts' piece headers
    // (LoadPart, SetFigureScale) and the meshes the parts draw and shadow through.
    Ref<LinkMesh> figure;
    Vector3 defaultScale;           // kDefaultFigureScale (constructor, Create3D)
    Rect dirtyRect;              // the screen area the last DrawCollectDirty covered
    std::vector<std::string> partNames;    // the figure's part names, in the order of the part boxes
    ClipBounds bounds;              // flags 1 and 2: the sphere and the box are valid; the box is relative to the centre
    // Cleared with the bounds; a part's piece record sets it (LoadPart). The AI adds it to a unit's eye height.
    float eyeHeight;
    std::vector<FootprintPart> partBoxes;   // one box per animated part (SetUpAnimatedFigure); FootprintPart: model.h

protected:
    Object3D();
};
