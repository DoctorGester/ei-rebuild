// The text manager's archive list: adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in text_manager.h.
#include "text_manager.h"
#include "resource.h"

#include "asset_text.h"
#include "engine_object.h"

#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>

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
