// The message box's and the difficulty question's input handlers (CMessageBox / CDifficultyBox,
// ui.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "ui_screens.h"
#include "asset_text.h"
#include "app_helpers.h"
#include "world_glue.h"
#include "game_strings.h"

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
// password boxes, CMpConnectScreen::boxText_).
constexpr std::size_t kEditTextSize = 0x80;
constexpr std::uint32_t kAnswerOk = 1;
constexpr std::uint32_t kAnswerCancel = 2;

void PlayExitMovieAndQuit() {
    {
        std::string path = *GameBasePathString() + ExitMovie();
        PlayMovie(path, 0, 0, 0, 1, 1, 1);
    }
    app::QuitGame(0);
}

} // namespace

// --- CMessageBox ---------------------------------------------------------------------------------

void CMessageBox::CopyEditText() {
    // The text is copied through a temporary CString (strcpy into the caller's buffer).
    const char* text = edit_->text_.c_str();
    // The field's 50 characters can take up to 200 bytes in UTF-8; cut to the buffer at a character's end.
    text::CopyFitting(editText_, kEditTextSize, text);
}

void CMessageBox::OnLButtonDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    if (region == 0) {
        if (result_ == kExitResult) {
            PlayExitMovieAndQuit();
            return;
        }
        interface_->PlaySound(OkSound(), 0);
        if (flags_ & kEditField) CopyEditText();
    } else if (region == 1) {
        interface_->PlaySound(CancelSound(), 0);
    } else if (region < 0) {
        return;
    }
    interface_->SetPendingValue(result_ | static_cast<std::uint32_t>(region + 1));
}

void CMessageBox::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (handled_) return;
    if (key == VK_RETURN) {
        if (result_ == kExitResult) {
            PlayExitMovieAndQuit();
        } else {
            if (flags_ & kEditField) CopyEditText();
            interface_->PlaySound(OkSound(), 0);
            interface_->SetPendingValue(result_ | kAnswerOk);
        }
    } else if (key == VK_ESCAPE) {
        interface_->PlaySound(CancelSound(), 0);
        interface_->SetPendingValue(result_ | kAnswerCancel);
    } else {
        return;
    }
    handled_ = true;
}

// --- CDifficultyBox ------------------------------------------------------------------------------

void CDifficultyBox::OnLButtonDown(int, int x, int y) {
    switch (RegionAt(x, y)) {
    case 0:
        GameDifficulty() = easy_;
        interface_->PlaySound(OkSound(), 0);
        interface_->SetPendingValue(result_ | kAnswerOk);
        break;
    case 1:
        interface_->PlaySound(CancelSound(), 0);
        interface_->SetPendingValue(result_ | kAnswerCancel);
        break;
    case 2:
        EasyBox()->textureOffsetU_ = kCheckedOffset;
        HardBox()->textureOffsetU_ = 0.0f;
        easy_ = 1;
        break;
    case 3:
        HardBox()->textureOffsetU_ = kCheckedOffset;
        EasyBox()->textureOffsetU_ = 0.0f;
        easy_ = 0;
        break;
    default:
        break;
    }
}

void CDifficultyBox::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (handled_) return;
    if (key == VK_RETURN) {
        GameDifficulty() = easy_;
        interface_->PlaySound(OkSound(), 0);
        interface_->SetPendingValue(result_ | kAnswerOk);
    } else if (key == VK_ESCAPE) {
        interface_->PlaySound(CancelSound(), 0);
        interface_->SetPendingValue(result_ | kAnswerCancel);
    } else {
        return;
    }
    handled_ = true;
}
