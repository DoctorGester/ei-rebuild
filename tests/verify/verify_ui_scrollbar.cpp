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

// CUiScrollText::NextToken (0065D970; ui_scroll_text.cpp): side by side, ours first, then the
// original's token into a scratch string (SV2h1); the positions and (when not at the end) the tokens compared. Reached by every
// text the message log and the quest logs add (Tokenize; ui_objectives, the HUD scenarios).
// MOV EAX,FS:[0] (6 bytes).
static int(__fastcall* s_nextToken)(CUiScrollText*, void*, const mfc::CString*, int, mfc::CString*);
static int __fastcall NextTokenVerify(CUiScrollText* self, void*, const mfc::CString* text, int pos,
                                      mfc::CString* out) {
    int actual = self->CUiScrollText::NextToken(text, pos, out);
    mfc::CString scratch;
    int expected = s_nextToken(self, nullptr, text, pos, &scratch);
    bool ok = expected == actual && (actual == -1 || std::strcmp(scratch, *out) == 0);
    ei::verify::Check("CUiScrollText::NextToken", true, ok, "pos %d: %d / %d '%s' / '%s'", pos, expected, actual,
                      static_cast<const char*>(scratch), actual == -1 ? "" : static_cast<const char*>(*out));
    return actual;
}
EI_VERIFY(0x0065D970, NextTokenVerify, 6, s_nextToken);

// --- A-H20 -----------------------------------------------------------------------------------------
// - CUiSlider::SetPosition (0065B730): side by side as the scroll bar's (the options sliders,
//   ui_options).
// - The thumb drags (0065B1A0 / 0065B810) and the wheel (0065B430): side by side; the original
//   first, the position state (and the handled flag, the accumulated amount) put back, then ours.
//   No scenario drags a thumb or turns the wheel over a bar.

static void(__fastcall* s_sliderSetPosition)(CUiSlider*, void*, int);
static void __fastcall SliderSetPositionVerify(CUiSlider* self, void*, int position) {
    ScrollPosition before = Take(self);
    s_sliderSetPosition(self, nullptr, position);
    ScrollPosition expected = Take(self);
    Put(self, before);
    self->CUiSlider::SetPosition(position);
    ScrollPosition actual = Take(self);
    ei::verify::Check("CUiSlider::SetPosition", true, Same(expected, actual), "%d (max %d): %d / %d, thumb left %d / %d",
                      position, self->maxPosition_, expected.position, actual.position, expected.thumb.left,
                      actual.thumb.left);
}
// PUSH ECX; MOV EAX,[ESP+8] (5 bytes).
EI_VERIFY(0x0065B730, SliderSetPositionVerify, 5, s_sliderSetPosition);

namespace {
CUiScrollbar* BarOf(void* input) { return reinterpret_cast<CUiScrollbar*>(static_cast<char*>(input) - 0x94); }

template <typename Original, typename Ours>
void DragVerify(const char* name, void* input, int buttons, int x, int y, Original original, Ours ours) {
    CUiScrollbar* self = BarOf(input);
    ScrollPosition before = Take(self);
    bool handled = self->inputHandled_;
    original(input, nullptr, buttons, x, y);
    ScrollPosition expected = Take(self);
    bool expectedHandled = self->inputHandled_;
    Put(self, before);
    self->inputHandled_ = handled;
    ours(self);
    ScrollPosition actual = Take(self);
    ei::verify::Check(name, true, Same(expected, actual) && expectedHandled == self->inputHandled_,
                      "(%d,%d) state %d: %d / %d", x, y, self->state_, expected.position, actual.position);
}
} // namespace

// PUSH ECX; PUSH ESI; MOV ESI,ECX; CMP [ESI+0xF0],1 (11 bytes).
static void(__fastcall* s_scrollbarMouseMove)(void*, void*, int, int, int);
static void __fastcall ScrollbarMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    DragVerify("CUiScrollbar::OnMouseMove", input, buttons, x, y, s_scrollbarMouseMove,
               [&](CUiScrollbar* self) { self->CUiScrollbar::OnMouseMove(buttons, x, y); });
}
EI_VERIFY(0x0065B1A0, ScrollbarMouseMoveVerify, 11, s_scrollbarMouseMove);

static void(__fastcall* s_sliderMouseMove)(void*, void*, int, int, int);
static void __fastcall SliderMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    DragVerify("CUiSlider::OnMouseMove", input, buttons, x, y, s_sliderMouseMove,
               [&](CUiScrollbar* self) { static_cast<CUiSlider*>(self)->CUiSlider::OnMouseMove(buttons, x, y); });
}
EI_VERIFY(0x0065B810, SliderMouseMoveVerify, 11, s_sliderMouseMove);

// MOV EAX,[ESP+0x10]; PUSH ESI (5 bytes).
static int(__fastcall* s_scrollbarWheel)(void*, void*, int, int, int, int);
static int __fastcall ScrollbarWheelVerify(void* input, void*, int flags, int delta, int x, int y) {
    CUiScrollbar* self = BarOf(input);
    float accumulated = self->accumulated_;
    bool handled = self->inputHandled_;
    int expected = s_scrollbarWheel(input, nullptr, flags, delta, x, y);
    float expectedAccumulated = self->accumulated_;
    bool expectedHandled = self->inputHandled_;
    self->accumulated_ = accumulated;
    self->inputHandled_ = handled;
    int actual = self->CUiScrollbar::OnWheel(flags, delta, x, y);
    ei::verify::CheckNear("CUiScrollbar::OnWheel", expectedAccumulated, self->accumulated_, "delta %d at (%d,%d)", delta,
                          x, y);
    ei::verify::Check("CUiScrollbar::OnWheel handled", expectedHandled && expected == 1, self->inputHandled_ && actual == 1,
                      "delta %d", delta);
    return actual;
}
EI_VERIFY(0x0065B430, ScrollbarWheelVerify, 5, s_scrollbarWheel);
