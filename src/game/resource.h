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

#include "lib/stlp.h"

namespace mfc {
struct CStringArray;
}

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
    CMappedFile* Construct();                         // 006756B0 (the constructor)
    void Close();                                     // 006756D0 (00675720 is an identical copy)
    BOOL Open(const char* path, BOOL writable);
    void Flush();                                     // write back, stamp modification time

    void* file_;         // +0x00 HANDLE, INVALID_HANDLE_VALUE when closed
    void* mapping_;      // +0x04 HANDLE
    void* view_;         // +0x08 mapped base address
    std::uint32_t size_; // +0x0C
};
static_assert(sizeof(CMappedFile) == 16);

// A file loaded from an archive by CMappedResource::GetFile. 0x1C bytes, allocated with operator new.
struct LoadedFile {
    stlp::string name;      // +0x00 name as requested
    void* data;             // +0x0C contents (operator new)
    std::uint32_t size;     // +0x10
    int refCount;           // +0x14 GetFile calls not yet matched by CloseFile
    std::uint32_t offset;   // +0x18 offset in the archive, used to write the file back
};
static_assert(sizeof(LoadedFile) == 0x1C);

// The base of the archive readers (ResourceBaseClass in the game's messages; slot 0 is the scalar
// deleting destructor): the archive header and tables, and an index table (accelerator) from
// numbers to entries built by CreateAccelTable (00674910, only called by the unused 00674310).
// A real virtual class: the compiler makes its vtable (game.exe's 00743324 is retired).
class CResourceBase {
public:
    CResourceBase();                                // fields cleared
    // 00674810 (006747C0 the scalar deleting destructor): fields cleared, accelerator freed.
    virtual ~CResourceBase();
    // The deleting destructors free into the game's heap (resource_stream.cpp).
    static void operator delete(void* block);

    void FreeAccelTable();

    // 00674850 (resource.cpp). Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(const char* name) const;

    ResHeader header_;          // +0x04
    ResEntry* entries_;         // +0x14
    char* names_;               // +0x18
    int* accelTable_;           // +0x1C
    std::uint32_t accelCount_;  // +0x20
};
static_assert(sizeof(CResourceBase) == 0x24);

// Reads an archive with low-level file I/O, loading individual files into memory on request.
// A real virtual class: the compiler makes its vtable (game.exe's 007432E0 is retired).
class CMappedResource : public CResourceBase {
public:
    // An empty, closed archive: CResourceBase's fields cleared, the loaded-file maps with
    // 193 buckets each, no image, no file.
    CMappedResource();
    // Virtual methods, in vtable order.
    // 0: 006714A0 (006713A0 the scalar deleting destructor). The loaded files freed, the archive
    // closed, the maps destroyed, then CResourceBase's (the accelerator table freed).
    ~CMappedResource() override;
    virtual BOOL OpenResource(const char* path, BOOL writable);
    virtual BOOL OpenFromMemory(const void* image);
    virtual void CloseResource();
    virtual void Flush();                                             // 4: 00671390 (empty)
    virtual void* GetFile(const char* name, std::uint32_t* size);
    // GetFile without Rebirth's ResOverride (in the standalone image GetFile goes through
    // mod_folders.h, which calls this one on the game's and the mod's archives).
    void* GetFilePlain(const char* name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual void FreeAllFiles();

    // 006714A0, the destructor's body, called qualified where the original reaches it by address (Rebirth patches
    // it): the in-place archives of the loaders and screens.
    void Destroy();

    // 00674AC0 (resource_stream.cpp). Fills an MFC CStringArray with the names of the entries that
    // match `pattern` (drive, directory, name and extension matched separately with * and ?), or
    // with every name when `pattern` is null.
    void ListFiles(mfc::CStringArray* stringArray, const char* pattern) const;
    // ListFiles without Rebirth's ResOverride (see GetFilePlain).
    void ListFilesPlain(mfc::CStringArray* stringArray, const char* pattern) const;

    int fd_;                    // +0x24 CRT file descriptor, -1 when closed
    char* image_;                // +0x28 archive image when opened with OpenFromMemory
    std::uint32_t writable_;     // +0x2C
    stlp::hash_map<void*, LoadedFile*, stlp::PointerHash> filesByData_;         // +0x30
    stlp::hash_map<stlp::string, LoadedFile*, stlp::StringHash> filesByName_;  // +0x44
};
static_assert(sizeof(CMappedResource) == 0x58);

// Maps the whole archive into memory; GetFile returns pointers into the mapping. A real virtual
// class: the compiler makes its vtable (game.exe's 00743300 is retired).
class CMMIOResource : public CMappedResource {
public:
    // 006721D0 (resource_stream.cpp). CMappedResource's, a closed mapped file, no image.
    CMMIOResource();
    // The mapping closed (CloseResource inlined, then the CMappedFile destructor's),
    // then CMappedResource's.
    ~CMMIOResource() override;

    BOOL OpenResource(const char* path, BOOL writable) override;
    BOOL OpenFromMemory(const void* image) override;
    void CloseResource() override;
    void Flush() override;                                            // 4: 006725D0 (flushes file_)
    void* GetFile(const char* name, std::uint32_t* size) override;
    bool CloseFile(void* data) override;                              // 6: 00672640 (nothing to free)
    void FreeAllFiles() override;                                     // 7: 00672650 (empty)

    CMappedFile file_;           // +0x58
    char* mappedImage_;          // +0x68 base of the mapped archive
};
static_assert(sizeof(CMMIOResource) == 0x6C);
