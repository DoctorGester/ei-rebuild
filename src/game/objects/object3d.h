#pragma once

// The 3D object base under MapObject: the object's figure (a model resource shared through the figure
// manager), its parts (scene nodes, one per figure piece) and their bounding box, and the scale the figure
// was built for. See docs/notes/map_objects.md ("Figures").

#include "engine/core/core_transform.h"
#include "engine/scene/drawn_motion.h"
#include "engine/core/engine_object.h"
#include "engine/models/figure_mesh.h"            // FigureMesh (an object's figure parts)
#include "game/items/item_db.h"                // WeaponType
#include "engine/models/animation.h"   // AnimationTrack (a bone's animator)
#include "lib/vecmath.h"
#include "engine/core/ref.h"
#include "game/objects/map_object_manager.h"   // MapObjectType

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class Object3D;
class ParticleSource;
class SceneRenderer;

// The figures' default scale (zeros).
inline constexpr Vector3 kDefaultFigureScale = /*007AF1D0*/ {};
// The figure parts of a hero, in the game's order; "hr" is the hair ("hr.%.2d" with the prototype's number).
inline constexpr int kHeroPartCount = 16;
inline constexpr const char* kHeroFigureParts[kHeroPartCount] = {
    "hd", "bd", "hp", "hr", "lh1", "lh2", "lh3", "rh1", "rh2", "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3"};
class LinkMesh;
struct FigureModel;
struct RecordFile;
namespace toml {
class Value;
}

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
inline constexpr std::uint32_t kAnimationKindBits = ~kAnimationVariantBits;

// Whether an entry made for `entryKey` answers a request for `key`: the bits above the variant bits are equal,
// and the request's variant bits all lie in the entry's (none asked: an entry without any).
constexpr bool AnimationKeyFits(std::uint32_t entryKey, std::uint32_t key) {
    const std::uint32_t wanted = key & kAnimationVariantBits;
    const bool variantFits = wanted == 0 ? (entryKey & kAnimationVariantBits) == 0 : (~entryKey & wanted) == 0;
    return variantFits && ((entryKey ^ key) & kAnimationKindBits) == 0;
}

// A figure's part counts as near when its squared distance from the figure's origin is at most this (farther parts
// are skipped by the bounds and the draw).
inline constexpr float kPartsNearSquared = 1e9f;

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
constexpr std::uint32_t AnimationWeaponBit(WeaponType type) { return 1u << (static_cast<int>(type) & 0x1F); }

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
// the MoveMode they stand for. A hero's own walk (Walk without onGuard) has none.
enum class AnimationMoveBits : std::uint32_t {
    None = 0,
    Stop = 0x8000,
    Walk = 0x10000,
    Crawl = 0x20000,
    Lie = 0x30000,
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

// The bits of an animation's flags that the code reads. The .adb files set others too (1..3, 15..21, 25, 26: most
// entries repeat bits of their key there), which have no reader: the load clears them (kAnimationFlagBits).
enum class AnimationFlag : std::uint32_t {
    Arrow = 0x1,             // a bow's arrow shows from frame arrowFrom to arrowTo (UnitClientSpecific::UpdateArrow)
    // A hero plays the animation only if it has the bit for each armour group it wears (Unit::AcceptsAnimation): a
    // shirt, pants, boots or gloves (HasOtherArmour), a helmet, body armour or leggings (HasMainArmour).
    WithUnderArmour = 0x10,
    WithOuterArmour = 0x20,
};
template <>
inline constexpr bool lib::kBitFlags<AnimationFlag> = true;
inline constexpr std::uint32_t kAnimationFlagBits =
    static_cast<std::uint32_t>(AnimationFlag::Arrow | AnimationFlag::WithUnderArmour | AnimationFlag::WithOuterArmour) |
    kBlowSideMask;

// One animation of a figure's animation database as "<figure>.adb" holds it (MapObjectManager::AnimationDatabaseOf
// reads the file).
struct FigureAnimationRecord {
    AnimationKind Kind() const { return AnimationKindOf(key); }
    int Number() const { return AnimationNumber(key); }
    BlowSide Side() const { return static_cast<BlowSide>(flags & kBlowSideMask); }

    char name[0x10] = {};           // (the figure manager takes the entry as an animation's name)
    std::uint8_t index = 0;         // its index in the database (a unit keeps it)
    std::uint8_t unused_11[3] = {};
    std::uint32_t key = 0;          // the animation key (above)
    std::uint32_t flags = 0;        // AnimationFlag bits and a death's BlowSide (kAnimationFlagBits)
    std::uint32_t chance = 0;       // percent (a random pick drops entries below a random percent)
    std::uint32_t frameCount = 0;   // the animation's length in frames
    // The distance one play of the animation moves the unit (0: the speed comes from the unit instead).
    float stride = 0.0f;
    std::uint32_t arrowFrom = 0;
    std::uint32_t arrowTo = 0;
    std::uint32_t stepFrames[4] = {};  // the footsteps' frames + 1 (0 ends the list)
    std::uint32_t moment = 0;       // the action's moment (frame)
    int soundFrame = 0;             // the frame of the action's sound (0: none)
    std::uint32_t sounds[4] = {};   // sound numbers to pick from (0 ends the list)
};
static_assert(offsetof(FigureAnimationRecord, arrowFrom) == 0x28);
static_assert(offsetof(FigureAnimationRecord, soundFrame) == 0x44);
static_assert(sizeof(FigureAnimationRecord) == 0x58);
constexpr bool NoPadding(const FigureAnimationRecord*) {
    return lib::FieldsFill<&FigureAnimationRecord::name, &FigureAnimationRecord::index,
                           &FigureAnimationRecord::unused_11, &FigureAnimationRecord::key,
                           &FigureAnimationRecord::flags, &FigureAnimationRecord::chance,
                           &FigureAnimationRecord::frameCount, &FigureAnimationRecord::stride,
                           &FigureAnimationRecord::arrowFrom, &FigureAnimationRecord::arrowTo,
                           &FigureAnimationRecord::stepFrames, &FigureAnimationRecord::moment,
                           &FigureAnimationRecord::soundFrame, &FigureAnimationRecord::sounds>();
}

struct AnimationDatabase;

// An animation: an entry of its database. What an object plays need not come from its own database (the rest
// pose of UiModel and of the main menu's stones comes from MapObjectManager::FindAnimation).
struct FigureAnimation : FigureAnimationRecord {
    const AnimationDatabase* database = nullptr;  // the database it is an entry of
};

// A figure's animation database, read from "<figure name>.adb" by MapObjectManager::AnimationDatabaseOf (which
// keeps it), kept in Object3D::animations.
struct AnimationDatabase {
    AnimationDatabase() = default;
    AnimationDatabase(const AnimationDatabase&) = delete;   // the entries point back at it
    AnimationDatabase& operator=(const AnimationDatabase&) = delete;

    // (start + (end - start) * t) / length; 1 for a length of 0.
    float TimeFactor(float t) const {
        if (length == 0.0f) return 1.0f;
        return ((end - start) * t + start) / length;
    }

    // TOML (docs/notes/mods.md, "Records as TOML"): name, start, length, end and [[animation]] tables (name, index,
    // the key's and the flags' parts by name ("Readable values"), chance, frames, stride, arrow = [from, to], steps,
    // moment, sound_frame, sounds; a missing number is 0, a missing index the record's place). Merge: the fields given over the header's; an animation replaces
    // the first of its name (not one this file replaced already), or is appended; an unknown key is logged (with
    // `path`).
    void MergeToml(const toml::Value& root, const std::string& path);
    std::string ToToml() const;

    std::string unreadName;         // the header's ("Zombie00")
    float start = 0.0f;             // \ the animation time factor maps t to
    float length = 0.0f;            // | (start + (end - start) * t) / length
    float end = 0.0f;               // /
    std::vector<FigureAnimation> animations;
};
// The database of a figure's .adb `file`: the original's, then each TOML file over it (AnimationDatabase::MergeToml).
std::unique_ptr<AnimationDatabase> ReadAnimationDatabase(const RecordFile& file);

// The side-specific half of a 3D object (Object3D::attached): Object3DClientSpecific (class id 0x37) on
// the client, Object3DServer (0x38, stubs) on the server.
class Object3DSpecific;
class Object3DClientSpecific;
class Object3DSpecific : public EngineObject {
public:
    // This half as the client's: null on the server.
    virtual Object3DClientSpecific* ClientSide() { return nullptr; }
    ~Object3DSpecific() override;

    virtual int OnPartAdded(FigureMesh* part) = 0;   // server: 0
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
    // Screen picking: whether the point (x, y) is in the object's dirtyRect (Pick with HitTest::Bounds).
    virtual int BoundsContainPoint(const std::int32_t* point);
    // Whether the screen rectangle (a RECT) meets a drawn part's rectangle.
    virtual int PartsIntersectRect(const Rect* rect);
    // Whether the point is in the screen rectangle of a part with flag 0x40 (Pick with HitTest::Parts).
    virtual int PartsContainPoint(const std::int32_t* point);
    // The depth Pick sorts a hit by: the view depth of the object's nearest drawn surface under the point, else of
    // its origin as drawn. The base (nothing drawn): FLT_MAX.
    virtual float PickDepth(const Point& point);
    virtual void SetLit(bool) {}                     // the object's light is on / off (client); base: nothing

    Object3D* object = nullptr;    // the object it belongs to
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
    int OnPartRemoved(FigureMesh* part) override;
    FigureMesh* FindPart(std::string_view name) override;
    FigureMesh* FindPartOfType(int type) override;
};

// A bone of a figure's skeleton (made from the skeleton links). The animation state's bone list holds them,
// one of its members the root. Only the fields the pose code uses.
struct FigureBone : EngineObject {
    std::string name;                      // the piece's name (children are linked by it)
    std::string parentName;                // its parent's name (empty for the root)
    std::string modelName;                 // the figure's model name (LinkBone)
    FigureBone* parent = nullptr;           // null for the root
    std::vector<FigureBone*> children;
    Ref<FigureMesh> mesh;                   // the part the bone places, or null
    AnimationTrack* pose = nullptr;         // the animator whose values place the bone
    Ref<AnimationTrack> animator;           // made on first use; Play copies it to `pose`
    Vector3 offset;                         // the place relative to the parent
    float morphS = 0.0f;                    // the figure's morph coefficients
    float morphT = 0.0f;
    float morphU = 0.0f;                    // (also the time factor's t)
    Vector3 position;                       // in figure space
    Quaternion rotation;                    // in figure space
    std::vector<Matrix4> matrices;    // (emptied without freeing; freed by the destructor)
    // The blend from one animation into the next (FigureAnimationState::BeginBlend): the bone's own turn and
    // (the root) its position before the origin as last placed (`shown` once placed); the outgoing pose a switch
    // blends from (`blendFrom` while it has one: the mesh's offsets then too) and the blended offsets.
    Quaternion shownTurn;
    Vector3 shownPosition;
    bool shown = false;
    Quaternion fromTurn;
    Vector3 fromPosition;
    std::vector<Vector3> fromOffsets;
    bool blendFrom = false;
    std::vector<Vector3> blendedOffsets;

    FigureBone();
    ~FigureBone() override;

    // The names "none"; no parent, children, mesh, animator or pose; the default place.
    void Create() override;
    // The mesh and the animator released, then Create.
    void Destroy() override;
    std::string_view GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Places this bone and, recursively, its children: a child turns its offset by the parent's
    // rotation ((w^2 - u.u) o + 2w (u x o) + 2 (u.o) u, the scaled terms rounded) onto the parent's position and
    // takes rotation = pose * parent (Hamilton); the root takes the pose's position (times the animation's time
    // factor at morphU while one plays) minus `origin`, and its rotation; a bone without a pose keeps both. A bone
    // with a mesh gives it the position and rotation and the pose's morphed vertices as offsets. With `blend`
    // below 1 a bone with an outgoing pose takes that much of the animator's (its turn slerped, the root's
    // position and the offsets lerped); at 1 the animator's alone.
    void Place(const Vector3& origin, float blend);
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
    // The bones released, then Reset.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void Reset();
    // The first bone named `boneName` (_mbscmp), or null.
    FigureBone* FindBone(std::string_view boneName);
    // A new bone (the figure manager's class 0x300C) named `child` with parent `parentName` joins through
    // LinkBone.
    void LinkPiece(FigureModel* model, std::string_view child, std::string_view parentName);
    // `bone` becomes a child of the first bone named as its parent (parentName) unless that one has a child of its
    // name; it joins the list (referenced), takes the model name and morph, and its manager loads its
    // key frames from `model`. `changed` set.
    void LinkBone(FigureModel* model, FigureBone* bone);
    // The bone named as `part` carries it as its mesh (referenced, the old one released): the part takes the
    // bone's position and rotation. 0 when no bone has the name, else 1. `changed` set.
    int AttachMesh(FigureMesh* part);
    // The root becomes the first bone without a parent name. 1, or 0 when none.
    int FindRoot();
    // The figure manager links the skeleton of `model` into this state; `changed` set. Returns its result.
    int SetModel(std::string_view model);
    // The first bone carrying `part` lets it go (released). 1, or 0 when none.
    int DetachMesh(FigureMesh* part);
    // Every bone lets its mesh go (released). `changed` set.
    void DetachMeshes();
    // When `changed`: cleared, and the root (if any) places the skeleton at the owner's time factor (at its scale
    // z) times its default scale.
    void PlaceIfChanged();
    // When `changed`: each bone's placing animator is evaluated at the frame reached then PlaceIfChanged. While a
    // blend runs the skeleton is placed again at its weight now, changed or not.
    void UpdatePose();
    // UpdatePose for a draw (the world's, a shadow's, the interface's): the pose placed is the one shown.
    void PlaceForDraw();
    // The morph becomes (s, t, u) on every bone, whose manager reloads its key frames from the owner's model,
    // and on the state (`changed` cleared); then the root places the skeleton as PlaceIfChanged does.
    void Rescale(float s, float t, float u);
    // Every bone's name.
    std::vector<std::string> BoneNames();
    // The figure manager's SetAnimation without an animation. Every bone's placing track forgets its animation and
    // key frames (ClearKeys). `changed` set.
    void ClearBoneTracks();
    // The figure manager's SetAnimation with an animation. Every bone's placing track loads the entry of `part`
    // named as the bone (LoadKeyFrames), or without one is cleared as above, and takes `animation`. `changed` set.
    void LoadBoneTracks(MappedResource* part, const FigureAnimation* animation);

    Object3D* owner = nullptr;
    FigureBone* root = nullptr;     // the skeleton's root bone (FindRoot)
    std::vector<HeldRef<FigureBone>> bones;   // LinkBone adds
    FigureAnimation* current = nullptr;  // the animation playing, or null
    // The animations queued after it (the client update keeps fewer than 3).
    std::deque<FigureAnimation*> queue;
    float time = 0.0f;              // the frame reached
    // Called each time an animation's end is passed, or null.
    void(*onEnd)(Object3D* owner) = nullptr;
    float morphS = 0.0f;            // the figure's morph coefficients (Rescale; bones copy them)
    float morphT = 0.0f;
    float morphU = 0.0f;
    bool changed = true;            // set when the frame, the animation or the bones changed

    // A switch from one animation to another blends the pose last placed into the new one's over a short time
    // (client side only: what plays and its frame stay the game's). The blend runs from `placedAt`, when the
    // outgoing pose was placed (the draw before the switch), so a draw a blend's length after it shows the new
    // pose alone, as at the harness's default frames. In the platform's milliseconds; `blendLength` 0 while none
    // runs.
    std::uint32_t blendStart = 0;
    std::uint32_t blendLength = 0;
    std::uint32_t placedAt = 0;
    // A switch blends only from a pose the player saw, in a figure seen moving: `poseShown` while the pose last
    // placed has been drawn (PlaceForDraw), `seenMoving` once a placement replaced a drawn pose. So a figure's first
    // shown pose is its current animation's, never a blend from the rest pose its building placed (the user's Lost
    // in Astral load: the HUD's hero swung out of a T pose) or from a pose shown frozen before its world ran (the
    // main menu after a game: its creatures' idles, drawn in the paused world before the server's first tick, blended
    // into their walks).
    bool poseShown = false;
    bool seenMoving = false;
    // Before a switch from `from` to `to`: each bone keeps the pose last placed as the outgoing one and the blend
    // starts, as long as `to`'s kind asks (none when either is null or the figure was not seen as above; one running
    // is cut short).
    void BeginBlend(const FigureAnimation* from, const FigureAnimation* to);
    // The incoming animation's share of the pose now (eased; 1 once the blend is over, which ends it).
    float BlendWeight();
    // The harness's seam (its "animblend off"): switches snap, as in the original, while off.
    static void SetBlends(bool on);
    // The harness's count ("animblend count"): the blends begun so far in world figures (not the interface's
    // previews, MapObjectFlag::Preview).
    static std::uint32_t WorldBlendsBegun();
};

// The 3D object: a transform object with a figure, its parts and their animation (abstract: class name and
// id are pure). WorldClient's CollectVisible sets its world position to the position in view space.
class Object3D : public TransformObject {
public:
    ~Object3D() override;
    // type as the map object factory's type.
    MapObjectType MapType() const { return static_cast<MapObjectType>(type); }
    // Back to an empty object: no half, flags, figure or bounds.
    void Create() override;
    // The figure, the effect sources, the animation state, the parts and the side half let go.
    void Destroy() override;
    void ClearState() override { RemoveAllEffectSources(); }
    // Whether the object may play the animation entry (FigureAnimation); any here, Unit overrides it.
    virtual bool AcceptsAnimation(const FigureAnimation* animation);
    // A side half (SideObjectType::Object3D) becomes the attached one (SetHalf); other objects (and null) are
    // ignored. MapObject and units override it.
    virtual void AttachObject(EngineObject* object);
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
    void SetFigureScale(const Vector3& newScale);

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
    Vector3 PartWorldPosition(const FigureMesh* part);

    // The animation database, loaded on first use from the owner (AnimationDatabaseOf "<name>"; nothing without
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
    // (start + (end - start) * time) / length, else 1.
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
    // (failures logged). Without a figure manager the game ends. Returns 1.
    int LoadParts(std::string_view model, const std::vector<std::string>& partList);
    // When a unit becomes the controlled one (world client): with the figure loaded (flags 0x80) the parts'
    // world transforms are updated and the bounds recomputed from their world boxes.
    void UpdatePartBounds();
    // Builds the figure of the model for the part names `partList` (none: the animation
    // state's parts and the whole figure), then LoadParts and the bounds; flags 0x80 set.
    void MakeFigure(std::string_view model, std::vector<std::string>* partList);
    // The index of the part name equal to `partName` (_mbscmp), or -1.
    int FindPartIndex(std::string_view partName);
    // The box (FigureMesh::bounds) of part `index`.
    FootprintPart* PartBox(int index);

    // Adds `source` (a particle source; it calls this from its Attach, attached to this object already) to the
    // effect sources unless it is there: referenced. (The game calls Attach(this) back, which then does nothing.)
    void AddEffectSource(ParticleSource* source);
    // Takes `source` out of the effect sources, detaches it and releases it. Nothing when not there.
    void RemoveEffectSource(ParticleSource* source);

    // The release-effects virtual: every effect source is taken out of the list, detached and released.
    void RemoveAllEffectSources();

    // `half` (or null) replaces the attached half: the old one is detached and released, the new one points back
    // here and gains a reference.
    void SetHalf(Object3DSpecific* half);
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
    int AttachPartMesh(FigureMesh* part);
    // The animation state (if any) goes to frame 0 and places the skeleton.
    void RestartPose();

    // The same half on the client (defined in map_object_shadow.h).
    Object3DClientSpecific* ClientSpecific() const;

    Matrix4 worldMatrix;            // object -> world: the rotation, the position as column 3
    Ref<Object3DSpecific> attached;    // the side half (SetHalf)
    Ref<FigureAnimationState> animation;   // the figure's animation state (SetAnimationState)
    AnimationDatabase* animations = nullptr;  // loaded on first use
    std::string name;               // the figure's name
    std::vector<HeldRef<ParticleSource>> effectSources;   // the particle sources attached to the object
    Vector3 scale;
    // the figure's parts, each holding a reference (released by RemovePart and Destroy)
    std::vector<HeldRef<FigureMesh>> parts;
    // the figure (counted; deleted at its last reference), made by the figure cache: the parts' piece headers
    // (LoadPart, SetFigureScale) and the meshes the parts draw and shadow through.
    Ref<LinkMesh> figure;
    Vector3 defaultScale;           // kDefaultFigureScale (constructor, Create)
    Rect dirtyRect;              // the screen area the last DrawCollectDirty covered
    std::vector<std::string> partNames;    // the figure's part names, in the order of the part boxes
    ClipBounds bounds;              // flags 1 and 2: the sphere and the box are valid; the box is relative to the centre
    // Cleared with the bounds; a part's piece record sets it (LoadPart). The AI adds it to a unit's eye height.
    float eyeHeight = 0.0f;
    std::vector<FootprintPart> partBoxes;   // one box per animated part (SetUpAnimatedFigure); FootprintPart: pipeline.h
    drawn::ObjectStep drawnStep;    // client: its last tick's step as the draw spreads it (drawn_motion.h)

protected:
    Object3D();
};
