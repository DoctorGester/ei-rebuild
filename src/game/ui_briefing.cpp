// The briefing / base screen's main-vtable slots (CBriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state. See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "camera.h"
#include "core_streams.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstring>
#include <new>

namespace {

// The map edge fade (a float the zone screens set while they are open).
float& MapEdgeFade() { return *reinterpret_cast<float*>(0x0079BD34); }
// The save game's stream.
CStream* SaveGameStream() { return reinterpret_cast<CStream*>(0x007AFDF8); }
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
void* TheGame() { return reinterpret_cast<void*>(0x007AFE20); }
const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// The load window the game-over box opens (0x194 bytes, built inline).
void** SaveLoadVtable() { return reinterpret_cast<void**>(0x00741388); }
void** SaveLoadInputVtable() { return reinterpret_cast<void**>(0x00741354); }

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kGameOverResult = 0x20000;
constexpr int kShowChatSlot = 36;

// 006091C0: the chat window shows itself (thiscall, plain RET).
__declspec(naked) void __fastcall ChatWindowShowAt(CUiWindow* /*chat*/) EI_ORIGINAL(0x006091C0)

} // namespace

// --- 00602BB0: slot 26 --------------------------------------------------------------------------

void CBriefingScreen::Close() {
    MapEdgeFade() = savedEdgeFade_;
    MenuSetOriginal(1);
    MenuCallOriginal();
    std::uint32_t& flags = camera::TheCamera()->flags_;
    flags = (flags & ~(CCamera::kBounded | CCamera::kAlternate)) | CCamera::kFree;
}
EI_REPLACE_METHOD(0x00602BB0, CBriefingScreen::Close);

// --- 00606F40: slot 27 --------------------------------------------------------------------------

void CBriefingScreen::OnMessageResult(std::uint32_t result) {
    switch (result) {
    case kLeaveZoneYes:
        GameLeaveThroughExitOriginal(ClientGlue(), nullptr, exit_);
        break;
    case kGameOverLoad: {
        auto* window = static_cast<CSaveLoadWindow*>(gamecrt::operator_new(sizeof(CSaveLoadWindow)));
        if (window) {
            ConstructContainerAt(window, nullptr, interface_);
            window->entriesBegin_ = nullptr;
            window->entriesEnd_ = nullptr;
            window->entriesCapacity_ = nullptr;
            window->isSave_ = false;
            window->sliding_ = false;
            new (static_cast<void*>(&window->name_)) mfc::CString();
            window->vtable_ = SaveLoadVtable();
            window->inputVtable_ = SaveLoadInputVtable();
        }
        // (Like the original, the flags are written again without checking the allocation.)
        window->isSave_ = false;
        window->sliding_ = false;
        InterfacePushWindowOriginal(interface_, nullptr, window, 1, 1);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        interface_->closeAll_ = true;
        GameDisconnectOriginal(TheGame());
        uiorig::LeaveGame();
        break;
    default:
        break;
    }
    if (IsNetworkClientOriginal()) {
        using Fn = void(__thiscall*)(CBriefingScreen*, char);
        reinterpret_cast<Fn>(vtable_[kShowChatSlot])(this, 1);
    }
}

// --- 00603010: slot 30 --------------------------------------------------------------------------

void CBriefingScreen::OnUnitLeft(CUnit* unit) {
    if (unit == leader_) leader_ = nullptr;
    int count = party_.size_;
    int index = 0;
    while (index < count && reinterpret_cast<CUnit*>(party_.data_[index])->id_ != unit->id_) ++index;
    if (index >= count) return;
    int after = count - index - 1;
    if (after != 0) std::memmove(&party_.data_[index], &party_.data_[index + 1], after * sizeof(party_.data_[0]));
    --party_.size_;
}
EI_REPLACE_METHOD(0x00603010, CBriefingScreen::OnUnitLeft);

// --- 00608C70: slot 31 --------------------------------------------------------------------------

void CBriefingScreen::OnGameOver(int) {
    interface_->PlaySound(GameString(0x00793528), 0);   // "buttons\gameover.wav"
    void* memory = gamecrt::operator_new(sizeof(CMessageBox));
    void* box = memory ? MessageBoxConstructOriginal(memory, nullptr, interface_, GameString(0x0079350C),   // "game_over"
                                                     GameString(0x00793518), kGameOverResult, 3, 1)       // "game_over_msg"
                       : nullptr;
    InterfacePushWindowOriginal(interface_, nullptr, box, 1, 1);
}
EI_REPLACE_METHOD(0x00608C70, CBriefingScreen::OnGameOver);

// --- 006030C0: slot 34 --------------------------------------------------------------------------

void CBriefingScreen::OnBriefing(const char* name) {
    topic_ = name;
    BriefingStartDialogueOriginal(this, nullptr, 1, 1);
}
EI_REPLACE_METHOD(0x006030C0, CBriefingScreen::OnBriefing);

// --- 00603090: slot 36 --------------------------------------------------------------------------

void CBriefingScreen::ShowChat(char show) {
    if (IsNetworkClientOriginal() && show && chat_) ChatWindowShowAt(chat_);
}
EI_REPLACE_METHOD(0x00603090, CBriefingScreen::ShowChat);

// --- 00602490: slot 40 --------------------------------------------------------------------------

void CBriefingScreen::SaveState() {
    int zero = 0;
    SaveGameStream()->Write(&zero, 4);
}
EI_REPLACE_METHOD(0x00602490, CBriefingScreen::SaveState);
EI_REPLACE_METHOD(0x00606F40, CBriefingScreen::OnMessageResult);
