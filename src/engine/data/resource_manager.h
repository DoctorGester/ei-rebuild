#pragma once

#include "engine/core/base_manager.h"
#include "engine/data/resource.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// An archive opened by a resource manager, kept in open order.
struct ArchiveEntry {
    std::string name;                // path passed to AddArchive
    MappedResource* resource = nullptr;
};

// Base of the managers that load their data from archives (TextureManager, FigureManager, ...):
// BaseManager's archive slots (16-22) over a list of open archives, and a 26th slot, the folder
// name. Name provisional. resource_manager.cpp.
class ResourceManager : public BaseManager {
public:
    // Deletes (closing) every archive, then BaseManager::Destroy.
    void Destroy() override;
    bool AddArchive(const std::string& path) override;
    void* GetFile(std::string_view name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;
    bool HasFile(std::string_view name) override;
    // MappedResource::MakePngs of `names`, each in the first archive that has it (as GetFile takes it).
    void MakePngs(std::span<const std::string> names);
    // The record file `name` (docs/notes/mods.md, "Records as TOML"): each archive's in order, its TOML files kept,
    // down to the first that has the original.
    RecordFile FindRecord(std::string_view name);
    void FreeAllFiles() override;

    // FigureManager's AddArchive virtual with memory-mapped archives.
    bool AddMmioArchive(const std::string& path);

    ~ResourceManager() override;

protected:
    ResourceManager();

private:
    // Opens `path` in a new archive, a MMIOResource when `mapped`; true when the name is listed already.
    bool AddArchiveOf(const std::string& path, bool mapped);

public:
    std::vector<ArchiveEntry> archives;
};
