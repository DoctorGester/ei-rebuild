// A Bink movie's frame step (CBinkVideo, 005DBD80, thiscall, RET 4; called once per frame by the
// movie screen's draw 0062BD80, inside the game's frame). When the movie's next frame is due
// (BinkWait) it is decoded (BinkDoFrame) into the video's own surface (its holder, made by 005DBA50
// in the format BinkDDSurfaceType gave) through a Lock / BinkCopyToBuffer / Unlock, and the movie
// steps on (BinkNextFrame). Every call then blits the whole movie frame into the video's rectangle
// (letterboxed by 005DBA50) of the render target, or of the primary surface when `toPrimary`.
// Returns whether frames are left. The new renderer (src/render) gets the same blit as part of its
// frame, from a CPU copy of the decoded frame; when it presents by itself the video's surface is not
// filled and the old blit is skipped. See docs/notes/renderer.md.
//
// Also the two movie players that draw outside the game's frame, straight to the primary surface
// (ticket R6): the blocking player 005DB3F0 (intro, titles, the script's PlayMovie; its message loop
// at 005DB788) with its screen fill 005DB9E0, and the loading screen's movie 005DA400 (progres*.bik,
// one step per call). With the new renderer they hand each frame over as a frame of its own
// (render::MovieFrame / MovieBlit, then the old present 004EF610 with "present", or render::Present);
// with "present" their surfaces are not filled and nothing is drawn to the primary.
//
// And the frame player's open (005DBA50, ticket R7). In the switch-over with "cpuwindows" the
// movies' holders (004EF7B0) have no DirectDraw surface at all: nothing decodes into them and
// BinkDDSurfaceType is not asked (OldPathDraws, SurfaceType).
#include "app.h"
#include "bink_video.h"
#include "game_strings.h"
#include "mod_folders.h"
#include "device.h"   // device::SameSurface, device::WithoutDirectDraw
#include "sound.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <string>
#include <vector>
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "ui_cursor.h"

namespace {

// Bink through game.exe's import table (stdcall).
using BinkFn = int(__stdcall*)(BinkHeader*);
using BinkCopyFn = int(__stdcall*)(BinkHeader*, void*, int, int, int, int, unsigned);
using BinkOpenFn = BinkHeader*(__stdcall*)(const char*, unsigned);
using BinkCloseFn = void(__stdcall*)(BinkHeader*);
using BinkSurfaceTypeFn = unsigned(__stdcall*)(void*);
using BinkVolumeFn = void(__stdcall*)(BinkHeader*, int);
using BinkSoundSystemFn = void(__stdcall*)(void*, void*);   // (open function, Miles driver)
using BinkGotoFn = void(__stdcall*)(BinkHeader*, unsigned, int);
template <typename F>
F Import(std::uintptr_t slot) {
    return *reinterpret_cast<F*>(slot);
}
BinkFn BinkWait() { return Import<BinkFn>(0x0073B728); }
BinkFn BinkDoFrame() { return Import<BinkFn>(0x0073B718); }
BinkFn BinkNextFrame() { return Import<BinkFn>(0x0073B724); }
BinkCopyFn BinkCopyToBuffer() { return Import<BinkCopyFn>(0x0073B740); }
BinkOpenFn BinkOpen() { return Import<BinkOpenFn>(0x0073B720); }
BinkCloseFn BinkClose() { return Import<BinkCloseFn>(0x0073B73C); }
BinkSurfaceTypeFn BinkDDSurfaceType() { return Import<BinkSurfaceTypeFn>(0x0073B71C); }
BinkVolumeFn BinkSetVolume() { return Import<BinkVolumeFn>(0x0073B730); }
BinkSoundSystemFn BinkSetSoundSystem() { return Import<BinkSoundSystemFn>(0x0073B734); }
void* BinkOpenMiles() { return Import<void*>(0x0073B738); }
BinkGotoFn BinkGoto() { return Import<BinkGotoFn>(0x0073B714); }
// BinkOpen, as the import slot reaches it: with Rebirth's MoviesPatch (addon's slot) the mod's
// Movies\ copy when there is one. In a hooked Rebirth launch the slot is addon's already.
BinkHeader* OpenBink(const char* name, unsigned flags) {
    if (modfolders::Active(ei::rebirth::Patch::MoviesPatch)) return BinkOpen()(modfolders::MovieFile(name).c_str(), flags);
    return BinkOpen()(name, flags);
}

constexpr int kLock = 25, kUnlock = 32, kBlt = 5;            // IDirectDrawSurface7 slots
constexpr unsigned long kLockFlags = 0x21;                   // DDLOCK_WAIT | DDLOCK_NOSYSLOCK
constexpr unsigned long kBltWait = 0x1000000;                // DDBLT_WAIT
constexpr unsigned long kBltColourFill = 0x1000400;          // DDBLT_WAIT | DDBLT_COLORFILL
constexpr unsigned kBinkSurface32R = 4;                      // BinkCopyToBuffer: R, G, B, X bytes (checked offline)
constexpr std::uint32_t kDescSize = 0x7C;                    // DDSURFACEDESC2

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

// The loading screen's movie (005DA400): globals from 007C551C on.
struct LoadingMovie {
    CDXSurface* holder;         // 007C551C
    RECT screen;                // 007C5520
    std::uint8_t unused_14[0x30 - 0x14];
    char off;                   // 007C554C
    std::uint8_t unused_31[3];
    BinkHeader* bink;           // 007C5550
    unsigned surfaceType;       // 007C5554
};
static_assert(offsetof(LoadingMovie, off) == 0x30);
static_assert(offsetof(LoadingMovie, bink) == 0x34);
static_assert(offsetof(LoadingMovie, surfaceType) == 0x38);

// Game globals.
template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}
CRITICAL_SECTION* DeviceLock() { return &Global<CRITICAL_SECTION>(0x007AEE78); }
CTextureDevice* DeviceObject() { return Global<CTextureDevice*>(0x007AF080); }
void* Cursor() { return &Global<std::uint8_t>(0x007C7BD8); }
void* RendererObject() { return &Global<std::uint8_t>(0x007AF060); }
CSoundManager* Sound() { return Global<CSoundManager*>(0x007AF784); }
int MoviesMode() { return Global<int>(0x0079BCBC); }           // 0 off, 1 at size, 2 stretched to the width
int MovieSound() { return Global<int>(0x0079BCC0); }
int MovieVolume() { return Global<int>(0x0079BCC4); }          // percent
bool VideoMemoryOff() { return Global<std::uint8_t>(0x00784756) != 0; }
const char* GameFolder() { return Global<const char*>(0x007AF460); }   // a CString
const char* LoadingMovieName() { return Global<const char*>(0x00791EE0); }
LoadingMovie& Loading() { return Global<LoadingMovie>(0x007C551C); }
// 00455F10 (fastcall, CL): sets the byte 00784751 (cleared while a timed movie plays).
void SetFlag784751(std::uint8_t value) { Global<std::uint8_t>(0x00784751) = value; }

// 004800E0 (cdecl): a DirectX error report; nonzero to go on.
// 0044C470 (cdecl): an error message; returns a value whose low byte the caller clears.
unsigned __cdecl ErrorMessage(const char* a0) { return game::DebugLog(a0); }

// 005DB9E0 through its address (the verify wrapper sees the call).
__declspec(naked) long __fastcall HolderFillAt(CDXSurface*, void*, RECT*, std::uint32_t) EI_ORIGINAL(0x005DB9E0)

void Blt(CDXSurface* target, RECT* to, CDXSurface* source, RECT* from) {
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(target->surface, kBlt, to, source->surface, from, kBltWait, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}

// The decoded frame as BinkCopyToBuffer's 32-bit R, G, B, X type, for the new renderer.
void CopyFrame(BinkHeader* bink, std::vector<std::uint8_t>* frame) {
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    BinkCopyToBuffer()(bink, frame->data(), width * 4, height, 0, 0, kBinkSurface32R);
}

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The old path draws the movie: outside the switch-over, into the holder's DirectDraw surface (a
// holder made in the switch-over has none, only the new renderer's memory: ticket R7).
bool OldPathDraws(const CDXSurface* holder) { return !render::Presenting() && holder->surface; }

// BinkDDSurfaceType of the holder's surface; without one (made in the switch-over) the type of the
// CPU copy the new renderer gets (nothing decodes into the holder then).
unsigned SurfaceType(const CDXSurface* holder) {
    return holder->surface ? BinkDDSurfaceType()(holder->surface) : kBinkSurface32R;
}

} // namespace

// 005DBD80.
unsigned CBinkVideo::Frame(char toPrimary) {
    if (!MoviesMode()) return 0;   // movies off
    BinkHeader* binkHandle = this->bink;
    if (!binkHandle) return 0;
    if (BinkWait()(binkHandle) == 0) {
        BinkDoFrame()(binkHandle);
        // The new renderer gets the frame from a CPU buffer (BinkCopyToBuffer's 32-bit R, G, B, X
        // type); when it presents by itself the video's DirectDraw surface is not filled at all.
        if (render::Active()) {
            static std::vector<std::uint8_t> frame;
            CopyFrame(binkHandle, &frame);
            render::MovieFrame(this, frame.data(), static_cast<int>(binkHandle->width), static_cast<int>(binkHandle->height));
        }
        if (!OldPathDraws(this->holder)) {
            BinkNextFrame()(binkHandle);
        } else {
            CDXSurface* surfaceHolder = this->holder;
            std::uint32_t desc[0x1F] = {};
            desc[0] = kDescSize;
            gameapi::EnterCriticalSection(DeviceLock());
            long hr = ComCall(surfaceHolder->surface, kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockFlags,
                              static_cast<void*>(nullptr));
            surfaceHolder->result = hr;
            if (hr != 0 && !app::FatalErrorCode(hr, game::Text(0x0078E30C))) {   // "Can't lock surface"
                // The original leaves the device lock held here.
                return ErrorMessage(game::Text(0x0079234C)) & 0xFFFFFF00u;
            }
            // DDSURFACEDESC2: +0x08 height, +0x10 pitch, +0x24 the pixels.
            BinkCopyToBuffer()(binkHandle, reinterpret_cast<void*>(desc[9]), static_cast<int>(desc[4]), static_cast<int>(desc[2]), 0, 0,
                               surfaceType);
            surfaceHolder = this->holder;
            gameapi::LeaveCriticalSection(DeviceLock());
            hr = ComCall(surfaceHolder->surface, kUnlock, static_cast<void*>(nullptr));
            surfaceHolder->result = hr;
            if (hr != 0 && !app::FatalErrorCode(hr, game::Text(0x0078E2F4)))   // "Can't unlock surface"
                return ErrorMessage(game::Text(0x00792324)) & 0xFFFFFF00u;
            BinkNextFrame()(binkHandle);
        }
    }
    const BinkHeader* header = this->bink;
    RECT source{0, 0, static_cast<LONG>(header->width), static_cast<LONG>(header->height)};
    CDXSurface* surfaceHolder = this->holder;
    CDXSurface* target = toPrimary ? surfaceHolder->device->primary_ : surfaceHolder->device->renderTarget_;
    if (!device::SameSurface(target, surfaceHolder)) {
        if (render::Active()) render::MovieBlit(this, ToRender(to));
        if (OldPathDraws(surfaceHolder)) Blt(target, &to, surfaceHolder, &source);
    }
    header = this->bink;
    return static_cast<int>(header->lastFrame) < static_cast<int>(header->frames) ? 1u : 0u;
}
EI_REPLACE_METHOD(0x005DBD80, CBinkVideo::Frame);

// --- The movie players outside the frame (005DB3F0, 005DB9E0, 005DA400) -------------------------

namespace {

// The movie's surface holder (0x20 bytes on the game's heap) for a w x h frame. The original asks
// for a video-memory surface through a DDSURFACEDESC2 (004EFA60) when 00784756 is 0, falling back to
// 004EF7B0 in a catch block when that throws; 00784756 is 1 in the exe and never written by
// address, so the plain off-screen surface of 004EF7B0 (system memory, the screen's format;
// game-owned memory with "cpuwindows") is what is made. The video-memory branch is kept without the
// catch. 00784756 is in fact written by the game start (00456E35, from GetAvailableVidMem): the
// switch-over's stand-in reports 256 MB, which clears it; without DirectDraw (device.h) the
// video-memory surface would be refused and its throw end the game, so the holder is 004EF7B0's
// (only the new renderer's memory, ticket R9a2).
CDXSurface* NewMovieHolder(CTextureDevice* device, int width, int height) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    CDXSurface* holder = nullptr;
    if (!VideoMemoryOff() && !device::WithoutDirectDraw()) {
        std::uint32_t desc[0x1F] = {};
        desc[0] = kDescSize;
        desc[1] = 7;                       // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH
        desc[2] = static_cast<std::uint32_t>(height);
        desc[3] = static_cast<std::uint32_t>(width);
        desc[0x68 / 4] = 0x4040;           // DDSCAPS_VIDEOMEMORY | DDSCAPS_OFFSCREENPLAIN
        if (memory) holder = static_cast<CDXSurface*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc));
    } else if (memory) {
        holder = reinterpret_cast<CDXSurface*>(DXSurfaceConstructOriginal(memory, nullptr, device, width, height));
    }
    if (holder) ++holder->refCount;
    return holder;
}

// A movie frame drawn outside the game's frame becomes a frame of the new renderer: with "present"
// the old present (004EF610, which copies the new frame into the render target first) shows it;
// otherwise the old path draws the movie itself and the sidecar's frame just ends here.
void PresentMovieFrame() {
    if (render::Presenting()) DevicePresentOriginal(DeviceObject());
    else render::Present();
}

int g_movieKey;   // the blocking player's movie in the new renderer

// Locks the holder's surface (DDLOCK_WAIT | DDLOCK_NOSYSLOCK) under the device lock and copies the
// decoded frame into it as `surfaceType`, as both players do. False when the Unlock failed and its
// report says stop. A failed Lock whose report says go on skips the copy and leaves the device lock
// held (as the original).
bool CopyFrameToSurface(CDXSurface* const* holderAt, BinkHeader* bink, unsigned surfaceType) {
    CDXSurface* holder = *holderAt;
    std::uint32_t desc[0x1F] = {};
    desc[0] = kDescSize;
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(holder->surface, kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockFlags,
                      static_cast<void*>(nullptr));
    holder->result = hr;
    if (hr != 0 && !app::FatalErrorCode(hr, game::Text(0x0078E30C))) return true;   // "Can't lock surface"
    // DDSURFACEDESC2: +0x08 height, +0x10 pitch, +0x24 the pixels.
    BinkCopyToBuffer()(bink, reinterpret_cast<void*>(desc[9]), static_cast<int>(desc[4]), static_cast<int>(desc[2]), 0, 0,
                       surfaceType);
    holder = *holderAt;
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = ComCall(holder->surface, kUnlock, static_cast<void*>(nullptr));
    holder->result = hr;
    return hr == 0 || app::FatalErrorCode(hr, game::Text(0x0078E2F4));   // "Can't unlock surface"
}

} // namespace

// 005DB9E0 (thiscall on a surface holder, RET 8): a colour fill of `rect` with `colour`
// (DDBLT_COLORFILL) under the device lock; returns Blt's result.
long CDXSurface::Fill(RECT* rect, std::uint32_t colour) {
    std::uint32_t fx[0x19] = {};   // DDBLTFX
    fx[0] = 100;
    fx[0x50 / 4] = colour;         // dwFillColor
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(surface, kBlt, rect, static_cast<void*>(nullptr), static_cast<void*>(nullptr), kBltColourFill,
                      static_cast<void*>(fx));
    gameapi::LeaveCriticalSection(DeviceLock());
    return hr;
}
EI_REPLACE_METHOD(0x005DB9E0, CDXSurface::Fill);

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
    if (loadingStep) LoadingStepOriginal(Sound(), nullptr, 1);
    if (MovieSound()) BinkSetSoundSystem()(BinkOpenMiles(), Sound()->waveDriver);
    BinkHeader* bink = OpenBink(file, 0);
    if (!bink) {
        game::DebugLog(game::Text(0x007922F8), file);   // "Can't open (or bad format) video file %s."
        return 1;
    }
    BinkSetVolume()(bink, MovieVolume() * 0x7FF6 / 100);
    CursorShowOriginal(Cursor(), nullptr, 0);
    bool presenting = render::Presenting();
    if (clearScreen && !presenting) {
        // The screen black (the primary), then the render target cleared. With "present" nothing is
        // drawn to the primary: the new frames fill the screen themselves (render::Fill below).
        CTextureDevice* device = DeviceObject();
        gameapi::EnterCriticalSection(DeviceLock());
        device->lastLoadResult_ = HolderFillAt(device->primary_, nullptr, &device->screen_, 0);
        gameapi::LeaveCriticalSection(DeviceLock());
        RendererClearOriginal(RendererObject());
    }
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &DeviceObject()->screen_);
    CDXSurface* holder = NewMovieHolder(DeviceObject(), width, height);
    unsigned surfaceType = SurfaceType(holder);
    bool oldPath = OldPathDraws(holder);
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
    if (frameTime) SetFlag784751(0);
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
        if (render::Active()) {
            CopyFrame(bink, &frame);
            render::MovieFrame(&g_movieKey, frame.data(), width, height);
        }
        if (oldPath && !CopyFrameToSurface(&holder, bink, surfaceType)) break;
        RECT source{0, 0, width, height};
        CDXSurface* primary = holder->device->primary_;
        if (!device::SameSurface(primary, holder)) {
            if (render::Active()) {
                if (clearScreen) render::Fill(ToRender(screen), 0);
                render::MovieBlit(&g_movieKey, ToRender(to));
                PresentMovieFrame();
            }
            if (oldPath) Blt(primary, &to, holder, &source);
        }
        if (bink->lastFrame != bink->frames) BinkNextFrame()(bink);
        start = now;
    }
    BinkClose()(bink);
    if (holder) holder->Release();
    SetFlag784751(1);
    static_cast<CCursor*>(Cursor())->SaveBackground(1);
    CursorShowOriginal(Cursor(), nullptr, 1);
    return escaped ? 0 : 1;
}
EI_REPLACE(0x005DB3F0, PlayMovie);

// 005DA400 (fastcall: the loading progress as a frame number; plain RET). The loading screen's movie
// (the file named by 00791EE0, opened once into 007C5550 with its surface holder 007C551C, the
// surface type 007C5554 and the screen rectangle 007C5520) is decoded up to that frame and its
// frame stretched over the screen of the primary surface. Nothing when 007C554C is set.
void __fastcall ShowLoadingMovie(int progress) {
    NetworkTimers();   // 004122D0
    LoadingMovie& movie = Loading();
    if (movie.off) return;
    CursorShowOriginal(Cursor(), nullptr, 0);
    if (!movie.holder) {
        // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
        std::string path = GameFolder();
        path += game::Text(0x00784A4C);    // "Movies\"
        path += LoadingMovieName();  // progres.bik, or the zone's
        if (movie.bink) {
            BinkClose()(movie.bink);
            movie.bink = nullptr;
        }
        movie.bink = OpenBink(path.c_str(), 0);
        if (!movie.bink) {
            game::DebugLog(game::Text(0x00791EE4), path.c_str());   // "Can't open video file \"%s\""
            return;
        }
        RECT screen;
        gameapi::CopyRect(&screen, &DeviceObject()->screen_);
        CDXSurface* holder =
            NewMovieHolder(DeviceObject(), static_cast<int>(movie.bink->width), static_cast<int>(movie.bink->height));
        CDXSurface* old = movie.holder;
        movie.holder = holder;
        if (old) old->Release();
        movie.surfaceType = SurfaceType(holder);
        movie.screen = screen;
        static_cast<CCursor*>(Cursor())->SaveBackground(1);
    }
    BinkHeader* bink = movie.bink;
    int last = static_cast<int>(bink->frames) - 1;
    if (last < progress) progress = last;
    unsigned target = static_cast<unsigned>(progress) + 1;
    static std::vector<std::uint8_t> frame;
    bool decoded = false;
    for (;;) {
        unsigned current = bink->frameNum;
        if (current >= target && current != 1) break;
        BinkDoFrame()(bink);
        if (render::Active()) {
            CopyFrame(bink, &frame);
            decoded = true;
        }
        if (OldPathDraws(movie.holder) && !CopyFrameToSurface(&movie.holder, movie.bink, movie.surfaceType)) return;
        bink = movie.bink;
        BinkGoto()(bink, bink->frameNum + 1, 0);
        bink = movie.bink;
        if (bink->frames == bink->lastFrame) break;
    }
    static_cast<CCursor*>(Cursor())->TakeLock();
    bink = movie.bink;
    CDXSurface* holder = movie.holder;
    RECT source{0, 0, static_cast<LONG>(bink->width), static_cast<LONG>(bink->height)};
    CDXSurface* primary = holder->device->primary_;
    if (!device::SameSurface(primary, holder)) {
        const void* key = &movie;   // the loading movie in the new renderer
        if (render::Active()) {
            if (decoded) render::MovieFrame(key, frame.data(), source.right, source.bottom);
            render::MovieBlit(key, ToRender(movie.screen));
            PresentMovieFrame();
        }
        if (OldPathDraws(holder)) Blt(primary, &movie.screen, holder, &source);
    }
    static_cast<CCursor*>(Cursor())->SaveBackground(1);
    static_cast<CCursor*>(Cursor())->ReleaseLock();
}
EI_REPLACE(0x005DA400, ShowLoadingMovie);

// --- Opening a movie for the frame's player (005DBA50) ----------------------------------------------

// 005DBA50 (thiscall, RET 8; the titles' Create 0062B7D0 starts Titles.bik with it). The movie open
// before is closed (its handle stays in +0x18 when movies are off), the loading screen steps when
// asked, Bink gets the Miles driver when movie sound is on; then the movie is opened, the cursor
// hidden (005E5270 0), the decode surface's holder made (NewMovieHolder: in the switch-over with
// "cpuwindows" one without a DirectDraw surface, so BinkDDSurfaceType is not asked then) and the
// screen rectangle set: movies mode 1 centred at the movie's size, otherwise stretched to the screen's
// width with the height trunc(height / width * screen width), centred vertically (divisions by 2
// round towards zero). The original adds two zero offsets to the centring (kept out).
char CBinkVideo::Open(const char* name, char loadingStep) {
    // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
    if (bink) BinkClose()(bink);
    if (!MoviesMode()) return 0;
    if (loadingStep) LoadingStepOriginal(Sound(), nullptr, 1);
    if (MovieSound()) BinkSetSoundSystem()(BinkOpenMiles(), Sound()->waveDriver);
    bink = OpenBink(name, 0);
    if (!bink) {
        game::DebugLog(game::Text(0x007922F8), name);   // "Can't open (or bad format) video file %s."
        return 0;
    }
    CursorShowOriginal(Cursor(), nullptr, 0);
    int width = static_cast<int>(bink->width), height = static_cast<int>(bink->height);
    RECT screen;
    gameapi::CopyRect(&screen, &DeviceObject()->screen_);
    CDXSurface* made = NewMovieHolder(DeviceObject(), width, height);
    CDXSurface* previous = holder;
    holder = made;
    if (previous) previous->Release();
    surfaceType = SurfaceType(holder);
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
EI_REPLACE_METHOD(0x005DBA50, CBinkVideo::Open);

// --- Closing (A-W14) --------------------------------------------------------------------------------

void CloseLoadingMovie() {
    LoadingMovie& movie = Loading();
    BinkClose()(movie.bink);
    CDXSurface* holder = movie.holder;
    movie.bink = nullptr;
    movie.holder = nullptr;
    if (holder) holder->Release();
    CursorShowOriginal(Cursor(), nullptr, 1);
}
EI_REPLACE(0x005DA3C0, CloseLoadingMovie);

CBinkVideo* CBinkVideo::Construct() {
    holder = nullptr;
    bink = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x005DB340, CBinkVideo::Construct);

void CBinkVideo::Close() {
    if (bink) {
        BinkClose()(bink);
        bink = nullptr;
    }
    CDXSurface* old = holder;
    holder = nullptr;
    if (old) old->Release();
    static_cast<CCursor*>(Cursor())->SaveBackground(1);
    CursorShowOriginal(Cursor(), nullptr, 1);
}
EI_REPLACE_METHOD(0x005DBF70, CBinkVideo::Close);

void CBinkVideo::Destruct() { Close(); }
EI_REPLACE_METHOD(0x005DB350, CBinkVideo::Destruct);
