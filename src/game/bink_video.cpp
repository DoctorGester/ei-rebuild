// A Bink movie's frame step (CBinkVideo, 005DBD80, thiscall, RET 4; called once per frame by the
// movie screen's draw 0062BD80, inside the game's frame). When the movie's next frame is due
// (BinkWait) it is decoded (BinkDoFrame) into the video's own surface (+0x00 its holder, made by
// 005DBA50 in the format +0x04 BinkDDSurfaceType gave) through a Lock / BinkCopyToBuffer / Unlock,
// and the movie steps on (BinkNextFrame). Every call then blits the whole movie frame into the
// rectangle +0x08 (letterboxed by 005DBA50) of the render target, or of the primary surface when
// `toPrimary`. Returns whether frames are left. The new renderer (src/render) gets the same blit as
// part of its frame, from a CPU copy of the decoded frame; when it presents by itself the video's
// surface is not filled and the old blit is skipped. See docs/notes/renderer.md.
//
// Also the two movie players that draw outside the game's frame, straight to the primary surface
// (ticket R6): the blocking player 005DB3F0 (intro, titles, the script's PlayMovie; its message loop
// at 005DB788) with its screen fill 005DB9E0, and the loading screen's movie 005DA400 (progres*.bik,
// one step per call). With the new renderer they hand each frame over as a frame of its own
// (render::MovieFrame / MovieBlit, then the old present 004EF610 with "present", or render::Present);
// with "present" their surfaces are not filled and nothing is drawn to the primary.
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

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

// Bink through game.exe's import table (stdcall).
using BinkFn = int(__stdcall*)(void*);
using BinkCopyFn = int(__stdcall*)(void*, void*, int, int, int, int, unsigned);
BinkFn BinkWait() { return *reinterpret_cast<BinkFn*>(0x0073B728); }
BinkFn BinkDoFrame() { return *reinterpret_cast<BinkFn*>(0x0073B718); }
BinkFn BinkNextFrame() { return *reinterpret_cast<BinkFn*>(0x0073B724); }
BinkCopyFn BinkCopyToBuffer() { return *reinterpret_cast<BinkCopyFn*>(0x0073B740); }
using BinkOpenFn = void*(__stdcall*)(const char*, unsigned);
using BinkCloseFn = void(__stdcall*)(void*);
using BinkSurfaceTypeFn = unsigned(__stdcall*)(void*);
using BinkSetFn = void(__stdcall*)(void*, int);
using BinkGotoFn = void(__stdcall*)(void*, unsigned, int);
BinkOpenFn BinkOpen() { return *reinterpret_cast<BinkOpenFn*>(0x0073B720); }
BinkCloseFn BinkClose() { return *reinterpret_cast<BinkCloseFn*>(0x0073B73C); }
BinkSurfaceTypeFn BinkDDSurfaceType() { return *reinterpret_cast<BinkSurfaceTypeFn*>(0x0073B71C); }
BinkSetFn BinkSetVolume() { return *reinterpret_cast<BinkSetFn*>(0x0073B730); }
BinkSetFn BinkSetSoundSystem() { return *reinterpret_cast<BinkSetFn*>(0x0073B734); }   // (open function, Miles driver)
BinkGotoFn BinkGoto() { return *reinterpret_cast<BinkGotoFn*>(0x0073B714); }

// The Bink handle's first fields: frame width, height, frame count +0x08, current frame +0x10.
struct BinkHeader {
    std::uint32_t width, height, frames, unknown_0C, frame;
};

constexpr int kLock = 25, kUnlock = 32, kBlt = 5;            // IDirectDrawSurface7 slots
constexpr unsigned long kLockFlags = 0x21;                   // DDLOCK_WAIT | DDLOCK_NOSYSLOCK
constexpr unsigned long kBltWait = 0x1000000;                // DDBLT_WAIT
constexpr unsigned kBinkSurface32R = 4;                      // BinkCopyToBuffer: R, G, B, X bytes (checked offline)

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// 004800E0 (cdecl): a DirectX error report; nonzero to go on.
// 0044C470 (cdecl): an error message; returns a value whose low byte the caller clears.
unsigned __cdecl ErrorMessage(const char* a0) { return DebugMessageOriginal(a0); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

} // namespace

// 005DBD80.
unsigned __fastcall BinkVideoFrame(void* self, void*, char toPrimary) {
    if (!*reinterpret_cast<int*>(0x0079BCBC)) return 0;   // movies off
    void* bink = At<void*>(self, 0x18);
    if (!bink) return 0;
    if (BinkWait()(bink) == 0) {
        BinkDoFrame()(bink);
        // The new renderer gets the frame from a CPU buffer (BinkCopyToBuffer's 32-bit R, G, B, X
        // type); when it presents by itself the video's DirectDraw surface is not filled at all.
        if (render::Active()) {
            const auto* size = static_cast<const BinkHeader*>(bink);
            int width = static_cast<int>(size->width), height = static_cast<int>(size->height);
            static std::vector<std::uint8_t> frame;
            frame.resize(static_cast<std::size_t>(width) * height * 4);
            BinkCopyToBuffer()(bink, frame.data(), width * 4, height, 0, 0, kBinkSurface32R);
            render::MovieFrame(self, frame.data(), width, height);
        }
        if (render::Presenting()) {
            BinkNextFrame()(bink);
        } else {
            std::uint8_t* holder = At<std::uint8_t*>(self, 0);
            std::uint32_t desc[0x1F] = {};
            desc[0] = 0x7C;
            gameapi::EnterCriticalSection(DeviceLock());
            long hr = ComCall(At<void*>(holder, 0x10), kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockFlags,
                              static_cast<void*>(nullptr));
            At<long>(holder, 8) = hr;
            if (hr != 0 && !FatalDirectX(hr, Text(0x0078E30C))) {   // "Can't lock surface"
                // The original leaves the device lock held here.
                return ErrorMessage(Text(0x0079234C)) & 0xFFFFFF00u;
            }
            // DDSURFACEDESC2: +0x08 height, +0x10 pitch, +0x24 the pixels.
            BinkCopyToBuffer()(bink, reinterpret_cast<void*>(desc[9]), static_cast<int>(desc[4]), static_cast<int>(desc[2]), 0, 0,
                               At<std::uint32_t>(self, 4));
            holder = At<std::uint8_t*>(self, 0);
            gameapi::LeaveCriticalSection(DeviceLock());
            hr = ComCall(At<void*>(holder, 0x10), kUnlock, static_cast<void*>(nullptr));
            At<long>(holder, 8) = hr;
            if (hr != 0 && !FatalDirectX(hr, Text(0x0078E2F4)))   // "Can't unlock surface"
                return ErrorMessage(Text(0x00792324)) & 0xFFFFFF00u;
            BinkNextFrame()(bink);
        }
    }
    const auto* header = static_cast<const BinkHeader*>(At<void*>(self, 0x18));
    RECT source{0, 0, static_cast<LONG>(header->width), static_cast<LONG>(header->height)};
    std::uint8_t* holder = At<std::uint8_t*>(self, 0);
    RECT* destination = &At<RECT>(self, 8);
    std::uint8_t* target = At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), toPrimary ? 0x14 : 0x18);
    if (At<void*>(target, 0x10) != At<void*>(holder, 0x10)) {
        if (render::Active())
            render::MovieBlit(self, render::Rect{destination->left, destination->top, destination->right, destination->bottom});
        if (!render::Presenting()) {
            gameapi::EnterCriticalSection(DeviceLock());
            ComCall(At<void*>(target, 0x10), kBlt, destination, At<void*>(holder, 0x10), &source, kBltWait,
                    static_cast<void*>(nullptr));
            gameapi::LeaveCriticalSection(DeviceLock());
        }
    }
    header = static_cast<const BinkHeader*>(At<void*>(self, 0x18));
    return static_cast<int>(header->frame) < static_cast<int>(header->frames) ? 1u : 0u;
}
EI_REPLACE(0x005DBD80, BinkVideoFrame);

// --- The movie players outside the frame (005DB3F0, 005DB9E0, 005DA400) -------------------------

namespace {

template <typename T>
T& Global(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

constexpr std::uint32_t kDescSize = 0x7C;                    // DDSURFACEDESC2
constexpr unsigned long kBltColourFill = 0x1000400;          // DDBLT_WAIT | DDBLT_COLORFILL
std::uint8_t* DeviceObject() { return Global<std::uint8_t*>(0x007AF080); }
void* Cursor() { return reinterpret_cast<void*>(0x007C7BD8); }
void* RendererObject() { return reinterpret_cast<void*>(0x007AF060); }

// 005DB9E0 through its address (the verify wrapper sees the call).
__declspec(naked) long __fastcall HolderFillAt(std::uint8_t*, void*, RECT*, std::uint32_t) EI_ORIGINAL(0x005DB9E0)

// A surface holder (CDXSurface): +0x04 its reference count; released as 004E6BA0 does (the
// destructor through vtable slot 0 with 1 when the count reaches 0).
void ReleaseHolder(std::uint8_t* holder) {
    if (--At<int>(holder, 4) != 0) return;
    using Destroy = void(__fastcall*)(void*, void*, int);
    (*reinterpret_cast<Destroy**>(holder))[0](holder, nullptr, 1);
}

// The movie's surface holder (0x20 bytes on the game's heap) for a w x h frame. The original asks
// for a video-memory surface through a DDSURFACEDESC2 (004EFA60) when 00784756 is 0, falling back to
// 004EF7B0 in a catch block when that throws; 00784756 is 1 in the exe and never written by
// address, so the plain off-screen surface of 004EF7B0 (system memory, the screen's format;
// game-owned memory with "cpuwindows") is what is made. The video-memory branch is kept without the
// catch.
std::uint8_t* NewMovieHolder(std::uint8_t* device, int width, int height) {
    void* memory = gamecrt::operator_new(0x20);
    std::uint8_t* holder = nullptr;
    if (!Global<std::uint8_t>(0x00784756)) {
        std::uint32_t desc[0x1F] = {};
        desc[0] = kDescSize;
        desc[1] = 7;                       // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH
        desc[2] = static_cast<std::uint32_t>(height);
        desc[3] = static_cast<std::uint32_t>(width);
        desc[0x68 / 4] = 0x4040;           // DDSCAPS_VIDEOMEMORY | DDSCAPS_OFFSCREENPLAIN
        if (memory) holder = static_cast<std::uint8_t*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc));
    } else if (memory) {
        holder = reinterpret_cast<std::uint8_t*>(DXSurfaceConstructOriginal(memory, nullptr, device, width, height));
    }
    if (holder) ++At<int>(holder, 4);
    return holder;
}

// A movie frame drawn outside the game's frame becomes a frame of the new renderer: with "present"
// the old present (004EF610, which copies the new frame into the render target first) shows it;
// otherwise the old path draws the movie itself and the sidecar's frame just ends here.
void PresentMovieFrame() {
    if (render::Presenting()) DevicePresentOriginal(DeviceObject());
    else render::Present();
}

// The decoded frame as BinkCopyToBuffer's 32-bit R, G, B, X type, for the new renderer.
void CopyFrame(void* bink, std::vector<std::uint8_t>* frame) {
    const auto* size = static_cast<const BinkHeader*>(bink);
    int width = static_cast<int>(size->width), height = static_cast<int>(size->height);
    frame->resize(static_cast<std::size_t>(width) * height * 4);
    BinkCopyToBuffer()(bink, frame->data(), width * 4, height, 0, 0, kBinkSurface32R);
}

// 00455F10 (fastcall, CL): sets the byte 00784751 (cleared while a timed movie plays).
void SetFlag784751(std::uint8_t value) { Global<std::uint8_t>(0x00784751) = value; }

int g_movieKey;   // the blocking player's movie in the new renderer
const void* const kLoadingMovieKey = reinterpret_cast<const void*>(0x007C551C);

// Locks the holder's surface (DDLOCK_WAIT | DDLOCK_NOSYSLOCK) under the device lock and copies the
// decoded frame into it as `surfaceType`, as both players do. False when the Unlock failed and its
// report says stop. A failed Lock whose report says go on skips the copy and leaves the device lock
// held (as the original).
bool CopyFrameToSurface(std::uint8_t* const* holderAt, void* bink, unsigned surfaceType) {
    std::uint8_t* holder = *holderAt;
    std::uint32_t desc[0x1F] = {};
    desc[0] = kDescSize;
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(At<void*>(holder, 0x10), kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockFlags,
                      static_cast<void*>(nullptr));
    At<long>(holder, 8) = hr;
    if (hr != 0 && !FatalDirectX(hr, Text(0x0078E30C))) return true;   // "Can't lock surface"
    // DDSURFACEDESC2: +0x08 height, +0x10 pitch, +0x24 the pixels.
    BinkCopyToBuffer()(bink, reinterpret_cast<void*>(desc[9]), static_cast<int>(desc[4]), static_cast<int>(desc[2]), 0, 0,
                       surfaceType);
    holder = *holderAt;
    gameapi::LeaveCriticalSection(DeviceLock());
    hr = ComCall(At<void*>(holder, 0x10), kUnlock, static_cast<void*>(nullptr));
    At<long>(holder, 8) = hr;
    return hr == 0 || FatalDirectX(hr, Text(0x0078E2F4));   // "Can't unlock surface"
}

} // namespace

// 005DB9E0 (thiscall on a surface holder, RET 8): a colour fill of `rect` with `colour`
// (DDBLT_COLORFILL) under the device lock; returns Blt's result.
long __fastcall HolderFill(std::uint8_t* holder, void*, RECT* rect, std::uint32_t colour) {
    std::uint32_t fx[0x19] = {};   // DDBLTFX
    fx[0] = 100;
    fx[0x50 / 4] = colour;         // dwFillColor
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(At<void*>(holder, 0x10), kBlt, rect, static_cast<void*>(nullptr), static_cast<void*>(nullptr),
                      kBltColourFill, static_cast<void*>(fx));
    gameapi::LeaveCriticalSection(DeviceLock());
    return hr;
}
EI_REPLACE(0x005DB9E0, HolderFill);

// 005DB3F0 (fastcall: the file name, the time per frame in ms or 0 for the movie's own rate; then on
// the stack: Esc / Space end it, Space jumps to the next frame (when they do not end it), fill the
// screen black first, a loading screen step first, and a fifth argument never read; RET 0x14).
// Plays the movie in a message loop of its own, each frame decoded into the movie's surface and
// blitted into its letterboxed rectangle of the primary surface. Returns 0 when Esc ended it.
char __fastcall PlayMovie(const char* name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                          char loadingStep, int /*unused*/) {
    // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
    if (!Global<int>(0x0079BCBC)) return 1;   // movies off
    const char* file = name ? name : "";      // the original copies it into a CString
    if (loadingStep) LoadingStepOriginal(Global<void*>(0x007AF784), nullptr, 1);
    if (Global<int>(0x0079BCC0))
        BinkSetSoundSystem()(Global<void*>(0x0073B738), At<int>(Global<void*>(0x007AF784), 0x1C));   // BinkOpenMiles
    void* bink = BinkOpen()(file, 0);
    if (!bink) {
        DebugMessageOriginal(Text(0x007922F8), file);   // "Can't open (or bad format) video file %s."
        return 1;
    }
    BinkSetVolume()(bink, Global<int>(0x0079BCC4) * 0x7FF6 / 100);
    CursorShowOriginal(Cursor(), nullptr, 0);
    bool presenting = render::Presenting();
    if (clearScreen && !presenting) {
        // The screen black (the primary), then the render target cleared. With "present" nothing is
        // drawn to the primary: the new frames fill the screen themselves (render::Fill below).
        std::uint8_t* device = DeviceObject();
        gameapi::EnterCriticalSection(DeviceLock());
        At<long>(device, 8) = HolderFillAt(At<std::uint8_t*>(device, 0x14), nullptr, &At<RECT>(device, 0x24), 0);
        gameapi::LeaveCriticalSection(DeviceLock());
        RendererClearOriginal(RendererObject());
    }
    const auto* header = static_cast<const BinkHeader*>(bink);
    int width = static_cast<int>(header->width), height = static_cast<int>(header->height);
    RECT screen;
    gameapi::CopyRect(&screen, &At<RECT>(DeviceObject(), 0x24));
    std::uint8_t* holder = NewMovieHolder(DeviceObject(), width, height);
    unsigned surfaceType = BinkDDSurfaceType()(At<void*>(holder, 0x10));
    // The frame's rectangle: mode 2 stretched to the screen's width (aspect kept), otherwise
    // centred at its size; both centred vertically (divisions round towards zero).
    RECT to;
    if (Global<int>(0x0079BCBC) == 2) {
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
        if (static_cast<int>(header->frame) >= static_cast<int>(header->frames)) {
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
        if (!presenting && !CopyFrameToSurface(&holder, bink, surfaceType)) break;
        RECT source{0, 0, width, height};
        std::uint8_t* primary = At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x14);
        if (At<void*>(primary, 0x10) != At<void*>(holder, 0x10)) {
            if (render::Active()) {
                if (clearScreen) render::Fill(render::Rect{screen.left, screen.top, screen.right, screen.bottom}, 0);
                render::MovieBlit(&g_movieKey, render::Rect{to.left, to.top, to.right, to.bottom});
                PresentMovieFrame();
            }
            if (!presenting) {
                gameapi::EnterCriticalSection(DeviceLock());
                ComCall(At<void*>(primary, 0x10), kBlt, &to, At<void*>(holder, 0x10), &source, kBltWait,
                        static_cast<void*>(nullptr));
                gameapi::LeaveCriticalSection(DeviceLock());
            }
        }
        if (header->frame != header->frames) BinkNextFrame()(bink);
        start = now;
    }
    BinkClose()(bink);
    if (holder) ReleaseHolder(holder);
    SetFlag784751(1);
    CursorSaveBackgroundOriginal(Cursor(), nullptr, 1);
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
    if (Global<char>(0x007C554C)) return;
    CursorShowOriginal(Cursor(), nullptr, 0);
    bool presenting = render::Presenting();
    if (!Global<std::uint8_t*>(0x007C551C)) {
        // (00694120 on the texture manager 007AF744 when there is one: an empty function.)
        std::string path = Global<const char*>(0x007AF460);   // the game folder (a CString)
        path += Text(0x00784A4C);                              // "Movies\"
        path += Global<const char*>(0x00791EE0);               // progres.bik, or the zone's
        if (Global<void*>(0x007C5550)) {
            BinkClose()(Global<void*>(0x007C5550));
            Global<void*>(0x007C5550) = nullptr;
        }
        Global<void*>(0x007C5550) = BinkOpen()(path.c_str(), 0);
        if (!Global<void*>(0x007C5550)) {
            DebugMessageOriginal(Text(0x00791EE4), path.c_str());   // "Can't open video file \"%s\""
            return;
        }
        RECT screen;
        gameapi::CopyRect(&screen, &At<RECT>(DeviceObject(), 0x24));
        const auto* header = Global<const BinkHeader*>(0x007C5550);
        std::uint8_t* holder = NewMovieHolder(DeviceObject(), static_cast<int>(header->width), static_cast<int>(header->height));
        std::uint8_t* old = Global<std::uint8_t*>(0x007C551C);
        Global<std::uint8_t*>(0x007C551C) = holder;
        if (old) ReleaseHolder(old);
        Global<unsigned>(0x007C5554) = BinkDDSurfaceType()(At<void*>(holder, 0x10));
        Global<RECT>(0x007C5520) = screen;
        CursorSaveBackgroundOriginal(Cursor(), nullptr, 1);
    }
    auto* bink = Global<std::uint8_t*>(0x007C5550);
    // Bink's handle: +0x08 the frame count, +0x0C the frame number, +0x10 the last frame number.
    int last = At<int>(bink, 8) - 1;
    if (last < progress) progress = last;
    unsigned target = static_cast<unsigned>(progress) + 1;
    static std::vector<std::uint8_t> frame;
    bool decoded = false;
    for (;;) {
        unsigned current = At<unsigned>(bink, 0x0C);
        if (current >= target && current != 1) break;
        BinkDoFrame()(bink);
        if (render::Active()) {
            CopyFrame(bink, &frame);
            decoded = true;
        }
        if (!presenting &&
            !CopyFrameToSurface(&Global<std::uint8_t*>(0x007C551C), bink, Global<unsigned>(0x007C5554)))
            return;
        bink = Global<std::uint8_t*>(0x007C5550);
        BinkGoto()(bink, At<unsigned>(bink, 0x0C) + 1, 0);
        bink = Global<std::uint8_t*>(0x007C5550);
        if (At<int>(bink, 8) == At<int>(bink, 0x10)) break;
    }
    CursorLockOriginal(Cursor());
    bink = Global<std::uint8_t*>(0x007C5550);
    std::uint8_t* holder = Global<std::uint8_t*>(0x007C551C);
    RECT source{0, 0, At<LONG>(bink, 0), At<LONG>(bink, 4)};
    std::uint8_t* primary = At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x14);
    if (At<void*>(primary, 0x10) != At<void*>(holder, 0x10)) {
        if (render::Active()) {
            if (decoded) render::MovieFrame(kLoadingMovieKey, frame.data(), source.right, source.bottom);
            const RECT& to = Global<RECT>(0x007C5520);
            render::MovieBlit(kLoadingMovieKey, render::Rect{to.left, to.top, to.right, to.bottom});
            PresentMovieFrame();
        }
        if (!presenting) {
            gameapi::EnterCriticalSection(DeviceLock());
            ComCall(At<void*>(primary, 0x10), kBlt, &Global<RECT>(0x007C5520), At<void*>(holder, 0x10), &source, kBltWait,
                    static_cast<void*>(nullptr));
            gameapi::LeaveCriticalSection(DeviceLock());
        }
    }
    CursorSaveBackgroundOriginal(Cursor(), nullptr, 1);
    CursorUnlockOriginal(Cursor());
}
EI_REPLACE(0x005DA400, ShowLoadingMovie);
