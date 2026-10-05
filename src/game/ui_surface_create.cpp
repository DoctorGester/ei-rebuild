// The window surface's constructors and destructor body (CUiSurface, 0x38 bytes, vtable 00740F28):
// 005FCF80 (a window showing an .mmp picture), 005FD120 (ReleaseContents), 005FCC00 (thiscall:
// interface, layout left, top, right, bottom, format, scale flag; RET 0x1C) and 005FCDD0 (thiscall:
// interface, layout rectangle, format; always scaled; RET 0xC). Both scale the rectangle to the
// screen (005F7CA0, Rebirth's wide-screen patch sits there), make a surface holder of its size
// (004EF7B0), add height * pitch to the surface-bytes total 007C7E78, clear the surface to 0 with a
// colour-fill Blt and set flag 4 of +0x0C.
//
// With the new renderer's memory ("cpuwindows", dx_surface.cpp) the surface is already zero (a new
// DIB section) and the colour fill is left out; in the switch-over ("present") its description comes
// from that memory too, and the holder has no DirectDraw surface at all (ticket R7), so making a
// window asks DirectDraw for nothing.
// The surface descriptions 005FCF80 / 005FD120 take for the surface-bytes total come from that
// memory too in the switch-over, for picture windows as well: their holders (004EFC60 -> 004F0060,
// a system-memory picture surface) get game-owned memory like the others. (005FCF80 itself has
// no caller in game.exe or the mods: config/dead_code.csv.)
// The original's unwinding when 004EF7B0 throws (freeing the holder's memory) is not reproduced:
// that throw ends the game.
#include "ui.h"

#include "dx_surface.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_render.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

constexpr int kBlt = 5, kGetSurfaceDesc = 22;              // IDirectDrawSurface7
constexpr unsigned long kColourFillWait = 0x01000400;      // DDBLT_COLORFILL | DDBLT_WAIT

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
std::uint32_t& SurfaceBytesTotal() { return *reinterpret_cast<std::uint32_t*>(0x007C7E78); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

__declspec(naked) void* __cdecl GameNew(unsigned) EI_ORIGINAL(0x007005AF)
__declspec(naked) char* __fastcall ConstructHolder(void*, void*, void*, int, int) EI_ORIGINAL(0x004EF7B0)

// 005F7CA0 (thiscall on the interface, RET 4).
struct ScaleAt {
    int ScaleRectToScreen(int* rect);
};
__declspec(naked) int ScaleAt::ScaleRectToScreen(int*) EI_ORIGINAL(0x005F7CA0)

// The common tail: +0x24..+0x30 hold the layout rectangle, +0x04 the interface, +0x0C the format.
char* Finish(char* self, bool scale) {
    int* rect = &At<int>(self, 0x24);
    char* ui = At<char*>(self, 4);
    if (scale) reinterpret_cast<ScaleAt*>(ui)->ScaleRectToScreen(rect);
    At<int>(self, 0x14) = rect[2] - rect[0];
    At<int>(self, 0x18) = rect[3] - rect[1];
    void* memory = GameNew(0x20);
    char* made = memory ? ConstructHolder(memory, nullptr, At<void*>(At<char*>(self, 4), 8), At<int>(self, 0x14),
                                          At<int>(self, 0x18))
                        : nullptr;
    char* previous = At<char*>(self, 0x10);
    At<char*>(self, 0x10) = made;
    if (made) ++At<int>(made, 4);
    if (previous && --At<int>(previous, 4) == 0) {
        using DeleteFn = void(__thiscall*)(void*, int);
        reinterpret_cast<DeleteFn>(**reinterpret_cast<void***>(previous))(previous, 1);
    }
    char* holder = At<char*>(self, 0x10);
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    if (!DirectPixels(holder, desc)) {
        long hr = ComCall(At<void*>(holder, 0x10), kGetSurfaceDesc, static_cast<void*>(desc));
        At<long>(holder, 8) = hr;
        if (hr != 0) FatalDirectX(hr, Text(0x0078E6C4));   // "Can't get surface description"
    }
    SurfaceBytesTotal() += desc[2] * desc[4];   // height * pitch
    if (!ClientBitmap(holder)) {
        std::uint32_t fx[0x19] = {};   // DDBLTFX: size, fill colour (+0x50) 0
        fx[0] = 100;
        gameapi::EnterCriticalSection(DeviceLock());
        ComCall(At<void*>(holder, 0x10), kBlt, static_cast<void*>(nullptr), static_cast<void*>(nullptr),
                static_cast<void*>(nullptr), kColourFillWait, static_cast<void*>(fx));
        gameapi::LeaveCriticalSection(DeviceLock());
    }
    At<int>(self, 0x1C) = 0;
    At<std::uint32_t>(self, 0x0C) |= 4;
    At<int>(self, 0x20) = 0;
    return self;
}

void Begin(char* self, void* ui, int format) {
    At<void*>(self, 0x10) = nullptr;
    At<int>(self, 0x0C) = format;
    At<std::uintptr_t>(self, 0) = 0x00740F28;
    At<int>(self, 0x34) = 0;
    At<void*>(self, 4) = ui;
}

} // namespace

namespace {

// The surface's description: from its game-owned memory in the switch-over (DirectPixels: window
// and picture holders alike), else GetSurfaceDesc (the result in the holder's +8, reported).
void Describe(char* holder, std::uint32_t* desc) {
    std::memset(desc, 0, 0x7C);
    desc[0] = 0x7C;
    if (DirectPixels(holder, desc)) return;
    long hr = ComCall(At<void*>(holder, 0x10), kGetSurfaceDesc, static_cast<void*>(desc));
    At<long>(holder, 8) = hr;
    if (hr != 0) FatalDirectX(hr, Text(0x0078E6C4));   // "Can't get surface description"
}

void ReleaseHolderReference(char* holder) {
    if (holder && --At<int>(holder, 4) == 0) {
        using DeleteFn = void(__thiscall*)(void*, int);
        reinterpret_cast<DeleteFn>(**reinterpret_cast<void***>(holder))(holder, 1);
    }
}

void __fastcall DropHolder(char* a0) { SurfaceHolderReleaseOriginal((void*)a0); }

} // namespace

// 005FD120 (thiscall). The destructor body (CUiSurface::ReleaseContents): frees the kept bytes
// (total 007C7E74), takes the surface's height * pitch off 007C7E78, drops the holder and empties
// the screen rectangle.
void __fastcall UiSurfaceRelease(char* self) {
    At<std::uintptr_t>(self, 0) = 0x00740F28;
    if (void* kept = At<void*>(self, 0x1C)) {
        gamecrt::operator_delete(kept);
        At<void*>(self, 0x1C) = nullptr;
        *reinterpret_cast<std::uint32_t*>(0x007C7E74) -= At<std::uint32_t>(self, 0x20);
    }
    if (char* holder = At<char*>(self, 0x10)) {
        std::uint32_t desc[0x1F];
        Describe(holder, desc);
        SurfaceBytesTotal() -= desc[2] * desc[4];   // height * pitch
    }
    char* holder = At<char*>(self, 0x10);
    At<char*>(self, 0x10) = nullptr;
    ReleaseHolderReference(holder);
    gameapi::SetRectEmpty(&At<RECT>(self, 0x24));
}
EI_REPLACE(0x005FD120, UiSurfaceRelease);

// 005FCF80 (thiscall: interface, picture name by value; RET 8). A window showing the picture
// "<name>.mmp" from the texture manager (007AF744 slots 19 / 20: find, release), through a picture
// holder (004EFC60); its size is the picture's, the screen rectangle (0, 0, width, height), flag 1.
char* __fastcall UiSurfaceFromPicture(char* self, void*, void* ui, mfc::CString name) {
    At<void*>(self, 0x10) = nullptr;
    At<std::uintptr_t>(self, 0) = 0x00740F28;
    At<int>(self, 0x34) = 0;
    At<void*>(self, 4) = ui;
    void* manager = *reinterpret_cast<void* const*>(0x007AF744);
    void* picture;
    {
        mfc::CString file = name + Text(0x0078FB4C);   // ".mmp"
        picture = ei::VCall<void*>(manager, 19, *reinterpret_cast<const char* const*>(&file), 0);
    }
    void* memory = GameNew(0x20);
    char* made = memory ? static_cast<char*>(DXSurfaceFromPictureOriginal(memory, nullptr, At<void*>(At<char*>(self, 4), 8),
                                                                          picture, 0))
                        : nullptr;
    char* previous = At<char*>(self, 0x10);
    At<char*>(self, 0x10) = made;
    if (made) ++At<int>(made, 4);
    if (previous) DropHolder(previous);
    ei::VCall<void>(manager, 20, picture);
    char* holder = At<char*>(self, 0x10);
    At<int>(self, 0x14) = At<int>(holder, 0x14);
    At<int>(self, 0x18) = At<int>(holder, 0x18);
    std::uint32_t desc[0x1F];
    Describe(holder, desc);
    SurfaceBytesTotal() += desc[2] * desc[4];   // height * pitch
    gameapi::SetRect(&At<RECT>(self, 0x24), 0, 0, At<int>(At<char*>(self, 0x10), 0x14),
                     At<int>(At<char*>(self, 0x10), 0x18));
    At<int>(self, 0x1C) = 0;
    At<int>(self, 0x20) = 0;
    At<std::uint32_t>(self, 0x0C) |= 1;
    return self;
}
EI_REPLACE(0x005FCF80, UiSurfaceFromPicture);

// 005FCC00.
char* __fastcall UiSurfaceConstruct(char* self, void*, void* ui, int left, int top, int right, int bottom, int format,
                                    char scale) {
    Begin(self, ui, format);
    At<int>(self, 0x24) = left;
    At<int>(self, 0x28) = top;
    At<int>(self, 0x2C) = right;
    At<int>(self, 0x30) = bottom;
    return Finish(self, scale != 0);
}
EI_REPLACE(0x005FCC00, UiSurfaceConstruct);

// 005FCDD0.
char* __fastcall UiSurfaceConstructRect(char* self, void*, void* ui, const int* rect, int format) {
    Begin(self, ui, format);
    std::memcpy(&At<int>(self, 0x24), rect, 16);
    return Finish(self, true);
}
EI_REPLACE(0x005FCDD0, UiSurfaceConstructRect);
