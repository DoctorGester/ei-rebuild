// Checks for the scrolling text's members (ui_scroll_text.cpp, A-H20). Scenarios: every new game
// builds the battle screen's message log (constructor, AddText for each message, SetText when a
// panel clears it, Draw every frame, the destructor at exit); ui_objectives (the quest log:
// SetText, Clear, AddText), ui_briefing_* (the dialogue text).
// - CUiScrollText::Layout (0065DAA0): side by side. The original runs on a stand-in (a byte copy
//   of the text with an empty line array, sharing the tokens and the sprites), ours on the real
//   one; the lines (heights, first tokens), the maximum, position and thumb compared. The
//   original's SetMaximum and SetPosition on the stand-in write the shared sprites as ours does.
// - CUiScrollbar::SetMaximum (0065AFE0): side by side; the original first, the bar's state, the
//   four sprites' visible flags and the thumb sprite's position taken and put back, then ours.
// - CUiScrollbar::Maximum (0065B720): against the original.
// - Construct, DestroyBody, AddText, SetText, SetRect, Draw, Clear: ours only (they allocate,
//   free or draw): the vtables, arrays and rectangles; AddText ends at the end (position =
//   maximum), SetText at the top; Draw returns 1 and takes the moved flag; Clear empties the
//   tokens.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

constexpr int kSprites = 4;

struct BarState {
    int maximum;
    int position;
    bool moved;
    RECT thumb;
    int visible[kSprites];
    Vector3 thumbSprite;
};
CUiMesh* Sprite(CUiScrollbar* self, int i) { return reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[i]); }
BarState Take(CUiScrollbar* self) {
    BarState s{self->maxPosition_, self->position_, self->moved_, self->thumb_, {}, {}};
    for (int i = 0; i < kSprites; ++i) s.visible[i] = Sprite(self, i)->visible_;
    std::memcpy(&s.thumbSprite, &Sprite(self, 1)->position_, sizeof(Vector3));
    return s;
}
void Put(CUiScrollbar* self, const BarState& s) {
    self->maxPosition_ = s.maximum;
    self->position_ = s.position;
    self->moved_ = s.moved;
    self->thumb_ = s.thumb;
    for (int i = 0; i < kSprites; ++i) Sprite(self, i)->visible_ = s.visible[i];
    std::memcpy(&Sprite(self, 1)->position_, &s.thumbSprite, sizeof(Vector3));
}
bool Same(const BarState& a, const BarState& b) {
    return a.maximum == b.maximum && a.position == b.position && a.moved == b.moved &&
           std::memcmp(&a.thumb, &b.thumb, sizeof(RECT)) == 0 && std::memcmp(a.visible, b.visible, sizeof(a.visible)) == 0 &&
           std::memcmp(&a.thumbSprite, &b.thumbSprite, sizeof(Vector3)) == 0;
}

bool HasVtables(const CUiScrollText* self) {
    return self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742E9C)) &&
           self->inputVtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742E68)) &&
           self->tokens_.vtable == reinterpret_cast<void*>(std::uintptr_t(0x00742F14)) &&
           self->lines_.vtable == reinterpret_cast<void*>(std::uintptr_t(0x00742F08));
}
bool TextRectFor(const CUiScrollText* self, const RECT& rect) {
    return self->textRect_.left == 0 && self->textRect_.top == 0 && self->textRect_.right == rect.right - rect.left - 14 &&
           self->textRect_.bottom == rect.bottom - rect.top;
}
} // namespace

// --- Layout --------------------------------------------------------------------------------------

// 0065DAA0: SUB ESP,0x28; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes).
static void(__fastcall* s_layout)(CUiScrollText*);
static void __fastcall LayoutVerify(CUiScrollText* self) {
    alignas(8) static std::uint8_t standIn[sizeof(CUiScrollText)];
    std::memcpy(standIn, self, sizeof(CUiScrollText));
    auto* scratch = reinterpret_cast<CUiScrollText*>(standIn);
    scratch->lines_.data = nullptr;
    scratch->lines_.size = 0;
    scratch->lines_.maxSize = 0;
    s_layout(scratch);
    self->CUiScrollText::Layout();
    bool ok = scratch->lines_.size == self->lines_.size && scratch->maxPosition_ == self->maxPosition_ &&
              scratch->position_ == self->position_ && std::memcmp(&scratch->thumb_, &self->thumb_, sizeof(RECT)) == 0;
    int bad = -1;
    for (int i = 0; ok && i < self->lines_.size; ++i) {
        const UiTextLine* a = scratch->lines_.data[i];
        const UiTextLine* b = self->lines_.data[i];
        if (a->height_ != b->height_ || a->firstToken_ != b->firstToken_) {
            ok = false;
            bad = i;
        }
    }
    ei::verify::Check("CUiScrollText::Layout", true, ok, "tokens %d width %d: lines %d / %d (first difference %d), max %d / %d",
                      self->tokens_.size, self->textRect_.right - self->textRect_.left, scratch->lines_.size,
                      self->lines_.size, bad, scratch->maxPosition_, self->maxPosition_);
    for (int i = 0; i < scratch->lines_.size; ++i) gamecrt::operator_delete(scratch->lines_.data[i]);
    if (scratch->lines_.data) gamecrt::operator_delete(scratch->lines_.data);
}
EI_VERIFY(0x0065DAA0, LayoutVerify, 6, s_layout);

// --- The scroll bar's maximum ------------------------------------------------------------------------

// 0065AFE0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static void(__fastcall* s_setMaximum)(CUiScrollbar*, void*, int);
static void __fastcall SetMaximumVerify(CUiScrollbar* self, void*, int maximum) {
    BarState before = Take(self);
    s_setMaximum(self, nullptr, maximum);
    BarState expected = Take(self);
    Put(self, before);
    self->CUiScrollbar::SetMaximum(maximum);
    BarState actual = Take(self);
    ei::verify::Check("CUiScrollbar::SetMaximum", true, Same(expected, actual), "%d (position %d): %d / %d, position %d / %d",
                      maximum, before.position, expected.maximum, actual.maximum, expected.position, actual.position);
}
EI_VERIFY(0x0065AFE0, SetMaximumVerify, 5, s_setMaximum);

// 0065B720: MOV EAX,[ECX+0x148] (6 bytes).
static int(__fastcall* s_maximum)(const CUiScrollbar*);
static int __fastcall MaximumVerify(const CUiScrollbar* self) {
    int expected = s_maximum(self);
    int actual = self->CUiScrollbar::Maximum();
    ei::verify::Check("CUiScrollbar::Maximum", expected, actual, "");
    return actual;
}
EI_VERIFY(0x0065B720, MaximumVerify, 6, s_maximum);

// --- Ours only ---------------------------------------------------------------------------------------

static CUiScrollText* __fastcall ScrollTextConstructVerify(CUiScrollText* self, void*, CInterface3D* ui, const char* text,
                                                           RECT rect, float depth) {
    self->CUiScrollText::Construct(ui, text, rect, depth);
    bool ok = HasVtables(self) && TextRectFor(self, rect) && self->align_ == 2 && self->surfaces_.size == 1 &&
              self->position_ == 0 && self->maxPosition_ >= 0 && (self->tokens_.size == 0) == (self->lines_.size == 0) &&
              std::memcmp(&self->depth_, &depth, sizeof(float)) == 0;
    ei::verify::Check("CUiScrollText::Construct", true, ok, "(%d,%d,%d,%d) tokens %d lines %d max %d", rect.left, rect.top,
                      rect.right, rect.bottom, self->tokens_.size, self->lines_.size, self->maxPosition_);
    return self;
}
EI_VERIFY(0x0065CC00, ScrollTextConstructVerify, 0, s_unused);

static void __fastcall ScrollTextDestroyVerify(CUiScrollText* self) {
    self->CUiScrollText::DestroyBody();
    bool ok = !self->tokens_.data && !self->lines_.data && !self->sceneObjects_.data && !self->surfaces_.data &&
              !self->regions_.data;
    ei::verify::Check("CUiScrollText::DestroyBody", true, ok, "");
}
EI_VERIFY(0x0065CE20, ScrollTextDestroyVerify, 0, s_unused);

static void __fastcall ScrollTextAddVerify(CUiScrollText* self, void*, const char* text) {
    int before = self->tokens_.size;
    self->CUiScrollText::AddText(text);
    bool ok = self->tokens_.size >= before && self->position_ == self->maxPosition_;
    ei::verify::Check("CUiScrollText::AddText", true, ok, "tokens %d -> %d, position %d max %d", before,
                      self->tokens_.size, self->position_, self->maxPosition_);
}
EI_VERIFY(0x0065CF30, ScrollTextAddVerify, 0, s_unused);

static void __fastcall ScrollTextSetVerify(CUiScrollText* self, void*, const char* text) {
    self->CUiScrollText::SetText(text);
    bool ok = self->position_ == 0 && self->moved_ && (self->tokens_.size == 0) == (self->lines_.size == 0);
    ei::verify::Check("CUiScrollText::SetText", true, ok, "tokens %d lines %d position %d", self->tokens_.size,
                      self->lines_.size, self->position_);
}
EI_VERIFY(0x0065CFE0, ScrollTextSetVerify, 0, s_unused);

static void __fastcall ScrollTextSetRectVerify(CUiScrollText* self, void*, const RECT* rect) {
    RECT copy = *rect;
    self->CUiScrollText::SetRect(rect);
    bool ok = std::memcmp(&self->rect_, &copy, sizeof(RECT)) == 0 && TextRectFor(self, copy);
    ei::verify::Check("CUiScrollText::SetRect", true, ok, "(%d,%d,%d,%d)", copy.left, copy.top, copy.right, copy.bottom);
}
EI_VERIFY(0x0065D0A0, ScrollTextSetRectVerify, 0, s_unused);

static BOOL __fastcall ScrollTextDrawVerify(CUiScrollText* self, void*, int pass) {
    BOOL result = self->CUiScrollText::Draw(pass);
    ei::verify::Check("CUiScrollText::Draw", true, result == 1 && !self->moved_, "result %d", result);
    return result;
}
EI_VERIFY(0x0065D120, ScrollTextDrawVerify, 0, s_unused);

static void __fastcall ScrollTextClearVerify(CUiScrollText* self) {
    self->CUiScrollText::Clear();
    ei::verify::Check("CUiScrollText::Clear", true, self->tokens_.size == 0 && !self->tokens_.data, "");
}
EI_VERIFY(0x0065D450, ScrollTextClearVerify, 0, s_unused);
