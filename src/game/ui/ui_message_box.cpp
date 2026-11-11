// The message box's and the difficulty question's input handlers (MessageWindow / DifficultyBox,
// ui_message_box.h; built in ui_layouts.cpp). See docs/notes/ui.md, section 4.
#include "game/ui/ui_screens.h"
#include "game/ui/ui_message_box.h"
#include "engine/core/asset_text.h"
#include "game/app/app.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include "game/app/movies.h"
#include "game/app/singletons.h"
#include "game/app/options.h"
#include "platform/platform.h"

#include <cstring>

namespace {

// The sound system is handed the game's own string pointers.
constexpr const char* kExitMovie = "Movies\\Exit.bik";

// The callers' buffer for the edit field's text: every box with one is the connection screen's (its address and
// password boxes, MpConnectScreen::boxText).
constexpr std::size_t kEditTextSize = 0x80;

void PlayExitMovieAndQuit() {
    PlayMovie(g_dataFolder + kExitMovie, false, true, true);
    platform::PostQuit(0);
}

} // namespace

// --- MessageWindow ---------------------------------------------------------------------------------

void MessageWindow::CopyEditText() {
    // The text is copied through a temporary string (strcpy into the caller's buffer).
    // The field's 50 characters can take up to 200 bytes in UTF-8; cut to the buffer at a character's end.
    text::CopyFitting(editText, kEditTextSize, edit->text);
}

void MessageWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void MessageWindow::LeftDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    UiRegion* region = RegionUnder(event.at);
    if (!region) return;
    const bool ok = region == okButton;
    if (ok) {
        if (question == MessageQuestion::Exit) {
            PlayExitMovieAndQuit();
            return;
        }
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        if (buttons == MessageButtons::OkCancelEdit) CopyEditText();
    } else {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    }
    g_interface.SetPendingValue(BoxResult(question, ok ? MessageAnswer::Ok : MessageAnswer::Cancel));
}

void MessageWindow::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    if (event.handled) return;
    if (event.key == platform::Key::Return) {
        if (question == MessageQuestion::Exit) {
            PlayExitMovieAndQuit();
        } else {
            if (buttons == MessageButtons::OkCancelEdit) CopyEditText();
            g_interface.PlayInterfaceSound(kMessageBoxOkSound);
            g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Ok));
        }
    } else if (event.key == platform::Key::Escape) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
    } else {
        return;
    }
    event.handled = true;
}

// --- DifficultyBox ------------------------------------------------------------------------------

void DifficultyBox::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: MessageWindow::OnEvent(event); break;
    }
}

void DifficultyBox::LeftDown(UiEvent& event) {
    UiRegion* region = RegionUnder(event.at);
    if (!region) return;
    if (region == okButton) {
        options::g_options.difficulty = easy;
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Ok));
    } else if (region == cancelButton) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
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

void DifficultyBox::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    if (event.handled) return;
    if (event.key == platform::Key::Return) {
        options::g_options.difficulty = easy;
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Ok));
    } else if (event.key == platform::Key::Escape) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(BoxResult(question, MessageAnswer::Cancel));
    } else {
        return;
    }
    event.handled = true;
}

void MessageWindow::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}
