// The text manager's string lookup and the item texts (names, descriptions, kinds). The line reader is
// text_manager.cpp, the archive list text_manager.cpp. See text_manager.h.
#include "text_manager.h"

#include "asset_text.h"
#include "engine_object.h"
#include "item.h"
#include "resource.h"
#include "spell.h"
#include "text_split.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>

using namespace itemdb;

namespace {

// The record's name as used in keys: spaces become '_', lower case. `name` is a string field of a database record
// that may be null (reading it then faults).
std::string KeyName(const std::string& name) {
    std::string key = name;
    text::ReplaceChar(&key, ' ', '_');
    str::MakeLower(key);
    return key;
}

// Looks up `key`; when found, reads the name line and the description.
void ReadItemText(TextManager* texts, const std::string& key, ItemTexts* out) {
    if (texts->Open(key)) {
        out->name = texts->ReadLine(true);
        out->description = texts->ReadToHash();
    }
}

// The text key and kind key of a loot item standing for a record.
void RecordKeys(const ItemLoot* loot, std::string* key, const char** kindKey) {
    switch (loot->recordType) {
    case LootRecordType::Weapon:
        *key = "instr " + KeyName(WeaponAt(loot->index)->name);
        *kindKey = "item_weapon_shape";
        break;
    case LootRecordType::Armor: {
        const ArmorRecord* armor = ArmorAt(loot->index);
        *key = "instr " + KeyName(armor->name);
        *kindKey = IsUpperBodyArmor(armor->type) ? "item_armor_shape_upper" : "item_armor_shape_lower";
        break;
    }
    case LootRecordType::Quick:
        *key = "instr " + KeyName(QuickItemAt(loot->index)->name);
        *kindKey = "item_wand_shape";
        break;
    case LootRecordType::Spell:
        *key = std::string("spell ") + SpellAt(loot->index)->code;
        *kindKey = "item_prototype";
        break;
    case LootRecordType::Modifier:
        *key = std::string("modifier ") + SpellModifierAt(loot->index)->code;
        *kindKey = "item_modifier";
        break;
    default: break;
    }
}

} // namespace

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

ItemTexts TextManager::GetItemTexts(Item* item) {
    std::uint32_t materialIndex = MaterialIndex(item->prototypeId);
    std::uint32_t prototypeIndex = PrototypeIndex(item->prototypeId);
    std::string key;                 // the item's text; none: no name and description
    const char* kindKey = nullptr;   // the kind's string; none: no kind
    bool wand = false;
    switch (item->Kind()) {
    case ItemKind::Weapon:
        key = "weapon " + KeyName(WeaponAt(prototypeIndex)->name) + " " + KeyName(MaterialAt(materialIndex)->key);
        kindKey = "item_weapon";
        break;
    case ItemKind::Armor: {
        const ArmorRecord* armor = ArmorAt(prototypeIndex);
        key = "armor " + KeyName(armor->name) + " " + KeyName(MaterialAt(materialIndex)->key);
        kindKey = IsUpperBodyArmor(armor->type) ? "item_armor_upper" : "item_armor_lower";
        break;
    }
    case ItemKind::Quick: {
        key = "qitem " + KeyName(QuickItemAt(prototypeIndex)->name);
        // A quick item whose prototype has no material is a potion.
        const ItemRecordHead* prototype = item->GetPrototype();
        if (prototype && text::CompareIgnoringCase(prototype->materialClass, "none") == 0) {
            kindKey = "item_potion";
            break;
        }
        key += " " + KeyName(MaterialAt(materialIndex)->key);
        // Only wands get a kind; their name is followed by the spell's name.
        wand = item->GetCategory() == QuickItemCategory::Wand;
        if (wand) kindKey = "item_wand";
        break;
    }
    case ItemKind::Loot: {
        auto* loot = AsLoot(item);
        if (loot->kind == LootKind::Self) {
            key = "litem " + KeyName(LootAt(prototypeIndex)->name);
            kindKey = "item_loot";
        } else if (loot->kind == LootKind::Material) {
            key = "material " + KeyName(MaterialAt(loot->index)->key);
            kindKey = "item_material";
        } else if (loot->kind == LootKind::Record) {
            RecordKeys(loot, &key, &kindKey);
        }
        break;
    }
    case ItemKind::Scroll:
        if (Spell* spell = item->spell) {
            key = std::string("spell ") + spell->GetPrototype()->code;
            kindKey = "item_spell";
        }
        break;
    case ItemKind::Quest:
        key = "questitem " + KeyName(QuestItemAt(prototypeIndex)->name);
        kindKey = "item_quest";
        break;
    default: break;
    }

    ItemTexts texts;
    if (!key.empty()) ReadItemText(this, key, &texts);
    if (wand) {
        if (Spell* spell = item->spell) {
            std::string spellName = "Unknown";
            if (Open(std::string("spell ") + spell->GetPrototype()->code)) spellName = ReadLine(true);
            texts.name += ": " + spellName;
        }
    }
    if (kindKey) texts.kind = GetString(kindKey);
    return texts;
}

std::string TextManager::FormatItemName(Item* item) {
    ItemTexts texts = GetItemTexts(item);
    if (static_cast<int>(item->count) > 1) return str::Format(GetString("format_item2"), texts.kind, texts.name, item->count);
    return str::Format(GetString("format_item1"), texts.kind, texts.name);
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
// into `*source` (when not null).
void* FindFile(TextSources* list, std::string_view key, std::uint32_t* size, MappedResource** source) {
    // A key that is text (the people's names, "pers <name>") back in the archive's code page.
    std::string assetKey;
    if (text::ToAssetText(key, &assetKey)) key = assetKey;
    for (const TextSource& archive : list->archives) {
        if (void* data = archive.source->GetFile(key, size)) {
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
    source->CloseResource();
    delete source;   // the scalar deleting destructor (flag 1); a null source is skipped
}

} // namespace

bool TextSources::Add(const std::string& path, bool writable) {
    for (const TextSource& archive : archives)
        if (SameName(archive, path)) return true;
    auto* resource = new MappedResource();
    archives.push_back(TextSource{path, resource});
    return archives.back().source->OpenResource(path, writable);
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
    return sources.Add(path, 0);
}

void TextManager::RemoveSource(const std::string& path) { sources.Remove(path); }

void TextManager::ClearSources() { sources.Clear(); }

TextManager::TextManager() {
    TextManager::Create();
}

TextManager::~TextManager() { ClearSources(); }

void* TextSources::Find(std::string_view key, std::uint32_t* size) {
    return FindFile(this, key, size, nullptr);
}

void* TextSources::FindKeyText(std::string_view key, std::uint32_t* size) {
    MappedResource* source = nullptr;
    void* data = FindFile(this, key, size, &source);
    return data ? AsGameText(source, data, size) : nullptr;
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
