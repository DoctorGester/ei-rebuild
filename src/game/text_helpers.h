#pragma once

// Small text helpers of the engine base (0x00445D20-0x00447F50; A-E1). See docs/notes/core.md.

#include "lib/mfc.h"

// Splits a CString at a separator string. The credits screen (0062B7D0) is the only user: it walks
// the lines of the titles text.
struct TextTokenizer {
    int position;                    // where the next word starts; -1 at the end
    const mfc::CString* source;
    const char* separator;
    mfc::CString word;               // the last word returned

    // 00445D20. From the start of the text: position 0, then Next.
    const char* Start();
    // 00445D30. The next word: up to the separator (the position moves past it) or the rest of the
    // text. Returns `word`'s text, or null once the position has reached the end of the text.
    const char* Next();
};
static_assert(sizeof(TextTokenizer) == 0x10);

// 004461E0. Removes every carriage return from `text` (CString::Remove); returns the count.
int __fastcall RemoveCarriageReturns(mfc::CString* text);

// 00447F50 (RET 4; `name` is passed by value and destroyed here). Constructs `out` as the entry of
// `names` that equals `name` ignoring case (_mbsicmp), or as `name` itself when there is none.
// Returns `out`.
mfc::CString* __fastcall FindListedName(mfc::CString* out, const mfc::CStringArray* names, mfc::CString name);
