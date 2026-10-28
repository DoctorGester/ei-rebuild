// The text manager's archive list: adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in item_db_text.h.
#include "item_db_text.h"
#include "resource.h"

#include "asset_text.h"
#include "engine_object.h"

#include "lib/crt.h"

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
    const CMappedResource* source = nullptr;   // the archive holding the data
    std::uint32_t size = 0;                    // the data's size in its archive
    std::string text;
};
std::unordered_map<const void*, ConvertedText>& ConvertedTexts() {
    static std::unordered_map<const void*, ConvertedText> texts;
    return texts;
}

// A text of the database at `data` (`*size` bytes) in `source` as the game reads it: its UTF-8 copy (`*size`
// becomes the copy's length).
void* AsGameText(const CMappedResource* source, void* data, std::uint32_t* size) {
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
void ForgetConvertedTexts(const CMappedResource* source) {
    auto& texts = ConvertedTexts();
    for (auto it = texts.begin(); it != texts.end();) it = it->second.source == source ? texts.erase(it) : std::next(it);
}

// The lookup: the data of file `key` in the first archive of `list` that has it, its archive
// into `*source` (when not null).
void* FindFile(TextSources* list, const char* key, std::uint32_t* size, CMappedResource** source) {
    // A key that is text (the people's names, "pers <name>") back in the archive's code page.
    std::string assetKey;
    if (text::ToAssetText(key, &assetKey)) key = assetKey.c_str();
    for (const TextSource& archive : list->archives) {
        if (void* data = archive.source->GetFile(key, size)) {
            if (source) *source = archive.source;
            return data;
        }
    }
    return nullptr;
}

// The exact name comparison of the list (length, then the bytes).
bool SameName(const TextSource& archive, const char* path) {
    std::size_t length = std::strlen(path);
    return archive.name.size() == length && std::memcmp(archive.name.data(), path, length) == 0;
}

void CloseAndDelete(CMappedResource* source) {
    ForgetConvertedTexts(source);
    source->CloseResource();
    delete source;   // the scalar deleting destructor (slot 0, flag 1); a null source is skipped
}

} // namespace

BOOL TextSources::Add(const char* path, BOOL writable) {
    for (const TextSource& archive : archives)
        if (SameName(archive, path)) return 1;
    auto* resource = new CMappedResource();
    archives.push_back(TextSource{std::string(path), resource});
    return archives.back().source->OpenResource(path, writable);
}

void TextSources::Remove(const char* path) {
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

BOOL CTextManager::AddSource(const char* path) {
    return sources_.Add(path, 0);
}

void CTextManager::RemoveSource(const char* path) { sources_.Remove(path); }

void CTextManager::ClearSources() { sources_.Clear(); }

CTextManager::CTextManager() {
    ClearFields();
    CTextManager::Create();
}

CTextManager::~CTextManager() { ClearSources(); }

void* TextSources::Find(const char* key, std::uint32_t* size) {
    return FindFile(this, key, size, nullptr);
}

void* TextSources::FindKeyText(const char* key, std::uint32_t* size) {
    CMappedResource* source = nullptr;
    void* data = FindFile(this, key, size, &source);
    return data ? AsGameText(source, data, size) : nullptr;
}

void TextSources::ListKeys(std::vector<std::string>* out, const char* pattern) {
    std::vector<std::string> found;
    out->clear();
    for (const TextSource& archive : archives) {
        archive.source->ListFiles(&found, pattern);
        out->insert(out->end(), found.begin(), found.end());
    }
}

void CTextManager::ListKeys(std::vector<std::string>* out, const char* pattern) {
    sources_.ListKeys(out, pattern);
    // The names in UTF-8, as Find takes them (one person's key holds Cyrillic).
    for (std::string& name : *out) text::FromAssetText(&name);
}

int CTextManager::Create() {
    // The original set LC_CTYPE from the "Font CharSet" option ("english" unless the registry says otherwise),
    // then the multibyte code page from it. The text is UTF-8 and the CRT's case and classes do not depend on a
    // locale (lib/text_case.h): the option is not used and LC_CTYPE stays "C".
    return 1;
}
