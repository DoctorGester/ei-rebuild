// Checks for the Bink movie's frame step and the movie players (bink_video.cpp).
#include "verify.h"

#include "../../src/game/bink_video.h"
#include "../../src/game/texture.h"

#include <windows.h>

#include <cstdint>

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
