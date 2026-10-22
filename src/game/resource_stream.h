#pragma once

// The rest of the resource code (00672190-00675050): the sound system's file reader (vtable
// 00743320), the name listing of an archive and the constructors and destructors of
// CMMIOResource (the readers' base CResourceBase is in resource.h). Names provisional.

#include "resource.h"

#include <cstdint>

// Opens files for the sound system (a member of CSoundManager): a loose file through mmioOpen, or an
// entry of an archive opened with mmio (the archive part is never opened: its Open, 00674310, has no
// callers). ResourceStreamClass (slot 0 the deleting destructor). 0x34 bytes. A real virtual class:
// the compiler makes its vtable (C19; game.exe's 00743320 is retired).
struct CResourceStream : CResourceBase {
    CResourceStream();                              // 00674280: no file, no archive
    ~CResourceStream() override;                    // 006742C0 (006742A0 slot 0): Close, then CResourceBase's

    void* archive_;             // +0x24 HMMIO of the archive
    std::int32_t offset_;       // +0x28 start of the open entry in the archive; 1 for a loose file; 0: none
    std::int32_t size_;         // +0x2C size of the open entry (only the unused 006745C0 sets it)
    void* file_;                // +0x30 HMMIO of a loose file

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
