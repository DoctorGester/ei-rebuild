// The spell column's unit: a sprite and a tooltip per spell of the unit. See ui.h
// (CHudSpellColumn::SetUnit) and docs/notes/ui.md ("Battle HUD panels").
#include "item_db_text.h"
#include "spell.h"
#include "ui.h"

#include "ei/x87.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "unit.h"
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <vector>

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


// The original truncates through _ftol.
int Truncate(float value) { return static_cast<int>(value); }

} // namespace

void CHudSpellColumn::SetUnit(CUnit* unit) {
    std::string tip;
    std::string description;
    unit_ = unit;
    ClearSceneObjects();
    ClearRegions();
    count_ = 0;
    selected_ = -1;
    if (!unit_) return;

    for (unsigned i = 0; i < 8; ++i) {
        CItem* spell = unit_->GetSpell(i);
        if (!spell) continue;
        {
            std::string image = spell->FigureName();
            std::string model = spell->PictureName();
            AddSpell(model, image);
        }
        g_clientWorld.texts_->GetItemTexts(spell, &tip, &description, &description);

        // The spell's numbers below its name: one line per label of the panel's table.
        CSpell* data = spell->spell_;   // the spell item's spell
        if (data) {
            const std::vector<std::string>& labels = labels_;
            std::string line;
            tip += " \n";   // " \n"
            line = str::Format("%s %d  ", labels[0], Truncate(Get<float>(data, 0x40)));
            tip += line;
            line = "Unknown";
            description = str::Format("school_%d", SpellPrototypeOf(data)->castType);
            g_clientWorld.texts_->CTextManager::GetString(description, &line);
            line = labels[1] + " " + line + " \n";
            tip += line;
            if (SpellPrototypeOf(data)->allowedModifiers[3] == 0)
                line = str::Format("%s %s  ", labels[2], labels[12]);
            else
                line = str::Format("%s %d  ", labels[2], Truncate(Get<float>(data, 0x24)));
            tip += line;
            const float one = 1.0f;
            float duration = Get<float>(data, 0x30);
            EI_NAN_AUDIT(duration);
            if (duration < one)
                line = str::Format("%s %s \n", labels[3], labels[8]);   // "%s %s \n"
            else
                line = str::Format("%s %d \n", labels[3], Truncate(duration));   // "%s %d \n"
            tip += line;
            line = str::Format("%s %d%s  ", labels[4], Truncate(Get<float>(data, 0x34)), labels[10]);
            tip += line;
            if (Get<std::uint32_t>(data, 0x3C) & 0x10000000) {
                line = str::Format("%s %s \n", labels[5], labels[9]);
            } else {
                // The area: radius squared times pi (a float).
                float radius = Get<float>(data, 0x28);
                double area = static_cast<double>(radius) * radius * 3.1415927f;
                line = str::Format("%s %.1f%s \n", labels[5], area, labels[10]);   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
            if (ticks < 2) {
                line = str::Format("%s %s  ", labels[6], labels[8]);
            } else {
                // Ticks to seconds (1/15).
                double seconds = x87::FromUnsigned(ticks) * 0.06666667f;
                line = str::Format("%s %.1f%s  ", labels[6], seconds, labels[11]);
            }
            tip += line;
            line = str::Format("%s %d \n", labels[7], Get<int>(data, 0x38));
            tip += line;
        }
        // The region of the spell's index (the original does not count the missing ones).
        regions_[i]->SetTipString(tip, static_cast<int>(i) + 0x1E);
    }
}
