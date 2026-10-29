// The text manager's line reader (TextManager, text_manager.h).
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
bool Inside(const TextManager* t, const char* p) {
    return static_cast<int>(p - t->text) < static_cast<int>(t->length);
}

// How one inlined "move the cursor past the next LF" tests the end and reads characters.
enum class Test { Inline, Call };
struct AdvanceKind {
    Test entry;     // the first test of the cursor
    Test loopEnd;   // the end test of the scanning loop
    Test exit;      // the final test (cursor = p at the end, else just past the LF)
};

bool InsideBy(TextManager* t, const char* p, Test test) {
    return test == Test::Inline ? Inside(t, p) : !t->IsPastEnd(p);
}

void Advance(TextManager* t, const AdvanceKind& kind) {
    const char* p = t->cursor;
    if (!InsideBy(t, p, kind.entry)) return;
    while (InsideBy(t, p, kind.loopEnd) && *p != '\n') ++p;
    t->cursor = InsideBy(t, p, kind.exit) ? p + 1 : p;
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
void SkipToNextLine(TextManager* t) {
    Advance(t, kAfterRead);
    while (std::optional<std::string> word = t->PeekWord()) {
        if (!IsComment(*word)) break;
        Advance(t, kInline);
    }
}

} // namespace

bool TextManager::Open(std::string_view key) {
    if (text) {
        text = nullptr;
        cursor = nullptr;
    }
    // The first archive that has `key` (TextSources::FindKeyText: the text's size written to length).
    const char* line = static_cast<const char*>(sources.FindKeyText(key, &length));
    cursor = line;
    text = line;
    return line && length != 0;
}

bool TextManager::OpenData(std::string_view key) {
    if (text) {
        text = nullptr;
        cursor = nullptr;
    }
    const char* data = static_cast<const char*>(sources.Find(key, &length));
    cursor = data;
    text = data;
    return data && length != 0;
}

void TextManager::Close() {
    text = nullptr;
    cursor = nullptr;
}

std::optional<std::string> TextManager::PeekWord() {
    const char* p = cursor;
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

bool TextManager::NextLine() {
    Advance(this, kInline);
    while (std::optional<std::string> word = PeekWord()) {
        if (!IsComment(*word)) return true;
        Advance(this, kInline);
    }
    return false;
}

std::string TextManager::ReadLine(bool advance) {
    const char* p = cursor;
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

std::string TextManager::ReadToHash() {
    const char* p = cursor;
    std::string line;
    if (!Inside(this, p)) return line;
    while (Inside(this, p)) {
        if (*p == '#') break;
        line.push_back(*p);
        ++p;
    }
    cursor = p;
    return line;
}

std::vector<std::string> TextManager::Split(bool advance) {
    std::vector<std::string> words;
    std::string word;
    bool inWord = false;
    const char* p = cursor;
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

bool TextManager::FindHeader() {
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

bool TextManager::FindLine(std::string_view key) {
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
    while (Inside(this, cursor)) {
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
