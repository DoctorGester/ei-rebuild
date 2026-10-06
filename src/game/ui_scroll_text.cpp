// A scrolling text's tokeniser (CUiScrollText::Tokenize, 0065D4B0): the message log and the
// objectives window's quest log hand it each text they add. See ui.h and docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

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

__declspec(naked) int CUiScrollText::NextToken(const mfc::CString*, int, mfc::CString*) EI_ORIGINAL(0x0065D970)

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
