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
    std::memcpy(&flags, reinterpret_cast<std::uint8_t*>(w->surfaces_.data_[0]) + 0x0C, 4);
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
