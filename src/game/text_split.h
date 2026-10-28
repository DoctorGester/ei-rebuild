#pragma once

// Text helpers: splitting a text into strings at a separator, joining
// them, and replacing a character in a string. See docs/notes/script.md ("Leftovers").

#include <string>
#include <string_view>
#include <vector>

namespace text {

// The array is emptied; the text with the separator appended is cut at every separator and each piece,
// trimmed of white space at both ends, is appended (empty pieces too). Returns 1.
int Split(std::string_view text, std::vector<std::string>* array, char separator);
// As Split, but a piece between two adjacent separators (or before a leading one) is skipped; a piece
// that is empty only after trimming is still added. Returns 1.
int SplitWords(std::string_view text, std::vector<std::string>* array, char separator);
// Every `from` in the string becomes `to`. Returns 1.
int ReplaceChar(std::string* string, char from, char to);
// `out` becomes the strings with `separator` between them (empty for none). Returns 1.
int Join(std::string* out, const std::vector<std::string>& parts, char separator);

} // namespace text
