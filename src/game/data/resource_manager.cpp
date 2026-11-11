// ResourceManager. See resource_manager.h.
#include "game/data/resource_manager.h"

#include "game/core/engine_globals.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Deletes every archive (its destructor closes it) and empties the list.
void CloseAll(std::vector<ArchiveEntry>& archives) {
    for (ArchiveEntry& e : archives) delete e.resource;
    archives.clear();
}

} // namespace

// The game's folders (engine_globals.h; app/app.cpp sets them).
std::string g_startFolder;
std::string g_textFolder;
std::string g_dataFolder;

// --- Construction and destruction ---------------------------------------------------------------

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() { Destroy(); }

void ResourceManager::Destroy() {
    CloseAll(archives);
    BaseManager::Destroy();
}

bool ResourceManager::AddArchive(const std::string& path) { return AddArchiveOf(path, false); }

bool ResourceManager::AddMmioArchive(const std::string& path) { return AddArchiveOf(path, true); }

bool ResourceManager::AddArchiveOf(const std::string& path, bool mapped) {
    for (const ArchiveEntry& e : archives)
        if (e.name == path) return true;

    MappedResource* resource = mapped ? new MMIOResource() : new MappedResource();
    archives.push_back({path, resource});
    // The result is ignored: a missing archive stays in the list, empty.
    resource->OpenResource(path);
    return true;
}

void* ResourceManager::GetFile(std::string_view name, std::uint32_t* size) {
    for (ArchiveEntry& e : archives)
        if (void* data = e.resource->GetFile(name, size)) return data;
    return nullptr;
}

bool ResourceManager::CloseFile(void* data) {
    for (ArchiveEntry& e : archives)
        if (e.resource->CloseFile(data)) return true;
    return false;
}

bool ResourceManager::HasFile(std::string_view name) {
    for (ArchiveEntry& e : archives)
        if (e.resource->HasFile(name)) return true;
    return false;
}

void ResourceManager::MakePngs(std::span<const std::string> names) {
    std::vector<std::vector<std::string>> inArchive(archives.size());
    for (const std::string& name : names)
        for (std::size_t i = 0; i < archives.size(); ++i)
            if (archives[i].resource->HasFile(name)) {
                inArchive[i].push_back(name);
                break;
            }
    for (std::size_t i = 0; i < archives.size(); ++i)
        if (!inArchive[i].empty()) archives[i].resource->MakePngs(inArchive[i]);
}

RecordFile ResourceManager::FindRecord(std::string_view name) {
    RecordFile record;
    for (ArchiveEntry& e : archives) {
        RecordFile found = e.resource->FindRecord(name);
        for (TomlFile& toml : found.tomls) record.tomls.push_back(std::move(toml));
        record.replaced = found.replaced;
        if (found.replaced) break;
        if (!found.found) continue;
        record.found = true;
        record.original = std::move(found.original);
        break;
    }
    return record;
}

void ResourceManager::FreeAllFiles() {
    for (ArchiveEntry& e : archives) e.resource->FreeAllFiles();
}
