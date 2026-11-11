#pragma once

// What an object's active effects do to its stats (docs/modding.md, "Active effects"; docs/notes/items_spells.md,
// "Active effects on scripts"): each kind of effect on it (the spell record whose script holds its OnStats:
// SpellRecord::effectKind) adds to the totals below, which the unit's recompute, regeneration, hits, scale, stun and
// casting read.

#include "lib/enum_array.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

class MapObject;

// A total the effects give, by its name in the scripts (kEffectStatNames). Each starts at 0 and is read where the
// comment says.
enum class EffectStat : std::uint8_t {
    FireProtection,       // added to the fire protection (UnitStats::Recompute)
    LightningProtection,  // added to the lightning protection
    AcidProtection,       // added to the acid protection
    Sight,                // added to the sight perception
    DaySight,             // added to the sight's percentage by day
    NearSense,            // added to the near sense
    SightConspicuity,     // added to how visible the unit is, which then stays at least 0
    HearingConspicuity,   // added to how loud it is, at least 0
    NearConspicuity,      // added to how near it is sensed, at least 0
    SmellConspicuity,     // added to how it smells
    Haste,                // added to the action rate, which stays at least 1
    // The hit points times (1 + Might x 0.005) / (1 + Frailty x 0.005), at least 1; a unit's damage likewise with
    // 0.003 (unit_vitals.cpp's EffectScale).
    Might,
    Frailty,
    Regeneration,         // the hit points' regeneration times this, when above 0 (UnitVitals::Regenerate)
    // The figure's steps of 0.4 (MapObject::RecomputeScale): with Size 0 Girth widens it (x, and z but for the hero);
    // else every axis by Size + Girth steps.
    Girth,
    Size,
    Stunned,              // above 0: stunned (MapObjectFlag::Frozen, no actions)
    NoCasting,            // above 0: no spells (but by a unit whose natural weapon is spells only)
    Count,
};

inline constexpr std::size_t kEffectStatCount = static_cast<std::size_t>(EffectStat::Count);

// The totals' names in the scripts (stats:Add("Haste", ...)), in EffectStat's order.
inline constexpr std::array<std::string_view, kEffectStatCount> kEffectStatNames = {
    "FireProtection",   "LightningProtection", "AcidProtection",  "Sight",
    "DaySight",         "NearSense",           "SightConspicuity", "HearingConspicuity",
    "NearConspicuity",  "SmellConspicuity",    "Haste",           "Might",
    "Frailty",          "Regeneration",        "Girth",           "Size",
    "Stunned",          "NoCasting",
};

using EffectStats = lib::EnumArray<float, EffectStat, kEffectStatCount>;

// The totals of an object without effects (or without a server half: the client knows no effect's value).
inline constexpr EffectStats kNoEffectStats{};

// One kind of effect on an object: its record's place, the strongest value of its effects (MapEffect::value, from 0
// and by MaxKeepB as the original took it: a NaN wins, then loses to the next) and the totals its OnStats touched.
struct EffectKindStats {
    int kind = -1;
    float value = 0.0f;
    std::bitset<kEffectStatCount> touched;
};

// A server object's effect totals (MapObjectServer::effectStats), worked out again only when its kinds or their
// strongest values changed: a kind's OnStats sees nothing but its kind and value, and the world's bindings are off in
// it.
class ActiveEffectStats {
public:
    // The totals of the object's effects: each kind's OnStats in the server's Lua state, by ascending kind.
    const EffectStats& Of(MapObject& object);
    // Whether the kind's OnStats touched any of `stats` (false for a kind not on the object at the last Of).
    bool Touches(int kind, std::initializer_list<EffectStat> stats) const;

private:
    std::vector<EffectKindStats> kinds;     // the kinds of `totals`, ascending
    std::vector<EffectKindStats> current;   // the object's kinds now (Of's scratch, kept for its capacity)
    EffectStats totals;
};
