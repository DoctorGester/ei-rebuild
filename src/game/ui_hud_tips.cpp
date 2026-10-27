// The battle screen panels' tooltips: the weapon bar's unit, a quick item's tip
// and the action column's unit. See ui.h and docs/notes/ui.md ("Battle screen").
#include "ui.h"
#include "game_strings.h"

#include "item.h"
#include "item_db_text.h"
#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "game_data.h"
#include "game_singletons.h"
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
    mfc::CString tip;
    mfc::CString description;
    mfc::CString line;
    unit_ = unit;
    ClearSceneObjects();
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 4; ++i) regions_.data[i]->SetTip(0, 1, 0);
    if (!unit_) return;

    for (unsigned slot = 0; slot < 4; ++slot) {
        CItemWeapon* weapon = unit_->GetWeapon(slot);
        if (!weapon) continue;
        {
            std::string image = weapon->FigureName().GetString();   // the figure's name
            std::string model = weapon->PictureName().GetString();  // the picture's name
            AddWeapon(model.c_str(), image.c_str());
        }
        TheTextManager()->GetItemTexts(weapon, &tip, &description, &line);
        tip += "\n";   // "\n"

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        float types[7];
        weapon->GetDamageTypes(types);
        const float zero = 0.0f /*0073B858*/;
        unsigned type = 0;
        EI_NAN_AUDIT(types[0], types[1], types[2], types[3], types[4], types[5], types[6]);
        while (type < 7 && types[type] == zero) ++type;
        description = "Unknown";
        if (type < 7) {
            line.Format("damage_%d", type);
            TheTextManager()->GetString(line, &description);
        }
        line.Format("%s %d-%d %s\n", labels_[0].GetString(), Truncate(weapon->damage_),   // "%s %d-%d %s\n"
                    Truncate(weapon->damageSpread_ + weapon->damage_), description.GetString());
        tip += line;
        float value = weapon->reach_;
        int category = static_cast<int>(weapon->GetPrototype()->category);
        line.Format("%s %d  %s %d\n", labels_[1].GetString(), category, labels_[2].GetString(), Truncate(value));   // "%s %d  %s %d\n"
        tip += line;

        // A weapon with a spell: the spell's name ("spell <name>").
        if (CSpell* spell = weapon->spell_) {
            description = "Unknown";
            int found = TheTextManager()->CTextManager::Open(mfc::operator+(mfc::CString("spell "),
                                                SpellPrototype(spell)->code));
            if (found) TheTextManager()->ReadLine(&description, 1);
            line.Format("%s %s\n", labels_[3].GetString(), description.GetString());   // "%s %s\n"
            tip += line;
        }
        regions_.data[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x26);
    }
    selected_ = static_cast<int>(unit_->pendingWeapon);
    if (selected_ >= count_) selected_ = -1;
    Highlight(selected_, 1);
}
EI_REWRITTEN(0x0063C180, CHudWeaponBar::SetUnit);

// --- A quick item's tooltip ----------------------------------------------------------

void CHudQuickItems::ItemTip(unsigned index, mfc::CString* text) {
    CItem* object = unit_->GetQuickItem(index);
    if (!object) return;
    auto* item = object;
    mfc::CString line;
    text->Empty();
    // (The original passes one CString as the description and the kind.)
    TheTextManager()->GetItemTexts(item, text, &line, &line);
    CSpell* data = item->spell_;
    if (!data) return;
    const float one = 1.0f /*0073B84C*/;
    const float pi = 3.1415927f /*007423F8*/;
    const float perTick = 0.06666667f /*00740544*/;   // 1/15

    if (item->GetCategory() == 5) {
        // A wand: level and charges, mana, duration, range, area, time, the last number.
        *text += " \n";   // " \n"
        float charges = item->chargeMax_;
        float level = data->manaCost_;
        int now = Truncate(item->charge_);
        int maximum = Truncate(charges);
        line.Format("%s %d/%d (%d)\n", labels_[0].GetString(), Truncate(level), maximum, now);   // "%s %d/%d (%d)\n"
        *text += line;
        if (SpellPrototype(data)->bytes78[3] == 0)
            line.Format("%s %s  ", labels_[1].GetString(), labels_[11].GetString());
        else
            line.Format("%s %d  ", labels_[1].GetString(), Truncate(data->damage_));
        *text += line;
        float duration = data->speed_;
        EI_NAN_AUDIT(duration);
        if (duration < one)
            line.Format("%s %s \n", labels_[2].GetString(), labels_[7].GetString());   // "%s %s \n"
        else
            line.Format("%s %d \n", labels_[2].GetString(), Truncate(duration));   // "%s %d \n"
        *text += line;
        line.Format("%s %d%s  ", labels_[3].GetString(), Truncate(data->range_), labels_[9].GetString());
        *text += line;
        if (data->targetFlags_ & 0x10000000) {
            line.Format("%s %s \n", labels_[4].GetString(), labels_[8].GetString());
        } else {
            float radius = data->radius_;
            double area = static_cast<double>(radius) * radius * pi;
            line.Format("%s %.1f%s \n", labels_[4].GetString(), area, labels_[9].GetString());   // "%s %.1f%s \n"
        }
        *text += line;
        std::uint32_t ticks = data->duration_;
        if (ticks < 2) {
            line.Format("%s %s  ", labels_[5].GetString(), labels_[7].GetString());
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line.Format("%s %.1f%s  ", labels_[5].GetString(), seconds, labels_[10].GetString());
        }
        *text += line;
        line.Format("%s %d \n", labels_[6].GetString(), static_cast<int>(data->targetCount_));
        *text += line;
    }
    if (item->GetCategory() == 8) {
        // A scroll: mana and time.
        *text += " \n";   // " \n"
        if (SpellPrototype(data)->bytes78[3] == 0)
            line.Format("%s %s\n", labels_[1].GetString(), labels_[11].GetString());   // "%s %s\n"
        else
            line.Format("%s %d\n", labels_[1].GetString(), Truncate(data->damage_));   // "%s %d\n"
        *text += line;
        std::uint32_t ticks = data->duration_;
        if (ticks < 2) {
            line.Format("%s %s\n", labels_[5].GetString(), labels_[7].GetString());
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line.Format("%s %.1f%s\n", labels_[5].GetString(), seconds, labels_[10].GetString());   // "%s %.1f%s\n"
        }
        *text += line;
    }
}
EI_REWRITTEN(0x0063CEE0, CHudQuickItems::ItemTip);

// --- The action column's unit --------------------------------------------------------

void CHudActionColumn::SetUnit(CUnit* unit, int extra) {
    unit_ = unit;
    ClearSceneObjects();
    actions_.SetSize(0, -1);
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 6; ++i) regions_.data[i]->SetTip(0, 1, 0);
    if (static_cast<std::uint8_t>(extra) != 0) {
        AddAction("initqi1item", "skill0011", -1);   // "initqi1item", "skill0011"
        regions_.data[count_ - 1]->SetTip(0x290E, 1, 0x1C);
    }
    CUnit* shown = unit_;
    if (!shown) return;

    // The science button (the original loops once over a switch of three buttons).
    std::string title;
    mfc::CString tip;
    mfc::CString hint;
    AddAction("initqi1item", "skill0009", 0);
    title = "Science";
    int found = TheTextManager()->CTextManager::Open(mfc::operator+(mfc::CString("perk "), title));
    if (found) TheTextManager()->ReadLine(&tip, 1);
    title = str::Format("%s %d\n", labels_[0].GetString(), shown->Params()->attributes[0]);   // "%s %d\n"
    if (TheTextManager()->CTextManager::Open("tip 10511") != 0) TheTextManager()->ReadToHash(&hint, 1);
    tip = mfc::operator+(mfc::operator+(mfc::operator+(tip, "\n"), title), hint);   // "\n"
    regions_.data[count_ - 1]->SetTipString(tip, 1, 0x1D);
}
EI_REWRITTEN(0x0063F800, CHudActionColumn::SetUnit);
