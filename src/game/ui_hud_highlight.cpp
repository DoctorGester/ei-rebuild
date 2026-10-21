// The HUD buttons' highlights: the quick item panel's slots (0063CD20), the spell and action
// columns' buttons (0063E750 / 0063F730) and their clearing (0063E7C0 / 0063F7A0), and the
// sprite scale they set (005E7060). The pending action (ui_hud_pick.cpp, SetPendingAction) and
// the panels' SetUnit drive them. See docs/notes/ui.md ("Battle screen").
#include "ui.h"

#include "ei/hook.h"
#include "originals_ui.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kColourOn = 0xFF404040;
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr float kButtonScaleOn = 1.15f;   // 3F933333
constexpr float kButtonScaleOff = 1.0f;
constexpr float kQuickScaleOn = 0.65f;    // 3F266666, the unit's quick items (bottom row)
constexpr float kQuickScaleOff = 0.55f;   // 3F0CCCCD
constexpr float kOwnScaleOn = 1.0f;       // the player's items (top row)
constexpr float kOwnScaleOff = 0.85f;     // 3F59999A
constexpr int kBottomRow = 4;             // slots 0-3

CUiMesh* Sprite(CUiWindow* window, int index) { return window->SceneMesh(index); }

void SetSpriteScale(CUiMesh* sprite, float scale) { sprite->SetScale(scale); }

// The columns' button highlight (0063E750 and 0063F730 are the same code).
void HighlightButton(CUiWindow* column, int* selected, int index, int on) {
    CUiMesh* sprite = Sprite(column, index);
    SetSpriteScale(sprite, on ? kButtonScaleOn : kButtonScaleOff);
    sprite->flatSpecular_ = on ? kColourOn : kColourOff;
    *selected = index;
}

// The columns' clearing (0063E7C0 and 0063F7A0): every button off, selected_ passing through
// each index, then -1.
void ClearButtons(CUiWindow* column, int count, int* selected) {
    for (int i = 0; i < count; ++i) {
        CUiMesh* sprite = Sprite(column, i);
        SetSpriteScale(sprite, kButtonScaleOff);
        sprite->flatSpecular_ = kColourOff;
        *selected = i;
    }
    *selected = -1;
}

} // namespace

// --- 005E7060: a sprite's scale ----------------------------------------------------------------

void CUiMesh::SetScale(float scale) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(scale);
    std::memcpy(&ScaleX(), &bits, sizeof(bits));
    std::memcpy(&ScaleY(), &bits, sizeof(bits));
    std::memcpy(&ScaleZ(), &bits, sizeof(bits));
}
EI_REPLACE_METHOD(0x005E7060, CUiMesh::SetScale);

// --- 0063CD20: a quick item slot ---------------------------------------------------------------

void CHudQuickItems::Highlight(int slot, int on) {
    int sprite = regionItem_[slot];
    if (sprite < 0 || sprite >= shownCount_) return;
    CUiMesh* mesh = Sprite(this, sprite);
    if (slot < kBottomRow)
        SetSpriteScale(mesh, on ? kQuickScaleOn : kQuickScaleOff);
    else
        SetSpriteScale(mesh, on ? kOwnScaleOn : kOwnScaleOff);
    if (on) {
        mesh->flatSpecular_ = kColourOn;
        selected_ = slot;
    } else {
        mesh->flatSpecular_ = kColourOff;
    }
}
EI_REWRITTEN(0x0063CD20, CHudQuickItems::Highlight);

// --- 0063E750 / 0063F730 / 0063E7C0 / 0063F7A0: the columns' buttons -----------------------------

void CHudSpellColumn::Highlight(int index, int on) { HighlightButton(this, &selected_, index, on); }
EI_REWRITTEN(0x0063E750, CHudSpellColumn::Highlight);

void CHudActionColumn::Highlight(int index, int on) { HighlightButton(this, &selected_, index, on); }
EI_REWRITTEN(0x0063F730, CHudActionColumn::Highlight);

void CHudSpellColumn::ClearHighlights() { ClearButtons(this, count_, &selected_); }
EI_REWRITTEN(0x0063E7C0, CHudSpellColumn::ClearHighlights);

void CHudActionColumn::ClearHighlights() { ClearButtons(this, count_, &selected_); }
EI_REWRITTEN(0x0063F7A0, CHudActionColumn::ClearHighlights);
