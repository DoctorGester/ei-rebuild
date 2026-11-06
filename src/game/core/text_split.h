#pragma once

// Text helpers: splitting a text into strings at a separator, joining
// them, and replacing a character in a string; the credits' tokenizer and carriage returns. See
// docs/notes/script.md ("Leftovers").

#include <string>
#include <string_view>
#include <vector>

namespace text {

// The text with the separator appended, cut at every separator: each piece trimmed of white space at both
// ends (empty pieces too).
std::vector<std::string> Split(std::string_view text, char separator);
// As Split, but a piece between two adjacent separators (or before a leading one) is skipped; a piece
// that is empty only after trimming is still kept.
std::vector<std::string> SplitWords(std::string_view text, char separator);
// Every `from` in the string becomes `to`. Returns 1.
int ReplaceChar(std::string* string, char from, char to);
// The strings with `separator` between them (empty for none).
std::string Join(const std::vector<std::string>& parts, char separator);

} // namespace text

// Splits a string at a separator string. The credits screen is the only user: it walks
// the lines of the titles text.
struct TextTokenizer {
    int position = 0;                // where the next word starts; -1 at the end
    const std::string* source = nullptr;
    std::string_view separator;
    std::string word;                // the last word returned

    // From the start of the text: position 0, then Next.
    std::string_view Start();
    // The next word: up to the separator (the position moves past it) or the rest of the
    // text. Returns `word`'s text, or an empty view once the position has reached the end of the text.
    std::string_view Next();
};

// Removes every carriage return from `text`; returns the count.
int RemoveCarriageReturns(std::string* text);
