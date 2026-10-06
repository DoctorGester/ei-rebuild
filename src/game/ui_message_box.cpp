// The message box's and the difficulty question's input handlers (CMessageBox / CDifficultyBox,
// ui.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"

#include <cstring>

namespace {

const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
// The sound system is handed the game's own string pointers.
const char* OkSound() { return GameString(0x00793A88); }       // "buttons\messbox\ok.wav"
const char* CancelSound() { return GameString(0x007937E4); }   // "buttons\messbox\cancel.wav"
const char* ExitMovie() { return GameString(0x0079422C); }     // "Movies\Exit.bik"
// The client glue object the original passes in ECX (not read).
void* Glue() { return reinterpret_cast<void*>(0x007AFEA4); }

// The result of the exit question: OK plays the exit movie and quits.
constexpr std::uint32_t kExitResult = 0xFFFF0000;
constexpr std::uint32_t kEditField = 4;
constexpr std::uint32_t kAnswerOk = 1;
constexpr std::uint32_t kAnswerCancel = 2;

void PlayExitMovieAndQuit() {
    {
        mfc::CString path = *GameDirectoryOriginal(Glue()) + ExitMovie();
        PlayMovieFile(path, 0, 0, 0, 1, 1, 1);
    }
    QuitGame(0);
}

} // namespace

// --- CMessageBox ---------------------------------------------------------------------------------

void CMessageBox::CopyEditText() {
    // The original copies the text through a temporary CString (strcpy into the caller's buffer).
    const char* text = edit_->text_;
    std::memcpy(editText_, text, std::strlen(text) + 1);
}

void CMessageBox::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->BaseLButtonDown(buttons, x, y);
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
    InputOf(this)->BaseKeyDown(key, repeat, flags);
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
void __fastcall MessageBoxLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CMessageBox>(input)->OnLButtonDown(buttons, x, y);
}
void __fastcall MessageBoxKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CMessageBox>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x00634D20, MessageBoxLButtonDown);
EI_REPLACE(0x00634E60, MessageBoxKeyDown);

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
        EasyBox()->uOffset_ = kCheckedOffset;
        HardBox()->uOffset_ = 0.0f;
        easy_ = 1;
        break;
    case 3:
        HardBox()->uOffset_ = kCheckedOffset;
        EasyBox()->uOffset_ = 0.0f;
        easy_ = 0;
        break;
    default:
        break;
    }
}

void CDifficultyBox::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
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
void __fastcall DifficultyLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CDifficultyBox>(input)->OnLButtonDown(buttons, x, y);
}
void __fastcall DifficultyKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CDifficultyBox>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x00633DD0, DifficultyLButtonDown);
EI_REPLACE(0x00633EC0, DifficultyKeyDown);
