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

#include "lib/stlp.h"

// BOOL: these functions return 0/1 in the whole of EAX, and callers test all 32 bits.
using BOOL = int;

#pragma pack(push, 1)
struct ResHeader {
    std::uint32_t magic;        // kResMagic
    std::uint32_t count;        // number of entries (also the hash table size)
    std::uint32_t tableOffset;  // file offset of the entry table
    std::uint32_t namesSize;    // bytes of name data after the table
};
static_assert(sizeof(ResHeader) == 16);

struct ResEntry {
    std::int32_t next;          // next entry in the hash chain, -1 = end
    std::uint32_t size;         // data size in bytes
    std::uint32_t offset;       // data offset from the start of the archive
    std::uint32_t time;         // modification time (time_t)
    std::uint16_t nameLength;
    std::uint32_t nameOffset;   // offset into the names block
};
static_assert(sizeof(ResEntry) == 22);
#pragma pack(pop)

constexpr std::uint32_t kResMagic = 0x019CE23C;

// A file mapped into memory with CreateFileMapping/MapViewOfFile. 16 bytes.
class CMappedFile {
public:
    CMappedFile();
    CMappedFile* Construct();                         // the constructor body
    void Close();                                     // an identical copy of Close's body
    BOOL Open(const char* path, BOOL writable);
    void Flush();                                     // write back, stamp modification time

    void* file_;         // HANDLE, INVALID_HANDLE_VALUE when closed
    void* mapping_;      // HANDLE
    void* view_;         // mapped base address
    std::uint32_t size_;
};
static_assert(sizeof(CMappedFile) == 16);

// A file loaded from an archive by CMappedResource::GetFile. 0x1C bytes, allocated with operator new.
struct LoadedFile {
    std::string name;       // name as requested
    void* data;             // contents (operator new)
    std::uint32_t size;
    int refCount;           // GetFile calls not yet matched by CloseFile
    std::uint32_t offset;   // offset in the archive, used to write the file back
};

// The base of the archive readers (ResourceBaseClass in the game's messages; slot 0 is the scalar
// deleting destructor): the archive header and tables, and an index table (accelerator) from
// numbers to entries built by CreateAccelTable (only called by the unused).
class CResourceBase {
public:
    CResourceBase();                                // fields cleared
    virtual ~CResourceBase();
    // The deleting destructors free into the game's heap (resource_stream.cpp).
    static void operator delete(void* block);

    void FreeAccelTable();

    // Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(const char* name) const;

    ResHeader header_;
    ResEntry* entries_;
    char* names_;
    int* accelTable_;
    std::uint32_t accelCount_;
};
static_assert(sizeof(CResourceBase) == 0x24);

// Reads an archive with low-level file I/O, loading individual files into memory on request.
class CMappedResource : public CResourceBase {
public:
    CMappedResource();
    // Virtual methods, in vtable order.
    // The loaded files freed, the archive
    // closed, the maps destroyed, then CResourceBase's (the accelerator table freed).
    ~CMappedResource() override;
    virtual BOOL OpenResource(const char* path, BOOL writable);
    virtual BOOL OpenFromMemory(const void* image);
    virtual void CloseResource();
    virtual void Flush();                                             // (empty)
    virtual void* GetFile(const char* name, std::uint32_t* size);
    // GetFile without Rebirth's ResOverride (in the standalone image GetFile goes through
    // mod_folders.h, which calls this one on the game's and the mod's archives).
    void* GetFilePlain(const char* name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual void FreeAllFiles();

    // the destructor's body, called qualified where Rebirth patches
    // it): the in-place archives of the loaders and screens.
    void Destroy();

    // Fills `names` with the names of the entries that
    // match `pattern` (drive, directory, name and extension matched separately with * and ?), or
    // with every name when `pattern` is null.
    void ListFiles(std::vector<std::string>* names, const char* pattern) const;
    // ListFiles without Rebirth's ResOverride (see GetFilePlain).
    void ListFilesPlain(std::vector<std::string>* names, const char* pattern) const;

    void* handle_;               // the archive's file (a HANDLE), INVALID_HANDLE_VALUE when closed
    char* image_;                // archive image when opened with OpenFromMemory
    std::uint32_t writable_;
    std::unordered_map<void*, LoadedFile*> filesByData_;
    std::unordered_map<std::string, LoadedFile*> filesByName_;
};

// Maps the whole archive into memory; GetFile returns pointers into the mapping. A real virtual class.
class CMMIOResource : public CMappedResource {
public:
    CMMIOResource();
    ~CMMIOResource() override;

    BOOL OpenResource(const char* path, BOOL writable) override;
    BOOL OpenFromMemory(const void* image) override;
    void CloseResource() override;
    void Flush() override;                                            // (flushes file_)
    void* GetFile(const char* name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;                              // (nothing to free)
    void FreeAllFiles() override;                                     // (empty)

    CMappedFile file_;
    char* mappedImage_;          // base of the mapped archive
};
