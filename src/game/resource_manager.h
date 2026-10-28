#pragma once

#include "base_manager.h"
#include "resource.h"

#include <cstddef>
#include <vector>

// An archive opened by a resource manager, kept in open order.
struct ArchiveEntry {
    std::string name;                // path passed to AddArchive
    CMappedResource* resource;

    ArchiveEntry(const std::string& n, CMappedResource* r) : name(n), resource(r) {}
    ArchiveEntry(const ArchiveEntry& e) : name(e.name), resource(e.resource) {}
    void AssignFrom(const ArchiveEntry& e) { name = e.name; resource = e.resource; }
};

// Base of the managers that load their data from archives (CTextureManager, CFigureManager, ...):
// CBaseManager's archive slots (16-22) over a list of open archives, and a 26th slot, the folder
// name. Name provisional. resource_manager.cpp.
class CResourceManager : public CBaseManager {
public:
    // Slot 0 is the scalar deleting destructor.
    // Closes and deletes every archive, then CBaseManager::Destroy.
    void Destroy() override;
    BOOL AddArchive(const char* path) override;
    BOOL RemoveArchive(const char* path) override;
    BOOL RemoveAllArchives() override;
    void* GetFile(const char* name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;
    BOOL HasFile(const char* name) override;
    void FreeAllFiles() override;

    // The manager's folder name ("textures", "spells", ...; the item,
    // figure and material managers' too).
    virtual const char* GetFolderName() = 0;

    // CFigureManager's slot 16, AddArchive with memory-mapped archives.
    BOOL AddMmioArchive(const char* path);

    ~CResourceManager() override;

protected:
    CResourceManager();

private:
    BOOL AddArchiveOf(const char* path, std::size_t size, CMappedResource*(__fastcall* construct)(void*));

public:
    std::vector<ArchiveEntry> archives_;
};
static_assert(offsetof(CResourceManager, archives_) == 0xB0);
static_assert(sizeof(CResourceManager) == 0xBC);
