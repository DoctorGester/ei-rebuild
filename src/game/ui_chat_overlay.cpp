// The in-game chat overlay: its constructor, per-frame update and redraw, the chat line's opening and its
// keys; the chat line's destructor. docs/notes/ui.md, section 3.
#include "asset_text.h"
#include "console.h"
#include "net_classes.h"
#include "net_messages.h"
#include "ui.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/rebirth.h"
#include "lib/containers.h"
#include "platform/clock.h"
#include "lib/str.h"
#include "lib/win_user.h"
#include "platform/keys.h"

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
    auto* say = net::NewCounted<net::SayMessage>();
    // A failed allocation is not checked from here on.
    say->text = text;
    net::g_gameClient->AddTimed(say, kSayLifetime);
}

constexpr int kMaxLines = 12;
constexpr int kLineWidth = 0x30C;
constexpr int kLineFont = 1;
constexpr int kLineSurfaceHeight = 0x11, kLineSurfacePad = 3;
constexpr int kLineDrawRight = 0x30F;
constexpr UINT kLineDrawFormat = 0x800;   // DT_NOPREFIX
constexpr COLORREF kWhite = 0xFFFFFF;
const char* const kColourMark = "^";
const char* const kDefaultColour = "7";

// The colour of "^<digit>": bit 0 blue, bit 1 green, bit 2 red (COLORREF 0x00BBGGRR).
COLORREF MarkColour(char digit) {
    int bits = static_cast<signed char>(digit) - '0';
    return ((bits & 1) ? 0xFF0000u : 0u) | ((bits & 2) ? 0xFF00u : 0u) | ((bits & 4) ? 0xFFu : 0u);
}

void DrawPiece(UiSurface* surface, std::string_view text, int x, COLORREF colour) {
    RECT rect = {x, 0, kLineDrawRight, kLineSurfaceHeight};
    surface->DrawTextIn(text, &rect, kLineFont, colour, kLineDrawFormat, 1);
}

constexpr int kTypedTextRight = 0x30F, kTypedTextBottom = 0x11;
constexpr int kCaretBottom = 0x10;
constexpr COLORREF kTypedColour = 0xFFFFFF;

} // namespace

// --- a chat line added ---------------------------------------------------------------------------

void ChatOverlay::AddLine(const char* text) {
    const int before = static_cast<int>(lines.size());
    std::vector<std::string> plain;       // the lines without colour codes
    std::vector<std::string> formatted;   // the same lines with them
    std::string source = text;
    std::string ch, word, wordFormatted, line, lineFormatted;
    bool mark = false;
    // A character is its whole UTF-8 sequence, so a word wider than a line is broken between characters and
    // measured as text.
    for (const char* p = source.c_str(); *p != 0; p += text::CharacterLength(p)) {
        ch.assign(p, static_cast<std::size_t>(text::CharacterLength(p)));
        const char c = *p;
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
            if (ui->TextExtent(line + word + ch, kLineFont).cx > kLineWidth) {
                if (line.size() != 0) {
                    plain.push_back(line);
                    formatted.push_back(lineFormatted);
                }
                line.clear();
                lineFormatted.clear();
                if (ui->TextExtent(word + ch, kLineFont).cx > kLineWidth) {
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

    std::uint32_t now = platform::Milliseconds();
    COLORREF colour = kWhite;
    for (int i = 0; i < count; ++i) {
        ChatLine& added = lines.emplace_back();
        added.name = plain[i];
        added.time = now;
        const int width = ui->TextExtent(plain[i], kLineFont).cx + kLineSurfacePad;
        added.surface = std::make_unique<UiSurface>(ui, 0, 0, width, kLineSurfaceHeight, SurfaceBlend::Keyed, true);
        UiSurface* surface = added.surface.get();
        surface->Clear();
        const std::string& codes = formatted[i];
        std::string drawn;
        std::string piece;
        int x = 0;
        int start = 0;
        for (int at = str::Find(codes, kColourMark, 0); at != -1; at = str::Find(codes, kColourMark, start)) {
            piece = str::Mid(codes, start, at - start);
            DrawPiece(surface, piece, x, colour);
            drawn += piece;
            x = ui->TextExtent(drawn, kLineFont).cx;
            colour = MarkColour(codes[at + 1]);
            start = at + 2;
        }
        piece = str::Mid(codes, start);
        if (piece.size() != 0) DrawPiece(surface, piece, x, colour);
    }
}

// --- opening the chat line, clearing the lines ---------------------------------------------------

void ChatOverlay::SetOpen(bool open) {
    chatOpen = open;
    if (open) {
        typed.clear();
        caret = 0;
    }
    DrawTyped();
}

void ChatOverlay::ClearLines() {
    lines.clear();
}

// --- the typed line drawn (Rebirth-patched: WideScreen reproduced) -------------------------------

void ChatOverlay::DrawTyped() {
    if (!chatOpen) {
        surfaces[0]->Hide(false);
        return;
    }
    surfaces[0]->Show();
    if (caret > static_cast<int>(typed.size())) caret = lib::Count(typed);
    if (caret < 0) caret = 0;
    UiSurface* surface = surfaces[0];
    surface->Clear();
    RECT area{0, 0, kTypedTextRight, kTypedTextBottom};
    surface->DrawTextIn(typed, &area, kTypedFont, kTypedColour, DT_NOPREFIX, 1);
    int width = ui->TextExtent(str::Left(typed, caret), kTypedFont).cx;
    if (width < kMaxTypedWidth) {
        POINT top{width, 0};
        POINT bottom{width, kCaretBottom};
        // WideScreen: without the margin.
        ui->CallScalePointToScreenNoMargin(&top);
        ui->CallScalePointToScreenNoMargin(&bottom);
        surface->Line(top.x, top.y, bottom.x, bottom.y, 1, kTypedColour);
    }
}

// --- the chat line's keys -------------------------------------------------------------------------

void ChatOverlay::KeyDown(int key, int, int) {
    if (!chatOpen) return;
    handled = true;
    switch (key) {
    case platform::kKeyBack: {   // by character, as Delete and the caret's steps
        if (caret <= 0) break;
        const int previous = text::PreviousCharacter(typed, caret);
        typed = str::Left(typed, previous) + str::Mid(typed, caret);
        caret = previous;
        break;
    }
    case platform::kKeyReturn:
        if (typed.size() != 0 && IsMultiplayerClient()) SendSay(typed);
        chatOpen = false;
        break;
    case platform::kKeyEscape:
        chatOpen = false;
        break;
    case platform::kKeyEnd:
        caret = lib::Count(typed);
        break;
    case platform::kKeyHome:
        caret = 0;
        break;
    case platform::kKeyLeft:
        caret = text::PreviousCharacter(typed, caret);
        break;
    case platform::kKeyRight:
        caret = text::NextCharacter(typed, caret);
        break;
    case platform::kKeyDelete:
        if (caret >= static_cast<int>(typed.size())) break;
        typed = str::Left(typed, caret) + str::Mid(typed, text::NextCharacter(typed, caret));
        break;
    default:
        return;
    }
    DrawTyped();
}

void ChatOverlay::Char(int key, int, int) {
    if (!chatOpen) return;
    if (key == platform::kKeyBack || key == platform::kKeyTab || key == platform::kKeyReturn || key == platform::kKeyEscape) return;
    std::string line = text::Typed(str::Left(typed, caret), key) + str::Mid(typed, caret);
    // The line also stays within kMaxTyped bytes (what 200 one-byte characters took).
    // That is what the sent SayMessage fits in: a client's update carries 220 bytes, and a message larger
    // than that would never be counted in one.
    if (!ui->ctrlDown && text::CharacterCount(typed) < kMaxTyped && static_cast<int>(line.size()) <= kMaxTyped &&
        ui->TextExtent(line, kTypedFont).cx < kMaxTypedWidth) {
        typed = line;
        caret += text::TypedLength(key);
    }
    DrawTyped();
}

// --- the constructor -----------------------------------------------------------------------------

// Its own redraw and update; the input side's keys and characters.
ChatOverlay::ChatOverlay(Interface3D* ownerUi) : UiWindow(ownerUi) {
    UiSurface* newTyped = new UiSurface(ui, kTypedLeft, kTypedTop, kTypedRight, kTypedBottom, SurfaceBlend::Opaque, true);
    AddSurface(newTyped);
    chatOpen = false;
    caret = 0;
    DrawTyped();
}

void ChatOverlay::Update() {
    std::uint32_t now = platform::Milliseconds();
    std::erase_if(lines, [now](const ChatLine& line) { return static_cast<int>(now - line.time) > kLineLifetime; });
}

void ChatOverlay::Redraw() {
    int y = kTop;
    for (const ChatLine& line : lines) {
        line.surface->MoveTo(kLeft, y);
        line.surface->Blit();
        y += kLineHeight;
    }
    UiWindow::Redraw();
}
