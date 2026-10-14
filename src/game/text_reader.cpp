// The text manager's line reader (004DEFC0-004DFFFF). See text_reader.h.
//
// The original inlines the same few loops many times with small differences (the end test inline
// or through IsPastEnd 004E0000, the character code inline or through 004DF4E0); `Advance` below
// reproduces each variant by its call pattern.
#include "text_reader.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// MBCS CRT: 006E99F2 _ismbblead, 006E9A6C isleadbyte, 006E9947 _mbsinc, 006E848B _mbscmp.
int __cdecl MbsCmp(const char* a0, const char* a1) { return GameMbscmp(a0, a1); }

// CString members not in lib/mfc.h.
struct TextString : mfc::CString {
    void AppendChar(char c);                    // 00700D73 operator+=(TCHAR)
};
void TextString::AppendChar(char a0) { CStringAppendCharOriginal((void*)this, nullptr, a0); }
TextString& Ops(mfc::CString& s) { return static_cast<TextString&>(s); }
const TextString& Ops(const mfc::CString& s) { return static_cast<const TextString&>(s); }

// 004DBE40 (__fastcall, separator on the stack): splits `text` at `separator` into `out`.

// The functions of this file and 004E0000 / 004DF4E0, called through their addresses where the
// original calls them (their verify wrappers run).
__declspec(naked) BOOL __fastcall IsPastEndCall(CTextManager*, void*, const char*) EI_ORIGINAL(0x004E0000)
__declspec(naked) int __fastcall CharCodeCall(const char*) EI_ORIGINAL(0x004DF4E0)
__declspec(naked) BOOL __fastcall PeekWordCall(CTextManager*, void*, mfc::CString*) EI_ORIGINAL(0x004DF770)
__declspec(naked) BOOL __fastcall SkipLineCall(CTextManager*) EI_ORIGINAL(0x004DF6E0)
__declspec(naked) BOOL __fastcall SplitCall(CTextManager*, void*, console::CStringArray*, BOOL) EI_ORIGINAL(0x004DFCB0)

const char* const kComment = reinterpret_cast<const char*>(0x0078DCF8);   // "##"
const char* const kHeader = reinterpret_cast<const char*>(0x0078DCFC);    // "#"

// The cursor `p` is inside the text (signed compare, as every inlined test).
bool Inside(const CTextManager* t, const char* p) {
    return static_cast<int>(p - t->text_) < static_cast<int>(t->length_);
}

// 004DF4E0 as inlined: the lead test on the unsigned byte, the code from the signed bytes.
int CharCode(const char* p) {
    int high = 0;
    if (IsMbbLead(static_cast<unsigned char>(*p))) {
        high = static_cast<signed char>(*p) << 8;
        ++p;
    }
    return static_cast<signed char>(*p) + high;
}

// Appends the character at `p` (one or two bytes, by isleadbyte on the signed byte) to `out`
// through the temporary `tmp`.
void AppendCharAt(mfc::CString* out, mfc::CString& tmp, const char* p) {
    if (IsLeadByte(static_cast<signed char>(*p))) {
        Ops(tmp).AssignChar(p[0]);
        Ops(tmp).AppendChar(p[1]);
    } else {
        Ops(tmp).AssignChar(p[0]);
    }
    *out += tmp;
}

// How one inlined "move the cursor past the next LF" tests the end and reads characters.
enum class Test { Inline, Call };
struct AdvanceKind {
    Test entry;     // the first test of the cursor
    Test loopEnd;   // the end test of the scanning loop
    Test code;      // the character code in the loop
    Test exit;      // the final test (cursor = p at the end, else just past the LF)
};

bool InsideBy(CTextManager* t, const char* p, Test test) {
    return test == Test::Inline ? Inside(t, p) : !IsPastEndCall(t, nullptr, p);
}

void Advance(CTextManager* t, const AdvanceKind& kind) {
    const char* p = t->cursor_;
    if (!InsideBy(t, p, kind.entry)) return;
    while (InsideBy(t, p, kind.loopEnd) && (kind.code == Test::Inline ? CharCode(p) : CharCodeCall(p)) != 10)
        p = MbsInc(p);
    t->cursor_ = InsideBy(t, p, kind.exit) ? MbsInc(p) : p;
}

constexpr AdvanceKind kAfterRead{Test::Inline, Test::Inline, Test::Inline, Test::Call};    // ReadLine / Split
constexpr AdvanceKind kInline{Test::Inline, Test::Inline, Test::Inline, Test::Inline};     // comment skipping
constexpr AdvanceKind kFindFirst{Test::Inline, Test::Call, Test::Call, Test::Call};        // FindLine, no key / one word
constexpr AdvanceKind kFindMulti{Test::Call, Test::Call, Test::Call, Test::Call};          // FindLine, several words
constexpr AdvanceKind kFindComment{Test::Inline, Test::Inline, Test::Call, Test::Call};    // FindLine, one word, comments

// Whether a word starts with "##".
bool IsComment(const mfc::CString& word) {
    mfc::CString start = word.Left(2);
    return MbsCmp(start, kComment) == 0;
}

// After a read: the cursor moves past the line (`first`), then past every "##" comment line.
void SkipToNextLine(CTextManager* t, const AdvanceKind& first, const AdvanceKind& comment) {
    mfc::CString word;
    Advance(t, first);
    while (PeekWordCall(t, nullptr, &word)) {
        if (!IsComment(word)) break;
        Advance(t, comment);
    }
}

} // namespace

BOOL __fastcall TextOpen(CTextManager* self, void*, const char* key) {
    if (self->text_) {
        self->text_ = nullptr;
        self->cursor_ = nullptr;
    }
    const char* text = nullptr;
    for (CTextManager::SourceNode* node = self->sources_.head->next; node != self->sources_.head; node = node->next) {
        // CMappedResource vtable slot 5, GetFile(name, size): the file's data or null.
        text = ei::VCall<const char*>(node->data.source, 5, key, &self->length_);
        if (text) break;
    }
    self->cursor_ = text;
    self->text_ = text;
    return text && self->length_ != 0;
}
EI_REPLACE(0x004DEFC0, TextOpen);

BOOL __fastcall TextClose(CTextManager* self) {
    self->text_ = nullptr;
    self->cursor_ = nullptr;
    return 1;
}
EI_REPLACE(0x004DF020, TextClose);

int __fastcall TextCharCode(const char* p) { return CharCode(p); }
EI_REPLACE(0x004DF4E0, TextCharCode);

BOOL __fastcall TextPeekWord(CTextManager* self, void*, mfc::CString* out) {
    mfc::CString tmp;
    const char* p = self->cursor_;
    bool started = false;
    out->Empty();
    if (!Inside(self, p)) return 0;
    while (Inside(self, p)) {
        int code = CharCode(p);
        if (code == 13 || code == 9) break;
        if (code == 0x20 || code == 10) {
            if (started) break;
        } else {
            started = true;
            AppendCharAt(out, tmp, p);
        }
        p = MbsInc(p);
    }
    return 1;
}
EI_REPLACE(0x004DF770, TextPeekWord);

BOOL __fastcall TextSkipLine(CTextManager* self) {
    const char* p = self->cursor_;
    if (!Inside(self, p)) return 0;
    while (Inside(self, p) && CharCode(p) != 10) p = MbsInc(p);
    if (!Inside(self, p)) {
        self->cursor_ = p;
        return 0;
    }
    self->cursor_ = MbsInc(p);
    return 1;
}
EI_REPLACE(0x004DF6E0, TextSkipLine);

BOOL __fastcall TextNextLine(CTextManager* self) {
    mfc::CString word;
    Advance(self, kInline);
    while (PeekWordCall(self, nullptr, &word)) {
        if (!IsComment(word)) return 1;
        Advance(self, kInline);
    }
    return 0;
}
EI_REPLACE(0x004DF510, TextNextLine);

BOOL __fastcall TextReadLine(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    mfc::CString tmp;
    const char* p = self->cursor_;
    out->Empty();
    if (!Inside(self, p)) return 0;
    while (Inside(self, p)) {
        int code = CharCode(p);
        if (code == 10 || code == 13) break;
        AppendCharAt(out, tmp, p);
        p = MbsInc(p);
    }
    if (advance) SkipToNextLine(self, kAfterRead, kInline);
    return 1;
}
EI_REPLACE(0x004DF8D0, TextReadLine);

BOOL __fastcall TextReadToHash(CTextManager* self, void*, mfc::CString* out, BOOL advance) {
    mfc::CString tmp;
    const char* p = self->cursor_;
    out->Empty();
    if (!Inside(self, p)) return 0;
    while (Inside(self, p)) {
        if (CharCode(p) == '#') break;
        AppendCharAt(out, tmp, p);
        p = MbsInc(p);
    }
    if (advance) self->cursor_ = p;
    return 1;
}
EI_REPLACE(0x004DFB80, TextReadToHash);

BOOL __fastcall TextSplit(CTextManager* self, void*, console::CStringArray* words, BOOL advance) {
    mfc::CString tmp;
    mfc::CString word;
    bool inWord = false;
    const char* p = self->cursor_;
    words->SetSize(0, -1);
    if (!Inside(self, p)) return 0;
    while (Inside(self, p)) {
        int code = CharCode(p);
        if (code == 10 || code == 13) {
            // The line's last word (a word still open when the text ends is not added).
            if (console::Length(word) != 0) {
                words->SetAtGrow(words->size, word);
                word.Empty();
            }
            break;
        }
        if (code == 0x20 || code == 9) {
            if (inWord) {
                inWord = false;
                if (console::Length(word) != 0) {
                    words->SetAtGrow(words->size, word);
                    word.Empty();
                }
            }
        } else {
            inWord = true;
            AppendCharAt(&word, tmp, p);
        }
        p = MbsInc(p);
    }
    if (advance) SkipToNextLine(self, kAfterRead, kInline);
    return 1;
}
EI_REPLACE(0x004DFCB0, TextSplit);

BOOL __fastcall TextFindLine(CTextManager* self, void*, const char* key) {
    console::CStringArray keyWords;
    console::CStringArray lineWords;
    mfc::CString word;
    if (!key) {
        // The next "#" header line (not a "##" comment).
        while (PeekWordCall(self, nullptr, &word)) {
            bool header;
            {
                mfc::CString first = word.Left(1);
                header = MbsCmp(first, kHeader) == 0 && !IsComment(word);
            }
            if (header) return 1;
            Advance(self, kFindFirst);
        }
        return 0;
    }
    StringSplit(key, &keyWords, ' ');
    if (keyWords.size <= 1) {
        // One word: the line whose first word is the key.
        while (PeekWordCall(self, nullptr, &word)) {
            if (MbsCmp(word, key) == 0) return 1;
            mfc::CString next;
            Advance(self, kFindFirst);
            while (PeekWordCall(self, nullptr, &next)) {
                if (!IsComment(next)) break;
                Advance(self, kFindComment);
            }
        }
        return 0;
    }
    // Several words: the line whose first words, lower-cased, are the key's words.
    while (SplitCall(self, nullptr, &lineWords, 0)) {
        if (lineWords.size >= keyWords.size) {
            int i = 0;
            if (keyWords.size <= 0) return 1;
            for (;;) {
                word = lineWords.data[i];
                word.MakeUpper();
                if (MbsCmp(word, keyWords.data[i]) != 0) break;
                if (++i >= keyWords.size) return 1;
            }
        }
        mfc::CString next;
        Advance(self, kFindMulti);
        while (PeekWordCall(self, nullptr, &next)) {
            if (!IsComment(next)) break;
            SkipLineCall(self);
        }
    }
    return 0;
}
EI_REPLACE(0x004DF030, TextFindLine);
