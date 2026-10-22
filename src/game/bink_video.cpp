// A Bink movie's frame step (CBinkVideo, 005DBD80, thiscall, RET 4; called once per frame by the
// movie screen's draw 0062BD80, inside the game's frame). When the movie's next frame is due
// (BinkWait) it is decoded (BinkDoFrame) and the movie steps on (BinkNextFrame). Every call then
// blits the whole movie frame into the video's rectangle (letterboxed by 005DBA50) of the render
// target, or of the primary surface when `toPrimary`. Returns whether frames are left. The new
// renderer (src/render) takes the frame from a CPU copy of the decoded frame (render::MovieFrame)
// and the blit as part of its frame (MovieBlit). See docs/notes/renderer.md.
//
// Also the two movie players that draw outside the game's frame, straight to the primary surface
// in the original (ticket R6): the blocking player 005DB3F0 (intro, titles, the script's PlayMovie;
// its message loop at 005DB788), and the loading screen's movie 005DA400 (progres*.bik, one step per
// call). They hand each frame over as a frame of the new renderer's own (render::MovieFrame /
// MovieBlit, then the present 004EF610).
//
// And the frame player's open (005DBA50, ticket R7). The movies' holders (004EF7B0) have no
// DirectDraw surface: nothing decodes into them and BinkDDSurfaceType is not asked (the type kept is
// that of the CPU copy). The original's Lock / BinkCopyToBuffer / Unlock into the holder's surface
// and its Blts are gone with the DirectDraw fallback (R11a), and so is the holder's colour fill
// 005DB9E0, which nothing of ours calls (R11b).
#include "app.h"
#include "bink_video.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "device.h"   // device::SameSurface
#include "net_session.h"
#include "sound.h"
#include "texture.h"

#include "ei/hook.h"
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

// Set while a .mob file loads (CWorldServer::LoadMobFile, AddMobEx): the loading screen's movie stays as it is.
std::uint8_t g_loadingMovieOff = /*007C554C*/ {};

namespace {

// Bink through game.exe's import table (stdcall; the image's: ei_hook.dll's table, S18f).
using BinkFn = int(__stdcall*)(BinkHeader*);
using BinkCopyFn = int(__stdcall*)(BinkHeader*, void*, int, int, int, int, unsigned);
using BinkOpenFn = BinkHeader*(__stdcall*)(const char*, unsigned);
using BinkCloseFn = void(__stdcall*)(BinkHeader*);
using BinkVolumeFn = void(__stdcall*)(BinkHeader*, int);
using BinkSoundSystemFn = void(__stdcall*)(void*, void*);   // (open function, Miles driver)
using BinkGotoFn = void(__stdcall*)(BinkHeader*, unsigned, int);
BinkFn BinkWait() { return ei::ImportAs<BinkFn>(ei::Imports().BinkWait); }
BinkFn BinkDoFrame() { return ei::ImportAs<BinkFn>(ei::Imports().BinkDoFrame); }
BinkFn BinkNextFrame() { return ei::ImportAs<BinkFn>(ei::Imports().BinkNextFrame); }
BinkCopyFn BinkCopyToBuffer() { return ei::ImportAs<BinkCopyFn>(ei::Imports().BinkCopyToBuffer); }
BinkOpenFn BinkOpen() { return ei::ImportAs<BinkOpenFn>(ei::Imports().BinkOpen); }
BinkCloseFn BinkClose() { return ei::ImportAs<BinkCloseFn>(ei::Imports().BinkClose); }
BinkVolumeFn BinkSetVolume() { return ei::ImportAs<BinkVolumeFn>(ei::Imports().BinkSetVolume); }
BinkSoundSystemFn BinkSetSoundSystem() { return ei::ImportAs<BinkSoundSystemFn>(ei::Imports().BinkSetSoundSystem); }
void* BinkOpenMiles() { return ei::ImportAs<void*>(ei::Imports().BinkOpenMiles); }
BinkGotoFn BinkGoto() { return ei::ImportAs<BinkGotoFn>(ei::Imports().BinkGoto); }
// BinkOpen, as the import slot reaches it: with Rebirth's MoviesPatch (addon's slot) the mod's
// Movies\ copy when there is one. In a hooked Rebirth launch the slot is addon's already.
BinkHeader* OpenBink(const char* name, unsigned flags) {
    if (modfolders::Active(ei::rebirth::Patch::MoviesPatch)) return BinkOpen()(modfolders::MovieFile(name).c_str(), flags);
    return BinkOpen()(name, flags);
}

constexpr unsigned kBinkSurface32R = 4;                      // BinkCopyToBuffer: R, G, B, X bytes (checked offline)

// The loading screen's movie (005DA400): where it is drawn on the screen, its Bink handle and the surface type it
// was opened for (its surface holder is g_loadingHolder; the off switch g_loadingMovieOff, bink_video.h).
RECT g_loadingScreen = /*007C5520*/ {};
BinkHeader* g_loadingBink = /*007C5550*/ {nullptr};
unsigned g_loadingSurfaceType = /*007C5554*/ {};

// Game globals.
int MoviesMode() { return options::TheOptions()->video_; }           // 0 off, 1 at size, 2 stretched to the width
int MovieSound() { return options::TheOptions()->audio_; }
int MovieVolume() { return options::TheOptions()->audioStreaming_; }          // percent

// The movie's surface holder: null at the start (the static initialiser 005DA360 stores it); the exit function that
// initialiser registers releases it once (guard bit in 007C5518). docs/notes/standalone.md, "The static initialisers'
// objects".
CDXSurface* g_loadingHolder = /*007C551C*/ {nullptr};
std::uint8_t g_loadingHolderReleased = /*007C5518*/ 0;

void __cdecl ReleaseLoadingHolder() {   // 005DA380
    if (g_loadingHolderReleased & 1) return;
    g_loadingHolderReleased = static_cast<std::uint8_t>(g_loadingHolderReleased | 1);
    if (g_loadingHolder) g_loadingHolder->Release();
}
// 00455F10 (fastcall, CL): sets app::g_quitAllowed (cleared while a timed movie plays).
void SetQuitAllowed(std::uint8_t value) { app::g_quitAllowed = value; }

// The decoded frame as BinkCopyToBuffer's 32-bit R, G, B, X type, for the new renderer.
void CopyFrame(BinkHeader* bink, std::vector<std::uint8_t>* frame) {
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    BinkCopyToBuffer()(bink, frame->data(), width * 4, height, 0, 0, kBinkSurface32R);
}

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The surface type the players keep (the original's BinkDDSurfaceType of the holder's surface): the
// holders have none, so the type of the CPU copy the new renderer gets.
unsigned SurfaceType() { return kBinkSurface32R; }

} // namespace

// 005DBD80.
unsigned CBinkVideo::Frame(char toPrimary) {
    if (!MoviesMode()) return 0;   // movies off
    BinkHeader* binkHandle = this->bink;
    if (!binkHandle) return 0;
    if (BinkWait()(binkHandle) == 0) {
        BinkDoFrame()(binkHandle);
        // The new renderer gets the frame from a CPU buffer (BinkCopyToBuffer's 32-bit R, G, B, X
        // type).
        static std::vector<std::uint8_t> frame;
        CopyFrame(binkHandle, &frame);
        render::MovieFrame(this, frame.data(), static_cast<int>(binkHandle->width), static_cast<int>(binkHandle->height));
        BinkNextFrame()(binkHandle);
    }
    CDXSurface* surfaceHolder = this->holder;
    CDXSurface* target = toPrimary ? surfaceHolder->device->primary_ : surfaceHolder->device->renderTarget_;
    if (!device::SameSurface(target, surfaceHolder)) render::MovieBlit(this, ToRender(to));
    const BinkHeader* header = this->bink;
    return static_cast<int>(header->lastFrame) < static_cast<int>(header->frames) ? 1u : 0u;
}
EI_REWRITTEN(0x005DBD80, CBinkVideo::Frame);

// --- The movie players outside the frame (005DB3F0, 005DA400) -----------------------------------

namespace {

// The movie's surface holder (0x20 bytes on the game's heap) for a w x h frame. The original asks
// for a video-memory surface through a DDSURFACEDESC2 (004EFA60) when 00784756 is 0, falling back to
// 004EF7B0 in a catch block when that throws. 00784756 is written by the game start (00456E35, from
// GetAvailableVidMem): the switch-over's stand-in reports 256 MB, which clears it; without DirectDraw
// (device.h) the video-memory surface would be refused and its throw end the game, so the holder is
// 004EF7B0's (only the new renderer's memory, ticket R9a2).
CDXSurface* NewMovieHolder(CTextureDevice* device, int width, int height) {
    CDXSurface* memory = gamecrt::operator_new_as<CDXSurface>();
    CDXSurface* holder = nullptr;
    if (memory) holder = memory->Construct(device, width, height);
    if (holder) ++holder->refCount;
    return holder;
}

// A movie frame drawn outside the game's frame becomes a frame of the new renderer, shown by the
// present (004EF610).
void PresentMovieFrame() { DevicePresent(TheRenderDevice()); }

int g_movieKey;   // the blocking player's movie in the new renderer

} // namespace

// 005DB3F0 (fastcall: the file name, the time per frame in ms or 0 for the movie's own rate; then on
// the stack: Esc / Space end it, Space jumps to the next frame (when they do not end it), fill the
// screen black first, a loading screen step first, and a fifth argument never read; RET 0x14).
// Plays the movie in a message loop of its own, each frame decoded into the movie's surface and
// blitted into its letterboxed rectangle of the primary surface. Returns 0 when Esc ended it.
char __fastcall PlayMovie(const char* name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                          char loadingStep, int /*unused*/) {
    // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
    if (!MoviesMode()) return 1;
    const char* file = name ? name : "";   // the original copies it into a CString
    if (loadingStep) TheSoundManager()->StopAll(1);
    if (MovieSound()) BinkSetSoundSystem()(BinkOpenMiles(), TheSoundManager()->waveDriver);
    BinkHeader* bink = OpenBink(file, 0);
    if (!bink) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", file);   // "Can't open (or bad format) video file %s."
        return 1;
    }
    BinkSetVolume()(bink, MovieVolume() * 0x7FF6 / 100);
    TheCursor()->ShowImage(0);
    // (The original fills the primary black and clears the render target when `clearScreen`: the new
    // frames fill the screen themselves, render::Fill below.)
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &TheRenderDevice()->screen_);
    CDXSurface* holder = NewMovieHolder(TheRenderDevice(), width, height);
    // The frame's rectangle: mode 2 stretched to the screen's width (aspect kept), otherwise
    // centred at its size; both centred vertically (divisions round towards zero).
    RECT to;
    if (MoviesMode() == 2) {
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
        // (The harness recognised the original's loop by PeekMessageA's return address, 005DB788.)
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
            // The last frame stays up for the frame time.
            if (frameTime == 0 || now - start >= frameTime) break;
            continue;
        }
        if (frameTime == 0) {
            if (BinkWait()(bink) != 0) continue;
        } else if (now - start < frameTime) {
            continue;
        }
        BinkDoFrame()(bink);
        CopyFrame(bink, &frame);
        render::MovieFrame(&g_movieKey, frame.data(), width, height);
        CDXSurface* primary = holder->device->primary_;
        if (!device::SameSurface(primary, holder)) {
            if (clearScreen) render::Fill(ToRender(screen), 0);
            render::MovieBlit(&g_movieKey, ToRender(to));
            PresentMovieFrame();
        }
        if (bink->lastFrame != bink->frames) BinkNextFrame()(bink);
        start = now;
    }
    BinkClose()(bink);
    if (holder) holder->Release();
    SetQuitAllowed(1);
    TheCursor()->SaveBackground(1);
    TheCursor()->ShowImage(1);
    return escaped ? 0 : 1;
}
EI_REWRITTEN(0x005DB3F0, PlayMovie);

// 005DA400 (fastcall: the loading progress as a frame number; plain RET). The loading screen's movie
// (the file named by 00791EE0, opened once into 007C5550 with its surface holder 007C551C, the
// surface type 007C5554 and the screen rectangle 007C5520) is decoded up to that frame and its
// frame stretched over the screen of the primary surface. Nothing when 007C554C is set.
void __fastcall ShowLoadingMovie(int progress) {
    net::Pump();   // 004122D0
    if (g_loadingMovieOff) return;
    TheCursor()->ShowImage(0);
    if (!g_loadingHolder) {
        // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
        std::string path = DataFolderText();
        path += "Movies\\";
        path += app::g_loadingMovieName;  // progres.bik, or the zone's
        if (g_loadingBink) {
            BinkClose()(g_loadingBink);
            g_loadingBink = nullptr;
        }
        g_loadingBink = OpenBink(path.c_str(), 0);
        if (!g_loadingBink) {
            game::DebugLog("Can't open video file \"%s\"\n", path.c_str());   // "Can't open video file \"%s\""
            return;
        }
        RECT screen;
        gameapi::CopyRect(&screen, &TheRenderDevice()->screen_);
        CDXSurface* holder =
            NewMovieHolder(TheRenderDevice(), static_cast<int>(g_loadingBink->width), static_cast<int>(g_loadingBink->height));
        CDXSurface* old = g_loadingHolder;
        g_loadingHolder = holder;
        if (old) old->Release();
        g_loadingSurfaceType = SurfaceType();
        g_loadingScreen = screen;
        TheCursor()->SaveBackground(1);
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
        BinkDoFrame()(bink);
        CopyFrame(bink, &frame);
        decoded = true;
        bink = g_loadingBink;
        BinkGoto()(bink, bink->frameNum + 1, 0);
        bink = g_loadingBink;
        if (bink->frames == bink->lastFrame) break;
    }
    TheCursor()->TakeLock();
    bink = g_loadingBink;
    CDXSurface* holder = g_loadingHolder;
    RECT source{0, 0, static_cast<LONG>(bink->width), static_cast<LONG>(bink->height)};
    CDXSurface* primary = holder->device->primary_;
    if (!device::SameSurface(primary, holder)) {
        const void* key = &g_loadingScreen;   // the loading movie in the new renderer
        if (decoded) render::MovieFrame(key, frame.data(), source.right, source.bottom);
        render::MovieBlit(key, ToRender(g_loadingScreen));
        PresentMovieFrame();
    }
    TheCursor()->SaveBackground(1);
    TheCursor()->ReleaseLock();
}
EI_REWRITTEN(0x005DA400, ShowLoadingMovie);

// --- Opening a movie for the frame's player (005DBA50) ----------------------------------------------

// 005DBA50 (thiscall, RET 8; the titles' Create 0062B7D0 starts Titles.bik with it). The movie open
// before is closed (its handle stays in +0x18 when movies are off), the loading screen steps when
// asked, Bink gets the Miles driver when movie sound is on; then the movie is opened, the cursor
// hidden (005E5270 0), the decode surface's holder made (NewMovieHolder: one without a DirectDraw
// surface, so BinkDDSurfaceType is not asked) and the
// screen rectangle set: movies mode 1 centred at the movie's size, otherwise stretched to the screen's
// width with the height trunc(height / width * screen width), centred vertically (divisions by 2
// round towards zero). The original adds two zero offsets to the centring (kept out).
char CBinkVideo::Open(const char* name, char loadingStep) {
    // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
    if (bink) BinkClose()(bink);
    if (!MoviesMode()) return 0;
    if (loadingStep) TheSoundManager()->StopAll(1);
    if (MovieSound()) BinkSetSoundSystem()(BinkOpenMiles(), TheSoundManager()->waveDriver);
    bink = OpenBink(name, 0);
    if (!bink) {
        game::DebugLog("Can't open (or bad format) video file %s.\n", name);   // "Can't open (or bad format) video file %s."
        return 0;
    }
    TheCursor()->ShowImage(0);
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &TheRenderDevice()->screen_);
    CDXSurface* made = NewMovieHolder(TheRenderDevice(), width, height);
    CDXSurface* previous = holder;
    holder = made;
    if (previous) previous->Release();
    surfaceType = SurfaceType();
    if (MoviesMode() == 1) {
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
EI_REWRITTEN(0x005DBA50, CBinkVideo::Open);

// --- Closing (A-W14) --------------------------------------------------------------------------------

void CloseLoadingMovie() {
    BinkClose()(g_loadingBink);
    CDXSurface* holder = g_loadingHolder;
    g_loadingBink = nullptr;
    g_loadingHolder = nullptr;
    if (holder) holder->Release();
    TheCursor()->ShowImage(1);
}
EI_REWRITTEN(0x005DA3C0, CloseLoadingMovie);
EI_REPLACE(0x005DA380, ReleaseLoadingHolder);

CBinkVideo* CBinkVideo::Construct() {
    holder = nullptr;
    bink = nullptr;
    return this;
}
EI_REWRITTEN(0x005DB340, CBinkVideo::Construct);

void CBinkVideo::Close() {
    if (bink) {
        BinkClose()(bink);
        bink = nullptr;
    }
    CDXSurface* old = holder;
    holder = nullptr;
    if (old) old->Release();
    TheCursor()->SaveBackground(1);
    TheCursor()->ShowImage(1);
}
EI_REWRITTEN(0x005DBF70, CBinkVideo::Close);

void CBinkVideo::Destruct() { Close(); }
EI_REWRITTEN(0x005DB350, CBinkVideo::Destruct);
