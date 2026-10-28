// The DirectX version probe (called once by WinMain): the DirectX SDK's GetDXVersion, returning 0 (no DirectDraw)
// up to 0x800; as in the SDK sample, some interfaces are never released. See docs/notes/app.md. With the
// system's DDRAW.DLL (none in the exe's folder, such as dgVoodoo's) nothing is probed and the result is 0x800:
// the probe cost half a second and failed now and then with several games on the machine.
#include "game_strings.h"
#include "lib/gameapi.h"

#include "../hook/log.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace app {

namespace {

// A COM method through the object's vtable (STDMETHODCALLTYPE: `this` first on the stack).
template <typename... Args>
long Com(void* object, int slot, Args... args) {
    using Method = long(__stdcall*)(void*, Args...);
    return reinterpret_cast<Method>((*static_cast<void***>(object))[slot])(object, args...);
}
constexpr int kQueryInterface = 0;
constexpr int kRelease = 2;
constexpr int kCreateSurface = 6;
constexpr int kSetCooperativeLevel = 20;

constexpr IID kIidDirectMusic2 = /*00744900*/ {0x6536115A, 0x7B2D, 0x11D2, {0xBA, 0x18, 0x00, 0x00, 0xF8, 0x75, 0xAC, 0x12}};
constexpr CLSID kClsidDirectMusic = /*00744930*/ {0x636B9F10, 0x0C7D, 0x11D1, {0x95, 0xB2, 0x00, 0x20, 0xAF, 0xDC, 0x74, 0x21}};
constexpr IID kIidDirectDrawSurface4 = /*00745C50*/ {0x0B2B8630, 0xAD35, 0x11D0, {0x8E, 0xA6, 0x00, 0x60, 0x97, 0x97, 0xEA, 0x5B}};
constexpr IID kIidDirectDrawSurface3 = /*00745C60*/ {0xDA044E00, 0x69B2, 0x11D0, {0xA1, 0xD5, 0x00, 0xAA, 0x00, 0xB8, 0xDF, 0xBB}};
constexpr IID kIidDirectDraw7 = /*00745C90*/ {0x15E65EC0, 0x3B9C, 0x11D2, {0xB9, 0x2F, 0x00, 0x60, 0x97, 0x97, 0xEA, 0x5B}};
constexpr IID kIidDirectDraw2 = /*00745CB0*/ {0xB3A6F3E0, 0x2B43, 0x11CF, {0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56}};

using DirectDrawCreate = long(__stdcall*)(void*, void**, void*);
using DirectDrawCreateEx = long(__stdcall*)(void*, void**, const void*, void*);

const char* DirectDrawLibraryName() { return "DDRAW.DLL"; }

// The folder part of a module's file name (up to and including the last backslash), or "".
void ModuleFolder(HMODULE module, char (&folder)[MAX_PATH]) {
    const DWORD length = gameapi::GetModuleFileNameA(module, folder, MAX_PATH);
    char* slash = length > 0 && length < MAX_PATH ? std::strrchr(folder, '\\') : nullptr;
    if (slash) slash[1] = 0;
    else folder[0] = 0;
}

// The game imports the system's DirectDraw: no DDRAW.DLL (dgVoodoo) next to game.exe.
bool SystemDirectDraw() {
    const HMODULE ddraw = gameapi::GetModuleHandleA(DirectDrawLibraryName());
    if (!ddraw) return false;
    char exeFolder[MAX_PATH], ddrawFolder[MAX_PATH];
    ModuleFolder(nullptr, exeFolder);
    ModuleFolder(ddraw, ddrawFolder);
    return exeFolder[0] && ddrawFolder[0] && gameapi::lstrcmpiA(exeFolder, ddrawFolder) != 0;
}

bool DirectXIsGiven() {
    if (!SystemDirectDraw()) return false;
    ei::Log("app: native run: DirectX 7 taken as given, the version probe (0045E310) skipped");
    return true;
}

} // namespace

std::uint32_t GetDxVersion() {
    if (DirectXIsGiven()) return 0x800;
    void* ddraw = nullptr;     // IDirectDraw
    void* ddraw2 = nullptr;
    void* surface = nullptr;   // IDirectDrawSurface
    void* surface3 = nullptr;
    void* surface4 = nullptr;
    HMODULE ddrawLibrary = gameapi::LoadLibraryA(DirectDrawLibraryName());
    if (!ddrawLibrary) return 0;
    FARPROC create = gameapi::GetProcAddress(ddrawLibrary, "DirectDrawCreate");
    if (!create) {
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA("Couldn't LoadLibrary DDraw\r\n");
        return 0;
    }
    if (reinterpret_cast<DirectDrawCreate>(create)(nullptr, &ddraw, nullptr) < 0) {
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA("Couldn't create DDraw\r\n");
        return 0;
    }
    if (Com(ddraw, kQueryInterface, &kIidDirectDraw2, &ddraw2) < 0) {
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA("Couldn't QI DDraw2\r\n");
        return 0x100;
    }
    Com(ddraw2, kRelease);

    HMODULE inputLibrary = gameapi::LoadLibraryA("DINPUT.DLL");
    if (!inputLibrary) {
        gameapi::OutputDebugStringA("Couldn't LoadLibrary DInput\r\n");
        Com(ddraw, kRelease);
        return 0x200;
    }
    if (!gameapi::GetProcAddress(inputLibrary, "DirectInputCreateA")) {
        gameapi::FreeLibrary(inputLibrary);
        gameapi::FreeLibrary(ddrawLibrary);
        Com(ddraw, kRelease);
        gameapi::OutputDebugStringA("Couldn't GetProcAddress DInputCreate\r\n");
        return 0x200;
    }
    gameapi::FreeLibrary(inputLibrary);

    std::uint32_t desc[27] = {};   // DDSURFACEDESC
    desc[0] = 0x6C;                // dwSize
    desc[1] = 1;                   // DDSD_CAPS
    desc[26] = 0x200;              // DDSCAPS_PRIMARYSURFACE
    if (Com(ddraw, kSetCooperativeLevel, static_cast<void*>(nullptr), 8) < 0) {   // DDSCL_NORMAL
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA("Couldn't Set coop level\r\n");
        return 0;
    }
    if (Com(ddraw, kCreateSurface, static_cast<void*>(desc), &surface, static_cast<void*>(nullptr)) < 0) {
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA("Couldn't CreateSurface\r\n");
        return 0;
    }
    if (Com(surface, kQueryInterface, &kIidDirectDrawSurface3, &surface3) < 0) {
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x300;
    }
    if (Com(surface, kQueryInterface, &kIidDirectDrawSurface4, &surface4) < 0) {
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x500;
    }
    Com(surface, kRelease);
    Com(ddraw, kRelease);

    void* music = nullptr;   // IDirectMusic
    gameapi::CoInitialize(nullptr);
    if (gameapi::CoCreateInstance(kClsidDirectMusic, nullptr, 1, kIidDirectMusic2, &music) < 0) {
        gameapi::OutputDebugStringA("Couldn't create CLSID_DirectMusic\r\n");
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x600;
    }
    Com(music, kRelease);
    gameapi::CoUninitialize();

    FARPROC createEx = gameapi::GetProcAddress(ddrawLibrary, "DirectDrawCreateEx");
    if (!createEx) {
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x601;
    }
    void* ddraw7 = nullptr;
    if (reinterpret_cast<DirectDrawCreateEx>(createEx)(nullptr, &ddraw7, &kIidDirectDraw7, nullptr) < 0) {
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x601;
    }
    Com(ddraw7, kRelease);
    HMODULE d3d8 = gameapi::LoadLibraryA("D3D8.DLL");
    if (!d3d8) {
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x700;
    }
    gameapi::FreeLibrary(ddrawLibrary);
    gameapi::FreeLibrary(d3d8);
    return 0x800;
}

} // namespace app
