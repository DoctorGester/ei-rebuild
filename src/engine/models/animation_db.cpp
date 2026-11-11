// A figure's animation database: the .adb file's records (four old files' shorter entries read past their end as
// zeros) and the TOML over it, read and written by field (docs/notes/mods.md, "Records as TOML" and "Readable
// values"). See animation_db.h.
#include "engine/models/animation_db.h"

#include "engine/core/asset_text.h"
#include "engine/data/database_toml.h"
#include "engine/data/resource.h"

#include "lib/bytes.h"
#include "lib/str.h"
#include "lib/toml.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The header of "<figure>.adb"; `count` FigureAnimationRecords follow.
struct AdbHeader {
    char magic[4] = {};             // "ADB"
    std::uint32_t count = 0;
    char unreadName[0x18] = {};     // the database's name ("Zombie00")
    float start = 0.0f;
    float length = 0.0f;
    float end = 0.0f;
};
static_assert(sizeof(AdbHeader) == 0x2C);
constexpr bool NoPadding(const AdbHeader*) {
    return lib::FieldsFill<&AdbHeader::magic, &AdbHeader::count, &AdbHeader::unreadName, &AdbHeader::start,
                           &AdbHeader::length, &AdbHeader::end>();
}

// `out` from the file's bytes at `offset`; what lies beyond the file's `size` reads as zero.
template <class T>
void LoadWithinFile(T& out, const std::uint8_t* data, std::uint32_t size, std::size_t offset) {
    std::uint8_t bytes[sizeof(T)] = {};
    if (offset < size) std::memcpy(bytes, data + offset, std::min<std::size_t>(sizeof(T), size - offset));
    out = lib::Load<T>(bytes);
}

// The database of an .adb file. Four of the game's (unmozozo, unmoco3, unmoco4, inmm1) are of an older, shorter
// entry and end before `count` full entries: their entries are read at the current stride, the bytes past the file as
// zero (the original read past the file). The flags keep the bits the code reads (kAnimationFlagBits).
std::unique_ptr<AnimationDatabase> ReadAdbImage(const std::uint8_t* data, std::uint32_t size) {
    AdbHeader header;
    LoadWithinFile(header, data, size, 0);
    auto database = std::make_unique<AnimationDatabase>();
    database->unreadName.assign(header.unreadName, str::BoundedLength(header.unreadName, sizeof(header.unreadName)));
    database->start = header.start;
    database->length = header.length;
    database->end = header.end;
    database->animations.reserve(header.count);
    for (std::size_t i = 0; i < header.count; ++i) {
        FigureAnimationRecord record;
        LoadWithinFile(record, data, size, sizeof(AdbHeader) + i * sizeof(FigureAnimationRecord));
        record.flags &= kAnimationFlagBits;
        database->animations.push_back(FigureAnimation{record, database.get()});
    }
    return database;
}

// The name's bytes up to its terminator.
std::string_view BoundedName(const FigureAnimationRecord& record) {
    return std::string_view(record.name, str::BoundedLength(record.name, sizeof(record.name)));
}

} // namespace

// ---- .adb as TOML ----------------------------------------------------------------------------------------------

// An animation's arrow frames, [from, to] (left out when 0, 0); a list of numbers written without its trailing zeros
// (the footsteps' frames, the sound numbers; the rest read back zero).
struct ArrowFrames {
    std::uint32_t& from;
    std::uint32_t& to;
};
struct TrimmedNumbers {
    std::uint32_t (&values)[4];
};

std::string ValueText(ArrowFrames field) { return toml::Array({toml::Int(field.from), toml::Int(field.to)}); }
bool ReadValue(const toml::Value& value, ArrowFrames field) {
    std::uint32_t arrow[2];
    if (!toml::ReadArray(value, arrow, 2)) return false;
    field.from = arrow[0];
    field.to = arrow[1];
    return true;
}
bool IsUnset(ArrowFrames field) { return field.from == 0 && field.to == 0; }

std::string ValueText(TrimmedNumbers field) {
    int count = 4;
    while (count > 0 && field.values[count - 1] == 0) --count;
    std::vector<std::string> items;
    for (int i = 0; i < count; ++i) items.push_back(toml::Int(field.values[i]));
    return toml::Array(items);
}
bool ReadValue(const toml::Value& value, TrimmedNumbers field) { return dbtoml::ReadValue(value, field.values); }

// The key's and the flags' parts (animation_db.h; docs/notes/mods.md, "Readable values").
using dbtoml::Name;

constexpr Name kKinds[] = {
    {0, "none"}, {1, "special"}, {2, "attack"}, {3, "cast"}, {4, "run"},
    {5, "walk"}, {6, "idle"},    {7, "death"},  {8, "hit"},  {9, "cross"},
};
// The variant bits (0..14) are weapon bits: AnimationWeaponBit by weapon type, as the requests make them (the held
// weapon's type, the natural weapon); "any" the seven weapons. A type without a name is its number (7: the old short
// entries of unmoco3 and unmoco4 read at the current stride have it).
constexpr Name kWeapons[] = {
    {0x7F, "any"}, {0x1, "sword"}, {0x2, "axe"},       {0x4, "dagger"}, {0x8, "spear"},
    {0x10, "club"}, {0x20, "bow"}, {0x40, "crossbow"}, {0x100, "none"},
};
constexpr std::string_view kWeaponNumbers = "a weapon type number 0..14";
constexpr Name kMoves[] = {{1, "stop"}, {2, "walk"}, {4, "crawl"}, {6, "lie"}};   // AnimationMoveBits >> 15
constexpr Name kPlays[] = {{1, "once"}, {2, "looped"}};                           // AnimationKeyFlag >> 30
// A Special's number (SkillAnimation): the skills' and the interface's by name; another is the number a briefing
// phrase plays (ui_briefing_phrases.cpp: any number the phrase gives).
constexpr Name kSkills[] = {
    {0x1, "briefing01"}, {0x2, "briefing02"}, {0x3, "briefing03"}, {0x4, "briefing04"}, {0x5, "briefing05"},
    {0x6, "briefing06"}, {0x7, "briefing07"}, {0x8, "briefing08"}, {0x9, "briefing09"}, {0xA, "briefing10"},
    {0xB, "briefing11"}, {0xC, "briefing12"}, {0xD, "briefing13"}, {0x13, "tame"},      {0x14, "steal"},
    {0x15, "use"},       {0x16, "search_body_ranged"},             {0x21, "special14"},
    {0x30, "search_body_walking"},            {0x34, "search_body_walking_ranged"},       {0x35, "search_body"},
};
constexpr Name kHeights[] = {{1, "high"}, {2, "middle"}, {3, "low"}};                       // AttackHeight >> 24
constexpr Name kSides[] = {{1, "back"}, {2, "front"}, {3, "minus_x"}, {4, "plus_x"}};   // BlowSide >> 22
// A death's or hit's fall bits (bits 25, 26; UnitServer::BuildDeathAnimation): a hero hit while it runs
// (kKeyFallWalking), while it moves otherwise (kKeyFall: HitWhileMoving's fall), or no fall (kKeyDeath: a hit
// standing or turning, a death, a lying hero, every other unit).
constexpr Name kFalls[] = {{1, "running"}, {2, "moving"}, {3, "none"}};
// A walk's or run's number: a hero's legs' damage level (AnimationKeyFlag::DamageLevel1 / 2: the lowest leg slot's
// hit points at most ai.reg's [RPG] DamageLevel1Value / DamageLevel2Value; UnitServer::BuildMoveSteps,
// NextAnimation), the tired walk "cwalk08" and "cwalk06".
constexpr Name kLegs[] = {{1, "wounded"}, {2, "badly_wounded"}};
// An idle's number: every idle a unit asks for has number 1 (kAnimationSpecialIdleKey: FillIdle's, a client's special
// idle mode); no request has number 2, so such an entry never plays.
constexpr Name kIdles[] = {{1, "special"}, {2, "unused"}};
// A cross's number: the movement mode it goes into (EnterBits; `move` is the one it leaves): 1 a hero's own walk (Walk
// without onGuard, whose key has no move bits), 2 the guarded walk or another unit's.
constexpr Name kCrossTo[] = {{1, "hero_walk"}, {2, "walk"}, {3, "crawl"}, {4, "lie"}, {5, "stop"}};
// AnimationFlag: the bits the code reads (the others are cleared at the load).
constexpr Name kFlagBits[] = {
    {static_cast<std::int64_t>(AnimationFlag::Arrow), "arrow"},
    {static_cast<std::int64_t>(AnimationFlag::WithUnderArmour), "with_under_armour"},
    {static_cast<std::int64_t>(AnimationFlag::WithOuterArmour), "with_outer_armour"},
};
constexpr std::uint32_t kBlowBit = kAnimationAttackBlow;
constexpr std::uint32_t kHeightMask = 0x3000000;
constexpr std::uint32_t kFallMask = 0x6000000;

// The parts of the key's number (bits 22..29), which mean what the kind says: written as the parts the kind has
// when they hold the whole number, else as `number`; the reader takes each.
enum class NumberPart { Number, Skill, Height, Blow, Side, Fall, Legs, Idle, To };

struct KeyNumber {
    std::uint32_t& key;
    NumberPart part = NumberPart::Number;

    // The parts the key is written with.
    static bool ByParts(std::uint32_t key, NumberPart part) {
        const int number = AnimationNumber(key);
        switch (AnimationKindOf(key)) {
        case AnimationKind::Special: return part == NumberPart::Skill;
        case AnimationKind::Walk:
        case AnimationKind::Run: return part == NumberPart::Legs && !dbtoml::NameOf(kLegs, number).empty();
        case AnimationKind::Idle: return part == NumberPart::Idle && !dbtoml::NameOf(kIdles, number).empty();
        case AnimationKind::Cross: return part == NumberPart::To && !dbtoml::NameOf(kCrossTo, number).empty();
        case AnimationKind::Attack:
        case AnimationKind::Cast:
            return (part == NumberPart::Height || part == NumberPart::Blow) && (number & ~0xE) == 0;
        case AnimationKind::Death:
        case AnimationKind::Hit:
            return (part == NumberPart::Side || part == NumberPart::Fall) && (number & ~0x1F) == 0 && (number & 7) <= 4;
        default: return false;
        }
    }
    dbtoml::Code AsCode() const {
        switch (part) {
        case NumberPart::Skill: return {key, kAnimationNumberMask, kSkills};
        case NumberPart::Height: return {key, kHeightMask, kHeights};
        case NumberPart::Side: return {key, kBlowSideMask, kSides};
        case NumberPart::Fall: return {key, kFallMask, kFalls};
        case NumberPart::Legs: return {key, kAnimationNumberMask, kLegs};
        case NumberPart::Idle: return {key, kAnimationNumberMask, kIdles};
        case NumberPart::To: return {key, kAnimationNumberMask, kCrossTo};
        default: return {key, kAnimationNumberMask, {}};
        }
    }
    bool Skipped() const {
        if ((key & AsCode().mask) == 0 || (part == NumberPart::Blow && (key & kBlowBit) == 0)) return true;
        bool byParts = false;
        for (NumberPart other : {NumberPart::Skill, NumberPart::Height, NumberPart::Blow, NumberPart::Side,
                                 NumberPart::Fall, NumberPart::Legs, NumberPart::Idle, NumberPart::To})
            byParts = byParts || ByParts(key, other);
        return part == NumberPart::Number ? byParts : !ByParts(key, part);
    }
};

std::string ValueText(KeyNumber field) {
    if (field.part == NumberPart::Blow) return toml::Bool((field.key & kBlowBit) != 0);
    return dbtoml::ValueText(field.AsCode());
}
bool ReadValue(const toml::Value& value, KeyNumber field) {
    if (field.part != NumberPart::Blow) return dbtoml::ReadValue(value, field.AsCode());
    if (!value.IsBool()) return false;
    field.key = value.Bool() ? field.key | kBlowBit : field.key & ~kBlowBit;
    return true;
}
std::string ValidNames(const KeyNumber& field) { return dbtoml::ValidNames(field.AsCode()); }

template <class Visit>
void Fields(FigureAnimationRecord& r, Visit& v) {
    using dbtoml::Code;
    v("name", dbtoml::Chars<16>{r.name});
    v("index", r.index);
    v("kind", Code{r.key, kAnimationKindMask, kKinds});
    v("weapons", dbtoml::Bits{r.key, kWeapons, kAnimationVariantBits, 0, kWeaponNumbers});
    v("move", Code{r.key, kAnimationMoveMask, kMoves});
    v("play", Code{r.key, 0xC0000000u, kPlays});
    v("skill", KeyNumber{r.key, NumberPart::Skill});
    v("height", KeyNumber{r.key, NumberPart::Height});
    v("blow", KeyNumber{r.key, NumberPart::Blow});
    v("side", KeyNumber{r.key, NumberPart::Side});
    v("fall", KeyNumber{r.key, NumberPart::Fall});
    v("legs", KeyNumber{r.key, NumberPart::Legs});
    v("idle", KeyNumber{r.key, NumberPart::Idle});
    v("to", KeyNumber{r.key, NumberPart::To});
    v("number", KeyNumber{r.key, NumberPart::Number});
    v("flags", dbtoml::Bits{r.flags, kFlagBits});
    v("death_side", Code{r.flags, kBlowSideMask, kSides});
    v("chance", r.chance);
    v("frames", r.frameCount);
    v("stride", r.stride);
    v.Unread("arrow", ArrowFrames{r.arrowFrom, r.arrowTo});
    v("steps", TrimmedNumbers{r.stepFrames});
    v("moment", r.moment);
    v("sound_frame", r.soundFrame);
    v("sounds", TrimmedNumbers{r.sounds});
}

std::unique_ptr<AnimationDatabase> ReadAnimationDatabase(const RecordFile& file) {
    auto database = file.found ? ReadAdbImage(reinterpret_cast<const std::uint8_t*>(file.original.data()),
                                              static_cast<std::uint32_t>(file.original.size()))
                               : std::make_unique<AnimationDatabase>();
    AnimationDatabase* read = database.get();
    MergeTomls(file, [read](const toml::Value& root, const std::string& path) { read->MergeToml(root, path); });
    return database;
}

void AnimationDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    std::string name;
    if (toml::Read(root["name"], &name)) unreadName = text::ToAssetText(name);
    toml::Read(root["start"], &start);
    toml::Read(root["length"], &length);
    toml::Read(root["end"], &end);
    dbtoml::MergeRecords(
        root["animation"], animations, "name",
        [](const FigureAnimation& animation) { return std::string(BoundedName(animation)); }, ".adb", path,
        [this](FigureAnimation& animation, std::size_t index) {
            animation.database = this;
            animation.index = static_cast<std::uint8_t>(index);
        });
    dbtoml::CheckTables(root, {"name", "start", "length", "end", "animation"}, ".adb", path);
}

std::string AnimationDatabase::ToToml() const {
    toml::Writer writer;
    writer.Pair("name", toml::String(text::FromAssetText(unreadName)));
    writer.Pair("start", toml::Float(start));
    writer.Pair("length", toml::Float(length));
    writer.Pair("end", toml::Float(end));
    dbtoml::WriteRecords(writer, "animation", animations, "name", [](FigureAnimation& animation, std::size_t index) {
        animation.index = static_cast<std::uint8_t>(index);
    });
    return writer.text;
}
