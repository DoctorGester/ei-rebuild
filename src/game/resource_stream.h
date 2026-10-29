#pragma once

// The rest of the resource code: the sound system's file reader,
// the name listing of an archive and the constructors and destructors of
// CMMIOResource (the readers' base CResourceBase is in resource.h). Names provisional.

#include "resource.h"

#include <cstdint>
#include <string>

// Opens files for the sound system (a member of CSoundManager): a loose file through mmioOpen, or an
// entry of an archive opened with mmio (the archive part is never opened: its Open, has no
// callers). ResourceStreamClass (virtual deleting destructor). 0x34 bytes. A real virtual class:
// the compiler makes its vtable.
struct CResourceStream : CResourceBase {
    CResourceStream();                              // no file, no archive
    ~CResourceStream() override;                    // Close, then CResourceBase's

    void* archive_;             // HMMIO of the archive
    std::int32_t offset_;       // start of the open entry in the archive; 1 for a loose file; 0: none
    std::int32_t size_;         // size of the open entry (only the unused sets it)
    void* file_;                // HMMIO of a loose file

    void Close();                                   // closes the file and the archive
    // Opens `name`: a loose file when one is readable, else the archive entry. Stores
    // the size when `size` is not null. Returns 1 on success.
    bool OpenFile(const std::string& name, std::uint32_t* size);
    long Read(char* buffer, long size);             // mmioRead; -1 when nothing is open
    long Seek(long offset, int origin);             // mmioSeek relative to the entry
};

// Matches `name` against `pattern` (* ends the match successfully, ? matches any
// character, other characters compare through toupper). An empty pattern matches an empty name,
// and a non-empty name gives `emptyPatternResult`.
int WildcardMatch(const char* name, const char* pattern, int emptyPatternResult);
