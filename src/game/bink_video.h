#pragma once

// Bink movies: the Bink handle's first fields and the game's video object.

#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>

struct Surface;

// Set while a .mob file loads: the loading screen's movie stays as it is.
extern std::uint8_t g_loadingMovieOff;

void ShowLoadingMovie(int progress);
// Plays a movie in a message loop of its own (name, the time per frame or 0, Esc / Space end it, Space skips
// frames, the screen cleared first, a loading screen step first, unused). Returns 0 when Esc ended it.
char PlayMovie(const std::string& name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                          char loadingStep, int unused);
// Closes the loading screen's movie once loading is over (game start and zone load): the handle and the holder
// let go, the cursor shown again.
void CloseLoadingMovie();

// The Bink handle's first fields (Bink 1.x: Width, Height, Frames, FrameNum, LastFrameNum).
struct BinkHeader {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t frames;       // the frame count
    std::uint32_t frameNum;     // the frame to decode next (1-based)
    std::uint32_t lastFrame;    // the frame decoded last
};
static_assert(offsetof(BinkHeader, lastFrame) == 0x10);

// CBinkVideo: the decode surface, its Bink surface type, the letterboxed screen rectangle and the Bink handle.
struct CBinkVideo {
    unsigned Frame(char toPrimary);
    // Closes the movie open before, opens `name` (with movies on), hides the
    // cursor, makes the decode surface's holder and the screen rectangle (centred at the movie's size
    // in movies mode 1, else stretched to the screen's width). Nonzero (the low byte) when it plays.
    char Open(const char* name, char loadingStep);
    CBinkVideo* Construct();
    // Closes the movie when one is open, lets the holder go and shows the cursor again.
    void Close();
    void Destruct();

    Surface* holder;
    std::uint32_t surfaceType;  // BinkDDSurfaceType of the holder's surface
    RECT to;
    BinkHeader* bink;
};
