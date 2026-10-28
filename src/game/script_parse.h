#pragma once

// Helpers shared by the script loader files (script_parse.cpp, script_parse_load.cpp) and their
// verify wrappers. Not part of the game's interface.

#include "game_strings.h"
#include "script.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace scriptparse {

// The lexer's constants (CScript::Tokenize, ClassifyToken).
inline constexpr char kQuote = '"';
inline constexpr const char* kCommentOpen = /*0078BC8C*/ "/*";
inline constexpr const char* kCommentClose = /*0078BC90*/ "*/";
inline constexpr const char* kLineComment = /*0078BC94*/ "//";

// Game CRT (locale-dependent ctype functions).
int IsDigit(int c);
int IsSpace(int c);
int Mbscmp(std::string_view a, std::string_view b);


// Formats the error message into the CScript's error text and sets code and line;
// returns 0. (CScript::Error is the same.)
int ScriptErrorV(CScript* script, int line, int code, const char* format, char* args);

// script_text_file.cpp: a text file's lines joined into
// *text (emptied first). 0 when the file cannot be opened or read (MFC's CFileException, caught).
int ReadTextFile(const char* path, mfc::CString* text);

} // namespace scriptparse
