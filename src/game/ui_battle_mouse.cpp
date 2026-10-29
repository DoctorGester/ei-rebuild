// The battle screen's mouse move, left button down and a unit's name line. See ui.h (CBattleScreen) and
// docs/notes/ui.md ("Battle screen").
#include "text_split.h"
#include "ui.h"
#include "ui_camp.h"

#include "item_db_text.h"
#include "object_tables.h"
#include "ui_cursor.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include <string>
#include "game_singletons.h"


namespace {
constexpr int kCursorArrow = 0;
constexpr int kCursorPickPlayer = 0xD;   // a portrait click picks a player
constexpr const char* kUnitTextPrefix = "unit ";

int Distance(int a, int b) { return a < b ? b - a : a - b; }
} // namespace

// --- the mouse moved over the battle screen ------------------------------------------------------

void CBattleScreen::MouseMove(int buttons, int x, int y) {
    if (portraitMode_ != 0) {
        g_cursor.SetShape(kCursorPickPlayer);
        if (portraits_) portraits_->Input()->MouseMove(buttons, x, y);
        return;
    }
    g_cursor.SetShape(kCursorArrow);
    if (viewPressed_ && options::g_options->cameraFrameSelectionArea_ > 0 && !IsMultiplayerClient()) {
        int threshold = options::g_options->cameraFrameSelectionArea_;
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
    CUnit* unit = g_clientWorld->UnitAt(&point);
    HoverUnit(unit, point.x, point.y);
}

// --- the left button went down on the battle screen ----------------------------------------------

void CBattleScreen::LeftDown(int buttons, int x, int y) {
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

// --- a unit's name line --------------------------------------------------------------------------

// Each text is copied up to its first NUL.
std::string CBattleScreen::UnitName(CUnit* unit) {
    CTextManager* texts = g_clientWorld->texts_;
    std::string name = unit->GetName();
    if (name.size() != 0) return name.c_str();
    if (IsTextKey(unit->id_)) {
        auto node = personTexts_.find(unit->id_);
        return node == personTexts_.end() ? std::string() : std::string(node->second.c_str());
    }
    name = unit->GetPrototype()->name;
    text::ReplaceChar(&name, ' ', '_');
    if (!texts->Open(std::string(kUnitTextPrefix) + name)) return {};
    std::string line;
    texts->ReadLine(&line, 1);
    return line.c_str();
}
