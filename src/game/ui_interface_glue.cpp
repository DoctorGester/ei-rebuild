// The interface manager's small services to the rest of the game (CInterface3D): interface sounds,
// the console window and debug panel settings, the frame rate surface and quest variable news for
// the top window. See ui.h and docs/notes/ui.md, section 3.
#include "ui.h"
#include "app.h"

#include "console.h"
#include "sound.h"
#include "text_split.h"
#include "ui_native.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

namespace {
constexpr int kInterfacePriority = 1000;
constexpr int kFullVolume = 100;
constexpr int kNoSound = -1;
constexpr int kLayoutWidth = 800;
// The frame rate's surface: (730, 10)-(800, 28), format 5.
constexpr int kFpsLeft = 730, kFpsTop = 10, kFpsRight = 800, kFpsBottom = 28, kFpsFormat = 5;
constexpr int kQuestPart = 2;   // "q.<zone>.<quest>": the quest's name
// The status line: a surface over (0, 280)-(800, 320), the text in green in its top 40 pixels.
constexpr int kStatusTop = 280, kStatusBottom = 320, kStatusFormat = 5;
constexpr int kStatusTextFormat = 2, kStatusTextHeight = 40;
constexpr std::uint32_t kStatusColour = 0xFF00;
} // namespace

int CInterface3D::PlaySound(const char* name, int loop) {
    return TheSoundManager()->Play2D(name, kInterfacePriority, kFullVolume, 0, loop, 0, 0, 0);
}
EI_REWRITTEN(0x005F7E60, CInterface3D::PlaySound);

void CInterface3D::StopSound(int handle) {
    if (handle != kNoSound) TheSoundManager()->StopSound(handle);
}
EI_REWRITTEN(0x005F7E90, CInterface3D::StopSound);

void CInterface3D::ConsoleAddText(const char* text) {
    if (console_) console_->AddText(text);
}
EI_REWRITTEN(0x005F7EB0, CInterface3D::ConsoleAddText);

void CInterface3D::SetConsoleRect(const RECT* rect) {
    if (console_) console_->SetRect(rect);
}
EI_REWRITTEN(0x005F7ED0, CInterface3D::SetConsoleRect);

void CInterface3D::SetConsoleHistory(int lines) {
    if (console_) console_->SetHistory(lines);
}
EI_REWRITTEN(0x005F7EF0, CInterface3D::SetConsoleHistory);

void CInterface3D::SetDebugInfoRect(const RECT* rect) { DebugInfoPanel()->SetRect(rect); }
EI_REWRITTEN(0x005F7F10, CInterface3D::SetDebugInfoRect);

void CInterface3D::ShowFps(int on) {
    if (!static_cast<std::uint8_t>(on)) {
        if (fpsSurface_) {
            delete fpsSurface_;
        }
        fpsSurface_ = nullptr;
        return;
    }
    if (fpsSurface_) return;
    fpsSurface_ = new CUiSurface(this, kFpsLeft, kFpsTop, kFpsRight, kFpsBottom, kFpsFormat, 1);
    ui_native::Adopt(fpsSurface_, "fps");
}
EI_REWRITTEN(0x005F5700, CInterface3D::ShowFps);

void CInterface3D::OnQuestVariable(const char* name, float value) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->QuestVariable(name, value);
    console::CStringArray parts;
    EI_NAN_AUDIT(value);
    if (value != 1.0f) return;
    text::SplitWords(name, &parts, '.');
    if (parts.GetSize() > kQuestPart) savedText_ = parts[kQuestPart];
}
EI_REWRITTEN(0x005F7FC0, CInterface3D::OnQuestVariable);

// --- News for the top window -------------------------------------

namespace {
// A unit's speech without an extra argument.
constexpr char* kNoExtra = nullptr;
} // namespace

void CInterface3D::OnItemsPicked(void* items, int money, int crippled) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->ItemsPicked(items, money, crippled);
}
EI_REWRITTEN(0x005F7F30, CInterface3D::OnItemsPicked);

void CInterface3D::OnUnitJoined(CUnit* unit) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->UnitJoined(unit);
}
EI_REWRITTEN(0x005F7F60, CInterface3D::OnUnitJoined);

void CInterface3D::OnUnitLeft(CUnit* unit) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->UnitLeft(unit);
}
EI_REWRITTEN(0x005F7F80, CInterface3D::OnUnitLeft);

void CInterface3D::OnGameLost() {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->GameOver(0);
}
EI_REWRITTEN(0x005F7FA0, CInterface3D::OnGameLost);

void CInterface3D::OnUnitSpeech(int id, unsigned event) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->Speech(id, event, kNoExtra);
}
EI_REWRITTEN(0x005F82A0, CInterface3D::OnUnitSpeech);

void CInterface3D::OnPlayersChanged(int added) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->PlayersChanged(added);
}
EI_REWRITTEN(0x005F82D0, CInterface3D::OnPlayersChanged);

void CInterface3D::OnChatLine(const char* text, std::uint32_t number) {
    root_->AddLine(text, number);
}
EI_REWRITTEN(0x005F82F0, CInterface3D::OnChatLine);

void CInterface3D::OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Finish()[-1])->ExchangeDone(partyB, partyA, partyC);
}
EI_REWRITTEN(0x005F8310, CInterface3D::OnExchangeDone);

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void CInterface3D::SaveWindowState() {
    if (stack_.Start() != stack_.Finish()) ScreenOf(stack_.Start()[0])->SaveState();
}
EI_REWRITTEN(0x005F86D0, CInterface3D::SaveWindowState);

void CInterface3D::ShowStatusText(const char* text) {
    statusSurface_ = new CUiSurface(this, 0, kStatusTop, kLayoutWidth, kStatusBottom, kStatusFormat, 1);
    if (!statusSurface_) return;
    ui_native::Adopt(statusSurface_, "status");
    UiSurfaceGetDC(statusSurface_);
    statusSurface_->Clear();
    RECT rect = {0, 0, kLayoutWidth, kStatusTextHeight};
    statusSurface_->DrawTextIn(text, &rect, kStatusTextFormat, kStatusColour, 1, 1);   // Rebirth-patched
    UiSurfaceReleaseDC(statusSurface_);
    app::FrameStep(true, true);
    if (statusSurface_) {
        delete statusSurface_;
    }
    statusSurface_ = nullptr;
}
EI_REWRITTEN(0x005F75B0, CInterface3D::ShowStatusText);
