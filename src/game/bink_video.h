#pragma once

// Bink movies (bink_video.cpp): the Bink handle's first fields and the game's video object.

#include <cstddef>
#include <cstdint>

#include <windows.h>

struct CDXSurface;

// The Bink handle's first fields (Bink 1.x: Width, Height, Frames, FrameNum, LastFrameNum).
struct BinkHeader {
    std::uint32_t width;        // +0x00
    std::uint32_t height;       // +0x04
    std::uint32_t frames;       // +0x08 the frame count
    std::uint32_t frameNum;     // +0x0C the frame to decode next (1-based)
    std::uint32_t lastFrame;    // +0x10 the frame decoded last
};
static_assert(offsetof(BinkHeader, lastFrame) == 0x10);

// CBinkVideo (opened by 005DBA50, stepped by 005DBD80): the decode surface, its Bink surface type,
// the letterboxed screen rectangle and the Bink handle.
struct CBinkVideo {
    CDXSurface* holder;         // +0x00
    std::uint32_t surfaceType;  // +0x04 BinkDDSurfaceType of the holder's surface
    RECT to;                    // +0x08
    BinkHeader* bink;           // +0x18
};
static_assert(offsetof(CBinkVideo, to) == 0x08);
static_assert(offsetof(CBinkVideo, bink) == 0x18);
