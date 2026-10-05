// Differential checks for text_split.cpp: the original splits into a scratch CStringArray (or
// replaces in a copy of the string), ours into the real one; the results (size and every string) and
// the return values are compared. Split is reached by every console line with a line break
// (`console`) and the item texts; SplitWords by keyboard.ini at startup; ReplaceChar by the item text
// keys (every scenario).
#include "verify.h"

#include "../../src/game/text_split.h"

#include <cstring>

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
    console::CStringArray expected;
    int expectedRet = original(text, &expected, separator);
    int actualRet = ours(text, array, separator);
    ei::verify::Check(name, true, expectedRet == actualRet && SameArray(expected, *array), "separator %02X size %d/%d",
                      static_cast<unsigned char>(separator), expected.GetSize(), array->GetSize());
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
    mfc::CString expected(*string);
    int expectedRet = s_replace(&expected, from, to);
    int actualRet = text::ReplaceChar(string, from, to);
    ei::verify::Check("text::ReplaceChar", true, expectedRet == actualRet && std::strcmp(expected, *string) == 0,
                      "'%c' -> '%c'", from, to);
    return actualRet;
}

} // namespace

EI_VERIFY(0x004DBE40, SplitVerify, 7, s_split);
EI_VERIFY(0x004DBF50, SplitWordsVerify, 7, s_splitWords);
EI_VERIFY(0x004DC400, ReplaceVerify, 7, s_replace);
