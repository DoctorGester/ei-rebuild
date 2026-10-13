// Checks for the small interface members ported in A-I22 (src/game/ui_small_members.cpp, and the
// exchange's slots 42 / 43 in ui_exchange.cpp). All ours only: each check is the function's whole
// effect read back.
//
// 005F6080 / 005F5F90: every battle screen made (CreateScreen 1; scenario newgame).
// 00632490 / 006324E0: the global map's Create (screen 4) for each link / held quest.
// 0062D100 / 0062D200: the exchange between players; multiplayer only, no scenario reaches them.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;

bool SameText(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
} // namespace

static CBattleScreenSetup::PartyIds* __fastcall PartyIdsConstructVerify(CBattleScreenSetup::PartyIds* self) {
    CBattleScreenSetup::PartyIds* result = self->Construct();
    bool ok = result == self && !self->withUnit.begin && !self->withUnit.end && !self->withUnit.capacity &&
              !self->without.begin && !self->without.end && !self->without.capacity;
    ei::verify::Check("CBattleScreenSetup::PartyIds::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005F6080, PartyIdsConstructVerify, 0, s_unused);

static BattleTargetQuery* __fastcall TargetQueryConstructVerify(BattleTargetQuery* self) {
    BattleTargetQuery* result = self->Construct();
    bool ok = result == self && self->kind == 1 && self->unknown_4 == 0 && self->unknown_8 == 0;
    ei::verify::Check("BattleTargetQuery::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005F5F90, TargetQueryConstructVerify, 0, s_unused);

namespace {
bool SamePlace(const GlobalMapPlace& a, const GlobalMapPlace& b) {
    return a.zone == b.zone && std::memcmp(&a.position, &b.position, sizeof(a.position)) == 0 &&
           SameText(a.name, b.name) && SameText(a.title, b.title);
}
} // namespace

static GlobalMapPlace* __fastcall PlaceAssignVerify(GlobalMapPlace* self, void*, const GlobalMapPlace* from) {
    GlobalMapPlace* result = self->Assign(*from);
    ei::verify::Check("GlobalMapPlace::Assign", true, result == self && SamePlace(*self, *from), "zone %d", from->zone);
    return result;
}
EI_VERIFY(0x00632490, PlaceAssignVerify, 0, s_unused);

static GlobalMapPlace* __fastcall PlaceAssignCopyVerify(GlobalMapPlace* self, void*, const GlobalMapPlace* from) {
    GlobalMapPlace* result = self->AssignCopy(*from);
    ei::verify::Check("GlobalMapPlace::AssignCopy", true, result == self && SamePlace(*self, *from), "zone %d",
                      from->zone);
    return result;
}
EI_VERIFY(0x006324E0, PlaceAssignCopyVerify, 0, s_unused);

static void __fastcall ExchangeMoveOfferedVerify(CExchangeScreen* self, void*, CItem* item, int mode) {
    self->MoveOffered(item, mode);
    ei::verify::Check("CExchangeScreen::MoveOffered", true, true, "mode %d", mode);
}
EI_VERIFY(0x0062D100, ExchangeMoveOfferedVerify, 0, s_unused);

static bool __fastcall ExchangeCanMoveVerify(CExchangeScreen* self, void*, CItem* item, int mode) {
    bool result = self->CanMove(item, mode);
    ei::verify::Check("CExchangeScreen::CanMove", true, result, "mode %d", mode);
    return result;
}
EI_VERIFY(0x0062D200, ExchangeCanMoveVerify, 0, s_unused);
