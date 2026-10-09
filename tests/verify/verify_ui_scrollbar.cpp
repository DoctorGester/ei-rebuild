// Checks for ui_scrollbar.cpp (A-H19). Scenarios: the objectives window (ui_objectives,
// its quest list and log bars), the save / load window (ui_saveload) and the
// options window's sliders (ui_options) construct and lay out bars.
// - CUiScrollbar::Construct (0065AEA0), CUiSlider::Construct (0065BC30): ours only (they allocate):
//   the fields, four sprites, three regions, the vtables, the position 0.
// - CUiScrollbar::SetPosition (0065B0C0): side by side; the original first, its position, moved
//   flag, thumb rectangle and thumb sprite position (written by Place, 005E6FA0) taken and put
//   back, then ours; all compared (the sprite position bit for bit: both pass Place the same
//   floats or not).
// - CUiScrollbar::SetRect (0065B490): ours only (sprites and regions through Rebirth-patched code):
//   rect_ and the track.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;

bool LaidOut(const CUiScrollbar* self, const RECT& rect) {
    return std::memcmp(&self->rect_, &rect, sizeof(rect)) == 0 && self->track_.left == rect.right - 10 &&
           self->track_.right == rect.right && self->track_.top == rect.top + 26 && self->track_.bottom == rect.bottom - 26;
}

bool Constructed(const CUiScrollbar* self, const RECT& rect, float depth, int maximum, float speed) {
    return self->maxPosition_ == maximum && std::memcmp(&self->depth_, &depth, 4) == 0 &&
           std::memcmp(&self->speed_, &speed, 4) == 0 && self->position_ == 0 && self->moved_ &&
           self->state_ == 0 && self->accumulated_ == 0.0f && self->sceneObjects_.size == 4 &&
           self->regions_.size == 3 && LaidOut(self, rect);
}

struct ScrollPosition {
    int position;
    bool moved;
    RECT thumb;
    Vector3 sprite;
};
ScrollPosition Take(CUiScrollbar* self) {
    ScrollPosition s{self->position_, self->moved_, self->thumb_, {}};
    std::memcpy(&s.sprite, &reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[1])->position_, sizeof(Vector3));
    return s;
}
void Put(CUiScrollbar* self, const ScrollPosition& s) {
    self->position_ = s.position;
    self->moved_ = s.moved;
    self->thumb_ = s.thumb;
    std::memcpy(&reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[1])->position_, &s.sprite, sizeof(Vector3));
}
bool Same(const ScrollPosition& a, const ScrollPosition& b) {
    return a.position == b.position && a.moved == b.moved && std::memcmp(&a.thumb, &b.thumb, sizeof(RECT)) == 0 &&
           std::memcmp(&a.sprite, &b.sprite, sizeof(Vector3)) == 0;
}

} // namespace

static CUiScrollbar* __fastcall ScrollConstructVerify(CUiScrollbar* self, void*, CInterface3D* ui, RECT rect,
                                                      float depth, int maximum, float speed) {
    self->CUiScrollbar::Construct(ui, rect, depth, maximum, speed);
    bool ok = Constructed(self, rect, depth, maximum, speed) &&
              self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742C18));
    ei::verify::Check("CUiScrollbar::Construct", true, ok, "(%d,%d,%d,%d) max %d", rect.left, rect.top, rect.right,
                      rect.bottom, maximum);
    return self;
}
EI_VERIFY(0x0065AEA0, ScrollConstructVerify, 0, s_unused);

static CUiSlider* __fastcall SliderConstructVerify(CUiSlider* self, void*, CInterface3D* ui, RECT rect, float depth,
                                                   int maximum, float speed) {
    self->CUiSlider::Construct(ui, rect, depth, maximum, speed);
    bool ok = Constructed(self, rect, depth, maximum, speed) &&
              self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742CBC));
    ei::verify::Check("CUiSlider::Construct", true, ok, "(%d,%d,%d,%d) max %d", rect.left, rect.top, rect.right,
                      rect.bottom, maximum);
    return self;
}
EI_VERIFY(0x0065BC30, SliderConstructVerify, 0, s_unused);

// 0065B0C0: PUSH ECX; MOV EAX,[ESP+8] (5 bytes).
static void(__fastcall* s_setPosition)(CUiScrollbar*, void*, int);
static void __fastcall SetPositionVerify(CUiScrollbar* self, void*, int position) {
    ScrollPosition before = Take(self);
    s_setPosition(self, nullptr, position);
    ScrollPosition expected = Take(self);
    Put(self, before);
    self->CUiScrollbar::SetPosition(position);
    ScrollPosition actual = Take(self);
    ei::verify::Check("CUiScrollbar::SetPosition", true, Same(expected, actual), "%d (max %d): %d / %d, thumb top %d / %d",
                      position, self->maxPosition_, expected.position, actual.position, expected.thumb.top,
                      actual.thumb.top);
}
EI_VERIFY(0x0065B0C0, SetPositionVerify, 5, s_setPosition);

static void __fastcall SetRectVerify(CUiScrollbar* self, void*, const RECT* rect) {
    RECT copy = *rect;
    self->CUiScrollbar::SetRect(rect);
    ei::verify::Check("CUiScrollbar::SetRect", true, LaidOut(self, copy), "(%d,%d,%d,%d)", copy.left, copy.top,
                      copy.right, copy.bottom);
}
EI_VERIFY(0x0065B490, SetRectVerify, 0, s_unused);

// CUiScrollText::NextToken (0065D970; ui_scroll_text.cpp): side by side, the original's token into
// a scratch string; the positions and (when not at the end) the tokens compared. Reached by every
// text the message log and the quest logs add (Tokenize; ui_objectives, the HUD scenarios).
// MOV EAX,FS:[0] (6 bytes).
static int(__fastcall* s_nextToken)(CUiScrollText*, void*, const mfc::CString*, int, mfc::CString*);
static int __fastcall NextTokenVerify(CUiScrollText* self, void*, const mfc::CString* text, int pos,
                                      mfc::CString* out) {
    mfc::CString scratch;
    int expected = s_nextToken(self, nullptr, text, pos, &scratch);
    int actual = self->CUiScrollText::NextToken(text, pos, out);
    bool ok = expected == actual && (actual == -1 || std::strcmp(scratch, *out) == 0);
    ei::verify::Check("CUiScrollText::NextToken", true, ok, "pos %d: %d / %d '%s' / '%s'", pos, expected, actual,
                      static_cast<const char*>(scratch), actual == -1 ? "" : static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x0065D970, NextTokenVerify, 6, s_nextToken);
