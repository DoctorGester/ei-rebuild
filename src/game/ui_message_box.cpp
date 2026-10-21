// The message box's and the difficulty question's input handlers (CMessageBox / CDifficultyBox,
// ui.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "ui_screens.h"
#include "app_helpers.h"
#include "world_glue.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"
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
constexpr std::uint32_t kAnswerOk = 1;
constexpr std::uint32_t kAnswerCancel = 2;

void PlayExitMovieAndQuit() {
    {
        mfc::CString path = *GameBasePathString() + ExitMovie();
        PlayMovie(path, 0, 0, 0, 1, 1, 1);
    }
    app::QuitGame(0);
}

} // namespace

// --- CMessageBox ---------------------------------------------------------------------------------

void CMessageBox::CopyEditText() {
    // The original copies the text through a temporary CString (strcpy into the caller's buffer).
    const char* text = edit_->text_;
    std::memcpy(editText_, text, std::strlen(text) + 1);
}

void CMessageBox::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->OnLeftDown(buttons, x, y);
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
    InputOf(this)->OnKeyDown(key, repeat, flags);
    if (inputHandled_) return;
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
    inputHandled_ = true;
}

namespace {
} // namespace
EI_REPLACE_METHOD(0x00634D20, InputForward<&CMessageBox::OnLButtonDown>::Call);
EI_REPLACE_METHOD(0x00634E60, InputForward<&CMessageBox::OnKeyDown>::Call);

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
    InputOf(this)->OnKeyDown(key, repeat, flags);
    if (inputHandled_) return;
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
    inputHandled_ = true;
}

namespace {
} // namespace
EI_REPLACE_METHOD(0x00633DD0, InputForward<&CDifficultyBox::OnLButtonDown>::Call);
EI_REPLACE_METHOD(0x00633EC0, InputForward<&CDifficultyBox::OnKeyDown>::Call);
