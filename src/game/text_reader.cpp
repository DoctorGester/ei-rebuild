// The text manager's line reader. See text_reader.h.
// The game inlines the same few loops many times with small differences (the end test inline
// or through IsPastEnd, the character code inline or through a helper); `Advance` below
// reproduces each variant by its call pattern.
#include "item_db_text.h"
#include "text_reader.h"

#include "resource.h"

#include "text_split.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace {

// Split (__fastcall, separator on the stack): splits `text` at `separator` into `out`.

const char* const kComment = "##";
const char* const kHeader = "#";

// The cursor `p` is inside the text (signed compare, as every inlined test).
bool Inside(const CTextManager* t, const char* p) {
    return static_cast<int>(p - t->text_) < static_cast<int>(t->length_);
}

// How one inlined "move the cursor past the next LF" tests the end and reads characters.
enum class Test { Inline, Call };
struct AdvanceKind {
    Test entry;     // the first test of the cursor
    Test loopEnd;   // the end test of the scanning loop
    Test exit;      // the final test (cursor = p at the end, else just past the LF)
};

bool InsideBy(CTextManager* t, const char* p, Test test) {
    return test == Test::Inline ? Inside(t, p) : !t->IsPastEnd(p);
}

void Advance(CTextManager* t, const AdvanceKind& kind) {
    const char* p = t->cursor_;
    if (!InsideBy(t, p, kind.entry)) return;
    while (InsideBy(t, p, kind.loopEnd) && *p != '\n') ++p;
    t->cursor_ = InsideBy(t, p, kind.exit) ? p + 1 : p;
}

constexpr AdvanceKind kAfterRead{Test::Inline, Test::Inline, Test::Call};     // ReadLine / Split
constexpr AdvanceKind kInline{Test::Inline, Test::Inline, Test::Inline};      // comment skipping
constexpr AdvanceKind kFindFirst{Test::Inline, Test::Call, Test::Call};       // FindLine, no key / one word
constexpr AdvanceKind kFindMulti{Test::Call, Test::Call, Test::Call};         // FindLine, several words
constexpr AdvanceKind kFindComment{Test::Inline, Test::Inline, Test::Call};   // FindLine, one word, comments

// Whether a word starts with "##".
bool IsComment(const std::string& word) {
    std::string start = str::Left(word, 2);
    return std::strcmp(start.c_str(), kComment) == 0;
}

// After a read: the cursor moves past the line (`first`), then past every "##" comment line.
void SkipToNextLine(CTextManager* t, const AdvanceKind& first, const AdvanceKind& comment) {
    std::string word;
    Advance(t, first);
    while (t->PeekWord(&word)) {
        if (!IsComment(word)) break;
        Advance(t, comment);
    }
}

} // namespace

BOOL CTextManager::Open(const char* key) {
    if (text_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    // The first archive that has `key` (TextSources::FindKeyText: the text's size written to length_).
    const char* text = static_cast<const char*>(sources_.FindKeyText(key, &length_));
    cursor_ = text;
    text_ = text;
    return text && length_ != 0;
}

BOOL CTextManager::OpenData(const char* key) {
    if (text_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    const char* data = static_cast<const char*>(sources_.Find(key, &length_));
    cursor_ = data;
    text_ = data;
    return data && length_ != 0;
}

BOOL TextClose(CTextManager* self) {
    self->text_ = nullptr;
    self->cursor_ = nullptr;
    return 1;
}

BOOL CTextManager::PeekWord(std::string* out) {
    const char* p = cursor_;
    bool started = false;
    out->clear();
    if (!Inside(this, p)) return 0;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 13 || code == 9) break;
        if (code == 0x20 || code == 10) {
            if (started) break;
        } else {
            started = true;
            out->push_back(*p);
        }
        ++p;
    }
    return 1;
}

BOOL TextSkipLine(CTextManager* self) {
    const char* p = self->cursor_;
    if (!Inside(self, p)) return 0;
    while (Inside(self, p) && *p != 10) ++p;
    if (!Inside(self, p)) {
        self->cursor_ = p;
        return 0;
    }
    self->cursor_ = p + 1;
    return 1;
}

BOOL TextNextLine(CTextManager* self) {
    std::string word;
    Advance(self, kInline);
    while (self->PeekWord(&word)) {
        if (!IsComment(word)) return 1;
        Advance(self, kInline);
    }
    return 0;
}

BOOL CTextManager::ReadLine(std::string* out, BOOL advance) {
    const char* p = cursor_;
    out->clear();
    if (!Inside(this, p)) return 0;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 10 || code == 13) break;
        out->push_back(*p);
        ++p;
    }
    if (advance) SkipToNextLine(this, kAfterRead, kInline);
    return 1;
}

BOOL CTextManager::ReadToHash(std::string* out, BOOL advance) {
    const char* p = cursor_;
    out->clear();
    if (!Inside(this, p)) return 0;
    while (Inside(this, p)) {
        if (*p == '#') break;
        out->push_back(*p);
        ++p;
    }
    if (advance) cursor_ = p;
    return 1;
}

BOOL CTextManager::Split(std::vector<std::string>* words, BOOL advance) {
    std::string word;
    bool inWord = false;
    const char* p = cursor_;
    words->clear();
    if (!Inside(this, p)) return 0;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 10 || code == 13) {
            // The line's last word (a word still open when the text ends is not added).
            if (!word.empty()) {
                words->push_back(word);
                word.clear();
            }
            break;
        }
        if (code == 0x20 || code == 9) {
            if (inWord) {
                inWord = false;
                if (!word.empty()) {
                    words->push_back(word);
                    word.clear();
                }
            }
        } else {
            inWord = true;
            word.push_back(*p);
        }
        ++p;
    }
    if (advance) SkipToNextLine(this, kAfterRead, kInline);
    return 1;
}

BOOL CTextManager::FindLine(const char* key) {
    std::vector<std::string> keyWords;
    std::vector<std::string> lineWords;
    std::string word;
    if (!key) {
        // The next "#" header line (not a "##" comment).
        while (PeekWord(&word)) {
            bool header;
            {
                std::string first = str::Left(word, 1);
                header = std::strcmp(first.c_str(), kHeader) == 0 && !IsComment(word);
            }
            if (header) return 1;
            Advance(this, kFindFirst);
        }
        return 0;
    }
    text::Split(key, &keyWords, ' ');
    if (keyWords.size() <= 1) {
        // One word: the line whose first word is the key.
        while (PeekWord(&word)) {
            if (std::strcmp(word.c_str(), key) == 0) return 1;
            std::string next;
            Advance(this, kFindFirst);
            while (PeekWord(&next)) {
                if (!IsComment(next)) break;
                Advance(this, kFindComment);
            }
        }
        return 0;
    }
    // Several words: the line whose first words, lower-cased, are the key's words.
    while (Split(&lineWords, 0)) {
        if (lineWords.size() >= keyWords.size()) {
            std::size_t i = 0;
            if (keyWords.empty()) return 1;
            for (;;) {
                word = lineWords[i];
                str::MakeLower(word);
                if (std::strcmp(word.c_str(), keyWords[i].c_str()) != 0) break;
                if (++i >= keyWords.size()) return 1;
            }
        }
        std::string next;
        Advance(this, kFindMulti);
        while (PeekWord(&next)) {
            if (!IsComment(next)) break;
            TextSkipLine(this);
        }
    }
    return 0;
}
