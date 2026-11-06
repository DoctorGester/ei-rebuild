#pragma once

// Helpers shared by the script loader files (script_parse.cpp, script_parse_load.cpp). Not part of the
// game's interface.

#include "game/script/script.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace scriptparse {

// The lexer's constants (Script::Tokenize, ClassifyToken).
inline constexpr char kQuote = '"';
inline constexpr const char* kCommentOpen = /*0078BC8C*/ "/*";
inline constexpr const char* kCommentClose = /*0078BC90*/ "*/";
inline constexpr const char* kLineComment = /*0078BC94*/ "//";


// Formats the error message into the Script's error text and sets code and line;
// returns 0. (Script::Error is the same.)
EI_PRINTF_FORMAT(4, 0) int ScriptErrorV(Script* script, int line, ScriptParseError code, const char* format, va_list args);

// script_exec_helpers.cpp: a text file's lines joined into
// *text (emptied first). 0 when the file cannot be opened or read.
int ReadTextFile(const std::string& path, std::string* text);

} // namespace scriptparse
