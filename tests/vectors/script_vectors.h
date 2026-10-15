#pragma once

// Payloads of the "script" family (vectors.h): the script lexer and loader (src/game/script_parse.cpp).
// Recorded by record_script.cpp, replayed by replay_script.cpp. All records are variable-size, in the
// order given here (Writer / Reader).
//
// "@ScriptKeywords" (fixture): the keyword table as ClassifyToken finds it (the address stored at
// 004CF69B; u32), its entry count (u32) and per entry the id (i32) and the name, function and command
// strings; then the lexer's data: the quote character (u8), the count (u32) of string pointers and
// per pointer its address (u32) and string (the comment markers at 0078BC8C..94), the count (u32) of
// strings at fixed addresses and per string its address and text ("None", "Unknown command").
//
// ClassifyToken: the text (string), the game CRT's answers for its negative chars
// (crt::PutNegativeClasses), which outputs were asked for (u8: bit 0 id, bit 1 rest), then the id and
// rest (i32 each, 0x7777 where not asked).
// FunctionSignature: id and type (i32), the signature (string, null when none).
// KeywordName: id (i32), the name (string).
// CScript::Tokenize: the source (u32 length and its bytes), the game CRT's answers for its negative
// chars (crt::PutNegativeClasses), the token list's block size (i32), the
// result (i32), the error when it is 0 (code and line i32, text string), then the token count (u32),
// TokenHash of the whole list (u64) and of each token (u32 each).
// ApplyDefines: the line before (string), the define count (u32) and each define's name and value
// (strings), the line after (string).

#include <cstdint>

namespace ei::vectors::script {

constexpr std::uintptr_t kKeywordTablePointer = 0x004CF69B;   // ClassifyToken's MOV EBP,imm32 operand
constexpr std::uintptr_t kQuoteChar = 0x0078BC98;
constexpr std::uintptr_t kStringPointers[] = {0x0078BC8C, 0x0078BC90, 0x0078BC94};   // "/*", "*/", "//"
constexpr std::uintptr_t kTexts[] = {0x0078BC6C, 0x0078D848};   // "None", "Unknown command"

constexpr int kUnasked = 0x7777;

// Tokenize's sources up to this size are kept (bucket: up to 1 KB, up to 16 KB, larger).
constexpr std::uint32_t kMaxSource = 0x10000;
inline unsigned SourceBucket(std::uint32_t size) { return size <= 0x400 ? 0 : size <= 0x4000 ? 1 : 2; }

// A token's text and line, hashed (FNV-1a; the list's hash runs over every token's text, a zero
// byte and its line).
inline std::uint64_t TokenHash(const char* text, int line, std::uint64_t h = 0xCBF29CE484222325ull) {
    for (const char* c = text;; ++c) {
        h = (h ^ static_cast<unsigned char>(*c)) * 0x100000001B3ull;
        if (!*c) break;
    }
    for (int i = 0; i < 4; ++i) h = (h ^ ((static_cast<unsigned>(line) >> (8 * i)) & 0xFF)) * 0x100000001B3ull;
    return h;
}

} // namespace ei::vectors::script
