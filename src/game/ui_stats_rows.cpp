// The statistics panel's row painters: a labelled value (006242E0) and a body part's armour
// (00624590). See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_camp.h"

#include "item_db_text.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

namespace {

const char* const kPerkPrefix = "perk ";
const char* const kValueFormat = "%d";
const char* const kRangeFormat = "%d/%d";
const char* const kPartPrefix = "hl_";
const char* const kPartIndent = "     ";
const char* const kArmourFormat = "%.1f";

constexpr std::uint32_t kRowColour = 0xA7D7E4;
constexpr int kLeft = 0;
constexpr int kRight = 2;
constexpr int kDamageTypes = 7;
constexpr int kShownTypes = 6;               // the seventh is summed but not compared
constexpr int kLayers = 3;

UnitStats* HeroStats(CItemScreen* owner) { return &owner->HeroRecord()->stats; }

} // namespace

// --- 006242E0: a labelled value ---------------------------------------------------------------------

void CHeroStatsPanel::DrawRow(RECT* rect, const char* label, int value, int maximum) {
    CTextManager* texts = TheTextManager();
    mfc::CString name;
    std::string number;
    BOOL found;
    {
        std::string key = (mfc::CString(kPerkPrefix) + label).GetString();
        found = texts->CTextManager::Open(key.c_str());
    }
    if (found)
        texts->ReadLine(&name, 1);
    else
        name = label;
    if (maximum >= 0)
        number = str::Format(kRangeFormat, value, maximum);
    else
        number = str::Format(kValueFormat, value);
    CUiSurface* surface = surfaces_.data[0];
    surface->DrawTextIn(name, rect, 0, kRowColour, kLeft, 1);
    surface->DrawTextIn(number.c_str(), rect, 0, kRowColour, kRight, 1);
}
EI_REWRITTEN(0x006242E0, CHeroStatsPanel::DrawRow);

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
        if (sums[type] >= largest) largest = sums[type];
    return largest;
}

void CHeroStatsPanel::DrawPart(RECT* rect, const char* name, int part) {
    mfc::CString label;
    std::string number;
    std::string unused;
    {
        std::string key = (mfc::CString(kPartPrefix) + name).GetString();
        TheTextManager()->CTextManager::GetString(key.c_str(), &label);
    }
    label = mfc::CString(kPartIndent) + label;
    CUiSurface* surface = surfaces_.data[0];
    surface->DrawTextIn(label, rect, 0, kRowColour, kLeft, 1);
    number = str::Format(kArmourFormat, static_cast<double>(PartArmour(part)));
    surface->DrawTextIn(number.c_str(), rect, 0, kRowColour, kRight, 1);
}
EI_REWRITTEN(0x00624590, CHeroStatsPanel::DrawPart);
