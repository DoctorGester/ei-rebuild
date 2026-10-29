// Out-of-line interface destructors: the chat line's (its surface) and the battle screen's empty one.
// docs/notes/ui.md, section 2 "Deleting destructors".
#include "ui.h"

ChatLine::~ChatLine() { delete surface; }

CBattleScreen::~CBattleScreen() {
}
