// The battle screen's mouse move, left button down and a unit's name line. See ui.h (CBattleScreen) and docs/notes/ui.md ("Battle screen").
#include "text_split.h"
#include "ui.h"
#include "ui_camp.h"

#include "item_db_text.h"
#include "object_tables.h"
#include "text_reader.h"
#include "ui_cursor.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "game_singletons.h"

namespace {
constexpr int kCursorArrow = 0;
constexpr int kCursorPickPlayer = 0xD;   // a portrait click picks a player
constexpr const char* kUnitTextPrefix = "unit ";

int Distance(int a, int b) { return a < b ? b - a : a - b; }
} // namespace

// --- the mouse moved over the battle screen ------------------------------------------------------

void CBattleScreen::OnMouseMove(int buttons, int x, int y) {
    if (portraitMode_ != 0) {
        TheCursor()->SetShape(kCursorPickPlayer);
        if (portraits_) portraits_->Input()->MouseMove(buttons, x, y);
        return;
    }
    TheCursor()->SetShape(kCursorArrow);
    if (viewPressed_ && BoxSelectThreshold() > 0 && !IsMultiplayerClient()) {
        int threshold = BoxSelectThreshold();
        if (Distance(x, pressX_) > threshold || Distance(y, pressY_) > threshold) boxSelecting_ = true;
        if (boxSelecting_) {
            CHudUnitPanel* panel = unitPanel_;
            if (panel->hoverUnit_) panel->hoverUnit_ = nullptr;
            selectionBox_.left = x < pressX_ ? x : pressX_;
            selectionBox_.top = y < pressY_ ? y : pressY_;
            selectionBox_.right = x > pressX_ ? x : pressX_;
            selectionBox_.bottom = y > pressY_ ? y : pressY_;
            return;
        }
    } else {
        CUiContainer::MouseMove(buttons, x, y);
        if (handled_) return;
    }
    // The (x, y) pair is read back through its address.
    POINT point = {x, y};
    CUnit* unit = TheClientWorld()->UnitAt(&point);
    HoverUnit(unit, point.x, point.y);
}
EI_REWRITTEN(0x00639D60, CBattleScreen::OnMouseMove);

// --- the left button went down on the battle screen ----------------------------------------------

void CBattleScreen::OnLButtonDown(int buttons, int x, int y) {
    if (portraitMode_ != 0) {
        if (portraits_) portraits_->Input()->LeftDown(buttons, x, y);
        return;
    }
    if (!viewPressed_) {
        CUiContainer::LeftDown(buttons, x, y);
        if (handled_) return;
    }
    viewPressed_ = true;
    pressX_ = x;
    pressY_ = y;
    boxSelecting_ = false;
}
EI_REWRITTEN(0x00638A40, CBattleScreen::OnLButtonDown);

// --- a unit's name line --------------------------------------------------------------------------

void CBattleScreen::UnitName(CUnit* unit, mfc::CString* out) {
    CTextManager* texts = TheTextManager();
    mfc::CString name;
    name = unit->GetName();
    if (name.GetLength() != 0) {
        *out = name;
        return;
    }
    if (IsTextKey(unit->id_)) {
        const PersonTextNode* node = personTexts_.find(unit->id_);
        if (node == personTexts_.end())
            out->Empty();
        else
            *out = node->second;
        return;
    }
    out->Empty();
    name = unit->GetPrototype()->name;
    text::ReplaceChar(&name, ' ', '_');
    if (texts->Open(mfc::CString(kUnitTextPrefix) + name)) texts->ReadLine(out, 1);
}
EI_REWRITTEN(0x0063B920, CBattleScreen::UnitName);
