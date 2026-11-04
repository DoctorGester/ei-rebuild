#pragma once

// Resource archives (*.res): the game's packed file format, used for textures, figures, maps,
// scripts and everything else under Res\. Class names come from the game's own error messages.
// File layout:
//   ResHeader                  at offset 0
//   ResEntry[header.count]     at header.tableOffset
//   char names[header.namesSize] immediately after the table (not NUL-terminated)
// Entries form a hash table: an entry's home slot is
//   (sum of tolower(name[i])) % count,
// and colliding names are chained through ResEntry::next (-1 ends the chain).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/platform.h"


#pragma pack(push, 1)
struct ResHeader {
    std::uint32_t magic = 0;    // kResMagic
    std::uint32_t count = 0;    // number of entries (also the hash table size)
    std::uint32_t tableOffset = 0;  // file offset of the entry table
    std::uint32_t namesSize = 0;  // bytes of name data after the table
};
static_assert(sizeof(ResHeader) == 16);

struct ResEntry {
    static constexpr std::int32_t kChainEnd = -1;
    std::int32_t next = 0;      // next entry in the hash chain, or kChainEnd
    std::uint32_t size = 0;     // data size in bytes
    std::uint32_t offset = 0;   // data offset from the start of the archive
    std::uint32_t time = 0;     // modification time (time_t)
    std::uint16_t nameLength = 0;
    std::uint32_t nameOffset = 0;  // offset into the names block
};
static_assert(sizeof(ResEntry) == 22);
#pragma pack(pop)

constexpr std::uint32_t kResMagic = 0x019CE23C;

// A file loaded from an archive by MappedResource::GetFile.
struct LoadedFile {
    std::string name;                   // name as requested
    std::vector<std::uint8_t> bytes;    // contents (at least one byte, so that an empty file still has an address)
    std::uint32_t size = 0;
    int refCount = 0;                   // GetFile calls not yet matched by CloseFile
};

// The base of the archive readers (ResourceBaseClass in the game's messages): the archive header and tables. (The game's index table from numbers to
// entries, CreateAccelTable's, is never built: it has no caller.)
class ResourceBase {
public:
    virtual ~ResourceBase() = default;

    // Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(std::string_view name) const;

    ResHeader header{};
    ResEntry* entries = nullptr;
    char* names = nullptr;
};

// Reads an archive with file I/O, loading individual files into memory on request.
class MappedResource : public ResourceBase {
public:
    // Rebirth's ResOverride forgets the archive, then CloseResource.
    ~MappedResource() override;
    virtual bool OpenResource(const std::string& path);
    virtual bool OpenFromMemory(const void* image);
    virtual void CloseResource();
    virtual void* GetFile(std::string_view name, std::uint32_t* size);
    // GetFile without Rebirth's ResOverride (in the standalone image GetFile goes through
    // mod_folders.h, which calls this one on the game's and the mod's archives).
    void* GetFilePlain(std::string_view name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual void FreeAllFiles();

    // Fills `names` with the names of the entries that
    // match `pattern` (drive, directory, name and extension matched separately with * and ?), or
    // with every name when `pattern` is null.
    void ListFiles(std::vector<std::string>* names, const char* pattern) const;
    // The names ListFiles fills in, without Rebirth's ResOverride (see GetFilePlain).
    std::vector<std::string> ListFilesPlain(const char* pattern) const;
    // The header checked and the entry table (packed 22-byte records) copied from the archive image at `base`,
    // the names pointed at in the image; false when it is not an archive. Both readers' OpenFromMemory.
    bool ReadImage(char* base);

    platform::File archive;           // the archive's file
    char* image = nullptr;              // archive image when opened with OpenFromMemory
    std::vector<ResEntry> entryTable;   // the entry table (entries points into it)
    std::vector<char> nameTable;        // the names OpenResource reads (names points into them)
    std::unordered_map<void*, std::unique_ptr<LoadedFile>> filesByData;   // the loaded files, by their contents
    std::map<std::string, LoadedFile*, std::less<>> filesByName;
};

// Maps the whole archive into memory; GetFile returns pointers into the mapping.
class MMIOResource : public MappedResource {
public:
    bool OpenResource(const std::string& path) override;
    bool OpenFromMemory(const void* image) override;
    void CloseResource() override;
    void* GetFile(std::string_view name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;                              // (nothing to free)
    void FreeAllFiles() override;                                     // (empty)

    platform::MappedFile file;
    char* mappedImage = nullptr;         // base of the mapped archive
};
