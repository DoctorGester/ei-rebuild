#pragma once

// Bink movies: the Bink handle's first fields and the game's video object.

#include <cstddef>
#include <cstdint>
#include <string>

#include <windows.h>


// Set while a .mob file loads: the loading screen's movie stays as it is.
extern std::uint8_t g_loadingMovieOff;

void ShowLoadingMovie(int progress);
// Plays a movie in a message loop of its own (name, the time per frame or 0, Esc / Space end it, Space skips
// frames, the screen cleared first, a loading screen step first). Returns false when Esc ended it.
bool PlayMovie(const std::string& name, unsigned frameTime, bool keysEnd, bool spaceSkips, bool clearScreen,
               bool loadingStep);
// Closes the loading screen's movie once loading is over (game start and zone load): the handle closed, the cursor
// shown again.
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

// CBinkVideo: the letterboxed screen rectangle and the Bink handle.
struct CBinkVideo {
    CBinkVideo() = default;
    // Closes the movie as Close does (the cursor shown again).
    ~CBinkVideo() { Close(); }
    CBinkVideo(const CBinkVideo&) = delete;
    CBinkVideo& operator=(const CBinkVideo&) = delete;

    // The next frame decoded when due, then drawn into the rectangle; 0 when movies are off, nothing is open or the
    // last frame was decoded.
    unsigned Frame();
    // Closes the movie open before, opens `name` (with movies on), hides the cursor and sets the screen rectangle
    // (centred at the movie's size in movies mode 1, else stretched to the screen's width). Nonzero (the low byte)
    // when it plays.
    char Open(const char* name, char loadingStep);
    // Closes the movie when one is open and shows the cursor again.
    void Close();

    RECT to{};
    BinkHeader* bink = nullptr;
};
