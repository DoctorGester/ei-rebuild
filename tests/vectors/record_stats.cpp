// Record mode for the "stats" family (stats_vectors.h): each recorder runs the original and offers the
// call to its sampler. Stolen bytes as in the verify wrappers S21d deleted (verify_unit_stats.cpp,
// verify_unit_small_a12.cpp, verify_unit_db.cpp, verify_unit_skill_price.cpp, verify_unit_params_*.cpp,
// verify_unit_hit_location.cpp). Reached
// by every scenario with units (the panel, the AI's attack options, hits and healing).
#include "stats_vectors.h"
#include "../verify/exe_objects.h"
#include "../verify/harness_original.h"
#include "vectors.h"

#include "../../src/game/logic_settings.h"
#include "../../src/game/random.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit_db.h"

#include "ei/hook.h"
#include "game/game_data.h"

#include <cstring>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::stats;
namespace exe = ei::verify::exe;

constexpr std::uint32_t kCap = 256;
constexpr std::size_t kOriginalWeaponSkillBonus = 0x7B;   // game.exe's WeaponRecord::skillBonus
constexpr std::uint32_t kOriginalDifficulty = 0x250;       // game.exe's GameOptions::difficulty_ (ours has std::string members)

// The recorders run in game.exe: its logic settings, not our g_logicSettings.
const LogicSettings& OriginalLogicSettings() { return ei::OriginalGameData<const LogicSettings>(0x007C53F8); }

template <typename T>
struct Recorded {
    Sampler sampler;
    explicit Recorded(const char* name, unsigned buckets = 1) : sampler("stats", name, sizeof(T), kCap, buckets) {}
};

StatView ViewOf(const UnitStats* s, unsigned parts) {
    StatView v;
    std::memset(&v, 0, sizeof(v));
    if (parts & kStat10) v.strength = s->strength;
    if (parts & kStat18) v.dexterity = s->dexterity;
    if (parts & kStat20) v.intelligence = s->intelligence;
    if (parts & kAttributes) std::memcpy(v.attributes, s->attributes, sizeof(v.attributes));
    if (parts & kSkills) std::memcpy(v.skills, s->skills, sizeof(v.skills));
    if (parts & kSlots) {
        for (int i = 0; i < 6; ++i) {
            v.slots[i].kind = s->slots[i].kind;
            v.slots[i].current = s->slots[i].current;
            v.slots[i].maximum = s->slots[i].maximum;
            v.slots[i].factor = s->slots[i].factor;
        }
    }
    return v;
}

// 00523CD0
Recorded<IndexCall> g_period20("UnitStats::Period20");
double(__fastcall* s_period20)(UnitStats*);
double __fastcall Period20Record(UnitStats* self) {
    auto v = Zeroed<IndexCall>();
    v.stats = ViewOf(self, kStat20);
    std::uint16_t cw = ControlWord();
    double result = s_period20(self);
    v.result = result;
    g_period20.sampler.Offer(v, cw);
    return result;
}

// 00525DF0
Recorded<IndexCall> g_attributeBase("UnitStats::AttributeBase");
float(__fastcall* s_attributeBase)(UnitStats*, void*, int);
float __fastcall AttributeBaseRecord(UnitStats* self, void*, int index) {
    auto v = Zeroed<IndexCall>();
    v.stats = ViewOf(self, kAttributes);
    v.arg = index;
    std::uint16_t cw = ControlWord();
    float result = s_attributeBase(self, nullptr, index);
    v.result = result;
    g_attributeBase.sampler.Offer(v, cw);
    return result;
}

// 00525E10
Recorded<IndexCall> g_attributeWithBonus("UnitStats::AttributeWithBonus");
double(__fastcall* s_attributeWithBonus)(UnitStats*, void*, int);
double __fastcall AttributeWithBonusRecord(UnitStats* self, void*, int index) {
    auto v = Zeroed<IndexCall>();
    v.stats = ViewOf(self, kStat18 | kStat20 | kAttributes);
    v.arg = index;
    std::uint16_t cw = ControlWord();
    double result = s_attributeWithBonus(self, nullptr, index);
    v.result = result;
    g_attributeWithBonus.sampler.Offer(v, cw);
    return result;
}

// 00527030
Recorded<IndexCall> g_skillBase("UnitStats::SkillBase");
float(__fastcall* s_skillBase)(UnitStats*, void*, int);
float __fastcall SkillBaseRecord(UnitStats* self, void*, int index) {
    auto v = Zeroed<IndexCall>();
    v.stats = ViewOf(self, kSkills);
    v.arg = index;
    std::uint16_t cw = ControlWord();
    float result = s_skillBase(self, nullptr, index);
    v.result = result;
    g_skillBase.sampler.Offer(v, cw);
    return result;
}

// 00526780
Recorded<IndexCall> g_weaponSkill("UnitStats::WeaponSkill");
float(__fastcall* s_weaponSkill)(UnitStats*, void*, int);
float __fastcall WeaponSkillRecord(UnitStats* self, void*, int type) {
    auto v = Zeroed<IndexCall>();
    v.stats = ViewOf(self, kSkills);
    v.arg = type;
    std::uint16_t cw = ControlWord();
    float result = s_weaponSkill(self, nullptr, type);
    v.result = result;
    g_weaponSkill.sampler.Offer(v, cw);
    return result;
}

// 005259B0 and 00524020
Recorded<ReduceCall> g_reduce("UnitStats::Reduce"), g_reduceKind1("UnitStats::ReduceKind1");
double(__fastcall* s_reduce)(UnitStats*, void*, float);
double(__fastcall* s_reduceKind1)(UnitStats*, void*, float);
double RecordReduce(Recorded<ReduceCall>& recorded, decltype(s_reduce) original, UnitStats* self, float value) {
    auto v = Zeroed<ReduceCall>();
    v.stats = ViewOf(self, kSlots);
    v.value = value;
    std::uint16_t cw = ControlWord();
    double result = original(self, nullptr, value);
    v.result = result;
    recorded.sampler.Offer(v, cw);
    return result;
}
double __fastcall ReduceRecord(UnitStats* self, void*, float value) {
    return RecordReduce(g_reduce, s_reduce, self, value);
}
double __fastcall ReduceKind1Record(UnitStats* self, void*, float value) {
    return RecordReduce(g_reduceKind1, s_reduceKind1, self, value);
}

// 00522A80
Recorded<OpensCall> g_opens("LockKey::Opens");
int(__fastcall* s_opens)(LockKey*, void*, const LockKey*);
int __fastcall OpensRecord(LockKey* self, void*, const LockKey* lock) {
    auto v = Zeroed<OpensCall>();
    v.key = *self;
    v.lock = *lock;
    std::uint16_t cw = ControlWord();
    int result = s_opens(self, nullptr, lock);
    v.result = result;
    g_opens.sampler.Offer(v, cw);
    return result;
}

// 00522B40
Recorded<ResetDamageCall> g_resetDamage("ResetDamageInfo");
std::uint32_t(__fastcall* s_resetDamage)(DamageInfo*);
std::uint32_t __fastcall ResetDamageRecord(DamageInfo* info) {
    auto v = Zeroed<ResetDamageCall>();
    std::uint16_t cw = ControlWord();
    std::uint32_t result = s_resetDamage(info);
    v.after = *info;
    v.result = result;
    g_resetDamage.sampler.Offer(v, cw);
    return result;
}

// 00522B80
Recorded<ElementalCall> g_elemental("HasElementalDamage");
int(__fastcall* s_elemental)(const DamageInfo*);
int __fastcall ElementalRecord(const DamageInfo* info) {
    auto v = Zeroed<ElementalCall>();
    for (int i = 0; i < 3; ++i) v.weights[i] = info->typeWeights[3 + i];
    std::uint16_t cw = ControlWord();
    int result = s_elemental(info);
    v.result = result;
    g_elemental.sampler.Offer(v, cw);
    return result;
}

// 00522AE0. Only AX is a result.
Recorded<ExcessCall> g_excess("ExcessDamage");
std::uint32_t(__fastcall* s_excess)(const std::uint16_t*, void*, const std::uint16_t*);
std::uint32_t __fastcall ExcessRecord(const std::uint16_t* damage, void*, const std::uint16_t* protection) {
    auto v = Zeroed<ExcessCall>();
    std::memcpy(v.damage, damage + 1, sizeof(v.damage));
    std::memcpy(v.protection, protection + 1, sizeof(v.protection));
    std::uint16_t cw = ControlWord();
    std::uint32_t result = s_excess(damage, nullptr, protection);
    v.result = result & 0xFFFF;
    g_excess.sampler.Offer(v, cw);
    return result;
}

// 00522B20
Recorded<MaxCall> g_max("MaxDamage");
std::uint16_t(__fastcall* s_max)(const std::uint16_t*);
std::uint16_t __fastcall MaxRecord(const std::uint16_t* damage) {
    auto v = Zeroed<MaxCall>();
    std::memcpy(v.damage, damage + 1, sizeof(v.damage));
    std::uint16_t cw = ControlWord();
    std::uint16_t result = s_max(damage);
    v.result = result;
    g_max.sampler.Offer(v, cw);
    return result;
}

// ---- SV4d: what the formulas read of the game's settings and the skill database ----

// game.exe's STLport vector<T>: start, finish, end of storage.
template <typename T>
struct OriginalVector {
    T* start;
    T* finish;
    T* eos;

    std::uint32_t size() const { return static_cast<std::uint32_t>(finish - start); }
    T* begin() const { return start; }
    T& operator[](std::size_t i) const { return start[i]; }
};

// game.exe's skill records: their names are CStrings (exe_objects.h: a char pointer each) where ours are std::strings (larger), so
// the recorders read them through game.exe's layout, not unit_db.h's.
struct OriginalSkillRecord {
    exe::CString name;
    exe::CString field1;
    std::uint32_t field2;
    OriginalVector<exe::CString> statNames;
};
static_assert(offsetof(OriginalSkillRecord, statNames) == 0x0C && sizeof(OriginalSkillRecord) == 0x18);
struct OriginalSkillLevelRecord {
    exe::CString field0;
    exe::CString name;
    std::uint32_t field2;
    exe::CString requiredLevelName;
    std::uint32_t exclusionGroup;
    exe::CString requiredSkillName;
    std::uint32_t values[10];
    std::uint8_t learnable;
    std::uint8_t padding[3];

    int WorthExponent() const { return static_cast<int>(values[6]); }
};
static_assert(offsetof(OriginalSkillLevelRecord, values) == 0x18 && sizeof(OriginalSkillLevelRecord) == 0x44);
struct OriginalSkillDatabase {
    OriginalVector<OriginalSkillRecord> skills;
    OriginalVector<OriginalSkillLevelRecord> levels;
};

// game.exe's skill database (007AF268): the recording runs the original, which fills its own (H30).
OriginalSkillDatabase* GameSkillDb() { return &ei::OriginalGameData<OriginalSkillDatabase>(0x007AF268); }

// Its level record and skill record `index`, or null past the end (as unitdb::SkillLevelAt / SkillAt).
const OriginalSkillLevelRecord* GameSkillLevelAt(std::uint32_t index) {
    const OriginalVector<OriginalSkillLevelRecord>& levels = GameSkillDb()->levels;
    return index < levels.size() ? levels.start + index : nullptr;
}
const OriginalSkillRecord* GameSkillAt(std::uint32_t index) {
    const OriginalVector<OriginalSkillRecord>& skills = GameSkillDb()->skills;
    return index < skills.size() ? skills.start + index : nullptr;
}

// Level record `index` as a LevelView.
LevelView LevelOf(std::uint32_t index) {
    LevelView v{};
    v.index = index;
    const OriginalSkillLevelRecord* record = GameSkillLevelAt(index);
    if (record) {
        v.exists = 1;
        v.value = record->values[7];
        v.exponent = record->WorthExponent();
    }
    return v;
}

// The first letter of skill `index`'s first stat name, or -1 without the record or a name (the
// original then faults or reads past the names: such calls are not recorded).
int LetterOf(std::uint32_t index) {
    const OriginalSkillRecord* record = GameSkillAt(index);
    if (!record || record->statNames.size() == 0) return -1;
    const char* name = record->statNames.begin()[0];
    return name ? static_cast<unsigned char>(name[0]) : -1;
}

constexpr int kSkillLevelBytes = 0x24;                // UnitStats::SkillPrice sums these
// 00527050
Recorded<SkillBonusCall> g_skillWithBonus("UnitStats::SkillWithBonus");
double(__fastcall* s_skillWithBonus)(UnitStats*, void*, std::uint32_t);
double __fastcall SkillWithBonusRecord(UnitStats* self, void*, std::uint32_t index) {
    auto v = Zeroed<SkillBonusCall>();
    v.strength = self->strength;
    v.dexterity = self->dexterity;
    v.intelligence = self->intelligence;
    std::memcpy(v.skills, self->skills, sizeof(v.skills));
    v.index = index;
    int letter = LetterOf(index);
    v.letter = static_cast<char>(letter);
    std::uint16_t cw = ControlWord();
    double result = s_skillWithBonus(self, nullptr, index);
    v.result = result;
    if (letter >= 0 && index < 8) g_skillWithBonus.sampler.Offer(v, cw);
    return result;
}

// 005267E0. Bucket 1: a level byte that is set.
Recorded<LevelCall> g_skillLevelValue("UnitStats::SkillLevelValue", 2);
double(__fastcall* s_skillLevelValue)(UnitStats*, void*, int);
double __fastcall SkillLevelValueRecord(UnitStats* self, void*, int index) {
    std::uint16_t cw = ControlWord();
    double result = s_skillLevelValue(self, nullptr, index);
    if (index >= 0 && index < kSkillLevelBytes) {
        auto v = Zeroed<LevelCall>();
        v.index = index;
        v.level = self->skillLevels[index];
        if (v.level != 0) v.record = LevelOf(static_cast<std::uint32_t>(index * 3 - 1 + v.level));
        v.result = result;
        g_skillLevelValue.sampler.Offer(v, cw, v.level != 0 ? 1 : 0);
    }
    return result;
}

// 00524280 and 00525CD0: the level bytes at +0x6E8 / +0x6E9, the level records from 0x3E / 0x41.
Recorded<LevelCall> g_scaledStrength("UnitStats::Value6E8", 2), g_backstabFactor("UnitStats::Value6E9", 2);
double(__fastcall* s_scaledStrength)(UnitStats*);
double(__fastcall* s_backstabFactor)(UnitStats*);
double __fastcall ScaledStrengthRecord(UnitStats* self) {
    auto v = Zeroed<LevelCall>();
    v.strength = self->strength;
    v.level = self->skillLevels[0x6E8 - 0x6D3];
    if (v.level != 0) v.record = LevelOf(v.level + 0x3Eu);
    std::uint16_t cw = ControlWord();
    double result = s_scaledStrength(self);
    v.result = result;
    g_scaledStrength.sampler.Offer(v, cw, v.level != 0 ? 1 : 0);
    return result;
}
double __fastcall BackstabFactorRecord(UnitStats* self) {
    auto v = Zeroed<LevelCall>();
    v.setting = OriginalLogicSettings().backstabAdd;
    v.level = self->skillLevels[0x6E9 - 0x6D3];
    if (v.level != 0) v.record = LevelOf(v.level + 0x41u);
    std::uint16_t cw = ControlWord();
    double result = s_backstabFactor(self);
    v.result = result;
    g_backstabFactor.sampler.Offer(v, cw, v.level != 0 ? 1 : 0);
    return result;
}

// 00523CB0 and 00523CC0
Recorded<RatioCall> g_strengthRatio("UnitStats::StrengthRatio"), g_dexterityRatio("UnitStats::DexterityRatio");
double(__fastcall* s_strengthRatio)(UnitStats*);
double(__fastcall* s_dexterityRatio)(UnitStats*);
double __fastcall StrengthRatioRecord(UnitStats* self) {
    auto v = Zeroed<RatioCall>();
    v.stat = self->strength;
    v.setting = OriginalLogicSettings().hpVal[0];
    std::uint16_t cw = ControlWord();
    double result = s_strengthRatio(self);
    v.result = result;
    g_strengthRatio.sampler.Offer(v, cw);
    return result;
}
double __fastcall DexterityRatioRecord(UnitStats* self) {
    auto v = Zeroed<RatioCall>();
    v.stat = self->dexterity;
    v.setting = OriginalLogicSettings().mpVal[0];
    std::uint16_t cw = ControlWord();
    double result = s_dexterityRatio(self);
    v.result = result;
    g_dexterityRatio.sampler.Offer(v, cw);
    return result;
}

// 00528840
Recorded<FactorsCall> g_getFactors("UnitStats::GetFactors");
void(__fastcall* s_getFactors)(UnitStats*, void*, float*, float*, int);
void __fastcall GetFactorsRecord(UnitStats* self, void*, float* a, float* b, int unused) {
    auto v = Zeroed<FactorsCall>();
    v.strength = self->strength;
    v.dexterity = self->dexterity;
    v.range = ei::OriginalGameData<const float>(0x007AF280);   // unit_stats.cpp's kFactorStatRange
    v.aliased = a == b;
    std::uint16_t cw = ControlWord();
    s_getFactors(self, nullptr, a, b, unused);
    v.a = *a;
    v.b = *b;
    g_getFactors.sampler.Offer(v, cw);
}

// 00527FF0
Recorded<SchoolPriceCall> g_schoolPrice("SchoolPrice");
int(__fastcall* s_schoolPrice)(int, int);
int __fastcall SchoolPriceRecord(int from, int to) {
    auto v = Zeroed<SchoolPriceCall>();
    v.from = from;
    v.to = to;
    v.skillVal[0] = OriginalLogicSettings().skillVal[0];
    v.skillVal[1] = OriginalLogicSettings().skillVal[1];
    std::uint16_t cw = ControlWord();
    int result = s_schoolPrice(from, to);
    v.result = result;
    g_schoolPrice.sampler.Offer(v, cw);
    return result;
}

// 00528050
Recorded<SkillPriceCall> g_skillPrice("UnitStats::SkillPrice");
int(__fastcall* s_skillPrice)(UnitStats*, void*, const char*);
int __fastcall SkillPriceRecord(UnitStats* self, void*, const char* record) {
    auto v = Zeroed<SkillPriceCall>();
    const OriginalSkillDatabase* db = GameSkillDb();
    auto* level = reinterpret_cast<const OriginalSkillLevelRecord*>(record);
    const std::ptrdiff_t offset = record - reinterpret_cast<const char*>(db->levels.begin());
    const std::ptrdiff_t stride = sizeof(OriginalSkillLevelRecord);
    const std::ptrdiff_t size = static_cast<std::ptrdiff_t>(db->levels.size()) * stride;
    v.index = offset >= 0 && offset < size && offset % stride == 0 ? static_cast<std::int32_t>(offset / stride) : -1;
    v.exponent = level->WorthExponent();
    if (v.index % 3 > 0) v.previousExponent = db->levels[static_cast<std::size_t>(v.index - 1)].WorthExponent();
    std::memcpy(v.skillLevels, self->skillLevels, sizeof(v.skillLevels));
    v.skillVal[0] = OriginalLogicSettings().skillVal[0];
    v.skillVal[1] = OriginalLogicSettings().skillVal[1];
    v.perkPowerBase = OriginalLogicSettings().perkPowerBase;
    std::uint16_t cw = ControlWord();
    int result = s_skillPrice(self, nullptr, record);
    v.result = result;
    g_skillPrice.sampler.Offer(v, cw);
    return result;
}

// 0051FF00
Recorded<IndexOfLevelCall> g_indexOfLevel("SkillDatabase::IndexOfLevel");
int(__fastcall* s_indexOfLevel)(OriginalSkillDatabase*, void*, const OriginalSkillLevelRecord*);
int __fastcall IndexOfLevelRecord(OriginalSkillDatabase* self, void*, const OriginalSkillLevelRecord* record) {
    auto v = Zeroed<IndexOfLevelCall>();
    v.count = static_cast<std::uint32_t>(self->levels.size());
    v.offset = static_cast<std::int32_t>(reinterpret_cast<const char*>(record) -
                                         reinterpret_cast<const char*>(self->levels.begin()));
    std::uint16_t cw = ControlWord();
    int result = s_indexOfLevel(self, nullptr, record);
    v.result = result;
    if (self == GameSkillDb()) g_indexOfLevel.sampler.Offer(v, cw);
    return result;
}

// 005270E0. Bucket 1: keyed.
Recorded<ProtectionCall> g_protectionBlock("UnitParams::ProtectionBlock", 2);
void(__fastcall* s_protectionBlock)(UnitParams*, void*, const UnitVitalsPacked*, float*, std::uint8_t);
void __fastcall ProtectionBlockRecord(UnitParams* self, void*, const UnitVitalsPacked* vitals, float* out,
                                      std::uint8_t keyed) {
    auto v = Zeroed<ProtectionCall>();
    bool recordable = true;
    for (int i = 0; i < 6; ++i) {
        const UnitParamsSlot& slot = self->slots[i];
        ProtectionSlot& view = v.slots[i];
        view.type = slot.type;
        view.kind = slot.kind;
        std::memcpy(view.protection, slot.protection, sizeof(view.protection));
        if (keyed && slot.kind > 1) {
            if (slot.type < 0 || slot.type >= unitdb::kSlotTypes) recordable = false;
            else view.weight = unitdb::kSlotWeights[slot.type];
        }
    }
    std::memcpy(v.vitals, vitals->protection, sizeof(v.vitals));
    v.keyed = keyed;
    if (!keyed) {
        v.difficulty = ei::OriginalGameData<const int>(0x0079BC70 + kOriginalDifficulty);   // game.exe's own: the recording runs the original
        if (v.difficulty < 0 || v.difficulty >= 3) recordable = false;
        else v.factor = OriginalLogicSettings().absorption[v.difficulty];
    }
    std::uint16_t cw = ControlWord();
    s_protectionBlock(self, nullptr, vitals, out, keyed);
    std::memcpy(v.out, out, sizeof(v.out));
    if (recordable) g_protectionBlock.sampler.Offer(v, cw, keyed ? 1 : 0);
}

// 00527850. Buckets: types 0-4, 5-6, the rest.
Recorded<WeaponBonusCall> g_weaponSkillBonus("UnitParams::WeaponSkillBonus", 3);
double(__fastcall* s_weaponSkillBonus)(UnitParams*, void*, int, const char*);
double __fastcall WeaponSkillBonusRecord(UnitParams* self, void*, int type, const char* weapon) {
    auto v = Zeroed<WeaponBonusCall>();
    v.type = type;
    v.strength = self->strength;
    v.dexterity = self->dexterity;
    v.intelligence = self->intelligence;
    v.skills[0] = self->skills[0];
    v.skills[1] = self->skills[1];
    if (type >= 0 && type < 7) {
        v.level = self->skillLevels[type];
        if (v.level != 0) v.record = LevelOf(static_cast<std::uint32_t>(type * 3 - 1 + v.level));
    }
    int letter0 = LetterOf(0), letter1 = LetterOf(1);
    v.letters[0] = static_cast<char>(letter0);
    v.letters[1] = static_cast<char>(letter1);
    if (weapon) {
        // game.exe's record: its skill bonus bytes at +0x7B (ours has std::string head members, larger).
        const auto* bonus = reinterpret_cast<const std::uint8_t*>(weapon) + kOriginalWeaponSkillBonus;
        v.weapon = 1;
        v.bonus[0] = bonus[0];
        v.bonus[1] = bonus[1];
    }
    std::uint16_t cw = ControlWord();
    double result = s_weaponSkillBonus(self, nullptr, type, weapon);
    v.result = result;
    if (type >= 0 && type <= 8 && letter0 >= 0 && letter1 >= 0)
        g_weaponSkillBonus.sampler.Offer(v, cw, type < 5 ? 0 : type < 7 ? 1 : 2);
    return result;
}

// 00525B20. Bucket 1: it drew from the generator.
Recorded<PickLocationCall> g_pickLocation("UnitStats::PickLocation", 2);
int(__fastcall* s_pickLocation)(UnitStats*, void*, int);
int __fastcall PickLocationRecord(UnitStats* self, void*, int location) {
    auto v = Zeroed<PickLocationCall>();
    for (int i = 0; i < 6; ++i) v.kinds[i] = self->slots[i].kind;
    v.location = location;
    IsaacRandom& rng = ei::OriginalGameData<IsaacRandom>(0x007A9D40);   // game.exe's generator
    std::uint32_t countBefore = rng.count_;
    std::uint16_t cw = ControlWord();
    int result = s_pickLocation(self, nullptr, location);
    v.result = result;
    v.drew = rng.count_ != countBefore;
    if (v.drew) v.draw = rng.results_[rng.count_ & 0xFF];
    // A draw that refilled the generator is left out (the replay's generator holds one value).
    if (location >= 0 && location < 6 && !(v.drew && countBefore == 0))
        g_pickLocation.sampler.Offer(v, cw, v.drew ? 1 : 0);
    return result;
}

} // namespace

// Not declared in a header (unit_stats.cpp, unit_skill_price.cpp).
std::uint16_t MaxDamage(const std::uint16_t* damage);
int SchoolPrice(int from, int to);

EI_RECORD(0x00523CD0, Period20Record, 6, s_period20, UnitStats::Period20);
EI_RECORD(0x00525DF0, AttributeBaseRecord, 6, s_attributeBase, UnitStats::AttributeBase);
EI_RECORD(0x00525E10, AttributeWithBonusRecord, 7, s_attributeWithBonus, UnitStats::AttributeWithBonus);
EI_RECORD(0x00527030, SkillBaseRecord, 6, s_skillBase, UnitStats::SkillBase);
EI_RECORD(0x00526780, WeaponSkillRecord, 7, s_weaponSkill, UnitStats::WeaponSkill);
EI_RECORD(0x005259B0, ReduceRecord, 6, s_reduce, UnitStats::Reduce);
EI_RECORD(0x00524020, ReduceKind1Record, 6, s_reduceKind1, UnitStats::ReduceKind1);
EI_RECORD(0x00522A80, OpensRecord, 7, s_opens, LockKey::Opens);
EI_RECORD(0x00522B40, ResetDamageRecord, 8, s_resetDamage, ResetDamageInfo);
EI_RECORD(0x00522B80, ElementalRecord, 9, s_elemental, HasElementalDamage);
EI_RECORD(0x00522AE0, ExcessRecord, 6, s_excess, DamageWords::ExcessOver);
EI_RECORD(0x00522B20, MaxRecord, 6, s_max, MaxDamage);
EI_RECORD(0x00527050, SkillWithBonusRecord, 6, s_skillWithBonus, UnitStats::SkillWithBonus);
EI_RECORD(0x005267E0, SkillLevelValueRecord, 7, s_skillLevelValue, UnitStats::SkillLevelValue);
EI_RECORD(0x00524280, ScaledStrengthRecord, 6, s_scaledStrength, UnitStats::ScaledStrength);
EI_RECORD(0x00525CD0, BackstabFactorRecord, 6, s_backstabFactor, UnitStats::BackstabFactor);
EI_RECORD(0x00523CB0, StrengthRatioRecord, 9, s_strengthRatio, UnitStats::StrengthRatio);
EI_RECORD(0x00523CC0, DexterityRatioRecord, 9, s_dexterityRatio, UnitStats::DexterityRatio);
EI_RECORD(0x00528840, GetFactorsRecord, 9, s_getFactors, UnitStats::GetFactors);
EI_RECORD(0x00527FF0, SchoolPriceRecord, 9, s_schoolPrice, SchoolPrice);
EI_RECORD(0x00528050, SkillPriceRecord, 6, s_skillPrice, UnitStats::SkillPrice);
EI_RECORD(0x0051FF00, IndexOfLevelRecord, 6, s_indexOfLevel, SkillDatabase::IndexOfLevel);
EI_RECORD(0x005270E0, ProtectionBlockRecord, 5, s_protectionBlock, UnitParams::ProtectionBlock);
EI_RECORD(0x00527850, WeaponSkillBonusRecord, 7, s_weaponSkillBonus, UnitParams::WeaponSkillBonus);
EI_RECORD(0x00525B20, PickLocationRecord, 7, s_pickLocation, UnitStats::PickLocation);
