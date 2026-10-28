#pragma once

// Small text helpers of the engine base. See docs/notes/core.md.

#include "lib/mfc.h"

#include <string>

// Splits a CString at a separator string. The credits screen is the only user: it walks
// the lines of the titles text.
struct TextTokenizer {
    int position;                    // where the next word starts; -1 at the end
    const std::string* source;
    const char* separator;
    std::string word;                // the last word returned

    // From the start of the text: position 0, then Next.
    const char* Start();
    // The next word: up to the separator (the position moves past it) or the rest of the
    // text. Returns `word`'s text, or null once the position has reached the end of the text.
    const char* Next();
};

// Removes every carriage return from `text` (CString::Remove); returns the count.
int RemoveCarriageReturns(std::string* text);
