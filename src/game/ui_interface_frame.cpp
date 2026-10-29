// CInterface3D's per-frame update. See docs/notes/ui.md ("Interface base").
#include "app.h"
#include "object_tables.h"
#include "text_split.h"
#include "ui.h"

#include "console.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"

#include "ei/rebirth.h"
#include "net_classes.h"

#include <iterator>
#include <list>
#include <vector>
#include "ui_cursor.h"
#include "world.h"
#include "world_glue.h"
#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// The value a window leaves pending that ends the game session (the message boxes' "yes").
constexpr std::uint32_t kLeaveGame = 0x990001;

} // namespace

void CInterface3D::Update() {
    // A pending value closes the top window: it is cancelled, deactivated and deleted, the
    // backdrop changes when asked to, the last window closing drops the backdrop and hands the
    // server flag to the world server (single player), and the window below gets the value.
    while (pendingFlag_) {
        pendingFlag_ = false;
        g_cursor.SetShape(3);
        ctrlDown_ = false;
        altDown_ = false;
        shiftDown_ = 0;
        rightDown_ = false;
        doubleClick_ = false;
        if (!stack_.empty()) stack_.back()->Input()->Deactivate();
        if (!stack_.empty()) {
            ScreenOf(stack_.back())->CloseScreen();
            delete stack_.back();
            stack_.pop_back();
        }
        app::PumpMessages();
        if (backdropMode_ == 4) {
            delete backdrop_;
            CUiSurface* next = secondBackdrop_;
            secondBackdrop_ = nullptr;
            backdrop_ = next;
            backdropMode_ = next ? 2 : 0;
            if (next) next->SwapContents(true);
        }
        if (std::ssize(stack_) == 1) {
            delete backdrop_;
            backdrop_ = nullptr;
            screenCopy_ = {};
            backdropMode_ = 0;
            if (!IsMultiplayerClient()) GameSetServerPaused(serverFlag_);
            serverFlag_ = 0;
        }
        if (!stack_.empty()) {
            stack_.back()->Show(true);
            ScreenOf(stack_.back())->MessageResult(static_cast<int>(pendingValue_));
        }
        if (pendingValue_ == kLeaveGame) {
            closeAll_ = true;
            g_gameFlow.DropSessions();
            g_gameFlow.CloseSessions();
        }
        mouseCapture_ = nullptr;
        g_cursor.SetShape(0);
    }
    if (pollTop_ && !stack_.empty()) pollTop_ = !ScreenOf(stack_.back())->ReadyToStopPolling();
    if (closeAll_) {
        closeAll_ = false;
        g_cursor.SetShape(3);
        while (!stack_.empty()) {
            stack_.back()->Input()->Deactivate();
            ScreenOf(stack_.back())->CloseScreen();
            delete stack_.back();
            stack_.pop_back();
        }
        ResetInput();
        app::PumpMessages();
        app::g_interfaceHasScreen = 0;
    }
    if (queuedWindow_) {
        Activate(queuedWindow_);
        queuedWindow_ = nullptr;
    }
    mouseX_ = g_cursor.position_.x;
    mouseY_ = g_cursor.position_.y;
    if (disconnected_ && IsMultiplayerClient() && net::g_gameClient->joinState_ != 2) {
        disconnected_ = false;
        ei::Log("interface: the network client is not joined (state %u): the disconnect box",   // not the game's
                static_cast<unsigned>(net::g_gameClient->joinState_));
        CMessageBox* box = new CMessageBox(this, "disconnect", "disconnect_msg", 0x990000, 1, 1);
        PushWindow(box, true, true);
    } else if (!stack_.empty()) {
        stack_.back()->Update();
    }
    root_->Update();
}
// ---- String events ------------------------------------------------------------------

void CInterface3D::OnStringEvent(std::string_view event) {
    std::vector<std::string> words;
    std::string text(event);
    text::SplitWords(text, &words, ' ');
    str::MakeLower(words[0]);
    // Rebirth's NewStringEvents: "gameover [title [text]]" plays
    // buttons\gameover.wav and shows a message window (the texts default to the keys game_over and
    // game_over_msg).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::NewStringEvents) && str::Compare(words[0], "gameover") == 0) {
        GameStopSounds(1);
        GameStopAmbience();
        PlaySound("buttons\\gameover.wav");
        const std::string_view title = words.size() > 1 ? std::string_view(words[1]) : "game_over";
        const char* message = words.size() > 2 ? words[2].c_str() : "game_over_msg";
        CMessageBox* window = new CMessageBox(this, title, message, 0x20000, 3, 1);
        PushWindow(window, true, true);
        return;
    }
    auto top = [this]() { return stack_.back(); };
    if (str::Compare(words[0], "say") == 0 || str::Compare(words[0], "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size() < 2) return;
        int number = words.size() > 2 ? TextKeyFromName(words[2]) : 0;
        AckKind kind = str::Compare(words[0], "say_block") != 0 ? AckKind::Scenario : AckKind::ScenarioTalking;
        if (!stack_.empty())
            ScreenOf(top())->Speech(number, kind, const_cast<char*>(words[1].c_str()));
    } else if (str::Compare(words[0], "tutorial") == 0) {   // queued
        if (words.size() < 2) return;
        messages_.insert(messages_.end(), words[1]);
    } else if (str::Compare(words[0], "briefing") == 0) {
        if (words.size() < 2 || stack_.empty()) return;
        ScreenOf(top())->ShowBriefing(words[1]);
    } else if (str::Compare(words[0], "autosave") == 0) {
        pollTop_ = true;
    } else if (str::Compare(words[0], "endofgame") == 0) {
        if (!stack_.empty()) ScreenOf(top())->GameOver(true);
    }
}
