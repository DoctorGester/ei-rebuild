// The text manager's string lookup (the items' texts are items/item_texts.cpp's). The line reader is
// text_manager.cpp, the archive list text_manager.cpp. See text_manager.h.
#include "engine/data/text_manager.h"

#include "engine/core/asset_text.h"
#include "engine/core/engine_object.h"
#include "engine/data/resource.h"
#include "engine/core/text_split.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>

int TextKeyFromName(std::string_view name) {
    std::uint32_t h = 0;
    for (char c : name.substr(0, name.find('\0')))
        h = static_cast<std::uint32_t>(text::ToUpper(static_cast<unsigned char>(c))) + h * 5;
    return static_cast<int>(h % 1000000000u + 1000000000u);
}

bool IsTextKey(int id) {
    return id >= 1000000000 && id < 2000000000;
}

bool TextManager::IsPastEnd(const char* p) {
    int offset = static_cast<int>(p - text);
    return offset >= static_cast<int>(length);
}

std::optional<std::string> TextManager::FindString(std::string_view name) {
    if (cursor) {
        text = nullptr;
        cursor = nullptr;
    }
    std::optional<std::string> line;
    if (Open("string " + std::string(name))) {
        line = ReadToHash();
        str::TrimRight(*line, "\r\n");
    }
    text = nullptr;
    cursor = nullptr;
    return line;
}

std::string TextManager::GetString(std::string_view name) {
    std::optional<std::string> line = FindString(name);
    return line ? std::move(*line) : std::string(name);
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kTextManagerClassName = "CTextManager";
} // namespace

const char* TextManager::ClassName() { return kTextManagerClassName; }
ClassId TextManager::GetClassId() { return ClassId::TextManager; }

// --- Text reader -------------------------------------------------------------------------------------

// The text manager's line reader (TextManager, text_manager.h).
// The game inlines the same few loops many times with small differences (the end test inline
// or through IsPastEnd, the character code inline or through a helper); `Advance` below
// reproduces each variant by its call pattern.

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
    Test entry = {};  // the first test of the cursor
    Test loopEnd = {};  // the end test of the scanning loop
    Test exit = {};  // the final test (cursor = p at the end, else just past the LF)
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
        if (str::Compare(str::Left(*word, 1), kHeader) == 0 && !IsComment(*word)) return true;
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

// --- Text sources ------------------------------------------------------------------------------------

// The text manager's archive list: adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in text_manager.h.

namespace {

// The UTF-8 copy of each text found, by the address of its data. An archive keeps a file's
// data in place until the archive is closed (GetFile shares it; the text manager never closes a
// file), so the address names the text until its archive is closed, and only then is the copy
// forgotten: a text manager may still be reading a text of another archive, or of another list.
struct ConvertedText {
    const MappedResource* source = nullptr;   // the archive holding the data
    std::uint32_t size = 0;                    // the data's size in its archive
    std::string text;
};
std::unordered_map<const void*, ConvertedText>& ConvertedTexts() {
    static std::unordered_map<const void*, ConvertedText> texts;
    return texts;
}

// A text of the database at `data` (`*size` bytes) in `source` as the game reads it: its UTF-8 copy (`*size`
// becomes the copy's length).
void* AsGameText(const MappedResource* source, void* data, std::uint32_t* size) {
    ConvertedText& entry = ConvertedTexts()[data];
    if (entry.text.empty() || entry.source != source || entry.size != *size) {
        entry.source = source;
        entry.size = *size;
        entry.text = text::FromAssetText(std::string_view(static_cast<const char*>(data), *size));
    }
    *size = static_cast<std::uint32_t>(entry.text.size());
    return entry.text.data();
}

// The copies of the texts of `source`, which is being closed, forgotten.
void ForgetConvertedTexts(const MappedResource* source) {
    auto& texts = ConvertedTexts();
    for (auto it = texts.begin(); it != texts.end();) it = it->second.source == source ? texts.erase(it) : std::next(it);
}

// The lookup: the data of file `key` in the first archive of `list` that has it, its archive
// into `*source` (when not null). With `utf8` a text of a texts.toml too (MappedResource::FindText; *utf8 true then).
void* FindFile(TextSources* list, std::string_view key, std::uint32_t* size, MappedResource** source, bool* utf8) {
    // A key that is text (the people's names, "pers <name>") back in the archive's code page.
    std::string assetKey;
    if (text::ToAssetText(key, &assetKey)) key = assetKey;
    for (const TextSource& archive : list->archives) {
        void* data = utf8 ? archive.source->FindText(key, size, utf8) : archive.source->GetFile(key, size);
        if (data) {
            if (source) *source = archive.source;
            return data;
        }
    }
    return nullptr;
}

// The exact name comparison of the list (length, then the bytes).
bool SameName(const TextSource& archive, const std::string& path) {
    return archive.name.size() == path.size() && std::memcmp(archive.name.data(), path.data(), path.size()) == 0;
}

void CloseAndDelete(MappedResource* source) {
    ForgetConvertedTexts(source);
    delete source;   // closes it
}

} // namespace

bool TextSources::Add(const std::string& path) {
    for (const TextSource& archive : archives)
        if (SameName(archive, path)) return true;
    auto* resource = new MappedResource();
    archives.push_back(TextSource{path, resource});
    return archives.back().source->OpenResource(path);
}

void TextSources::Remove(const std::string& path) {
    for (auto archive = archives.begin(); archive != archives.end(); ++archive) {
        if (!SameName(*archive, path)) continue;
        CloseAndDelete(archive->source);
        archives.erase(archive);
        return;
    }
}

void TextSources::Clear() {
    for (const TextSource& archive : archives) CloseAndDelete(archive.source);
    archives.clear();
}

bool TextManager::AddSource(const std::string& path) {
    return sources.Add(path);
}

void TextManager::RemoveSource(const std::string& path) { sources.Remove(path); }

void TextManager::ClearSources() { sources.Clear(); }

TextManager::TextManager() {
    TextManager::Create();
}

TextManager::~TextManager() { ClearSources(); }

void* TextSources::Find(std::string_view key, std::uint32_t* size) {
    return FindFile(this, key, size, nullptr, nullptr);
}

void* TextSources::FindKeyText(std::string_view key, std::uint32_t* size) {
    MappedResource* source = nullptr;
    bool utf8 = false;
    void* data = FindFile(this, key, size, &source, &utf8);
    return data && !utf8 ? AsGameText(source, data, size) : data;
}

std::vector<std::string> TextSources::ListKeys(const char* pattern) {
    std::vector<std::string> keys;
    std::vector<std::string> found;
    for (const TextSource& archive : archives) {
        archive.source->ListFiles(&found, pattern);
        keys.insert(keys.end(), found.begin(), found.end());
    }
    return keys;
}

std::vector<std::string> TextManager::ListKeys(const char* pattern) {
    std::vector<std::string> keys = sources.ListKeys(pattern);
    // The names in UTF-8, as Find takes them (one person's key holds Cyrillic).
    for (std::string& name : keys) text::FromAssetText(&name);
    return keys;
}

void TextManager::Create() {
    // The original set LC_CTYPE from the "Font CharSet" option ("english" unless the registry says otherwise),
    // then the multibyte code page from it. The text is UTF-8 and the CRT's case and classes do not depend on a
    // locale (lib/text_case.h): the option is not used and LC_CTYPE stays "C".
}
