// Checks for text_helpers.cpp (A-E1). Main thread.
//  - TextTokenizer::Next: side by side on a copy of the tokenizer (the same source and separator,
//    its own word); the result, the position and the word are compared by content.
//  - TextTokenizer::Start: the original is one store and a jump into Next, which would run Next's
//    wrapper, so the copy is rewound here and the original Next run on it.
//  - RemoveCarriageReturns: ours only (8 bytes: a push and a call, nothing to steal); the count
//    against the text before.
//  - FindListedName: pure, side by side; the two results by content.
// The credits screen (0062B7D0) is the only user of the first three and no scenario opens it.
// FindListedName is the scene registry's (scene_registry.cpp); no scenario is known to reach it.
#include "verify.h"

#include "../../src/game/text_helpers.h"

#include <cstring>

namespace {
void* s_unused;

bool SameText(const char* a, const char* b) {
    if (!a || !b) return a == b;
    return std::strcmp(a, b) == 0;
}

// 00445D30: MOV EAX,FS:[0] (6).
const char*(__fastcall* s_next)(TextTokenizer*);
const char* Compare(const char* name, TextTokenizer* self, bool rewind) {
    TextTokenizer copy{rewind ? 0 : self->position, self->source, self->separator, self->word};
    const char* expected = s_next(&copy);
    const char* actual = rewind ? self->Start() : self->Next();
    bool same = SameText(expected, actual) && copy.position == self->position && SameText(copy.word, self->word);
    ei::verify::Check(name, true, same, "position %d (original %d)", self->position, copy.position);
    return actual;
}
const char* __fastcall NextVerify(TextTokenizer* self) { return Compare("TextTokenizer::Next", self, false); }
const char* __fastcall StartVerify(TextTokenizer* self) { return Compare("TextTokenizer::Start", self, true); }

int __fastcall RemoveCrVerify(mfc::CString* text) {
    int before = text->GetLength();
    int returns = 0;
    for (const char* p = *text; *p; ++p)
        if (*p == '\r') ++returns;
    int removed = RemoveCarriageReturns(text);
    bool same = removed == returns && text->GetLength() == before - returns && !std::strchr(*text, '\r');
    ei::verify::Check("RemoveCarriageReturns", true, same, "%d of %d characters removed (%d counted)", removed, before,
                      returns);
    return removed;
}

// 00447F50: PUSH -1 (2), PUSH imm32 (5).
mfc::CString*(__fastcall* s_findName)(mfc::CString*, const mfc::CStringArray*, mfc::CString);
mfc::CString* __fastcall FindNameVerify(mfc::CString* out, const mfc::CStringArray* names, mfc::CString name) {
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    auto* slot = reinterpret_cast<mfc::CString*>(storage);
    mfc::CString* expected = s_findName(slot, names, name);
    mfc::CString* actual = FindListedName(out, names, name);
    bool same = expected == slot && actual == out && SameText(*expected, *actual);
    ei::verify::Check("FindListedName", true, same, "\"%.40s\": \"%.40s\" (original \"%.40s\")",
                      static_cast<const char*>(name), static_cast<const char*>(*out), static_cast<const char*>(*slot));
    slot->~CString();
    return actual;
}
} // namespace

EI_VERIFY(0x00445D30, NextVerify, 6, s_next);
EI_VERIFY(0x00445D20, StartVerify, 0, s_unused);
EI_VERIFY(0x004461E0, RemoveCrVerify, 0, s_unused);
EI_VERIFY(0x00447F50, FindNameVerify, 7, s_findName);
