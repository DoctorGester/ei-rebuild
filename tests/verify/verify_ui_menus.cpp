// Checks for the save/load and options windows (ui_menus.cpp).
//
// These handlers play sounds, open message boxes and call into the save code, so ours run alone
// and the wrapper checks the state they must leave (selection, pending close value, flags). The
// options window's HelpTopic is pure and runs side by side.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;

template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - 0x94);
}

struct Pending {
    std::uint32_t value;
    bool flag;
};
Pending PendingOf(CUiWindow* w) { return Pending{w->interface_->pendingValue_, w->interface_->pendingFlag_}; }

bool PendingIs(CUiWindow* w, std::uint32_t value) {
    return w->interface_->pendingFlag_ && w->interface_->pendingValue_ == value;
}
bool PendingUnchanged(CUiWindow* w, const Pending& before) {
    return w->interface_->pendingValue_ == before.value && w->interface_->pendingFlag_ == before.flag;
}

// Flag 4 of the first surface (+0x0C): set while the window rests, cleared while it slides.
bool SurfaceFrozen(CUiWindow* w) {
    std::uint32_t flags;
    std::memcpy(&flags, reinterpret_cast<std::uint8_t*>(w->surfaces_.data[0]) + 0x0C, 4);
    return (flags & 4) != 0;
}
} // namespace

// --- Save/load window ----------------------------------------------------------------------------

static void __fastcall SaveLoadMouseDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CSaveLoadWindow>(input);
    bool sliding = w->sliding_;
    int region = w->RegionAt(x, y);
    int selected = w->selected_;
    int count = w->EntryCount();
    unsigned index = static_cast<unsigned>(w->firstVisible_ + region - 3);
    Pending pending = PendingOf(w);
    w->CSaveLoadWindow::OnMouseDown(buttons, x, y);
    bool ok;
    if (sliding)
        ok = w->selected_ == selected && PendingUnchanged(w, pending);
    else if (region == 1)
        ok = PendingIs(w, 0x100001);
    else if (region >= 3 && index < static_cast<unsigned>(count))
        ok = w->selected_ == static_cast<int>(index);
    else if (region < 0)
        ok = w->selected_ == selected;
    else
        ok = true;   // OK and delete go to original code
    ei::verify::Check("CSaveLoadWindow::OnMouseDown", true, ok, "(%d,%d) region %d sliding %d selected %d->%d", x, y,
                      region, sliding, selected, w->selected_);
}
EI_VERIFY(0x00654660, SaveLoadMouseDownVerify, 0, s_unused);

static void __fastcall SaveLoadDoubleClickVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<CSaveLoadWindow>(input);
    bool sliding = w->sliding_;
    int region = w->RegionAt(x, y);
    int selected = w->selected_;
    w->CSaveLoadWindow::OnDoubleClick(buttons, x, y);
    bool ok = !(sliding || region < 3) || w->selected_ == selected;
    ei::verify::Check("CSaveLoadWindow::OnDoubleClick", true, ok, "(%d,%d) region %d sliding %d", x, y, region,
                      sliding);
}
EI_VERIFY(0x006547B0, SaveLoadDoubleClickVerify, 0, s_unused);

template <typename W>
BOOL CheckSlideDraw(const char* name, W* w, int pass, float rest) {
    bool sliding = w->sliding_;
    BOOL result = w->W::Draw(pass);
    bool ok = result == 1 && SurfaceFrozen(w) == !sliding;
    ei::verify::Check(name, true, ok, "sliding %d rest %g", sliding, rest);
    return result;
}

static BOOL __fastcall SaveLoadDrawVerify(CSaveLoadWindow* self, void*, int pass) {
    return CheckSlideDraw("CSaveLoadWindow::Draw", self, pass, self->isSave_ ? 620.0f : 550.0f);
}
EI_VERIFY(0x006544A0, SaveLoadDrawVerify, 0, s_unused);

static void __fastcall SaveLoadUpdateVerify(CSaveLoadWindow* self, void*) {
    bool sliding = self->sliding_;
    float slide = self->slide_;
    self->CSaveLoadWindow::Update();
    bool ok = sliding ? self->slide_ >= slide && (self->sliding_ || self->slide_ > 1.0f) : !self->sliding_;
    ei::verify::Check("CSaveLoadWindow::Update", true, ok, "sliding %d slide %g->%g", sliding, slide, self->slide_);
}
EI_VERIFY(0x006545E0, SaveLoadUpdateVerify, 0, s_unused);

static void __fastcall SaveLoadCloseDialogVerify(CSaveLoadWindow* self, void*) {
    self->CSaveLoadWindow::CloseDialog();
    ei::verify::Check("CSaveLoadWindow::CloseDialog", true, self->dialog_ == nullptr, "");
}
EI_VERIFY(0x00654430, SaveLoadCloseDialogVerify, 0, s_unused);

static void __fastcall SaveLoadDialogResultVerify(CSaveLoadWindow* self, void*, int result) {
    Pending pending = PendingOf(self);
    self->CSaveLoadWindow::OnDialogResult(result);
    bool ok = result == 0x30001 ? PendingIs(self, 0x100002) : result == 0x10001 || result == 0x20001 ||
                                                                  PendingUnchanged(self, pending);
    ei::verify::Check("CSaveLoadWindow::OnDialogResult", true, ok, "result %X", result);
}
EI_VERIFY(0x00654450, SaveLoadDialogResultVerify, 0, s_unused);

// --- Options window --------------------------------------------------------------------------------

static void __fastcall OptionsMouseDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<COptionsWindow>(input);
    bool sliding = w->sliding_;
    int region = w->RegionAt(x, y);
    int row = w->selectedRow_;
    bool waiting = w->waitingForKey_;
    bool hasRow = region >= 13 && (w->RegionRow(region).binding_ || w->RegionRow(region).spin_);
    Pending pending = PendingOf(w);
    w->COptionsWindow::OnMouseDown(buttons, x, y);
    bool ok;
    if (sliding)
        ok = w->selectedRow_ == row && w->waitingForKey_ == waiting && PendingUnchanged(w, pending);
    else if (region == 0 || region == 1)
        ok = PendingIs(w, 0x110001);
    else if (region >= 2 && region < 13)
        ok = !w->waitingForKey_;
    else if (hasRow)
        ok = w->selectedRow_ == region - 13 && !w->waitingForKey_;
    else
        ok = w->selectedRow_ == row && w->waitingForKey_ == waiting;
    ei::verify::Check("COptionsWindow::OnMouseDown", true, ok, "(%d,%d) region %d sliding %d", x, y, region, sliding);
}
EI_VERIFY(0x00650C20, OptionsMouseDownVerify, 0, s_unused);

static void __fastcall OptionsDoubleClickVerify(void* input, void*, int buttons, int x, int y) {
    auto* w = FromInput<COptionsWindow>(input);
    bool sliding = w->sliding_;
    int region = w->RegionAt(x, y);
    int row = w->selectedRow_;
    bool binding = !sliding && region >= 13 && w->RegionRow(region).binding_;
    w->COptionsWindow::OnDoubleClick(buttons, x, y);
    bool ok = binding ? w->selectedRow_ == region - 13 && w->waitingForKey_ : sliding ? w->selectedRow_ == row : true;
    ei::verify::Check("COptionsWindow::OnDoubleClick", true, ok, "(%d,%d) region %d sliding %d", x, y, region,
                      sliding);
}
EI_VERIFY(0x00650D70, OptionsDoubleClickVerify, 0, s_unused);

static BOOL __fastcall OptionsDrawVerify(COptionsWindow* self, void*, int pass) {
    return CheckSlideDraw("COptionsWindow::Draw", self, pass, 480.0f);
}
EI_VERIFY(0x00650920, OptionsDrawVerify, 0, s_unused);

static void __fastcall OptionsRedrawVerify(COptionsWindow* self, void*) {
    bool sliding = self->sliding_;
    self->COptionsWindow::Redraw();
    ei::verify::Check("COptionsWindow::Redraw", sliding, self->sliding_, "");
}
EI_VERIFY(0x00650A20, OptionsRedrawVerify, 0, s_unused);

static void __fastcall OptionsDialogResultVerify(COptionsWindow* self, void*, int result) {
    self->COptionsWindow::OnDialogResult(result);
    ei::verify::Check("COptionsWindow::OnDialogResult", true, true, "");
}
EI_VERIFY(0x006508E0, OptionsDialogResultVerify, 0, s_unused);

// 00650900: MOV AL,[ECX+0x3F4] (6 bytes).
static const char*(__fastcall* s_optionsHelpTopic)(COptionsWindow*, void*);
static const char* __fastcall OptionsHelpTopicVerify(COptionsWindow* self, void*) {
    const char* expected = s_optionsHelpTopic(self, nullptr);
    const char* actual = self->COptionsWindow::HelpTopic();
    ei::verify::Check("COptionsWindow::HelpTopic", expected, actual, "waiting %d", self->waitingForKey_);
    return expected;
}
EI_VERIFY(0x00650900, OptionsHelpTopicVerify, 6, s_optionsHelpTopic);

// Ours only (it applies volumes and gamma): the slide as for the save/load window, and every
// setting whose scroll bar moved takes the bar's position (the bar's moved flag is consumed).
static void __fastcall OptionsUpdateVerify(COptionsWindow* self, void*) {
    bool sliding = self->sliding_;
    float slide = self->slide_;
    bool moved[26];
    for (int i = 0; i < 26; ++i) moved[i] = self->settings_[i].bar_ && self->settings_[i].bar_->moved_;
    self->COptionsWindow::Update();
    bool ok = sliding ? self->slide_ >= slide && (self->sliding_ || self->slide_ > 1.0f) : !self->sliding_;
    int bad = -1;
    for (int i = 0; i < 26 && bad < 0; ++i) {
        CUiScrollbar* bar = self->settings_[i].bar_;
        if (!bar) continue;
        if (bar->moved_ || (moved[i] && self->settings_[i].value_ != bar->position_)) bad = i;
    }
    ok = ok && bad < 0;
    ei::verify::Check("COptionsWindow::Update", true, ok, "sliding %d slide %g->%g setting %d", sliding, slide,
                      self->slide_, bad);
}
EI_VERIFY(0x00650A30, OptionsUpdateVerify, 0, s_unused);

// --- Layout builders (ui_layouts.cpp): ours only; what they add is counted ------------------------

namespace {
struct WindowCounts {
    int objects, sceneObjects, surfaces, regions, models, children;
};
WindowCounts CountsOf(CUiContainer* w) {
    return WindowCounts{w->objects_.size, w->sceneObjects_.size, w->surfaces_.size, w->regions_.size,
                        w->models_.size, w->children_.size};
}
bool HasChild(CUiContainer* w, CUiWindow* child) {
    for (int i = 0; i < w->children_.size; ++i)
        if (w->children_.data[i] == child) return true;
    return false;
}
} // namespace

// The save/load window: 2 textures (and the screenshot FillEntries may take), 11 sprites, 1 surface, 19 regions (3 buttons, 16 rows), the
// model, the scroll bar as a child; the selection and the offered name as the original sets them.
static void __fastcall SaveLoadBuildVerify(CSaveLoadWindow* self, void*) {
    WindowCounts before = CountsOf(self);
    self->CSaveLoadWindow::Build();
    WindowCounts after = CountsOf(self);
    int count = self->EntryCount();
    bool ok = after.objects >= before.objects + 2 && after.sceneObjects == before.sceneObjects + 11 &&
              after.surfaces == before.surfaces + 1 && after.regions == before.regions + 19 &&
              after.models == before.models + 1 && self->scrollbar_ && HasChild(self, self->scrollbar_) &&
              self->rect_.right == 800 && self->rect_.bottom == 600;
    if (self->isSave_)
        ok = ok && self->selected_ == 0 &&
             (count == 0 || self->cursor_ == static_cast<int>(std::strlen(self->name_)));
    else
        ok = ok && self->selected_ == (count ? 0 : -1);
    ei::verify::Check("CSaveLoadWindow::Build", true, ok, "save %d entries %d regions %d->%d sprites %d->%d",
                      self->isSave_, count, before.regions, after.regions, before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x00653DD0, SaveLoadBuildVerify, 0, s_unused);

// The options window: 16 sprites, 27 regions (OK, cancel, 11 groups, 14 rows), 1 surface, the
// model, 11 more group titles; the key wait and the display flag cleared.
static void __fastcall OptionsBuildVerify(COptionsWindow* self, void*) {
    WindowCounts before = CountsOf(self);
    int titles = self->groupNames_.size;
    self->COptionsWindow::Build();
    WindowCounts after = CountsOf(self);
    int titlesAfter = self->groupNames_.size;
    bool ok = after.objects >= before.objects + 2 && after.sceneObjects == before.sceneObjects + 16 &&
              after.surfaces == before.surfaces + 1 && after.regions == before.regions + 27 &&
              after.models == before.models + 1 && titlesAfter == titles + 11 && !self->waitingForKey_ &&
              !self->displayModeChanged_ && self->rect_.right == 800 && self->rect_.bottom == 600;
    ei::verify::Check("COptionsWindow::Build", true, ok, "regions %d->%d sprites %d->%d titles %d->%d",
                      before.regions, after.regions, before.sceneObjects, after.sceneObjects, titles, titlesAfter);
}
EI_VERIFY(0x006503C0, OptionsBuildVerify, 0, s_unused);

// --- Scroll bar --------------------------------------------------------------------------------------

// 0065B090: MOV EDX,[ESP+4]; MOV EAX,[ECX+0x150] (10 bytes). The original on the real object,
// then the moved flag put back and ours run.
static BOOL(__fastcall* s_takePosition)(CUiScrollbar*, void*, int*);
static BOOL __fastcall TakePositionVerify(CUiScrollbar* self, void*, int* position) {
    bool moved = self->moved_;
    int expectedPosition = 0;
    BOOL expected = s_takePosition(self, nullptr, &expectedPosition);
    bool expectedMoved = self->moved_;
    self->moved_ = moved;
    BOOL actual = self->CUiScrollbar::TakePosition(position);
    bool same = expected == actual && expectedPosition == *position && expectedMoved == self->moved_;
    ei::verify::Check("CUiScrollbar::TakePosition", true, same, "moved %d result %d/%d", moved, expected, actual);
    return expected;
}
EI_VERIFY(0x0065B090, TakePositionVerify, 10, s_takePosition);

namespace {
// Everything the scroll bar's button handlers change.
struct ScrollInput {
    void* capture;
    std::uint32_t handled;
    int state;
    int grabX;
    int grabY;
    float accumulated;
};
ScrollInput TakeScrollInput(CUiScrollbar* w) {
    return ScrollInput{w->interface_->mouseCapture_, w->inputHandled_ ? 1u : 0u, w->state_, w->grabX_, w->grabY_,
                       w->accumulated_};
}
void PutScrollInput(CUiScrollbar* w, const ScrollInput& s) {
    w->interface_->mouseCapture_ = s.capture;
    w->inputHandled_ = s.handled != 0;
    w->state_ = s.state;
    w->grabX_ = s.grabX;
    w->grabY_ = s.grabY;
    w->accumulated_ = s.accumulated;
}

using ScrollHandler = void(__fastcall*)(void*, void*, int, int, int);

template <typename Ours>
void ScrollSideBySide(const char* name, ScrollHandler original, void* input, int buttons, int x, int y, Ours ours) {
    auto* w = FromInput<CUiScrollbar>(input);
    ScrollInput before = TakeScrollInput(w);
    original(input, nullptr, buttons, x, y);
    ScrollInput expected = TakeScrollInput(w);
    PutScrollInput(w, before);
    ours(w);
    ScrollInput actual = TakeScrollInput(w);
    ei::verify::Check(name, expected, actual, "(%d,%d) state %d->%d/%d", x, y, before.state, expected.state,
                      actual.state);
}
} // namespace

// 0065B240: PUSH EBX; MOV EBX,[ESP+0xC] (5 bytes).
static ScrollHandler s_scrollMouseDown;
static void __fastcall ScrollMouseDownVerify(void* input, void*, int buttons, int x, int y) {
    ScrollSideBySide("CUiScrollbar::OnMouseDown", s_scrollMouseDown, input, buttons, x, y,
                     [&](CUiScrollbar* w) { w->CUiScrollbar::OnMouseDown(buttons, x, y); });
}
EI_VERIFY(0x0065B240, ScrollMouseDownVerify, 5, s_scrollMouseDown);

// 0065B340: XOR EAX,EAX; MOV [ECX+0xF0],EAX (8 bytes).
static ScrollHandler s_scrollMouseUp;
static void __fastcall ScrollMouseUpVerify(void* input, void*, int buttons, int x, int y) {
    ScrollSideBySide("CUiScrollbar::OnMouseUp", s_scrollMouseUp, input, buttons, x, y,
                     [&](CUiScrollbar* w) { w->CUiScrollbar::OnMouseUp(buttons, x, y); });
}
EI_VERIFY(0x0065B340, ScrollMouseUpVerify, 8, s_scrollMouseUp);

// Ours only (it moves the position through a virtual call): what is left over afterwards is less
// than one step either way.
static void __fastcall ScrollUpdateVerify(CUiScrollbar* self, void*) {
    self->CUiScrollbar::Update();
    float rest = self->accumulated_;
    bool ok = rest < 1.0f && rest > -1.0f;
    ei::verify::Check("CUiScrollbar::Update", true, ok, "state %d rest %g", self->state_, rest);
}
EI_VERIFY(0x0065B360, ScrollUpdateVerify, 0, s_unused);

// --- Tooltip -----------------------------------------------------------------------------------------

// Ours only (drawing): returns 1 either way (the base drawing returns 1 too).
static BOOL __fastcall TooltipDrawVerify(CUiTooltip* self, void*, int pass) {
    BOOL result = self->CUiTooltip::Draw(pass);
    ei::verify::Check("CUiTooltip::Draw", 1, result, "state %d", self->state_);
    return result;
}
EI_VERIFY(0x0065EA50, TooltipDrawVerify, 0, s_unused);

static void __fastcall TooltipRedrawVerify(CUiTooltip* self, void*) {
    int state = self->state_;
    self->CUiTooltip::Redraw();
    ei::verify::Check("CUiTooltip::Redraw", state, self->state_, "");
}
EI_VERIFY(0x0065EA80, TooltipRedrawVerify, 0, s_unused);
