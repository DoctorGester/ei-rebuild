// The battle screen's mouse move, left button down and a unit's name line. See ui.h (BattleScreen) and
// docs/notes/ui.md ("Battle screen").
#include "text_split.h"
#include "ui.h"
#include "ui_camp.h"

#include "text_manager.h"
#include "object_tables.h"
#include "ui_cursor.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include <string>
#include "game_singletons.h"
#include "options.h"

namespace {
constexpr int kCursorArrow = 0;
constexpr int kCursorPickPlayer = 0xD;   // a portrait click picks a player
constexpr const char* kUnitTextPrefix = "unit ";

int Distance(int a, int b) { return a < b ? b - a : a - b; }
} // namespace

// --- the mouse moved over the battle screen ------------------------------------------------------

void BattleScreen::MouseMove(int buttons, int x, int y) {
    if (portraitMode != 0) {
        g_cursor.SetShape(kCursorPickPlayer);
        if (portraits) portraits->Input()->MouseMove(buttons, x, y);
        return;
    }
    g_cursor.SetShape(kCursorArrow);
    if (viewPressed && options::g_options.cameraFrameSelectionArea > 0 && !IsMultiplayerClient()) {
        int threshold = options::g_options.cameraFrameSelectionArea;
        if (Distance(x, pressX) > threshold || Distance(y, pressY) > threshold) boxSelecting = true;
        if (boxSelecting) {
            HudUnitPanel* panel = unitPanel;
            if (panel->hoverUnit) panel->hoverUnit = nullptr;
            selectionBox.left = x < pressX ? x : pressX;
            selectionBox.top = y < pressY ? y : pressY;
            selectionBox.right = x > pressX ? x : pressX;
            selectionBox.bottom = y > pressY ? y : pressY;
            return;
        }
    } else {
        UiContainer::MouseMove(buttons, x, y);
        if (handled) return;
    }
    // The (x, y) pair is read back through its address.
    POINT point = {x, y};
    CUnit* unit = g_clientWorld.UnitAt(&point);
    HoverUnit(unit, point.x, point.y);
}

// --- the left button went down on the battle screen ----------------------------------------------

void BattleScreen::LeftDown(int buttons, int x, int y) {
    if (portraitMode != 0) {
        if (portraits) portraits->Input()->LeftDown(buttons, x, y);
        return;
    }
    if (!viewPressed) {
        UiContainer::LeftDown(buttons, x, y);
        if (handled) return;
    }
    viewPressed = true;
    pressX = x;
    pressY = y;
    boxSelecting = false;
}

// --- a unit's name line --------------------------------------------------------------------------

// Each text is copied up to its first NUL.
std::string BattleScreen::UnitName(CUnit* unit) {
    TextManager* texts = g_clientWorld.texts;
    std::string name = unit->GetName();
    if (name.size() != 0) return name.c_str();
    if (IsTextKey(unit->id)) {
        auto node = personTexts.find(unit->id);
        return node == personTexts.end() ? std::string() : std::string(node->second.c_str());
    }
    name = unit->GetPrototype()->name;
    text::ReplaceChar(&name, ' ', '_');
    if (!texts->Open(std::string(kUnitTextPrefix) + name)) return {};
    std::string line = texts->ReadLine(true);
    return line.c_str();
}
