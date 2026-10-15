// Checks for the HUD buttons' highlights and the sprite scale (ui_hud_highlight.cpp) and the
// battle screen's left button down (ui_battle_mouse.cpp). The highlights run side by side: the
// original first, the sprites' scale fields and colour and the panel's selection recorded and put
// back, then ours. Reached in every game scenario: the selection refresh (00637DC0) and every
// pending action change (SetPendingAction: SetUnit, Pick, Click, Select, the spell keys) clear
// and set them; ui_battle_spells / ui_quick_items click the buttons. SetScale has 45 callers (the
// HUD's and the menus' sprites).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct SpriteState {
    float scale[3];
    std::uint32_t colour;
};
SpriteState Read(CUiMesh* m) { return SpriteState{{m->ScaleX(), m->ScaleY(), m->ScaleZ()}, m->flatSpecular_}; }
void Write(CUiMesh* m, const SpriteState& s) {
    m->ScaleX() = s.scale[0];
    m->ScaleY() = s.scale[1];
    m->ScaleZ() = s.scale[2];
    m->flatSpecular_ = s.colour;
}

// The panel's sprites 0 .. count - 1 and its selection.
struct PanelState {
    std::vector<SpriteState> sprites;
    int selected;
    bool operator==(const PanelState& o) const {
        return selected == o.selected && sprites.size() == o.sprites.size() &&
               std::memcmp(sprites.data(), o.sprites.data(), sprites.size() * sizeof(SpriteState)) == 0;
    }
};
PanelState Snapshot(CUiWindow* panel, int count, int selected) {
    PanelState s{{}, selected};
    for (int i = 0; i < count && i < panel->sceneObjects_.size; ++i)
        s.sprites.push_back(Read(reinterpret_cast<CUiMesh*>(panel->sceneObjects_.data[i])));
    return s;
}
void Restore(CUiWindow* panel, const PanelState& s, int* selected) {
    for (std::size_t i = 0; i < s.sprites.size(); ++i)
        Write(reinterpret_cast<CUiMesh*>(panel->sceneObjects_.data[i]), s.sprites[i]);
    *selected = s.selected;
}

// Runs `original` and `ours` on the same state and compares the panel afterwards.
template <typename Original, typename Ours>
void SideBySide(const char* name, CUiWindow* panel, int count, int* selected, Original original, Ours ours, int a,
                int b) {
    PanelState before = Snapshot(panel, count, *selected);
    original();
    PanelState expected = Snapshot(panel, count, *selected);
    Restore(panel, before, selected);
    ours();
    PanelState actual = Snapshot(panel, count, *selected);
    ei::verify::Check(name, true, expected == actual, "(%d, %d) count %d selected %d -> %d / %d", a, b, count,
                      before.selected, expected.selected, actual.selected);
}

} // namespace

// CUiMesh::SetScale (005E7060): the three fields as the original leaves them. MOV EAX,[ESP+4];
// MOV EDX,EAX (6).
static void(__fastcall* s_setScale)(CUiMesh*, void*, float);
static void __fastcall SetScaleVerify(CUiMesh* self, void*, float scale) {
    SpriteState before = Read(self);
    s_setScale(self, nullptr, scale);
    SpriteState expected = Read(self);
    Write(self, before);
    self->CUiMesh::SetScale(scale);
    SpriteState actual = Read(self);
    ei::verify::Check("CUiMesh::SetScale", true, std::memcmp(&expected, &actual, sizeof(SpriteState)) == 0,
                      "%08X %08X %08X", *reinterpret_cast<std::uint32_t*>(&actual.scale[0]),
                      *reinterpret_cast<std::uint32_t*>(&actual.scale[1]), *reinterpret_cast<std::uint32_t*>(&actual.scale[2]));
}
EI_VERIFY(0x005E7060, SetScaleVerify, 6, s_setScale);

// CHudQuickItems::Highlight (0063CD20). PUSH EBX; MOV EBX,[ESP+8] (5).
static void(__fastcall* s_quickHighlight)(CHudQuickItems*, void*, int, int);
static void __fastcall QuickHighlightVerify(CHudQuickItems* self, void*, int slot, int on) {
    SideBySide(
        "CHudQuickItems::Highlight", self, self->shownCount_, &self->selected_,
        [&] { s_quickHighlight(self, nullptr, slot, on); }, [&] { self->CHudQuickItems::Highlight(slot, on); }, slot, on);
}
EI_VERIFY(0x0063CD20, QuickHighlightVerify, 5, s_quickHighlight);

// CHudSpellColumn::Highlight (0063E750), CHudActionColumn::Highlight (0063F730). PUSH EBX;
// MOV EBX,[ESP+8] (5).
static void(__fastcall* s_spellHighlight)(CHudSpellColumn*, void*, int, int);
static void __fastcall SpellHighlightVerify(CHudSpellColumn* self, void*, int index, int on) {
    SideBySide(
        "CHudSpellColumn::Highlight", self, self->count_, &self->selected_,
        [&] { s_spellHighlight(self, nullptr, index, on); }, [&] { self->CHudSpellColumn::Highlight(index, on); }, index, on);
}
EI_VERIFY(0x0063E750, SpellHighlightVerify, 5, s_spellHighlight);

static void(__fastcall* s_actionHighlight)(CHudActionColumn*, void*, int, int);
static void __fastcall ActionHighlightVerify(CHudActionColumn* self, void*, int index, int on) {
    SideBySide(
        "CHudActionColumn::Highlight", self, self->count_, &self->selected_,
        [&] { s_actionHighlight(self, nullptr, index, on); }, [&] { self->CHudActionColumn::Highlight(index, on); }, index,
        on);
}
EI_VERIFY(0x0063F730, ActionHighlightVerify, 5, s_actionHighlight);

// CHudSpellColumn::ClearHighlights (0063E7C0), CHudActionColumn::ClearHighlights (0063F7A0).
// PUSH ESI; PUSH EDI; MOV EDI,ECX; XOR ESI,ESI (6).
static void(__fastcall* s_spellClear)(CHudSpellColumn*, void*);
static void __fastcall SpellClearVerify(CHudSpellColumn* self, void*) {
    SideBySide(
        "CHudSpellColumn::ClearHighlights", self, self->count_, &self->selected_, [&] { s_spellClear(self, nullptr); },
        [&] { self->CHudSpellColumn::ClearHighlights(); }, 0, 0);
}
EI_VERIFY(0x0063E7C0, SpellClearVerify, 6, s_spellClear);

static void(__fastcall* s_actionClear)(CHudActionColumn*, void*);
static void __fastcall ActionClearVerify(CHudActionColumn* self, void*) {
    SideBySide(
        "CHudActionColumn::ClearHighlights", self, self->count_, &self->selected_, [&] { s_actionClear(self, nullptr); },
        [&] { self->CHudActionColumn::ClearHighlights(); }, 0, 0);
}
EI_VERIFY(0x0063F7A0, ActionClearVerify, 6, s_actionClear);

// CBattleScreen::OnLButtonDown (00638A40): ours only (the container's handler passes the
// press to the panels, which must not get it twice). Without the portrait pick: a press already
// on, or one no panel took, leaves the press on at (x, y) without a drag; a press a panel took
// leaves it off. Reached by every click on the battle screen (ui_hud, ui_battle_commands,
// ui_order_move, ...). PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5).
static void(__fastcall* s_battleLeftDown)(void*, void*, int, int, int);
static void __fastcall BattleLeftDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBattleScreen>(input);
    bool portraits = self->portraitMode_ != 0;
    bool pressed = self->viewPressed_;
    self->CBattleScreen::OnLButtonDown(buttons, x, y);
    if (portraits) return;
    bool started = self->viewPressed_ && self->pressX_ == x && self->pressY_ == y && !self->boxSelecting_;
    bool ok = pressed ? started : (self->inputHandled_ ? !self->viewPressed_ : started);
    ei::verify::Check("CBattleScreen::OnLButtonDown", true, ok, "(%d,%d) pressed %d handled %d press %d at %d,%d drag %d",
                      x, y, pressed ? 1 : 0, self->inputHandled_ ? 1 : 0, self->viewPressed_ ? 1 : 0, self->pressX_,
                      self->pressY_, self->boxSelecting_ ? 1 : 0);
}
EI_VERIFY(0x00638A40, BattleLeftDownVerify, 0, s_battleLeftDown);
