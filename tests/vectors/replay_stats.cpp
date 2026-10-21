// Replay of the "stats" family (stats_vectors.h) against src/game/unit_stats.cpp and the other sources
// stats_vectors.h names. Doubles and
// words are compared bitwise, as the verify wrappers compare them.
#include "stats_vectors.h"
#include "vectors.h"

#include "../../src/game/logic_settings.h"
#include "../../src/game/random.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit_db.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

// The two damage word functions are not declared in a header (unit_stats.cpp).
std::uint32_t __fastcall ExcessDamage(const std::uint16_t* damage, void*, const std::uint16_t* protection);
std::uint16_t __fastcall MaxDamage(const std::uint16_t* damage);
int __fastcall SchoolPrice(int from, int to);   // unit_skill_price.cpp

namespace {

using namespace ei::vectors;
using namespace ei::vectors::stats;

// A UnitStats holding the view's fields (the rest zero).
UnitStats& StatsOf(const StatView& view) {
    static UnitStats stats;   // 0x704 bytes
    std::memset(&stats, 0, sizeof(stats));
    stats.stat10 = view.stat10;
    stats.stat18 = view.stat18;
    stats.stat20 = view.stat20;
    std::memcpy(stats.attributes, view.attributes, sizeof(view.attributes));
    std::memcpy(stats.skills, view.skills, sizeof(view.skills));
    for (int i = 0; i < 6; ++i) {
        stats.slots[i].kind = view.slots[i].kind;
        stats.slots[i].current = view.slots[i].current;
        stats.slots[i].maximum = view.slots[i].maximum;
        stats.slots[i].factor = view.slots[i].factor;
    }
    return stats;
}

bool Fail(char* detail, std::size_t size, const char* name, int arg, double ours, double original) {
    std::snprintf(detail, size, "%s(%d): ours %.17g, original %.17g", name, arg, ours, original);
    return false;
}

bool CheckPeriod20(const IndexCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).Period20();
    return Same(result, v.result) || Fail(d, n, "Period20", 0, result, v.result);
}
EI_VECTOR_CHECK("UnitStats::Period20", IndexCall, CheckPeriod20);

bool CheckAttributeBase(const IndexCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).AttributeBase(v.arg);
    return Same(result, v.result) || Fail(d, n, "AttributeBase", v.arg, result, v.result);
}
EI_VECTOR_CHECK("UnitStats::AttributeBase", IndexCall, CheckAttributeBase);

bool CheckAttributeWithBonus(const IndexCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).AttributeWithBonus(v.arg);
    return Same(result, v.result) || Fail(d, n, "AttributeWithBonus", v.arg, result, v.result);
}
EI_VECTOR_CHECK("UnitStats::AttributeWithBonus", IndexCall, CheckAttributeWithBonus);

bool CheckSkillBase(const IndexCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).SkillBase(v.arg);
    return Same(result, v.result) || Fail(d, n, "SkillBase", v.arg, result, v.result);
}
EI_VECTOR_CHECK("UnitStats::SkillBase", IndexCall, CheckSkillBase);

bool CheckWeaponSkill(const IndexCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).WeaponSkill(v.arg);
    return Same(result, v.result) || Fail(d, n, "WeaponSkill", v.arg, result, v.result);
}
EI_VECTOR_CHECK("UnitStats::WeaponSkill", IndexCall, CheckWeaponSkill);

bool CheckReduce(const ReduceCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).Reduce(v.value);
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "Reduce(%g): ours %.17g, original %.17g", v.value, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::Reduce", ReduceCall, CheckReduce);

bool CheckReduceKind1(const ReduceCall& v, char* d, std::size_t n) {
    double result = StatsOf(v.stats).ReduceKind1(v.value);
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "ReduceKind1(%g): ours %.17g, original %.17g", v.value, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::ReduceKind1", ReduceCall, CheckReduceKind1);

bool CheckOpens(const OpensCall& v, char* d, std::size_t n) {
    LockKey key = v.key;
    BOOL result = key.Opens(&v.lock);
    if (result == v.result) return true;
    std::snprintf(d, n, "key %X/%u/%u lock %X/%u/%u: ours %d, original %d", v.key.mask, v.key.id, v.key.level,
                  v.lock.mask, v.lock.id, v.lock.level, result, v.result);
    return false;
}
EI_VECTOR_CHECK("LockKey::Opens", OpensCall, CheckOpens);

bool CheckResetDamage(const ResetDamageCall& v, char* d, std::size_t n) {
    DamageInfo info;
    std::memset(&info, 0xCD, sizeof(info));
    std::uint32_t result = ResetDamageInfo(&info);
    if (result == v.result && Same(info, v.after)) return true;
    std::snprintf(d, n, "ours result %08X, original %08X; record %s", result, v.result,
                  Same(info, v.after) ? "same" : "differs");
    return false;
}
EI_VECTOR_CHECK("ResetDamageInfo", ResetDamageCall, CheckResetDamage);

bool CheckElemental(const ElementalCall& v, char* d, std::size_t n) {
    DamageInfo info;
    std::memset(&info, 0, sizeof(info));
    for (int i = 0; i < 3; ++i) info.typeWeights[3 + i] = v.weights[i];
    BOOL result = HasElementalDamage(&info);
    if (result == v.result) return true;
    std::snprintf(d, n, "weights %g %g %g: ours %d, original %d", v.weights[0], v.weights[1], v.weights[2], result,
                  v.result);
    return false;
}
EI_VECTOR_CHECK("HasElementalDamage", ElementalCall, CheckElemental);

bool CheckExcess(const ExcessCall& v, char* d, std::size_t n) {
    std::uint16_t damage[8] = {}, protection[8] = {};
    std::memcpy(damage + 1, v.damage, sizeof(v.damage));
    std::memcpy(protection + 1, v.protection, sizeof(v.protection));
    std::uint32_t result = ExcessDamage(damage, nullptr, protection) & 0xFFFF;
    if (result == v.result) return true;
    std::snprintf(d, n, "ours %u, original %u", result, v.result);
    return false;
}
EI_VECTOR_CHECK("ExcessDamage", ExcessCall, CheckExcess);

bool CheckMax(const MaxCall& v, char* d, std::size_t n) {
    std::uint16_t damage[8] = {};
    std::memcpy(damage + 1, v.damage, sizeof(v.damage));
    std::uint16_t result = MaxDamage(damage);
    if (result == v.result) return true;
    std::snprintf(d, n, "ours %u, original %u", result, v.result);
    return false;
}
EI_VECTOR_CHECK("MaxDamage", MaxCall, CheckMax);

// ---- SV4d: the formulas that read the game's settings and the skill database. Each check writes what
// the record read to the stand-in (src/game/game_data.h; cleared before every record).

constexpr std::uintptr_t kFactorStatRange = 0x007AF280;   // unit_stats.cpp
constexpr std::uint32_t kMostLevelRecords = 4096;

// The stand-in's level records: `count` zeroed records, the skill database's level table on them.
std::vector<unsigned char> g_levels;
SkillLevelRecord* Levels(std::uint32_t count) {
    g_levels.assign((count ? count : 1) * sizeof(SkillLevelRecord), 0);
    auto* first = reinterpret_cast<SkillLevelRecord*>(g_levels.data());
    RecordTable& table = const_cast<RecordTable&>(unitdb::SkillLevels());
    table.begin = reinterpret_cast<char*>(first);
    table.end = reinterpret_cast<char*>(first + count);
    return first;
}

// The level table holding the record a LevelView names (none when the table did not have it).
bool PutLevel(const LevelView& view, char* d, std::size_t n) {
    if (!view.exists) {
        Levels(0);
        return true;
    }
    if (view.index >= kMostLevelRecords) {
        std::snprintf(d, n, "level record %u: too many records for the stand-in", view.index);
        return false;
    }
    SkillLevelRecord& record = Levels(view.index + 1)[view.index];
    record.values[7] = view.value;
    record.values[6] = static_cast<std::uint32_t>(view.exponent);
    return true;
}

// The stand-in's skill records 0..count-1 (count at most 8): skill i's first stat name is the one
// letter letters[i]. A CString is its char pointer; the records are raw bytes, never constructed.
alignas(4) unsigned char g_skillRecords[8 * sizeof(SkillRecord)];
char g_skillNames[8][2];
const char* g_skillNamePointers[8];
void PutSkills(const char* letters, std::uint32_t count) {
    struct VectorView {   // CStringVector's three pointers
        const void* start;
        const void* finish;
        const void* eos;
    };
    static_assert(sizeof(VectorView) == sizeof(CStringVector) && sizeof(mfc::CString) == sizeof(const char*));
    std::memset(g_skillRecords, 0, sizeof(g_skillRecords));
    for (std::uint32_t i = 0; i < count; ++i) {
        g_skillNames[i][0] = letters[i];
        g_skillNames[i][1] = 0;
        g_skillNamePointers[i] = g_skillNames[i];
        const VectorView names = {&g_skillNamePointers[i], &g_skillNamePointers[i] + 1, &g_skillNamePointers[i] + 1};
        std::memcpy(g_skillRecords + i * sizeof(SkillRecord) + offsetof(SkillRecord, statNames), &names, sizeof(names));
    }
    RecordTable& table = const_cast<RecordTable&>(unitdb::Skills());
    table.begin = reinterpret_cast<char*>(g_skillRecords);
    table.end = reinterpret_cast<char*>(g_skillRecords + count * sizeof(SkillRecord));
}

UnitStats& ZeroStats() {
    static UnitStats stats;
    std::memset(&stats, 0, sizeof(stats));
    return stats;
}

bool CheckSkillWithBonus(const SkillBonusCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.stat10 = v.stat10;
    stats.stat18 = v.stat18;
    stats.stat20 = v.stat20;
    std::memcpy(stats.skills, v.skills, sizeof(v.skills));
    char letters[8] = {};
    letters[v.index] = v.letter;
    PutSkills(letters, v.index + 1);
    double result = stats.SkillWithBonus(v.index);
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "skill %u ('%c'): ours %.17g, original %.17g", v.index, v.letter, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::SkillWithBonus", SkillBonusCall, CheckSkillWithBonus);

bool CheckSkillLevelValue(const LevelCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.skillLevels[v.index] = v.level;
    if (!PutLevel(v.record, d, n)) return false;
    double result = stats.SkillLevelValue(v.index);
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "skill %d level %u: ours %.17g, original %.17g", v.index, v.level, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::SkillLevelValue", LevelCall, CheckSkillLevelValue);

bool CheckValue6E8(const LevelCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.stat10 = v.stat10;
    stats.skillLevels[0x6E8 - 0x6D3] = v.level;
    if (!PutLevel(v.record, d, n)) return false;
    double result = stats.Value6E8();
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "stat10 %g level %u: ours %.17g, original %.17g", v.stat10, v.level, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::Value6E8", LevelCall, CheckValue6E8);

bool CheckValue6E9(const LevelCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.skillLevels[0x6E9 - 0x6D3] = v.level;
    GameLogicSettings().backstabAdd = v.setting;
    if (!PutLevel(v.record, d, n)) return false;
    double result = stats.Value6E9();
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "setting %g level %u: ours %.17g, original %.17g", v.setting, v.level, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::Value6E9", LevelCall, CheckValue6E9);

bool CheckStrengthRatio(const RatioCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.stat10 = v.stat;
    GameLogicSettings().hpVal[0] = v.setting;
    double result = stats.StrengthRatio();
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "%g / %g: ours %.17g, original %.17g", v.stat, v.setting, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::StrengthRatio", RatioCall, CheckStrengthRatio);

bool CheckDexterityRatio(const RatioCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.stat18 = v.stat;
    GameLogicSettings().mpVal[0] = v.setting;
    double result = stats.DexterityRatio();
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "%g / %g: ours %.17g, original %.17g", v.stat, v.setting, result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::DexterityRatio", RatioCall, CheckDexterityRatio);

bool CheckGetFactors(const FactorsCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    stats.stat10 = v.stat10;
    stats.stat18 = v.stat18;
    ei::GameData<float>(kFactorStatRange) = v.range;
    float out[2] = {};
    stats.GetFactors(&out[0], v.aliased ? &out[0] : &out[1], 0);
    const float b = v.aliased ? out[0] : out[1];
    if (Same(out[0], v.a) && Same(b, v.b)) return true;
    std::snprintf(d, n, "stats %g %g range %g%s: ours %.9g / %.9g, original %.9g / %.9g", v.stat10, v.stat18, v.range,
                  v.aliased ? " (aliased)" : "", out[0], b, v.a, v.b);
    return false;
}
EI_VECTOR_CHECK("UnitStats::GetFactors", FactorsCall, CheckGetFactors);

bool CheckSchoolPrice(const SchoolPriceCall& v, char* d, std::size_t n) {
    GameLogicSettings().skillVal[0] = v.skillVal[0];
    GameLogicSettings().skillVal[1] = v.skillVal[1];
    int result = SchoolPrice(v.from, v.to);
    if (result == v.result) return true;
    std::snprintf(d, n, "%d -> %d (settings %g %g): ours %d, original %d", v.from, v.to, v.skillVal[0], v.skillVal[1],
                  result, v.result);
    return false;
}
EI_VECTOR_CHECK("SchoolPrice", SchoolPriceCall, CheckSchoolPrice);

bool CheckSkillPrice(const SkillPriceCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    std::memcpy(stats.skillLevels, v.skillLevels, sizeof(v.skillLevels));
    GameLogicSettings().skillVal[0] = v.skillVal[0];
    GameLogicSettings().skillVal[1] = v.skillVal[1];
    GameLogicSettings().perkPowerBase = v.perkPowerBase;
    // A record that is not among the level records (raw bytes: a CString's constructor reads the game).
    alignas(4) static unsigned char outside[sizeof(SkillLevelRecord)];
    std::memset(outside, 0, sizeof(outside));
    auto* record = reinterpret_cast<SkillLevelRecord*>(outside);
    if (v.index >= 0) {
        if (static_cast<std::uint32_t>(v.index) >= kMostLevelRecords) {
            std::snprintf(d, n, "level record %d: too many records for the stand-in", v.index);
            return false;
        }
        SkillLevelRecord* levels = Levels(static_cast<std::uint32_t>(v.index) + 1);
        record = &levels[v.index];
        if (v.index % 3 > 0) levels[v.index - 1].values[6] = static_cast<std::uint32_t>(v.previousExponent);
    } else {
        Levels(0);
    }
    record->values[6] = static_cast<std::uint32_t>(v.exponent);
    int result = stats.SkillPrice(reinterpret_cast<const char*>(record));
    if (result == v.result) return true;
    std::snprintf(d, n, "record %d (exponents %d, %d): ours %d, original %d", v.index, v.exponent, v.previousExponent,
                  result, v.result);
    return false;
}
EI_VECTOR_CHECK("UnitStats::SkillPrice", SkillPriceCall, CheckSkillPrice);

bool CheckIndexOfLevel(const IndexOfLevelCall& v, char* d, std::size_t n) {
    if (v.count >= kMostLevelRecords) {
        std::snprintf(d, n, "%u level records: too many for the stand-in", v.count);
        return false;
    }
    SkillLevelRecord* levels = Levels(v.count);
    // Only compared, never read: a record outside the table stays outside.
    auto* record = reinterpret_cast<const SkillLevelRecord*>(reinterpret_cast<std::uintptr_t>(levels) +
                                                             static_cast<std::uintptr_t>(v.offset));
    int result = SkillDb()->IndexOfLevel(record);
    if (result == v.result) return true;
    std::snprintf(d, n, "%u records, offset %d: ours %d, original %d", v.count, v.offset, result, v.result);
    return false;
}
EI_VECTOR_CHECK("SkillDatabase::IndexOfLevel", IndexOfLevelCall, CheckIndexOfLevel);

bool CheckProtectionBlock(const ProtectionCall& v, char* d, std::size_t n) {
    static UnitParams params;
    std::memset(&params, 0, sizeof(params));
    for (int i = 0; i < 6; ++i) {
        const ProtectionSlot& view = v.slots[i];
        UnitParamsSlot& slot = params.slots[i];
        slot.type = view.type;
        slot.kind = view.kind;
        std::memcpy(slot.protection, view.protection, sizeof(view.protection));
    }
    UnitVitalsPacked vitals;
    std::memset(&vitals, 0, sizeof(vitals));
    std::memcpy(vitals.protection, v.vitals, sizeof(v.vitals));
    if (!v.keyed) {
        GameDifficulty() = v.difficulty;
        GameLogicSettings().absorption[v.difficulty] = v.factor;
    }
    float out[7];
    std::memset(out, 0xCD, sizeof(out));
    params.ProtectionBlock(&vitals, out, v.keyed);
    if (Same(out, v.out)) return true;
    int k = 0;
    while (Same(out[k], v.out[k])) ++k;
    std::snprintf(d, n, "%s, type %d: ours %.9g, original %.9g", v.keyed ? "keyed" : "not keyed", k, out[k], v.out[k]);
    return false;
}
EI_VECTOR_CHECK("UnitParams::ProtectionBlock", ProtectionCall, CheckProtectionBlock);

bool CheckWeaponSkillBonus(const WeaponBonusCall& v, char* d, std::size_t n) {
    static UnitParams params;
    std::memset(&params, 0, sizeof(params));
    params.stat10 = v.stat10;
    params.stat18 = v.stat18;
    params.attackRate = v.attackRate;
    params.skills[0] = v.skills[0];
    params.skills[1] = v.skills[1];
    if (v.type < 7) params.skillLevels[v.type] = v.level;
    if (!PutLevel(v.record, d, n)) return false;
    PutSkills(v.letters, 2);
    static itemdb::WeaponRecord* weapon = [] {
        static unsigned char storage[sizeof(itemdb::WeaponRecord)];
        return reinterpret_cast<itemdb::WeaponRecord*>(storage);
    }();
    std::memset(static_cast<void*>(weapon), 0, sizeof(*weapon));
    weapon->skillBonus[0] = v.bonus[0];
    weapon->skillBonus[1] = v.bonus[1];
    double result = params.WeaponSkillBonus(v.type, v.weapon ? reinterpret_cast<const char*>(weapon) : nullptr);
    if (Same(result, v.result)) return true;
    std::snprintf(d, n, "type %d level %u weapon %u: ours %.17g, original %.17g", v.type, v.level, v.weapon, result,
                  v.result);
    return false;
}
EI_VECTOR_CHECK("UnitParams::WeaponSkillBonus", WeaponBonusCall, CheckWeaponSkillBonus);

bool CheckPickLocation(const PickLocationCall& v, char* d, std::size_t n) {
    UnitStats& stats = ZeroStats();
    for (int i = 0; i < 6; ++i) stats.slots[i].kind = v.kinds[i];
    IsaacRandom& rng = IsaacRandom::Global();
    rng.count_ = 1;
    rng.results_[0] = v.draw;
    int result = stats.PickLocation(v.location);
    const bool drew = rng.count_ != 1;
    if (result == v.result && drew == (v.drew != 0)) return true;
    std::snprintf(d, n, "location %d, kinds %d %d %d %d %d %d: ours %d%s, original %d%s", v.location, v.kinds[0],
                  v.kinds[1], v.kinds[2], v.kinds[3], v.kinds[4], v.kinds[5], result, drew ? " (drew)" : "", v.result,
                  v.drew ? " (drew)" : "");
    return false;
}
EI_VECTOR_CHECK("UnitStats::PickLocation", PickLocationCall, CheckPickLocation);

} // namespace
