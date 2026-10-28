// Bink movies: the frame player (CBinkVideo::Frame, once per frame from the movie screen's draw: the next frame
// decoded when due, then blitted into the letterboxed rectangle), the blocking player (PlayMovie) and the
// loading screen's movie. Every frame goes to the new renderer as a CPU copy (render::MovieFrame / MovieBlit);
// the holders have no DirectDraw surface. See docs/notes/renderer.md, section 8.
#include "app.h"
#include "bink_video.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "device.h"
#include "net_session.h"
#include "sound.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>
#include "ui_cursor.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"
#include "world.h"

// Set while a .mob file loads (CWorldServer::LoadMobFile, AddMobEx): the loading screen's movie stays as it is.
std::uint8_t g_loadingMovieOff = /*007C554C*/ {};

namespace {

// BinkOpen, as the import slot reaches it: with Rebirth's MoviesPatch (addon's slot) the mod's
// Movies\ copy when there is one.
BinkHeader* OpenBink(const char* name, unsigned flags) {
    if (modfolders::Active(ei::rebirth::Patch::MoviesPatch)) return gameapi::BinkOpen(modfolders::MovieFile(name).c_str(), flags);
    return gameapi::BinkOpen(name, flags);
}

// The surface type the players keep (the game's BinkDDSurfaceType of the holder's surface): the holders have
// none, so the type of the CPU copy the new renderer gets.
constexpr unsigned kBinkSurface32R = 4;                      // BinkCopyToBuffer: R, G, B, X bytes (checked offline)

// The loading screen's movie: where it is drawn on the screen, its Bink handle and the surface type it was
// opened for.
RECT g_loadingScreen = /*007C5520*/ {};
BinkHeader* g_loadingBink = /*007C5550*/ {nullptr};
unsigned g_loadingSurfaceType = /*007C5554*/ {};

// The movie's surface holder: null at the start; the exit function its static initialiser registers releases
// it once.
Surface* g_loadingHolder = /*007C551C*/ {nullptr};
std::uint8_t g_loadingHolderReleased = /*007C5518*/ 0;

// Cleared while a timed movie plays.
void SetQuitAllowed(std::uint8_t value) { app::g_quitAllowed = value; }

// The decoded frame as BinkCopyToBuffer's 32-bit R, G, B, X type, for the new renderer.
void CopyFrame(BinkHeader* bink, std::vector<std::uint8_t>* frame) {
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    gameapi::BinkCopyToBuffer(bink, frame->data(), width * 4, height, 0, 0, kBinkSurface32R);
}

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The movie's surface holder for a w x h frame: the system-memory kind (the new renderer's memory). The game
// asked for a video-memory surface while a flag set from GetAvailableVidMem was 0, falling back to system
// memory when that threw.
Surface* NewMovieHolder(CTextureDevice* device, int width, int height) {
    Surface* holder = new Surface(device, width, height);
    if (holder) ++holder->refs_;
    return holder;
}

// A movie frame drawn outside the game's frame becomes a frame of the new renderer, shown by the present.
void PresentMovieFrame() { DevicePresent(g_sceneRenderer->device_); }

int g_movieKey;   // the blocking player's movie in the new renderer

} // namespace

void ReleaseLoadingHolder() {
    if (g_loadingHolderReleased & 1) return;
    g_loadingHolderReleased = static_cast<std::uint8_t>(g_loadingHolderReleased | 1);
    if (g_loadingHolder) g_loadingHolder->Release();
}

unsigned CBinkVideo::Frame(char toPrimary) {
    if (!options::g_options->video_) return 0;   // movies off
    BinkHeader* binkHandle = this->bink;
    if (!binkHandle) return 0;
    if (gameapi::BinkWait(binkHandle) == 0) {
        gameapi::BinkDoFrame(binkHandle);
        // The new renderer gets the frame from a CPU buffer (BinkCopyToBuffer's 32-bit R, G, B, X type).
        static std::vector<std::uint8_t> frame;
        CopyFrame(binkHandle, &frame);
        render::MovieFrame(this, frame.data(), static_cast<int>(binkHandle->width), static_cast<int>(binkHandle->height));
        gameapi::BinkNextFrame(binkHandle);
    }
    Surface* surfaceHolder = this->holder;
    Surface* target = toPrimary ? surfaceHolder->device->primary_ : surfaceHolder->device->renderTarget_;
    if (!device::SameSurface(target, surfaceHolder)) render::MovieBlit(this, ToRender(to));
    const BinkHeader* header = this->bink;
    return static_cast<int>(header->lastFrame) < static_cast<int>(header->frames) ? 1u : 0u;
}

// --- The movie players outside the frame -----------------------------------

// `frameTime`: ms per frame, or 0 for the movie's own rate. Esc / Space end it (`keysEnd`), Space jumps to the
// next frame when it does not; the screen is filled black first, a loading screen step first.
// Plays the movie in a message loop of its own, each frame into its letterboxed rectangle. Returns 0 when Esc ended it.
char PlayMovie(const std::string& name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
               char loadingStep) {
    if (!options::g_options->video_) return 1;
    const char* file = name.c_str();
    if (loadingStep) g_clientWorld->sounds_->StopAll(1);
    if (options::g_options->audio_) gameapi::BinkSetSoundSystem(gameapi::BinkOpenMiles, g_clientWorld->sounds_->waveDriver);
    BinkHeader* bink = OpenBink(file, 0);
    if (!bink) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", file);
        return 1;
    }
    gameapi::BinkSetVolume(bink, options::g_options->audioStreaming_ * 0x7FF6 / 100);
    g_cursor->ShowImage(0);
    // (The game fills the primary black and clears the render target when `clearScreen`: the new
    // frames fill the screen themselves, render::Fill below.)
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &g_sceneRenderer->device_->screen_);
    Surface* holder = NewMovieHolder(g_sceneRenderer->device_, width, height);
    // The frame's rectangle: mode 2 stretched to the screen's width (aspect kept), otherwise
    // centred at its size; both centred vertically (divisions round towards zero).
    RECT to;
    if (options::g_options->video_ == 2) {
        int screenWidth = screen.right - screen.left;
        int shown = static_cast<int>(static_cast<double>(screenWidth) * (static_cast<double>(height) / static_cast<double>(width)));
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
    DWORD start = gameapi::timeGetTime() - frameTime;
    bool escaped = false;
    std::vector<std::uint8_t> frame;
    MSG msg;
    for (;;) {
        if (gameapi::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_KEYDOWN) {
                if (msg.wParam != VK_ESCAPE && msg.wParam != VK_SPACE) continue;
                if (keysEnd) {
                    if (msg.wParam == VK_ESCAPE) escaped = true;
                    break;
                }
                if (!spaceSkips) continue;
                start = gameapi::timeGetTime() - frameTime * 2;
            }
            if (msg.message == WM_PAINT) {
                gameapi::TranslateMessage(&msg);
                gameapi::DispatchMessageA(&msg);
            }
            continue;
        }
        DWORD now = gameapi::timeGetTime();
        if (static_cast<int>(bink->lastFrame) >= static_cast<int>(bink->frames)) {
            if (frameTime == 0 || now - start >= frameTime) break;
            continue;
        }
        if (frameTime == 0) {
            if (gameapi::BinkWait(bink) != 0) continue;
        } else if (now - start < frameTime) {
            continue;
        }
        gameapi::BinkDoFrame(bink);
        CopyFrame(bink, &frame);
        render::MovieFrame(&g_movieKey, frame.data(), width, height);
        Surface* primary = holder->device->primary_;
        if (!device::SameSurface(primary, holder)) {
            if (clearScreen) render::Fill(ToRender(screen), 0);
            render::MovieBlit(&g_movieKey, ToRender(to));
            PresentMovieFrame();
        }
        if (bink->lastFrame != bink->frames) gameapi::BinkNextFrame(bink);
        start = now;
    }
    gameapi::BinkClose(bink);
    if (holder) holder->Release();
    SetQuitAllowed(1);
    g_cursor->ShowImage(1);
    return escaped ? 0 : 1;
}

// `progress` is a frame number. The loading screen's movie (the file named by a global, opened once) is decoded
// up to that frame and stretched over the screen. Nothing when the off switch is set.
void ShowLoadingMovie(int progress) {
    net::Pump();
    if (g_loadingMovieOff) return;
    g_cursor->ShowImage(0);
    if (!g_loadingHolder) {
        std::string path = DataFolderText();
        path += "Movies\\";
        path += app::g_loadingMovieName;  // progres.bik, or the zone's
        if (g_loadingBink) {
            gameapi::BinkClose(g_loadingBink);
            g_loadingBink = nullptr;
        }
        g_loadingBink = OpenBink(path.c_str(), 0);
        if (!g_loadingBink) {
            game::DebugLog("Can't open video file \"%s\"\n", path);
            return;
        }
        RECT screen;
        gameapi::CopyRect(&screen, &g_sceneRenderer->device_->screen_);
        Surface* holder =
            NewMovieHolder(g_sceneRenderer->device_, static_cast<int>(g_loadingBink->width), static_cast<int>(g_loadingBink->height));
        Surface* old = g_loadingHolder;
        g_loadingHolder = holder;
        if (old) old->Release();
        g_loadingSurfaceType = kBinkSurface32R;
        g_loadingScreen = screen;
    }
    BinkHeader* bink = g_loadingBink;
    int last = static_cast<int>(bink->frames) - 1;
    if (last < progress) progress = last;
    unsigned target = static_cast<unsigned>(progress) + 1;
    static std::vector<std::uint8_t> frame;
    bool decoded = false;
    for (;;) {
        unsigned current = bink->frameNum;
        if (current >= target && current != 1) break;
        gameapi::BinkDoFrame(bink);
        CopyFrame(bink, &frame);
        decoded = true;
        bink = g_loadingBink;
        gameapi::BinkGoto(bink, bink->frameNum + 1, 0);
        bink = g_loadingBink;
        if (bink->frames == bink->lastFrame) break;
    }
    g_cursor->TakeLock();
    bink = g_loadingBink;
    Surface* holder = g_loadingHolder;
    RECT source{0, 0, static_cast<LONG>(bink->width), static_cast<LONG>(bink->height)};
    Surface* primary = holder->device->primary_;
    if (!device::SameSurface(primary, holder)) {
        const void* key = &g_loadingScreen;   // the loading movie in the new renderer
        if (decoded) render::MovieFrame(key, frame.data(), source.right, source.bottom);
        render::MovieBlit(key, ToRender(g_loadingScreen));
        PresentMovieFrame();
    }
    g_cursor->ReleaseLock();
}

// --- Opening a movie for the frame's player ----------------------------------------------

// The titles' Create starts Titles.bik with it. The movie open before is closed (its handle stays in `bink` when
// movies are off), the loading screen steps when asked, Bink gets the Miles driver when movie sound is on; the
// screen rectangle: movies mode 1 centred at the movie's size, otherwise stretched to the screen's width with
// the height trunc(height / width * screen width), centred vertically (divisions by 2 round towards zero).
char CBinkVideo::Open(const char* name, char loadingStep) {
    if (bink) gameapi::BinkClose(bink);
    if (!options::g_options->video_) return 0;
    if (loadingStep) g_clientWorld->sounds_->StopAll(1);
    if (options::g_options->audio_) gameapi::BinkSetSoundSystem(gameapi::BinkOpenMiles, g_clientWorld->sounds_->waveDriver);
    bink = OpenBink(name, 0);
    if (!bink) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", name);
        return 0;
    }
    g_cursor->ShowImage(0);
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &g_sceneRenderer->device_->screen_);
    Surface* made = NewMovieHolder(g_sceneRenderer->device_, width, height);
    Surface* previous = holder;
    holder = made;
    if (previous) previous->Release();
    surfaceType = kBinkSurface32R;
    if (options::g_options->video_ == 1) {
        int x = (screen.right - width - screen.left) / 2;
        int y = (screen.bottom - height - screen.top) / 2;
        to = RECT{x, y, width + x, height + y};
    } else {
        int screenWidth = screen.right - screen.left;
        int shown = static_cast<int>(static_cast<double>(height) / static_cast<double>(width) * static_cast<double>(screenWidth));
        to.left = 0;
        to.right = screenWidth;
        to.top = (screen.bottom - screen.top - shown) / 2;
        to.bottom = to.top + shown;
    }
    return 1;
}

// --- Closing --------------------------------------------------------------------------------

void CloseLoadingMovie() {
    gameapi::BinkClose(g_loadingBink);
    Surface* holder = g_loadingHolder;
    g_loadingBink = nullptr;
    g_loadingHolder = nullptr;
    if (holder) holder->Release();
    g_cursor->ShowImage(1);
}

CBinkVideo* CBinkVideo::Construct() {
    holder = nullptr;
    bink = nullptr;
    return this;
}

void CBinkVideo::Close() {
    if (bink) {
        gameapi::BinkClose(bink);
        bink = nullptr;
    }
    Surface* old = holder;
    holder = nullptr;
    if (old) old->Release();
    g_cursor->ShowImage(1);
}

void CBinkVideo::Destruct() { Close(); }
