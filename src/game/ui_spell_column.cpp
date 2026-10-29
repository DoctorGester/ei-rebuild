// The spell column's unit: a sprite and a tooltip per spell of the unit. See ui.h
// (HudSpellColumn::SetUnit) and docs/notes/ui.md ("Battle HUD panels").
#include "text_manager.h"
#include "spell.h"
#include "ui.h"
#include "ui_float.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "unit.h"
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <vector>
#include <format>

namespace {


template <typename T> T Get(const void* base, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return v;
}

// The unit's spell `index` (0..7) or null.
// The next spell's sprite (model `model`, picture `image`; Rebirth-patched), counted in a member.
// An item's texts; the text of a key (the text manager).
// A spell's prototype record (school, a flag that shows a number).
const itemdb::SpellRecord* SpellPrototypeOf(CSpell* spell) {
    return spell->GetPrototype();
}


} // namespace

void HudSpellColumn::SetUnit(CUnit* newUnit) {
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
        CSpell* data = spell->spell;    // the spell item's spell
        if (data) {
            const std::vector<std::string>& currentLabels = labels;
            std::string line;
            tip += " \n";   // " \n"
            line = std::format("{} {}  ", currentLabels[0], Truncate(Get<float>(data, 0x40)));
            tip += line;
            line = "Unknown";
            description = std::format("school_{}", SpellPrototypeOf(data)->castType);
            line = g_clientWorld.texts->TextManager::GetString(description);
            line = currentLabels[1] + " " + line + " \n";
            tip += line;
            if (SpellPrototypeOf(data)->allowedModifiers[3] == 0)
                line = std::format("{} {}  ", currentLabels[2], currentLabels[12]);
            else
                line = std::format("{} {}  ", currentLabels[2], Truncate(Get<float>(data, 0x24)));
            tip += line;
            const float one = 1.0f;
            float duration = Get<float>(data, 0x30);
            EI_NAN_AUDIT(duration);
            if (duration < one)
                line = std::format("{} {} \n", currentLabels[3], currentLabels[8]);
            else
                line = std::format("{} {} \n", currentLabels[3], Truncate(duration));
            tip += line;
            line = std::format("{} {}{}  ", currentLabels[4], Truncate(Get<float>(data, 0x34)), currentLabels[10]);
            tip += line;
            if (Get<std::uint32_t>(data, 0x3C) & 0x10000000) {
                line = std::format("{} {} \n", currentLabels[5], currentLabels[9]);
            } else {
                // The area: radius squared times pi (a float).
                float radius = Get<float>(data, 0x28);
                float area = radius * radius * 3.1415927f;
                line = str::Format("%s %.1f%s \n", currentLabels[5], area, currentLabels[10]);   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
            if (ticks < 2) {
                line = std::format("{} {}  ", currentLabels[6], currentLabels[8]);
            } else {
                // Ticks to seconds (1/15).
                float seconds = ticks * 0.06666667f;
                line = str::Format("%s %.1f%s  ", currentLabels[6], seconds, currentLabels[11]);
            }
            tip += line;
            line = std::format("{} {} \n", currentLabels[7], Get<int>(data, 0x38));
            tip += line;
        }
        // The region of the spell's index (the original does not count the missing ones).
        regions[i]->SetTipString(tip, static_cast<int>(i) + 0x1E);
    }
}
