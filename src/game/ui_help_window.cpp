// The help / message window (CHelpWindow, ui.h; vtables 00740E28 / 00740DF4): its Build (slot
// 25, 00658E10), Close (26), the pulse (Update, 24) and the input side's button and key handlers.
// The page itself is drawn by 00659420 (Rebirth-patched, through its address). See
// docs/notes/ui.md.
#include "ui.h"
#include "ui_screens.h"

#include "item_db_text.h"
#include "text_reader.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_ui.h"
#include "render/render_state.h"

#include <cmath>
#include <cstdint>

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* SaveLoadTexture() { return Text(0x00793380); }   // "saveload"
const char* TutorialSection() { return Text(0x00794A78); }   // "tutorial "
const char* ScreenLine() { return Text(0x00794A70); }        // "#screen"
const char* TutorSection() { return Text(0x00794A68); }      // "tutor "
const char* LineEnds() { return Text(0x0078DD00); }          // "\r\n"
const char* NextSound() { return Text(0x00794A84); }         // "buttons\tutorial\next.wav"
const char* OkSound() { return Text(0x0079477C); }           // "buttons\tutorial\ok.wav"

constexpr char kHelpAction = ';';   // the help key's action (KeyActions)

// Direct3D render states through the device's vtable (slot 20), fed to the renderer's mirror.
void SetRenderState(UiD3DDevice* device, int state, std::uint32_t value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t);
    reinterpret_cast<Fn>(device->vtable_[20])(device, state, value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}

// Opens text `section` + `key` and, when it is there, replaces `text` with it up to the next '#'
// (the line ends trimmed).
void ReadTutorText(CTextManager* texts, const mfc::CString& key, mfc::CString* text) {
    BOOL found;
    {
        mfc::CString section = mfc::CString(TutorSection()) + key;
        found = TextOpen(texts, nullptr, section);
    }
    if (!found) return;
    TextReadToHash(texts, nullptr, text, 1);
    text->TrimRight(LineEnds());
}

int Clamp255(int value) { return value > 0xFF ? 0xFF : value < 0 ? 0 : value; }

} // namespace

__declspec(naked) void CHelpWindow::ShowPage() EI_ORIGINAL(0x00659420)

void CHelpWindow::Build() {
    LoadTexture(SaveLoadTexture(), 3, nullptr);
    CreateTexture(0x100, 0x100, 0x20);
    CTextManager* texts = GameTexts();
    BOOL found;
    {
        mfc::CString section = mfc::CString(TutorialSection()) + text_;
        found = TextOpen(texts, nullptr, section);
    }
    if (!found) {
        interface_->SetPendingValue(0);
        return;
    }
    TextReadLine(texts, nullptr, &title_, 1);
    mfc::CString line;
    while (TextFindLine(texts, nullptr, ScreenLine())) {
        TextNextLine(texts);
        TextReadLine(texts, nullptr, &line, 1);
        lines_.SetAtGrow(lines_.size, line);
    }
    if (lines_.size == 0) {
        interface_->SetPendingValue(0);
        return;
    }
    ReadTutorText(texts, title_, &title_);
    for (int i = 0; i < lines_.size; ++i) ReadTutorText(texts, lines_.data[i], &lines_.data[i]);
    if (page_ < 0) page_ = 0;
    if (page_ >= lines_.size) page_ = lines_.size - 1;
    pulseTime_ = 0.0f;
    SetRenderState(interface_->renderer_->device_, kRenderStateAlphaRef, 1);
    ShowPage();
}
EI_REPLACE_METHOD(0x00658E10, CHelpWindow::Build);

void CHelpWindow::Close() {
    pulsing_.ClearWith([](CUiMesh**) {});
    listB_.ClearWith([](std::uint32_t*) {});
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();
}
EI_REPLACE_METHOD(0x006590F0, CHelpWindow::Close);

void CHelpWindow::Update() {
    UpdateBase();
    double time = FrameTime() + pulseTime_;
    pulseTime_ = static_cast<float>(time);
    double wave = (std::cos(time * 5.0f) + 1.0f) * 0.5f;
    double alpha = wave * 255.0f;
    volatile float alphaStored = static_cast<float>(alpha);   // the original's FST to a local
    double rest = (1.0f - wave) * 80.0f;
    // _ftol truncates.
    std::uint32_t a = static_cast<std::uint32_t>(Clamp255(static_cast<int>(alpha)));
    std::uint32_t r = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest + alphaStored)));
    std::uint32_t gb = static_cast<std::uint32_t>(Clamp255(static_cast<int>(rest)));
    std::uint32_t colour = a << 24 | r << 16 | gb << 8 | gb;
    for (auto* node = pulsing_.First(); node != pulsing_.End(); node = node->next) node->data->plainColour_ = colour;
}
EI_REPLACE_METHOD(0x0065A5E0, CHelpWindow::Update);

void CHelpWindow::Finish() {
    interface_->PlaySound(OkSound(), 0);
    if (!message_)
        *HelpTopicsAtOriginal(&interface_->helpTopics_, nullptr, &text_) = page_;
    else
        interface_->messageFlag_ = page_;
    interface_->SetPendingValue(0);
}

void CHelpWindow::OnLeftDown(int buttons, int x, int y) {
    InputOf(this)->BaseLButtonDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case 0:
        if (page_ > 0) {
            interface_->PlaySound(NextSound(), 0);
            --page_;
            ShowPage();
        }
        break;
    case 1:
        Finish();
        break;
    case 2:
        if (page_ < lines_.size - 1) {
            interface_->PlaySound(NextSound(), 0);
            ++page_;
            ShowPage();
        }
        break;
    default:
        break;
    }
}

void CHelpWindow::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (key == VK_RETURN && page_ < lines_.size - 1) {
        interface_->PlaySound(NextSound(), 0);
        ++page_;
        ShowPage();
        inputHandled_ = true;
        return;
    }
    if (key != VK_RETURN && key != VK_ESCAPE && KeyActions()[flags & 0x1FF] != kHelpAction) return;
    Finish();
    inputHandled_ = true;
}

namespace {
void __fastcall HelpWindowLeftDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CHelpWindow>(input)->OnLeftDown(buttons, x, y);
}
void __fastcall HelpWindowKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CHelpWindow>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x00659190, HelpWindowLeftDown);
EI_REPLACE(0x006592C0, HelpWindowKeyDown);
