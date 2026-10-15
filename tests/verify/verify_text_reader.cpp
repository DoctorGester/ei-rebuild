// Differential checks for the text manager's line reader (src/game/text_reader.cpp), as in
// verify_item_db.cpp: ours runs first on the real outputs, its final cursor state (+0x20..+0x28)
// and result are recorded and the state restored, then the original runs on scratch strings / word
// arrays made afterwards (freed again; SV2h1: ours allocates first) and ours' state is put back;
// results, outputs and states are compared. Nested reader calls run through their own wrappers in
// both runs.
#include "verify.h"

#include "kept_strings.h"

#include "../../src/game/text_reader.h"

#include <cstring>
#include <string>

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

// Runs `ours` then `original` (both return BOOL) from the same state; compares results and the
// final state, plus whatever `same` says about the outputs; leaves ours' state.
template <typename Ours, typename Original, typename SameOutputs>
BOOL Both(const char* name, CTextManager* self, Ours ours, Original original, SameOutputs sameOutputs) {
    CursorState before = Save(self);
    BOOL actual = ours();
    CursorState mine = Save(self);
    Restore(self, before);
    BOOL expected = original();
    CursorState theirs = Save(self);
    Restore(self, mine);
    bool ok = expected == actual && Same(theirs, mine) && sameOutputs();
    ei::verify::Check(name, true, ok, "result %d vs %d, cursor +%d (original +%d)", expected, actual, Offset(self),
                      theirs.text ? static_cast<int>(theirs.cursor - theirs.text) : -1);
    return actual;
}

// 004DEFC0: PUSH EDI; MOV EDI,ECX; XOR EAX,EAX (5 bytes).
BOOL(__fastcall* s_open)(CTextManager*, void*, const char*);
BOOL __fastcall OpenVerify(CTextManager* self, void*, const char* key) {
    return Both(
        "TextOpen", self, [&] { return self->Open(key); }, [&] { return s_open(self, nullptr, key); },
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

// The readers into a CString: ours into the caller's, then the original into a scratch string made
// from the text the caller's had (kept before ours ran).

// 004DF770: PUSH -1; PUSH 0x71B1C8 (7 bytes).
BOOL(__fastcall* s_peekWord)(CTextManager*, void*, mfc::CString*);
BOOL __fastcall PeekWordVerify(CTextManager* self, void*, mfc::CString* out) {
    const std::string kept = ei::verify::KeepText(*out);
    mfc::CString scratch;
    return Both(
        "TextPeekWord", self, [&] { return self->PeekWord(out); },
        [&] {
            scratch = kept.c_str();
            return s_peekWord(self, nullptr, &scratch);
        },
        [&] { return SameText(scratch, *out); });
}

// 004DF6E0: PUSH EBP; MOV EBP,ECX; PUSH ESI; MOV ESI,[EBP+0x20] (7 bytes).
BOOL(__fastcall* s_skipLine)(CTextManager*);
BOOL __fastcall SkipLineVerify(CTextManager* self) {
    return Both(
        "TextSkipLine", self, [&] { return TextSkipLine(self); }, [&] { return s_skipLine(self); },
        [] { return true; });
}

// 004DF510: PUSH -1; PUSH 0x71B1A8 (7 bytes).
BOOL(__fastcall* s_nextLine)(CTextManager*);
BOOL __fastcall NextLineVerify(CTextManager* self) {
    return Both(
        "TextNextLine", self, [&] { return TextNextLine(self); }, [&] { return s_nextLine(self); },
        [] { return true; });
}

// 004DF8D0: PUSH -1; PUSH 0x71B1F0 (7 bytes).
BOOL(__fastcall* s_readLine)(CTextManager*, void*, mfc::CString*, BOOL);
BOOL __fastcall ReadLineVerify(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    const std::string kept = ei::verify::KeepText(*out);
    mfc::CString scratch;
    return Both(
        "TextReadLine", self, [&] { return self->ReadLine(out, advance); },
        [&] {
            scratch = kept.c_str();
            return s_readLine(self, nullptr, &scratch, advance);
        },
        [&] { return SameText(scratch, *out); });
}

// 004DFB80: PUSH -1; PUSH 0x71B208 (7 bytes).
BOOL(__fastcall* s_readToHash)(CTextManager*, void*, mfc::CString*, BOOL);
BOOL __fastcall ReadToHashVerify(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    const std::string kept = ei::verify::KeepText(*out);
    mfc::CString scratch;
    return Both(
        "TextReadToHash", self, [&] { return self->ReadToHash(out, advance); },
        [&] {
            scratch = kept.c_str();
            return s_readToHash(self, nullptr, &scratch, advance);
        },
        [&] { return SameText(scratch, *out); });
}

// 004DFCB0: PUSH -1; PUSH 0x71B238 (7 bytes). Ours fills the caller's array, then the original a
// scratch array, whose strings are freed last to first.
BOOL(__fastcall* s_split)(CTextManager*, void*, console::CStringArray*, BOOL);
BOOL __fastcall SplitVerify(CTextManager* self, void*, console::CStringArray* words, BOOL advance) {
    console::CStringArray scratch;
    BOOL result = Both(
        "TextSplit", self, [&] { return self->Split(words, advance); },
        [&] { return s_split(self, nullptr, &scratch, advance); }, [&] { return SameWords(scratch, *words); });
    ei::verify::EmptyBackwards(scratch);
    return result;
}

// 004DF030: PUSH -1; PUSH 0x71B190 (7 bytes).
BOOL(__fastcall* s_findLine)(CTextManager*, void*, const char*);
BOOL __fastcall FindLineVerify(CTextManager* self, void*, const char* key) {
    return Both(
        "TextFindLine", self, [&] { return self->FindLine(key); },
        [&] { return s_findLine(self, nullptr, key); }, [] { return true; });
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
