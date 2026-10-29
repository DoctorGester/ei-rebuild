// The interface manager's small services to the rest of the game (CInterface3D): interface sounds,
// the console window settings, the frame rate surface and quest variable news for
// the top window. See ui.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "console.h"
#include "game_singletons.h"
#include "sound.h"
#include "text_split.h"
#include "ui.h"
#include "world.h"

#include "lib/nan_audit.h"

#include <vector>

namespace {
constexpr int kInterfacePriority = 1000;
constexpr int kFullVolume = 100;
constexpr int kNoSound = -1;
constexpr int kLayoutWidth = 800;
// The frame rate's surface: (730, 10)-(800, 28).
constexpr int kFpsLeft = 730, kFpsTop = 10, kFpsRight = 800, kFpsBottom = 28;
constexpr int kQuestPart = 2;   // "q.<zone>.<quest>": the quest's name
// The status line: a surface over (0, 280)-(800, 320), the text in green in its top 40 pixels.
constexpr int kStatusTop = 280, kStatusBottom = 320;
constexpr int kStatusTextFormat = 2, kStatusTextHeight = 40;
constexpr std::uint32_t kStatusColour = 0xFF00;

// A unit's speech without an extra argument.
constexpr char* kNoExtra = nullptr;

} // namespace

int CInterface3D::PlaySound(std::string_view name, bool loop) {
    return g_clientWorld->sounds_->Play2D(name, kInterfacePriority, kFullVolume, 0, loop, 0, 0, 0);
}

void CInterface3D::StopSound(int handle) {
    if (handle != kNoSound) g_clientWorld->sounds_->StopSound(handle);
}

void CInterface3D::ConsoleAddText(std::string_view text) {
    if (console_) console_->AddText(text);
}

void CInterface3D::SetConsoleRect(const RECT* rect) {
    if (console_) console_->SetRect(rect);
}

void CInterface3D::SetConsoleHistory(int lines) {
    if (console_) console_->SetHistory(lines);
}

void CInterface3D::ShowFps(int on) {
    if (!static_cast<std::uint8_t>(on)) {
        if (fpsSurface_) {
            delete fpsSurface_;
        }
        fpsSurface_ = nullptr;
        return;
    }
    if (fpsSurface_) return;
    fpsSurface_ = new CUiSurface(this, kFpsLeft, kFpsTop, kFpsRight, kFpsBottom, SurfaceBlend::Keyed, true);
}

void CInterface3D::OnQuestVariable(std::string_view name, float value) {
    if (!stack_.empty()) ScreenOf(stack_.back())->QuestVariable(name, value);
    std::vector<std::string> parts;
    EI_NAN_AUDIT(value);
    if (value != 1.0f) return;
    text::SplitWords(name, &parts, '.');
    if (parts.size() > kQuestPart) savedText_ = parts[kQuestPart];
}

// --- News for the top window -------------------------------------

void CInterface3D::OnItemsPicked(std::list<CItem*>* items, int money, bool crippled) {
    if (!stack_.empty()) ScreenOf(stack_.back())->ItemsPicked(items, money, crippled);
}

void CInterface3D::OnUnitJoined(CUnit* unit) {
    if (!stack_.empty()) ScreenOf(stack_.back())->UnitJoined(unit);
}

void CInterface3D::OnUnitLeft(CUnit* unit) {
    if (!stack_.empty()) ScreenOf(stack_.back())->UnitLeft(unit);
}

void CInterface3D::OnGameLost() {
    if (!stack_.empty()) ScreenOf(stack_.back())->GameOver(false);
}

void CInterface3D::OnUnitSpeech(int id, AckKind kind) {
    if (!stack_.empty()) ScreenOf(stack_.back())->Speech(id, kind, kNoExtra);
}

void CInterface3D::OnPlayersChanged(int added) {
    if (!stack_.empty()) ScreenOf(stack_.back())->PlayersChanged(added);
}

void CInterface3D::OnChatLine(const char* text) {
    root_->AddLine(text);
}

void CInterface3D::OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    if (!stack_.empty()) ScreenOf(stack_.back())->ExchangeDone(partyB, partyA, partyC);
}

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void CInterface3D::SaveWindowState() {
    if (!stack_.empty()) ScreenOf(stack_[0])->SaveState();
}

void CInterface3D::ShowStatusText(std::string_view text) {
    statusSurface_ = new CUiSurface(this, 0, kStatusTop, kLayoutWidth, kStatusBottom, SurfaceBlend::Keyed, true);
    statusSurface_->Clear();
    RECT rect = {0, 0, kLayoutWidth, kStatusTextHeight};
    statusSurface_->DrawTextIn(text, &rect, kStatusTextFormat, kStatusColour, 1, 1);   // Rebirth-patched
    app::FrameStep(true, true);
    if (statusSurface_) {
        delete statusSurface_;
    }
    statusSurface_ = nullptr;
}
