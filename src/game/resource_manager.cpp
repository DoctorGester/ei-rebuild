// CResourceManager. See resource_manager.h.
#include "resource_manager.h"

#include <cstring>
#include <type_traits>
#include <vector>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

bool SameName(const std::string& a, const char* b) {
    std::size_t n = std::strlen(b);
    return a.size() == n && std::memcmp(a.data(), b, n) == 0;
}

// Closes and deletes every archive (its slot 3, then its scalar deleting destructor) and empties
// the list.
void CloseAll(std::vector<ArchiveEntry>& archives) {
    for (ArchiveEntry& e : archives) {
        e.resource->CloseResource();
        if (e.resource != nullptr) delete e.resource;
    }
    archives.clear();
}
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


namespace {
CMappedResource* NewMappedResource() { return new CMappedResource(); }
CMappedResource* NewMmioResource() { return new CMMIOResource(); }
} // namespace

BOOL CResourceManager::AddArchive(const char* path) {
    return AddArchiveOf(path, NewMappedResource);
}

BOOL CResourceManager::AddMmioArchive(const char* path) {
    return AddArchiveOf(path, NewMmioResource);
}

BOOL CResourceManager::AddArchiveOf(const char* path, CMappedResource* (*create)()) {
    for (const ArchiveEntry& e : archives_)
        if (SameName(e.name, path)) return 1;

    CMappedResource* resource = create();
    {
        std::string name(path);
        std::string copy(name);
        archives_.push_back(ArchiveEntry(copy, resource));
    }
    // The result is ignored: a missing archive stays in the list, empty.
    archives_.back().resource->OpenResource(path, 0);
    return 1;
}

BOOL CResourceManager::RemoveArchive(const char* path) {
    for (auto e = archives_.begin(); e != archives_.end(); ++e) {
        if (!SameName(e->name, path)) continue;
        e->resource->CloseResource();
        delete e->resource;
        archives_.erase(e);
        return 1;
    }
    return 0;
}

BOOL CResourceManager::RemoveAllArchives() {
    CloseAll(archives_);
    return 1;
}

void* CResourceManager::GetFile(const char* name, std::uint32_t* size) {
    for (ArchiveEntry& e : archives_)
        if (void* data = e.resource->GetFile(name, size)) return data;
    return nullptr;
}

bool CResourceManager::CloseFile(void* data) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->CloseFile(data)) return true;
    return false;
}

BOOL CResourceManager::HasFile(const char* name) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->FindEntry(name) != -1) return 1;
    return 0;
}

void CResourceManager::FreeAllFiles() {
    for (ArchiveEntry& e : archives_) e.resource->FreeAllFiles();
}
