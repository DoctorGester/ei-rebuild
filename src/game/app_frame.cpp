// WinMain's frame step (00458010: the interface and the game ticked, the frame drawn and flipped,
// a screenshot per frame while recording a movie) and the intro movies (00458270: the list read
// from an .ini file, each played, the screen cleared before and after). Every
// callee is reached through its original address. See docs/notes/app.md.
#include "dib.h"
#include "game_strings.h"
#include "model.h"
#include "texture.h"   // CTextureDevice, CDXSurface

#include "ei/hook.h"
#include "ei/main_loop.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ini_file.h"
#include "bink_video.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace app {

namespace {


bool Running() { return ei::GameData<std::uint8_t>(0x0079BC5C) != 0 && ei::GameData<std::uint8_t>(0x00784750) != 0; }

__declspec(naked) void __fastcall InterfaceDrawCursor(void*) EI_ORIGINAL(0x005F5320)
// The software cursor's lock (005E65F0) is called through the target of the original CALL at
// 004580D7 (the original body stays in place behind the hook's jump): the harness's frame capture
// redirects that call (tests/verify/harness_capture.cpp) to read the finished frame before the
// cursor is blitted and the frame flipped, and keeps working with this rewrite. Without the
// harness, and in the standalone image (no original code there: CodeAddress), the target is 005E65F0's code. A
// harness hook for the call (ei/main_loop.h) comes first wherever one is set; in the image it is the
// harness's only way in.
using CursorLock = ei::mainloop::HarnessHooks::CursorLock;
CursorLock CursorLockAtCallSite() {
    if (auto hook = ei::mainloop::g_harnessHooks.cursorLock) return hook;
    constexpr std::uintptr_t kCall = std::uintptr_t(0x004580D7);   // the original CALL of the cursor lock
    const auto direct = ei::Code<CursorLock>(0x005E65F0);
    if (ei::Standalone()) return direct;
    const auto* code = reinterpret_cast<const unsigned char*>(kCall);
    std::int32_t rel;
    std::memcpy(&rel, code + 1, 4);
    if (code[0] != 0xE8) return direct;
    return reinterpret_cast<CursorLock>(kCall + 5 + rel);
}

// The movie list: CStringArray (library), the .ini reader, CString concatenation (AFXAPI), the
// video player.
__declspec(naked) void __fastcall IniConstruct(void*) EI_ORIGINAL(0x00695B80)
__declspec(naked) int __fastcall IniSetPath(void*, void*, const char*) EI_ORIGINAL(0x00695BF0)
__declspec(naked) void __fastcall IniDestruct(void*) EI_ORIGINAL(0x00695BE0)
mfc::CString* __stdcall ConcatText(mfc::CString* a0, const mfc::CString* a1, const char* a2) { return CStringConcatTextOriginal(a0, a1, a2); }
mfc::CString* __stdcall ConcatString(mfc::CString* a0, const mfc::CString* a1, const mfc::CString* a2) { return CStringConcatOriginal(a0, a1, a2); }

// The original fills the device object's screen rectangle of the primary surface with black (a
// colour-fill Blt under the render lock, taken twice) and keeps the result in the device's
// lastLoadResult_. The primary is a holder without a surface (renderer.md section 9): nothing to
// fill, the result 0.
void ClearScreen() {
    CTextureDevice* device = TheRenderDevice();
    gameapi::EnterCriticalSection(TheDeviceLock());
    device->lastLoadResult_ = 0;
    gameapi::LeaveCriticalSection(TheDeviceLock());
}

} // namespace

// 00458010 (CL draw, DL cursor). Nothing while the application is inactive (a 50 ms sleep when
// no window of the interface is active).
void __fastcall FrameStep(bool draw, bool cursor) {
    if (!Running()) return;
    if (!InterfaceActiveWindowOriginal(reinterpret_cast<CInterface3D*>(TheInterface()))) {
        gameapi::Sleep(50);
        return;
    }
    if (!TheSceneRenderer()->BeginScene()) return;
    static_cast<CInterface3D*>(TheInterface())->Draw();
    TheSceneRenderer()->EndScene();
    if (cursor) InterfaceDrawCursor(TheInterface());
    if (!draw || !Running()) return;
    CursorLockAtCallSite()(TheCursor());
    CursorDrawFrame(TheCursor());   // 005E54A0
    if (DevicePresent(TheRenderDevice())) game::FatalError("Can't flip render surface");   // 004EF610: "Can't flip render surface"
    TheCursor()->ReleaseLock();
    if (!ei::GameData<std::uint8_t>(0x0079BC54)) return;
    char path[256];
    gamecrt::sprintf(path, "%sshot%.4d.bmp", ei::GameData<std::uint32_t>(0x0079BA0C), ei::GameData<std::uint32_t>(0x0079BC58));
    mfc::CFile file;
    if (!file.Open(path, 0x1001, nullptr)) {
        game::FatalError("Can't open file \"%s\" to write movie frame.\n", path);   // "Can't open file (%s) to write movie"
        ei::GameData<std::uint8_t>(0x0079BC54) = 0;
        return;
    }
    // {0, 0, width, height} of the render target's surface; without one the original passes two
    // stack words it never set.
    std::uint32_t rect[5];
    CTextureDevice* device = TheRenderDevice();
    std::uint32_t width = rect[4], height = rect[4];
    if (device->primary_) {
        CDXSurface* target = device->renderTarget_;
        width = static_cast<std::uint32_t>(target->width);
        height = static_cast<std::uint32_t>(target->height);
    }
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = width;
    rect[3] = height;
    CDib* bitmap = CaptureSurfaceRect(device->primary_, reinterpret_cast<const RECT*>(rect));
    if (bitmap) bitmap->Write(&file);
    if (bitmap) reinterpret_cast<mfc::CObjectVirtuals*>(bitmap)->DeletingDestructor(1);   // slot 1 Delete(1)
    file.Close();
    ++ei::GameData<std::uint32_t>(0x0079BC58);
}
EI_REPLACE(0x00458010, FrameStep);

// 00458270 (ECX section, DL clear first): plays the movies listed under `section` in the .ini at
// 007AF460 + "..." (each from the directory 007AF458 + "..."), clearing the screen before and,
// with `clearAfter`, after each.
void __fastcall PlayMovies(const char* section, bool clearFirst, bool clearAfter, int a, int b) {
    mfc::CStringArray list;
    list.Construct();
    alignas(4) std::uint8_t ini[4];
    IniConstruct(ini);
    {
        alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
        mfc::CString* path = ConcatText(reinterpret_cast<mfc::CString*>(raw), &TheDataFolder(), "config\\movie.ini");
        IniSetPath(ini, nullptr, *path);
        path->~CString();
    }
    reinterpret_cast<CIniFile*>(ini)->GetList(section, "movies", reinterpret_cast<console::CStringArray*>(&list), ',');
    if (clearFirst) ClearScreen();
    for (int i = 0; i < list.size; ++i) {
        alignas(mfc::CString) unsigned char dirRaw[sizeof(mfc::CString)];
        alignas(mfc::CString) unsigned char fullRaw[sizeof(mfc::CString)];
        mfc::CString* dir = ConcatText(reinterpret_cast<mfc::CString*>(dirRaw), ei::GamePointer<const mfc::CString>(0x007AF458), "Movies\\");
        mfc::CString* full = ConcatString(reinterpret_cast<mfc::CString*>(fullRaw), dir, &list.data[i]);
        PlayMovie(*full, 0, 1, 0, 0, static_cast<char>(a), b);
        full->~CString();
        dir->~CString();
        if (clearAfter) ClearScreen();
    }
    IniDestruct(ini);
    list.Destruct();
}
EI_REPLACE(0x00458270, PlayMovies);

} // namespace app
