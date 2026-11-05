// The spell column's unit: a sprite and a tooltip per spell of the unit. See ui_hud.h
// (HudSpellColumn::SetUnit) and docs/notes/ui.md ("Battle HUD panels").
#include "text_manager.h"
#include "spell.h"
#include "ui.h"
#include "ui_hud.h"
#include "ui_float.h"

#include <cstdint>
#include <string>
#include "unit.h"
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"

#include <vector>
#include <format>

void HudSpellColumn::SetUnit(Unit* newUnit) {
    std::string tip;
    std::string description;
    unit = newUnit;
    ClearSceneObjects();
    ClearRegions();
    count = 0;
    selected = -1;
    if (!unit) return;

    for (unsigned i = 0; i < 8; ++i) {
        Item* spell = unit->GetSpell(i);
        if (!spell) continue;
        {
            std::string image = spell->FigureName();
            std::string model = spell->PictureName();
            AddSpell(model, image);
        }
        tip = g_clientWorld.texts->GetItemTexts(spell).name;

        // The spell's numbers below its name: one line per label of the panel's table.
        Spell* data = spell->spell;    // the spell item's spell
        if (data) {
            const std::vector<std::string>& currentLabels = labels;
            std::string line;
            tip += " \n";   // " \n"
            line = std::format("{} {}  ", currentLabels[0], Truncate(data->manaCost));
            tip += line;
            line = "Unknown";
            description = std::format("school_{}", data->GetPrototype()->castType);
            line = g_clientWorld.texts->TextManager::GetString(description);
            line = currentLabels[1] + " " + line + " \n";
            tip += line;
            if (data->GetPrototype()->allowedModifiers[3] == 0)
                line = std::format("{} {}  ", currentLabels[2], currentLabels[12]);
            else
                line = std::format("{} {}  ", currentLabels[2], Truncate(data->damage));
            tip += line;
            const float one = 1.0f;
            if (data->speed < one)
                line = std::format("{} {} \n", currentLabels[3], currentLabels[8]);
            else
                line = std::format("{} {} \n", currentLabels[3], Truncate(data->speed));
            tip += line;
            line = std::format("{} {}{}  ", currentLabels[4], Truncate(data->range), currentLabels[10]);
            tip += line;
            if (lib::Has(data->targetFlags, SpellTargetFlag::AtUnits)) {
                line = std::format("{} {} \n", currentLabels[5], currentLabels[9]);
            } else {
                // The area: radius squared times pi (a float).
                float area = data->radius * data->radius * 3.1415927f;
                line = str::Format("%s %.1f%s \n", currentLabels[5], area, currentLabels[10]);   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = data->duration;
            if (ticks < 2) {
                line = std::format("{} {}  ", currentLabels[6], currentLabels[8]);
            } else {
                // Ticks to seconds (1/15).
                float seconds = ticks * 0.06666667f;
                line = str::Format("%s %.1f%s  ", currentLabels[6], seconds, currentLabels[11]);
            }
            tip += line;
            line = std::format("{} {} \n", currentLabels[7], static_cast<int>(data->targetCount));
            tip += line;
        }
        // The slot's button is the one AddSpell just added (the original indexed the regions by slot, reading past
        // them after an empty slot); the tip's key is still by slot.
        regions.back()->SetTipString(tip, static_cast<int>(i) + 0x1E);
    }
}
