// CResourceManager. See resource_manager.h.
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

CMappedResource* NewMappedResource() { return new CMappedResource(); }
CMappedResource* NewMmioResource() { return new CMMIOResource(); }

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

CResourceManager::CResourceManager() = default;

CResourceManager::~CResourceManager() {
    CloseAll(archives_);
    CBaseManager::Destroy();
}

void CResourceManager::Destroy() {
    CloseAll(archives_);
    CBaseManager::Destroy();
}


bool CResourceManager::AddArchive(const std::string& path) {
    return AddArchiveOf(path, NewMappedResource);
}

bool CResourceManager::AddMmioArchive(const std::string& path) {
    return AddArchiveOf(path, NewMmioResource);
}

bool CResourceManager::AddArchiveOf(const std::string& path, CMappedResource* (*create)()) {
    for (const ArchiveEntry& e : archives_)
        if (SameName(e.name, path)) return true;

    CMappedResource* resource = create();
    {
        archives_.push_back(ArchiveEntry(path, resource));
    }
    // The result is ignored: a missing archive stays in the list, empty.
    archives_.back().resource->OpenResource(path, 0);
    return true;
}

bool CResourceManager::RemoveArchive(const std::string& path) {
    for (auto e = archives_.begin(); e != archives_.end(); ++e) {
        if (!SameName(e->name, path)) continue;
        e->resource->CloseResource();
        delete e->resource;
        archives_.erase(e);
        return true;
    }
    return false;
}

bool CResourceManager::RemoveAllArchives() {
    CloseAll(archives_);
    return true;
}

void* CResourceManager::GetFile(std::string_view name, std::uint32_t* size) {
    for (ArchiveEntry& e : archives_)
        if (void* data = e.resource->GetFile(name, size)) return data;
    return nullptr;
}

bool CResourceManager::CloseFile(void* data) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->CloseFile(data)) return true;
    return false;
}

bool CResourceManager::HasFile(std::string_view name) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->FindEntry(name) != -1) return true;
    return false;
}

void CResourceManager::FreeAllFiles() {
    for (ArchiveEntry& e : archives_) e.resource->FreeAllFiles();
}
