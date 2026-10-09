// A scrolling text's tokeniser (CUiScrollText::Tokenize, 0065D4B0): the message log and the
// objectives window's quest log hand it each text they add; and its drawing of the laid out lines
// (DrawLines, 0065D150). See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {
constexpr std::uintptr_t kFormatVtable = 0x00742F28;
constexpr std::uintptr_t kTokenVtable = 0x00742F24;   // the base, replaced by the word's
constexpr std::uintptr_t kWordVtable = 0x00742F20;
constexpr int kFormatType = 1, kWordType = 2;
constexpr COLORREF kDefaultColour = 0xA7D7E4;
constexpr COLORREF kConstructedColour = 0x1C8DC;     // the format token constructor's colour
constexpr int kDefaultFont = 1;
constexpr char kEscape = '\a';

// 005F8340 (Rebirth-patched, called through its address; thiscall on the interface, RET 0xC):
// the extent of `text` in interface font `font`. It takes a CString by value and releases it; a
// naked forwarder cannot declare one, so the string's data pointer is handed over (as ui_text.cpp).
struct TextMetricsAt {
    SIZE TextExtentRaw(char* text, int font);
    SIZE TextExtent(const mfc::CString& text, int font) {
        mfc::CString copy(text);
        char* data;
        std::memcpy(&data, &copy, sizeof(data));
        mfc::CString empty;
        std::memcpy(&copy, &empty, sizeof(data));   // `copy` now owns nothing
        return TextExtentRaw(data, font);
    }
};
__declspec(naked) SIZE TextMetricsAt::TextExtentRaw(char*, int) EI_ORIGINAL(0x005F8340)

// 0065E010 (RET 8): CArray::SetSize of the token array. The original calls it for a format token
// and inlines the same template code (SetAtGrow) for a word.
__declspec(naked) void __fastcall TokensSetSize(UiTokenArray*, void*, int, int) EI_ORIGINAL(0x0065E010)

void Append(UiTokenArray& tokens, UiTextToken* token) {
    int index = tokens.size;
    TokensSetSize(&tokens, nullptr, index + 1, -1);
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
EI_REPLACE_METHOD(0x0065D970, CUiScrollText::NextToken);

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
                    ei::SetVtable(format, kFormatVtable);
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
                ei::SetVtable(w, kTokenVtable);
                w->type_ = 0;
                w->text_ = *reinterpret_cast<char**>(0x00796E48);   // afxEmptyString
                w->type_ = kWordType;
                ei::SetVtable(w, kWordVtable);
                w->width_ = 0;
            }
            SIZE size = reinterpret_cast<TextMetricsAt*>(interface_)->TextExtent(token, font);
            w->width_ = size.cx;
            *reinterpret_cast<mfc::CString*>(&w->text_) = token;
            Append(tokens_, w);
        }
    } while (pos < text->GetLength());
}
EI_REPLACE_METHOD(0x0065D4B0, CUiScrollText::Tokenize);

// --- 0065D150 --------------------------------------------------------------------------------------

namespace {
constexpr int kAlignLeft = 1;
const char* FontSampleText() { return reinterpret_cast<const char*>(0x00787BCC); }   // "W", measured for each font's height
constexpr int kMeasuredFonts = 3;

// A word of a line (005FDE20, Rebirth-patched: through its address).
void DrawWord(CUiSurface* surface, const char* text, RECT* rect, int font, COLORREF colour) {
    SurfaceDrawText(surface, nullptr, text, rect, font, colour, 0, 1);
}
} // namespace

void CUiScrollText::DrawLines(int scroll) {
    CUiSurface* surface = surfaces_.size > 0 ? surfaces_.data[0] : nullptr;
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    int lineCount = lines_.size;
    if (lineCount <= 0) {
        SurfaceReleaseDC(surface);
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
        heights[font] = reinterpret_cast<TextMetricsAt*>(interface_)->TextExtent(sample, font).cy;
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
    SurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x0065D150, CUiScrollText::DrawLines);
