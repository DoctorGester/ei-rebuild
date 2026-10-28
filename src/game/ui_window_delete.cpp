// The interface windows' scalar deleting destructors (the destructor body, then the memory freed when bit 0
// of the flags is set; they return the object), the input sides' copies of them (`this` moved back from the
// input side to the window, then a jump to the window's), and the short destructor bodies only they call.
// Bodies that only jump to the window's or container's destructor are those destructors themselves. docs/notes/ui.md,
// section 2 "Deleting destructors".
#include "global_map_clouds.h"
#include "party.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "core_refcount.h"
#include "lib/crt.h"
#include "game_data.h"

#include <memory>
#include <string>

// --- Destructor bodies -------------------------------------------------------------------------

// The input sides' storage: the game's heap.

// The base input side's deleting destructor.
UiInputSide::~UiInputSide() {}

// The inert extra side's deleting destructor, the base side's table back.
UiInertInput::~UiInertInput() {}

CItemInfoPanel::~CItemInfoPanel() {
    ItemParty()->CParty::~CParty();
}

CChatWindow::~CChatWindow() {}

CForwardingStrip::~CForwardingStrip() = default;

CSpellMakerPage::~CSpellMakerPage() = default;

CHeroStatsPanel::~CHeroStatsPanel() = default;

CMessageBox::~CMessageBox() = default;

// The link, then the box's.
CMasterRequestBox::~CMasterRequestBox() = default;

ChatLine::~ChatLine() { delete surface; }

CChatOverlay::~CChatOverlay() {}

CBattleScreen::~CBattleScreen() {
}

// The shown items, then the movers (members).
CBriefingScreen::~CBriefingScreen() {}

CMpConnectScreen::~CMpConnectScreen() {}

CHelpWindow::~CHelpWindow() {}

CObjectivesWindow::~CObjectivesWindow() = default;

CSaveLoadWindow::~CSaveLoadWindow() {}
