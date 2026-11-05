// The message box's and the difficulty question's input handlers (MessageWindow / DifficultyBox,
// ui.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "ui_screens.h"
#include "asset_text.h"
#include "app.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include "bink_video.h"
#include "game_singletons.h"
#include "options.h"
#include "platform/platform.h"

#include <cstring>

namespace {

// The sound system is handed the game's own string pointers.
const char* ExitMovie() { return "Movies\\Exit.bik"; }

// The callers' buffer for the edit field's text: every box with one is the connection screen's (its address and
// password boxes, MpConnectScreen::boxText).
constexpr std::size_t kEditTextSize = 0x80;

void PlayExitMovieAndQuit() {
    {
        std::string path = g_dataFolder + ExitMovie();
        PlayMovie(path, false, true, true);
    }
    platform::PostQuit(0);
}

} // namespace

// --- MessageWindow ---------------------------------------------------------------------------------

void MessageWindow::CopyEditText() {
    // The text is copied through a temporary string (strcpy into the caller's buffer).
    // The field's 50 characters can take up to 200 bytes in UTF-8; cut to the buffer at a character's end.
    text::CopyFitting(editText, kEditTextSize, edit->text);
}

void MessageWindow::LeftDown(Point point) {
    UiContainer::LeftDown(point);
    UiRegion* region = RegionUnder(point);
    if (!region) return;
    const bool ok = region == okButton;
    if (ok) {
        if (question == MessageQuestion::Exit) {
            PlayExitMovieAndQuit();
            return;
        }
        ui->PlayInterfaceSound(kMessageBoxOkSound);
        if (buttons == MessageButtons::OkCancelEdit) CopyEditText();
    } else {
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
    }
    ui->SetPendingValue(BoxResult(question, ok ? MessageAnswer::Ok : MessageAnswer::Cancel));
}

void MessageWindow::KeyDown(platform::Key key, int scan, bool repeat) {
    UiContainer::KeyDown(key, scan, repeat);
    if (handled) return;
    if (key == platform::Key::Return) {
        if (question == MessageQuestion::Exit) {
            PlayExitMovieAndQuit();
        } else {
            if (buttons == MessageButtons::OkCancelEdit) CopyEditText();
            ui->PlayInterfaceSound(kMessageBoxOkSound);
            ui->SetPendingValue(BoxResult(question, MessageAnswer::Ok));
        }
    } else if (key == platform::Key::Escape) {
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
    } else {
        return;
    }
    handled = true;
}

// --- DifficultyBox ------------------------------------------------------------------------------

void DifficultyBox::LeftDown(Point point) {
    UiRegion* region = RegionUnder(point);
    if (!region) return;
    if (region == okButton) {
        options::g_options.difficulty = easy;
        ui->PlayInterfaceSound(kMessageBoxOkSound);
        ui->SetPendingValue(BoxResult(question, MessageAnswer::Ok));
    } else if (region == cancelButton) {
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
    } else if (region == easyArea) {
        easyBox->textureOffsetU = kCheckedOffset;
        hardBox->textureOffsetU = 0.0f;
        easy = 1;
    } else if (region == hardArea) {
        hardBox->textureOffsetU = kCheckedOffset;
        easyBox->textureOffsetU = 0.0f;
        easy = 0;
    }
}

void DifficultyBox::KeyDown(platform::Key key, int scan, bool repeat) {
    UiContainer::KeyDown(key, scan, repeat);
    if (handled) return;
    if (key == platform::Key::Return) {
        options::g_options.difficulty = easy;
        ui->PlayInterfaceSound(kMessageBoxOkSound);
        ui->SetPendingValue(BoxResult(question, MessageAnswer::Ok));
    } else if (key == platform::Key::Escape) {
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
    } else {
        return;
    }
    handled = true;
}

void MessageWindow::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}
