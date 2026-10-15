// Differential checks for text_split.cpp: ours splits into the real CStringArray (or replaces in the
// real string), then the original into a scratch CStringArray (or a copy of the old string) made
// afterwards and freed again (SV2h1: ours allocates first); the results (size and every string) and
// the return values are compared. Split is reached by every console line with a line break
// (`console`) and the item texts; SplitWords by keyboard.ini at startup; ReplaceChar by the item text
// keys (every scenario).
#include "verify.h"

#include "kept_strings.h"

#include "../../src/game/text_split.h"

#include <cstring>
#include <string>

namespace {

bool SameArray(const console::CStringArray& expected, const console::CStringArray& actual) {
    if (expected.GetSize() != actual.GetSize()) return false;
    for (int i = 0; i < expected.GetSize(); ++i) {
        if (std::strcmp(expected[i], actual[i]) != 0) return false;
    }
    return true;
}

// 004DBE40 / 004DBF50: PUSH -1 (2), PUSH imm32 (5).
using SplitFn = int(__fastcall*)(const char*, console::CStringArray*, char);
SplitFn s_split, s_splitWords;
int CheckSplit(const char* name, SplitFn original, SplitFn ours, const char* text, console::CStringArray* array,
               char separator) {
    int actualRet = ours(text, array, separator);
    console::CStringArray expected;
    int expectedRet = original(text, &expected, separator);
    ei::verify::Check(name, true, expectedRet == actualRet && SameArray(expected, *array), "separator %02X size %d/%d",
                      static_cast<unsigned char>(separator), expected.GetSize(), array->GetSize());
    ei::verify::EmptyBackwards(expected);
    return actualRet;
}
int __fastcall SplitVerify(const char* text, console::CStringArray* array, char separator) {
    return CheckSplit("text::Split", s_split, text::Split, text, array, separator);
}
int __fastcall SplitWordsVerify(const char* text, console::CStringArray* array, char separator) {
    return CheckSplit("text::SplitWords", s_splitWords, text::SplitWords, text, array, separator);
}

// 004DC400: PUSH -1 (2), PUSH imm32 (5).
int(__fastcall* s_replace)(mfc::CString*, char, char);
int __fastcall ReplaceVerify(mfc::CString* string, char from, char to) {
    const std::string kept = ei::verify::KeepText(*string);
    int actualRet = text::ReplaceChar(string, from, to);
    mfc::CString expected(kept.c_str());
    int expectedRet = s_replace(&expected, from, to);
    ei::verify::Check("text::ReplaceChar", true, expectedRet == actualRet && std::strcmp(expected, *string) == 0,
                      "'%c' -> '%c'", from, to);
    return actualRet;
}

// 004DC390: PUSH EBX, PUSH EDI, MOV EDI,EDX, MOV EBX,ECX (6). Ours joins into the real string, then
// the original into a scratch one. Reached only by CTexture::LoadBmp (loose .bmp textures; no
// scenario).
int(__fastcall* s_join)(mfc::CString*, const mfc::CStringArray*, char);
int __fastcall JoinVerify(mfc::CString* out, const mfc::CStringArray* parts, char separator) {
    int actualRet = text::Join(out, parts, separator);
    mfc::CString expected;
    int expectedRet = s_join(&expected, parts, separator);
    ei::verify::Check("text::Join", true, expectedRet == actualRet && std::strcmp(expected, *out) == 0,
                      "%d parts, separator %02X", parts->GetSize(), static_cast<unsigned char>(separator));
    return actualRet;
}

} // namespace

EI_VERIFY(0x004DBE40, SplitVerify, 7, s_split);
EI_VERIFY(0x004DBF50, SplitWordsVerify, 7, s_splitWords);
EI_VERIFY(0x004DC400, ReplaceVerify, 7, s_replace);
EI_VERIFY(0x004DC390, JoinVerify, 6, s_join);
