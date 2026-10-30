// "hudstate" (harness.cpp, HudState): the battle screen's state logged, compiled in image/harness_objects_image.cpp
// over our classes. The including file defines UnitQuickItem, UnitSpell, UnitWeapon (the unit's slots) and
// WeaponType (ItemWeapon::GetType).

//
// Logs the pending action (pendingAction, pendingSlot, pendingItem), each party member's selection (flags 0x10)
// and quick items (slots 0-3), and the quick item panel's (quickItems) unit, entries shown and region items, the spell
// column's (spellColumn) count and highlight. With <minquick>, fails when the quick item panel shows fewer than that many of its unit's own quick items
// (regions 0-3).
std::string HudStateOf(void* window, const std::string& argument) {
    auto* battle = static_cast<BattleScreen*>(window);
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", window, battle->pendingAction,
        battle->pendingSlot, battle->pendingItem, std::ssize(battle->party));
    for (int i = 0; i < std::ssize(battle->party) && i < 8; ++i) {
        Unit* member = battle->party[i];
        if (!member) continue;
        void* quick[4];
        for (int s = 0; s < 4; ++s) quick[s] = UnitQuickItem(member, s);
        Log("harness: hudstate: member %d %p flags %08X, quick items %p %p %p %p", i, member,
            member->flags, quick[0], quick[1], quick[2], quick[3]);
    }
    HudQuickItems* quickPanel = battle->quickItems;
    int own = 0;
    if (quickPanel) {
        for (int r = 0; r < 4; ++r) {
            int entry = quickPanel->regionItem[r];
            if (entry >= 0 && entry < std::ssize(quickPanel->entries) && quickPanel->entries[entry].own) ++own;
        }
        const int* items = quickPanel->regionItem;
        Log("harness: hudstate: quick panel unit %p, %d entries (%d own), highlight %d, regions %d %d %d %d %d %d %d %d",
            quickPanel->unit, std::ssize(quickPanel->entries), own, quickPanel->selected, items[0], items[1], items[2],
            items[3], items[4], items[5], items[6], items[7]);
        // The player's quest items in the top row: the id a lever's lock matches (SetScience).
        for (int e = 0; e < std::ssize(quickPanel->entries); ++e) {
            const HudQuickItems::Entry& entry = quickPanel->entries[e];
            if (!entry.own && entry.item && entry.item->type == 0x3009)
                Log("harness: hudstate: quick panel entry %d: quest item id %u (object %d)", e,
                    static_cast<ItemQuest*>(entry.item)->questItemId, entry.id);
        }
    }
    if (HudSpellColumn* spells = battle->spellColumn) {
        Log("harness: hudstate: spell column unit %p, %d spells, highlight %d", static_cast<void*>(spells->unit), spells->count,
            spells->selected);
        // Each spell slot of the column's unit: the spell's prototype index and target flags.
        if (Unit* unit = spells->unit) {
            for (unsigned s = 0; s < 8; ++s) {
                Item* item = UnitSpell(unit, s);
                if (!item) continue;
                auto* spell = static_cast<Spell*>(item->spell);
                Log("harness: hudstate: spell slot %u item %p, prototype %d, flags %08X", s, item,
                    spell ? spell->prototypeIndex : -1, spell ? spell->targetFlags : 0);
            }
            // Its weapons (their kind: 5 bow, 6 crossbow) and the one in use.
            for (unsigned w = 0; w < 4; ++w)
                if (ItemWeapon* weapon = UnitWeapon(unit, w))
                    Log("harness: hudstate: weapon slot %u item %p kind %d%s", w, static_cast<void*>(weapon),
                        WeaponType(weapon), unit->activeWeapon == w ? " (in use)" : "");
        }
    }
    if (!argument.empty()) {
        int minimum = std::atoi(argument.c_str());
        if (own < minimum) return "hudstate: the quick item panel shows " + std::to_string(own) + " own items, wanted " +
                                  std::to_string(minimum);
    }
    return "";
}
