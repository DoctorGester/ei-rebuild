#include "resource_manager.h"

#include "ei/hook.h"
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
    for (ArchiveEntry& e : archives_) {
        e.resource->CloseResource();
        delete e.resource;
    }
    archives_.clear();
    return 1;
}

void* CResourceManager::GetFile(const char* name, std::uint32_t* size) {
    for (ArchiveEntry& e : archives_)
        if (void* data = e.resource->GetFile(name, size)) return data;
    return nullptr;
}

BOOL CResourceManager::CloseFile(void* data) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->CloseFile(data)) return 1;
    return 0;
}

BOOL CResourceManager::HasFile(const char* name) {
    for (ArchiveEntry& e : archives_)
        if (e.resource->FindEntry(name) != -1) return 1;
    return 0;
}

void CResourceManager::FreeAllFiles() {
    for (ArchiveEntry& e : archives_) e.resource->FreeAllFiles();
}

EI_REPLACE_METHOD(0x00675050, CResourceManager::AddArchive);
EI_REPLACE_METHOD(0x005AA360, CResourceManager::AddMmioArchive);
EI_REPLACE_METHOD(0x00675200, CResourceManager::RemoveArchive);
EI_REPLACE_METHOD(0x00675340, CResourceManager::RemoveAllArchives);
EI_REPLACE_METHOD(0x00675440, CResourceManager::GetFile);
EI_REPLACE_METHOD(0x00675490, CResourceManager::CloseFile);
EI_REPLACE_METHOD(0x006754E0, CResourceManager::HasFile);
EI_REPLACE_METHOD(0x00675530, CResourceManager::FreeAllFiles);
