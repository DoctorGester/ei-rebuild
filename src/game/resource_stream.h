#pragma once

// The rest of the resource code (00672190-00675050): the archive readers' common base
// CResourceBase, the sound system's file reader (vtable 00743320), the name listing of an archive
// and the constructors and destructors of CMMIOResource. Names provisional; the class name
// CResourceBase is from the game's own error messages.

#include "resource.h"

#include <cstdint>

// The base of the archive readers (vtable 00743324; slot 0 is the scalar deleting destructor). The
// archive header and tables, and an index table (accelerator) from numbers to entries built by
// CreateAccelTable (00674910, only called by the unused 00674310).
struct CResourceBase {
    void* vtable_;
    ResHeader header_;          // +0x04
    ResEntry* entries_;         // +0x14
    char* names_;               // +0x18
    int* accelTable_;           // +0x1C
    std::uint32_t accelCount_;  // +0x20

    CResourceBase* Construct();                     // 00674790: fields cleared; returns this
    void Destruct();                                // 00674810: fields cleared, accelerator freed
    CResourceBase* DeletingDestruct(unsigned flags);    // 006747C0 (slot 0)
    void FreeAccelTable();                          // 00674A90
};
static_assert(sizeof(CResourceBase) == 0x24);

// Opens files for the sound system (created by 006B6040): a loose file through mmioOpen, or an
// entry of an archive opened with mmio (the archive part is never opened: its Open, 00674310, has no
// callers). Vtable 00743320 (slot 0 deleting destructor, slot 1 the base's). 0x34 bytes.
struct CResourceStream : CResourceBase {
    void* archive_;             // +0x24 HMMIO of the archive
    std::int32_t offset_;       // +0x28 start of the open entry in the archive; 1 for a loose file; 0: none
    std::int32_t size_;         // +0x2C size of the open entry (only the unused 006745C0 sets it)
    void* file_;                // +0x30 HMMIO of a loose file

    CResourceStream* Construct();                   // 00674280: returns this
    void Destruct();                                // 006742C0
    CResourceStream* DeletingDestruct(unsigned flags);  // 006742A0 (slot 0)
    void Close();                                   // 00674430: closes the file and the archive
    // 006744C0. Opens `name`: a loose file when one is readable, else the archive entry. Stores
    // the size when `size` is not null. Returns 1 on success.
    BOOL OpenFile(const char* name, std::uint32_t* size);
    long Read(char* buffer, long size);             // 006746B0: mmioRead; -1 when nothing is open
    long Seek(long offset, int origin);             // 006746F0: mmioSeek relative to the entry
};
static_assert(sizeof(CResourceStream) == 0x34);

// 00674CE0. Matches `name` against `pattern` (* ends the match successfully, ? matches any
// character, other characters compare through toupper). An empty pattern matches an empty name,
// and a non-empty name gives `emptyPatternResult`. __fastcall (name in ECX, pattern in EDX).
int __fastcall WildcardMatch(const char* name, const char* pattern, int emptyPatternResult);
