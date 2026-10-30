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
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/platform.h"


#pragma pack(push, 1)
struct ResHeader {
    std::uint32_t magic;        // kResMagic
    std::uint32_t count;        // number of entries (also the hash table size)
    std::uint32_t tableOffset;  // file offset of the entry table
    std::uint32_t namesSize;    // bytes of name data after the table
};
static_assert(sizeof(ResHeader) == 16);

struct ResEntry {
    static constexpr std::int32_t kChainEnd = -1;
    std::int32_t next;          // next entry in the hash chain, or kChainEnd
    std::uint32_t size;         // data size in bytes
    std::uint32_t offset;       // data offset from the start of the archive
    std::uint32_t time;         // modification time (time_t)
    std::uint16_t nameLength;
    std::uint32_t nameOffset;   // offset into the names block
};
static_assert(sizeof(ResEntry) == 22);
#pragma pack(pop)

constexpr std::uint32_t kResMagic = 0x019CE23C;

// A file loaded from an archive by MappedResource::GetFile. 0x1C bytes, allocated with operator new.
struct LoadedFile {
    std::string name;       // name as requested
    void* data;             // contents (operator new)
    std::uint32_t size;
    int refCount;           // GetFile calls not yet matched by CloseFile
    std::uint32_t offset;   // offset in the archive
};

// The base of the archive readers (ResourceBaseClass in the game's messages; the first virtual is the scalar
// deleting destructor): the archive header and tables. (The game's index table from numbers to
// entries, CreateAccelTable's, is never built: it has no caller.)
class ResourceBase {
public:
    ResourceBase();                                // fields cleared
    virtual ~ResourceBase();

    // Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(std::string_view name) const;

    ResHeader header;
    ResEntry* entries;
    char* names;
};

// Reads an archive with low-level file I/O, loading individual files into memory on request.
class MappedResource : public ResourceBase {
public:
    MappedResource();
    // Virtual methods, in vtable order.
    // The loaded files freed, the archive
    // closed, the maps destroyed, then ResourceBase's (the accelerator table freed).
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

    // the destructor's body, called qualified where Rebirth patches
    // it): the in-place archives of the loaders and screens.
    void Destroy();

    // Fills `names` with the names of the entries that
    // match `pattern` (drive, directory, name and extension matched separately with * and ?), or
    // with every name when `pattern` is null.
    void ListFiles(std::vector<std::string>* names, const char* pattern) const;
    // The names ListFiles fills in, without Rebirth's ResOverride (see GetFilePlain).
    std::vector<std::string> ListFilesPlain(const char* pattern) const;

    platform::File archive;           // the archive's file
    char* image;                 // archive image when opened with OpenFromMemory
    std::vector<ResEntry> entryTable;   // the tables OpenResource reads (entries and names point into them)
    std::vector<char> nameTable;
    std::unordered_map<void*, LoadedFile*> filesByData;
    std::unordered_map<std::string, LoadedFile*> filesByName;
};

// Maps the whole archive into memory; GetFile returns pointers into the mapping. A real virtual class.
class MMIOResource : public MappedResource {
public:
    MMIOResource();
    ~MMIOResource() override;

    bool OpenResource(const std::string& path) override;
    bool OpenFromMemory(const void* image) override;
    void CloseResource() override;
    void* GetFile(std::string_view name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;                              // (nothing to free)
    void FreeAllFiles() override;                                     // (empty)

    platform::MappedFile file;
    char* mappedImage;           // base of the mapped archive
};
