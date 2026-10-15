// The statistics panel's row painters: a labelled value (006242E0) and a body part's armour
// (00624590). See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "item_db_text.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"

#include <cstdint>

namespace {

const char* const kPerkPrefix = reinterpret_cast<const char*>(0x00792FC8);    // "perk "
const char* const kValueFormat = reinterpret_cast<const char*>(0x0078EEEC);   // "%d"
const char* const kRangeFormat = reinterpret_cast<const char*>(0x00793BB8);   // "%d/%d"
const char* const kPartPrefix = reinterpret_cast<const char*>(0x00793DD4);    // "hl_"
const char* const kPartIndent = reinterpret_cast<const char*>(0x00793DCC);    // "     "
const char* const kArmourFormat = reinterpret_cast<const char*>(0x00792F78);  // "%.1f"

constexpr std::uint32_t kRowColour = 0xA7D7E4;
constexpr int kLeft = 0;
constexpr int kRight = 2;
constexpr int kDamageTypes = 7;
constexpr int kShownTypes = 6;               // the seventh is summed but not compared
constexpr int kLayers = 3;

UnitStats* HeroStats(CUiWindow* owner) {
    using Fn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    PartyUnitRecord* hero = reinterpret_cast<Fn>(owner->vtable_[51])(owner);
    return reinterpret_cast<UnitStats*>(hero->stats);
}

} // namespace

// --- 006242E0: a labelled value ---------------------------------------------------------------------

void CHeroStatsPanel::DrawRow(RECT* rect, const char* label, int value, int maximum) {
    CTextManager* texts = GameTexts();
    mfc::CString name;
    mfc::CString number;
    BOOL found;
    {
        mfc::CString key = mfc::CString(kPerkPrefix) + label;
        found = TextsOpenSection(texts, nullptr, key);
    }
    if (found)
        TextsReadLineOriginal(texts, nullptr, &name, 1);
    else
        name = label;
    if (maximum >= 0)
        number.Format(kRangeFormat, value, maximum);
    else
        number.Format(kValueFormat, value);
    CUiSurface* surface = surfaces_.data[0];
    SurfaceDrawText(surface, nullptr, name, rect, 0, kRowColour, kLeft, 1);
    SurfaceDrawText(surface, nullptr, number, rect, 0, kRowColour, kRight, 1);
}
EI_REPLACE_METHOD(0x006242E0, CHeroStatsPanel::DrawRow);

// --- 00624590: a body part's armour ------------------------------------------------------------------

float CHeroStatsPanel::PartArmour(int part) {
    const UnitStatSlot& slot = HeroStats(owner_)->slots[part];
    float sums[kDamageTypes];
    for (int type = 0; type < kDamageTypes; ++type) {
        double sum = 0.0;
        for (int layer = 0; layer < kLayers; ++layer) sum += slot.layers[layer].absorb[type];
        sums[type] = static_cast<float>(sum);
    }
    // The largest of the first six (a branch-free select that keeps the old value on a NaN).
    float largest = 0.0f;
    for (int type = 0; type < kShownTypes; ++type)
        if (!x87::IsNaN(sums[type]) && !(sums[type] < largest)) largest = sums[type];
    return largest;
}

void CHeroStatsPanel::DrawPart(RECT* rect, const char* name, int part) {
    mfc::CString label;
    mfc::CString number;
    mfc::CString unused;
    {
        mfc::CString key = mfc::CString(kPartPrefix) + name;
        TextsGetString(GameTexts(), nullptr, key, &label);
    }
    label = mfc::CString(kPartIndent) + label;
    CUiSurface* surface = surfaces_.data[0];
    SurfaceDrawText(surface, nullptr, label, rect, 0, kRowColour, kLeft, 1);
    number.Format(kArmourFormat, static_cast<double>(PartArmour(part)));
    SurfaceDrawText(surface, nullptr, number, rect, 0, kRowColour, kRight, 1);
}
EI_REPLACE_METHOD(0x00624590, CHeroStatsPanel::DrawPart);
