// Splitting texts into CStringArrays and replacing characters. See text_split.h.
#include "text_split.h"

#include "game_strings.h"

#include "lib/str.h"

#include <string>
#include <vector>

namespace text {

namespace {

void Clear(mfc::CStringArray* array) { array->SetSize(0, -1); }
void Clear(std::vector<std::string>* array) { array->clear(); }
void Add(mfc::CStringArray* array, const std::string& piece) { array->SetAtGrow(array->GetSize(), piece); }
void Add(std::vector<std::string>* array, const std::string& piece) { array->push_back(piece); }

// The two splitters: the text plus a final separator, cut at each separator.
template <typename Array>
int SplitAt(const char* text, Array* array, char separator, bool skipAdjacent) {
    std::string piece;
    std::string work = text;
    Clear(array);
    work += separator;
    int start = 0;
    int at = str::Find(work, separator);
    while (at != -1) {
        if (!skipAdjacent || at != start) {
            piece = str::Mid(work, start, at - start);
            str::TrimLeft(piece);
            str::TrimRight(piece);
            Add(array, piece);
        }
        start = at + 1;
        at = str::Find(work, separator, start);
    }
    return 1;
}

} // namespace

int __fastcall Split(const char* text, mfc::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, false);
}

int SplitStrings(const char* text, std::vector<std::string>* array, char separator) {
    return SplitAt(text, array, separator, false);
}

int __fastcall SplitWords(const char* text, console::CStringArray* array, char separator) {
    return SplitAt(text, array, separator, true);
}

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

int __fastcall Join(std::string* out, const mfc::CStringArray* parts, char separator) {
    *out = game::EmptyText();
    if (parts->GetSize() == 0) return 1;
    *out += (*parts)[0];
    for (int i = 1; i < parts->GetSize(); ++i) {
        *out += separator;
        *out += (*parts)[i];
    }
    return 1;
}

} // namespace text
