// The map object manager ("map objects", class id 0x2D): its lifetime, the objects it makes complete with
// their halves, the animation database lookup, the object factory, the creature builders (vitals and statistics
// from the prototype records, the prototype applied to a map object) and a new unit's starting equipment. See
// map_object_manager.h and docs/notes/units.md ("Creation, databases, save").
#include "game/objects/map_object_manager.h"

#include "game/core/base_manager.h"
#include "game/core/core_maths.h"
#include "game/models/figure_manager.h"   // FigureObjectType
#include "game/app/singletons.h"
#include "game/app/logic_settings.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/lever.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/torch.h"
#include "game/objects/traps.h"
#include "game/data/resource_manager.h"
#include "game/core/asset_text.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "game/data/resource.h"
#include "game/data/database_toml.h"
#include "lib/toml.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kClassName = "CMapObjectManager";
constexpr const char* kNoDatabaseText = "Can't find animation database file for \"%s\".\n";

// The third half an object of type `type` gets (the world's specific manager makes the client's halves: the 3D
// object's, the map object's and the unit's; the server's theirs), or null for none.
std::optional<SideObjectType> ThirdHalfOf(MapObjectType type) {
    if (IsUnitType(type)) return SideObjectType::Unit;
    if (type == MapObjectType::Torch) return SideObjectType::Torch;
    return std::nullopt;
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

MapObjectManager::MapObjectManager() {}

MapObjectManager::~MapObjectManager() {}

const char* MapObjectManager::ClassName() { return kClassName; }
ClassId MapObjectManager::GetClassId() { return ClassId::MapObjectManager; }

void MapObjectManager::Create() {
    std::string folder = g_textFolder;
    BaseManager::Create();
    AddArchive((folder + "database.res"));
}

MapObject* MapObjectManager::FindMapObject(unsigned objectId) const { return FindObjectAs<MapObject>(objectId); }

EngineObject* MapObjectManager::CreateObject(int newType, int newId) {
    ResourceManager* manager = this;
    const auto kind = static_cast<MapObjectType>(newType);
    auto* object = static_cast<MapObject*>(BaseManager::CreateObject(newType, newId));
    if (!object) return nullptr;
    FigureManager* figures = manager->context->GetFigureManager();
    object->SetAnimationState(figures->CreateObjectAs<FigureAnimationState>(FigureObjectType::AnimationState, kNoObjectId));
    BaseManager* halves = manager->context->SpecificManager();
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::Object3D, kNoObjectId));
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::MapObject, kNoObjectId));
    if (auto half = ThirdHalfOf(kind)) object->AttachObject(halves->CreateObjectAs<EngineObject>(*half, kNoObjectId));
    return object;
}

void MapObjectManager::Destroy() {
    ResourceManager::Destroy();
    animationDatabases.clear();
}

// --- Animation databases --------------------------------------------------------------------------

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

// The key's and the flags' parts (object3d.h; docs/notes/mods.md, "Readable values").
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

AnimationDatabase* MapObjectManager::AnimationDatabaseOf(const std::string& figure) {
    std::string file = figure + ".adb";
    if (auto found = animationDatabases.find(file); found != animationDatabases.end()) return found->second.get();
    const RecordFile record = FindRecord(file);
    if (!record.Any()) return nullptr;
    return animationDatabases.emplace(std::move(file), ReadAnimationDatabase(record)).first->second.get();
}

FigureAnimation* MapObjectManager::FindAnimation(const std::string& figure, std::uint32_t key) {
    AnimationDatabase* database = AnimationDatabaseOf(figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, figure);
        return nullptr;
    }
    for (FigureAnimation& entry : database->animations)
        if (AnimationKeyFits(entry.key, key)) return &entry;
    return nullptr;
}

FigureAnimation* MapObjectManager::FindFigureAnimation(const std::string& figure, std::string_view name) {
    AnimationDatabase* database = AnimationDatabaseOf(figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, figure);
        return nullptr;
    }
    for (FigureAnimation& entry : database->animations)
        if (text::CompareIgnoringCase(entry.name, name) == 0) return &entry;
    return nullptr;
}

void MapObjectManager::GiveDefaults(MapObject* object) {
    // The vitals and statistics start zeroed, so the fields Reset and SetDefaults leave (the shares and factors,
    // fixedMaxHitPoints, builtFlags, the layers' spare values, ...) are 0, not what the stack held: a determinism
    // fix. The original's were WorldServer::CreateObject's earlier callees' leftovers, heap addresses among them,
    // and gave the main menu's unmoco2 (the one unit added without a prototype) hit points that followed the
    // heap.
    UnitVitals vitals{};
    vitals.Reset();
    if (object->GetClassId() != ClassId::Unit) {
        SetDefaults(&vitals, nullptr);
        object->SetServerData(&vitals);
    } else {
        UnitStats stats{};
        stats.Reset();
        SetDefaults(&vitals, &stats);
        object->SetStats(&stats);
    }
    object->ScaleEffectChanged(false);
    object->prototypeId = 0;
}

// --- Creature builders ---------------------------------------------------------------------------

namespace {

constexpr float kThird = 0.33333334f;
constexpr float kStat25 = 25.0f;
constexpr float kRate15 = 15.0f;
constexpr float kDefaultStat = 50.0f;
constexpr float kDefaultModeSpeed = 0.13333334f;
constexpr float kDefaultSpeed = 0.2f;
constexpr float kDefaultRateBase = 1.0f;
constexpr float kDefaultFullLevel = 100.0f;   // protection, perception, conspicuity and a slot's capacity at their start
constexpr float kDefaultVitalPool = 1000.0f;   // hit points and mana of a unit without stats
constexpr float kDefaultRegen = 10.0f;
constexpr int kSlotWorking = 3;

// A dword copied as its bits.
template <typename To, typename From>
void CopyDword(To& to, const From& from) {
    to = std::bit_cast<To>(from);
}

std::uint8_t RoundToByte(float value) { return static_cast<std::uint8_t>(lib::RoundEven(value)); }

const UnitPrototypeRecord* FindPrototype(std::string_view name) { return g_unitDatabase.prototypes.FindByName(name); }
const UnitNpcRecord* FindNpc(std::string_view name) { return g_unitDatabase.npcs.FindByName(name); }

// The prototype's model.
const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees.
float HalfAngleCosine(const UnitModelRecord* model) {
    float half = model->angle * 0.5f;
    return lib::Cos(half * ei::kRadiansPerDegree);
}

float RightAngleCosine() { return lib::Cos(ei::kRadiansPerDegree * 90.0f); }

void SetAttributesAndSkills(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitNpcRecord* npc) {
    if (npc) {
        static_assert(offsetof(UnitNpcRecord, skills) - offsetof(UnitNpcRecord, attributes) == 3);
        std::memcpy(stats->attributes, npc->attributes, 3 + 8);   // the attributes and the skills
        return;
    }
    for (int i = 0; i < 3; ++i) stats->attributes[i] = RoundToByte(prototype->skillLevel);
    for (int i = 0; i < 8; ++i) stats->skills[i] = RoundToByte(prototype->skillLevel);
    stats->attributes[0] = RoundToByte(prototype->attributes01);
    stats->attributes[1] = RoundToByte(prototype->attributes01);
    stats->attributes[2] = RoundToByte(prototype->attribute2);
    // Truncated as _ftol does (the low half of the 64-bit integer; 0 for a NaN).
    for (int i = 0; i < 3; ++i)
        stats->skills[3 + i] = static_cast<std::uint8_t>(static_cast<std::int64_t>(prototype->skills3to5[i]));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record and no layers; the others unused.
void SetSlots(UnitStats* stats, const UnitModelRecord* model) {
    for (int i = 0; i < 6; ++i) {
        const UnitModelSlot& entry = model->slots[i];
        UnitStatSlot& slot = stats->slots[i];
        float share = entry.share;
        if (share == 0.0f) {
            slot.kind = 0;
            continue;
        }
        const UnitLevelRecord* level = unitdb::ModelLevelAt(static_cast<std::uint32_t>(entry.levelIndex));
        slot.type = level->slotType;
        CopyDword(slot.share, entry.share);
        CopyDword(slot.factor, entry.factor);
        slot.vitalMask = level->vitalMask;
        slot.layers[0].layerMask = 0;
        slot.layers[0].state = 0;
        slot.layers[1].state = 0;
        slot.layers[2].state = 0;
        slot.kind = kSlotWorking;
    }
}

void SetCombat(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitModelRecord* model) {
    for (int i = 0; i < 5; ++i) CopyDword(stats->combat[i], prototype->combat[i]);
    CopyDword(stats->damage, prototype->damage);
    CopyDword(stats->damageSpread, prototype->damageSpread);
    static_assert(sizeof(stats->damageWeights) == sizeof(model->damageWeights));
    std::memcpy(stats->damageWeights, model->damageWeights, sizeof(stats->damageWeights));
    CopyDword(stats->naturalWeapon, prototype->naturalWeapon);
}

void LearnNpcSkills(UnitStats* stats, const UnitNpcRecord* npc) {
    stats->spentExperience = 0.0f;
    if (!npc) return;
    stats->AddScaledExperience(npc->experience);
    for (const auto& name : npc->skillNames) {
        ::SkillLevelRecord* skill = g_skillDatabase.FindLevel(name);
        if (skill) stats->LearnSkill(&g_skillDatabase, skill);
    }
    stats->spentExperience = stats->experience;
}

struct FactoryEntry {
    MapObjectType type = {};
    EngineObject* (*create)() = nullptr;
};
// The class's entry: its id and a new object of it.
template <class T>
constexpr FactoryEntry Make(MapObjectType type) {
    return {type, [] () -> EngineObject* { return new T(); }};
}
constexpr FactoryEntry kFactory[] = {
    Make<Unit>(MapObjectType::Hero),
    Make<Unit>(MapObjectType::Creature),
    Make<Unit>(MapObjectType::UnitSpare),
    Make<CTree>(MapObjectType::Tree),
    Make<CBuilding>(MapObjectType::Building),
    Make<CStone>(MapObjectType::Stone),
    Make<CStuffObject>(MapObjectType::StuffObject),
    Make<TorchObject>(MapObjectType::Torch),
    Make<MagicTrap>(MapObjectType::MagicTrap),
    Make<Lever>(MapObjectType::Lever),
    Make<FigureAnimationState>(MapObjectType::AnimationState),
    Make<TrapObject>(MapObjectType::Trap),
};

std::uint32_t Count(const std::vector<std::string>& names) { return static_cast<std::uint32_t>(names.size()); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const std::string& name) {
    return str::Compare(name,
                           "none") != 0 &&
           static_cast<int>(name.size()) != 0;
}

// The weapon named `name`; a warning when it cannot be made (null is returned then).
Item* MakeWeapon(ItemManager* items, const std::string& name, const std::string& prototype) {
    Item* weapon = items->CreateItem(name);
    if (!weapon) game::Warning("Can't create weapon \"%s\" for unit \"%s\"", name, prototype);
    return weapon;
}

} // namespace

// --- The factory ----------------------------------------------------------------------------------

EngineObject* MapObjectManager::NewObject(int objectType) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.type != static_cast<MapObjectType>(objectType)) continue;
        return entry.create();
    }
    return nullptr;
}

// --- Defaults and builders ------------------------------------------------------------------------

void MapObjectManager::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
    if (!stats) {
        vitals->hitPoints = kDefaultVitalPool;
        vitals->maxHitPoints = kDefaultVitalPool;
        vitals->hitPointRegen = kDefaultRegen;
        vitals->mana = kDefaultVitalPool;
        vitals->maxMana = kDefaultVitalPool;
        vitals->manaRegen = kDefaultRegen;
        for (UnitStatPair& protection : vitals->protection) {
            protection.current = kDefaultFullLevel;
            protection.base = kDefaultFullLevel;
        }
        return;
    }
    stats->experience = 0.0f;
    stats->strengthBase = kDefaultStat;
    stats->dexterityBase = kDefaultStat;
    stats->intelligenceBase = kDefaultStat;
    for (float& modeSpeed : stats->modeSpeeds) modeSpeed = kDefaultModeSpeed;
    stats->spentExperience = 0.0f;
    stats->speedBase = kDefaultSpeed;
    stats->rateBase = kDefaultRateBase;
    stats->capacity = 0.0f;
    stats->movementKind = 0;
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = kDefaultFullLevel;
        stats->perception[i].current = kDefaultFullLevel;
        stats->conspicuity[i].base = kDefaultFullLevel;
        stats->conspicuity[i].current = kDefaultFullLevel;
    }
    for (UnitStatSlot& slot : stats->slots) {
        slot.type = 1;
        slot.vitalMask = 0;
        slot.kind = kSlotWorking;
        slot.current = kDefaultFullLevel;
        slot.maximum = kDefaultFullLevel;
        for (UnitStatLayer& layer : slot.layers) {
            layer.layerMask = 0;
            layer.state = 0;
            layer.reserved[0] = 0;
            layer.reserved[1] = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
}

int MapObjectManager::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, std::string_view name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        SetDefaults(vitals, stats);
        return 1;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!vitals || !stats) return 0;
    // The vitals start zeroed: the fields their reset and this builder leave are 0, not what the caller's stack
    // held (a determinism fix; the harness zeroes them in the original too). The statistics' reset zeroes all.
    std::memset(vitals, 0, sizeof(UnitVitals));
    vitals->Reset();
    stats->Reset();

    vitals->maxHitPoints = prototype->hitPoints;
    vitals->hitPoints = prototype->hitPoints;
    CopyDword(vitals->hitPointRegen, model->hitPointRegen);
    vitals->maxMana = prototype->mana;
    vitals->mana = prototype->mana;
    CopyDword(vitals->manaRegen, model->manaRegen);
    stats->fixedMaxHitPoints = vitals->maxHitPoints;

    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        float rate = stats->dexterity * 0.5f;
        CopyDword(stats->speedBase, prototype->speed);
        stats->rateBase = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->rateBase = kRate15;
        CopyDword(stats->speedBase, prototype->speed);
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        CopyDword(stats->perception[i].base, prototype->perception[i]);
        CopyDword(stats->conspicuity[i].base, prototype->conspicuity[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;

    for (int i = 0; i < 7; ++i) {
        float value = model->protection[i] * prototype->protectionScale;
        vitals->protection[i].base = value;
        vitals->protection[i].current = value;
    }
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    return 1;
}

void BuildNpcStats(UnitStats* stats, const std::string& name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!prototype) game::FatalExit("Can't find prototype \"%s\" for new HERO.\n", name);
    const UnitModelRecord* model = ModelOf(prototype);
    if (!npc) game::DebugLog("Can't find NPC prototype \"%s\" for new HERO.\n", name);
    stats->Reset();

    const float* speed = &prototype->speed;
    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        stats->speedBase = *speed;
        stats->speed = *speed;
        float rate = stats->dexterity * 0.5f;
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->speedBase = *speed;
        stats->speed = *speed;
        stats->rateBase = kRate15;
        stats->rate = kRate15;
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = prototype->perception[i];
        stats->perception[i].current = prototype->perception[i];
        stats->conspicuity[i].base = prototype->conspicuity[i];
        stats->conspicuity[i].current = prototype->conspicuity[i];
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    stats->ResetToBase(1.0f);
    stats->RefillSlots();
    stats->ResetToBase(1.0f);
}

MapObjectType PrototypeObjectClass(std::string_view name) {
    constexpr MapObjectType kDefaultClass = MapObjectType::Creature;
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) return kDefaultClass;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return static_cast<MapObjectType>(model->objectClass);
}

// --- The prototype applied to a map object --------------------------------------------------------

void MapObjectManager::ApplyPrototype(MapObject* object, const std::string& name) {
    UnitVitals vitals{};   // zeroed (the stack's old contents are not kept)
    UnitStats stats{};
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        vitals.Reset();
        if (object->GetClassId() != ClassId::Unit) {
            SetDefaults(&vitals, nullptr);
            object->SetServerData(&vitals);
        } else {
            stats.Reset();
            SetDefaults(&vitals, &stats);
            object->SetStats(&stats);
        }
        object->ScaleEffectChanged(false);
        object->prototypeId = 0;
        game::DebugLog("Can't find prototype \"%s\" for object %d.\n", name, object->id);
        return;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    if (object->GetClassId() == ClassId::Unit) {
        BuildFromPrototype(&vitals, &stats, name);
        stats.builtFlags |= 1;
        vitals.builtFlags |= 1;
        object->MapObject::SetServerData(&vitals);
        object->SetStats(&stats);
        float lift = -model->height;

        Object3D* figure = object;
        figure->defaultScale.x = 0.0f;
        figure->defaultScale.y = lift;
        figure->defaultScale.z = 0.0f;
        Vector3 scale;
        static_assert(sizeof(scale) == sizeof(prototype->scale));
        scale = prototype->scale;
        object->SetFigureScale(scale);
        object->baseScale = scale;
        object->ApplyBaseScale();
        UnitOrNull(object)->SetSecondPrototype(prototype);
    }
    UnitPrototypeRecord* record = g_unitDatabase.prototypes.FindByName(name);
    object->prototypeId = record ? static_cast<std::uint32_t>(record - g_unitDatabase.prototypes.records.data()) : 0xFFFFFFFFu;
}

// --- Starting equipment ---------------------------------------------------------------------------

// A new unit's starting equipment from its prototype (the world adds zone units with it after
// ApplyPrototype). See objects/map_object_manager.h and docs/notes/units.md ("Creation helpers").

int MapObjectManager::GiveStartingEquipment(Unit* unit, const std::string& name) {
    SpellManager* spellOwner = context->GetSpellManager();   // passed on to the spell factory
    ItemManager* items = context->ItemFactory();
    auto* prototype = g_unitDatabase.prototypes.FindByName(name);
    if (!prototype) {   // a map's unit of a prototype the units database lacks (Lost in Astral's gz8g, gz17g)
        game::Warning("Can't find prototype \"%s\" for unit %d: no starting equipment", name, unit->id);
        return 0;
    }
    if (unit->MapType() == MapObjectType::Hero) {
        // The loop bounds are read again on every pass.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            Item* armour = items->CreateItem(prototype->armourNames.begin()[i]);
            if (armour) unit->EquipArmour(armour);
            else game::Warning("Can't create armor \"%s\" for unit \"%s\"", prototype->armourNames.begin()[i], name);
        }
        if (Named(prototype->weaponName)) {
            int slot = unit->AddWeapon(MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->pendingWeapon = slot;
        }
        if (Named(prototype->secondWeaponName))
            unit->AddWeapon(MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        Item* spell = items->CreateSpellItem(prototype->spellNames.begin()[i], spellOwner);
        if (spell) unit->AddSpell(spell);
        else game::Warning("Can't create spell \"%s\" for unit \"%s\"", prototype->spellNames.begin()[i], name);
    }
    return 1;
}

// --- References by id -----------------------------------------------------------------------------

template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, World* world) {
    const int id = stream->ReadId();
    if (id == kNoObjectId) {
        ref.Reset();
        return;
    }
    auto* manager = world->GetMapObjectManager();
    ref.Assign(manager->FindObjectAs<T>(static_cast<unsigned>(id)));
}

// The references loaded: the AI's map objects and units, the effects' figures.
template void LoadRef(Ref<MapObject>&, ByteStream*, World*);
template void LoadRef(Ref<Unit>&, ByteStream*, World*);
