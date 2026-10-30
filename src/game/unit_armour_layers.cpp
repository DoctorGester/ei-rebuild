// Creature statistics: the armour layers of the body slots, the body's packed protection and two skill values
// (an attribute with the quick items' bonuses, a spell school's skill). See unit.h (UnitStats) and
// docs/notes/units.md ("Statistics in play").
#include "unit.h"

#include "item.h"
#include "object_tables.h"

#include "lib/rounding.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kStatBase = 25.0f;

constexpr int kNoSlot = 7;              // an entry of the coverage table that covers nothing
constexpr int kBodySlot = 1;
constexpr int kLayerThere = 2;          // UnitStatLayer::state from which a layer counts
constexpr int kLayerAdded = 3;
constexpr int kQuickItems = 4;
constexpr int kArmourSlots = 7;         // CUnit::armor

// For each armour type, the three body slots it covers (kNoSlot: none).
constexpr int kArmourCoverage[7][3] = /*0073E048*/ {{0, 7, 7}, {1, 2, 3}, {7, 4, 5}, {1, 2, 3}, {7, 4, 5}, {7, 4, 5}, {7, 2, 3}};

int CoveredSlot(int type, int which) { return kArmourCoverage[type][which]; }

} // namespace

void UnitStats::AddArmourLayer(ItemArmor* piece) {
    for (int which = 0; which < 3; ++which) {
        int slotIndex = CoveredSlot(piece->GetType(), which);
        if (slotIndex == kNoSlot) continue;
        UnitStatSlot& slot = slots[slotIndex];
        UnitStatLayer& layer = slot.layers[piece->GetTypeGroup()];
        if (slot.kind == 0) continue;
        int part = which != 0 ? 1 : 0;
        for (int k = 0; k < 7; ++k) layer.absorb[k] = layer.absorb[k] + piece->absorb[part][k];
        layer.state = kLayerAdded;
        layer.layerMask |= static_cast<int>(piece->layerMask[part]);
    }
}

void UnitStats::RebuildArmourLayers(CUnit* unit) {
    for (UnitStatSlot& slot : slots) std::memset(&slot.layers[1], 0, 2 * sizeof(UnitStatLayer));
    for (int i = 0; i < kArmourSlots; ++i) {
        ItemArmor* piece = unit->armor[i];
        if (!piece) continue;
        float condition = piece->condition;
        if (lib::RoundEven(condition) > 0) AddArmourLayer(piece);   // to nearest
    }
}

void UnitStats::PackBodyProtection(CUnit* unit, const UnitVitals* vitals, DamageWords* out) {
    bool keyed = IsTextKey(unit->id);
    int top = keyed ? 2 : 0;
    const UnitStatSlot& body = slots[kBodySlot];
    for (int k = 0; k < 7; ++k) {
        float value = vitals->protection[k].current;
        for (int j = top; j >= 0; --j) {
            if (body.layers[j].state >= kLayerThere) value += body.layers[j].absorb[k];
        }
        out->words[1 + k] = static_cast<std::uint16_t>(lib::RoundEven(value));
    }
}

float UnitStats::AttributeWithItems(int index, CUnit* unit) {
    if (!IsTextKey(unit->id)) return attributes[index];    // any index
    float best = 0.0f;
    for (std::uint32_t i = 0; i < kQuickItems; ++i) {
        Item* item = unit->GetQuickItem(i);
        if (!item) continue;
        const auto* prototype = itemdb::RecordAs<itemdb::QuickItemRecord>(item->GetPrototype());
        float bonus = static_cast<float>(prototype->attributeBonus[index]);   // any index
        best = best >= bonus ? best : bonus;   // a select
    }
    switch (index) {
    case 0: return (dexterity - kStatBase) + attributes[0] + best;
    case 1: return (dexterity - kStatBase) + attributes[1] + best;
    case 2: return (intelligence - kStatBase) + attributes[2] + best;
    default: return 0.0f + best;
    }
}

float UnitStats::SpellSchoolSkill(int school) {
    if (school == 4) school = 3;
    else if (school == 6 || school == 7) school = 5;
    float base = 0.0f;
    if (school >= 0 && school <= 2) base = skills[3];
    else if (school >= 3 && school <= 4) base = skills[4];
    else if (school >= 5 && school <= 7) base = skills[5];
    // skillLevels[school + 7] and its record.
    float level = SkillLevelValue(school + 7);

    return (base + level) + (intelligence - kStatBase);
}
