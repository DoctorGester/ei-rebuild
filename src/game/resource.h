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
    CMappedFile();                                    // 006756B0
    void Close();                                     // 006756D0 (00675720 is an identical copy)
    bool Open(const char* path, bool writable);       // 00675770

    void* file_;         // +0x00 HANDLE, INVALID_HANDLE_VALUE when closed
    void* mapping_;      // +0x04 HANDLE
    void* view_;         // +0x08 mapped base address
    std::uint32_t size_; // +0x0C
};
static_assert(sizeof(CMappedFile) == 16);

// Reads an archive with low-level file I/O, loading individual files into memory on request.
// vtable 007432E0.
class CMappedResource {
public:
    // Virtual methods, in vtable order.
    virtual ~CMappedResource();                                       // 0: 006713A0 / 006714A0
    virtual bool OpenResource(const char* path, bool writable);       // 1: 00671730
    virtual bool OpenFromMemory(const void* image);                   // 2: 00671A40
    virtual void CloseResource();                                     // 3: 00671AC0
    virtual void Slot4();                                             // 4: 00671390 (empty)
    virtual void* GetFile(const char* name, std::uint32_t* size);     // 5: 00671B40
    virtual bool CloseFile(void* data);                               // 6: 00671E00
    virtual void FreeAllFiles();                                      // 7: 00672040

    // 00674850. Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(const char* name) const;

    ResHeader header_;           // +0x04
    ResEntry* entries_;          // +0x14
    char* names_;                // +0x18
    std::uint32_t unknown_1C;    // +0x1C
    std::uint32_t unknown_20;    // +0x20
    int fd_;                     // +0x24 CRT file descriptor, -1 when closed
    const void* image_;          // +0x28 archive image when opened with OpenFromMemory
    std::uint32_t writable_;     // +0x2C
    // +0x30 two STLport hash_maps tracking files loaded by GetFile (layout not recovered yet)
    std::uint8_t loadedFiles_[0x28];
};
static_assert(sizeof(CMappedResource) == 0x58);

// Maps the whole archive into memory; GetFile returns pointers into the mapping. vtable 00743300.
class CMMIOResource : public CMappedResource {
public:
    // Overrides: 0: 006722D0 / 006722F0, 1: 006724E0, 2: 00672520, 3: 006725A0,
    // 4: 006725D0 (forwards to file_), 5: 006725E0, 6: 00672640 (returns true), 7: 00672650 (empty).
    CMappedFile file_;           // +0x58
    const void* mappedImage_;    // +0x68 base of the mapped archive
};
static_assert(sizeof(CMMIOResource) == 0x6C);
