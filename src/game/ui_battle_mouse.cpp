// The battle screen's mouse move (00639D60) and a unit's name line (0063B920). See ui_camp.h
// (CBattleScreenSetup) and docs/notes/ui.md ("Nineteenth pass (ticket I10)").
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
#include "originals_world.h"

namespace {
constexpr int kCursorArrow = 0;
constexpr int kCursorPickPlayer = 0xD;   // a portrait click picks a player
constexpr const char* kUnitTextPrefix = "unit ";   // 00794498

// 00639EF0 (CBattleScreen::HoverUnit, rewritten in ui_battle_hover.cpp; thiscall on the window,
// RET 0xC), through its address so its verify wrapper sees the battle screen's calls.
__declspec(naked) void __fastcall HoverUnitAt(CBattleScreenSetup*, void*, CUnit*, int, int) EI_ORIGINAL(0x00639EF0)

int Distance(int a, int b) { return a < b ? b - a : a - b; }
} // namespace

// --- 00639D60: the mouse moved over the battle screen ------------------------------------------

void CBattleScreenSetup::OnMouseMove(int buttons, int x, int y) {
    if (portraitMode_ != 0) {
        GameCursor()->SetShape(kCursorPickPlayer);
        if (portraits_) portraits_->InputMouseMove(buttons, x, y);
        return;
    }
    GameCursor()->SetShape(kCursorArrow);
    if (unknown_1C0 && BoxSelectThreshold() > 0 && !IsMultiplayerClient()) {
        int threshold = BoxSelectThreshold();
        if (Distance(x, pressX_) > threshold || Distance(y, pressY_) > threshold) unknown_1C1 = true;
        if (unknown_1C1) {
            CHudUnitPanel* panel = static_cast<CHudUnitPanel*>(unitPanel_);
            if (panel->hoverUnit_) panel->hoverUnit_ = nullptr;
            selectionBox_.left = x < pressX_ ? x : pressX_;
            selectionBox_.top = y < pressY_ ? y : pressY_;
            selectionBox_.right = x > pressX_ ? x : pressX_;
            selectionBox_.bottom = y > pressY_ ? y : pressY_;
            return;
        }
    } else {
        InputOf(this)->BaseMouseMove(buttons, x, y);
        if (inputHandled_) return;
    }
    // The original passes the address of its own (x, y) arguments and reads them back.
    int point[2] = {x, y};
    CUnit* unit = WorldUnitAtPointOriginal(WorldClient(), nullptr, point);
    HoverUnitAt(this, nullptr, unit, point[0], point[1]);
}
namespace {
void __fastcall BattleScreenMouseMove(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CBattleScreenSetup>(input)->OnMouseMove(buttons, x, y);
}
} // namespace
EI_REPLACE(0x00639D60, BattleScreenMouseMove);

// --- 0063B920: a unit's name line --------------------------------------------------------------

const PersonTextNode* PersonTextMap::Find(int key) const {
    const PersonTextNode* bound = header;
    for (const PersonTextNode* node = header->parent; node;) {
        if (node->key < key) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    if (bound == header || key < bound->key) return header;
    return bound;
}

void CBattleScreenSetup::UnitName(CUnit* unit, mfc::CString* out) {
    CTextManager* texts = GameTexts();
    mfc::CString name;
    name = unit->GetName();
    if (name.GetLength() != 0) {
        *out = name;
        return;
    }
    if (IsTextKey(unit->id_)) {
        const PersonTextNode* node = personTexts_.Find(unit->id_);
        if (node == personTexts_.header)
            out->Empty();
        else
            *out = node->text;
        return;
    }
    out->Empty();
    name = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype())->name;
    ReplaceChar(&name, ' ', '_');
    if (TextOpen(texts, nullptr, mfc::CString(kUnitTextPrefix) + name)) TextReadLine(texts, nullptr, out, 1);
}
namespace {
void __fastcall BattleScreenUnitName(CBattleScreenSetup* self, void*, CUnit* unit, mfc::CString* out) {
    self->UnitName(unit, out);
}
} // namespace
EI_REPLACE(0x0063B920, BattleScreenUnitName);
