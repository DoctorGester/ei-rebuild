// Splitting texts into strings, joining them and replacing characters. See text_split.h.
#include "text_split.h"

#include "lib/str.h"

#include <string>
#include <vector>

namespace text {

namespace {

// The two splitters: the text plus a final separator, cut at each separator.
int SplitAt(const char* text, std::vector<std::string>* array, char separator, bool skipAdjacent) {
    std::string piece;
    std::string work = text;
    array->clear();
    work += separator;
    int start = 0;
    int at = str::Find(work, separator);
    while (at != -1) {
        if (!skipAdjacent || at != start) {
            piece = str::Mid(work, start, at - start);
            str::TrimLeft(piece);
            str::TrimRight(piece);
            array->push_back(piece);
        }
        start = at + 1;
        at = str::Find(work, separator, start);
    }
    return 1;
}

} // namespace

int Split(const char* text, std::vector<std::string>* array, char separator) {
    return SplitAt(text, array, separator, false);
}

int SplitWords(const char* text, std::vector<std::string>* array, char separator) {
    return SplitAt(text, array, separator, true);
}

int ReplaceChar(std::string* string, char from, char to) {
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

int Join(std::string* out, const std::vector<std::string>& parts, char separator) {
    out->clear();
    if (parts.empty()) return 1;
    *out += parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) {
        *out += separator;
        *out += parts[i];
    }
    return 1;
}

} // namespace text
