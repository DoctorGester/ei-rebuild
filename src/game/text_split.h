#pragma once

// Text helpers: splitting a text into strings at a separator, joining
// them, and replacing a character in a string. See docs/notes/script.md ("Leftovers").

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
