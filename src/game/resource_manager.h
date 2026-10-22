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
// CBaseManager's archive slots (16-22) over a list of open archives, and a 26th slot, the folder
// name. Name provisional; game.exe's vtable 00743338 is retired. resource_manager.cpp.
class CResourceManager : public CBaseManager {
public:
    // Slot 0 is the scalar deleting destructor 00674DD0.
    // 3: 00674F40. Closes and deletes every archive, then CBaseManager::Destroy.
    void Destroy() override;
    BOOL AddArchive(const char* path) override;                     // 16: 00675050
    BOOL RemoveArchive(const char* path) override;                  // 17: 00675200
    BOOL RemoveAllArchives() override;                              // 18: 00675340
    void* GetFile(const char* name, std::uint32_t* size) override;  // 19: 00675440
    bool CloseFile(void* data) override;                            // 20: 00675490
    BOOL HasFile(const char* name) override;                        // 21: 006754E0
    void FreeAllFiles() override;                                   // 22: 00675530

    // 25: the manager's folder name ("textures" 00691200, "spells" 00688940, ...; the item,
    // figure and material managers' too).
    virtual const char* GetFolderName() = 0;

    // 005AA360: CFigureManager's slot 16, AddArchive with memory-mapped archives.
    BOOL AddMmioArchive(const char* path);

    // 00674DF0: every archive closed and deleted, CBaseManager::Destroy, the list freed, then CBaseManager's.
    ~CResourceManager() override;

protected:
    // 00674D90: no archives.
    CResourceManager();

private:
    BOOL AddArchiveOf(const char* path, std::size_t size, CMappedResource*(__fastcall* construct)(void*));

public:
    stlp::vector<ArchiveEntry> archives_;   // +0xB0
};
static_assert(offsetof(CResourceManager, archives_) == 0xB0);
static_assert(sizeof(CResourceManager) == 0xBC);
