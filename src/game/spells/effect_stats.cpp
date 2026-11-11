// An object's active effects' stat totals. See effect_stats.h and docs/notes/items_spells.md, "Active effects on
// scripts".
#include "game/spells/effect_stats.h"

#include "game/objects/map_object.h"
#include "game/script/script.h"
#include "game/script/script_lua_scheduler.h"
#include "game/world/world.h"

#include "lib/float_select.h"

#include <algorithm>
#include <bit>

namespace {

bool SameKinds(const std::vector<EffectKindStats>& a, const std::vector<EffectKindStats>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const EffectKindStats& x, const EffectKindStats& y) {
        return x.kind == y.kind && std::bit_cast<std::uint32_t>(x.value) == std::bit_cast<std::uint32_t>(y.value);
    });
}

} // namespace

const EffectStats& ActiveEffectStats::Of(MapObject& object) {
    current.clear();
    for (const MapEffect& effect : object.effects) {
        const int kind = effect.Kind();
        if (kind < 0) continue;
        auto at = std::lower_bound(current.begin(), current.end(), kind,
                                   [](const EffectKindStats& entry, int value) { return entry.kind < value; });
        if (at == current.end() || at->kind != kind) at = current.insert(at, EffectKindStats{kind});
        at->value = MaxKeepB(at->value, effect.value);
    }
    if (SameKinds(current, kinds)) return totals;
    totals = {};
    WorldServer* world = object.owner ? object.ServerWorld() : nullptr;
    if (current.empty() || !world) {
        kinds.clear();
        return totals;
    }
    std::swap(kinds, current);
    world->MainScript()->Runner().spells.EffectTotals(kinds, totals);
    return totals;
}

bool ActiveEffectStats::Touches(int kind, std::initializer_list<EffectStat> stats) const {
    const auto at = std::lower_bound(kinds.begin(), kinds.end(), kind,
                                     [](const EffectKindStats& entry, int value) { return entry.kind < value; });
    if (at == kinds.end() || at->kind != kind) return false;
    return std::any_of(stats.begin(), stats.end(), [&](EffectStat stat) { return at->touched[lib::Index(stat)]; });
}
