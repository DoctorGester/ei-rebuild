// A scrolling text's tokeniser (UiScrollText::Tokenize): the message log and the objectives window's quest
// log hand it each text they add; and the placing of the laid out lines' words (PlaceWords) that its paint draws;
// also the rest of the class (constructor, texts, layout, drawing) and the scroll bar's maximum. See ui_scroll_text.h and
// docs/notes/ui.md, section 8.
#include "game/ui/ui.h"
#include "game/ui/ui_scroll_text.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/str.h"

#include <cstring>
#include <optional>

namespace {
constexpr int kDefaultFont = 1;
constexpr char kEscape = '\a';

bool IsFormat(const char* token) {
    return token[0] == '\n' || (token[0] == kEscape && (token[1] == 'C' || token[1] == 'F'));
}

constexpr const char* kFontSampleText = "W";   // measured for each font's height
constexpr int kMeasuredFonts = 3;

constexpr float kScrollSpeed = 20.0f;        // lines per second while an arrow is held
constexpr int kBarWidth = 14;                // the text's rectangle stops short of the bar
constexpr int kMaxStretch = 3;               // a justified line's gaps at most this many normal gaps
// The layout measures fonts 0-2; a format token may name any digit. The original reads its other
// stack memory (font 3: its return address) for the others; here they are 0.
constexpr int kFontSlots = 10;

} // namespace

int UiScrollText::NextToken(const std::string& text, int pos, std::string* out) {
    int length = lib::Count(text);
    const char* chars = text.c_str();
    while (pos < length && (chars[pos] == ' ' || chars[pos] == '\t')) ++pos;
    if (pos >= length) return -1;
    std::string rest = chars + pos;
    const char* r = rest.c_str();
    int count;
    if (r[0] == kEscape) {
        if (r[1] == 'F') {
            count = 3;
        } else if (r[1] == 'C') {
            count = 5;
        } else {
            // An unknown escape (a text's data error): the token is the rest of the text, as with Rebirth (the
            // original read past the end).
            game::Warning("ScrollText: incorrect special symbol '%c'", static_cast<int>(r[1]));
            count = lib::Count(rest);
        }
    } else if (r[0] == '\n') {
        count = 1;
    } else {
        count = str::FindOneOf(rest, " \t\a\n");
        if (count == -1) count = lib::Count(rest);
    }
    *out = str::Left(rest, count);
    return pos + count;
}

void UiScrollText::Tokenize(const std::string& text) {
    std::string token;
    int font = kDefaultFont;
    std::uint32_t colour = kUiTextColour;
    int pos = 0;
    if (text.empty()) return;
    do {
        if (pos == -1) break;
        std::optional<TextToken> format;
        bool word = false;
        pos = NextToken(text, pos, &token);
        // A run of format tokens shares one format token; the token after it is a word.
        while (pos != -1) {
            const char* t = token.c_str();
            if (token.size() == 0) break;
            if (!IsFormat(t)) {
                word = true;
                break;
            }
            if (!format) format = TextToken{.kind = TokenKind::Format, .font = font, .colour = colour};
            if (t[0] == '\n') {
                format->newline = true;
            } else if (t[1] == 'C') {
                colour = static_cast<unsigned char>(t[2]) | static_cast<unsigned char>(t[3]) << 8 |
                         static_cast<unsigned char>(t[4]) << 16;
                format->colour = colour;
            } else if (t[1] == 'F') {
                format->font = t[2] - '0';
                format->newline = !tokens.empty();
                font = t[2] - '0';
            }
            pos = NextToken(text, pos, &token);
        }
        if (format) tokens.push_back(std::move(*format));
        if (word) tokens.push_back(TextToken{.text = token, .width = g_interface.TextExtent(token, font).w});
    } while (pos < static_cast<int>(text.size()));
}

void UiScrollText::PlaceWords(int scroll) {
    shownWords.clear();
    int lineCount = lib::Count(lines);
    if (lineCount <= 0) {
        return;
    }

    // The first line reaching `scroll`, and how far above the surface its top lies.
    int above = 0;
    int line = 0;
    while (line < lineCount && above < scroll) above += lines[line++].height;
    --line;
    if (line < 0) line = 0;
    int offset = 0;
    if (scroll != 0) offset = above - lines[line].height - scroll;

    // The fonts' heights (the extent of the sample text in fonts 0-2).
    int heights[kMeasuredFonts];
    for (int font = 0; font < kMeasuredFonts; ++font) {
        std::string sample = kFontSampleText;
        heights[font] = g_interface.TextExtent(sample, font).h;
    }

    // The format in effect at the line: the last format token before it (the first token
    // itself and token 0 are not looked at), else font 1 in the default colour.
    std::uint32_t colour = kUiTextColour;
    int font = kDefaultFont;
    int first = lines[line].firstToken;
    if (tokens[first].kind != TokenKind::Format) {
        for (int k = first; k > 0; --k) {
            if (tokens[k].kind == TokenKind::Format) {
                colour = tokens[k].colour;
                font = tokens[k].font;
                break;
            }
        }
    }

    Rect row = textRect;
    row.top = offset;
    int drawn = 0;
    do {
        const UiTextLine& record = lines[line];
        row.bottom = row.top + record.height;
        int begin = record.firstToken;
        int end = line == lib::Count(lines) - 1 ? lib::Count(tokens) : lines[line + 1].firstToken;

        int wordsWidth = 0;
        int words = 0;
        for (int t = begin; t < end; ++t) {
            if (tokens[t].kind == TokenKind::Word) {
                wordsWidth += tokens[t].width;
                ++words;
            }
        }
        // Left aligned (a third of the font's height apart), or justified over the width: the room
        // the words leave shared between the gaps between words (the original divided by the line's
        // tokens less one, format tokens too, so a line holding one fell short of the right edge). A
        // line ending a paragraph or the text, of one word, or whose gaps would stretch past
        // kMaxStretch normal gaps stays left aligned (the original stretched every line, a line of
        // three long words to gaps of eight normal ones). (A font above 2 reads past `heights`; the
        // original reads its other stack memory there.)
        const int normalGap = heights[font] / 3;
        int gap = normalGap;
        const TextToken& last = tokens[end - 1];
        if (align != kAlignLeft && !(last.kind == TokenKind::Format && last.newline) && words >= 2 &&
            end != std::ssize(tokens)) {
            const int stretched = (textRect.Width() - wordsWidth) / (words - 1);
            if (stretched <= kMaxStretch * normalGap) gap = stretched;
        }

        Rect word = row;
        for (int t = begin; t < end; ++t) {
            const TextToken& token = tokens[t];
            if (token.kind == TokenKind::Format) {
                colour = token.colour;
                font = token.font;
            } else {
                shownWords.push_back({token.text, word, font, colour});
                word.left += token.width + gap;
            }
        }
        int height = lines[line].height;
        row.top += height;
        drawn += height;
        ++line;
    } while (drawn < textRect.Height() - offset && line < std::ssize(lines));
}

// Each placed word through the surface's text call (Rebirth-patched).
void UiScrollText::PaintWords(UiSurface& surface) const {
    for (const ShownWord& shown : shownWords) {
        Rect area = shown.rect;
        surface.DrawTextIn(shown.text, area, TextStyle{shown.font, shown.colour});
    }
}

// --- Construction, texts, layout ----------------------------------------------------------

UiScrollText::UiScrollText(const char* text, Rect area, float depth)
    : bar(area, depth, 0, kScrollSpeed) {
    rect = area;
    AddSurface(std::make_unique<UiSurface>(&area, SurfaceBlend::Keyed))
        ->PaintWith([this](UiSurface& out) { PaintWords(out); });
    bar.SetRect(area);
    PlaceText(area);
    Tokenize(std::string(text));
    Layout();
    bar.SetPosition(0);
}

void UiScrollText::PlaceText(const Rect& area) {
    if (!surfaces.empty() && surfaces[0]) surfaces[0]->MoveTo(area.TopLeft());
    textRect = {0, 0, area.Width(), area.Height()};
    textRect.right -= kBarWidth;
    Layout();
}

void UiScrollText::AddText(std::string_view text) {
    bar.SetPosition(bar.maxPosition);
    bar.moved = true;
    const int scrollPosition = bar.TakePosition();
    int before = bar.maxPosition;
    Tokenize(std::string(text));
    Layout();
    bar.SetPosition(bar.maxPosition - before + scrollPosition);
}

void UiScrollText::SetText(std::string_view text) {
    Clear();
    Tokenize(std::string(text));
    Layout();
    bar.SetPosition(0);
}

void UiScrollText::SetRect(const Rect& newRect) {
    rect = newRect;
    bar.SetRect(newRect);
    PlaceText(newRect);
}

void UiScrollText::Update() { bar.Update(); }

void UiScrollText::OnEvent(UiEvent& event) { bar.OnEvent(event); }

void UiScrollText::DrawSprites(SceneRenderer* pass) {
    if (bar.TakeMoved()) PlaceWords(bar.Position());
    bar.DrawSprites(pass);
}

void UiScrollText::Clear() { tokens.clear(); }

void UiScrollText::Layout() {
    int total = 0;
    int width = textRect.Width();
    int font = kDefaultFont;
    int heights[kFontSlots] = {};
    for (int f = 0; f < kMeasuredFonts; ++f) {
        std::string sample = kFontSampleText;
        heights[f] = g_interface.TextExtent(sample, f).h;
    }
    lines.clear();

    for (int t = 0; t < std::ssize(tokens); ++t) {
        const int height = heights[font];
        const int firstToken = t;
        lines.push_back({height, firstToken});
        int x = 0;
        for (; t < std::ssize(tokens) && x < width; ++t) {
            const TextToken& token = tokens[t];
            if (token.kind == TokenKind::Format) {
                if (token.newline) {
                    font = token.font;
                    break;
                }
            } else {
                x += token.width + height / 3;
                if (x >= width) {
                    if (firstToken != t) --t;    // the word starts the next line
                    break;
                }
            }
        }
        total += height;
    }
    bar.SetMaximum(textRect.top - textRect.bottom + total);
}
