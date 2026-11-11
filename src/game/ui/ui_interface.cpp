// Interface3D coordinate scaling and small accessors, surface placement, model previews and the
// spell book's record lookups, and the text layout on interface surfaces. See ui.h and docs/notes/ui.md
// ("Interface base").
#include "game/ui/ui.h"
#include "game/ui/ui_chat_overlay.h"
#include "game/app/key_bindings.h"
#include "engine/core/asset_text.h"

#include "game/app/app.h"
#include "game/console/console.h"
#include "engine/core/core_streams.h"
#include "engine/models/dib.h"
#include "game/app/singletons.h"
#include "game/app/options.h"
#include "game/saves/save_versions.h"
#include "engine/data/text_manager.h"
#include "engine/core/text_split.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/screen_layout.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/render_text.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// --- Interface3D ----------------------------------------------------------------------------

// The scaling is the window's layout (screen_layout.h): the original's x * width / 800, y * height / 600 on a 4:3
// window (where the 4:3 part is the window), Rebirth's WideScreen on a wider one.

UiContainer* Interface3D::GetActiveWindow() const {
    return stack.empty() ? nullptr : stack.back().get();
}

void Interface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag = true;
    pendingValue = value;
}

// --- Create ------------------------------------------------------------------------------------------

// The interface manager's start (Interface3D::Create, called once by the application's start with the
// renderer's device object): the screen size, the three fonts, the keyboard layout, the console window, the
// chat overlay, the fade and the help topics seen (config\tutorial.ini).
// The fonts' scale is the window layout's (screen_layout::FontScale: the fonts follow the window's 4:3 part). See ui.h
// and docs/notes/ui.md, section 3.

namespace {

// The fonts: the face, the size the scale is measured with (tenths of a point) and each font's
// share of the screen width.
constexpr const char* kFontFace = "Times New Roman";
constexpr int kMeasureSize = 750;
constexpr int kFontShares[3] = {104, 112, 144};

// A font of `size` tenths of a point, kept in `font`; one without a size ends the game.
void MakeFont(UiFont& font, int size) {
    font = PointFont(size, kFontFace);
    if (!font) game::FatalExit("CInterface3D::Create.\n Cannot create font.\n");
}

// The keyboard's language when the option's (a Windows language id) has no layout: English.
constexpr std::uint32_t kEnglish = 0x409;

// The three fonts sized to the screen's width and the layout's font scale.
void MakeFonts(UiFont (&fonts)[3], int width) {
    // The scale: what a 75-point font is high here, in pixels / 100.
    MakeFont(fonts[0], kMeasureSize);
    const float scale = fonts[0].pixels * screen_layout::FontScale();    // 0.01
    for (int i = 0; i < 3; ++i) {
        float size = (width * kFontShares[i]) / scale * 0.00125f;    // 1 / 800
        MakeFont(fonts[i], static_cast<int>(size));
    }
}

} // namespace

void Interface3D::Create(TextureDevice* device) {
    renderer = device;
    width = device->screen.right;
    height = device->screen.bottom;
    MakeFonts(fonts, width);

    platform::ActivateKeyboardLayout(kEnglish, kEnglish);
    keybind::LoadKeyBindings();
    console = new console::ConsoleWindow();
    root = new ChatOverlay();
    drawing = true;
    fade = new ScreenFade();

    // The help topics seen: "topic count" lines.
    if (std::string bytes; platform::ReadWholeFile(g_dataFolder + "config\\tutorial.ini", &bytes)) {
        std::vector<std::string> words;
        for (const std::string& line : str::Lines(bytes)) {
            words = text::SplitWords(line, ' ');
            if (words.size() <= 1) continue;
            helpTopics[words[0]] = str::ToInt(words[1]);
        }
    }
    fpsTime = platform::FrameClock();
}

// --- Resize ------------------------------------------------------------------------------------------

void Interface3D::Resize(const screen_layout::Layout& before) {
    width = renderer->screen.right;
    height = renderer->screen.bottom;
    MakeFonts(fonts, width);
    UiSurface::RelayoutAll(before);
    if (console) console->OnResize();
    // (A window's LayoutChanged may replace its children, never the stack.)
    for (const std::unique_ptr<UiContainer>& window : stack) window->LayoutChanged();
    RetakeBackdrops();
}

void Interface3D::RetakeBackdrops() {
    if (backdropMode != Backdrop::First && backdropMode != Backdrop::Both) return;
    const std::size_t windows = stack.size();
    std::vector<std::unique_ptr<UiContainer>> cut;   // the windows taken off the stack, the top one first
    // The stack cut back to its first `count` windows, or given back its windows up to that many.
    auto showFirst = [this, &cut](std::size_t count) {
        while (stack.size() > count) {
            cut.push_back(std::move(stack.back()));
            stack.pop_back();
        }
        while (stack.size() < count && !cut.empty()) {
            stack.push_back(std::move(cut.back()));
            cut.pop_back();
        }
    };
    std::unique_ptr<UiSurface> over;   // mode 4's copy, of the windows under a help window
    if (backdropMode == Backdrop::Both) {
        over = std::move(backdrop);
        backdrop = std::move(secondBackdrop);
    }
    backdropMode = Backdrop::None;
    if (backdrop) {   // the bottom window greyed, and the save copy (DrawOverlays' mode 1, into mode 2)
        showFirst(1);
        backdropMode = Backdrop::TakeFirst;
        app::FrameStep(false);
    }
    if (over) {   // the windows under the help window over that (mode 3, into mode 4)
        over.reset();
        showFirst(std::min(backdropWindows, windows));
        backdropMode = Backdrop::TakeSecond;
        app::FrameStep(false);
    }
    showFirst(windows);
}

Rect Interface3D::PictureRect() const {
    Rect picture{0, 0, width, height};
    if (fade && fade->letterbox) {
        const int bar = ScreenFade::LetterboxBar(height);
        picture.top = bar;
        picture.bottom = height - bar;
    }
    return picture;
}

bool Interface3D::EdgePanLive() const {
    if (stack.empty()) return false;
    for (const auto& input : stack.back()->inputs)
        if (input->PansAtEdges()) return true;
    return false;
}

// --- Shutdown ----------------------------------------------------------------------------------------

// The interface manager's end (Interface3D): the shutdown from the
// main loop's exit (it writes config\tutorial.ini), the destructor body of the global interface run at exit
// and the console window's destructor body. See ui.h and docs/notes/ui.md, section 3.

void Interface3D::Shutdown() {
    CloseAll();

    if (platform::File file; file.Open(g_dataFolder + "config\\tutorial.ini", platform::FileMode::Create)) {
        for (const auto& [topic, page] : helpTopics) file.WriteLine(str::Format("%s %d", topic, page));
    }

    fpsSurface.reset();
    backdrop.reset();
    secondBackdrop.reset();
    screenCopy.reset();
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
    delete fade;
    fade = nullptr;
    helpTopics.clear();
}

console::ConsoleWindow::~ConsoleWindow() {
    delete surface;
    surface = nullptr;
    font = UiFont{};
}

// --- Save --------------------------------------------------------------------------------------------

// Interface3D's saved state. See ui.h.

namespace {

// The save stream: reads `size` bytes; version is the save's version.
void Read(ByteStream* stream, void* to, int size) { stream->Read(to, static_cast<std::uint32_t>(size)); }

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
    if (stream->version < kVersionFastGame) return;
    stream->ReadValue(fastGame);
    if (stream->version < kVersionInterfaceList) return;
    // An obsolete list: each name and four-byte value read and dropped.
    for (int count = stream->ReadCount(); count > 0; --count) {
        stream->ReadString();
        stream->Skip(4);
    }
    messageText = stream->ReadString();
    if (stream->version >= kVersionMessageFlag) {
        stream->ReadValue(messageFlag);
        if (stream->version >= kVersionSavedText) savedText = stream->ReadString();
    }
}

// --- The multiplayer messages' texts -------------------------------------------------

char g_pauseMessageKey[12] = /*007930EC*/ {"reserved_02"};

std::string Interface3D::MessageText(int id, std::string_view value) {
    std::string key;
    if (static_cast<unsigned>(id) <= 12) key = kMessageKeys[id];
    key = g_clientWorld.texts->TextManager::GetString(key);
    return str::Format(key, value);
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

} // namespace

void Interface3D::OnKeyDown(platform::Key key, int scan, bool repeat) {
    console::ConsoleWindow* consoleWindow = console;
    switch (scan) {
    case 0x1D:
    case 0x11D:   // Ctrl
        if (repeat) return;
        ctrlDown = true;
        break;
    case 0x2A:
    case 0x36:    // Shift
        if (repeat) return;
        shiftDown = 1;
        break;
    case 0x38:
    case 0x138:   // Alt
        if (repeat) return;
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
        platform::File file;
        if (file.Open(name, platform::FileMode::Create)) picture->Write(&file);
        delete picture;
        return;
    }
    default:
        break;
    }

    if (consoleWindow->shown) {
        consoleWindow->OnKeyDown(key, scan, repeat);
        return;
    }
    UiEvent event{.kind = UiEventKind::KeyDown, .key = key, .scan = scan, .repeat = repeat};
    ChatOverlay* chat = root;
    if (IsMultiplayerClient() && chat->chatOpen) {
        chat->OnEvent(event);
        return;
    }
    if (!stack.empty()) stack.back()->OnEvent(event);
    if (!stack.empty() && !event.handled && KeyCharacter(scan) == ';') OpenHelp(true);
}

// Every depth clear goes to render::ClearDepth (docs/notes/renderer.md, sections 5 and 8).
void ClearUiDepth(float z, const Rect* rects, int count) {
    render::ClearDepth(z, rects, !rects ? 0 : count < 8 ? count : 8);
}

// --- Text layout ------------------------------------------------------------------------------------

std::vector<std::string> Interface3D::WrapText(std::string_view text, int maxWidth, int font) const {
    std::string ch, word, line;
    std::string source(text);
    source += "\n";
    std::vector<std::string> lines;
    // Words collect characters up to a space or newline; a word that would make the line wider
    // than the width starts a new line (a newline then also ends the word's line).
    // (Up to the first NUL, as the C string it was.)
    for (const char c : std::string_view(source).substr(0, source.find('\0'))) {
        ch.assign(1, c);
        if (c == '\n') {
            if (TextExtent(line + word, font).w > maxWidth) {
                lines.push_back(line);
                lines.push_back(word);
            } else {
                line += word;
                lines.push_back(line);
            }
            line.clear();
            word.clear();
        } else if (c == ' ') {
            if (TextExtent(line + word, font).w > maxWidth) {
                lines.push_back(line);
                line = word + " ";
            } else {
                line += word;
                line += " ";
            }
            word.clear();
        } else {
            word += ch;
        }
    }
    return lines;
}

int UiSurface::DrawWrappedText(std::string_view text, const Rect& area, int maxLines, const TextStyle& style) {
    const std::vector<std::string> lines = g_interface.WrapText(text, area.Width(), style.font);
    Rect at = area;
    int boxHeight = area.Height();
    const int count = static_cast<int>(lines.size());
    for (int i = 0; i < maxLines && i < count; ++i) {
        DrawTextIn(lines[i], at, style);
        at.Offset(0, boxHeight);
    }
    return count;
}

// --- TextExtent ---------------------------------------------------------------------------------------

Size Interface3D::TextExtent(std::string_view text, int font) const {
    const Size size = ::TextExtent(fonts[font], text);
    return screen_layout::TextExtentToLayout(size);
}
