#pragma once

#include "base_manager.h"
#include "resource.h"

#include <cstddef>
#include <vector>

// An archive opened by a resource manager, kept in open order.
struct ArchiveEntry {
    std::string name;                // path passed to AddArchive
    MappedResource* resource;

    ArchiveEntry(const std::string& n, MappedResource* r) : name(n), resource(r) {}
    ArchiveEntry(const ArchiveEntry& e) : name(e.name), resource(e.resource) {}
};

// Base of the managers that load their data from archives (TextureManager, FigureManager, ...):
// BaseManager's archive slots (16-22) over a list of open archives, and a 26th slot, the folder
// name. Name provisional. resource_manager.cpp.
class ResourceManager : public BaseManager {
public:
    // The scalar deleting destructor.
    // Closes and deletes every archive, then BaseManager::Destroy.
    void Destroy() override;
    bool AddArchive(const std::string& path) override;
    bool RemoveArchive(const std::string& path) override;
    bool RemoveAllArchives() override;
    void* GetFile(std::string_view name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;
    bool HasFile(std::string_view name) override;
    void FreeAllFiles() override;

    // The manager's folder name ("textures", "spells", ...; the item,
    // figure and material managers' too).
    virtual const char* GetFolderName() = 0;

    // FigureManager's AddArchive virtual with memory-mapped archives.
    bool AddMmioArchive(const std::string& path);

    ~ResourceManager() override;

protected:
    ResourceManager();

private:
    bool AddArchiveOf(const std::string& path, MappedResource* (*create)());

public:
    std::vector<ArchiveEntry> archives;
};
