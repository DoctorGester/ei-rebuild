// Checks for the Bink movie's frame step and the movie players (bink_video.cpp).
#include "verify.h"

#include "../../src/game/bink_video.h"
#include "../../src/game/texture.h"
#include "../../src/render/render.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

unsigned __fastcall BinkVideoFrame(CBinkVideo* self, void*, char toPrimary);

// 005DBD80 decodes and blits: ours only. Checked: the result is 0 or 1 and the video's handle and
// surface are unchanged.
static void* s_binkFrame;
static unsigned __fastcall BinkVideoFrameVerify(CBinkVideo* self, void*, char toPrimary) {
    CDXSurface* holder = self->holder;
    BinkHeader* bink = self->bink;
    unsigned result = BinkVideoFrame(self, nullptr, toPrimary);
    bool ok = (result & 0xFF) <= 1 && self->holder == holder && self->bink == bink;
    ei::verify::Check("CBinkVideo::Frame", true, ok, "result %08X", result);
    return result;
}
EI_VERIFY(0x005DBD80, BinkVideoFrameVerify, 0, s_binkFrame);

// --- The movie players outside the frame (ticket R6) ----------------------------------------------

char __fastcall PlayMovie(const char* name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                          char loadingStep, int unused);
long __fastcall HolderFill(CDXSurface* holder, void*, RECT* rect, std::uint32_t colour);
void __fastcall ShowLoadingMovie(int progress);

// 005DB3F0 plays a whole movie in its own message loop: ours only. Checked: the result is 0 or 1,
// 1 with movies off (0079BCBC 0), and the flag 00784751 set again afterwards (a timed movie clears
// it while it plays). Scenarios render_movie / render_movie_present (the script's PlayMovie); the
// harness empties the startup and titles movie lists.
static void* s_playMovie;
static char __fastcall PlayMovieVerify(const char* name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                                       char loadingStep, int unused) {
    bool moviesOn = *reinterpret_cast<int*>(0x0079BCBC) != 0;
    char result = PlayMovie(name, frameTime, keysEnd, spaceSkips, clearScreen, loadingStep, unused);
    unsigned flag = *reinterpret_cast<unsigned char*>(0x00784751);
    bool ok = (result == 0 || result == 1) && (moviesOn || result == 1) && (!moviesOn || frameTime == 0 || flag == 1);
    ei::verify::Check("PlayMovie", true, ok, "result %d, movies %d, flag %u", result, moviesOn ? 1 : 0, flag);
    return result;
}
EI_VERIFY(0x005DB3F0, PlayMovieVerify, 0, s_playMovie);

// 005DB9E0 fills a rectangle of a surface: the original (6 stolen bytes: SUB ESP,0x64; PUSH ESI;
// MOV ESI,ECX), then ours (the same fill again); Blt's results compared. Reached from 005DB3F0 with
// its clear-screen flag outside the switch-over (render_movie).
using HolderFillFn = long(__fastcall*)(CDXSurface*, void*, RECT*, std::uint32_t);
static HolderFillFn s_holderFill;
static long __fastcall HolderFillVerify(CDXSurface* holder, void*, RECT* rect, std::uint32_t colour) {
    long expected = s_holderFill(holder, nullptr, rect, colour);
    long result = HolderFill(holder, nullptr, rect, colour);
    ei::verify::Check("HolderFill", expected, result, "result %08lX, original %08lX", static_cast<unsigned long>(result),
                      static_cast<unsigned long>(expected));
    return result;
}
EI_VERIFY(0x005DB9E0, HolderFillVerify, 6, s_holderFill);

// 005DA400 decodes and blits, once per loading step: ours only. Checked: when it went past its
// early returns (007C554C clear, the movie open), the movie has its surface holder (007C551C) and
// its frame number is past the requested frame, or the movie is at its end. Every scenario that
// loads a zone (newgame).
static void* s_loadingMovie;
static void __fastcall ShowLoadingMovieVerify(int progress) {
    ShowLoadingMovie(progress);
    if (*reinterpret_cast<char*>(0x007C554C)) return;
    const BinkHeader* bink = *reinterpret_cast<BinkHeader**>(0x007C5550);
    if (!bink) return;
    void* holder = *reinterpret_cast<void**>(0x007C551C);
    int frames = static_cast<int>(bink->frames);
    unsigned frame = bink->frameNum;
    int want = progress < frames - 1 ? progress : frames - 1;
    bool ok = holder != nullptr && (frame >= static_cast<unsigned>(want) + 1 || bink->frames == bink->lastFrame);
    ei::verify::Check("ShowLoadingMovie", true, ok, "holder %p, frame %u of %d, progress %d", holder, frame, frames, progress);
}
EI_VERIFY(0x005DA400, ShowLoadingMovieVerify, 0, s_loadingMovie);

// 005DBA50 opens a movie (Bink, a surface holder): ours only (ticket R7). Checked: the result's low
// byte 0 or 1, 0 with movies off (0079BCBC 0); when it plays, the Bink handle and a holder of the
// movie's size with pixels (a DirectDraw surface; in the switch-over with "cpuwindows" only the new
// renderer's memory, and no surface), and the screen rectangle: mode 1 the movie's size, otherwise the
// screen's width (device object +0x24). Reached by the titles (ui_main_menu region 4,
// render_titles_present).
static void* s_binkOpen;
static char __fastcall BinkVideoOpenVerify(CBinkVideo* self, void*, const char* name, char loadingStep) {
    int mode = *reinterpret_cast<int*>(0x0079BCBC);
    char result = self->Open(name, loadingStep);
    bool ok = result == 0 || result == 1;
    if (mode == 0) ok = ok && result == 0;
    const CDXSurface* holder = self->holder;
    const BinkHeader* bink = self->bink;
    if (result == 1) {
        const auto* device = *reinterpret_cast<CTextureDevice* const*>(0x007AF080);
        bool memoryOnly = render::Presenting() && render::CpuWindows();
        int width = self->to.right - self->to.left, height = self->to.bottom - self->to.top;
        ok = ok && bink && holder && holder->width == static_cast<int>(bink->width) &&
             holder->height == static_cast<int>(bink->height) &&
             (holder->surface ? !(memoryOnly && render::SurfaceMemoryOf(holder)) : render::SurfaceMemoryOf(holder) != nullptr) &&
             (mode == 1 ? width == static_cast<int>(bink->width) && height == static_cast<int>(bink->height)
                        : self->to.left == 0 && width == device->screen_.right - device->screen_.left);
    }
    ei::verify::Check("CBinkVideo::Open", true, ok, "%s: result %d, mode %d, holder %p (surface %p), rectangle %ld,%ld-%ld,%ld",
                      name ? name : "", result, mode, static_cast<const void*>(holder), holder ? holder->surface : nullptr,
                      static_cast<long>(self->to.left), static_cast<long>(self->to.top), static_cast<long>(self->to.right),
                      static_cast<long>(self->to.bottom));
    return result;
}
EI_VERIFY(0x005DBA50, BinkVideoOpenVerify, 0, s_binkOpen);

// --- Closing (A-W14) --------------------------------------------------------------------------------
// 005DA3C0, 005DBF70 and 005DB350 close a Bink handle and release a surface: ours only, checked by
// what they leave (no handle, no holder). 005DB340 (MOV EAX,ECX; MOV DWORD [EAX],0: 8 bytes) writes
// two fields: the original on a copy of the object, ours on the object.
// Reached: CloseLoadingMovie at the end of every zone load (the originals 00456440 / 00567AB0, both
// patched by Rebirth, call it): newgame. Close when the titles end (CreditsDraw, 0062BD80:
// ui_main_menu region 4, render_titles_present). Construct / Destruct with the hero screen (screen
// 8: Screen8Construct / Screen8Destroy) and the main menu's click (00632D70): the menu scenarios
// that open the new-game screen.
static void* s_closeUnused;
static void CloseLoadingMovieVerify() {
    CloseLoadingMovie();
    bool ok = *reinterpret_cast<BinkHeader**>(0x007C5550) == nullptr && *reinterpret_cast<void**>(0x007C551C) == nullptr;
    ei::verify::Check("CloseLoadingMovie", true, ok, "handle and holder cleared");
}
EI_VERIFY(0x005DA3C0, CloseLoadingMovieVerify, 0, s_closeUnused);

static void __fastcall BinkVideoCloseVerify(CBinkVideo* self) {
    self->Close();
    ei::verify::Check("CBinkVideo::Close", true, self->bink == nullptr && self->holder == nullptr, "%p", static_cast<void*>(self));
}
EI_VERIFY(0x005DBF70, BinkVideoCloseVerify, 0, s_closeUnused);

static void __fastcall BinkVideoDestructVerify(CBinkVideo* self) {
    self->Destruct();
    ei::verify::Check("CBinkVideo::Destruct", true, self->bink == nullptr && self->holder == nullptr, "%p",
                      static_cast<void*>(self));
}
EI_VERIFY(0x005DB350, BinkVideoDestructVerify, 0, s_closeUnused);

static CBinkVideo*(__fastcall* s_binkConstruct)(CBinkVideo*);
static CBinkVideo* __fastcall BinkVideoConstructVerify(CBinkVideo* self) {
    CBinkVideo copy;
    std::memcpy(&copy, self, sizeof(copy));
    CBinkVideo* expected = s_binkConstruct(&copy);
    CBinkVideo* result = self->Construct();
    ei::verify::Check("CBinkVideo::Construct", true,
                      expected == &copy && result == self && std::memcmp(&copy, self, sizeof(copy)) == 0, "%p",
                      static_cast<void*>(self));
    return result;
}
EI_VERIFY(0x005DB340, BinkVideoConstructVerify, 8, s_binkConstruct);
