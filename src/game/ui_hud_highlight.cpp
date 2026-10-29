// The HUD buttons' highlights: the quick item panel's slots, the spell and action
// columns' buttons and their clearing, and the
// sprite scale they set. The pending action (ui_hud_pick.cpp, SetPendingAction) and
// the panels' SetUnit drive them. See docs/notes/ui.md ("Battle screen").
#include "ui.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kColourOn = 0xFF404040;
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr float kButtonScaleOn = 1.15f;
constexpr float kButtonScaleOff = 1.0f;
constexpr float kQuickScaleOn = 0.65f;    // the unit's quick items (bottom row)
constexpr float kQuickScaleOff = 0.55f;
constexpr float kOwnScaleOn = 1.0f;       // the player's items (top row)
constexpr float kOwnScaleOff = 0.85f;
constexpr int kBottomRow = 4;             // slots 0-3
constexpr int kQuickSlots = 8;

// The columns' button highlight (both columns share the code).
void HighlightButton(CUiWindow* column, int* selected, int index, bool on) {
    CUiMesh* sprite = column->SceneMesh(index);
    sprite->SetScale(on ? kButtonScaleOn : kButtonScaleOff);
    sprite->flatSpecular_ = on ? kColourOn : kColourOff;
    *selected = index;
}

// The columns' clearing: every button off, selected_ passing through
// each index, then -1.
void ClearButtons(CUiWindow* column, int count, int* selected) {
    for (int i = 0; i < count; ++i) {
        CUiMesh* sprite = column->SceneMesh(i);
        sprite->SetScale(kButtonScaleOff);
        sprite->flatSpecular_ = kColourOff;
        *selected = i;
    }
    *selected = -1;
}

} // namespace

// --- A sprite's scale ----------------------------------------------------------------

void CUiMesh::SetScale(float scale) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(scale);
    ScaleX() = std::bit_cast<float>(bits);
    ScaleY() = std::bit_cast<float>(bits);
    ScaleZ() = std::bit_cast<float>(bits);
}

// --- A quick item slot ---------------------------------------------------------------

void CHudQuickItems::Highlight(int slot, bool on) {
    int sprite = regionItem_[slot];
    if (sprite < 0 || sprite >= shownCount_) return;
    CUiMesh* mesh = SceneMesh(sprite);
    if (slot < kBottomRow)
        mesh->SetScale(on ? kQuickScaleOn : kQuickScaleOff);
    else
        mesh->SetScale(on ? kOwnScaleOn : kOwnScaleOff);
    if (on) {
        mesh->flatSpecular_ = kColourOn;
        selected_ = slot;
    } else {
        mesh->flatSpecular_ = kColourOff;
    }
}

void CHudQuickItems::ClearHighlights() {
    for (int i = 0; i < kQuickSlots; ++i) Highlight(i, false);
    selected_ = -1;
}

// --- The columns' buttons -----------------------------------------------------------------

void CHudSpellColumn::Highlight(int index, bool on) { HighlightButton(this, &selected_, index, on); }

void CHudActionColumn::Highlight(int index, bool on) { HighlightButton(this, &selected_, index, on); }

void CHudSpellColumn::ClearHighlights() { ClearButtons(this, count_, &selected_); }

void CHudActionColumn::ClearHighlights() { ClearButtons(this, count_, &selected_); }
