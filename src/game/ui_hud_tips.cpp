// The battle screen panels' tooltips: the weapon bar's unit (0063C180), a quick item's tip
// (0063CEE0) and the action column's unit (0063F800). See ui.h and docs/notes/ui.md ("Battle screen").
#include "ui.h"
#include "game_strings.h"

#include "item.h"
#include "item_db_text.h"
#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_engine.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"

namespace {


template <typename T> T Get(const void* base, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return v;
}

CTextManager* Texts() { return ei::GameData<CTextManager* const>(0x007AF788); }

// 004DEFC0 (thiscall on the text manager, RET 4): makes the text `key` current; non-zero when found.
// 0063BCC0 (thiscall, RET 8; Rebirth-patched): the weapon bar's next slot (model `model`, picture
// `image`), counted in +0x14C.
__declspec(naked) void __fastcall AddWeaponSprite(CHudWeaponBar*, void*, const char* /*model*/, const char* /*image*/)
    EI_ORIGINAL(0x0063BCC0)
// 0063F500 (thiscall, RET 0xC; Rebirth-patched): the action column's next button (model, picture,
// action), counted in +0x14C.
__declspec(naked) void __fastcall AddActionButton(CHudActionColumn*, void*, const char* /*model*/,
                                                  const char* /*image*/, int /*action*/) EI_ORIGINAL(0x0063F500)

// The item's slot 19 (the picture's name) and 20 (the model's name).
void ItemName(CEngineObject* item, int slot, ReturnedString* out) {
    using Fn = mfc::CString*(__thiscall*)(CEngineObject*, mfc::CString*);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(item))[slot])(item, out->Slot());
}

// The original truncates through _ftol.
int Truncate(float value) { return static_cast<int>(value); }

// The spell's prototype record (0068BA20) of a spell's data block (an item's +0x44).
std::uint8_t* SpellPrototype(void* spell) {
    return reinterpret_cast<std::uint8_t*>(static_cast<CSpell*>(spell)->GetPrototype());
}

} // namespace

// --- 0063C180: the weapon bar's unit -----------------------------------------------------------

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
        auto* weapon = static_cast<CItemWeapon*>(unit_->GetWeapon(slot));
        if (!weapon) continue;
        {
            ReturnedString image;
            ItemName(weapon, 19, &image);
            ReturnedString model;
            ItemName(weapon, 20, &model);
            AddWeaponSprite(this, nullptr, model.data, image.data);
        }
        Texts()->GetItemTexts(weapon, &tip, &description, &line);
        tip += "\n";   // "\n"

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        float types[7];
        weapon->GetDamageTypes(types);
        const float zero = ei::GameData<const volatile float>(0x0073B858);
        unsigned type = 0;
        while (type < 7 && !(types[type] < zero) && !(types[type] > zero)) ++type;
        description = "Unknown";
        if (type < 7) {
            line.Format("damage_%d", type);
            Texts()->GetString(line, &description);
        }
        line.Format("%s %d-%d %s\n", labels_[0], Truncate(weapon->damage_),   // "%s %d-%d %s\n"
                    Truncate(weapon->damageSpread_ + weapon->damage_), static_cast<const char*>(description));
        tip += line;
        float value = weapon->reach_;
        int category = Get<int>(weapon->GetPrototype(), 0x6C);
        line.Format("%s %d  %s %d\n", labels_[1], category, labels_[2], Truncate(value));   // "%s %d  %s %d\n"
        tip += line;

        // A weapon with a spell: the spell's name ("spell <name>").
        if (void* spell = weapon->spell_) {
            description = "Unknown";
            int found = TextsOpenSection(Texts(), nullptr,
                                 mfc::operator+(mfc::CString("spell "),
                                                *reinterpret_cast<const mfc::CString*>(SpellPrototype(spell) + 4)));
            if (found) Texts()->ReadLine(&description, 1);
            line.Format("%s %s\n", labels_[3], static_cast<const char*>(description));   // "%s %s\n"
            tip += line;
        }
        regions_.data[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x26);
    }
    selected_ = static_cast<int>(unit_->pendingWeapon);
    if (selected_ >= count_) selected_ = -1;
    Highlight(selected_, 1);
}
EI_REPLACE_METHOD(0x0063C180, CHudWeaponBar::SetUnit);

// --- 0063CEE0: a quick item's tooltip ----------------------------------------------------------

void CHudQuickItems::ItemTip(unsigned index, void* out) {
    CEngineObject* object = unit_->GetQuickItem(index);
    if (!object) return;
    auto* item = static_cast<CItem*>(object);
    auto* text = static_cast<mfc::CString*>(out);
    mfc::CString line;
    text->Empty();
    // (The original passes one CString as the description and the kind.)
    Texts()->GetItemTexts(item, text, &line, &line);
    auto* data = reinterpret_cast<std::uint8_t*>(item->spell_);
    if (!data) return;
    const float one = ei::GameData<const volatile float>(0x0073B84C);
    const float pi = ei::GameData<const volatile float>(0x007423F8);
    const float perTick = ei::GameData<const volatile float>(0x00740544);   // 1/15

    if (item->GetCategory() == 5) {
        // A wand: level and charges, mana, duration, range, area, time, the last number.
        *text += " \n";   // " \n"
        float charges = item->chargeMax_;
        float level = Get<float>(data, 0x40);
        int now = Truncate(item->charge_);
        int maximum = Truncate(charges);
        line.Format("%s %d/%d (%d)\n", labels_[0], Truncate(level), maximum, now);   // "%s %d/%d (%d)\n"
        *text += line;
        if (SpellPrototype(data)[0x7B] == 0)
            line.Format("%s %s  ", labels_[1], labels_[11]);
        else
            line.Format("%s %d  ", labels_[1], Truncate(Get<float>(data, 0x24)));
        *text += line;
        float duration = Get<float>(data, 0x30);
        if (!(duration >= one))   // TEST AH,1: below 1 or NaN
            line.Format("%s %s \n", labels_[2], labels_[7]);   // "%s %s \n"
        else
            line.Format("%s %d \n", labels_[2], Truncate(duration));   // "%s %d \n"
        *text += line;
        line.Format("%s %d%s  ", labels_[3], Truncate(Get<float>(data, 0x34)), labels_[9]);
        *text += line;
        if (Get<std::uint32_t>(data, 0x3C) & 0x10000000) {
            line.Format("%s %s \n", labels_[4], labels_[8]);
        } else {
            float radius = Get<float>(data, 0x28);
            double area = static_cast<double>(radius) * radius * pi;
            line.Format("%s %.1f%s \n", labels_[4], area, labels_[9]);   // "%s %.1f%s \n"
        }
        *text += line;
        std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
        if (ticks < 2) {
            line.Format("%s %s  ", labels_[5], labels_[7]);
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line.Format("%s %.1f%s  ", labels_[5], seconds, labels_[10]);
        }
        *text += line;
        line.Format("%s %d \n", labels_[6], Get<int>(data, 0x38));
        *text += line;
    }
    if (item->GetCategory() == 8) {
        // A scroll: mana and time.
        *text += " \n";   // " \n"
        if (SpellPrototype(data)[0x7B] == 0)
            line.Format("%s %s\n", labels_[1], labels_[11]);   // "%s %s\n"
        else
            line.Format("%s %d\n", labels_[1], Truncate(Get<float>(data, 0x24)));   // "%s %d\n"
        *text += line;
        std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
        if (ticks < 2) {
            line.Format("%s %s\n", labels_[5], labels_[7]);
        } else {
            double seconds = x87::FromUnsigned(ticks) * perTick;
            line.Format("%s %.1f%s\n", labels_[5], seconds, labels_[10]);   // "%s %.1f%s\n"
        }
        *text += line;
    }
}
EI_REPLACE_METHOD(0x0063CEE0, CHudQuickItems::ItemTip);

// --- 0063F800: the action column's unit --------------------------------------------------------

void CHudActionColumn::SetUnit(CUnit* unit, int extra) {
    unit_ = unit;
    ClearSceneObjects();
    actions_.SetSize(0, -1);
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 6; ++i) regions_.data[i]->SetTip(0, 1, 0);
    if (static_cast<std::uint8_t>(extra) != 0) {
        AddActionButton(this, nullptr, "initqi1item", "skill0011", -1);   // "initqi1item", "skill0011"
        regions_.data[count_ - 1]->SetTip(0x290E, 1, 0x1C);
    }
    CUnit* shown = unit_;
    if (!shown) return;

    // The science button (the original loops once over a switch of three buttons).
    mfc::CString title;
    mfc::CString tip;
    mfc::CString hint;
    AddActionButton(this, nullptr, "initqi1item", "skill0009", 0);
    title = "Science";
    int found = TextsOpenSection(Texts(), nullptr, mfc::operator+(mfc::CString("perk "), title));
    if (found) Texts()->ReadLine(&tip, 1);
    title.Format("%s %d\n", labels_[0], Get<std::uint8_t>(shown, 0x408));   // "%s %d\n"
    if (TextsOpenSection(Texts(), nullptr, "tip 10511") != 0) Texts()->ReadToHash(&hint, 1);
    tip = mfc::operator+(mfc::operator+(mfc::operator+(tip, "\n"), title), hint);   // "\n"
    regions_.data[count_ - 1]->SetTipString(tip, 1, 0x1D);
}
EI_REPLACE_METHOD(0x0063F800, CHudActionColumn::SetUnit);
