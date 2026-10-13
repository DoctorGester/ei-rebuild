// Small members of the interface classes in 005E0000-00640000 (A-I22): member constructors and
// assignments, default slot answers, one-line setters and handlers. See ui.h, ui_camp.h,
// ui_screens.h and docs/notes/ui.md.
#include "ui_camp.h"

#include "ei/hook.h"
#include "lib/mfc.h"

namespace {
mfc::CString& String(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }
const mfc::CString& String(char* const& field) { return *reinterpret_cast<const mfc::CString*>(&field); }
} // namespace

// --- 005F6080 / 005F5F90: the battle screen's members constructed by CreateScreen 1 ---------------------

CBattleScreenSetup::PartyIds* CBattleScreenSetup::PartyIds::Construct() {
    withUnit = IdVector{nullptr, nullptr, nullptr};
    without = IdVector{nullptr, nullptr, nullptr};
    return this;
}
EI_REPLACE_METHOD(0x005F6080, CBattleScreenSetup::PartyIds::Construct);

BattleTargetQuery* BattleTargetQuery::Construct() {
    kind = 1;
    unknown_4 = 0;
    unknown_8 = 0;
    return this;
}
EI_REPLACE_METHOD(0x005F5F90, BattleTargetQuery::Construct);

// --- 00632490 / 006324E0: a global map place assigned ---------------------------------------------------

GlobalMapPlace* GlobalMapPlace::Assign(const GlobalMapPlace& from) {
    zone = from.zone;
    position = from.position;
    String(name) = String(from.name);
    String(title) = String(from.title);
    return this;
}
EI_REPLACE_METHOD(0x00632490, GlobalMapPlace::Assign);

GlobalMapPlace* GlobalMapPlace::AssignCopy(const GlobalMapPlace& from) { return Assign(from); }
EI_REPLACE_METHOD(0x006324E0, GlobalMapPlace::AssignCopy);
