// Checks for the multiplayer hero screen's left button (ui_hero_screen_click.cpp).
//
// The handler opens windows, changes modes and plays sounds, so ours runs alone; the wrapper
// checks the visible result of the region clicked: the rotate arrows (rotation and mouse
// capture), the mode changes (10 -> 9, 9 -> 10 with the turn reset, 11 -> 10), the inventory
// (the camp screen on top, opened from the hero screen), the voice row (the selected voice) and
// the equipment taken back on the statistics page.
#include "verify.h"

#include "../../src/game/ui_screens.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused;

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}

char* FirstOf(char* hero, int dataOffset) {
    return Get<int>(hero, dataOffset + 4) > 0 ? *Get<char**>(hero, dataOffset) : nullptr;
}

int HeroCount() {
    const RecordTable& heroes = MpHeroes();
    return static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize;
}

} // namespace

static void __fastcall HeroButtonDownVerify(void* input, void*, int flags, int x, int y) {
    auto* self = WindowFromInput<CMpHeroScreen>(input);
    CInterface3D* ui = self->window.interface_;
    bool editing = self->renameEdit_[0x179] != 0;
    int mode = self->mode_;
    int region = self->window.RegionAt(x, y);
    void* capture = UiCapture(ui);
    int heroIndex = self->heroIndex_;
    int voiceTop = self->voiceTop_;

    self->OnLButtonDown(flags, x, y);

    const char* name = "CMpHeroScreen::OnLButtonDown";
    if (editing) {
        ei::verify::Check(name, true, self->mode_ == mode && UiCapture(ui) == capture, "editing: mode %d -> %d", mode,
                          self->mode_);
        return;
    }
    if (UiHandled(self)) return;   // the base handler (a child) took it
    if ((mode == 9 || mode == 10) && (region == 2 || region == 3)) {
        ei::verify::Check(name, true, self->rotating_ == region - 1 && UiCapture(ui) == CaptureValue(input),
                          "mode %d arrow %d: rotating %d", mode, region, self->rotating_);
    } else if (mode == 10 && region == 0) {
        ei::verify::Check(name, 9, self->mode_, "back from a new hero");
    } else if (mode == 9 && region == 5) {
        ei::verify::Check(name, true, self->mode_ == 10 && self->turn_ == 0.0f, "new hero: mode %d", self->mode_);
    } else if (mode == 11 && region == 0) {
        ei::verify::Check(name, 10, self->mode_, "back from the statistics");
    } else if (mode == 9 && region == 8 && heroIndex >= 0 && heroIndex < HeroCount()) {
        CUiWindow* top = ui->GetActiveWindow();
        bool camp = top && Get<std::uint32_t>(top, 0) == 0x0074091C && Get<std::uint8_t>(top, 0x194) == 1;
        ei::verify::Check(name, true, camp, "inventory: top window %p", static_cast<void*>(top));
    } else if (mode == 10 && region >= 9 && region < 20) {
        int index = voiceTop + region - 9;
        auto** voices = reinterpret_cast<std::uint8_t**>(self->voices_);
        int count = static_cast<int>((voices[1] - voices[0]) >> 3);
        if (index >= 0 && index < count) ei::verify::Check(name, index, self->voiceIndex_, "voice row %d", region);
    } else if (mode == 11 && region == 4) {
        char* hero = self->Hero();
        bool same = self->weapon_ == FirstOf(hero, 0x730) && self->armor_ == FirstOf(hero, 0x740) &&
                    self->spellContainer_ == FirstOf(hero, 0x760);
        ei::verify::Check(name, true, same, "equipment taken back");
    }
}
EI_VERIFY(0x00617050, HeroButtonDownVerify, 0, s_unused);

// The keys (00618A10), ours only: Enter and Esc are always used; Esc moves 10 -> 9 and 11 -> 10 and
// closes the screen from the list (pending value 0); the paging keys keep the selection in its list
// (the hero list in mode 9, the voices in mode 10). Scenario: ui_screens (Up / Down / Esc on the list).
static void __fastcall HeroKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CMpHeroScreen>(input);
    CInterface3D* ui = self->window.interface_;
    int mode = self->mode_;
    bool renaming = self->renameEdit_[0x179] != 0;
    self->OnKeyDown(key, repeat, flags);
    bool ok = true;
    if (key == VK_RETURN || key == VK_ESCAPE) ok = UiHandled(self);
    if (key == VK_ESCAPE && !renaming) {
        if (mode == 10)
            ok = ok && self->mode_ == 9;
        else if (mode == 11)
            ok = ok && self->mode_ == 10;
        else if (mode == 9)
            ok = ok && ui->pendingFlag_ && ui->pendingValue_ == 0;
    }
    bool paging = key == VK_PRIOR || key == VK_NEXT || key == VK_UP || key == VK_DOWN;
    int heroes = HeroCount() > 0 ? HeroCount() : 1;
    if (paging && mode == 9) ok = UiHandled(self) && self->heroIndex_ >= -1 && self->heroIndex_ < heroes;
    if (paging && mode == 10) {
        auto** voices = reinterpret_cast<std::uint8_t**>(self->voices_);
        int count = static_cast<int>((voices[1] - voices[0]) >> 3);
        ok = UiHandled(self) && self->voiceIndex_ >= -1 && self->voiceIndex_ < (count > 0 ? count : 1);
    }
    ei::verify::Check("CMpHeroScreen::OnKeyDown", true, ok, "key %X, mode %d -> %d, hero %d, voice %d", key, mode,
                      self->mode_, self->heroIndex_, self->voiceIndex_);
}
EI_VERIFY(0x00618A10, HeroKeyDownVerify, 0, s_unused);
