// Interface3D coordinate scaling and small accessors, surface placement, model previews and the
// spell book's record lookups. See ui.h.
#include "ui.h"

#include "app.h"
#include "console.h"
#include "core_streams.h"
#include "dib.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "options.h"
#include "save_versions.h"
#include "text_manager.h"
#include "text_split.h"
#include "texture.h"
#include "ui_cursor.h"
#include "ui_widescreen.h"
#include "world.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <list>
#include <memory>
#include <string>
#include <utility>

// --- Interface3D ----------------------------------------------------------------------------

// The scaling is the window's layout (ui_widescreen.h): the original's x * width / 800, y * height / 600 on a 4:3
// window (where the 4:3 part is the window), Rebirth's WideScreen on a wider one.

void Interface3D::ScaleRectToScreen(Rect* rect) const { widescreen::RectToScreen(rect, true); }
void Interface3D::ScalePointToScreen(Point* point) const { widescreen::PointToScreen(point, true); }
void Interface3D::ScaleRectFromScreen(Rect* rect) const { widescreen::RectFromScreen(rect, true); }
void Interface3D::ScalePointFromScreen(Point* point) const { widescreen::PointFromScreen(point, true); }

void Interface3D::ScaleRectToScreenNoMargin(Rect* rect) const { widescreen::RectToScreen(rect, false); }
void Interface3D::ScalePointToScreenNoMargin(Point* point) const { widescreen::PointToScreen(point, false); }
void Interface3D::ScalePointFromScreenNoMargin(Point* point) const { widescreen::PointFromScreen(point, false); }

UiWindow* Interface3D::GetActiveWindow() const {
    return stack.begin() != stack.end() ? stack.end()[-1] : nullptr;
}

void Interface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag = true;
    pendingValue = value;
}

// --- UiSurface ------------------------------------------------------------------------------

// --- UiModel --------------------------------------------------------------------------------

// --- Record lookups --------------------------------------------------------------------------

// --- Create ------------------------------------------------------------------------------------------

// The interface manager's start (Interface3D::Create, called once by the application's start with the
// renderer's device object): the screen size, the three fonts, the keyboard layout, the console window, the
// chat overlay, the fade and the help topics seen (config\tutorial.ini).
// The fonts' scale is the window layout's (widescreen::FontScale: the fonts follow the window's 4:3 part). See ui.h
// and docs/notes/ui.md, section 3.

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

// The keyboard's language when the option's (a Windows language id) has no layout: English.
constexpr std::uint32_t kEnglish = 0x409;

} // namespace

void Interface3D::Create(TextureDevice* device) {
    renderer = device;
    width = device->screen.right;
    height = device->screen.bottom;

    // The scale: what a 75-point font is high here, in pixels / 100.
    if (!MakeFont(fonts[0], kMeasureSize)) return;
    const float scale = fonts[0].pixels * widescreen::FontScale();    // 0.01
    for (int i = 0; i < 3; ++i) {
        float size = (width * kFontShares[i]) / scale * 0.00125f;    // 1 / 800
        if (!MakeFont(fonts[i], static_cast<int>(size))) return;
    }

    platform::ActivateKeyboardLayout(static_cast<std::uint32_t>(options::g_options.keyboardLayout), kEnglish);
    keybind::LoadKeyBindings();
    console = new console::ConsoleWindow(this);
    root = new ChatOverlay(this);
    mouseX = 0;
    mouseCapture = nullptr;
    backdrop = nullptr;
    secondBackdrop = nullptr;
    backdropMode = 0;
    closeAll = false;
    queuedWindow = nullptr;
    drawing = true;
    mouseY = 0;
    fade = new ScreenFade();
    hudPanelsIn[0] = 0;
    hudPanelsIn[1] = 0;
    hudPanelsIn[2] = 0;
    hudBodyPart = 0;
    hudTopPage = 0;
    fastGame = 0;
    ctrlDown = false;
    shiftDown = 0;
    altDown = false;
    doubleClick = false;
    rightDown = false;
    pollTop = false;

    // The help topics seen: "topic count" lines.
    platform::TextFile file;
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
            helpTopics[words[0]] = str::ToInt(words[1]);
        }
        file.Close();
    }
    messageFlag = 0;
    fpsSurface = nullptr;
    fpsSpare = 0;
    fpsTime = platform::Milliseconds();
    fpsFrames = 0;
    disconnected = false;
}

// --- Shutdown ----------------------------------------------------------------------------------------

// The interface manager's end (Interface3D): the input reset after the windows close, the shutdown from the
// main loop's exit (it writes config\tutorial.ini), the destructor body of the global interface run at exit
// and the console window's destructor body. See ui.h and docs/notes/ui.md, section 3.

namespace {
constexpr int kCursorWait = 3;

// A surface the interface owns: its destructor body, then the memory.
void DeleteSurface(UiSurface* surface) {
    if (!surface) return;
    delete surface;
}
} // namespace

void Interface3D::ResetInput() {
    ctrlDown = false;
    altDown = false;
    shiftDown = 0;
    rightDown = false;
    doubleClick = false;
    if (!stack.empty()) stack.back()->Input()->Deactivate();
}

void Interface3D::Shutdown() {
    g_cursor.SetShape(kCursorWait);
    while (!stack.empty()) {
        stack.back()->Input()->Deactivate();
        ScreenOf(stack.back())->CloseScreen();
        delete stack.back();
        stack.pop_back();
    }
    ResetInput();
    app::PumpMessages();
    app::g_interfaceHasScreen = 0;

    platform::TextFile file;
    bool opened;
    {
        std::string path = g_dataFolder + game::TutorialIniText();
        opened = file.Open(path, "w") != 0;
    }
    if (opened) {
        std::string line;
        for (const auto& [topic, page] : helpTopics) {
            line = str::Format(game::TutorialLineFormat(), topic, page);
            file.WriteString(line);
        }
        file.Close();
    }

    DeleteSurface(fpsSurface);
    fpsSurface = nullptr;
    // The queued window is deleted but its pointer kept (like the original).
    delete queuedWindow;
    DeleteSurface(backdrop);
    backdrop = nullptr;
    DeleteSurface(secondBackdrop);
    secondBackdrop = nullptr;
    screenCopy = {};
    if (root) {
        delete root;
        root = nullptr;
    }
    if (console) {
        delete console;
        console = nullptr;
    }
    keybind::SaveKeyBindings();
    for (UiFont& font : fonts) font = UiFont{};
    delete fade;    // (the pointer is not cleared)
}

console::ConsoleWindow::~ConsoleWindow() {
    DeleteSurface(surface);
    surface = nullptr;
    font = UiFont{};
}

// --- Save --------------------------------------------------------------------------------------------

// Interface3D's saved state. See ui.h.

namespace {

// The save stream: reads `size` bytes; version is the save's version.
void Read(ByteStream* stream, void* to, int size) { stream->Read(to, static_cast<std::uint32_t>(size)); }
int Version(const ByteStream* stream) { return static_cast<int>(stream->version); }

const char* const kMessageKeys[13] = {
    "lmp_player_connected",
    "lmp_player_disconnected",
    "lmp_player_kicked",
    "lmp_player_banned",
    "lmp_player_lost_connection",
    "lmp_quest_taken",
    "lmp_quest_canceled",
    "lmp_quest_completed",
    "lmp_zone_enabled",
    "lmp_zone_disabled",
    "lmp_trade_requested",
    "lmp_trade_canceled",
    g_pauseMessageKey,
};

} // namespace

void Interface3D::LoadState(ByteStream* stream) {
    stream->ReadValue(hudPanelsIn[0]);
    stream->ReadValue(hudPanelsIn[1]);
    stream->ReadValue(hudPanelsIn[2]);
    stream->ReadValue(hudBodyPart);
    stream->ReadValue(hudTopPage);
    if (Version(stream) < kVersionFastGame) return;
    stream->ReadValue(fastGame);
    if (Version(stream) < kVersionInterfaceList) return;
    // An obsolete list: each name and value read and dropped (the last name is kept until the end, like
    // the original's local string).
    std::string last;
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        last = stream->ReadString();
        int value;
        stream->ReadValue(value);
    }
    messageText = stream->ReadString();
    if (Version(stream) >= kVersionMessageFlag) {
        stream->ReadValue(messageFlag);
        if (Version(stream) >= kVersionSavedText) savedText = stream->ReadString();
    }
}

// --- The multiplayer messages' texts -------------------------------------------------

char g_pauseMessageKey[12] = /*007930EC*/ {"reserved_02"};

std::string Interface3D::MessageText(int id, const char* value) {
    std::string key;
    if (static_cast<unsigned>(id) <= 12) key = kMessageKeys[id];
    key = g_clientWorld.texts->TextManager::GetString(key);
    if (value) return str::Format(key, value);
    return key;
}

// --- The saved state written -----------------------------------------------------------

void Interface3D::SaveState(ByteStream* stream) {
    for (std::uint8_t flag : hudPanelsIn) stream->WriteValue(flag);
    int value = hudBodyPart;
    stream->WriteValue(value);
    value = hudTopPage;
    stream->WriteValue(value);
    value = fastGame;
    stream->WriteValue(value);
    value = 0;   // the obsolete list LoadState skips: no entries
    stream->WriteValue(value);
    stream->WriteString(messageText);
    value = messageFlag;
    stream->WriteValue(value);
    stream->WriteString(savedText);
}

// --- Keys --------------------------------------------------------------------------------------------

// The interface's key-down handler (Interface3D). See ui.h and docs/notes/ui.md.
// The modifier flags it sets are Ctrl (scan codes 0x1D, 0x11D), Shift (0x2A, 0x36) and Alt (0x38, 0x138).

namespace {

// The key bindings' action of a scan code (g_keyActions), as a byte.
std::uint8_t KeyCharacter(int scan) { return static_cast<std::uint8_t>(g_keyActions[scan]); }

std::uint32_t g_shotCounter = /*007C7E24*/ {};

constexpr int kRepeat = 0x4000;

// The input side's key-down handler of a window.
void WindowKeyDown(UiWindow* window, int key, int repeat, int flags) {
    window->Input()->Event(UiInputSide::kEventKeyDown, key, repeat, flags);
}

} // namespace

void Interface3D::OnKeyDown(int key, int repeat, int flags) {
    console::ConsoleWindow* consoleWindow = console;
    int scan = flags & 0x1FF;
    switch (scan) {
    case 0x1D:
    case 0x11D:   // Ctrl
        if (flags & kRepeat) return;
        ctrlDown = true;
        break;
    case 0x2A:
    case 0x36:    // Shift
        if (flags & kRepeat) return;
        shiftDown = 1;
        break;
    case 0x38:
    case 0x138:   // Alt
        if (flags & kRepeat) return;
        altDown = true;
        break;
    case 0x29:    // the key under Esc
        consoleWindow->Show(!consoleWindow->shown);
        return;
    case 0x46: {  // Scroll Lock: a screenshot
        Dib* picture = CaptureScreen(g_sceneRenderer.device);
        if (!picture) return;
        std::string name;
        name = std::format("shot{:04}.bmp", g_shotCounter);
        ++g_shotCounter;
        platform::File file(name, platform::File::modeCreate | platform::File::modeWrite);
        picture->Write(&file);
        file.Close();
        delete picture;
        return;
    }
    default:
        break;
    }

    if (consoleWindow->shown) {
        consoleWindow->OnKeyDown(key, repeat, flags);
        return;
    }
    ChatOverlay* chat = root;
    if (IsMultiplayerClient() && chat->chatOpen) {
        WindowKeyDown(chat, key, repeat, flags);
        return;
    }
    if (!stack.empty()) WindowKeyDown(stack.back(), key, repeat, flags);
    if (!stack.empty() && !stack.back()->Input()->handled &&
        KeyCharacter(scan) == ';') {
        OpenHelp(true);
    }
}
