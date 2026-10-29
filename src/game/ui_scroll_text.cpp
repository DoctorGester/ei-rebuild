// A scrolling text's tokeniser (CUiScrollText::Tokenize): the message log and the objectives window's quest
// log hand it each text they add; and its drawing of the laid out lines (DrawLines); also the rest of the
// class (constructor, destructor body, texts, layout, drawing), the scroll bar's maximum and the tokens'
// destructor bodies. See ui.h and docs/notes/items_spells.md.
#include "ui.h"

#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/str.h"

#include <cstring>

namespace {
constexpr int kFormatType = 1, kWordType = 2;
constexpr COLORREF kDefaultColour = 0xA7D7E4;
constexpr COLORREF kConstructedColour = 0x1C8DC;     // the format token constructor's colour
constexpr int kDefaultFont = 1;
constexpr char kEscape = '\a';

void Append(std::vector<UiTextToken*>& tokens, UiTextToken* token) {
    int index = std::ssize(tokens);
    tokens.resize(index + 1);
    tokens[index] = token;
}

bool IsFormat(const char* token) {
    return token[0] == '\n' || (token[0] == kEscape && (token[1] == 'C' || token[1] == 'F'));
}

constexpr int kAlignLeft = 1;
const char* FontSampleText() { return "W"; }   // measured for each font's height
constexpr int kMeasuredFonts = 3;

// A word of a line (the surface's text call, Rebirth-patched).
void DrawWord(CUiSurface* surface, std::string_view text, RECT* rect, int font, COLORREF colour) {
    surface->DrawTextIn(text, rect, font, colour, 0, 1);
}

constexpr float kScrollSpeed = 20.0f;        // lines per second while an arrow is held
constexpr int kBarWidth = 14;                // the text's rectangle stops short of the bar
constexpr int kJustified = 2;
constexpr int kMaxStretch = 3;               // a justified line's gaps at most this many normal gaps
constexpr int kScrollbarSprites = 4;
// The layout measures fonts 0-2; a format token may name any digit. The original reads its other
// stack memory (font 3: its return address) for the others; here they are 0.
constexpr int kFontSlots = 10;

void TokenizeText(CUiScrollText* self, std::string_view text) {
    self->Tokenize(std::string(text));
}
void LayOut(CUiScrollText* self) { self->Layout(); }

} // namespace

int CUiScrollText::NextToken(const std::string& text, int pos, std::string* out) {
    int length = text.size();
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
            game::FatalError("ScrollText::Incorrect special symbol '%c'", static_cast<int>(r[1]));
            // Should the error come back, the original takes the text's data pointer as the count.
            count = static_cast<int>(reinterpret_cast<std::intptr_t>(r));
            if (count == -1) count = rest.size();
        }
    } else if (r[0] == '\n') {
        count = 1;
    } else {
        count = str::FindOneOf(rest, " \t\a\n");
        if (count == -1) count = rest.size();
    }
    *out = str::Left(rest, count);
    return pos + count;
}

void CUiScrollText::Tokenize(const std::string& text) {
    std::string token;
    int font = kDefaultFont;
    COLORREF colour = kDefaultColour;
    int pos = 0;
    if (text.empty()) return;
    do {
        if (pos == -1) break;
        UiFormatToken* format = nullptr;
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
            if (!format) {
                format = new UiFormatToken(font);
                // (A null token is written through, as in the original.)
                format->font = font;
                format->colour = colour;
                format->newline = false;
            }
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
        if (format) Append(tokens, format);
        if (word) {
            auto* w = new UiWordToken();
            SIZE size = ui->TextExtent(token, font);
            w->width = size.cx;
            w->text = token;
            Append(tokens, w);
        }
    } while (pos < static_cast<int>(text.size()));
}

void CUiScrollText::DrawLines(int scroll) {
    CUiSurface* surface = !surfaces.empty() ? surfaces[0] : nullptr;
    surface->Clear();
    int lineCount = std::ssize(lines);
    if (lineCount <= 0) {
        return;
    }

    // The first line reaching `scroll`, and how far above the surface its top lies.
    int above = 0;
    int line = 0;
    while (line < lineCount && above < scroll) above += lines[line++]->height;
    --line;
    if (line < 0) line = 0;
    int offset = 0;
    if (scroll != 0) offset = above - lines[line]->height - scroll;

    // The fonts' heights (the extent of the sample text in fonts 0-2).
    int heights[kMeasuredFonts];
    for (int font = 0; font < kMeasuredFonts; ++font) {
        std::string sample = FontSampleText();
        heights[font] = ui->TextExtent(sample, font).cy;
    }

    // The format in effect at the line: the last format token before it (the first token
    // itself and token 0 are not looked at), else font 1 in the default colour.
    COLORREF colour = kDefaultColour;
    int font = kDefaultFont;
    int first = lines[line]->firstToken;
    if (tokens[first]->type != kFormatType) {
        for (int k = first; k > 0; --k) {
            if (tokens[k]->type == kFormatType) {
                auto* format = tokens[k]->AsFormat();
                colour = format->colour;
                font = format->font;
                break;
            }
        }
    }

    RECT row = textRect;
    row.top = offset;
    int drawn = 0;
    do {
        const UiTextLine* record = lines[line];
        row.bottom = row.top + record->height;
        int begin = record->firstToken;
        int end = line == std::ssize(lines) - 1 ? std::ssize(tokens) : lines[line + 1]->firstToken;

        int wordsWidth = 0;
        int words = 0;
        for (int t = begin; t < end; ++t) {
            if (tokens[t]->type == kWordType) {
                wordsWidth += tokens[t]->AsWord()->width;
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
        const UiTextToken* last = tokens[end - 1];
        if (align != kAlignLeft && !(last->type == kFormatType && last->AsFormat()->newline) && words >= 2 &&
            end != std::ssize(tokens)) {
            const int stretched = (textRect.right - textRect.left - wordsWidth) / (words - 1);
            if (stretched <= kMaxStretch * normalGap) gap = stretched;
        }

        RECT word = row;
        for (int t = begin; t < end; ++t) {
            UiTextToken* token = tokens[t];
            if (token->type == kFormatType) {
                auto* format = token->AsFormat();
                colour = format->colour;
                font = format->font;
            } else if (token->type == kWordType) {
                auto* w = token->AsWord();
                DrawWord(surface, w->text, &word, font, colour);
                word.left += w->width + gap;
            }
        }
        int height = lines[line]->height;
        row.top += height;
        drawn += height;
        ++line;
    } while (drawn < textRect.bottom - textRect.top - offset && line < std::ssize(lines));
}

// --- Construction, texts, layout ----------------------------------------------------------

// The scroll bar's tables with its own drawing and layout.
CUiScrollText::CUiScrollText(CInterface3D* ownerUi, const char* text, RECT rect, float depth)
    : CUiScrollbar(ownerUi, rect, depth, 0, kScrollSpeed) {
    AddSurface(new CUiSurface(ui, &rect, SurfaceBlend::Keyed));
    CUiScrollbar::SetRect(&rect);
    PlaceText(rect);
    DeleteTokens();
    tokens.clear();
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(0);
    CUiScrollbar::SetPosition(0);
    align = kJustified;
}

void CUiScrollText::DeleteTokens() {
    for (int i = 0; i < std::ssize(tokens); ++i)
        if (UiTextToken* token = tokens[i]) delete token;
}

void CUiScrollText::PlaceText(const RECT& area) {
    if (!surfaces.empty() && surfaces[0]) surfaces[0]->MoveTo(area.left, area.top);
    textRect = {0, 0, area.right - area.left, area.bottom - area.top};
    textRect.right -= kBarWidth;
    LayOut(this);
}

CUiScrollText::~CUiScrollText() {
    DeleteTokens();
    tokens.clear();
    for (int i = 0; i < std::ssize(lines); ++i) delete lines[i];
    lines.clear();
}

void CUiScrollText::AddText(std::string_view text) {
    SetPosition(maxPosition);
    moved = true;
    const int scrollPosition = TakePosition();
    int before = maxPosition;
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(maxPosition - before + scrollPosition);
}

void CUiScrollText::SetText(std::string_view text) {
    Clear();
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(0);
}

void CUiScrollText::SetRect(const RECT* newRect) {
    CUiScrollbar::SetRect(newRect);
    PlaceText(*newRect);
}

void CUiScrollText::Draw(CSceneRenderer* pass) {
    if (TakeMoved()) DrawLines(Position());
    DrawBase(pass);
}

void CUiScrollText::Clear() {
    DeleteTokens();
    tokens.clear();
}

void CUiScrollText::Layout() {
    int total = 0;
    int width = textRect.right - textRect.left;
    int font = kDefaultFont;
    int heights[kFontSlots] = {};
    for (int f = 0; f < kMeasuredFonts; ++f) {
        std::string sample = FontSampleText();
        heights[f] = ui->TextExtent(sample, f).cy;
    }
    for (int i = 0; i < std::ssize(lines); ++i) delete lines[i];
    lines.clear();

    for (int t = 0; t < std::ssize(tokens); ++t) {
        // (The original does not check the allocation.)
        auto* line = new UiTextLine;
        int height = heights[font];
        line->firstToken = t;
        line->height = height;
        lines.push_back(line);
        int x = 0;
        for (; t < std::ssize(tokens) && x < width; ++t) {
            UiTextToken* token = tokens[t];
            if (token->type == kFormatType) {
                auto* format = token->AsFormat();
                if (format->newline) {
                    font = format->font;
                    break;
                }
            } else if (token->type == kWordType) {
                x += token->AsWord()->width + height / 3;
                if (x >= width) {
                    if (line->firstToken != t) --t;    // the word starts the next line
                    break;
                }
            }
        }
        total += height;
    }
    SetMaximum(textRect.top - textRect.bottom + total);
}

// --- The scroll bar's maximum ------------------------------------------------------------------


void CUiScrollbar::SetMaximum(int maximum) {
    if (maximum < 0) maximum = 0;
    maxPosition = maximum;
    moved = true;
    int visible = maximum != 0 ? 1 : 0;
    for (int i = 0; i < kScrollbarSprites; ++i) SceneMesh(i)->visible = visible;
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}

// --- The tokens' destructors ---------------------------------------------------------------------


UiFormatToken::UiFormatToken(int newFont) : UiTextToken(kFormatType), newline(false), font(newFont), colour(kConstructedColour) {}

UiTextToken::~UiTextToken() {}

UiFormatToken::~UiFormatToken() {}

UiWordToken::~UiWordToken() {}
