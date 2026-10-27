#pragma once

// Text helpers: splitting a text into a CStringArray at a separator, joining
// one, and replacing a character in a CString. See docs/notes/script.md ("Leftovers").

#include "console.h"

namespace text {

// Split (ECX text, EDX array). The array is emptied (SetSize(0, -1)); the text with the
// separator appended is cut at every separator and each piece, trimmed of white space at both ends,
// is appended (empty pieces too). Returns 1.
int __fastcall Split(const char* text, mfc::CStringArray* array, char separator);
// (same arguments). As Split, but a piece between two adjacent separators (or before a
// leading one) is skipped; a piece that is empty only after trimming is still added. Returns 1.
int __fastcall SplitWords(const char* text, console::CStringArray* array, char separator);
// (ECX string, EDX from; `to` on the stack). Every `from` in the string becomes `to`. Returns 1.
int __fastcall ReplaceChar(mfc::CString* string, char from, char to);
// (ECX out, EDX array; `separator` on the stack). `out` becomes the array's strings with
// `separator` between them (the exe's empty string for an empty array). Returns 1.
int __fastcall Join(mfc::CString* out, const mfc::CStringArray* parts, char separator);

} // namespace text
