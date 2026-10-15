#pragma once

// Resource archives (*.res): the game's packed file format, used for textures, figures, maps,
// scripts and everything else under Res\. Class names come from the game's own error messages.
//
// File layout:
//   ResHeader                  at offset 0
//   ResEntry[header.count]     at header.tableOffset
//   char names[header.namesSize] immediately after the table (not NUL-terminated)
//
// Entries form a hash table: an entry's home slot is
//   (sum of tolower(name[i])) % count,
// and colliding names are chained through ResEntry::next (-1 ends the chain).

#include <cstdint>

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
    CMappedFile* Construct();                         // 006756B0 (the constructor)
    void Close();                                     // 006756D0 (00675720 is an identical copy)
    BOOL Open(const char* path, BOOL writable);       // 00675770
    void Flush();                                     // 00675A50: write back, stamp modification time

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

// Reads an archive with low-level file I/O, loading individual files into memory on request.
// vtable 007432E0.
class CMappedResource {
public:
    // Virtual methods, in vtable order.
    virtual ~CMappedResource();                                       // 0: 006713A0 / 006714A0
    virtual BOOL OpenResource(const char* path, BOOL writable);       // 1: 00671730
    BOOL OpenResourceImpl(const char* path, BOOL writable);   // body of the slot above (registered by member pointer)
    virtual BOOL OpenFromMemory(const void* image);                   // 2: 00671A40
    BOOL OpenFromMemoryImpl(const void* image);   // body of the slot above (registered by member pointer)
    virtual void CloseResource();                                     // 3: 00671AC0
    virtual void Flush();                                             // 4: 00671390 (empty)
    virtual void* GetFile(const char* name, std::uint32_t* size);     // 5: 00671B40
    void* GetFileImpl(const char* name, std::uint32_t* size);   // body of the slot above (registered by member pointer)
    virtual bool CloseFile(void* data);                               // 6: 00671E00
    bool CloseFileImpl(void* data);   // body of the slot above (registered by member pointer)
    virtual void FreeAllFiles();                                      // 7: 00672040

    // 00671280. The constructor: empty, closed archive.
    CMappedResource* Construct();
    // 006714A0 (the destructor body) and 006713A0 (scalar deleting destructor).
    void Destroy();
    CMappedResource* DeletingDestroy(unsigned flags);

    // 00674850. Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(const char* name) const;
    // 00674AC0 (resource_stream.cpp). Fills an MFC CStringArray with the names of the entries that
    // match `pattern` (drive, directory, name and extension matched separately with * and ?), or
    // with every name when `pattern` is null.
    void ListFiles(void* stringArray, const char* pattern) const;

    ResHeader header_;           // +0x04
    ResEntry* entries_;          // +0x14
    char* names_;                // +0x18
    // +0x1C, +0x20: accelerator table of the CResourceBase base class (vtable 00743324); freed by
    // its destructor.
    void* accelTable_;           // +0x1C
    std::uint32_t accelCount_;   // +0x20
    int fd_;                     // +0x24 CRT file descriptor, -1 when closed
    const void* image_;          // +0x28 archive image when opened with OpenFromMemory
    std::uint32_t writable_;     // +0x2C
    stlp::hash_map<void*, LoadedFile*, stlp::PointerHash> filesByData_;         // +0x30
    stlp::hash_map<stlp::string, LoadedFile*, stlp::StringHash> filesByName_;  // +0x44
};
static_assert(sizeof(CMappedResource) == 0x58);

// Maps the whole archive into memory; GetFile returns pointers into the mapping. vtable 00743300.
class CMMIOResource : public CMappedResource {
public:
    // 006721D0 / 006722F0 / 006722D0 (resource_stream.cpp): constructor, destructor body and
    // scalar deleting destructor.
    CMMIOResource* Construct();
    void Destroy();
    CMMIOResource* DeletingDestroy(unsigned flags);

    // 0: 006722D0 / 006722F0 destructor (not rewritten yet)
    BOOL OpenResource(const char* path, BOOL writable) override;      // 1: 006724E0
    BOOL OpenResourceImpl(const char* path, BOOL writable);   // body of the slot above (registered by member pointer)
    BOOL OpenFromMemory(const void* image) override;                  // 2: 00672520
    BOOL OpenFromMemoryImpl(const void* image);   // body of the slot above (registered by member pointer)
    void CloseResource() override;                                    // 3: 006725A0
    void Flush() override;                                            // 4: 006725D0 (flushes file_)
    void* GetFile(const char* name, std::uint32_t* size) override;    // 5: 006725E0
    void* GetFileImpl(const char* name, std::uint32_t* size);   // body of the slot above (registered by member pointer)
    bool CloseFile(void* data) override;                              // 6: 00672640 (nothing to free)
    bool CloseFileImpl(void* data);   // body of the slot above (registered by member pointer)
    void FreeAllFiles() override;                                     // 7: 00672650 (empty)

    CMappedFile file_;           // +0x58
    const void* mappedImage_;    // +0x68 base of the mapped archive
};
static_assert(sizeof(CMMIOResource) == 0x6C);
