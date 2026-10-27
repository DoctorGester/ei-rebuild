// A scrolling text's tokeniser (CUiScrollText::Tokenize): the message log and the objectives window's quest
// log hand it each text they add; and its drawing of the laid out lines (DrawLines); also the rest of the
// class (constructor, destructor body, texts, layout, drawing), the scroll bar's maximum and the tokens'
// destructor bodies. See ui.h and docs/notes/items_spells.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "lib/mfc_string.h"
#include "lib/mfc_slots.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr int kFormatType = 1, kWordType = 2;
constexpr COLORREF kDefaultColour = 0xA7D7E4;
constexpr COLORREF kConstructedColour = 0x1C8DC;     // the format token constructor's colour
constexpr int kDefaultFont = 1;
constexpr char kEscape = '\a';

SIZE TextExtent(CInterface3D* ui, const std::string& text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size;
}

void Append(UiTokenArray& tokens, UiTextToken* token) {
    int index = tokens.size;
    tokens.SetSize(index + 1, -1);
    tokens.data[index] = token;
}

bool IsFormat(const char* token) {
    return token[0] == '\n' || (token[0] == kEscape && (token[1] == 'C' || token[1] == 'F'));
}
} // namespace


int CUiScrollText::NextToken(const mfc::CString* text, int pos, mfc::CString* out) {
    int length = text->GetLength();
    const char* chars = *text;
    while (pos < length && (chars[pos] == ' ' || chars[pos] == '\t')) ++pos;
    if (pos >= length) return -1;
    // GetBuffer(0) on the text, as the original (a shared text gets its own copy).
    char* buffer = const_cast<mfc::CString*>(text)->GetBuffer(0);
    std::string rest = buffer + pos;
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
EI_REWRITTEN(0x0065D970, CUiScrollText::NextToken);


void CUiScrollText::Tokenize(const mfc::CString* text) {
    mfc::CString token;
    int font = kDefaultFont;
    COLORREF colour = kDefaultColour;
    int pos = 0;
    if (!(text->GetLength() > 0)) return;
    do {
        if (pos == -1) break;
        UiFormatToken* format = nullptr;
        bool word = false;
        pos = NextToken(text, pos, &token);
        // A run of format tokens shares one format token; the token after it is a word.
        while (pos != -1) {
            const char* t = token;
            if (token.GetLength() == 0) break;
            if (!IsFormat(t)) {
                word = true;
                break;
            }
            if (!format) {
                format = new UiFormatToken(font);
                // (A null token is written through, as in the original.)
                format->font_ = font;
                format->colour_ = colour;
                format->newline_ = false;
            }
            if (t[0] == '\n') {
                format->newline_ = true;
            } else if (t[1] == 'C') {
                colour = static_cast<unsigned char>(t[2]) | static_cast<unsigned char>(t[3]) << 8 |
                         static_cast<unsigned char>(t[4]) << 16;
                format->colour_ = colour;
            } else if (t[1] == 'F') {
                format->font_ = t[2] - '0';
                format->newline_ = tokens_.size != 0;
                font = t[2] - '0';
            }
            pos = NextToken(text, pos, &token);
        }
        if (format) Append(tokens_, format);
        if (word) {
            auto* w = new UiWordToken();
            SIZE size = TextExtent(interface_, token.GetString(), font);
            w->width_ = size.cx;
            w->text_ = token;
            Append(tokens_, w);
        }
    } while (pos < text->GetLength());
}
EI_REWRITTEN(0x0065D4B0, CUiScrollText::Tokenize);


namespace {
constexpr int kAlignLeft = 1;
const char* FontSampleText() { return "W"; }   // measured for each font's height
constexpr int kMeasuredFonts = 3;

// A word of a line (the surface's text call, Rebirth-patched).
void DrawWord(CUiSurface* surface, std::string_view text, RECT* rect, int font, COLORREF colour) {
    surface->DrawTextIn(text, rect, font, colour, 0, 1);
}
} // namespace

void CUiScrollText::DrawLines(int scroll) {
    CUiSurface* surface = surfaces_.size > 0 ? surfaces_.data[0] : nullptr;
    UiSurfaceGetDC(surface);
    surface->Clear();
    int lineCount = lines_.size;
    if (lineCount <= 0) {
        UiSurfaceReleaseDC(surface);
        return;
    }

    // The first line reaching `scroll`, and how far above the surface its top lies.
    int above = 0;
    int line = 0;
    while (line < lineCount && above < scroll) above += lines_.data[line++]->height_;
    --line;
    if (line < 0) line = 0;
    int offset = 0;
    if (scroll != 0) offset = above - lines_.data[line]->height_ - scroll;

    // The fonts' heights (the extent of the sample text in fonts 0-2).
    int heights[kMeasuredFonts];
    for (int font = 0; font < kMeasuredFonts; ++font) {
        std::string sample = FontSampleText();
        heights[font] = TextExtent(interface_, sample, font).cy;
    }

    // The format in effect at the line: the last format token before it (the first token
    // itself and token 0 are not looked at), else font 1 in the default colour.
    COLORREF colour = kDefaultColour;
    int font = kDefaultFont;
    int first = lines_.data[line]->firstToken_;
    if (tokens_.data[first]->type_ != kFormatType) {
        for (int k = first; k > 0; --k) {
            if (tokens_.data[k]->type_ == kFormatType) {
                auto* format = tokens_.data[k]->AsFormat();
                colour = format->colour_;
                font = format->font_;
                break;
            }
        }
    }

    RECT row = textRect_;
    row.top = offset;
    int drawn = 0;
    do {
        const UiTextLine* record = lines_.data[line];
        row.bottom = row.top + record->height_;
        int begin = record->firstToken_;
        int end = line == lines_.size - 1 ? tokens_.size : lines_.data[line + 1]->firstToken_;

        int wordsWidth = 0;
        for (int t = begin; t < end; ++t)
            if (tokens_.data[t]->type_ == kWordType) wordsWidth += tokens_.data[t]->AsWord()->width_;
        // Left aligned (a third of the font's height apart), or justified over the width. The
        // last token is read before the count is checked, as in the original. (A font above 2
        // reads past `heights`; the original reads its other stack slots there.)
        int gap;
        const UiTextToken* last = align_ != kAlignLeft ? tokens_.data[end - 1] : nullptr;
        if (align_ == kAlignLeft ||
            (last->type_ == kFormatType && last->AsFormat()->newline_) || end - begin < 2 ||
            end == tokens_.size)
            gap = heights[font] / 3;
        else
            gap = (textRect_.right - textRect_.left - wordsWidth) / (end - begin - 1);

        RECT word = row;
        for (int t = begin; t < end; ++t) {
            UiTextToken* token = tokens_.data[t];
            if (token->type_ == kFormatType) {
                auto* format = token->AsFormat();
                colour = format->colour_;
                font = format->font_;
            } else if (token->type_ == kWordType) {
                auto* w = token->AsWord();
                DrawWord(surface, w->text_, &word, font, colour);
                word.left += w->width_ + gap;
            }
        }
        int height = lines_.data[line]->height_;
        row.top += height;
        drawn += height;
        ++line;
    } while (drawn < textRect_.bottom - textRect_.top - offset && line < lines_.size);
    UiSurfaceReleaseDC(surface);
}
EI_REWRITTEN(0x0065D150, CUiScrollText::DrawLines);

// --- Construction, texts, layout ----------------------------------------------------------

namespace {
constexpr float kScrollSpeed = 20.0f;        // lines per second while an arrow is held
constexpr int kBarWidth = 14;                // the text's rectangle stops short of the bar
constexpr int kSurfaceFormat = 5;
constexpr int kJustified = 2;
constexpr int kScrollbarSprites = 4;
// The layout measures fonts 0-2; a format token may name any digit. The original reads its other
// stack slots (font 3: its return address) for the others; here they are 0.
constexpr int kFontSlots = 10;

void TokenizeText(CUiScrollText* self, const char* text) {
    mfc::CString copy(text);
    self->Tokenize(&copy);
}
void LayOut(CUiScrollText* self) { self->Layout(); }
} // namespace

// The scroll bar's tables with its own drawing and layout.
CUiScrollText::CUiScrollText(CInterface3D* ui, const char* text, RECT rect, float depth)
    : CUiScrollbar(ui, rect, depth, 0, kScrollSpeed) {
    tokens_.Construct(mfc::impl::TokenPtrArrayClass::Vtable());   // CArray<UiTextToken*>
    lines_.Construct(mfc::impl::LinePtrArrayClass::Vtable());   // CArray<UiTextLine*>
    AddSurface(new CUiSurface(interface_, &rect, kSurfaceFormat));
    CUiScrollbar::SetRect(&rect);
    PlaceText(rect);
    DeleteTokens();
    tokens_.SetSize(0, -1);
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(0);
    CUiScrollbar::SetPosition(0);
    align_ = kJustified;
}
EI_REWRITTEN_SPECIAL(0x0065CC00, CUiScrollText::CUiScrollText);

void CUiScrollText::DeleteTokens() {
    for (int i = 0; i < tokens_.size; ++i)
        if (UiTextToken* token = tokens_.data[i]) delete token;
}

void CUiScrollText::PlaceText(const RECT& rect) {
    if (surfaces_.size > 0 && surfaces_.data[0]) surfaces_.data[0]->MoveTo(rect.left, rect.top);
    textRect_ = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    textRect_.right -= kBarWidth;
    LayOut(this);
}

CUiScrollText::~CUiScrollText() {
    DeleteTokens();
    tokens_.SetSize(0, -1);
    for (int i = 0; i < lines_.size; ++i) gamecrt::operator_delete(lines_.data[i]);
    lines_.SetSize(0, -1);
    lines_.vtable = mfc::impl::LinePtrArrayClass::Vtable();
    lines_.FreeData();
    tokens_.vtable = mfc::impl::TokenPtrArrayClass::Vtable();
    tokens_.FreeData();
}
EI_REWRITTEN_SPECIAL(0x0065CE20, CUiScrollText::~CUiScrollText);
EI_REWRITTEN_SPECIAL(0x0065CE00, CUiScrollText::~CUiScrollText);   // the deleting destructor
EI_REWRITTEN_SPECIAL(0x0065E2B0, CUiScrollText::~CUiScrollText);   // the input side's

void CUiScrollText::AddText(const char* text) {
    SetPosition(maxPosition_);
    int position;
    moved_ = true;
    TakePosition(&position);
    int before = Maximum();
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(Maximum() - before + position);
}
EI_REWRITTEN(0x0065CF30, CUiScrollText::AddText);

void CUiScrollText::SetText(const char* text) {
    Clear();
    TokenizeText(this, text);
    LayOut(this);
    SetPosition(0);
}
EI_REWRITTEN(0x0065CFE0, CUiScrollText::SetText);

void CUiScrollText::SetRect(const RECT* rect) {
    CUiScrollbar::SetRect(rect);
    PlaceText(*rect);
}
EI_REWRITTEN(0x0065D0A0, CUiScrollText::SetRect);

BOOL CUiScrollText::Draw(CSceneRenderer* pass) {
    int position;
    if (TakePosition(&position)) DrawLines(position);
    return DrawBase(pass);
}
EI_REWRITTEN(0x0065D120, CUiScrollText::Draw);

void CUiScrollText::Clear() {
    DeleteTokens();
    tokens_.SetSize(0, -1);
}
EI_REWRITTEN(0x0065D450, CUiScrollText::Clear);

void CUiScrollText::Layout() {
    int total = 0;
    int width = textRect_.right - textRect_.left;
    int font = kDefaultFont;
    int heights[kFontSlots] = {};
    for (int f = 0; f < kMeasuredFonts; ++f) {
        std::string sample = FontSampleText();
        heights[f] = TextExtent(interface_, sample, f).cy;
    }
    for (int i = 0; i < lines_.size; ++i) gamecrt::operator_delete(lines_.data[i]);
    lines_.SetSize(0, -1);

    for (int t = 0; t < tokens_.size; ++t) {
        // (The original does not check the allocation.)
        auto* line = gamecrt::operator_new_as<UiTextLine>();
        int height = heights[font];
        line->firstToken_ = t;
        line->height_ = height;
        lines_.Add(line);
        int x = 0;
        for (; t < tokens_.size && x < width; ++t) {
            UiTextToken* token = tokens_.data[t];
            if (token->type_ == kFormatType) {
                auto* format = token->AsFormat();
                if (format->newline_) {
                    font = format->font_;
                    break;
                }
            } else if (token->type_ == kWordType) {
                x += token->AsWord()->width_ + height / 3;
                if (x >= width) {
                    if (line->firstToken_ != t) --t;   // the word starts the next line
                    break;
                }
            }
        }
        total += height;
    }
    SetMaximum(textRect_.top - textRect_.bottom + total);
}
EI_REWRITTEN(0x0065DAA0, CUiScrollText::Layout);

// --- The scroll bar's maximum ------------------------------------------------------------------

int CUiScrollbar::Maximum() const { return maxPosition_; }
EI_REWRITTEN(0x0065B720, CUiScrollbar::Maximum);

void CUiScrollbar::SetMaximum(int maximum) {
    if (maximum < 0) maximum = 0;
    maxPosition_ = maximum;
    moved_ = true;
    int visible = maximum != 0 ? 1 : 0;
    for (int i = 0; i < kScrollbarSprites; ++i) SceneMesh(i)->visible_ = visible;
    int position = position_;
    position_ = position - 1;
    SetPosition(position);
}
EI_REWRITTEN(0x0065AFE0, CUiScrollbar::SetMaximum);

// --- The tokens' destructors ---------------------------------------------------------------------

void* UiTextToken::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void UiTextToken::operator delete(void* block) { gamecrt::operator_delete(block); }

UiFormatToken::UiFormatToken(int font) : UiTextToken(kFormatType), newline_(false), font_(font), colour_(kConstructedColour) {}

UiTextToken::~UiTextToken() {}
EI_REWRITTEN_SPECIAL(0x0065D960, UiTextToken::~UiTextToken);
EI_REWRITTEN_SPECIAL(0x0065D8A0, UiTextToken::~UiTextToken);

UiFormatToken::~UiFormatToken() {}
EI_REWRITTEN_SPECIAL(0x0065D8E0, UiFormatToken::~UiFormatToken);
EI_REWRITTEN_SPECIAL(0x0065D8C0, UiFormatToken::~UiFormatToken);

UiWordToken::~UiWordToken() {}
EI_REWRITTEN_SPECIAL(0x0065D910, UiWordToken::~UiWordToken);
EI_REWRITTEN_SPECIAL(0x0065D8F0, UiWordToken::~UiWordToken);
