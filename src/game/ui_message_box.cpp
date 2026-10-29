// The message box's and the difficulty question's input handlers (MessageWindow / DifficultyBox,
// ui.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "ui_screens.h"
#include "asset_text.h"
#include "app_helpers.h"
#include "world_glue.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include "bink_video.h"

#include <cstring>

namespace {

// The sound system is handed the game's own string pointers.
const char* OkSound() { return "buttons\\messbox\\ok.wav"; }
const char* CancelSound() { return "buttons\\messbox\\cancel.wav"; }
const char* ExitMovie() { return "Movies\\Exit.bik"; }

// The result of the exit question: OK plays the exit movie and quits.
constexpr std::uint32_t kExitResult = 0xFFFF0000;
constexpr std::uint32_t kEditField = 4;
// The callers' buffer for the edit field's text: every box with one is the connection screen's (its address and
// password boxes, MpConnectScreen::boxText).
constexpr std::size_t kEditTextSize = 0x80;
constexpr std::uint32_t kAnswerOk = 1;
constexpr std::uint32_t kAnswerCancel = 2;

void PlayExitMovieAndQuit() {
    {
        std::string path = g_discFolder + ExitMovie();
        PlayMovie(path, 0, 0, 0, 1, 1);
    }
    app::QuitGame(0);
}

} // namespace

// --- MessageWindow ---------------------------------------------------------------------------------

void MessageWindow::CopyEditText() {
    // The text is copied through a temporary string (strcpy into the caller's buffer).
    // The field's 50 characters can take up to 200 bytes in UTF-8; cut to the buffer at a character's end.
    text::CopyFitting(editText, kEditTextSize, edit->text);
}

void MessageWindow::LeftDown(int buttons, int x, int y) {
    UiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    if (region == 0) {
        if (result == kExitResult) {
            PlayExitMovieAndQuit();
            return;
        }
        ui->PlaySound(OkSound());
        if (flags & kEditField) CopyEditText();
    } else if (region == 1) {
        ui->PlaySound(CancelSound());
    } else if (region < 0) {
        return;
    }
    ui->SetPendingValue(result | static_cast<std::uint32_t>(region + 1));
}

void MessageWindow::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (handled) return;
    if (key == VK_RETURN) {
        if (result == kExitResult) {
            PlayExitMovieAndQuit();
        } else {
            if (flags & kEditField) CopyEditText();
            ui->PlaySound(OkSound());
            ui->SetPendingValue(result | kAnswerOk);
        }
    } else if (key == VK_ESCAPE) {
        ui->PlaySound(CancelSound());
        ui->SetPendingValue(result | kAnswerCancel);
    } else {
        return;
    }
    handled = true;
}

// --- DifficultyBox ------------------------------------------------------------------------------

void DifficultyBox::LeftDown(int, int x, int y) {
    switch (RegionAt(x, y)) {
    case 0:
        options::g_options.difficulty = easy;
        ui->PlaySound(OkSound());
        ui->SetPendingValue(result | kAnswerOk);
        break;
    case 1:
        ui->PlaySound(CancelSound());
        ui->SetPendingValue(result | kAnswerCancel);
        break;
    case 2:
        EasyBox()->textureOffsetU = kCheckedOffset;
        HardBox()->textureOffsetU = 0.0f;
        easy = 1;
        break;
    case 3:
        HardBox()->textureOffsetU = kCheckedOffset;
        EasyBox()->textureOffsetU = 0.0f;
        easy = 0;
        break;
    default:
        break;
    }
}

void DifficultyBox::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (handled) return;
    if (key == VK_RETURN) {
        options::g_options.difficulty = easy;
        ui->PlaySound(OkSound());
        ui->SetPendingValue(result | kAnswerOk);
    } else if (key == VK_ESCAPE) {
        ui->PlaySound(CancelSound());
        ui->SetPendingValue(result | kAnswerCancel);
    } else {
        return;
    }
    handled = true;
}
