// Record mode for the "stats" family (stats_vectors.h): each recorder runs the original and offers the
// call to its sampler. Stolen bytes as in verify_unit_stats.cpp and verify_unit_small_a12.cpp. Reached
// by every scenario with units (the panel, the AI's attack options, hits and healing).
#include "stats_vectors.h"
#include "vectors.h"

#include "ei/hook.h"

#include <cstring>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::stats;

constexpr std::uint32_t kCap = 256;

template <typename T>
struct Recorded {
    Sampler sampler;
    explicit Recorded(const char* name) : sampler("stats", name, sizeof(T), kCap) {}
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
