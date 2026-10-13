#pragma once

// Bink movies (bink_video.cpp): the Bink handle's first fields and the game's video object.

#include <cstddef>
#include <cstdint>

#include <windows.h>

struct CDXSurface;

// 005DA400. The loading screen's movie at progress step `progress` (bink_video.cpp).
void __fastcall ShowLoadingMovie(int progress);
// 005DA3C0 (the game start 00456440 and the zone load 00567AB0, once loading is over; A-W14). The
// loading screen's movie closed: BinkClose on its handle (open or not), the handle and the
// surface holder forgotten, the holder released, the cursor shown again.
void CloseLoadingMovie();

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
    // 005DBA50 (RET 8): closes the movie open before, opens `name` (with movies on), hides the
    // cursor, makes the decode surface's holder and the screen rectangle (centred at the movie's size
    // in movies mode 1, else stretched to the screen's width). Nonzero (the low byte) when it plays.
    char Open(const char* name, char loadingStep);
    // 005DB340 (A-W14). The constructor: no holder, no movie. Returns this.
    CBinkVideo* Construct();
    // 005DBF70 (the titles' end, 0062BD80 / 0062BFD0). Closes the movie when one is open, lets the
    // holder go, takes the cursor's background from the primary surface again and shows the cursor.
    void Close();
    // 005DB350. The destructor: the same steps (the holder member's own destructor then finds it
    // gone).
    void Destruct();

    CDXSurface* holder;         // +0x00
    std::uint32_t surfaceType;  // +0x04 BinkDDSurfaceType of the holder's surface
    RECT to;                    // +0x08
    BinkHeader* bink;           // +0x18
};
static_assert(offsetof(CBinkVideo, to) == 0x08);
static_assert(offsetof(CBinkVideo, bink) == 0x18);
