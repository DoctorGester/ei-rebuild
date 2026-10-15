// Checks for the region tooltips (ui_tooltip.cpp, A-H20). Scenarios: every screen with regions makes
// tooltips (the main menu at a new game, the battle HUD's panels: ui_hud_tips, ui_camp, ...); they
// are drawn when the mouse rests on a region (ui_hud_tips moves over them).
// - CUiTooltip::Construct (0065E320): side by side. Ours builds the real one, then the original a
//   scratch tooltip (fresh zeroed bytes in our memory, destroyed again with our DestroyBody); the
//   lines and,
//   for a non-empty text, the measurements and the two arguments compared.
// - CUiTooltip::SetText (0065E540): side by side; the original first, its lines copied, then ours;
//   both draw the same lines.
// - CUiTooltip::DrawLines (0065E880): ours only (GDI drawing): the lines and sizes are unchanged.
// - CUiTooltip::DestroyBody (0065E4F0): ours only: the window base holds nothing (MFC's array
//   destructor frees the lines without clearing the pointer).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <string>
#include <vector>

namespace {
void* s_unused;

std::vector<std::string> LinesOf(const CUiTooltip* tip) {
    std::vector<std::string> lines;
    for (int i = 0; i < tip->lines_.size; ++i) lines.emplace_back(static_cast<const char*>(tip->lines_[i]));
    return lines;
}
} // namespace

// PUSH -1; PUSH 0x72FDF6 (7 bytes).
static CUiTooltip*(__fastcall* s_construct)(void*, void*, CInterface3D*, CUiRegion*, const char*, int, int);
static CUiTooltip* __fastcall TooltipConstructVerify(CUiTooltip* self, void*, CInterface3D* ui, CUiRegion* region,
                                                     const char* text, int flag, int extra) {
    // Ours first, the scratch in our memory (the game reads the heap's address order, SV2f).
    std::vector<unsigned char> scratchBytes(sizeof(CUiTooltip), 0);
    auto* scratch = reinterpret_cast<CUiTooltip*>(scratchBytes.data());
    self->CUiTooltip::Construct(ui, region, text, flag, extra);
    s_construct(scratch, nullptr, ui, region, text, flag, extra);
    bool ok = LinesOf(scratch) == LinesOf(self) && scratch->vtable_ == self->vtable_ &&
              scratch->inputVtable_ == self->inputVtable_ && scratch->region_ == self->region_ &&
              scratch->state_ == self->state_ && scratch->timer_ == self->timer_;
    if (text && *text)
        ok = ok && scratch->width_ == self->width_ && scratch->height_ == self->height_ &&
             scratch->lineHeight_ == self->lineHeight_ && scratch->flag_ == self->flag_ && scratch->extra_ == self->extra_ &&
             scratch->keyName_ == self->keyName_ && scratch->keyWidth_ == self->keyWidth_;
    ei::verify::Check("CUiTooltip::Construct", true, ok, "\"%.40s\": lines %d / %d, size %dx%d / %dx%d", text ? text : "(null)",
                      scratch->lines_.size, self->lines_.size, scratch->width_, scratch->height_, self->width_,
                      self->height_);
    scratch->CUiTooltip::DestroyBody();
    return self;
}
EI_VERIFY(0x0065E320, TooltipConstructVerify, 7, s_construct);

// PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static void(__fastcall* s_setText)(CUiTooltip*, void*, const char*);
static void __fastcall TooltipSetTextVerify(CUiTooltip* self, void*, const char* text) {
    s_setText(self, nullptr, text);
    std::vector<std::string> expected = LinesOf(self);
    self->CUiTooltip::SetText(text);
    ei::verify::Check("CUiTooltip::SetText", true, expected == LinesOf(self), "\"%.40s\": lines %d / %d",
                      text ? text : "(null)", static_cast<int>(expected.size()), self->lines_.size);
}
EI_VERIFY(0x0065E540, TooltipSetTextVerify, 5, s_setText);

static void __fastcall TooltipDrawLinesVerify(CUiTooltip* self) {
    std::vector<std::string> before = LinesOf(self);
    int width = self->width_, height = self->height_;
    self->CUiTooltip::DrawLines();
    ei::verify::Check("CUiTooltip::DrawLines", true, before == LinesOf(self) && width == self->width_ && height == self->height_,
                      "");
}
EI_VERIFY(0x0065E880, TooltipDrawLinesVerify, 0, s_unused);

static void __fastcall TooltipDestroyVerify(CUiTooltip* self) {
    self->CUiTooltip::DestroyBody();
    bool ok = !self->sceneObjects_.data && !self->surfaces_.data && !self->regions_.data &&
              !self->objects_.data && !self->blackMaterial_ && !self->whiteMaterial_;
    ei::verify::Check("CUiTooltip::DestroyBody", true, ok, "");
}
EI_VERIFY(0x0065E4F0, TooltipDestroyVerify, 0, s_unused);

// CUiTooltip::OnMouseMove (0065E580, A-H26; hooked in the vanilla launch only): ours only, as the
// step makes or drops the tooltip's surface. The state it must reach follows from the state, the
// timer and the two settings before; a shown tooltip gains one surface, a dropped one holds none.
// Scenarios: every frame of a screen with region tooltips (ui_main_menu, ui_hud: the mouse rests on
// the HUD's buttons between clicks and leaves them).
static void __fastcall TooltipMouseMoveVerify(CUiTooltip* self, void*, int x, int y) {
    const float delay = *reinterpret_cast<const float*>(std::uintptr_t(0x0079BE80));
    const float appear = *reinterpret_cast<const float*>(std::uintptr_t(0x00742F2C));
    const int state = self->state_;
    const float timer = self->timer_;
    const int surfaces = self->surfaces_.size;
    int expected = state;
    if (state == 1) expected = self->lines_.size == 0 || delay == 0.0f ? 0 : timer < delay ? 1 : 2;
    if (state == 2) expected = timer < appear ? 2 : 4;
    if (state == 3) expected = timer > 0.0f ? 3 : 0;
    self->CUiTooltip::OnMouseMoveVanilla(x, y);
    bool ok = self->state_ == expected;
    if (state == 0 || state == 4) ok = ok && self->timer_ == timer && self->surfaces_.size == surfaces;
    if (state == 1 && expected == 1) ok = ok && self->timer_ >= timer && self->surfaces_.size == surfaces;
    if (state == 1 && expected == 2) {
        ok = ok && self->timer_ == 0.0f && self->surfaces_.size == surfaces + 1 &&
             (self->keyWidth_ == 0 || (self->keyName_ && self->keyWidth_ > 12));
    }
    if (state == 2 && expected == 4) ok = ok && self->timer_ == appear;
    if (state == 3 && expected == 3) ok = ok && self->timer_ <= timer;
    if (state == 3 && expected == 0) ok = ok && self->surfaces_.size == 0 && self->sceneObjects_.size == 0;
    ei::verify::Check("CUiTooltip::OnMouseMove", true, ok, "(%d,%d) state %d -> %d (expected %d), timer %g -> %g, surfaces %d -> %d",
                      x, y, state, self->state_, expected, timer, self->timer_, surfaces, self->surfaces_.size);
}
EI_VERIFY(0x0065E580, TooltipMouseMoveVerify, 0, s_unused);
