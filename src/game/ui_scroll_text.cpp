// A scrolling text's tokeniser (CUiScrollText::Tokenize, 0065D4B0): the message log and the
// objectives window's quest log hand it each text they add; and its drawing of the laid out lines
// (DrawLines, 0065D150); A-H20: the rest of the class (constructor, destructor body, texts, layout,
// drawing), the scroll bar's maximum and the tokens' destructor bodies. See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "game_data.h"
#include "lib/mfc_string.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {
constexpr int kFormatType = 1, kWordType = 2;
constexpr COLORREF kDefaultColour = 0xA7D7E4;
constexpr COLORREF kConstructedColour = 0x1C8DC;     // the format token constructor's colour
constexpr int kDefaultFont = 1;
constexpr char kEscape = '\a';

// 005F8340 (Rebirth-patched, called through its address; thiscall on the interface, RET 0xC):
// the extent of `text` in interface font `font`. It takes a CString by value and releases it; a
// naked forwarder cannot declare one, so the string's data pointer is handed over (as ui_text.cpp).
SIZE TextExtent(CInterface3D* ui, const mfc::CString& text, int font) {
    mfc::CString copy(text);
    char* data;
    std::memcpy(&data, &copy, sizeof(data));
    mfc::CString empty;
    std::memcpy(&copy, &empty, sizeof(data));   // `copy` now owns nothing
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
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

// --- 0065D970 (A-H19) ------------------------------------------------------------------------------

int CUiScrollText::NextToken(const mfc::CString* text, int pos, mfc::CString* out) {
    int length = text->GetLength();
    const char* chars = *text;
    while (pos < length && (chars[pos] == ' ' || chars[pos] == '\t')) ++pos;
    if (pos >= length) return -1;
    // GetBuffer(0) on the text, as the original (a shared text gets its own copy).
    char* buffer = const_cast<mfc::CString*>(text)->GetBuffer(0);
    mfc::CString rest(buffer + pos);
    const char* r = rest;
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
            if (count == -1) count = rest.GetLength();
        }
    } else if (r[0] == '\n') {
        count = 1;
    } else {
        count = rest.FindOneOf(" \t\a\n");
        if (count == -1) count = rest.GetLength();
    }
    *out = rest.Left(count);
    return pos + count;
}
EI_REWRITTEN(0x0065D970, CUiScrollText::NextToken);

// --- 0065D4B0 ------------------------------------------------------------------------------------

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
                format = static_cast<UiFormatToken*>(gamecrt::operator_new(sizeof(UiFormatToken)));
                if (format) {
                    ei::SetVtable(format, UiFormatToken::kVtableAddress);
                    format->type_ = kFormatType;
                    format->newline_ = false;
                    format->colour_ = kConstructedColour;
                    format->font_ = font;
                }
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
            auto* w = static_cast<UiWordToken*>(gamecrt::operator_new(sizeof(UiWordToken)));
            if (w) {
                ei::SetVtable(w, UiTextToken::kVtableAddress);   // the base, replaced by the word's
                w->type_ = 0;
                ::new (&w->text_) mfc::CString;   // afxEmptyString
                w->type_ = kWordType;
                ei::SetVtable(w, UiWordToken::kVtableAddress);
                w->width_ = 0;
            }
            SIZE size = TextExtent(interface_, token, font);
            w->width_ = size.cx;
            w->text_ = token;
            Append(tokens_, w);
        }
    } while (pos < text->GetLength());
}
EI_REWRITTEN(0x0065D4B0, CUiScrollText::Tokenize);

// --- 0065D150 --------------------------------------------------------------------------------------

namespace {
constexpr int kAlignLeft = 1;
const char* FontSampleText() { return "W"; }   // measured for each font's height
constexpr int kMeasuredFonts = 3;

// A word of a line (005FDE20, Rebirth-patched: through its address).
void DrawWord(CUiSurface* surface, const char* text, RECT* rect, int font, COLORREF colour) {
    SurfaceDrawText(surface, nullptr, text, rect, font, colour, 0, 1);
}
} // namespace

void CUiScrollText::DrawLines(int scroll) {
    CUiSurface* surface = surfaces_.size > 0 ? surfaces_.data[0] : nullptr;
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
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
        mfc::CString sample(FontSampleText());
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
                auto* format = static_cast<UiFormatToken*>(tokens_.data[k]);
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
            if (tokens_.data[t]->type_ == kWordType) wordsWidth += static_cast<UiWordToken*>(tokens_.data[t])->width_;
        // Left aligned (a third of the font's height apart), or justified over the width. The
        // last token is read before the count is checked, as in the original. (A font above 2
        // reads past `heights`; the original reads its other stack slots there.)
        int gap;
        const UiTextToken* last = align_ != kAlignLeft ? tokens_.data[end - 1] : nullptr;
        if (align_ == kAlignLeft ||
            (last->type_ == kFormatType && static_cast<const UiFormatToken*>(last)->newline_) || end - begin < 2 ||
            end == tokens_.size)
            gap = heights[font] / 3;
        else
            gap = (textRect_.right - textRect_.left - wordsWidth) / (end - begin - 1);

        RECT word = row;
        for (int t = begin; t < end; ++t) {
            UiTextToken* token = tokens_.data[t];
            if (token->type_ == kFormatType) {
                auto* format = static_cast<UiFormatToken*>(token);
                colour = format->colour_;
                font = format->font_;
            } else if (token->type_ == kWordType) {
                auto* w = static_cast<UiWordToken*>(token);
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

// --- A-H20: construction, texts, layout ----------------------------------------------------------

namespace {
constexpr std::uintptr_t kScrollTextVtable = std::uintptr_t(0x00742E9C);
constexpr std::uintptr_t kScrollTextInputVtable = std::uintptr_t(0x00742E68);
constexpr std::uintptr_t kTokenArrayVtable = std::uintptr_t(0x00742F14);   // CArray<UiTextToken*>
constexpr std::uintptr_t kLineArrayVtable = std::uintptr_t(0x00742F08);    // CArray<UiTextLine*>
constexpr float kScrollSpeed = 20.0f;        // lines per second while an arrow is held
constexpr int kBarWidth = 14;                // the text's rectangle stops short of the bar
constexpr int kSurfaceFormat = 5;
constexpr int kJustified = 2;
constexpr int kScrollbarSprites = 4;
// The layout measures fonts 0-2; a format token may name any digit. The original reads its other
// stack slots (font 3: its return address) for the others; here they are 0.
constexpr int kFontSlots = 10;

// The tokeniser and the layout through their addresses (their verify wrappers run).
void TokenizeText(CUiScrollText* self, const char* text) {
    mfc::CString copy(text);
    self->Tokenize(&copy);
}
void LayOut(CUiScrollText* self) { self->Layout(); }
} // namespace

CUiScrollText* CUiScrollText::Construct(CInterface3D* ui, const char* text, RECT rect, float depth) {
    CUiScrollbar::Construct(ui, rect, depth, 0, kScrollSpeed);
    tokens_.Construct(kTokenArrayVtable);
    lines_.Construct(kLineArrayVtable);
    vtable_ = ei::GamePointer<void*>(kScrollTextVtable);
    inputVtable_ = ei::GamePointer<void*>(kScrollTextInputVtable);
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->ConstructOnRect(interface_, &rect, kSurfaceFormat) : nullptr);
    CUiScrollbar::SetRect(&rect);
    PlaceText(rect);
    DeleteTokens();
    tokens_.SetSize(0, -1);
    TokenizeText(this, text);
    LayOut(this);
    CallSetPosition(0);
    CUiScrollbar::SetPosition(0);
    align_ = kJustified;
    return this;
}
EI_REWRITTEN(0x0065CC00, CUiScrollText::Construct);

void CUiScrollText::DeleteTokens() {
    for (int i = 0; i < tokens_.size; ++i)
        if (UiTextToken* token = tokens_.data[i]) token->Delete();
}

void CUiScrollText::PlaceText(const RECT& rect) {
    if (surfaces_.size > 0 && surfaces_.data[0]) surfaces_.data[0]->MoveTo(rect.left, rect.top);
    textRect_ = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    textRect_.right -= kBarWidth;
    LayOut(this);
}

void CUiScrollText::DestroyBody() {
    vtable_ = ei::GamePointer<void*>(kScrollTextVtable);
    inputVtable_ = ei::GamePointer<void*>(kScrollTextInputVtable);
    DeleteTokens();
    tokens_.SetSize(0, -1);
    for (int i = 0; i < lines_.size; ++i) gamecrt::operator_delete(lines_.data[i]);
    lines_.SetSize(0, -1);
    lines_.vtable = ei::GamePointer<void>(kLineArrayVtable);
    lines_.FreeData();
    tokens_.vtable = ei::GamePointer<void>(kTokenArrayVtable);
    tokens_.FreeData();
    CUiScrollbar::DestroyBody();
}
EI_REWRITTEN(0x0065CE20, CUiScrollText::DestroyBody);

void CUiScrollText::AddText(const char* text) {
    CallSetPosition(maxPosition_);
    int position;
    moved_ = true;
    TakePosition(&position);
    int before = Maximum();
    TokenizeText(this, text);
    LayOut(this);
    CallSetPosition(Maximum() - before + position);
}
EI_REWRITTEN(0x0065CF30, CUiScrollText::AddText);

void CUiScrollText::SetText(const char* text) {
    Clear();
    TokenizeText(this, text);
    LayOut(this);
    CallSetPosition(0);
}
EI_REWRITTEN(0x0065CFE0, CUiScrollText::SetText);

void CUiScrollText::SetRect(const RECT* rect) {
    CUiScrollbar::SetRect(rect);
    PlaceText(*rect);
}
EI_REPLACE_METHOD(0x0065D0A0, CUiScrollText::SetRect);

BOOL CUiScrollText::Draw(int pass) {
    int position;
    if (TakePosition(&position)) static_cast<CUiScrollText*>(this)->DrawLines(position);
    return DrawBase(pass);
}
EI_REPLACE_METHOD(0x0065D120, CUiScrollText::Draw);

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
        mfc::CString sample(FontSampleText());
        heights[f] = TextExtent(interface_, sample, f).cy;
    }
    for (int i = 0; i < lines_.size; ++i) gamecrt::operator_delete(lines_.data[i]);
    lines_.SetSize(0, -1);

    for (int t = 0; t < tokens_.size; ++t) {
        // (The original does not check the allocation.)
        auto* line = static_cast<UiTextLine*>(gamecrt::operator_new(sizeof(UiTextLine)));
        int height = heights[font];
        line->firstToken_ = t;
        line->height_ = height;
        lines_.Add(line);
        int x = 0;
        for (; t < tokens_.size && x < width; ++t) {
            UiTextToken* token = tokens_.data[t];
            if (token->type_ == kFormatType) {
                auto* format = static_cast<UiFormatToken*>(token);
                if (format->newline_) {
                    font = format->font_;
                    break;
                }
            } else if (token->type_ == kWordType) {
                x += static_cast<UiWordToken*>(token)->width_ + height / 3;
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
    for (int i = 0; i < kScrollbarSprites; ++i) reinterpret_cast<CUiMesh*>(sceneObjects_.data[i])->visible_ = visible;
    int position = position_;
    position_ = position - 1;
    CallSetPosition(position);
}
EI_REWRITTEN(0x0065AFE0, CUiScrollbar::SetMaximum);

// --- The tokens' destructors ---------------------------------------------------------------------

void UiTextToken::DestroyBase() { ei::SetVtable(this, UiTextToken::kVtableAddress); }
EI_REWRITTEN(0x0065D8E0, UiTextToken::DestroyBase);
EI_REWRITTEN(0x0065D960, UiTextToken::DestroyBase);

void UiWordToken::DestroyWord() {
    ei::SetVtable(this, kVtableAddress);
    text_.~CString();
    ei::SetVtable(this, UiTextToken::kVtableAddress);
}
EI_REWRITTEN(0x0065D910, UiWordToken::DestroyWord);
