// The interface manager's start (CInterface3D::Create, called once by the application's start with the
// renderer's device object): the screen size, the three fonts, the keyboard layout, the console window, the
// chat overlay, the fade and the help topics seen (config\tutorial.ini).
// Rebirth's WideScreen changes the fonts' scale (widescreen::FontScale). See ui.h and docs/notes/ui.md,
// section 3.
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "text_split.h"
#include "texture.h"
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "lib/file_io.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <string>
#include "game_singletons.h"
#include "options.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

// The fonts: the face, the size the scale is measured with (tenths of a point) and each font's
// share of the screen width.
const char* FontFace() { return "Times New Roman"; }
constexpr int kMeasureSize = 750;
constexpr int kFontShares[3] = {104, 112, 144};

// A font of `size` tenths of a point, kept in `font`; false (and the fatal error) when it has no size.
bool MakeFont(UiFont& font, int size) {
    font = PointFont(size, FontFace());
    if (font) return true;
    game::FatalError("CInterface3D::Create.\n Cannot create font.\n");
    return false;
}

// The language the keyboard starts in (the option, a Windows language id) and English.
DWORD KeyboardLanguage() { return static_cast<DWORD>(options::g_options.keyboardLayout_); }
constexpr DWORD kEnglish = 0x409;
constexpr int kMaxLayouts = 16;

// Activates the layout of the wanted language, else the English one, else the one in use. Every
// layout is activated on the way: its name (the language id in hex) can only be asked of the
// active one.
void ChooseKeyboardLayout() {
    const DWORD wanted = KeyboardLanguage();
    HKL layouts[kMaxLayouts];
    const int count = gameapi::GetKeyboardLayoutList(kMaxLayouts, layouts);
    int english = -1;
    int found = -1;
    HKL chosen = gameapi::GetKeyboardLayout(0);
    for (int i = 0; i < count; ++i) {
        gameapi::ActivateKeyboardLayout(layouts[i], 0);
        char name[12];
        if (!gameapi::GetKeyboardLayoutNameA(name)) continue;
        unsigned id = 0;
        sscanf_s(name, "%x", &id);
        id &= 0xFFFF;
        if (id == wanted) {
            found = i;
            break;
        }
        if (id == kEnglish) english = i;
    }
    if (found >= 0)
        chosen = layouts[found];
    else if (english >= 0)
        chosen = layouts[english];
    gameapi::ActivateKeyboardLayout(chosen, 0);
}
} // namespace

void CInterface3D::Create(CTextureDevice* device) {
    renderer_ = device;
    width_ = device->screen_.right;
    height_ = device->screen_.bottom;

    // The scale: what a 75-point font is high here, in pixels / 100.
    if (!MakeFont(fonts_[0], kMeasureSize)) return;
    const float scale = static_cast<float>(static_cast<float>(fonts_[0].pixels) * widescreen::FontScale());   // 0.01
    for (int i = 0; i < 3; ++i) {
        float size = static_cast<float>(width_ * kFontShares[i]) / scale * 0.00125f;   // 1 / 800
        if (!MakeFont(fonts_[i], static_cast<int>(libcrt::Ftol(size)))) return;
    }

    ChooseKeyboardLayout();
    keybind::LoadKeyBindings();
    console_ = new console::ConsoleWindow(this);
    root_ = new CChatOverlay(this);
    mouseX_ = 0;
    mouseCapture_ = nullptr;
    backdrop_ = nullptr;
    secondBackdrop_ = nullptr;
    backdropMode_ = 0;
    closeAll_ = false;
    queuedWindow_ = nullptr;
    drawing_ = true;
    mouseY_ = 0;
    fade_ = new CScreenFade();
    hudPanelsIn_[0] = 0;
    hudPanelsIn_[1] = 0;
    hudPanelsIn_[2] = 0;
    hudBodyPart_ = 0;
    hudTopPage_ = 0;
    fastGame_ = 0;
    ctrlDown_ = false;
    shiftDown_ = 0;
    altDown_ = false;
    doubleClick_ = false;
    rightDown_ = false;
    pollTop_ = false;

    // The help topics seen: "topic count" lines.
    lib::TextFile file;
    bool opened;
    {
        std::string path = g_dataFolder + game::TutorialIniText();
        opened = file.Open(path, "r") != 0;
    }
    if (opened) {
        std::string line;
        std::vector<std::string> words;
        while (file.ReadLine(line)) {
            words = text::SplitWords(line, ' ');
            if (words.size() <= 1) continue;
            helpTopics_[words[0]] = str::ToInt(words[1]);
        }
        file.Close();
    }
    messageFlag_ = 0;
    fpsSurface_ = nullptr;
    fpsSpare_ = 0;
    fpsTime_ = gameapi::timeGetTime();
    fpsFrames_ = 0;
    disconnected_ = false;
}
