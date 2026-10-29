// Out-of-line interface destructors: the item description panel's (its party), the chat line's (its surface)
// and the battle screen's empty one. docs/notes/ui.md, section 2 "Deleting destructors".
#include "party.h"
#include "ui.h"
#include "ui_item_info.h"

CItemInfoPanel::~CItemInfoPanel() {
    ItemParty()->CParty::~CParty();
}

ChatLine::~ChatLine() { delete surface; }

CBattleScreen::~CBattleScreen() {
}
