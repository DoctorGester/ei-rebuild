#pragma once

// Helpers shared by the script loader files (script_parse.cpp, script_parse_load.cpp) and their
// verify wrappers. Not part of the game's interface.

#include "game_strings.h"
#include "script.h"

#include <cstddef>
#include <cstdint>

namespace scriptparse {

// The lexer's constants (CScript::Tokenize, ClassifyToken; the quote is the byte at 0078BC98).
inline constexpr char kQuote = '"';
inline constexpr const char* kCommentOpen = /*0078BC8C*/ "/*";
inline constexpr const char* kCommentClose = /*0078BC90*/ "*/";
inline constexpr const char* kLineComment = /*0078BC94*/ "//";

// Game CRT (locale-dependent ctype functions).
int __cdecl IsDigit(int c);
int __cdecl IsSpace(int c);
int __cdecl Mbscmp(const char* a, const char* b);

// CString members beyond lib/mfc.h, called on existing strings through this view.
struct StringOps : mfc::CString {
    StringOps* InitN(const char* text, int length);   // 006FD4DE CString(LPCTSTR, int) on raw memory
    char* LockBuffer();
    void UnlockBuffer();
};
inline const StringOps& Ops(const mfc::CString& s) { return reinterpret_cast<const StringOps&>(s); }
inline StringOps& Ops(mfc::CString& s) { return reinterpret_cast<StringOps&>(s); }


// Formats the error message into the CScript's error text and sets code and line;
// returns 0. (CScript::Error is the same.)
int ScriptErrorV(CScript* script, int line, int code, const char* format, char* args);
int __cdecl ScriptError(CScript* script, int line, int code, const char* format, ...);

// 004D5880 (ECX the path, EDX the text; script_text_file.cpp): a text file's lines joined into
// *text (emptied first). 0 when the file cannot be opened or read (MFC's CFileException, caught).
int __fastcall ReadTextFile(const char* path, mfc::CString* text);

} // namespace scriptparse
