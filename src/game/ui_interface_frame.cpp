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
    while (pendingFlag) {
        pendingFlag = false;
        g_cursor.SetShape(3);
        ctrlDown = false;
        altDown = false;
        shiftDown = 0;
        rightDown = false;
        doubleClick = false;
        if (!stack.empty()) stack.back()->Input()->Deactivate();
        if (!stack.empty()) {
            ScreenOf(stack.back())->CloseScreen();
            delete stack.back();
            stack.pop_back();
        }
        app::PumpMessages();
        if (backdropMode == 4) {
            delete backdrop;
            CUiSurface* next = secondBackdrop;
            secondBackdrop = nullptr;
            backdrop = next;
            backdropMode = next ? 2 : 0;
            if (next) next->SwapContents(true);
        }
        if (std::ssize(stack) == 1) {
            delete backdrop;
            backdrop = nullptr;
            screenCopy = {};
            backdropMode = 0;
            if (!IsMultiplayerClient()) GameSetServerPaused(serverFlag);
            serverFlag = 0;
        }
        if (!stack.empty()) {
            stack.back()->Show(true);
            ScreenOf(stack.back())->MessageResult(static_cast<int>(pendingValue));
        }
        if (pendingValue == kLeaveGame) {
            closeAll = true;
            g_gameFlow.DropSessions();
            g_gameFlow.CloseSessions();
        }
        mouseCapture = nullptr;
        g_cursor.SetShape(0);
    }
    if (pollTop && !stack.empty()) pollTop = !ScreenOf(stack.back())->ReadyToStopPolling();
    if (closeAll) {
        closeAll = false;
        g_cursor.SetShape(3);
        while (!stack.empty()) {
            stack.back()->Input()->Deactivate();
            ScreenOf(stack.back())->CloseScreen();
            delete stack.back();
            stack.pop_back();
        }
        ResetInput();
        app::PumpMessages();
        app::g_interfaceHasScreen = 0;
    }
    if (queuedWindow) {
        Activate(queuedWindow);
        queuedWindow = nullptr;
    }
    mouseX = g_cursor.position.x;
    mouseY = g_cursor.position.y;
    if (disconnected && IsMultiplayerClient() && net::g_gameClient->joinState != 2) {
        disconnected = false;
        ei::Log("interface: the network client is not joined (state %u): the disconnect box",   // not the game's
                static_cast<unsigned>(net::g_gameClient->joinState));
        CMessageBox* box = new CMessageBox(this, "disconnect", "disconnect_msg", 0x990000, 1, 1);
        PushWindow(box, true, true);
    } else if (!stack.empty()) {
        stack.back()->Update();
    }
    root->Update();
}
// ---- String events ------------------------------------------------------------------

void CInterface3D::OnStringEvent(std::string_view event) {
    std::vector<std::string> words;
    std::string text(event);
    words = text::SplitWords(text, ' ');
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
    auto top = [this]() { return stack.back(); };
    if (str::Compare(words[0], "say") == 0 || str::Compare(words[0], "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size() < 2) return;
        int number = words.size() > 2 ? TextKeyFromName(words[2]) : 0;
        AckKind kind = str::Compare(words[0], "say_block") != 0 ? AckKind::Scenario : AckKind::ScenarioTalking;
        if (!stack.empty())
            ScreenOf(top())->Speech(number, kind, const_cast<char*>(words[1].c_str()));
    } else if (str::Compare(words[0], "tutorial") == 0) {   // queued
        if (words.size() < 2) return;
        messages.insert(messages.end(), words[1]);
    } else if (str::Compare(words[0], "briefing") == 0) {
        if (words.size() < 2 || stack.empty()) return;
        ScreenOf(top())->ShowBriefing(words[1]);
    } else if (str::Compare(words[0], "autosave") == 0) {
        pollTop = true;
    } else if (str::Compare(words[0], "endofgame") == 0) {
        if (!stack.empty()) ScreenOf(top())->GameOver(true);
    }
}
