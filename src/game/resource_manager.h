#pragma once

#include "base_manager.h"
#include "resource.h"

#include <cstddef>

// An archive opened by a resource manager, kept in open order.
struct ArchiveEntry {
    stlp::string name;               // path passed to AddArchive
    CMappedResource* resource;

    ArchiveEntry(const stlp::string& n, CMappedResource* r) : name(n), resource(r) {}
    ArchiveEntry(const ArchiveEntry& e) : name(e.name), resource(e.resource) {}   // 005AE030
    void AssignFrom(const ArchiveEntry& e) { name.AssignFrom(e.name); resource = e.resource; }
};
static_assert(sizeof(ArchiveEntry) == 16);

// Base of the managers that load their data from archives (CTextureManager, CFigureManager, ...):
// CBaseManager's archive slots (16-22) over a list of open archives. Name provisional; vtable
// 00743338 (slots 11, 12 and 14 are _purecall there, and so is a 26th entry that CTextureManager
// fills with 00691200: probably a slot this class adds, not declared yet). resource_manager.cpp.
class CResourceManager : public CBaseManager {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x00743338;

    // Slot 0 is the scalar deleting destructor 00674DD0 (DeletingDestruct).
    // 3: 00674F40. Closes and deletes every archive, then CBaseManager::Destroy.
    void Destroy() override;
    BOOL AddArchive(const char* path) override;                     // 16: 00675050
    BOOL RemoveArchive(const char* path) override;                  // 17: 00675200
    BOOL RemoveAllArchives() override;                              // 18: 00675340
    void* GetFile(const char* name, std::uint32_t* size) override;  // 19: 00675440
    bool CloseFile(void* data) override;                            // 20: 00675490
    BOOL HasFile(const char* name) override;                        // 21: 006754E0
    void FreeAllFiles() override;                                   // 22: 00675530

    // 005AA360: CFigureManager's slot 16, AddArchive with memory-mapped archives.
    BOOL AddMmioArchive(const char* path);

    // 00674D90 constructor body (returns this), 00674DF0 destructor body, 00674DD0 scalar deleting
    // destructor.
    CResourceManager* Construct();
    void Destruct();
    CResourceManager* DeletingDestruct(unsigned flags);

private:
    BOOL AddArchiveOf(const char* path, std::size_t size, CMappedResource*(__fastcall* construct)(void*));

public:
    stlp::vector<ArchiveEntry> archives_;   // +0xB0
};
static_assert(offsetof(CResourceManager, archives_) == 0xB0);
static_assert(sizeof(CResourceManager) == 0xBC);
