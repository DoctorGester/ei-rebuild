// Checks for the help / message window (ui_help_window.cpp). Everything loads textures, reads the
// texts, plays sounds or draws a page (00659420), so ours runs alone and the wrapper checks the
// window afterwards. Scenario: ui_help (F1 on the battle screen opens the help window: Build,
// the pulse every frame, Enter / clicks on the page arrows and OK, Esc, the help key).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_screens.h"

#include <cmath>
#include <cstdint>

namespace {
void* s_unused;

bool PageInRange(const CHelpWindow* w) { return w->lines_.size == 0 || (w->page_ >= 0 && w->page_ < w->lines_.size); }
} // namespace

// 00658E10 (stolen 0): with pages, the page within them and the pulse restarted; without, the
// interface's pending flag raised (SetPendingValue(0)).
static void __fastcall BuildVerify(CHelpWindow* self) {
    self->Build();
    bool ok = self->lines_.size > 0 ? PageInRange(self) && self->pulseTime_ == 0.0f
                                    : self->interface_->pendingFlag_ && self->interface_->pendingValue_ == 0;
    ei::verify::Check("CHelpWindow::Build", true, ok, "%d pages, page %d", self->lines_.size, self->page_);
}
EI_VERIFY(0x00658E10, BuildVerify, 0, s_unused);

// 006590F0 (stolen 0): both lists, the surfaces, regions and scene objects empty.
static void __fastcall CloseVerify(CHelpWindow* self) {
    self->Close();
    bool ok = self->pulsing_.Empty() && self->listB_.Empty() && self->surfaces_.size == 0 && self->regions_.size == 0 &&
              self->sceneObjects_.size == 0;
    ei::verify::Check("CHelpWindow::Close", true, ok, "");
}
EI_VERIFY(0x006590F0, CloseVerify, 0, s_unused);

// 0065A5E0 (stolen 0): the pulse time did not go back, and every pulsing sprite has one colour,
// which a recomputation from the new time matches within one step per channel.
static void __fastcall UpdateVerify(CHelpWindow* self) {
    float before = self->pulseTime_;
    self->Update();
    double wave = (std::cos(static_cast<double>(self->pulseTime_) * 5.0) + 1.0) * 0.5;
    auto channel = [](double v) { return v > 255.0 ? 255 : v < 0.0 ? 0 : static_cast<int>(v); };
    int a = channel(wave * 255.0), r = channel((1.0 - wave) * 80.0 + wave * 255.0), g = channel((1.0 - wave) * 80.0);
    bool ok = !(self->pulseTime_ < before);
    std::uint32_t colour = 0;
    bool first = true;
    for (auto* node = self->pulsing_.First(); ok && node != self->pulsing_.End(); node = node->next) {
        std::uint32_t c = node->data->plainColour_;
        if (first) colour = c;
        first = false;
        auto within1 = [](int x, int y) { return x - y <= 1 && y - x <= 1; };
        ok = c == colour && within1(static_cast<int>(c >> 24), a) && within1(static_cast<int>(c >> 16 & 0xFF), r) &&
             within1(static_cast<int>(c >> 8 & 0xFF), g) && (c & 0xFF) == (c >> 8 & 0xFF);
    }
    ei::verify::Check("CHelpWindow::Update", true, ok, "time %g, colour %08X (expected about %02X%02X%02X%02X)",
                      static_cast<double>(self->pulseTime_), colour, a, r, g, g);
}
EI_VERIFY(0x0065A5E0, UpdateVerify, 0, s_unused);

// 00659190 / 006592C0 (stolen 0): the page moved by at most one and stays within the pages.
static void __fastcall LeftDownVerify(void* input, void*, int buttons, int x, int y) {
    CHelpWindow* self = WindowFromInput<CHelpWindow>(input);
    int before = self->page_;
    self->OnLeftDown(buttons, x, y);
    bool ok = PageInRange(self) && self->page_ - before <= 1 && before - self->page_ <= 1;
    ei::verify::Check("CHelpWindow::OnLeftDown", true, ok, "page %d -> %d of %d", before, self->page_, self->lines_.size);
}
EI_VERIFY(0x00659190, LeftDownVerify, 0, s_unused);

static void __fastcall KeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    CHelpWindow* self = WindowFromInput<CHelpWindow>(input);
    int before = self->page_;
    self->OnKeyDown(key, repeat, flags);
    bool ok = PageInRange(self) && (self->page_ == before || (key == VK_RETURN && self->page_ == before + 1));
    ei::verify::Check("CHelpWindow::OnKeyDown", true, ok, "key %d: page %d -> %d of %d", key, before, self->page_,
                      self->lines_.size);
}
EI_VERIFY(0x006592C0, KeyDownVerify, 0, s_unused);
