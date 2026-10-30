// The help / message window (HelpWindow, ui.h): its Build, Close, the pulse (Update)
// and the input side's button and key handlers.
// The page itself is built by ShowPage (Rebirth-patched; its vanilla body is in ui_help_page.cpp). See
// docs/notes/ui.md.
#include "ui.h"
#include "ui_screens.h"

#include "text_manager.h"

#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "render/render_state.h"
#include "lib/str.h"

#include <cmath>
#include <windows.h>

namespace {

constexpr const char* kSaveLoadTexture = "saveload";
constexpr const char* kTutorialSection = "tutorial ";
constexpr const char* kScreenLine = "#screen";
constexpr const char* kTutorSection = "tutor ";
constexpr const char* kLineEnds = "\r\n";          // the line end
constexpr const char* kNextSound = "buttons\\tutorial\\next.wav";
constexpr const char* kOkSound = "buttons\\tutorial\\ok.wav";

constexpr char kHelpAction = ';';   // the help key's action (g_keyActions)

// Opens text "tutor " + `key` and, when it is there, replaces `text` with it up to the next '#' (the line ends
// trimmed).
void ReadTutorText(TextManager* texts, const std::string& key, std::string* text) {
    if (!texts->Open(std::string(kTutorSection) + key)) return;
    std::string read = texts->ReadToHash();
    str::TrimRight(read, kLineEnds);
    *text = read.c_str();
}

int Clamp255(int value) { return value > 0xFF ? 0xFF : value < 0 ? 0 : value; }

} // namespace

void HelpWindow::CreateScreen() {
    LoadTexture(kSaveLoadTexture, TextureKind::Interface);
    CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    TextManager* texts = g_clientWorld.texts;
    if (!texts->Open(std::string(kTutorialSection) + text)) {
        ui->SetPendingValue(0);
        return;
    }
    std::string line = texts->ReadLine(true);
    title = line.c_str();
    while (texts->FindLine(kScreenLine)) {
        texts->NextLine();
        line = texts->ReadLine(true);
        lines.push_back(line);
    }
    if (lines.empty()) {
        ui->SetPendingValue(0);
        return;
    }
    ReadTutorText(texts, title, &title);
    for (std::string& pageText : lines) ReadTutorText(texts, pageText, &pageText);
    if (page < 0) page = 0;
    if (page >= static_cast<int>(lines.size())) page = static_cast<int>(lines.size()) - 1;
    pulseTime = 0.0f;
    render::g_renderState.alphaReference = 1;
    ShowPage();
}

void HelpWindow::CloseScreen() {
    pulsing.clear();
    pictures.clear();
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();
}

void HelpWindow::Update() {
    UiContainer::Update();
    float time = FrameTime() + pulseTime;
    pulseTime = time;
    float wave = (std::cos(time * 5.0f) + 1.0f) * 0.5f;
    float alpha = wave * 255.0f;
    float alphaStored = alpha;   // stored to a float
    float rest = (1.0f - wave) * 80.0f;
    // _ftol truncates.
    std::uint32_t a = static_cast<std::uint32_t>(Clamp255(static_cast<int>(alpha)));
    std::uint32_t r = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest + alphaStored)));
    std::uint32_t gb = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest)));
    std::uint32_t colour = a << 24 | r << 16 | gb << 8 | gb;
    for (UiMesh* mesh : pulsing) mesh->flatColour = colour;
}

void HelpWindow::Finish() {
    ui->PlayInterfaceSound(kOkSound);
    if (!message)
        ui->helpTopics[text] = page;
    else
        ui->messageFlag = page;
    ui->SetPendingValue(0);
}

void HelpWindow::LeftDown(int buttons, int x, int y) {
    UiContainer::LeftDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case 0:
        if (page > 0) {
            ui->PlayInterfaceSound(kNextSound);
            --page;
            ShowPage();
        }
        break;
    case 1:
        Finish();
        break;
    case 2:
        if (page < static_cast<int>(lines.size()) - 1) {
            ui->PlayInterfaceSound(kNextSound);
            ++page;
            ShowPage();
        }
        break;
    default:
        break;
    }
}

void HelpWindow::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (key == VK_RETURN && page < static_cast<int>(lines.size()) - 1) {
        ui->PlayInterfaceSound(kNextSound);
        ++page;
        ShowPage();
        handled = true;
        return;
    }
    if (key != VK_RETURN && key != VK_ESCAPE && g_keyActions[modifiers & 0x1FF] != kHelpAction) return;
    Finish();
    handled = true;
}
