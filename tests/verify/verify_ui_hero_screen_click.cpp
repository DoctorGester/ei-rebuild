// Checks for the multiplayer hero screen's left button (ui_hero_screen_click.cpp).
//
// The handler opens windows, changes modes and plays sounds, so ours runs alone; the wrapper
// checks the visible result of the region clicked: the rotate arrows (rotation and mouse
// capture), the mode changes (10 -> 9, 9 -> 10 with the turn reset, 11 -> 10), the inventory
// (the camp screen on top, opened from the hero screen), the voice row (the selected voice) and
// the equipment taken back on the statistics page.
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/unit.h"

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

// Rename (00618060), ours only: with a hero selected the edit window ends up editing (+0x179) and
// enabled; without one nothing changes. Region 6 of the list (not in a scenario yet).
static void __fastcall HeroRenameVerify(CMpHeroScreen* self) {
    bool valid = self->heroIndex_ >= 0 && self->heroIndex_ < HeroCount();
    std::uint8_t editing = self->renameEdit_[0x179];
    self->Rename();
    bool ok = valid ? self->renameEdit_[0x179] != 0 : self->renameEdit_[0x179] == editing;
    ei::verify::Check("CMpHeroScreen::Rename", true, ok, "hero %d, editing %d", self->heroIndex_,
                      self->renameEdit_[0x179]);
}
EI_VERIFY(0x00618060, HeroRenameVerify, 0, s_unused);

// The voice (00619150), ours only (it plays a sound): the sound handle is -1 or a handle the
// interface gave. Reached by the new hero's voice rows and Up / Down there (ui_hero_screen).
static void __fastcall HeroPlayVoiceVerify(CMpHeroScreen* self) {
    self->PlayVoice();
    ei::verify::Check("CMpHeroScreen::PlayVoice", true, self->voiceSound_ >= -1, "sound %d", self->voiceSound_);
}
EI_VERIFY(0x00619150, HeroPlayVoiceVerify, 0, s_unused);

// Continue (006183B0), ours only: the window on top is the connection screen (vtable 00741CD0)
// with a hero selected, else another window (the message box). Enter or region 1 of the list.
static void __fastcall HeroContinueVerify(CMpHeroScreen* self) {
    bool valid = self->heroIndex_ >= 0 && self->heroIndex_ < HeroCount();
    self->Continue();
    CUiWindow* top = self->window.interface_->GetActiveWindow();
    bool connect = top && top->vtable_ == CMpConnectScreen::Vtable();
    ei::verify::Check("CMpHeroScreen::Continue", valid, connect, "top window %p", static_cast<void*>(top));
}
EI_VERIFY(0x006183B0, HeroContinueVerify, 0, s_unused);

// 00618850 (slot 47), ours only: with a preview figure, its scale is the hero's three size values
// (+0x724..+0x72C). The new hero's -/+ buttons (ui_hero_stats) and the mode changes.
static void __fastcall HeroRefreshVerify(CMpHeroScreen* self) {
    self->RefreshHero();
    if (!self->previewModel_) return;
    auto* hero = reinterpret_cast<PartyUnitRecord*>(self->Hero());
    auto* figure = reinterpret_cast<CObject3D*>(self->previewModel_);
    ei::verify::Check("CMpHeroScreen::RefreshHero", 0, std::memcmp(figure->scale, &hero->unknown_724, 12),
                      "scale %g / %g", static_cast<double>(figure->scale[0]), static_cast<double>(hero->unknown_724));
}
EI_VERIFY(0x00618850, HeroRefreshVerify, 0, s_unused);

// 006186F0, ours only: either the mode is now 11 and the hero carries the typed name, or nothing
// changed but the sound. Region 1 / Enter on the new hero (not in a scenario: ui_hero_stats leaves
// with Esc).
static void __fastcall HeroCreateVerify(CMpHeroScreen* self) {
    int mode = self->mode_;
    self->CreateHero();
    const char* typed = reinterpret_cast<CUiEditField*>(self->nameEdit_)->text_;
    const char* given = reinterpret_cast<PartyUnitRecord*>(self->Hero())->texts[0];
    bool ok = self->mode_ == mode || (self->mode_ == 11 && std::strcmp(typed, given) == 0);
    ei::verify::Check("CMpHeroScreen::CreateHero", true, ok, "mode %d -> %d", mode, self->mode_);
}
EI_VERIFY(0x006186F0, HeroCreateVerify, 0, s_unused);
// --- 0061A350 / 00617E40 / 00615940: ours only ------------------------------------------------------
// The record's file name equals the source's after the assignment; a delete with a selection
// leaves one record fewer, the selection within the list (or -1 for an empty one) and the top row
// in range; the message box's answer ends in mode 9 (00615D30 by address). ui_hero_delete.

namespace {
std::uint32_t MpHeroTotal() {
    return static_cast<std::uint32_t>(static_cast<int>(MpHeroes().end - MpHeroes().begin) / kMpHeroSize);
}
} // namespace

static MpHeroRecord* __fastcall HeroAssignVerify(MpHeroRecord* self, void*, const MpHeroRecord* other) {
    MpHeroRecord* result = self->Assign(other);
    ei::verify::Check("MpHeroRecord::Assign", true, result == self && std::strcmp(self->file, other->file) == 0, "");
    return result;
}
EI_VERIFY(0x0061A350, HeroAssignVerify, 0, s_unused);

static void __fastcall HeroDeleteVerify(CMpHeroScreen* self, void*) {
    std::uint32_t before = MpHeroTotal();
    int index = self->heroIndex_;
    bool selected = index >= 0 && static_cast<std::uint32_t>(index) < before;
    self->DeleteHero();
    std::uint32_t after = MpHeroTotal();
    bool ok = selected ? after + 1 == before && self->heroIndex_ < static_cast<int>(after) && self->heroIndex_ >= -1 &&
                             self->heroTop_ >= 0
                       : after == before;
    ei::verify::Check("CMpHeroScreen::DeleteHero", true, ok, "count %u -> %u index %d -> %d top %d", before, after, index,
                      self->heroIndex_, self->heroTop_);
}
EI_VERIFY(0x00617E40, HeroDeleteVerify, 0, s_unused);

static void __fastcall HeroMessageVerify(CMpHeroScreen* self, void*, int result) {
    self->OnMessageResult(result);
    ei::verify::Check("CMpHeroScreen::OnMessageResult", 9, self->mode_, "result %X", result);
}
EI_VERIFY(0x00615940, HeroMessageVerify, 0, s_unused);
