// ResourceManager. See resource_manager.h.
#include "resource_manager.h"

#include <cstring>
#include <type_traits>
#include <vector>

namespace {

bool SameName(const std::string& a, const std::string& b) { return a == b; }

// Closes and deletes every archive (closed, then its scalar deleting destructor) and empties
// the list.
void CloseAll(std::vector<ArchiveEntry>& archives) {
    for (ArchiveEntry& e : archives) {
        e.resource->CloseResource();
        if (e.resource != nullptr) delete e.resource;
    }
    archives.clear();
}

MappedResource* NewMappedResource() { return new MappedResource(); }
MappedResource* NewMmioResource() { return new MMIOResource(); }

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() {
    CloseAll(archives);
    BaseManager::Destroy();
}

void ResourceManager::Destroy() {
    CloseAll(archives);
    BaseManager::Destroy();
}


bool ResourceManager::AddArchive(const std::string& path) {
    return AddArchiveOf(path, NewMappedResource);
}

bool ResourceManager::AddMmioArchive(const std::string& path) {
    return AddArchiveOf(path, NewMmioResource);
}

bool ResourceManager::AddArchiveOf(const std::string& path, MappedResource* (*create)()) {
    for (const ArchiveEntry& e : archives)
        if (SameName(e.name, path)) return true;

    MappedResource* resource = create();
    {
        archives.push_back(ArchiveEntry(path, resource));
    }
    // The result is ignored: a missing archive stays in the list, empty.
    archives.back().resource->OpenResource(path);
    return true;
}

bool ResourceManager::RemoveArchive(const std::string& path) {
    for (auto e = archives.begin(); e != archives.end(); ++e) {
        if (!SameName(e->name, path)) continue;
        e->resource->CloseResource();
        delete e->resource;
        archives.erase(e);
        return true;
    }
    return false;
}

bool ResourceManager::RemoveAllArchives() {
    CloseAll(archives);
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
        if (e.resource->FindEntry(name) != -1) return true;
    return false;
}

void ResourceManager::FreeAllFiles() {
    for (ArchiveEntry& e : archives) e.resource->FreeAllFiles();
}
