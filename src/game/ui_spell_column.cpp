// The spell column's unit (0063E820): a sprite and a tooltip per spell of the unit. See ui.h
// (CHudSpellColumn::SetUnit) and docs/notes/ui.md ("Battle HUD panels").
#include "item_db_text.h"
#include "spell.h"
#include "ui.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "unit.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace {


template <typename T> T Get(const void* base, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return v;
}

// 005319A0 (thiscall on a unit, RET 4): the unit's spell `index` (0..7) or null.
// 0063E4B0 (thiscall, RET 8; Rebirth-patched): the next spell's sprite (model `model`, picture
// `image`), counted in +0x14C.
__declspec(naked) void __fastcall AddSpellSprite(CHudSpellColumn*, void*, const char* /*model*/, const char* /*image*/)
    EI_ORIGINAL(0x0063E4B0)
// 004E0150 (thiscall on the text manager, RET 0x10): an item's texts; 004E0020 (RET 8) the text of
// a key.
// 0068BA20 (__fastcall): a spell's prototype record (+0x10 school, +0x7B shows a number).
std::uint8_t* SpellPrototypeOf(void* spell) {
    return reinterpret_cast<std::uint8_t*>(static_cast<CSpell*>(spell)->GetPrototype());
}


// The spell's slot 19 (the picture's name) and 20 (the model's name).
void SpellName(CEngineObject* spell, int slot, ReturnedString* out) {
    using Fn = mfc::CString*(__thiscall*)(CEngineObject*, mfc::CString*);
    ei::ObjectSlot<Fn>(spell, slot)(spell, out->Slot());
}

// The original truncates through _ftol.
int Truncate(float value) { return static_cast<int>(value); }

} // namespace

void CHudSpellColumn::SetUnit(CUnit* unit) {
    mfc::CString tip;
    mfc::CString description;
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
            ReturnedString image;
            SpellName(spell, 19, &image);
            ReturnedString model;
            SpellName(spell, 20, &model);
            AddSpellSprite(this, nullptr, model.data, image.data);
        }
        TheTextManager()->GetItemTexts(spell, &tip, &description, &description);

        // The spell's numbers below its name: one line per label of the panel's table (+0x15C).
        std::uint8_t* data = Get<std::uint8_t*>(spell, 0x44);
        if (data) {
            const mfc::CStringArray& labels = labels_;
            mfc::CString line;
            tip += " \n";   // " \n"
            line.Format("%s %d  ", static_cast<const char*>(labels[0]), Truncate(Get<float>(data, 0x40)));
            tip += line;
            line = "Unknown";
            description.Format("school_%d", Get<int>(SpellPrototypeOf(data), 0x10));
            TextsGetString(TheTextManager(), nullptr, description, &line);
            line = mfc::operator+(mfc::operator+(mfc::operator+(labels[1],
                                                                " "),
                                                 line),
                                  " \n");
            tip += line;
            if (SpellPrototypeOf(data)[0x7B] == 0)
                line.Format("%s %s  ", static_cast<const char*>(labels[2]), static_cast<const char*>(labels[12]));
            else
                line.Format("%s %d  ", static_cast<const char*>(labels[2]), Truncate(Get<float>(data, 0x24)));
            tip += line;
            const float one = 1.0f /*0073B84C*/;
            float duration = Get<float>(data, 0x30);
            EI_NAN_AUDIT(duration);
            if (duration < one)
                line.Format("%s %s \n", static_cast<const char*>(labels[3]), static_cast<const char*>(labels[8]));   // "%s %s \n"
            else
                line.Format("%s %d \n", static_cast<const char*>(labels[3]), Truncate(duration));   // "%s %d \n"
            tip += line;
            line.Format("%s %d%s  ", static_cast<const char*>(labels[4]), Truncate(Get<float>(data, 0x34)), static_cast<const char*>(labels[10]));
            tip += line;
            if (Get<std::uint32_t>(data, 0x3C) & 0x10000000) {
                line.Format("%s %s \n", static_cast<const char*>(labels[5]), static_cast<const char*>(labels[9]));
            } else {
                // The area: radius squared times pi (007423F8, a float).
                float radius = Get<float>(data, 0x28);
                double area = static_cast<double>(radius) * radius * 3.1415927f /*007423F8*/;
                line.Format("%s %.1f%s \n", static_cast<const char*>(labels[5]), area, static_cast<const char*>(labels[10]));   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
            if (ticks < 2) {
                line.Format("%s %s  ", static_cast<const char*>(labels[6]), static_cast<const char*>(labels[8]));
            } else {
                // Ticks to seconds (00740544, 1/15).
                double seconds = x87::FromUnsigned(ticks) * 0.06666667f /*00740544*/;
                line.Format("%s %.1f%s  ", static_cast<const char*>(labels[6]), seconds, static_cast<const char*>(labels[11]));
            }
            tip += line;
            line.Format("%s %d \n", static_cast<const char*>(labels[7]), Get<int>(data, 0x38));
            tip += line;
        }
        // The region of the spell's index (the original does not count the missing ones).
        regions_.data[i]->SetTipString(tip, 1, static_cast<int>(i) + 0x1E);
    }
}
EI_REPLACE_METHOD(0x0063E820, CHudSpellColumn::SetUnit);
