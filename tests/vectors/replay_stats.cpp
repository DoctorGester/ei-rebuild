// Replay of the "stats" family (stats_vectors.h) against src/game/unit_stats.cpp. Doubles and
// words are compared bitwise, as the verify wrappers compare them.
#include "stats_vectors.h"
#include "vectors.h"

#include <cstdio>
#include <cstring>

// The two damage word functions are not declared in a header (unit_stats.cpp).
std::uint32_t __fastcall ExcessDamage(const std::uint16_t* damage, void*, const std::uint16_t* protection);
std::uint16_t __fastcall MaxDamage(const std::uint16_t* damage);

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

} // namespace
