// Differential checks for the text manager's line reader (src/game/text_reader.cpp), as in
// verify_item_db.cpp: the original runs first (on scratch copies of the output strings / word
// arrays), its final cursor state (+0x20..+0x28) and result are recorded and the state restored,
// then ours runs on the real outputs; results, outputs and states are compared. Nested reader calls
// run through their own wrappers in both runs.
#include "verify.h"

#include "../../src/game/text_reader.h"

#include <cstring>

namespace {

struct CursorState {
    const char* cursor;
    const char* text;
    std::uint32_t length;
};
CursorState Save(const CTextManager* t) { return {t->cursor_, t->text_, t->length_}; }
void Restore(CTextManager* t, const CursorState& s) {
    t->cursor_ = s.cursor;
    t->text_ = s.text;
    t->length_ = s.length;
}
bool Same(const CursorState& a, const CursorState& b) {
    return a.cursor == b.cursor && a.text == b.text && a.length == b.length;
}
bool SameText(const mfc::CString& a, const mfc::CString& b) {
    return std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
}
bool SameWords(const console::CStringArray& a, const console::CStringArray& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SameText(a.data[i], b.data[i])) return false;
    return true;
}
int Offset(const CTextManager* t) { return t->text_ ? static_cast<int>(t->cursor_ - t->text_) : -1; }

// Runs `original` then `ours` (both return BOOL) from the same state; compares results and the
// final state, plus whatever `same` says about the outputs.
template <typename Original, typename Ours, typename SameOutputs>
BOOL Both(const char* name, CTextManager* self, Original original, Ours ours, SameOutputs sameOutputs) {
    CursorState before = Save(self);
    BOOL expected = original();
    CursorState after = Save(self);
    Restore(self, before);
    BOOL actual = ours();
    bool ok = expected == actual && Same(after, Save(self)) && sameOutputs();
    ei::verify::Check(name, true, ok, "result %d vs %d, cursor +%d (original +%d)", expected, actual, Offset(self),
                      after.text ? static_cast<int>(after.cursor - after.text) : -1);
    return expected;
}

// 004DEFC0: PUSH EDI; MOV EDI,ECX; XOR EAX,EAX (5 bytes).
BOOL(__fastcall* s_open)(CTextManager*, void*, const char*);
BOOL __fastcall OpenVerify(CTextManager* self, void*, const char* key) {
    return Both(
        "TextOpen", self, [&] { return s_open(self, nullptr, key); }, [&] { return self->Open(key); },
        [] { return true; });
}

// 004DF020: ours only.
void* s_unused;
BOOL __fastcall CloseVerify(CTextManager* self) {
    BOOL r = TextClose(self);
    ei::verify::Check("TextClose", true, r == 1 && !self->text_ && !self->cursor_, "%d", r);
    return r;
}

// 004DF4E0: PUSH ESI; PUSH EDI; MOV EDI,ECX; XOR EAX,EAX (6 bytes). Pure.
int(__fastcall* s_charCode)(const char*);
int __fastcall CharCodeVerify(const char* p) {
    int expected = s_charCode(p);
    ei::verify::Check("TextCharCode", expected, TextCharCode(p), "%02X %02X", static_cast<unsigned char>(p[0]),
                      static_cast<unsigned char>(p[1]));
    return expected;
}

// 004DF770: PUSH -1; PUSH 0x71B1C8 (7 bytes).
BOOL(__fastcall* s_peekWord)(CTextManager*, void*, mfc::CString*);
BOOL __fastcall PeekWordVerify(CTextManager* self, void*, mfc::CString* out) {
    mfc::CString scratch(*out);
    return Both(
        "TextPeekWord", self, [&] { return s_peekWord(self, nullptr, &scratch); },
        [&] { return self->PeekWord(out); }, [&] { return SameText(scratch, *out); });
}

// 004DF6E0: PUSH EBP; MOV EBP,ECX; PUSH ESI; MOV ESI,[EBP+0x20] (7 bytes).
BOOL(__fastcall* s_skipLine)(CTextManager*);
BOOL __fastcall SkipLineVerify(CTextManager* self) {
    return Both(
        "TextSkipLine", self, [&] { return s_skipLine(self); }, [&] { return TextSkipLine(self); },
        [] { return true; });
}

// 004DF510: PUSH -1; PUSH 0x71B1A8 (7 bytes).
BOOL(__fastcall* s_nextLine)(CTextManager*);
BOOL __fastcall NextLineVerify(CTextManager* self) {
    return Both(
        "TextNextLine", self, [&] { return s_nextLine(self); }, [&] { return TextNextLine(self); },
        [] { return true; });
}

// 004DF8D0: PUSH -1; PUSH 0x71B1F0 (7 bytes).
BOOL(__fastcall* s_readLine)(CTextManager*, void*, mfc::CString*, BOOL);
BOOL __fastcall ReadLineVerify(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    mfc::CString scratch(*out);
    return Both(
        "TextReadLine", self, [&] { return s_readLine(self, nullptr, &scratch, advance); },
        [&] { return self->ReadLine(out, advance); }, [&] { return SameText(scratch, *out); });
}

// 004DFB80: PUSH -1; PUSH 0x71B208 (7 bytes).
BOOL(__fastcall* s_readToHash)(CTextManager*, void*, mfc::CString*, BOOL);
BOOL __fastcall ReadToHashVerify(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    mfc::CString scratch(*out);
    return Both(
        "TextReadToHash", self, [&] { return s_readToHash(self, nullptr, &scratch, advance); },
        [&] { return self->ReadToHash(out, advance); }, [&] { return SameText(scratch, *out); });
}

// 004DFCB0: PUSH -1; PUSH 0x71B238 (7 bytes). The original fills a scratch array.
BOOL(__fastcall* s_split)(CTextManager*, void*, console::CStringArray*, BOOL);
BOOL __fastcall SplitVerify(CTextManager* self, void*, console::CStringArray* words, BOOL advance) {
    console::CStringArray scratch;
    return Both(
        "TextSplit", self, [&] { return s_split(self, nullptr, &scratch, advance); },
        [&] { return self->Split(words, advance); }, [&] { return SameWords(scratch, *words); });
}

// 004DF030: PUSH -1; PUSH 0x71B190 (7 bytes).
BOOL(__fastcall* s_findLine)(CTextManager*, void*, const char*);
BOOL __fastcall FindLineVerify(CTextManager* self, void*, const char* key) {
    return Both(
        "TextFindLine", self, [&] { return s_findLine(self, nullptr, key); },
        [&] { return self->FindLine(key); }, [] { return true; });
}

} // namespace

EI_VERIFY(0x004DEFC0, OpenVerify, 5, s_open);
EI_VERIFY(0x004DF020, CloseVerify, 0, s_unused);
EI_VERIFY(0x004DF4E0, CharCodeVerify, 6, s_charCode);
EI_VERIFY(0x004DF770, PeekWordVerify, 7, s_peekWord);
EI_VERIFY(0x004DF6E0, SkipLineVerify, 7, s_skipLine);
EI_VERIFY(0x004DF510, NextLineVerify, 7, s_nextLine);
EI_VERIFY(0x004DF8D0, ReadLineVerify, 7, s_readLine);
EI_VERIFY(0x004DFB80, ReadToHashVerify, 7, s_readToHash);
EI_VERIFY(0x004DFCB0, SplitVerify, 7, s_split);
EI_VERIFY(0x004DF030, FindLineVerify, 7, s_findLine);
