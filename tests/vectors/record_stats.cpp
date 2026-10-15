// Record mode for the "stats" family (stats_vectors.h): each recorder runs the original and offers the
// call to its sampler. Stolen bytes as in the verify wrappers (verify_unit_stats.cpp,
// verify_unit_small_a12.cpp, verify_unit_db.cpp, verify_unit_skill_price.cpp, verify_unit_params_*.cpp,
// verify_unit_hit_location.cpp). Reached
// by every scenario with units (the panel, the AI's attack options, hits and healing).
#include "stats_vectors.h"
#include "vectors.h"

#include "../../src/game/logic_settings.h"
#include "../../src/game/random.h"
#include "../../src/game/ui.h"
#include "../../src/game/unit_db.h"

#include "ei/hook.h"

#include <cstring>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::stats;

constexpr std::uint32_t kCap = 256;

template <typename T>
struct Recorded {
    Sampler sampler;
    explicit Recorded(const char* name, unsigned buckets = 1) : sampler("stats", name, sizeof(T), kCap, buckets) {}
};

StatView ViewOf(const UnitStats* s, unsigned parts) {
    StatView v;
    std::memset(&v, 0, sizeof(v));
    if (parts & kStat10) v.stat10 = s->stat10;
    if (parts & kStat18) v.stat18 = s->stat18;
    if (parts & kStat20) v.stat20 = s->stat20;
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
BOOL(__fastcall* s_opens)(LockKey*, void*, const LockKey*);
BOOL __fastcall OpensRecord(LockKey* self, void*, const LockKey* lock) {
    auto v = Zeroed<OpensCall>();
    v.key = *self;
    v.lock = *lock;
    std::uint16_t cw = ControlWord();
    BOOL result = s_opens(self, nullptr, lock);
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
BOOL(__fastcall* s_elemental)(const DamageInfo*);
BOOL __fastcall ElementalRecord(const DamageInfo* info) {
    auto v = Zeroed<ElementalCall>();
    for (int i = 0; i < 3; ++i) v.weights[i] = info->typeWeights[3 + i];
    std::uint16_t cw = ControlWord();
    BOOL result = s_elemental(info);
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

// Level record `index` as a LevelView.
LevelView LevelOf(std::uint32_t index) {
    LevelView v{};
    v.index = index;
    auto* record = reinterpret_cast<const SkillLevelRecord*>(unitdb::SkillLevels().At(index, unitdb::kSkillLevelSize));
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
    auto* record = reinterpret_cast<const SkillRecord*>(unitdb::Skills().At(index, unitdb::kSkillSize));
    if (!record || record->statNames.size() == 0) return -1;
    const char* name = record->statNames.begin()[0];
    return name ? static_cast<unsigned char>(name[0]) : -1;
}

constexpr int kSkillLevelBytes = 0x24;                // UnitStats::SkillPrice sums these
constexpr std::uintptr_t kSlotWeights = 0x0073E0FC;   // unit_params_protection.cpp
constexpr std::uintptr_t kFactorStatRange = 0x007AF280;   // unit_stats.cpp

// 00527050
Recorded<SkillBonusCall> g_skillWithBonus("UnitStats::SkillWithBonus");
double(__fastcall* s_skillWithBonus)(UnitStats*, void*, std::uint32_t);
double __fastcall SkillWithBonusRecord(UnitStats* self, void*, std::uint32_t index) {
    auto v = Zeroed<SkillBonusCall>();
    v.stat10 = self->stat10;
    v.stat18 = self->stat18;
    v.stat20 = self->stat20;
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
Recorded<LevelCall> g_value6E8("UnitStats::Value6E8", 2), g_value6E9("UnitStats::Value6E9", 2);
double(__fastcall* s_value6E8)(UnitStats*);
double(__fastcall* s_value6E9)(UnitStats*);
double __fastcall Value6E8Record(UnitStats* self) {
    auto v = Zeroed<LevelCall>();
    v.stat10 = self->stat10;
    v.level = self->skillLevels[0x6E8 - 0x6D3];
    if (v.level != 0) v.record = LevelOf(v.level + 0x3Eu);
    std::uint16_t cw = ControlWord();
    double result = s_value6E8(self);
    v.result = result;
    g_value6E8.sampler.Offer(v, cw, v.level != 0 ? 1 : 0);
    return result;
}
double __fastcall Value6E9Record(UnitStats* self) {
    auto v = Zeroed<LevelCall>();
    v.setting = GameLogicSettings().backstabAdd;
    v.level = self->skillLevels[0x6E9 - 0x6D3];
    if (v.level != 0) v.record = LevelOf(v.level + 0x41u);
    std::uint16_t cw = ControlWord();
    double result = s_value6E9(self);
    v.result = result;
    g_value6E9.sampler.Offer(v, cw, v.level != 0 ? 1 : 0);
    return result;
}

// 00523CB0 and 00523CC0
Recorded<RatioCall> g_strengthRatio("UnitStats::StrengthRatio"), g_dexterityRatio("UnitStats::DexterityRatio");
double(__fastcall* s_strengthRatio)(UnitStats*);
double(__fastcall* s_dexterityRatio)(UnitStats*);
double __fastcall StrengthRatioRecord(UnitStats* self) {
    auto v = Zeroed<RatioCall>();
    v.stat = self->stat10;
    v.setting = GameLogicSettings().hpVal[0];
    std::uint16_t cw = ControlWord();
    double result = s_strengthRatio(self);
    v.result = result;
    g_strengthRatio.sampler.Offer(v, cw);
    return result;
}
double __fastcall DexterityRatioRecord(UnitStats* self) {
    auto v = Zeroed<RatioCall>();
    v.stat = self->stat18;
    v.setting = GameLogicSettings().mpVal[0];
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
    v.stat10 = self->stat10;
    v.stat18 = self->stat18;
    v.range = ei::GameData<const float>(kFactorStatRange);
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
    v.skillVal[0] = GameLogicSettings().skillVal[0];
    v.skillVal[1] = GameLogicSettings().skillVal[1];
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
    const SkillDatabase* db = SkillDb();
    auto* level = reinterpret_cast<const SkillLevelRecord*>(record);
    const std::ptrdiff_t offset = record - reinterpret_cast<const char*>(db->levels.begin());
    const std::ptrdiff_t stride = sizeof(SkillLevelRecord);
    const std::ptrdiff_t size = static_cast<std::ptrdiff_t>(db->levels.size()) * stride;
    v.index = offset >= 0 && offset < size && offset % stride == 0 ? static_cast<std::int32_t>(offset / stride) : -1;
    v.exponent = level->WorthExponent();
    if (v.index % 3 > 0) v.previousExponent = db->levels[static_cast<std::size_t>(v.index - 1)].WorthExponent();
    std::memcpy(v.skillLevels, self->skillLevels, sizeof(v.skillLevels));
    v.skillVal[0] = GameLogicSettings().skillVal[0];
    v.skillVal[1] = GameLogicSettings().skillVal[1];
    v.perkPowerBase = GameLogicSettings().perkPowerBase;
    std::uint16_t cw = ControlWord();
    int result = s_skillPrice(self, nullptr, record);
    v.result = result;
    g_skillPrice.sampler.Offer(v, cw);
    return result;
}

// 0051FF00
Recorded<IndexOfLevelCall> g_indexOfLevel("SkillDatabase::IndexOfLevel");
int(__fastcall* s_indexOfLevel)(SkillDatabase*, void*, const SkillLevelRecord*);
int __fastcall IndexOfLevelRecord(SkillDatabase* self, void*, const SkillLevelRecord* record) {
    auto v = Zeroed<IndexOfLevelCall>();
    v.count = static_cast<std::uint32_t>(self->levels.size());
    v.offset = static_cast<std::int32_t>(reinterpret_cast<const char*>(record) -
                                         reinterpret_cast<const char*>(self->levels.begin()));
    std::uint16_t cw = ControlWord();
    int result = s_indexOfLevel(self, nullptr, record);
    v.result = result;
    if (self == SkillDb()) g_indexOfLevel.sampler.Offer(v, cw);
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
            if (slot.type < 0 || slot.type >= 8) recordable = false;
            else view.weight = ei::GameData<const float>(kSlotWeights + 4 * slot.type);
        }
    }
    std::memcpy(v.vitals, vitals->protection, sizeof(v.vitals));
    v.keyed = keyed;
    if (!keyed) {
        v.difficulty = GameDifficulty();
        if (v.difficulty < 0 || v.difficulty >= 3) recordable = false;
        else v.factor = GameLogicSettings().absorption[v.difficulty];
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
    v.stat10 = self->stat10;
    v.stat18 = self->stat18;
    v.attackRate = self->attackRate;
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
        auto* record = reinterpret_cast<const itemdb::WeaponRecord*>(weapon);
        v.weapon = 1;
        v.bonus[0] = record->skillBonus[0];
        v.bonus[1] = record->skillBonus[1];
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
    IsaacRandom& rng = IsaacRandom::Global();
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

EI_RECORD(0x00523CD0, Period20Record, 6, s_period20);
EI_RECORD(0x00525DF0, AttributeBaseRecord, 6, s_attributeBase);
EI_RECORD(0x00525E10, AttributeWithBonusRecord, 7, s_attributeWithBonus);
EI_RECORD(0x00527030, SkillBaseRecord, 6, s_skillBase);
EI_RECORD(0x00526780, WeaponSkillRecord, 7, s_weaponSkill);
EI_RECORD(0x005259B0, ReduceRecord, 6, s_reduce);
EI_RECORD(0x00524020, ReduceKind1Record, 6, s_reduceKind1);
EI_RECORD(0x00522A80, OpensRecord, 7, s_opens);
EI_RECORD(0x00522B40, ResetDamageRecord, 8, s_resetDamage);
EI_RECORD(0x00522B80, ElementalRecord, 9, s_elemental);
EI_RECORD(0x00522AE0, ExcessRecord, 6, s_excess);
EI_RECORD(0x00522B20, MaxRecord, 6, s_max);
EI_RECORD(0x00527050, SkillWithBonusRecord, 6, s_skillWithBonus);
EI_RECORD(0x005267E0, SkillLevelValueRecord, 7, s_skillLevelValue);
EI_RECORD(0x00524280, Value6E8Record, 6, s_value6E8);
EI_RECORD(0x00525CD0, Value6E9Record, 6, s_value6E9);
EI_RECORD(0x00523CB0, StrengthRatioRecord, 9, s_strengthRatio);
EI_RECORD(0x00523CC0, DexterityRatioRecord, 9, s_dexterityRatio);
EI_RECORD(0x00528840, GetFactorsRecord, 9, s_getFactors);
EI_RECORD(0x00527FF0, SchoolPriceRecord, 9, s_schoolPrice);
EI_RECORD(0x00528050, SkillPriceRecord, 6, s_skillPrice);
EI_RECORD(0x0051FF00, IndexOfLevelRecord, 6, s_indexOfLevel);
EI_RECORD(0x005270E0, ProtectionBlockRecord, 5, s_protectionBlock);
EI_RECORD(0x00527850, WeaponSkillBonusRecord, 7, s_weaponSkillBonus);
EI_RECORD(0x00525B20, PickLocationRecord, 7, s_pickLocation);
