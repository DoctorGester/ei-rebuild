// Checks for the region tooltips (ui_tooltip.cpp, A-H20). Scenarios: every screen with regions makes
// tooltips (the main menu at a new game, the battle HUD's panels: ui_hud_tips, ui_camp, ...); they
// are drawn when the mouse rests on a region (ui_hud_tips moves over them).
// - CUiTooltip::Construct (0065E320): side by side. The original builds a scratch tooltip (a fresh
//   0x184-byte block, destroyed again with our DestroyBody), ours the real one; the lines and,
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
    auto* scratch = static_cast<CUiTooltip*>(gamecrt::operator_new(sizeof(CUiTooltip)));
    std::memset(scratch, 0, sizeof(CUiTooltip));
    s_construct(scratch, nullptr, ui, region, text, flag, extra);
    self->CUiTooltip::Construct(ui, region, text, flag, extra);
    bool ok = LinesOf(scratch) == LinesOf(self) && scratch->vtable_ == self->vtable_ &&
              scratch->inputVtable_ == self->inputVtable_ && scratch->region_ == self->region_ &&
              scratch->state_ == self->state_ && scratch->unknown_17C == self->unknown_17C;
    if (text && *text)
        ok = ok && scratch->width_ == self->width_ && scratch->height_ == self->height_ &&
             scratch->lineHeight_ == self->lineHeight_ && scratch->flag_ == self->flag_ && scratch->extra_ == self->extra_ &&
             scratch->keyName_ == self->keyName_ && scratch->keyWidth_ == self->keyWidth_;
    ei::verify::Check("CUiTooltip::Construct", true, ok, "\"%.40s\": lines %d / %d, size %dx%d / %dx%d", text ? text : "(null)",
                      scratch->lines_.size, self->lines_.size, scratch->width_, scratch->height_, self->width_,
                      self->height_);
    scratch->CUiTooltip::DestroyBody();
    gamecrt::operator_delete(scratch);
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
