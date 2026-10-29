// Splitting texts into strings, joining them and replacing characters. See text_split.h.
#include "text_split.h"

#include "lib/str.h"

#include <string>
#include <vector>

namespace text {

namespace {

// The two splitters: the text plus a final separator, cut at each separator.
std::vector<std::string> SplitAt(std::string_view text, char separator, bool skipAdjacent) {
    std::vector<std::string> pieces;
    std::string piece;
    std::string work(text);
    work += separator;
    int start = 0;
    int at = str::Find(work, separator);
    while (at != -1) {
        if (!skipAdjacent || at != start) {
            piece = str::Mid(work, start, at - start);
            str::TrimLeft(piece);
            str::TrimRight(piece);
            pieces.push_back(piece);
        }
        start = at + 1;
        at = str::Find(work, separator, start);
    }
    return pieces;
}

} // namespace

std::vector<std::string> Split(std::string_view text, char separator) { return SplitAt(text, separator, false); }

std::vector<std::string> SplitWords(std::string_view text, char separator) { return SplitAt(text, separator, true); }

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

std::string Join(const std::vector<std::string>& parts, char separator) {
    std::string joined;
    if (parts.empty()) return joined;
    joined += parts[0];
    for (std::size_t i = 1; i < parts.size(); ++i) {
        joined += separator;
        joined += parts[i];
    }
    return joined;
}

} // namespace text
