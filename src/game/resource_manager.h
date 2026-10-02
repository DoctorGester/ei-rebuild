#pragma once

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

// Base of the managers that load their data from archives (CTextureManager, CFigureManager, ...).
// Name provisional. The methods below are vtable slots 16-22 of those managers; slots 0-15 belong to
// the engine's CBaseManager, whose layout is not recovered yet.
class CResourceManager {
public:
    BOOL AddArchive(const char* path);                            // 16: 00675050
    BOOL RemoveArchive(const char* path);                         // 17: 00675200
    BOOL RemoveAllArchives();                                     // 18: 00675340
    void* GetFile(const char* name, std::uint32_t* size);         // 19: 00675440
    BOOL CloseFile(void* data);                                   // 20: 00675490
    BOOL HasFile(const char* name);                               // 21: 006754E0
    void FreeAllFiles();                                          // 22: 00675530

    void* vtable_;
    std::uint8_t baseManager_[0xAC];      // CBaseManager members (+0x04..+0xAF)
    stlp::vector<ArchiveEntry> archives_; // +0xB0
};
static_assert(offsetof(CResourceManager, archives_) == 0xB0);
