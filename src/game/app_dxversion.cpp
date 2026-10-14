// The DirectX version probe (0045E310, called once by WinMain): the DirectX SDK's GetDXVersion.
// It loads DDRAW.DLL and DINPUT.DLL, creates DirectDraw, a primary surface and DirectMusic, asks
// for the newer interfaces and finally loads D3D8.DLL, returning 0 (no DirectDraw), 0x100 (DX1),
// 0x200 (DX2), 0x300 (DX3), 0x500 (DX5), 0x600, 0x601, 0x700 or 0x800. As in the SDK sample, some
// interfaces are never released and some failures leave a library loaded; kept as is. The
// interface ids and class ids are the game's own (00744900 ... 00745CB0). See
// docs/notes/app.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "../hook/log.h"
#include <cstring>
#include <cstdlib>

#include <cstdint>

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
constexpr int kCreateSurface = 6;            // IDirectDraw::CreateSurface (+0x18)
constexpr int kSetCooperativeLevel = 20;     // IDirectDraw::SetCooperativeLevel (+0x50)

const void* Id(std::uintptr_t a) { return reinterpret_cast<const void*>(a); }
const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

using DirectDrawCreate = long(__stdcall*)(void*, void**, void*);
using DirectDrawCreateEx = long(__stdcall*)(void*, void**, const void*, void*);

} // namespace

std::uint32_t __cdecl GetDxVersion() {
    {   // PF3 EXPERIMENT
        char mode[32] = {};
        if (GetEnvironmentVariableA("EI_PF3_MODE", mode, sizeof(mode)) > 0) {
            ei::Log("PF3 experiment: %s", mode);
            if (std::strchr(mode, 'c')) {
                void* music = nullptr;
                gameapi::CoInitialize(nullptr);
                if (gameapi::CoCreateInstance(*static_cast<const CLSID*>(Id(0x00744930)), nullptr, 1, *static_cast<const IID*>(Id(0x00744900)), &music) >= 0) {
                    Com(music, kRelease);
                    gameapi::CoUninitialize();
                }
            }
            if (std::strchr(mode, 'd')) {
                void* dd = nullptr;
                FARPROC create = gameapi::GetProcAddress(gameapi::GetModuleHandleA("ddraw.dll"), Text(0x00785660));
                if (reinterpret_cast<DirectDrawCreate>(create)(nullptr, &dd, nullptr) >= 0) Com(dd, kRelease);
            }
            if (std::strchr(mode, 'i')) {
                gameapi::FreeLibrary(gameapi::LoadLibraryA(Text(0x00785604)));
            }
            ei::Log("PF3 experiment done");
            return 0x700;
        }
    }
    void* ddraw = nullptr;     // IDirectDraw
    void* ddraw2 = nullptr;
    void* surface = nullptr;   // IDirectDrawSurface
    void* surface3 = nullptr;
    void* surface4 = nullptr;
    HMODULE ddrawLibrary = gameapi::LoadLibraryA(Text(0x00785674));   // "DDRAW.DLL"
    if (!ddrawLibrary) return 0;
    FARPROC create = gameapi::GetProcAddress(ddrawLibrary, Text(0x00785660));   // "DirectDrawCreate"
    if (!create) {
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA(Text(0x00785640));
        return 0;
    }
    if (reinterpret_cast<DirectDrawCreate>(create)(nullptr, &ddraw, nullptr) < 0) {
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA(Text(0x00785628));
        return 0;
    }
    if (Com(ddraw, kQueryInterface, Id(0x00745CB0), &ddraw2) < 0) {   // IID_IDirectDraw2
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA(Text(0x00785610));
        return 0x100;
    }
    Com(ddraw2, kRelease);

    HMODULE inputLibrary = gameapi::LoadLibraryA(Text(0x00785604));   // "DINPUT.DLL"
    if (!inputLibrary) {
        gameapi::OutputDebugStringA(Text(0x007855E4));
        Com(ddraw, kRelease);
        return 0x200;
    }
    if (!gameapi::GetProcAddress(inputLibrary, Text(0x007855D0))) {   // "DirectInputCreateA"
        gameapi::FreeLibrary(inputLibrary);
        gameapi::FreeLibrary(ddrawLibrary);
        Com(ddraw, kRelease);
        gameapi::OutputDebugStringA(Text(0x007855A8));
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
        gameapi::OutputDebugStringA(Text(0x0078558C));
        return 0;
    }
    if (Com(ddraw, kCreateSurface, static_cast<void*>(desc), &surface, static_cast<void*>(nullptr)) < 0) {
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        gameapi::OutputDebugStringA(Text(0x00785570));
        return 0;
    }
    if (Com(surface, kQueryInterface, Id(0x00745C60), &surface3) < 0) {   // IID_IDirectDrawSurface3
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x300;
    }
    if (Com(surface, kQueryInterface, Id(0x00745C50), &surface4) < 0) {   // IID_IDirectDrawSurface4
        Com(ddraw, kRelease);
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x500;
    }
    Com(surface, kRelease);
    Com(ddraw, kRelease);

    void* music = nullptr;   // IDirectMusic
    gameapi::CoInitialize(nullptr);
    if (gameapi::CoCreateInstance(*static_cast<const CLSID*>(Id(0x00744930)), nullptr, 1, *static_cast<const IID*>(Id(0x00744900)),
                                  &music) < 0) {
        gameapi::OutputDebugStringA(Text(0x0078554C));
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x600;
    }
    Com(music, kRelease);
    gameapi::CoUninitialize();

    FARPROC createEx = gameapi::GetProcAddress(ddrawLibrary, Text(0x00785538));   // "DirectDrawCreateEx"
    if (!createEx) {
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x601;
    }
    void* ddraw7 = nullptr;
    if (reinterpret_cast<DirectDrawCreateEx>(createEx)(nullptr, &ddraw7, Id(0x00745C90), nullptr) < 0) {   // IID_IDirectDraw7
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x601;
    }
    Com(ddraw7, kRelease);
    HMODULE d3d8 = gameapi::LoadLibraryA(Text(0x0078552C));   // "D3D8.DLL"
    if (!d3d8) {
        gameapi::FreeLibrary(ddrawLibrary);
        return 0x700;
    }
    gameapi::FreeLibrary(ddrawLibrary);
    gameapi::FreeLibrary(d3d8);
    return 0x800;
}
EI_REPLACE(0x0045E310, GetDxVersion);

} // namespace app
