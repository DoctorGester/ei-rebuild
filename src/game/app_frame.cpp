// WinMain's frame step (00458010: the interface and the game ticked, the frame drawn and flipped,
// a screenshot per frame while recording a movie) and the intro movies (00458270: the list read
// from an .ini file, each played, the screen cleared before and after). Every
// callee is reached through its original address. See docs/notes/app.md.
#include "app.h"
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
// The software cursor's lock (005E65F0), unless the harness hooks the call (ei/main_loop.h): its frame
// capture reads the finished frame before the cursor is blitted and the frame flipped
// (tests/verify/harness_capture.cpp; in game.exe's own runs it patches the original CALL at 004580D7).
using CursorLock = ei::mainloop::HarnessHooks::CursorLock;
CursorLock CursorLockAtCallSite() {
    if (auto hook = ei::mainloop::g_harnessHooks.cursorLock) return hook;
    return ei::Code<CursorLock>(0x005E65F0);
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

std::uint32_t g_movieFrame = /*0079BC58*/ {};   // the number of the next frame file of the movie being written

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
    TheInterface()->Draw();
    TheSceneRenderer()->EndScene();
    if (cursor) InterfaceDrawCursor(TheInterface());
    if (!draw || !Running()) return;
    CursorLockAtCallSite()(TheCursor());
    CursorDrawFrame(TheCursor());   // 005E54A0
    if (DevicePresent(TheRenderDevice())) game::FatalError("Can't flip render surface");   // 004EF610: "Can't flip render surface"
    TheCursor()->ReleaseLock();
    if (!ei::GameData<std::uint8_t>(0x0079BC54)) return;
    char path[256];
    gamecrt::sprintf(path, "%sshot%.4d.bmp", static_cast<const char*>(MovieFolder()), g_movieFrame);
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
    ++g_movieFrame;
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
        mfc::CString path(mfc::CString::kUnset);
        IniSetPath(ini, nullptr, *ConcatText(&path, &TheDataFolder(), "config\\movie.ini"));
    }
    reinterpret_cast<CIniFile*>(ini)->GetList(section, "movies", reinterpret_cast<console::CStringArray*>(&list), ',');
    if (clearFirst) ClearScreen();
    for (int i = 0; i < list.size; ++i) {
        {
            mfc::CString dir(mfc::CString::kUnset);
            mfc::CString full(mfc::CString::kUnset);
            ConcatText(&dir, &TheDiscFolder(), "Movies\\");
            PlayMovie(*ConcatString(&full, &dir, &list.data[i]), 0, 1, 0, 0, static_cast<char>(a), b);
        }
        if (clearAfter) ClearScreen();
    }
    IniDestruct(ini);
    list.Destruct();
}
EI_REPLACE(0x00458270, PlayMovies);

} // namespace app
