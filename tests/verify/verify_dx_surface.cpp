// Checks for the off-screen surface holder (dx_surface.cpp) and the window surface's DC
// (ui_surface_dc.cpp). All ours only (DirectDraw objects, GDI).
#include "verify.h"

#include <windows.h>

#include <cstdint>

char* __fastcall DXSurfaceConstruct(char* holder, void*, char* device, int width, int height);
void __fastcall DXSurfaceDestroy(char* holder);
void* __fastcall UiSurfaceGetDC(char* surface);
void __fastcall UiSurfaceReleaseDC(char* surface);

// 004EF7B0: returns the holder with its vtable, the device and a surface of the asked size (it
// throws instead of returning without one).
static void* s_construct;
static char* __fastcall DXSurfaceConstructVerify(char* holder, void*, char* device, int width, int height) {
    char* result = DXSurfaceConstruct(holder, nullptr, device, width, height);
    bool ok = result == holder && *reinterpret_cast<std::uintptr_t*>(holder) == 0x0073D774 &&
              *reinterpret_cast<char**>(holder + 0xC) == device && *reinterpret_cast<void**>(holder + 0x10) != nullptr &&
              *reinterpret_cast<int*>(holder + 0x14) == width && *reinterpret_cast<int*>(holder + 0x18) == height;
    ei::verify::Check("DXSurfaceConstruct", true, ok, "%dx%d", width, height);
    return result;
}
EI_VERIFY(0x004EF7B0, DXSurfaceConstructVerify, 0, s_construct);

// 004EFD20: the base vtable and no device afterwards; the holder count one lower.
static void* s_destroy;
static void __fastcall DXSurfaceDestroyVerify(char* holder) {
    int count = *reinterpret_cast<int*>(0x007AEE94);
    DXSurfaceDestroy(holder);
    bool ok = *reinterpret_cast<std::uintptr_t*>(holder) == 0x0073D768 && *reinterpret_cast<char**>(holder + 0xC) == nullptr &&
              *reinterpret_cast<int*>(0x007AEE94) == count - 1;
    ei::verify::Check("DXSurfaceDestroy", true, ok, "holder %p", static_cast<void*>(holder));
}
EI_VERIFY(0x004EFD20, DXSurfaceDestroyVerify, 0, s_destroy);

// 005FDC00: with a surface and a CDC returned, the HDC is in +8.
static void* s_getDc;
static void* __fastcall UiSurfaceGetDCVerify(char* surface) {
    void* result = UiSurfaceGetDC(surface);
    bool ok = !result || *reinterpret_cast<HDC*>(surface + 8) != nullptr;
    ei::verify::Check("UiSurfaceGetDC", true, ok, "surface %p", static_cast<void*>(surface));
    return result;
}
EI_VERIFY(0x005FDC00, UiSurfaceGetDCVerify, 0, s_getDc);

// 005FDC60: +8 cleared.
static void* s_releaseDc;
static void __fastcall UiSurfaceReleaseDCVerify(char* surface) {
    UiSurfaceReleaseDC(surface);
    ei::verify::Check("UiSurfaceReleaseDC", true, *reinterpret_cast<HDC*>(surface + 8) == nullptr, "surface %p",
                      static_cast<void*>(surface));
}
EI_VERIFY(0x005FDC60, UiSurfaceReleaseDCVerify, 0, s_releaseDc);
