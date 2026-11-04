#pragma once

// Bink movies: the game's players on the platform's movies (platform/platform.h).

#include "lib/rect.h"
#include "platform/platform.h"
#include "video/movie.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Set while a .mob file loads: the loading screen's movie stays as it is.
extern std::uint8_t g_loadingMovieOff;

void ShowLoadingMovie(int progress);
// Plays a movie at its own rate in a message loop of its own (name, Esc / Space end it, the screen cleared first, a
// loading screen step first). Returns false when Esc ended it.
bool PlayMovie(const std::string& name, bool keysEnd, bool clearScreen, bool loadingStep);
// Closes the loading screen's movie once loading is over (game start and zone load): the handle closed, the cursor
// shown again.
void CloseLoadingMovie();

// BinkVideo: the movie and how it is placed on the screen (letterboxed).
struct BinkVideo {
    BinkVideo() = default;
    // Closes the movie as Close does (the cursor shown again).
    ~BinkVideo() { Close(); }
    BinkVideo(const BinkVideo&) = delete;
    BinkVideo& operator=(const BinkVideo&) = delete;

    // The next frame decoded when due, then drawn at its place on the screen as it is now; 0 when movies are off,
    // nothing is open or the last frame was decoded.
    unsigned Frame();
    // Closes the movie open before, opens `name` (with movies on), hides the cursor and chooses its place (centred at
    // the movie's size in movies mode 1, else as large as fits). True when it plays.
    bool Open(const char* name, bool loadingStep);
    // Closes the movie when one is open and shows the cursor again.
    void Close();

    bool centred = false;   // shown at its own size (movies mode 1), else as large as fits
    video::Movie* movie = nullptr;
};
