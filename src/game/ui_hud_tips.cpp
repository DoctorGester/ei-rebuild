// The battle screen panels' tooltips: the weapon bar's unit, a quick item's tip
// and the action column's unit. See ui.h and docs/notes/ui.md ("Battle screen").
#include "ui.h"
#include "ui_float.h"

#include "item.h"
#include "text_manager.h"
#include "spell.h"
#include "unit.h"

#include <cstdint>
#include <string>

#include <cstring>
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include <format>

// --- The weapon bar's unit -----------------------------------------------------------

void HudWeaponBar::SetUnit(CUnit* newUnit) {
    std::string tip;
    std::string description;
    std::string line;
    unit = newUnit;
    ClearSceneObjects();
    count = 0;
    selected = -1;
    for (int i = 0; i < 4; ++i) regions[i]->SetTip(0);
    if (!unit) return;

    for (unsigned slot = 0; slot < 4; ++slot) {
        ItemWeapon* weapon = unit->GetWeapon(slot);
        if (!weapon) continue;
        {
            std::string image = weapon->FigureName();
            std::string model = weapon->PictureName();
            AddWeapon(model, image);
        }
        tip = g_clientWorld.texts->GetItemTexts(weapon).name;
        tip += "\n";

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        float types[7];
        weapon->GetDamageTypes(types);
        const float zero = 0.0f;
        unsigned tipType = 0;
        EI_NAN_AUDIT(types[0], types[1], types[2], types[3], types[4], types[5], types[6]);
        while (tipType < 7 && types[tipType] == zero) ++tipType;
        description = "Unknown";
        if (tipType < 7) {
            line = std::format("damage_{}", tipType);
            description = g_clientWorld.texts->GetString(line);
        }
        line = std::format("{} {}-{} {}\n", labels[0], Truncate(weapon->damage),
                    Truncate(weapon->damageSpread + weapon->damage), description);
        tip += line;
        float value = weapon->reach;
        int category = static_cast<int>(weapon->GetPrototype()->category);
        line = std::format("{} {}  {} {}\n", labels[1], category, labels[2], Truncate(value));
        tip += line;

        // A weapon with a spell: the spell's name ("spell <name>").
        if (CSpell* spell = weapon->spell) {
            description = "Unknown";
            if (g_clientWorld.texts->TextManager::Open(std::string("spell ") + spell->GetPrototype()->code))
                description = g_clientWorld.texts->ReadLine(true);
            line = std::format("{} {}\n", labels[3], description);
            tip += line;
        }
        regions[slot]->SetTipString(tip, static_cast<int>(slot) + 0x26);
    }
    selected = static_cast<int>(unit->pendingWeapon);
    if (selected >= count) selected = -1;
    Highlight(selected, true);
}

// --- A quick item's tooltip ----------------------------------------------------------

std::string HudQuickItems::ItemTip(unsigned index) {
    std::string text;
    Item* item = unit->GetQuickItem(index);
    if (!item) return text;
    text = g_clientWorld.texts->GetItemTexts(item).name;
    std::string line;
    CSpell* data = item->spell;
    if (!data) return text;
    const float one = 1.0f;
    const float pi = 3.1415927f;
    const float perTick = 0.06666667f;   // 1/15

    if (item->GetCategory() == QuickItemCategory::Wand) {
        // A wand: level and charges, mana, duration, range, area, time, the last number.
        text += " \n";
        float charges = item->chargeMax;
        float level = data->manaCost;
        int now = Truncate(item->charge);
        int maximum = Truncate(charges);
        line = std::format("{} {}/{} ({})\n", labels[0], Truncate(level), maximum, now);
        text += line;
        if (data->GetPrototype()->allowedModifiers[3] == 0)
            line = std::format("{} {}  ", labels[1], labels[11]);
        else
            line = std::format("{} {}  ", labels[1], Truncate(data->damage));
        text += line;
        float duration = data->speed;
        EI_NAN_AUDIT(duration);
        if (duration < one)
            line = std::format("{} {} \n", labels[2], labels[7]);
        else
            line = std::format("{} {} \n", labels[2], Truncate(duration));
        text += line;
        line = std::format("{} {}{}  ", labels[3], Truncate(data->range), labels[9]);
        text += line;
        if (data->targetFlags & SpellTargetFlag::AtUnits) {
            line = std::format("{} {} \n", labels[4], labels[8]);
        } else {
            float radius = data->radius;
            float area = radius * radius * pi;
            line = str::Format("%s %.1f%s \n", labels[4], area, labels[9]);
        }
        text += line;
        std::uint32_t ticks = data->duration;
        if (ticks < 2) {
            line = std::format("{} {}  ", labels[5], labels[7]);
        } else {
            float seconds = ticks * perTick;
            line = str::Format("%s %.1f%s  ", labels[5], seconds, labels[10]);
        }
        text += line;
        line = std::format("{} {} \n", labels[6], static_cast<int>(data->targetCount));
        text += line;
    }
    if (item->GetCategory() == QuickItemCategory::Potion) {
        // A potion: mana and time.
        text += " \n";
        if (data->GetPrototype()->allowedModifiers[3] == 0)
            line = std::format("{} {}\n", labels[1], labels[11]);
        else
            line = std::format("{} {}\n", labels[1], Truncate(data->damage));
        text += line;
        std::uint32_t ticks = data->duration;
        if (ticks < 2) {
            line = std::format("{} {}\n", labels[5], labels[7]);
        } else {
            float seconds = ticks * perTick;
            line = str::Format("%s %.1f%s\n", labels[5], seconds, labels[10]);
        }
        text += line;
    }
    return text;
}

// --- The action column's unit --------------------------------------------------------

void HudActionColumn::SetUnit(CUnit* newUnit, bool extra) {
    unit = newUnit;
    ClearSceneObjects();
    actions.clear();
    count = 0;
    selected = -1;
    for (int i = 0; i < 6; ++i) regions[i]->SetTip(0);
    if (extra) {
        AddAction("initqi1item", "skill0011", -1);
        regions[count - 1]->SetTip(0x290E, 0x1C);
    }
    CUnit* shown = unit;
    if (!shown) return;

    // The science button (the original loops once over a switch of three buttons).
    std::string title;
    std::string tip;
    std::string hint;
    AddAction("initqi1item", "skill0009", 0);
    title = "Science";
    if (g_clientWorld.texts->TextManager::Open(std::string("perk ") + title)) tip = g_clientWorld.texts->ReadLine(true);
    title = std::format("{} {}\n", labels[0], shown->params.attributes[0]);
    if (g_clientWorld.texts->TextManager::Open("tip 10511") != 0) hint = g_clientWorld.texts->ReadToHash();
    tip = tip + "\n" + title + hint;
    regions[count - 1]->SetTipString(tip, 0x1D);
}
