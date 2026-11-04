// Bink movies: the frame player (BinkVideo::Frame, once per frame from the movie screen's draw: the next frame
// decoded when due, then blitted into the letterboxed rectangle), the blocking player (PlayMovie) and the
// loading screen's movie. Every frame goes to the new renderer as a CPU copy (render::MovieFrame / MovieBlit); no
// surface holds it. See docs/notes/renderer.md, section 8.
#include "app.h"
#include "bink_video.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "net_session.h"
#include "sound.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/platform.h"
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>
#include "ui_cursor.h"
#include "game_singletons.h"
#include "options.h"
#include "world.h"


// Set while a .mob file loads (WorldServer::LoadMobFile, AddMobEx): the loading screen's movie stays as it is.
std::uint8_t g_loadingMovieOff = /*007C554C*/ {};

namespace {

// A movie opened with its sound when the sound option is on; with Rebirth's MoviesPatch the mod's Movies\ copy
// when there is one.
video::Movie* OpenMovie(const char* name) {
    const bool sound = options::g_options.audio != 0;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::MoviesPatch)) return video::Open(modfolders::MovieFile(name).c_str(), sound);
    return video::Open(name, sound);
}

// The loading screen's movie: where it is drawn on the screen and the movie (null until it is opened).
Rect g_loadingScreen = /*007C5520*/ {};
video::Movie* g_loadingMovie = /*007C5550*/ {nullptr};

// The decoded frame as 32-bit R, G, B, X pixels, for the new renderer.
void CopyFrame(const video::Movie* movie, std::vector<std::uint8_t>* frame) {
    int width = video::Width(movie), height = video::Height(movie);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    video::CopyFrame(movie, frame->data(), width * 4);
}



// A movie frame drawn outside the game's frame becomes a frame of the new renderer, shown by the present.
void PresentMovieFrame() { DevicePresent(g_sceneRenderer.device); }

int g_movieKey;   // the blocking player's movie in the new renderer

// The movie's last frame has been decoded.
bool Ended(const video::Movie* movie) { return video::DecodedFrameNumber(movie) >= video::FrameCount(movie); }

// Where a movie of width x height shows on the screen as it is now (each frame asks: the screen may take another size
// while a movie plays): `centred` at its own size in the middle, otherwise as large as fits with its aspect kept
// (across the screen's width, letterboxed; on a screen wider than the movie, as high as the screen, pillarboxed),
// centred (divisions by 2 round towards zero).
Rect MovieRect(int width, int height, bool centred) {
    const Rect& screen = g_sceneRenderer.device->screen;
    const int screenWidth = screen.Width(), screenHeight = screen.Height();
    int shownWidth = width, shownHeight = height;
    if (!centred) {
        shownWidth = screenWidth;
        shownHeight = screenWidth * height / width;
        if (shownHeight > screenHeight) {
            shownHeight = screenHeight;
            shownWidth = screenHeight * width / height;
        }
    }
    const int left = (screenWidth - shownWidth) / 2, top = (screenHeight - shownHeight) / 2;
    return Rect{left, top, left + shownWidth, top + shownHeight};
}

} // namespace

unsigned BinkVideo::Frame() {
    if (!options::g_options.video) return 0;    // movies off
    if (!movie) return 0;
    if (!video::Waiting(movie)) {
        video::DecodeFrame(movie);
        // The new renderer gets the frame from a CPU buffer (32-bit R, G, B, X).
        static std::vector<std::uint8_t> frame;
        CopyFrame(movie, &frame);
        render::MovieFrame(this, frame.data(), video::Width(movie), video::Height(movie));
        video::Advance(movie);
    }
    render::MovieBlit(this, MovieRect(video::Width(movie), video::Height(movie), centred));
    return Ended(movie) ? 0u : 1u;
}

// --- The movie players outside the frame -----------------------------------

// Plays the movie at its own rate in a message loop of its own, each frame into its letterboxed rectangle. Esc / Space
// end it (`keysEnd`); the screen is filled black first (`clearScreen`), a loading screen step first (`loadingStep`).
// Returns false when Esc ended it.
bool PlayMovie(const std::string& name, bool keysEnd, bool clearScreen, bool loadingStep) {
    if (!options::g_options.video) return true;
    const char* file = name.c_str();
    if (loadingStep) g_clientWorld.sounds->StopAll(1);
    video::Movie* movie = OpenMovie(file);
    if (!movie) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", file);
        return true;
    }
    video::SetVolume(movie, options::g_options.audioStreaming * 0x7FF6 / 100);
    g_cursor.ShowImage(0);
    // (The game fills the primary black and clears the render target when `clearScreen`: the new
    // frames fill the screen themselves, render::Fill below.)
    int width = video::Width(movie), height = video::Height(movie);
    // Mode 2 as large as fits, otherwise centred at its size.
    const bool centred = options::g_options.video != 2;
    bool escaped = false;
    std::vector<std::uint8_t> frame;
    platform::Event event;
    for (;;) {
        if (platform::TakeEvent(&event)) {
            if (event.type == platform::EventType::KeyDown) {
                if (event.key != platform::kKeyEscape && event.key != platform::kKeySpace) continue;
                if (!keysEnd) continue;
                if (event.key == platform::kKeyEscape) escaped = true;
                break;
            } else if (event.type == platform::EventType::Quit) {   // the movie ends; the main loop ends on it
                platform::PostQuit(event.quitCode);
                break;
            } else {
                app::HandleWindowEvent(event);
            }
            continue;
        }
        if (Ended(movie)) break;
        if (video::Waiting(movie)) continue;
        video::DecodeFrame(movie);
        CopyFrame(movie, &frame);
        render::MovieFrame(&g_movieKey, frame.data(), width, height);
        if (clearScreen) render::Fill(g_sceneRenderer.device->screen, 0);
        render::MovieBlit(&g_movieKey, MovieRect(width, height, centred));
        PresentMovieFrame();
        if (!Ended(movie)) video::Advance(movie);
    }
    video::Close(movie);
    g_cursor.ShowImage(1);
    return !escaped;
}

// `progress` is a frame number. The loading screen's movie (the file named by a global, opened once) is decoded
// up to that frame and stretched over the screen. Nothing when the off switch is set.
void ShowLoadingMovie(int progress) {
    net::Pump();
    if (g_loadingMovieOff) return;
    g_cursor.ShowImage(0);
    if (!g_loadingMovie) {
        std::string path = DataFolderText();
        path += "Movies\\";
        path += app::g_loadingMovieName;  // progres.bik, or the zone's
        g_loadingMovie = OpenMovie(path.c_str());
        if (!g_loadingMovie) {
            game::DebugLog("Can't open video file \"%s\"\n", path);
            return;
        }
    }
    g_loadingScreen = g_sceneRenderer.device->screen;
    video::Movie* movie = g_loadingMovie;
    int last = static_cast<int>(video::FrameCount(movie)) - 1;
    if (last < progress) progress = last;
    unsigned target = static_cast<unsigned>(progress) + 1;
    static std::vector<std::uint8_t> frame;
    bool decoded = false;
    for (;;) {
        unsigned current = video::NextFrameNumber(movie);
        if (current >= target && current != 1) break;
        video::DecodeFrame(movie);
        CopyFrame(movie, &frame);
        decoded = true;
        video::GoToFrame(movie, video::NextFrameNumber(movie) + 1);
        if (Ended(movie)) break;
    }
    const void* key = &g_loadingScreen;   // the loading movie in the new renderer
    if (decoded) render::MovieFrame(key, frame.data(), video::Width(movie), video::Height(movie));
    render::MovieBlit(key, g_loadingScreen);
    PresentMovieFrame();
}

// --- Opening a movie for the frame's player ----------------------------------------------

// The titles' Create starts Titles.bik with it. The movie open before is closed, the loading screen steps when
// asked, the movie's sound plays when the sound option is on; its place: movies mode 1 centred at the movie's size,
// otherwise as large as fits (MovieRect).
char BinkVideo::Open(const char* name, char loadingStep) {
    video::Close(movie);
    movie = nullptr;
    if (!options::g_options.video) return 0;
    if (loadingStep) g_clientWorld.sounds->StopAll(1);
    movie = OpenMovie(name);
    if (!movie) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", name);
        return 0;
    }
    g_cursor.ShowImage(0);
    centred = options::g_options.video == 1;
    return 1;
}

// --- Closing --------------------------------------------------------------------------------

void CloseLoadingMovie() {
    video::Close(g_loadingMovie);
    g_loadingMovie = nullptr;
    g_cursor.ShowImage(1);
}

void BinkVideo::Close() {
    if (movie) {
        video::Close(movie);
        movie = nullptr;
    }
    g_cursor.ShowImage(1);
}
