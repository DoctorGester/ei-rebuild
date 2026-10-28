// CResourceManager. See resource_manager.h.
#include "resource_manager.h"

#include "lib/crt.h"

#include <cstring>
#include <type_traits>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

bool SameName(const stlp::string& a, const char* b) {
    std::size_t n = std::strlen(b);
    return a.size() == n && std::memcmp(a.data(), b, n) == 0;
}

// Closes and deletes every archive (its slot 3, then its scalar deleting destructor) and empties
// the list.
void CloseAll(stlp::vector<ArchiveEntry>& archives) {
    for (ArchiveEntry& e : archives) {
        e.resource->CloseResource();
        if (e.resource != nullptr) delete e.resource;
    }
    archives.clear();
}
} // namespace

// --- Construction and destruction ---------------------------------------------------------------

// The archive list has no destructor (the destructor frees it); its default constructor only nulls it.
static_assert(std::is_trivially_destructible_v<stlp::vector<ArchiveEntry>>);

CResourceManager::CResourceManager() { std::memset(&archives_, 0, sizeof(archives_)); }

CResourceManager::~CResourceManager() {
    CloseAll(archives_);
    CBaseManager::Destroy();
    archives_.clear();
    archives_.FreeStorage();
}

void CResourceManager::Destroy() {
    CloseAll(archives_);
    CBaseManager::Destroy();
}


namespace {
// CMappedResource's constructor and CMMIOResource's on the archive's memory.
CMappedResource* __fastcall ConstructMappedResource(void* memory) { return new (memory) CMappedResource(); }
CMappedResource* __fastcall ConstructMmioResource(void* memory) { return new (memory) CMMIOResource(); }
} // namespace

BOOL CResourceManager::AddArchive(const char* path) {
    return AddArchiveOf(path, sizeof(CMappedResource), ConstructMappedResource);
}

BOOL CResourceManager::AddMmioArchive(const char* path) {
    return AddArchiveOf(path, sizeof(CMMIOResource), ConstructMmioResource);
}

BOOL CResourceManager::AddArchiveOf(const char* path, std::size_t size, CMappedResource*(__fastcall* construct)(void*)) {
    for (const ArchiveEntry& e : archives_)
        if (SameName(e.name, path)) return 1;

    void* memory = gamecrt::operator_new(size);
    CMappedResource* resource = memory ? construct(memory) : nullptr;
    {
        stlp::string name(path);
        stlp::string copy(name);
        archives_.push_back(ArchiveEntry(copy, resource));
    }
    // The result is ignored: a missing archive stays in the list, empty.
    archives_.back().resource->OpenResource(path, 0);
    return 1;
}

BOOL CResourceManager::RemoveArchive(const char* path) {
    for (ArchiveEntry* e = archives_.begin(); e != archives_.end(); ++e) {
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
