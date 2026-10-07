// Checks for the regions' tooltips (ui_region_tips.cpp). All ours only: they delete the old
// tooltip and allocate a new one, which the original cannot do a second time. Reached by every
// screen and HUD builder (SetTip / SetTipString), the HUD's slot tips every frame (SetTipText,
// 0063D9D0) and every region removal and window destruction (ReleaseContents).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>

namespace {

constexpr std::uintptr_t kTooltipVtable = 0x00742F64;

// A fresh tooltip of `region`: the tooltip's vtable, its region, idle; with lines, the two
// arguments stored (the constructor writes them only for a non-empty text); no lines for an
// empty text.
bool FreshTip(const CUiRegion* region, int flag, int extra, bool emptyText) {
    const CUiTooltip* tip = region->tip_;
    if (!tip) return true;   // allocation failure: null, as in the original
    bool ok = reinterpret_cast<std::uintptr_t>(tip->vtable_) == kTooltipVtable && tip->region_ == region &&
              tip->state_ == 0 && tip->unknown_17C == 0;
    if (tip->lineCount_ > 0) ok = ok && tip->flag_ == flag && tip->extra_ == extra;
    if (emptyText) ok = ok && tip->lineCount_ == 0;
    return ok;
}

} // namespace

// CUiRegion::SetTip (005FC730). id 0 gives an empty tooltip with (1, 0).
static void(__fastcall* s_setTip)(CUiRegion*, void*, int, int, int);
static void __fastcall SetTipVerify(CUiRegion* self, void*, int id, int flag, int extra) {
    self->CUiRegion::SetTip(id, flag, extra);
    bool ok = id == 0 ? FreshTip(self, 1, 0, true) : FreshTip(self, flag, extra, false);
    const CUiTooltip* tip = self->tip_;
    ei::verify::Check("CUiRegion::SetTip", true, ok, "id %d (%d, %d): tip %p lines %d args %d %d", id, flag, extra, tip,
                      tip ? tip->lineCount_ : -1, tip ? tip->flag_ : 0, tip ? tip->extra_ : 0);
}
EI_VERIFY(0x005FC730, SetTipVerify, 0, s_setTip);

// CUiRegion::SetTipString (005FC6B0).
static void(__fastcall* s_setTipString)(CUiRegion*, void*, const char*, int, int);
static void __fastcall SetTipStringVerify(CUiRegion* self, void*, const char* text, int flag, int extra) {
    self->CUiRegion::SetTipString(text, flag, extra);
    bool ok = FreshTip(self, flag, extra, !text || !*text);
    const CUiTooltip* tip = self->tip_;
    ei::verify::Check("CUiRegion::SetTipString", true, ok, "\"%.40s\" (%d, %d): tip %p lines %d args %d %d",
                      text ? text : "(null)", flag, extra, tip, tip ? tip->lineCount_ : -1, tip ? tip->flag_ : 0,
                      tip ? tip->extra_ : 0);
}
EI_VERIFY(0x005FC6B0, SetTipStringVerify, 0, s_setTipString);

// CUiRegion::SetTipText (005FC690): the region keeps its tooltip.
static void(__fastcall* s_setTipText)(CUiRegion*, void*, const char*);
static void __fastcall SetTipTextVerify(CUiRegion* self, void*, const char* text) {
    CUiTooltip* before = self->tip_;
    self->CUiRegion::SetTipText(text);
    ei::verify::Check("CUiRegion::SetTipText", true, self->tip_ == before && (!before || before->region_ == self),
                      "tip %p -> %p", before, self->tip_);
}
EI_VERIFY(0x005FC690, SetTipTextVerify, 0, s_setTipText);

// CUiRegion::ReleaseContents (005FC600): the fields are left as they were.
static void(__fastcall* s_releaseContents)(CUiRegion*, void*);
static void __fastcall ReleaseContentsVerify(CUiRegion* self, void*) {
    CUiTooltip* tip = self->tip_;
    UiPoint* points = self->points_.data;
    self->CUiRegion::ReleaseContents();
    ei::verify::Check("CUiRegion::ReleaseContents", true, self->tip_ == tip && self->points_.data == points, "tip %p points %p",
                      tip, points);
}
EI_VERIFY(0x005FC600, ReleaseContentsVerify, 0, s_releaseContents);
