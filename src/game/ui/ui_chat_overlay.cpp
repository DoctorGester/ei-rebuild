// The in-game chat overlay: its constructor, per-frame update and redraw, the lines' and the typed line's
// paints, the chat line's opening and its keys. docs/notes/ui.md, section 3.
#include "engine/core/asset_text.h"
#include "game/console/console.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/ui/ui.h"
#include "game/ui/ui_chat_overlay.h"
#include "game/ui/ui_screens.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "render/text_format.h"
#include "game/ui/screen_layout.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
namespace {
constexpr int kLineLifetime = 20000;   // ms
constexpr int kLeft = 10;
constexpr int kTop = 10;
constexpr int kLineHeight = 16;
// The typed line's surface.
constexpr int kTypedLeft = 10, kTypedTop = 0x23D, kTypedRight = 0x316, kTypedBottom = 0x24E;
// The typed line: at most 200 characters and narrower than 780 pixels in font 1.
constexpr int kMaxTyped = 200;
constexpr int kMaxTypedWidth = 0x30C;
constexpr int kTypedFont = 1;
// A chat message stays on the client's timers list for 60 s (the float's bits).
constexpr std::uint32_t kSayLifetime = 0x42700000;


// A chat line for the server (SayMessage) carrying `text`, kept 60 s on the client's timers.
void SendSay(const std::string& text) {
    auto* say = new net::SayMessage();
    // A failed allocation is not checked from here on.
    say->text = text;
    net::g_gameClient->AddTimed(say, kSayLifetime);
}

constexpr int kMaxLines = 12;
constexpr int kLineWidth = 0x30C;
constexpr int kLineFont = 1;
constexpr int kLineSurfaceHeight = 0x11, kLineSurfacePad = 3;
constexpr int kLineDrawRight = 0x30F;
constexpr render::text::Format kLineDrawFormat = render::text::Format::NoPrefix;
constexpr std::uint32_t kWhite = 0xFFFFFF;
const char* const kColourMark = "^";
const char* const kDefaultColour = "7";

// The colour of "^<digit>": bit 0 blue, bit 1 green, bit 2 red (COLORREF 0x00BBGGRR).
std::uint32_t MarkColour(char digit) {
    int bits = static_cast<signed char>(digit) - '0';
    return ((bits & 1) ? 0xFF0000u : 0u) | ((bits & 2) ? 0xFF00u : 0u) | ((bits & 4) ? 0xFFu : 0u);
}

// A line's piece in one colour, `x` pixels from its start.
struct ChatPiece {
    std::string text;
    int x = 0;
    std::uint32_t colour = 0;
};

void PaintPieces(UiSurface& surface, const std::vector<ChatPiece>& pieces) {
    for (const ChatPiece& piece : pieces) {
        Rect rect = {piece.x, 0, kLineDrawRight, kLineSurfaceHeight};
        surface.DrawTextIn(piece.text, rect, TextStyle{kLineFont, piece.colour, kLineDrawFormat});
    }
}

constexpr int kTypedTextRight = 0x30F, kTypedTextBottom = 0x11;
constexpr int kCaretBottom = 0x10;
constexpr std::uint32_t kTypedColour = 0xFFFFFF;

} // namespace

// --- a chat line added ---------------------------------------------------------------------------

void ChatOverlay::AddLine(std::string_view text) {
    const int before = static_cast<int>(lines.size());
    std::vector<std::string> plain;       // the lines without colour codes
    std::vector<std::string> formatted;   // the same lines with them
    const std::string_view source = text.substr(0, text.find('\0'));   // up to the first NUL, as a C string
    std::string ch, word, wordFormatted, line, lineFormatted;
    bool mark = false;
    // A character is its whole UTF-8 sequence, so a word wider than a line is broken between characters and
    // measured as text.
    for (std::size_t p = 0, length = 0; p < source.size(); p += length) {
        length = static_cast<std::size_t>(text::CharacterLength(source.substr(p)));
        ch.assign(source.substr(p, length));
        const char c = source[p];
        if (mark) {
            if (c >= '0' && c < '8')
                wordFormatted += ch;
            else
                wordFormatted += kDefaultColour;
            mark = false;
        } else if (c == ' ') {
            word += ch;
            line += word;
            word.clear();
            wordFormatted += ch;
            lineFormatted += wordFormatted;
            wordFormatted.clear();
        } else if (c == '^') {
            wordFormatted += ch;
            mark = true;
        } else {
            if (g_interface.TextExtent(line + word + ch, kLineFont).w > kLineWidth) {
                if (line.size() != 0) {
                    plain.push_back(line);
                    formatted.push_back(lineFormatted);
                }
                line.clear();
                lineFormatted.clear();
                if (g_interface.TextExtent(word + ch, kLineFont).w > kLineWidth) {
                    if (word.size() != 0) {
                        plain.push_back(word);
                        formatted.push_back(wordFormatted);
                    }
                    word.clear();
                    wordFormatted.clear();
                }
            }
            word += ch;
            wordFormatted += ch;
        }
    }
    if (mark) wordFormatted += kDefaultColour;
    if (line.size() != 0 || word.size() != 0) {
        plain.push_back(line + word);
        formatted.push_back(lineFormatted + wordFormatted);
    }
    const int count = static_cast<int>(plain.size());

    // The oldest lines go.
    lines.erase(lines.begin(), lines.begin() + std::clamp(count + before - kMaxLines, 0, before));

    std::uint32_t now = platform::FrameClock();
    std::uint32_t colour = kWhite;
    for (int i = 0; i < count; ++i) {
        ChatLine& added = lines.emplace_back();
        added.name = plain[i];
        added.time = now;
        const int width = g_interface.TextExtent(plain[i], kLineFont).w + kLineSurfacePad;
        added.surface = std::make_unique<UiSurface>(0, 0, width, kLineSurfaceHeight, SurfaceBlend::Keyed, true);
        // The pieces are measured here; a line does not change, so its paint keeps them.
        const std::string& codes = formatted[i];
        std::vector<ChatPiece> pieces;
        std::string drawn;
        std::string piece;
        int x = 0;
        int start = 0;
        for (int at = str::Find(codes, kColourMark, 0); at != -1; at = str::Find(codes, kColourMark, start)) {
            piece = str::Mid(codes, start, at - start);
            pieces.push_back({piece, x, colour});
            drawn += piece;
            x = g_interface.TextExtent(drawn, kLineFont).w;
            colour = MarkColour(codes[at + 1]);
            start = at + 2;
        }
        piece = str::Mid(codes, start);
        if (piece.size() != 0) pieces.push_back({piece, x, colour});
        added.surface->PaintWith([pieces = std::move(pieces)](UiSurface& out) { PaintPieces(out, pieces); });
    }
}

// --- opening the chat line, clearing the lines ---------------------------------------------------

void ChatOverlay::SetOpen(bool open) {
    chatOpen = open;
    if (open) {
        typed.clear();
        caret = 0;
    }
    ShowTyped();
}

void ChatOverlay::ClearLines() {
    lines.clear();
}

// --- the typed line shown and painted (Rebirth-patched: WideScreen reproduced) --------------------

void ChatOverlay::ShowTyped() {
    if (!chatOpen) {
        surfaces[0]->Hide();
        return;
    }
    surfaces[0]->Show();
    if (caret > static_cast<int>(typed.size())) caret = lib::Count(typed);
    if (caret < 0) caret = 0;
}

void ChatOverlay::PaintTyped(UiSurface& surface) const {
    surface.Clear();
    Rect area{0, 0, kTypedTextRight, kTypedTextBottom};
    surface.DrawTextIn(typed, area, TextStyle{kTypedFont, kTypedColour, render::text::Format::NoPrefix});
    int width = g_interface.TextExtent(str::Left(typed, caret), kTypedFont).w;
    if (width < kMaxTypedWidth) {
        Point top{width, 0};
        Point bottom{width, kCaretBottom};
        // WideScreen: without the margin.
        top = screen_layout::ToScreenNoMargin(top);
        bottom = screen_layout::ToScreenNoMargin(bottom);
        surface.Line(top, bottom, 1, kTypedColour);
    }
}

// --- the chat line's keys -------------------------------------------------------------------------

void ChatOverlay::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::Char: Char(event); break;
    default: break;
    }
}

void ChatOverlay::KeyDown(UiEvent& event) {
    if (!chatOpen) return;
    event.handled = true;
    switch (event.key) {
    case platform::Key::Backspace: {   // by character, as Delete and the caret's steps
        if (caret <= 0) break;
        const int previous = text::PreviousCharacter(typed, caret);
        typed = str::Left(typed, previous) + str::Mid(typed, caret);
        caret = previous;
        break;
    }
    case platform::Key::Return:
        if (typed.size() != 0 && IsMultiplayerClient()) SendSay(typed);
        chatOpen = false;
        break;
    case platform::Key::Escape:
        chatOpen = false;
        break;
    case platform::Key::End:
        caret = lib::Count(typed);
        break;
    case platform::Key::Home:
        caret = 0;
        break;
    case platform::Key::Left:
        caret = text::PreviousCharacter(typed, caret);
        break;
    case platform::Key::Right:
        caret = text::NextCharacter(typed, caret);
        break;
    case platform::Key::Delete:
        if (caret >= static_cast<int>(typed.size())) break;
        typed = str::Left(typed, caret) + str::Mid(typed, text::NextCharacter(typed, caret));
        break;
    default:
        return;
    }
    ShowTyped();
}

void ChatOverlay::Char(UiEvent& event) {
    if (!chatOpen) return;
    const int character = event.character;
    if (character == '\b' || character == '\t' || character == '\r' || character == '\x1B') return;
    std::string line = text::Typed(str::Left(typed, caret), character) + str::Mid(typed, caret);
    // The line also stays within kMaxTyped bytes (what 200 one-byte characters took).
    // That is what the sent SayMessage fits in: a client's update carries 220 bytes, and a message larger
    // than that would never be counted in one.
    if (!g_interface.ctrlDown && text::CharacterCount(typed) < kMaxTyped && static_cast<int>(line.size()) <= kMaxTyped &&
        g_interface.TextExtent(line, kTypedFont).w < kMaxTypedWidth) {
        typed = line;
        caret += text::TypedLength(character);
    }
    ShowTyped();
}

// --- the constructor -----------------------------------------------------------------------------

// Its own redraw and update; its keys and characters.
ChatOverlay::ChatOverlay() {
    UiSurface* newTyped = AddSurface(std::make_unique<UiSurface>(kTypedLeft, kTypedTop, kTypedRight, kTypedBottom, SurfaceBlend::Opaque, true));
    newTyped->PaintWith([this](UiSurface& out) { PaintTyped(out); });
    ShowTyped();
}

void ChatOverlay::Update() {
    std::uint32_t now = platform::FrameClock();
    std::erase_if(lines, [now](const ChatLine& line) { return static_cast<int>(now - line.time) > kLineLifetime; });
}

void ChatOverlay::DrawSurfaces() {
    int y = kTop;
    for (const ChatLine& line : lines) {
        line.surface->MoveTo({kLeft, y});
        line.surface->Blit();
        y += kLineHeight;
    }
    UiWindow::DrawSurfaces();
}
