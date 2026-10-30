#pragma once

// Bink movies: the game's players on the platform's movies (platform/video.h).

#include "lib/rect.h"
#include "platform/video.h"

#include <cstddef>
#include <cstdint>
#include <string>

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

// BinkVideo: the letterboxed screen rectangle and the movie.
struct BinkVideo {
    BinkVideo() = default;
    // Closes the movie as Close does (the cursor shown again).
    ~BinkVideo() { Close(); }
    BinkVideo(const BinkVideo&) = delete;
    BinkVideo& operator=(const BinkVideo&) = delete;

    // The next frame decoded when due, then drawn into the rectangle; 0 when movies are off, nothing is open or the
    // last frame was decoded.
    unsigned Frame();
    // Closes the movie open before, opens `name` (with movies on), hides the cursor and sets the screen rectangle
    // (centred at the movie's size in movies mode 1, else stretched to the screen's width). Nonzero (the low byte)
    // when it plays.
    char Open(const char* name, char loadingStep);
    // Closes the movie when one is open and shows the cursor again.
    void Close();

    Rect to{};
    platform::video::Movie* movie = nullptr;
};
