// The help / message window (CHelpWindow, ui.h): its Build, Close, the pulse (Update)
// and the input side's button and key handlers.
// The page itself is built by ShowPage (Rebirth-patched; its vanilla body is in ui_help_page.cpp). See
// docs/notes/ui.md.
#include "ui.h"
#include "ui_screens.h"

#include "item_db_text.h"
#include "text_reader.h"

#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "render/render_state.h"
#include "lib/str.h"

#include <cmath>

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
void ReadTutorText(CTextManager* texts, const std::string& key, std::string* text) {
    if (!texts->Open(std::string(kTutorSection) + key)) return;
    std::string read;
    texts->ReadToHash(&read, 1);
    str::TrimRight(read, kLineEnds);
    *text = read.c_str();
}

int Clamp255(int value) { return value > 0xFF ? 0xFF : value < 0 ? 0 : value; }

} // namespace

void CHelpWindow::CreateScreen() {
    LoadTexture(kSaveLoadTexture, 3);
    CreateTexture(0x100, 0x100, 0x20);
    CTextManager* texts = g_clientWorld.texts_;
    if (!texts->Open(std::string(kTutorialSection) + text_)) {
        interface_->SetPendingValue(0);
        return;
    }
    std::string line;
    texts->ReadLine(&line, 1);
    title_ = line.c_str();
    while (texts->FindLine(kScreenLine)) {
        TextNextLine(texts);
        texts->ReadLine(&line, 1);
        lines_.push_back(line);
    }
    if (lines_.empty()) {
        interface_->SetPendingValue(0);
        return;
    }
    ReadTutorText(texts, title_, &title_);
    for (std::string& page : lines_) ReadTutorText(texts, page, &page);
    if (page_ < 0) page_ = 0;
    if (page_ >= static_cast<int>(lines_.size())) page_ = static_cast<int>(lines_.size()) - 1;
    pulseTime_ = 0.0f;
    render::g_renderState.alphaReference = 1;
    ShowPage();
}

void CHelpWindow::CloseScreen() {
    pulsing_.clear();
    pictures_.clear();
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();
}

void CHelpWindow::Update() {
    CUiContainer::Update();
    double time = FrameTime() + pulseTime_;
    pulseTime_ = static_cast<float>(time);
    double wave = (x87::fcos(time * 5.0f) + 1.0f) * 0.5f;
    double alpha = wave * 255.0f;
    float alphaStored = static_cast<float>(alpha);   // stored to a float
    double rest = (1.0f - wave) * 80.0f;
    // _ftol truncates.
    std::uint32_t a = static_cast<std::uint32_t>(Clamp255(static_cast<int>(alpha)));
    std::uint32_t r = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest + alphaStored)));
    std::uint32_t gb = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest)));
    std::uint32_t colour = a << 24 | r << 16 | gb << 8 | gb;
    for (CUiMesh* mesh : pulsing_) mesh->flatColour_ = colour;
}

void CHelpWindow::Finish() {
    interface_->PlaySound(kOkSound);
    if (!message_)
        interface_->helpTopics_[text_] = page_;
    else
        interface_->messageFlag_ = page_;
    interface_->SetPendingValue(0);
}

void CHelpWindow::LeftDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case 0:
        if (page_ > 0) {
            interface_->PlaySound(kNextSound);
            --page_;
            ShowPage();
        }
        break;
    case 1:
        Finish();
        break;
    case 2:
        if (page_ < static_cast<int>(lines_.size()) - 1) {
            interface_->PlaySound(kNextSound);
            ++page_;
            ShowPage();
        }
        break;
    default:
        break;
    }
}

void CHelpWindow::KeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (key == VK_RETURN && page_ < static_cast<int>(lines_.size()) - 1) {
        interface_->PlaySound(kNextSound);
        ++page_;
        ShowPage();
        handled_ = true;
        return;
    }
    if (key != VK_RETURN && key != VK_ESCAPE && g_keyActions[flags & 0x1FF] != kHelpAction) return;
    Finish();
    handled_ = true;
}
