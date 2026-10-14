#pragma once

// Helpers shared by the script loader files (script_parse.cpp, script_parse_load.cpp) and their
// verify wrappers. Not part of the game's interface.

#include "script.h"

#include <cstddef>
#include <cstdint>

namespace scriptparse {

// Game CRT (locale-dependent ctype functions and the CRT's own atof).
int __cdecl IsDigit(int c);                                       // 006E9379
int __cdecl IsSpace(int c);                                       // 006E93CE
int __cdecl Mbscmp(const char* a, const char* b);                 // 006E848B
double __cdecl Atof(const char* s);                               // 006E911A (result in ST0)

// The game CRT's tolower (006EBA53) of every byte, for the CRT's current LC_CTYPE locale (rebuilt
// when its locale handle or code page changes). For ClassifyToken's keyword compare.
const int* CaseFoldTable();
// gamecrt::strnicmp(a, b, n) == 0 (006FA1A0) with the table above, without a CRT call per byte: a
// zero byte in either string ends the compare (equal only when both are), otherwise the bytes' folds
// must match.
bool SameIgnoringCase(const int* fold, const char* a, const char* b, int n);

// CString members beyond lib/mfc.h, called on existing strings through this view.
struct StringOps : mfc::CString {
    StringOps* InitN(const char* text, int length);   // 006FD4DE CString(LPCTSTR, int) on raw memory
    char* LockBuffer();                               // 00700E6A
    void UnlockBuffer();                              // 00700E7C
};
inline const StringOps& Ops(const mfc::CString& s) { return reinterpret_cast<const StringOps&>(s); }
inline StringOps& Ops(mfc::CString& s) { return reinterpret_cast<StringOps&>(s); }

// CString length (stored before the characters).
inline int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// A string constant of game.exe (messages are passed by their original address).
inline const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// 004CF470: formats the error message into the CScript's error text and sets code and line;
// returns 0. (CScript::Error forwards to the original address.)
int ScriptErrorV(CScript* script, int line, int code, const char* format, char* args);
int __cdecl ScriptError(CScript* script, int line, int code, const char* format, ...);

// 004D5880 (ECX the path, EDX the text; script_text_file.cpp): a text file's lines joined into
// *text (emptied first). 0 when the file cannot be opened or read (MFC's CFileException, caught).
int __fastcall ReadTextFile(const char* path, mfc::CString* text);

// Loader functions called through their original addresses.
void __fastcall ClassifyAt(const char* text, int* id, int* rest);   // 004CF600
const char* __fastcall SignatureAt(int id, int type);               // 004D0B10
const char* __fastcall KeywordNameAt(int id);                        // 004D0BA0

} // namespace scriptparse
