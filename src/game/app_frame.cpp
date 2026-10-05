// WinMain's frame step (00458010: the interface and the game ticked, the frame drawn and flipped,
// a screenshot per frame while recording a movie) and the intro movies (00458270: the list read
// from an .ini file, each played, the screen cleared through the primary surface's Blt). Every
// callee is reached through its original address. See docs/notes/unmapped_410_470.md
// ("Ninth round").
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>

namespace app {

namespace {

template <typename T>
T& Global(std::uintptr_t address) { return *reinterpret_cast<T*>(address); }
template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

void* Interface() { return reinterpret_cast<void*>(0x007C7D50); }
void* Game() { return reinterpret_cast<void*>(0x007AF060); }
void* Screen() { return reinterpret_cast<void*>(0x007C7BD8); }
void* Renderer() { return Global<void*>(0x007AF080); }
bool Running() { return Global<std::uint8_t>(0x0079BC5C) != 0 && Global<std::uint8_t>(0x00784750) != 0; }
CRITICAL_SECTION* RenderLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

__declspec(naked) int __fastcall InterfaceActiveWindow(void*) EI_ORIGINAL(0x005F7590)
__declspec(naked) int __fastcall GameStep(void*) EI_ORIGINAL(0x00503F40)
__declspec(naked) void __fastcall InterfaceUpdate(void*) EI_ORIGINAL(0x005F5220)
__declspec(naked) void __fastcall GameDraw(void*) EI_ORIGINAL(0x005040A0)
__declspec(naked) void __fastcall InterfaceDrawCursor(void*) EI_ORIGINAL(0x005F5320)
// The software cursor's lock (005E65F0) is called through the target of the original CALL at
// 004580D7 (the original body stays in place behind the hook's jump): the harness's frame capture
// redirects that call (tests/verify/harness_capture.cpp) to read the finished frame before the
// cursor is blitted and the frame flipped, and keeps working with this rewrite. Without the
// harness the target is 005E65F0.
using CursorLock = void(__fastcall*)(void*);
CursorLock CursorLockAtCallSite() {
    constexpr std::uintptr_t kCall = 0x004580D7;
    const auto* code = reinterpret_cast<const unsigned char*>(kCall);
    std::int32_t rel;
    std::memcpy(&rel, code + 1, 4);
    if (code[0] != 0xE8) return reinterpret_cast<CursorLock>(0x005E65F0);
    return reinterpret_cast<CursorLock>(kCall + 5 + rel);
}
__declspec(naked) void __fastcall ScreenDraw(void*) EI_ORIGINAL(0x005E54A0)
__declspec(naked) int __fastcall RendererFlip(void*) EI_ORIGINAL(0x004EF610)
__declspec(naked) void __fastcall ScreenEnd(void*) EI_ORIGINAL(0x005E6610)
__declspec(naked) void* __fastcall CaptureSurface(void* /*surface*/, const std::uint32_t* /*rect*/) EI_ORIGINAL(0x006D6D40)
__declspec(naked) void __fastcall BitmapWrite(void* /*bitmap*/, void*, mfc::CFile*) EI_ORIGINAL(0x006D5AC0)

// The movie list: CStringArray (library), the .ini reader, CString concatenation (AFXAPI), the
// video player.
__declspec(naked) void __fastcall IniConstruct(void*) EI_ORIGINAL(0x00695B80)
__declspec(naked) int __fastcall IniSetPath(void*, void*, const char*) EI_ORIGINAL(0x00695BF0)
__declspec(naked) void __fastcall IniDestruct(void*) EI_ORIGINAL(0x00695BE0)
__declspec(naked) int __fastcall IniGetList(void*, void*, const char* /*section*/, const char* /*key*/, void* /*list*/, char) EI_ORIGINAL(0x00696090)
__declspec(naked) mfc::CString* __stdcall ConcatText(mfc::CString* /*out*/, const mfc::CString*, const char*) EI_ORIGINAL(0x00700C05)
__declspec(naked) mfc::CString* __stdcall ConcatString(mfc::CString* /*out*/, const mfc::CString*, const mfc::CString*) EI_ORIGINAL(0x00700B9F)

struct StringArray {
    void* vtable;
    mfc::CString* data;
    int size;
    int max;
    int grow;
};

// Fills the renderer's rectangle (+0x24) of the primary surface with black (Blt with
// DDBLT_COLORFILL | DDBLT_WAIT) under the render lock (taken twice, as the original does); the
// result goes to the renderer's +8.
void ClearScreen() {
    void* renderer = Renderer();
    gameapi::EnterCriticalSection(RenderLock());
    void* holder = At<void*>(renderer, 0x14);
    std::uint32_t fx[25] = {};   // DDBLTFX
    fx[0] = 100;
    fx[21] = 0;                  // dwFillColor
    gameapi::EnterCriticalSection(RenderLock());
    void* surface = At<void*>(holder, 0x10);
    using Blt = long(__stdcall*)(void*, void*, void*, void*, unsigned long, void*);
    long result = reinterpret_cast<Blt>((*static_cast<void***>(surface))[5])(surface, static_cast<std::uint8_t*>(renderer) + 0x24, nullptr,
                                                                               nullptr, 0x1000400, fx);
    gameapi::LeaveCriticalSection(RenderLock());
    At<long>(renderer, 8) = result;
    gameapi::LeaveCriticalSection(RenderLock());
}

} // namespace

// 00458010 (CL draw, DL cursor). Nothing while the application is inactive (a 50 ms sleep when
// no window of the interface is active).
void __fastcall FrameStep(bool draw, bool cursor) {
    if (!Running()) return;
    if (!InterfaceActiveWindow(Interface())) {
        gameapi::Sleep(50);
        return;
    }
    if (!GameStep(Game())) return;
    InterfaceUpdate(Interface());
    GameDraw(Game());
    if (cursor) InterfaceDrawCursor(Interface());
    if (!draw || !Running()) return;
    CursorLockAtCallSite()(Screen());
    ScreenDraw(Screen());
    if (RendererFlip(Renderer())) game::FatalError(Text(0x00784A30));   // "Can't flip render surface"
    ScreenEnd(Screen());
    if (!Global<std::uint8_t>(0x0079BC54)) return;
    char path[256];
    gamecrt::sprintf(path, Text(0x00784A20), Global<std::uint32_t>(0x0079BA0C), Global<std::uint32_t>(0x0079BC58));
    mfc::CFile file;
    if (!file.Open(path, 0x1001, nullptr)) {
        game::FatalError(Text(0x007849F4), path);   // "Can't open file (%s) to write movie"
        Global<std::uint8_t>(0x0079BC54) = 0;
        return;
    }
    // {0, 0, width, height} of the render target's surface; without one the original passes two
    // stack words it never set.
    std::uint32_t rect[5];
    void* renderer = Renderer();
    std::uint32_t width = rect[4], height = rect[4];
    if (At<void*>(renderer, 0x14)) {
        void* info = At<void*>(renderer, 0x18);
        width = At<std::uint32_t>(info, 0x14);
        height = At<std::uint32_t>(info, 0x18);
    }
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = width;
    rect[3] = height;
    void* bitmap = CaptureSurface(At<void*>(renderer, 0x14), rect);
    BitmapWrite(bitmap, nullptr, &file);
    if (bitmap) ei::VCall<void>(bitmap, 1, 1);
    file.Close();
    ++Global<std::uint32_t>(0x0079BC58);
}
EI_REPLACE(0x00458010, FrameStep);

// 00458270 (ECX section, DL clear first): plays the movies listed under `section` in the .ini at
// 007AF460 + "..." (each from the directory 007AF458 + "..."), clearing the screen before and,
// with `clearAfter`, after each.
void __fastcall PlayMovies(const char* section, bool clearFirst, bool clearAfter, int a, int b) {
    StringArray list;
    StringArrayConstruct(&list);
    alignas(4) std::uint8_t ini[4];
    IniConstruct(ini);
    {
        alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
        mfc::CString* path = ConcatText(reinterpret_cast<mfc::CString*>(raw), reinterpret_cast<const mfc::CString*>(0x007AF460), Text(0x00784A5C));
        IniSetPath(ini, nullptr, *path);
        path->~CString();
    }
    IniGetList(ini, nullptr, section, Text(0x00784A54), &list, ',');
    if (clearFirst) ClearScreen();
    for (int i = 0; i < list.size; ++i) {
        alignas(mfc::CString) unsigned char dirRaw[sizeof(mfc::CString)];
        alignas(mfc::CString) unsigned char fullRaw[sizeof(mfc::CString)];
        mfc::CString* dir = ConcatText(reinterpret_cast<mfc::CString*>(dirRaw), reinterpret_cast<const mfc::CString*>(0x007AF458), Text(0x00784A4C));
        mfc::CString* full = ConcatString(reinterpret_cast<mfc::CString*>(fullRaw), dir, &list.data[i]);
        PlayMovieFile(*full, 0, 1, 0, 0, a, b);
        full->~CString();
        dir->~CString();
        if (clearAfter) ClearScreen();
    }
    IniDestruct(ini);
    StringArrayDestruct(&list);
}
EI_REPLACE(0x00458270, PlayMovies);

} // namespace app
