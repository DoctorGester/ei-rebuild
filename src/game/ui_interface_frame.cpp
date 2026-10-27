// CInterface3D's per-frame update. See docs/notes/ui.md ("Interface base").
#include "app.h"
#include "object_tables.h"
#include "text_split.h"
#include "ui.h"

#include "console.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "net_classes.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "ui_cursor.h"
#include "world.h"
#include "world_glue.h"
#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// The value a window leaves pending that ends the game session (the message boxes' "yes").
constexpr std::uint32_t kLeaveGame = 0x990001;

void SetCursorShape(int shape) { TheCursor()->ChangeShape(shape); }

void ReleaseBackdrop(CUiSurface* surface) {
    if (!surface) return;
    delete surface;
}

} // namespace

void CInterface3D::Update() {
    // A pending value closes the top window: it is cancelled, deactivated and deleted, the
    // backdrop changes when asked to, the last window closing drops the backdrop and hands the
    // server flag to the world server (single player), and the window below gets the value.
    while (pendingFlag_) {
        pendingFlag_ = false;
        SetCursorShape(3);
        ctrlDown_ = false;
        altDown_ = false;
        shiftDown_ = 0;
        rightDown_ = false;
        doubleClick_ = false;
        if (stack_.Start() != stack_.Finish()) stack_.Finish()[-1]->Input()->Deactivate();
        if (stack_.Start() != stack_.Finish()) {
            ScreenOf(stack_.Finish()[-1])->CloseScreen();
            delete stack_.Finish()[-1];
            --stack_.Finish();
        }
        app::PumpMessages();
        if (backdropMode_ == 4) {
            ReleaseBackdrop(backdrop_);
            CUiSurface* next = secondBackdrop_;
            secondBackdrop_ = nullptr;
            backdrop_ = next;
            backdropMode_ = next ? 2 : 0;
            if (next) next->SwapContents(1);
        }
        if (stack_.Finish() - stack_.Start() == 1) {
            ReleaseBackdrop(backdrop_);
            backdrop_ = nullptr;
            if (block5C_) gamecrt::operator_delete(block5C_);
            block5C_ = nullptr;
            backdropMode_ = 0;
            if (!IsMultiplayerClient()) GameSetServerPaused(serverFlag_);
            serverFlag_ = 0;
        }
        if (stack_.Start() != stack_.Finish()) {
            stack_.Finish()[-1]->Show(1);
            ScreenOf(stack_.Finish()[-1])->MessageResult(static_cast<int>(pendingValue_));
        }
        if (pendingValue_ == kLeaveGame) {
            closeAll_ = true;
            TheGameFlow()->DropSessions();
            TheGameFlow()->CloseSessions();
        }
        mouseCapture_ = nullptr;
        SetCursorShape(0);
    }
    if (pollTop_ && stack_.Start() != stack_.Finish()) pollTop_ = !ScreenOf(stack_.Finish()[-1])->ReadyToStopPolling();
    if (closeAll_) {
        closeAll_ = false;
        SetCursorShape(3);
        while (stack_.Start() != stack_.Finish()) {
            stack_.Finish()[-1]->Input()->Deactivate();
            ScreenOf(stack_.Finish()[-1])->CloseScreen();
            delete stack_.Finish()[-1];
            --stack_.Finish();
        }
        ResetInput();
        app::PumpMessages();
        app::g_interfaceHasScreen = 0;
    }
    if (queuedWindow_) {
        Activate(queuedWindow_);
        queuedWindow_ = nullptr;
    }
    mouseX_ = MouseScreenX();
    mouseY_ = MouseScreenY();
    if (disconnected_ && IsMultiplayerClient() && net::GameClient()->joinState_ != 2) {
        disconnected_ = false;
        ei::Log("interface: the network client is not joined (state %u): the disconnect box",   // not the game's
                static_cast<unsigned>(net::GameClient()->joinState_));
        CMessageBox* box = new CMessageBox(this, "disconnect", "disconnect_msg", 0x990000, 1, 1);
        PushWindow(box, 1, 1);
    } else if (stack_.Start() != stack_.Finish()) {
        stack_.Finish()[-1]->Update();
    }
    root_->Update();
}
// ---- String events ------------------------------------------------------------------

namespace {

int Compare(const std::string& word, const char* text) {
    return gamecrt::mbscmp(word.c_str(),
                           text);
}

} // namespace

void CInterface3D::OnStringEvent(const char* event) {
    if (!event) return;
    console::CStringArray words;
    std::string unused;
    std::string text = event;
    text::SplitWords(text.c_str(), &words, ' ');
    words.data[0].MakeLower();
    // Rebirth's NewStringEvents: "gameover [title [text]]" plays
    // buttons\gameover.wav and shows a message window (the texts default to the keys game_over and
    // game_over_msg).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::NewStringEvents) && std::strcmp(words.data[0], "gameover") == 0) {
        GameStopSounds(1);
        GameStopAmbience();
        PlaySound("buttons\\gameover.wav", 0);
        const char* title = words.size > 1 ? words.data[1].GetString() : "game_over";
        const char* message = words.size > 2 ? words.data[2].GetString() : "game_over_msg";
        CMessageBox* window = new CMessageBox(this, title, message, 0x20000, 3, 1);
        PushWindow(window, 1, 1);
        return;
    }
    auto top = [this]() { return stack_.Finish()[-1]; };
    if (Compare(words.data[0].GetString(), "say") == 0 || Compare(words.data[0].GetString(), "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size < 2) return;
        int number = words.size > 2 ? TextKeyFromName(words.data[2]) : 0;
        int kind = Compare(words.data[0].GetString(), "say_block") != 0 ? 0x2A : 0x2B;
        if (stack_.Start() != stack_.Finish())
            ScreenOf(top())->Speech(number, static_cast<unsigned>(kind), const_cast<char*>(words.data[1].GetString()));
    } else if (Compare(words.data[0].GetString(), "tutorial") == 0) {   // queued
        if (words.size < 2) return;
        stlp::list<mfc::CString>::Insert(messages_.End(), words.data[1]);
    } else if (Compare(words.data[0].GetString(), "briefing") == 0) {
        if (words.size < 2 || stack_.Start() == stack_.Finish()) return;
        ScreenOf(top())->ShowBriefing(words.data[1].GetString());
    } else if (Compare(words.data[0].GetString(), "autosave") == 0) {
        pollTop_ = true;
    } else if (Compare(words.data[0].GetString(), "endofgame") == 0) {
        if (stack_.Start() != stack_.Finish()) ScreenOf(top())->GameOver(1);
    }
}

EI_REWRITTEN(0x005F57C0, CInterface3D::Update);
EI_REWRITTEN(0x005F8070, CInterface3D::OnStringEvent);
