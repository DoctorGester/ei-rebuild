// The interface manager's start (CInterface3D::Create 005F4990, called once by the application's
// start with the renderer's device object): the screen size, the three fonts, the keyboard layout,
// the console window, the debug information panel, the chat overlay, the fade and the help topics
// seen (config\tutorial.ini). Rebirth's WideScreen changes the fonts' scale (addon's FMUL at +BF
// reads its float at 005F4F81: widescreen::FontScale); hooked in both launches. See ui.h and
// docs/notes/ui.md, section 3.
#include "ui.h"

#include "console.h"
#include "game_strings.h"
#include "text_split.h"
#include "texture.h"
#include "ui_gdi.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <utility>

namespace {

// The fonts: the face, the size the scale is measured with (tenths of a point) and each font's
// share of the screen width.
const char* FontFace() { return "Times New Roman"; }
constexpr int kMeasureSize = 750;
constexpr int kFontShares[3] = {104, 112, 144};
void** FontVtable() { return ei::GamePointer<void*>(0x0073BFB4); }

// A new CFont without a handle (null when the memory cannot be had).
ui_gdi::GameFont* NewFont() {
    auto* font = static_cast<ui_gdi::GameFont*>(gamecrt::operator_new(sizeof(ui_gdi::GameFont)));
    if (!font) return nullptr;
    font->handle = nullptr;
    font->vtable = FontVtable();
    return font;
}

bool CreateFont(ui_gdi::GameFont* font, int size) {
    if (font->CreatePointFont(size, FontFace(), nullptr)) return true;
    game::FatalError("CInterface3D::Create.\n Cannot create font.\n");   // "CInterface3D::Create.\n Cannot create font.\n"
    return false;
}

// The language the keyboard starts in (the option 0079BE98, a Windows language id) and English.
DWORD KeyboardLanguage() { return ei::GameData<const volatile DWORD>(0x0079BE98); }
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
        libcrt::Sscanf(name, "%x", &id);
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

static_assert(sizeof(console::ConsoleWindow) == 0x7C && sizeof(CChatOverlay) == 0x158 && sizeof(CScreenFade) == 0x14);

} // namespace

void CInterface3D::Create(CTextureDevice* device) {
    renderer_ = reinterpret_cast<UiRenderer*>(device);
    // (Without a primary surface the original stores an uninitialised stack slot in both.)
    int width = 0;
    int height = 0;
    if (device->primary_) {
        width = device->renderTarget_->width;
        height = device->renderTarget_->height;
    }
    width_ = width;
    height_ = height;

    // The scale: what a 75-point font is high here, in pixels / 100.
    fonts_[0] = NewFont();
    if (!CreateFont(fonts_[0], kMeasureSize)) return;
    LOGFONTA measured;
    gameapi::GetObjectA(fonts_[0]->handle, sizeof(measured), &measured);
    const int pixels = measured.lfHeight < 0 ? -measured.lfHeight : measured.lfHeight;
    const float scale = static_cast<float>(static_cast<float>(pixels) * widescreen::FontScale());   // 0.01 (0073D09C)
    if (fonts_[0]) fonts_[0]->Delete();
    for (int i = 0; i < 3; ++i) {
        fonts_[i] = NewFont();
        float size = static_cast<float>(width * kFontShares[i]) / scale * 0.00125f /*00740164*/;   // 1 / 800
        if (!CreateFont(fonts_[i], static_cast<int>(gamecrt::Ftol(size)))) return;
    }

    ChooseKeyboardLayout();
    keybind::LoadKeyBindings();
    auto* window = static_cast<console::ConsoleWindow*>(gamecrt::operator_new(sizeof(console::ConsoleWindow)));
    console_ = window ? window->Construct(&width_) : nullptr;
    strmap::PanelInit(DebugInfoPanel(), nullptr, this);
    auto* chat = static_cast<CChatOverlay*>(gamecrt::operator_new(sizeof(CChatOverlay)));
    root_ = chat ? chat->Construct(this) : nullptr;
    mouseX_ = 0;
    mouseCapture_ = nullptr;
    backdrop_ = nullptr;
    secondBackdrop_ = nullptr;
    block5C_ = nullptr;
    backdropMode_ = 0;
    closeAll_ = false;
    queuedWindow_ = nullptr;
    drawing_ = true;
    mouseY_ = 0;
    auto* fade = static_cast<CScreenFade*>(gamecrt::operator_new(sizeof(CScreenFade)));
    fade_ = fade ? fade->Construct() : nullptr;
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
    console::CStdioFile file;
    bool opened;
    {
        mfc::CString path = TheDataFolder() + game::TutorialIniText();
        opened = file.Open(path, console::CStdioFile::typeText, nullptr) != 0;
    }
    if (opened) {
        mfc::CString line;
        console::CStringArray words;
        while (file.ReadString(line)) {
            text::SplitWords(line, &words, ' ');
            if (words.GetSize() <= 1) continue;
            const char* count = words[1];
            helpTopics_.Subscript(words[0]) = libcrt::Atoi(count);   // operator[], inlined by the original
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
EI_REPLACE_METHOD(0x005F4990, CInterface3D::Create);
