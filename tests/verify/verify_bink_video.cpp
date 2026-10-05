// Checks for the Bink movie's frame step and the movie players (bink_video.cpp).
#include "verify.h"

#include <windows.h>

#include <cstdint>

unsigned __fastcall BinkVideoFrame(void* self, void*, char toPrimary);

// 005DBD80 decodes and blits: ours only. Checked: the result is 0 or 1 and the video's handle and
// surface are unchanged.
static void* s_binkFrame;
static unsigned __fastcall BinkVideoFrameVerify(void* self, void*, char toPrimary) {
    void* holder = *static_cast<void**>(self);
    void* bink = static_cast<void**>(self)[6];
    unsigned result = BinkVideoFrame(self, nullptr, toPrimary);
    bool ok = (result & 0xFF) <= 1 && *static_cast<void**>(self) == holder && static_cast<void**>(self)[6] == bink;
    ei::verify::Check("CBinkVideo::Frame", true, ok, "result %08X", result);
    return result;
}
EI_VERIFY(0x005DBD80, BinkVideoFrameVerify, 0, s_binkFrame);

// --- The movie players outside the frame (ticket R6) ----------------------------------------------

char __fastcall PlayMovie(const char* name, unsigned frameTime, char keysEnd, char spaceSkips, char clearScreen,
                          char loadingStep, int unused);
long __fastcall HolderFill(std::uint8_t* holder, void*, RECT* rect, std::uint32_t colour);
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
using HolderFillFn = long(__fastcall*)(std::uint8_t*, void*, RECT*, std::uint32_t);
static HolderFillFn s_holderFill;
static long __fastcall HolderFillVerify(std::uint8_t* holder, void*, RECT* rect, std::uint32_t colour) {
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
    auto* bink = *reinterpret_cast<std::uint8_t**>(0x007C5550);
    if (!bink) return;
    void* holder = *reinterpret_cast<void**>(0x007C551C);
    int frames = *reinterpret_cast<int*>(bink + 8);
    unsigned frame = *reinterpret_cast<unsigned*>(bink + 0x0C);
    int want = progress < frames - 1 ? progress : frames - 1;
    bool ok = holder != nullptr && (frame >= static_cast<unsigned>(want) + 1 || frames == *reinterpret_cast<int*>(bink + 0x10));
    ei::verify::Check("ShowLoadingMovie", true, ok, "holder %p, frame %u of %d, progress %d", holder, frame, frames, progress);
}
EI_VERIFY(0x005DA400, ShowLoadingMovieVerify, 0, s_loadingMovie);
