#pragma once

// Text helpers of 004DBE40-004DC4F0: splitting a text into a CStringArray at a separator and
// replacing a character in a CString. See docs/notes/script.md ("Leftovers").

#include "console.h"

namespace text {

// 004DBE40 (ECX text, EDX array, RET 4). The array is emptied (SetSize(0, -1)); the text with the
// separator appended is cut at every separator and each piece, trimmed of white space at both ends,
// is appended (empty pieces too). Returns 1.
int __fastcall Split(const char* text, console::CStringArray* array, char separator);
// 004DBF50 (same arguments). As Split, but a piece between two adjacent separators (or before a
// leading one) is skipped; a piece that is empty only after trimming is still added. Returns 1.
int __fastcall SplitWords(const char* text, console::CStringArray* array, char separator);
// 004DC400 (ECX string, EDX from, RET 4 for `to`). Every `from` in the string becomes `to`. Returns 1.
int __fastcall ReplaceChar(mfc::CString* string, char from, char to);

} // namespace text
