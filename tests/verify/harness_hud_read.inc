// "hudstate" (harness.cpp, HudState): the battle screen's state logged, over either side's classes. Compiled twice:
// in harness.cpp over game.exe's views (exe_objects.h) and in image/harness_objects_image.cpp over our classes. The
// including file defines UnitQuickItem, UnitSpell, UnitWeapon (the unit's slots, CUnit 00531920 / 005319A0 /
// 005318B0) and WeaponType (CItemWeapon::GetType 006A07F0) for its side.
//
// Logs the pending action (pendingAction_, pendingSlot_, pendingItem_), each party member's selection (flags_ 0x10)
// and quick items (slots 0-3), and the quick item panel's (quickItems_) unit, entries shown and region items, the spell
// column's (spellColumn_) count and highlight. With <minquick>, fails when the quick item panel shows fewer than that many of its unit's own quick items
// (regions 0-3).
std::string HudStateOf(void* window, const std::string& argument) {
    auto* battle = static_cast<CBattleScreen*>(window);
    Log("harness: hudstate: window %p, mode %d, slot %d, item %d, %d members", window, battle->pendingAction_,
        battle->pendingSlot_, battle->pendingItem_, std::ssize(battle->party_));
    for (int i = 0; i < std::ssize(battle->party_) && i < 8; ++i) {
        CUnit* member = battle->party_[i];
        if (!member) continue;
        void* quick[4];
        for (int s = 0; s < 4; ++s) quick[s] = UnitQuickItem(member, s);
        Log("harness: hudstate: member %d %p flags %08X, quick items %p %p %p %p", i, member,
            member->flags_, quick[0], quick[1], quick[2], quick[3]);
    }
    CHudQuickItems* quickPanel = battle->quickItems_;
    int own = 0;
    if (quickPanel) {
        for (int r = 0; r < 4; ++r) {
            int entry = quickPanel->regionItem_[r];
            if (entry >= 0 && entry < std::ssize(quickPanel->entries_) && quickPanel->entries_[entry].own) ++own;
        }
        const int* items = quickPanel->regionItem_;
        Log("harness: hudstate: quick panel unit %p, %d entries (%d own), highlight %d, regions %d %d %d %d %d %d %d %d",
            quickPanel->unit_, std::ssize(quickPanel->entries_), own, quickPanel->selected_, items[0], items[1], items[2],
            items[3], items[4], items[5], items[6], items[7]);
        // The player's quest items in the top row: the id a lever's lock matches (SetScience).
        for (int e = 0; e < std::ssize(quickPanel->entries_); ++e) {
            const CHudQuickItems::Entry& entry = quickPanel->entries_[e];
            if (!entry.own && entry.item && entry.item->type_ == 0x3009)
                Log("harness: hudstate: quick panel entry %d: quest item id %u (object %d)", e,
                    static_cast<CItemQuest*>(entry.item)->questItemId_, entry.id);
        }
    }
    if (CHudSpellColumn* spells = battle->spellColumn_) {
        Log("harness: hudstate: spell column unit %p, %d spells, highlight %d", static_cast<void*>(spells->unit_), spells->count_,
            spells->selected_);
        // Each spell slot of the column's unit: the spell's prototype index and target flags.
        if (CUnit* unit = spells->unit_) {
            for (unsigned s = 0; s < 8; ++s) {
                CItem* item = UnitSpell(unit, s);
                if (!item) continue;
                auto* spell = static_cast<CSpell*>(item->spell_);
                Log("harness: hudstate: spell slot %u item %p, prototype %d, flags %08X", s, item,
                    spell ? spell->prototypeIndex_ : -1, spell ? spell->targetFlags_ : 0);
            }
            // Its weapons (their kind: 5 bow, 6 crossbow) and the one in use.
            for (unsigned w = 0; w < 4; ++w)
                if (CItemWeapon* weapon = UnitWeapon(unit, w))
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
