// CInterface3D's per-frame update (game.exe 005F57C0). See docs/notes/ui.md ("Interface base").
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
#include "originals_engine.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_core.h"
#include "ui_cursor.h"
#include "world.h"
#include "world_glue.h"
#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {

// The value a window leaves pending that ends the game session (the message boxes' "yes").
constexpr std::uint32_t kLeaveGame = 0x990001;

// Not rewritten: the mouse cursor's SetShape (005E5200 on 007C7BD8), the game session's
// (007AFE20) Disconnect (0057ECA0) and LeaveGame (00581660), the client's server flag (00578D10),
// a surface's contents saved (005FD7A0) and the message box constructor (00634650: interface,
// caption and text keys, the value it leaves pending, two flags).
void SetCursorShape(int shape) { TheCursor()->ChangeShape(shape); }

// 00458220: dispatches the window messages waiting in the queue.

void ReleaseBackdrop(CUiSurface* surface) {
    if (!surface) return;
    surface->ReleaseContents();
    gamecrt::operator_delete(surface);
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
        if (stackBegin_ != stackEnd_) stackEnd_[-1]->Input()->Deactivate();
        if (stackBegin_ != stackEnd_) {
            stackEnd_[-1]->CloseWindow();
            if (CUiWindow* top = stackEnd_[-1]) top->DeleteWindow();
            --stackEnd_;
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
        if (stackEnd_ - stackBegin_ == 1) {
            ReleaseBackdrop(backdrop_);
            backdrop_ = nullptr;
            if (block5C_) gamecrt::operator_delete(block5C_);
            block5C_ = nullptr;
            backdropMode_ = 0;
            if (!IsMultiplayerClient()) GameSetServerPaused(serverFlag_);
            serverFlag_ = 0;
        }
        if (stackBegin_ != stackEnd_) {
            stackEnd_[-1]->VirtualShow(1);
            stackEnd_[-1]->VirtualMessageResult(static_cast<int>(pendingValue_));
        }
        if (pendingValue_ == kLeaveGame) {
            closeAll_ = true;
            TheGameFlow()->DropSessions();
            TheGameFlow()->CloseSessions();
        }
        mouseCapture_ = nullptr;
        SetCursorShape(0);
    }
    if (pollTop_ && stackBegin_ != stackEnd_) pollTop_ = !stackEnd_[-1]->VirtualReadyToStopPolling();
    if (closeAll_) {
        closeAll_ = false;
        SetCursorShape(3);
        while (stackBegin_ != stackEnd_) {
            stackEnd_[-1]->Input()->Deactivate();
            stackEnd_[-1]->CloseWindow();
            if (CUiWindow* top = stackEnd_[-1]) top->DeleteWindow();
            --stackEnd_;
        }
        ResetInput();
        app::PumpMessages();
        ei::GameData<bool>(0x00784750) = false;
    }
    if (queuedWindow_) {
        Activate(queuedWindow_);
        queuedWindow_ = nullptr;
    }
    mouseX_ = ei::GameData<int>(0x007C7C50);
    mouseY_ = ei::GameData<int>(0x007C7C54);
    if (disconnected_ && IsMultiplayerClient() && net::GameClient()->joinState_ != 2) {
        disconnected_ = false;
        ei::Log("interface: the network client is not joined (state %u): the disconnect box",   // not the game's (N0c)
                static_cast<unsigned>(net::GameClient()->joinState_));
        void* memory = gamecrt::operator_new(0x180);
        void* box = memory ? static_cast<CMessageBox*>(memory)->Construct(this, "disconnect", "disconnect_msg", 0x990000, 1, 1)
                           : nullptr;
        PushWindow(static_cast<CUiWindow*>(box), 1, 1);
    } else if (stackBegin_ != stackEnd_) {
        stackEnd_[-1]->VirtualUpdate();
    }
    root_->VirtualUpdate();
}
// ---- String events (005F8070) ------------------------------------------------------------------

namespace {

// 00461360 (ECX text). A number from the text (its characters' upper-case codes mixed).
// The tutorial queue (+0x84, an STLport list of CStrings): end() (005F97D0) and insert(position,
// value) (005F97E0).
// Rebirth's game over message: the menu object at 007AFEA5 (00578920 with 1, then 00578940), the
// message window (0x180 bytes, constructor 00634650: interface, title, text, 0x20000, 3, 1) and
// the interface's ShowWindow (005F7350: window, 1, 1).

int Compare(const mfc::CString& word, const char* text) {
    return gamecrt::mbscmp(static_cast<const char*>(word),
                           text);
}

} // namespace

void CInterface3D::OnStringEvent(const char* event) {
    if (!event) return;
    console::CStringArray words;
    mfc::CString unused;
    mfc::CString text(event);
    text::SplitWords(text, &words, ' ');
    words.data[0].MakeLower();
    // Rebirth's NewStringEvents (context hook at 005F80EF): "gameover [title [text]]" plays
    // buttons\gameover.wav and shows a message window (the texts default to the keys game_over and
    // game_over_msg).
    if (ei::rebirth::Enabled(ei::rebirth::Patch::NewStringEvents) && std::strcmp(words.data[0], "gameover") == 0) {
        GameStopSounds(1);
        GameStopAmbience();
        PlaySound("buttons\\gameover.wav", 0);
        const char* title = words.size > 1 ? static_cast<const char*>(words.data[1]) : "game_over";
        const char* message = words.size > 2 ? static_cast<const char*>(words.data[2]) : "game_over_msg";
        void* memory = gamecrt::operator_new(0x180);
        void* window = memory ? static_cast<CMessageBox*>(memory)->Construct(this, title, message, 0x20000, 3, 1) : nullptr;
        PushWindow(static_cast<CUiWindow*>(window), 1, 1);
        return;
    }
    auto top = [this]() { return stackEnd_[-1]; };
    if (Compare(words.data[0], "say") == 0 || Compare(words.data[0], "say_block") == 0) {
        // "say" / "say_block" text [key]: the top window shows the text.
        if (words.size < 2) return;
        int number = words.size > 2 ? TextKeyFromName(words.data[2]) : 0;
        int kind = Compare(words.data[0], "say_block") != 0 ? 0x2A : 0x2B;
        if (stackBegin_ != stackEnd_)
            top()->VirtualSpeech(number, static_cast<unsigned>(kind), const_cast<char*>(static_cast<const char*>(words.data[1])));
    } else if (Compare(words.data[0], "tutorial") == 0) {   // queued
        if (words.size < 2) return;
        stlp::list<mfc::CString>::Insert(messages_.End(), words.data[1]);
    } else if (Compare(words.data[0], "briefing") == 0) {
        if (words.size < 2 || stackBegin_ == stackEnd_) return;
        top()->VirtualShowBriefing(static_cast<const char*>(words.data[1]));
    } else if (Compare(words.data[0], "autosave") == 0) {
        pollTop_ = true;
    } else if (Compare(words.data[0], "endofgame") == 0) {
        if (stackBegin_ != stackEnd_) top()->VirtualGameOver(1);
    }
}

EI_REWRITTEN(0x005F57C0, CInterface3D::Update);
EI_REPLACE_METHOD(0x005F8070, CInterface3D::OnStringEvent);
