// The text manager's line reader (CTextManager, text_manager.h).
// The game inlines the same few loops many times with small differences (the end test inline
// or through IsPastEnd, the character code inline or through a helper); `Advance` below
// reproduces each variant by its call pattern.
#include "text_manager.h"

#include "resource.h"

#include "text_split.h"
#include "lib/str.h"

#include <cstring>
#include <optional>
#include <string>

namespace {

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
    return str::Compare(start, kComment) == 0;
}

// After a read: the cursor moves past the line, then past every "##" comment line.
void SkipToNextLine(CTextManager* t) {
    Advance(t, kAfterRead);
    while (std::optional<std::string> word = t->PeekWord()) {
        if (!IsComment(*word)) break;
        Advance(t, kInline);
    }
}

} // namespace

bool CTextManager::Open(std::string_view key) {
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

bool CTextManager::OpenData(std::string_view key) {
    if (text_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    const char* data = static_cast<const char*>(sources_.Find(key, &length_));
    cursor_ = data;
    text_ = data;
    return data && length_ != 0;
}

void CTextManager::Close() {
    text_ = nullptr;
    cursor_ = nullptr;
}

std::optional<std::string> CTextManager::PeekWord() {
    const char* p = cursor_;
    bool started = false;
    if (!Inside(this, p)) return std::nullopt;
    std::string word;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 13 || code == 9) break;
        if (code == 0x20 || code == 10) {
            if (started) break;
        } else {
            started = true;
            word.push_back(*p);
        }
        ++p;
    }
    return word;
}

bool CTextManager::NextLine() {
    Advance(this, kInline);
    while (std::optional<std::string> word = PeekWord()) {
        if (!IsComment(*word)) return true;
        Advance(this, kInline);
    }
    return false;
}

std::string CTextManager::ReadLine(bool advance) {
    const char* p = cursor_;
    std::string line;
    if (!Inside(this, p)) return line;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 10 || code == 13) break;
        line.push_back(*p);
        ++p;
    }
    if (advance) SkipToNextLine(this);
    return line;
}

std::string CTextManager::ReadToHash() {
    const char* p = cursor_;
    std::string text;
    if (!Inside(this, p)) return text;
    while (Inside(this, p)) {
        if (*p == '#') break;
        text.push_back(*p);
        ++p;
    }
    cursor_ = p;
    return text;
}

std::vector<std::string> CTextManager::Split(bool advance) {
    std::vector<std::string> words;
    std::string word;
    bool inWord = false;
    const char* p = cursor_;
    if (!Inside(this, p)) return words;
    while (Inside(this, p)) {
        const char code = *p;
        if (code == 10 || code == 13) {
            // The line's last word (a word still open when the text ends is not added).
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
            break;
        }
        if (code == 0x20 || code == 9) {
            if (inWord) {
                inWord = false;
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
            }
        } else {
            inWord = true;
            word.push_back(*p);
        }
        ++p;
    }
    if (advance) SkipToNextLine(this);
    return words;
}

bool CTextManager::FindHeader() {
    while (std::optional<std::string> word = PeekWord()) {
        bool header;
        {
            std::string first = str::Left(*word, 1);
            header = str::Compare(first, kHeader) == 0 && !IsComment(*word);
        }
        if (header) return true;
        Advance(this, kFindFirst);
    }
    return false;
}

bool CTextManager::FindLine(std::string_view key) {
    const std::vector<std::string> keyWords = text::Split(key, ' ');
    if (keyWords.size() <= 1) {
        // One word: the line whose first word is the key.
        while (std::optional<std::string> word = PeekWord()) {
            if (str::Compare(*word, key) == 0) return true;
            Advance(this, kFindFirst);
            while (std::optional<std::string> next = PeekWord()) {
                if (!IsComment(*next)) break;
                Advance(this, kFindComment);
            }
        }
        return false;
    }
    // Several words: the line whose first words, lower-cased, are the key's words.
    std::string word;
    while (Inside(this, cursor_)) {
        const std::vector<std::string> lineWords = Split(false);
        if (lineWords.size() >= keyWords.size()) {
            std::size_t i = 0;
            if (keyWords.empty()) return true;
            for (;;) {
                word = lineWords[i];
                str::MakeLower(word);
                if (str::Compare(word, keyWords[i]) != 0) break;
                if (++i >= keyWords.size()) return true;
            }
        }
        Advance(this, kFindMulti);
        while (std::optional<std::string> next = PeekWord()) {
            if (!IsComment(*next)) break;
            Advance(this, kInline);
        }
    }
    return false;
}
