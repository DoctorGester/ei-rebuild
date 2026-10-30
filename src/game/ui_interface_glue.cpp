// The interface manager's small services to the rest of the game (Interface3D): interface sounds,
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

int Interface3D::PlayInterfaceSound(std::string_view name, bool loop) {
    return g_clientWorld.sounds->Play2D(name, kInterfacePriority, kFullVolume, 0, loop, 0, 0, 0);
}

void Interface3D::StopSound(int handle) {
    if (handle != kNoSound) g_clientWorld.sounds->StopSound(handle);
}

void Interface3D::ConsoleAddText(std::string_view text) {
    if (console) console->AddText(text);
}

void Interface3D::SetConsoleRect(const Rect* rect) {
    if (console) console->SetRect(rect);
}

void Interface3D::SetConsoleHistory(int lines) {
    if (console) console->SetHistory(lines);
}

void Interface3D::ShowFps(int on) {
    if (!static_cast<std::uint8_t>(on)) {
        if (fpsSurface) {
            delete fpsSurface;
        }
        fpsSurface = nullptr;
        return;
    }
    if (fpsSurface) return;
    fpsSurface = new UiSurface(this, kFpsLeft, kFpsTop, kFpsRight, kFpsBottom, SurfaceBlend::Keyed, true);
}

void Interface3D::OnQuestVariable(std::string_view name, float value) {
    if (!stack.empty()) ScreenOf(stack.back())->QuestVariable(name, value);
    std::vector<std::string> parts;
    EI_NAN_AUDIT(value);
    if (value != 1.0f) return;
    parts = text::SplitWords(name, '.');
    if (parts.size() > kQuestPart) savedText = parts[kQuestPart];
}

// --- News for the top window -------------------------------------

void Interface3D::OnItemsPicked(const std::vector<Item*>& items, int money, bool crippled) {
    if (!stack.empty()) ScreenOf(stack.back())->ItemsPicked(items, money, crippled);
}

void Interface3D::OnUnitJoined(Unit* unit) {
    if (!stack.empty()) ScreenOf(stack.back())->UnitJoined(unit);
}

void Interface3D::OnUnitLeft(Unit* unit) {
    if (!stack.empty()) ScreenOf(stack.back())->UnitLeft(unit);
}

void Interface3D::OnGameLost() {
    if (!stack.empty()) ScreenOf(stack.back())->GameOver(false);
}

void Interface3D::OnUnitSpeech(int id, AckKind kind) {
    if (!stack.empty()) ScreenOf(stack.back())->Speech(id, kind, kNoExtra);
}

void Interface3D::OnPlayersChanged(int added) {
    if (!stack.empty()) ScreenOf(stack.back())->PlayersChanged(added);
}

void Interface3D::OnChatLine(const char* text) {
    root->AddLine(text);
}

void Interface3D::OnExchangeDone(Party* partyB, Party* partyA, Party* partyC) {
    if (!stack.empty()) ScreenOf(stack.back())->ExchangeDone(partyB, partyA, partyC);
}

// The bottom window, not the top one (a save is made from the battle screen under the save window).
void Interface3D::SaveWindowState() {
    if (!stack.empty()) ScreenOf(stack[0])->SaveState();
}

void Interface3D::ShowStatusText(std::string_view text) {
    statusSurface = new UiSurface(this, 0, kStatusTop, kLayoutWidth, kStatusBottom, SurfaceBlend::Keyed, true);
    statusSurface->Clear();
    Rect rect = {0, 0, kLayoutWidth, kStatusTextHeight};
    statusSurface->DrawTextIn(text, &rect, kStatusTextFormat, kStatusColour, 1, 1);    // Rebirth-patched
    app::FrameStep(true, true);
    if (statusSurface) {
        delete statusSurface;
    }
    statusSurface = nullptr;
}

// --- The interface manager and its chat keys ---------------------------------------------------------

Interface3D g_interface;

void Interface3D::ChatKeyEnter() {
    if (!root->chatOpen) root->SetOpen(true);
}

void Interface3D::ChatKeyBackspace() {
    if (!root->chatOpen) root->ClearLines();
}
