// The spell column's unit (0063E820): a sprite and a tooltip per spell of the unit. See ui.h
// (CHudSpellColumn::SetUnit) and docs/notes/ui.md ("Tenth pass").
#include "ui.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

template <typename T> T Get(const void* base, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return v;
}

// 005319A0 (thiscall on a unit, RET 4): the unit's spell `index` (0..7) or null.
__declspec(naked) CEngineObject* __fastcall SpellAt(CUnit*, void*, unsigned) EI_ORIGINAL(0x005319A0)
// 0063E4B0 (thiscall, RET 8; Rebirth-patched): the next spell's sprite (model `model`, picture
// `image`), counted in +0x14C.
__declspec(naked) void __fastcall AddSpellSprite(CHudSpellColumn*, void*, const char* /*model*/, const char* /*image*/)
    EI_ORIGINAL(0x0063E4B0)
// 004E0150 (thiscall on the text manager, RET 0x10): an item's texts; 004E0020 (RET 8) the text of
// a key.
__declspec(naked) BOOL __fastcall SpellTexts(void*, void*, CEngineObject*, mfc::CString*, mfc::CString*, mfc::CString*)
    EI_ORIGINAL(0x004E0150)
__declspec(naked) BOOL __fastcall SpellTextString(void*, void*, const char*, mfc::CString*) EI_ORIGINAL(0x004E0020)
// 0068BA20 (__fastcall): a spell's prototype record (+0x10 school, +0x7B shows a number).
__declspec(naked) std::uint8_t* __fastcall SpellPrototypeOf(void*) EI_ORIGINAL(0x0068BA20)

void* TextManager() { return *reinterpret_cast<void* const*>(0x007AF788); }

// A CString a spell's virtual method returns (by hidden pointer); released when it goes.
struct ReturnedString {
    char* data = nullptr;
    mfc::CString* Slot() { return reinterpret_cast<mfc::CString*>(&data); }
    ~ReturnedString() { Slot()->~CString(); }
};
// The spell's slot 19 (the picture's name) and 20 (the model's name).
void SpellName(CEngineObject* spell, int slot, ReturnedString* out) {
    using Fn = mfc::CString*(__thiscall*)(CEngineObject*, mfc::CString*);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(spell))[slot])(spell, out->Slot());
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
        CEngineObject* spell = SpellAt(unit_, nullptr, i);
        if (!spell) continue;
        {
            ReturnedString image;
            SpellName(spell, 19, &image);
            ReturnedString model;
            SpellName(spell, 20, &model);
            AddSpellSprite(this, nullptr, model.data, image.data);
        }
        SpellTexts(TextManager(), nullptr, spell, &tip, &description, &description);

        // The spell's numbers below its name: one line per label of the panel's table (+0x15C).
        std::uint8_t* data = Get<std::uint8_t*>(spell, 0x44);
        if (data) {
            char* const* labels = labels_;
            mfc::CString line;
            tip += GameText(0x00794520);   // " \n"
            line.Format(GameText(0x00794500), labels[0], Truncate(Get<float>(data, 0x40)));   // "%s %d  "
            tip += line;
            line = GameText(0x0078DDEC);   // "Unknown"
            description.Format(GameText(0x00792F50), Get<int>(SpellPrototypeOf(data), 0x10));   // "school_%d"
            SpellTextString(TextManager(), nullptr, description, &line);
            line = mfc::operator+(mfc::operator+(mfc::operator+(*reinterpret_cast<const mfc::CString*>(&labels[1]),
                                                                GameText(0x0078DA40)),   // " "
                                                 line),
                                  GameText(0x00794520));
            tip += line;
            if (SpellPrototypeOf(data)[0x7B] == 0)
                line.Format(GameText(0x00794508), labels[2], labels[12]);   // "%s %s  "
            else
                line.Format(GameText(0x00794500), labels[2], Truncate(Get<float>(data, 0x24)));
            tip += line;
            const float one = *reinterpret_cast<const volatile float*>(0x0073B84C);
            float duration = Get<float>(data, 0x30);
            if (!(duration >= one))   // TEST AH,1: below 1 or NaN
                line.Format(GameText(0x007944F8), labels[3], labels[8]);   // "%s %s \n"
            else
                line.Format(GameText(0x007944F0), labels[3], Truncate(duration));   // "%s %d \n"
            tip += line;
            line.Format(GameText(0x007944E4), labels[4], Truncate(Get<float>(data, 0x34)), labels[10]);   // "%s %d%s  "
            tip += line;
            if (Get<std::uint32_t>(data, 0x3C) & 0x10000000) {
                line.Format(GameText(0x007944F8), labels[5], labels[9]);
            } else {
                // The area: radius squared times pi (007423F8, a float).
                float radius = Get<float>(data, 0x28);
                double area = static_cast<double>(radius) * radius * *reinterpret_cast<const volatile float*>(0x007423F8);
                line.Format(GameText(0x007944D8), labels[5], area, labels[10]);   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = Get<std::uint32_t>(data, 0x20);
            if (ticks < 2) {
                line.Format(GameText(0x00794508), labels[6], labels[8]);
            } else {
                // Ticks to seconds (00740544, 1/15).
                double seconds = x87::FromUnsigned(ticks) * *reinterpret_cast<const volatile float*>(0x00740544);
                line.Format(GameText(0x007944CC), labels[6], seconds, labels[11]);   // "%s %.1f%s  "
            }
            tip += line;
            line.Format(GameText(0x007944F0), labels[7], Get<int>(data, 0x38));
            tip += line;
        }
        // The region of the spell's index (the original does not count the missing ones).
        regions_.data_[i]->SetTipString(tip, 1, static_cast<int>(i) + 0x1E);
    }
}
EI_REPLACE_METHOD(0x0063E820, CHudSpellColumn::SetUnit);
