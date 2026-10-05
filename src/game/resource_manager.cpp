// CResourceManager (00674D90-00675530, 005AA360). See resource_manager.h.
#include "resource_manager.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace {
// Archive constructors (not rewritten yet; they build the STLport hash maps).
__declspec(naked) CMappedResource* __fastcall ConstructMappedResource(void*) EI_ORIGINAL(0x00671280)
__declspec(naked) CMappedResource* __fastcall ConstructMmioResource(void*) EI_ORIGINAL(0x006721D0)

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

CResourceManager* CResourceManager::Construct() {
    ConstructBase();
    std::memset(&archives_, 0, sizeof(archives_));
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x00674D90, CResourceManager::Construct);

void CResourceManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    CloseAll(archives_);
    CBaseManager::Destroy();
    archives_.clear();
    archives_.FreeStorage();
    DestructBody();
}
EI_REPLACE_METHOD(0x00674DF0, CResourceManager::Destruct);

CResourceManager* CResourceManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00674DD0, CResourceManager::DeletingDestruct);

void CResourceManager::Destroy() {
    CloseAll(archives_);
    CBaseManager::Destroy();
}

// --- Archives -----------------------------------------------------------------------------------

BOOL CResourceManager::AddArchive(const char* path) {
    return AddArchiveOf(path, sizeof(CMappedResource), ConstructMappedResource);
}

BOOL CResourceManager::AddMmioArchive(const char* path) {
    return AddArchiveOf(path, sizeof(CMMIOResource), ConstructMmioResource);
}
EI_REPLACE_METHOD(0x005AA360, CResourceManager::AddMmioArchive);

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

namespace {
// The virtual slots' entry points (rewrite-guide rule 4).
void __fastcall DestroyHook(CResourceManager* self) { self->CResourceManager::Destroy(); }
BOOL __fastcall AddArchiveHook(CResourceManager* self, void*, const char* path) {
    return self->CResourceManager::AddArchive(path);
}
BOOL __fastcall RemoveArchiveHook(CResourceManager* self, void*, const char* path) {
    return self->CResourceManager::RemoveArchive(path);
}
BOOL __fastcall RemoveAllArchivesHook(CResourceManager* self) { return self->CResourceManager::RemoveAllArchives(); }
void* __fastcall GetFileHook(CResourceManager* self, void*, const char* name, std::uint32_t* size) {
    return self->CResourceManager::GetFile(name, size);
}
bool __fastcall CloseFileHook(CResourceManager* self, void*, void* data) { return self->CResourceManager::CloseFile(data); }
BOOL __fastcall HasFileHook(CResourceManager* self, void*, const char* name) { return self->CResourceManager::HasFile(name); }
void __fastcall FreeAllFilesHook(CResourceManager* self) { self->CResourceManager::FreeAllFiles(); }
} // namespace
EI_REPLACE(0x00674F40, DestroyHook);
EI_REPLACE(0x00675050, AddArchiveHook);
EI_REPLACE(0x00675200, RemoveArchiveHook);
EI_REPLACE(0x00675340, RemoveAllArchivesHook);
EI_REPLACE(0x00675440, GetFileHook);
EI_REPLACE(0x00675490, CloseFileHook);
EI_REPLACE(0x006754E0, HasFileHook);
EI_REPLACE(0x00675530, FreeAllFilesHook);
