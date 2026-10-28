// The battle screen panels' tooltips: the weapon bar's unit, a quick item's tip
// and the action column's unit. See ui.h and docs/notes/ui.md ("Battle screen").
#include "ui.h"

#include "item.h"
#include "item_db_text.h"
#include "spell.h"
#include "unit.h"

#include "ei/x87.h"
#include <cstdint>
#include <string>

#include <cstring>
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

namespace {
// The original truncates through _ftol.
int Truncate(float value) { return static_cast<int>(value); }

// The spell's prototype record of a spell's data block.
const itemdb::SpellRecord* SpellPrototype(CSpell* spell) {
    return spell->GetPrototype();
}

} // namespace

// --- The weapon bar's unit -----------------------------------------------------------

void CHudWeaponBar::SetUnit(CUnit* unit) {
    std::string tip;
    std::string description;
    std::string line;
    unit_ = unit;
    ClearSceneObjects();
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 4; ++i) regions_[i]->SetTip(0, 1, 0);
    if (!unit_) return;

    for (unsigned slot = 0; slot < 4; ++slot) {
        CItemWeapon* weapon = unit_->GetWeapon(slot);
        if (!weapon) continue;
        {
            std::string image = weapon->FigureName();
            std::string model = weapon->PictureName();
            AddWeapon(model, image);
        }
        g_clientWorld->texts_->GetItemTexts(weapon, &tip, &description, &line);
        tip += "\n";

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        float types[7];
        weapon->GetDamageTypes(types);
        const float zero = 0.0f;
        unsigned type = 0;
        EI_NAN_AUDIT(types[0], types[1], types[2], types[3], types[4], types[5], types[6]);
        while (type < 7 && types[type] == zero) ++type;
        description = "Unknown";
        if (type < 7) {
            line = str::Format("damage_%d", type);
            g_clientWorld->texts_->GetString(line, &description);
        }
        line = str::Format("%s %d-%d %s\n", labels_[0], Truncate(weapon->damage_),
                    Truncate(weapon->damageSpread_ + weapon->damage_), description);
        tip += line;
        float value = weapon->reach_;
        int category = static_cast<int>(weapon->GetPrototype()->category);
        line = str::Format("%s %d  %s %d\n", labels_[1], category, labels_[2], Truncate(value));
        tip += line;

        // A weapon with a spell: the spell's name ("spell <name>").
        if (CSpell* spell = weapon->spell_) {
            description = "Unknown";
            int found = g_clientWorld->texts_->CTextManager::Open((std::string("spell ") + SpellPrototype(spell)->code));
            if (found) g_clientWorld->texts_->ReadLine(&description, 1);
            line = str::Format("%s %s\n", labels_[3], description);
            tip += line;
        }
        regions_[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x26);
    }
    selected_ = static_cast<int>(unit_->pendingWeapon);
    if (selected_ >= count_) selected_ = -1;
    Highlight(selected_, 1);
}

// --- A quick item's tooltip ----------------------------------------------------------

void CHudQuickItems::ItemTip(unsigned index, std::string* text) {
    CItem* object = unit_->GetQuickItem(index);
    if (!object) return;
    auto* item = object;
    std::string line;
    text->clear();
    // (The original passes one string as the description and the kind.)
    g_clientWorld->texts_->GetItemTexts(item, text, &line, &line);
    CSpell* data = item->spell_;
    if (!data) return;
    const float one = 1.0f;
    const float pi = 3.1415927f;
    const float perTick = 0.06666667f;   // 1/15

    if (item->GetCategory() == kCategoryWand) {
        // A wand: level and charges, mana, duration, range, area, time, the last number.
        *text += " \n";
        float charges = item->chargeMax_;
        float level = data->manaCost_;
        int now = Truncate(item->charge_);
        int maximum = Truncate(charges);
        line = str::Format("%s %d/%d (%d)\n", labels_[0], Truncate(level), maximum, now);
        *text += line;
        if (SpellPrototype(data)->allowedModifiers[3] == 0)
            line = str::Format("%s %s  ", labels_[1], labels_[11]);
        else
            line = str::Format("%s %d  ", labels_[1], Truncate(data->damage_));
        *text += line;
        float duration = data->speed_;
        EI_NAN_AUDIT(duration);
        if (duration < one)
            line = str::Format("%s %s \n", labels_[2], labels_[7]);
        else
            line = str::Format("%s %d \n", labels_[2], Truncate(duration));
        *text += line;
        line = str::Format("%s %d%s  ", labels_[3], Truncate(data->range_), labels_[9]);
        *text += line;
        if (data->targetFlags_ & 0x10000000) {
            line = str::Format("%s %s \n", labels_[4], labels_[8]);
        } else {
            float radius = data->radius_;
            double area = static_cast<double>(radius) * radius * pi;
            line = str::Format("%s %.1f%s \n", labels_[4], area, labels_[9]);
        }
        *text += line;
        std::uint32_t ticks = data->duration_;
        if (ticks < 2) {
            line = str::Format("%s %s  ", labels_[5], labels_[7]);
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line = str::Format("%s %.1f%s  ", labels_[5], seconds, labels_[10]);
        }
        *text += line;
        line = str::Format("%s %d \n", labels_[6], static_cast<int>(data->targetCount_));
        *text += line;
    }
    if (item->GetCategory() == kCategoryPotion) {
        // A potion: mana and time.
        *text += " \n";
        if (SpellPrototype(data)->allowedModifiers[3] == 0)
            line = str::Format("%s %s\n", labels_[1], labels_[11]);
        else
            line = str::Format("%s %d\n", labels_[1], Truncate(data->damage_));
        *text += line;
        std::uint32_t ticks = data->duration_;
        if (ticks < 2) {
            line = str::Format("%s %s\n", labels_[5], labels_[7]);
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line = str::Format("%s %.1f%s\n", labels_[5], seconds, labels_[10]);
        }
        *text += line;
    }
}

// --- The action column's unit --------------------------------------------------------

void CHudActionColumn::SetUnit(CUnit* unit, int extra) {
    unit_ = unit;
    ClearSceneObjects();
    actions_.clear();
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 6; ++i) regions_[i]->SetTip(0, 1, 0);
    if (static_cast<std::uint8_t>(extra) != 0) {
        AddAction("initqi1item", "skill0011", -1);
        regions_[count_ - 1]->SetTip(0x290E, 1, 0x1C);
    }
    CUnit* shown = unit_;
    if (!shown) return;

    // The science button (the original loops once over a switch of three buttons).
    std::string title;
    std::string tip;
    std::string hint;
    AddAction("initqi1item", "skill0009", 0);
    title = "Science";
    int found = g_clientWorld->texts_->CTextManager::Open((std::string("perk ") + title));
    if (found) g_clientWorld->texts_->ReadLine(&tip, 1);
    title = str::Format("%s %d\n", labels_[0], shown->params.attributes[0]);
    if (g_clientWorld->texts_->CTextManager::Open("tip 10511") != 0) g_clientWorld->texts_->ReadToHash(&hint, 1);
    tip = tip + "\n" + title + hint;
    regions_[count_ - 1]->SetTipString(tip, 1, 0x1D);
}
