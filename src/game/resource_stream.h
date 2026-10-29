#pragma once

// The rest of the resource code: the sound system's file reader,
// the name listing of an archive and the constructors and destructors of
// MMIOResource (the readers' base ResourceBase is in resource.h). Names provisional.

#include "resource.h"

#include "lib/gameapi.h"

#include <cstdint>
#include <string>

// Opens loose files for the sound system (a member of SoundManager) through mmio. (The original was a
// ResourceBase that could also read an entry of an archive; it never opened one.)
class ResourceStream {
public:
    ResourceStream() = default;
    ResourceStream(const ResourceStream&) = delete;
    ResourceStream& operator=(const ResourceStream&) = delete;
    ~ResourceStream() { Close(); }

    void Close();                                   // closes the file
    // Closes the file, then opens `name` when it exists. Stores its size when `size` is not null. Returns
    // true on success.
    bool OpenFile(const std::string& name, std::uint32_t* size);
    long Read(char* buffer, long size);             // mmioRead; -1 when nothing is open
    // mmioSeek: `origin` 2 from the end, anything else from the start. -1 when nothing is open.
    long Seek(long offset, int origin);

private:
    HMMIO file = nullptr;
};

// Matches `name` against `pattern` (* ends the match successfully, ? matches any
// character, other characters compare through toupper). An empty pattern matches an empty name,
// and a non-empty name gives `emptyPatternResult`.
int WildcardMatch(const char* name, const char* pattern, int emptyPatternResult);
