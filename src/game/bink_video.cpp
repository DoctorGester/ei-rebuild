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

// Cleared while a timed movie plays.
void SetQuitAllowed(std::uint8_t value) { app::g_quitAllowed = value; }

// The decoded frame as 32-bit R, G, B, X pixels, for the new renderer.
void CopyFrame(const video::Movie* movie, std::vector<std::uint8_t>* frame) {
    int width = video::Width(movie), height = video::Height(movie);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    video::CopyFrame(movie, frame->data(), width * 4);
}

render::Rect ToRender(const Rect& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }


// A movie frame drawn outside the game's frame becomes a frame of the new renderer, shown by the present.
void PresentMovieFrame() { DevicePresent(g_sceneRenderer.device); }

int g_movieKey;   // the blocking player's movie in the new renderer

// The movie's last frame has been decoded.
bool Ended(const video::Movie* movie) { return video::DecodedFrameNumber(movie) >= video::FrameCount(movie); }

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
    render::MovieBlit(this, ToRender(to));
    return Ended(movie) ? 0u : 1u;
}

// --- The movie players outside the frame -----------------------------------

// `frameTime`: ms per frame, or 0 for the movie's own rate. Esc / Space end it (`keysEnd`), Space jumps to the
// next frame when it does not; the screen is filled black first, a loading screen step first.
// Plays the movie in a message loop of its own, each frame into its letterboxed rectangle. Returns false when Esc ended it.
bool PlayMovie(const std::string& name, unsigned frameTime, bool keysEnd, bool spaceSkips, bool clearScreen,
               bool loadingStep) {
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
    Rect screen;
    rects::Copy(&screen, &g_sceneRenderer.device->screen);
    // The frame's rectangle: mode 2 stretched to the screen's width (aspect kept), otherwise
    // centred at its size; both centred vertically (divisions round towards zero).
    Rect to;
    if (options::g_options.video == 2) {
        int screenWidth = screen.right - screen.left;
        int shown = screenWidth * height / width;
        to.left = 0;
        to.right = screenWidth;
        to.top = (screen.bottom - screen.top - shown) / 2;
        to.bottom = to.top + shown;
    } else {
        to.left = (screen.right - width - screen.left) / 2;
        to.top = (screen.bottom - screen.top - height) / 2;
        to.right = width + to.left;
        to.bottom = height + to.top;
    }
    if (frameTime) SetQuitAllowed(0);
    std::uint32_t start = platform::Milliseconds() - frameTime;
    bool escaped = false;
    std::vector<std::uint8_t> frame;
    platform::Event event;
    for (;;) {
        if (platform::TakeEvent(&event)) {
            if (event.type == platform::EventType::KeyDown) {
                if (event.key != platform::kKeyEscape && event.key != platform::kKeySpace) continue;
                if (keysEnd) {
                    if (event.key == platform::kKeyEscape) escaped = true;
                    break;
                }
                if (!spaceSkips) continue;
                start = platform::Milliseconds() - frameTime * 2;
            }
            if (event.type == platform::EventType::Paint) platform::DispatchEvent(event);
            continue;
        }
        std::uint32_t now = platform::Milliseconds();
        if (Ended(movie)) {
            if (frameTime == 0 || now - start >= frameTime) break;
            continue;
        }
        if (frameTime == 0) {
            if (video::Waiting(movie)) continue;
        } else if (now - start < frameTime) {
            continue;
        }
        video::DecodeFrame(movie);
        CopyFrame(movie, &frame);
        render::MovieFrame(&g_movieKey, frame.data(), width, height);
        if (clearScreen) render::Fill(ToRender(screen), 0);
        render::MovieBlit(&g_movieKey, ToRender(to));
        PresentMovieFrame();
        if (!Ended(movie)) video::Advance(movie);
        start = now;
    }
    video::Close(movie);
    SetQuitAllowed(1);
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
        g_loadingScreen = g_sceneRenderer.device->screen;
    }
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
    g_cursor.TakeLock();
    const void* key = &g_loadingScreen;   // the loading movie in the new renderer
    if (decoded) render::MovieFrame(key, frame.data(), video::Width(movie), video::Height(movie));
    render::MovieBlit(key, ToRender(g_loadingScreen));
    PresentMovieFrame();
    g_cursor.ReleaseLock();
}

// --- Opening a movie for the frame's player ----------------------------------------------

// The titles' Create starts Titles.bik with it. The movie open before is closed, the loading screen steps when
// asked, the movie's sound plays when the sound option is on; the screen rectangle: movies mode 1 centred at the
// movie's size, otherwise stretched to the screen's width with the height trunc(height / width * screen width),
// centred vertically (divisions by 2 round towards zero).
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
    int width = video::Width(movie), height = video::Height(movie);
    Rect screen;
    rects::Copy(&screen, &g_sceneRenderer.device->screen);
    if (options::g_options.video == 1) {
        int x = (screen.right - width - screen.left) / 2;
        int y = (screen.bottom - height - screen.top) / 2;
        to = Rect{x, y, width + x, height + y};
    } else {
        int screenWidth = screen.right - screen.left;
        int shown = screenWidth * height / width;
        to.left = 0;
        to.right = screenWidth;
        to.top = (screen.bottom - screen.top - shown) / 2;
        to.bottom = to.top + shown;
    }
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
