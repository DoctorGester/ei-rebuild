// Splitting texts into CStringArrays and replacing characters. See text_split.h.
#include "text_split.h"

#include "game_strings.h"

#include "ei/hook.h"
#include "lib/str.h"

#include <string>

namespace text {

namespace {

// The two splitters: the text plus a final separator, cut at each separator.
int SplitAt(const char* text, mfc::CStringArray* array, char separator, bool skipAdjacent) {
    std::string piece;
    mfc::CString work(text);
    array->SetSize(0, -1);
    console::Ops(work).AppendChar(separator);
    int start = 0;
    int at = work.Find(separator);
    while (at != -1) {
        if (!skipAdjacent || at != start) {
            piece = console::Ops(work).Mid(start, at - start);
            str::TrimLeft(piece);
            str::TrimRight(piece);
            array->SetAtGrow(array->GetSize(), piece);
        }
        start = at + 1;
        at = work.Find(separator, start);
    }
    return 1;
}

} // namespace

int __fastcall Split(const char* text, mfc::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, false);
}
EI_REWRITTEN(0x004DBE40, Split);

int __fastcall SplitWords(const char* text, console::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, true);
}
EI_REWRITTEN(0x004DBF50, SplitWords);

int __fastcall ReplaceChar(std::string* string, char from, char to) {
    std::string result;
    int start = 0;
    int at = str::Find(*string, from);
    while (at != -1) {
        result += str::Mid(*string, start, at - start);
        result += to;
        start = at + 1;
        at = str::Find(*string, from, start);
    }
    result += str::Mid(*string, start);
    *string = result;
    return 1;
}
EI_REWRITTEN(0x004DC400, ReplaceChar);

int __fastcall Join(mfc::CString* out, const mfc::CStringArray* parts, char separator) {
    *out = game::EmptyText();
    if (parts->GetSize() == 0) return 1;
    *out += (*parts)[0];
    for (int i = 1; i < parts->GetSize(); ++i) {
        console::Ops(*out).AppendChar(separator);
        *out += (*parts)[i];
    }
    return 1;
}
EI_REWRITTEN(0x004DC390, Join);

} // namespace text
