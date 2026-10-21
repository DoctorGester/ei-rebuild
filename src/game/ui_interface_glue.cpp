// The interface manager's small services to the rest of the game (CInterface3D): interface sounds,
// the console window and debug panel settings, the frame rate surface and quest variable news for
// the top window (005F5700, 005F7E60-005F7FC0). See ui.h and docs/notes/ui.md, section 3.
#include "ui.h"
#include "app.h"

#include "console.h"
#include "sound.h"
#include "text_split.h"
#include "ui_native.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_ui.h"
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

void CInterface3D::StopSound(int handle) {
    if (handle != kNoSound) TheSoundManager()->StopSound(handle);
}

void CInterface3D::ConsoleAddText(const char* text) {
    if (console_) console_->AddText(text);
}

void CInterface3D::SetConsoleRect(const int* rect) {
    if (console_) console_->SetRect(rect);
}

void CInterface3D::SetConsoleHistory(int lines) {
    if (console_) console_->SetHistory(lines);
}

void CInterface3D::SetDebugInfoRect(const RECT* rect) { DebugInfoPanel()->SetRect(rect); }

void CInterface3D::ShowFps(int on) {
    if (!static_cast<std::uint8_t>(on)) {
        if (fpsSurface_) {
            fpsSurface_->ReleaseContents();
            gamecrt::operator_delete(fpsSurface_);
        }
        fpsSurface_ = nullptr;
        return;
    }
    if (fpsSurface_) return;
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    fpsSurface_ = memory ? static_cast<CUiSurface*>(memory)->Construct(this, kFpsLeft, kFpsTop, kFpsRight, kFpsBottom,
                                                                      kFpsFormat, 1)
                         : nullptr;
    ui_native::Adopt(fpsSurface_, "fps");
}
EI_REWRITTEN(0x005F5700, CInterface3D::ShowFps);

void CInterface3D::OnQuestVariable(const char* name, float value) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->OnQuestVariable(name, value);
    console::CStringArray parts;
    EI_NAN_AUDIT(value);
    if (value != 1.0f) return;
    text::SplitWords(name, &parts, '.');
    if (parts.GetSize() > kQuestPart) savedText_ = parts[kQuestPart];
}
EI_REWRITTEN(0x005F7FC0, CInterface3D::OnQuestVariable);

// --- 005F7F30-005F8310, 005F86D0: news for the top window -------------------------------------

namespace {
// A unit's speech without an extra argument (slot 32's third).
constexpr char* kNoExtra = nullptr;
} // namespace

void CInterface3D::OnItemsPicked(void* items, int money, int crippled) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualItemsPicked(items, money, crippled);
}

void CInterface3D::OnUnitJoined(void* unit) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualUnitJoined(unit);
}

void CInterface3D::OnUnitLeft(void* unit) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualUnitLeft(unit);
}

void CInterface3D::OnGameLost() {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualGameOver(0);
}

void CInterface3D::OnUnitSpeech(int id, unsigned event) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualSpeech(id, event, kNoExtra);
}

void CInterface3D::OnPlayersChanged(int added) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualPlayersChanged(added);
}

void CInterface3D::OnChatLine(const char* text, std::uint32_t number) {
    root_->AddLine(text, number);
}

void CInterface3D::OnExchangeDone(void* partyB, void* partyA, void* partyC) {
    if (stackBegin_ != stackEnd_) stackEnd_[-1]->VirtualExchangeDone(partyB, partyA, partyC);
}

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void CInterface3D::SaveWindowState() {
    if (stackBegin_ != stackEnd_) stackBegin_[0]->VirtualSaveState();
}

void CInterface3D::ShowStatusText(const char* text) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    statusSurface_ = memory ? static_cast<CUiSurface*>(memory)->Construct(this, 0, kStatusTop, kLayoutWidth, kStatusBottom,
                                                                         kStatusFormat, 1)
                            : nullptr;
    if (!statusSurface_) return;
    ui_native::Adopt(statusSurface_, "status");
    UiSurfaceGetDC(statusSurface_);
    SurfaceClearOriginal(statusSurface_);
    RECT rect = {0, 0, kLayoutWidth, kStatusTextHeight};
    SurfaceDrawText(statusSurface_, nullptr, text, &rect, kStatusTextFormat, kStatusColour, 1, 1);   // Rebirth-patched
    UiSurfaceReleaseDC(statusSurface_);
    app::FrameStep(true, true);
    if (statusSurface_) {
        statusSurface_->ReleaseContents();
        gamecrt::operator_delete(statusSurface_);
    }
    statusSurface_ = nullptr;
}
EI_REWRITTEN(0x005F75B0, CInterface3D::ShowStatusText);
