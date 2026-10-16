// Splitting texts into CStringArrays and replacing characters (004DBE40-004DC4F0). See text_split.h.
#include "text_split.h"

#include "game_strings.h"

#include "ei/hook.h"
#include "originals_library.h"

namespace text {

namespace {

// The two splitters: the text plus a final separator, cut at each separator.
int SplitAt(const char* text, console::CStringArray* array, char separator, bool skipAdjacent) {
    mfc::CString piece;
    mfc::CString work(text);
    array->SetSize(0, -1);
    console::Ops(work).AppendChar(separator);
    int start = 0;
    int at = work.Find(separator);
    while (at != -1) {
        if (!skipAdjacent || at != start) {
            piece = console::Ops(work).Mid(start, at - start);
            piece.TrimLeft();
            piece.TrimRight();
            array->SetAtGrow(array->GetSize(), piece);
        }
        start = at + 1;
        at = work.Find(separator, start);
    }
    return 1;
}

} // namespace

int __fastcall Split(const char* text, console::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, false);
}
EI_REPLACE(0x004DBE40, Split);

int __fastcall SplitWords(const char* text, console::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, true);
}
EI_REPLACE(0x004DBF50, SplitWords);

int __fastcall ReplaceChar(mfc::CString* string, char from, char to) {
    mfc::CString result;
    int start = 0;
    int at = static_cast<const mfc::CString*>(string)->Find(from);
    while (at != -1) {
        result += console::Ops(*string).Mid(start, at - start);
        console::Ops(result).AppendChar(to);
        start = at + 1;
        at = static_cast<const mfc::CString*>(string)->Find(from, start);
    }
    result += console::Ops(*string).Mid(start);
    *string = result;
    return 1;
}
EI_REPLACE(0x004DC400, ReplaceChar);

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
EI_REPLACE(0x004DC390, Join);

} // namespace text
