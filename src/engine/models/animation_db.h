#pragma once

// A figure's animation database ("<figure>.adb" in database.res, or TOML over it): its animations with their keys,
// lengths, moments and sounds, and the parts of the key the game asks by. See docs/notes/map_objects.md ("Skeleton")
// and docs/notes/mods.md ("Records as TOML").

#include "lib/bit_flags.h"
#include "lib/bytes.h"   // FieldsFill

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

// The key's number field (bits 22..29).
inline constexpr int kAnimationNumberShift = 22;
inline constexpr std::uint32_t kAnimationNumberMask = 0x3FC00000;
constexpr int AnimationNumber(std::uint32_t key) { return static_cast<int>((key >> kAnimationNumberShift) & 0xFF); }
constexpr std::uint32_t AnimationNumberBits(int number) {
    return static_cast<std::uint32_t>(number) << kAnimationNumberShift;
}

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
// keeps it), kept in FigureInstance::animations.
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
