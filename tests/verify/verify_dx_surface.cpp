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

// 004EFC60: returns the holder with its vtable, the device and a surface of the picture's size
// (+4 / +8 of the .mmp header); the holder count one higher.
char* __fastcall DXSurfaceFromPicture(char* holder, void*, char* device, const void* picture, int flag);
static void* s_fromPicture;
static char* __fastcall DXSurfaceFromPictureVerify(char* holder, void*, char* device, const void* picture, int flag) {
    int count = *reinterpret_cast<int*>(0x007AEE94);
    char* result = DXSurfaceFromPicture(holder, nullptr, device, picture, flag);
    const auto* header = static_cast<const int*>(picture);
    bool ok = result == holder && *reinterpret_cast<std::uintptr_t*>(holder) == 0x0073D774 &&
              *reinterpret_cast<char**>(holder + 0xC) == device && *reinterpret_cast<void**>(holder + 0x10) != nullptr &&
              *reinterpret_cast<int*>(holder + 0x14) == header[1] && *reinterpret_cast<int*>(holder + 0x18) == header[2] &&
              *reinterpret_cast<int*>(0x007AEE94) == count + 1;
    ei::verify::Check("DXSurfaceFromPicture", true, ok, "%dx%d flag %d", header[1], header[2], flag);
    return result;
}
EI_VERIFY(0x004EFC60, DXSurfaceFromPictureVerify, 0, s_fromPicture);

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

// 005FCC00 / 005FCDD0 (ui_surface_create.cpp): the vtable, a holder with a surface of the screen
// rectangle's size, flag 4, nothing kept, the surface-bytes total up by height * pitch.
char* __fastcall UiSurfaceConstruct(char* self, void*, void* ui, int left, int top, int right, int bottom, int format,
                                    char scale);
char* __fastcall UiSurfaceConstructRect(char* self, void*, void* ui, const int* rect, int format);
static void CheckWindowSurface(const char* name, char* self, std::uint32_t bytesBefore) {
    char* holder = *reinterpret_cast<char**>(self + 0x10);
    const int* rect = reinterpret_cast<const int*>(self + 0x24);
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    void* surface = holder ? *reinterpret_cast<void**>(holder + 0x10) : nullptr;
    bool described = surface && reinterpret_cast<long(__stdcall*)(void*, void*)>((*static_cast<void***>(surface))[22])(
                                    surface, desc) == 0;
    std::uint32_t grown = *reinterpret_cast<std::uint32_t*>(0x007C7E78) - bytesBefore;
    bool ok = *reinterpret_cast<std::uintptr_t*>(self) == 0x00740F28 && holder && described &&
              *reinterpret_cast<int*>(self + 0x14) == rect[2] - rect[0] &&
              *reinterpret_cast<int*>(self + 0x18) == rect[3] - rect[1] &&
              static_cast<int>(desc[3]) == rect[2] - rect[0] && static_cast<int>(desc[2]) == rect[3] - rect[1] &&
              (*reinterpret_cast<std::uint32_t*>(self + 0xC) & 4) && *reinterpret_cast<int*>(self + 0x1C) == 0 &&
              *reinterpret_cast<int*>(self + 0x20) == 0 && grown == desc[2] * desc[4];
    ei::verify::Check(name, true, ok, "%dx%d, total +%u", rect[2] - rect[0], rect[3] - rect[1], grown);
}

static void* s_uiSurface;
static char* __fastcall UiSurfaceConstructVerify(char* self, void*, void* ui, int left, int top, int right, int bottom,
                                                 int format, char scale) {
    std::uint32_t before = *reinterpret_cast<std::uint32_t*>(0x007C7E78);
    char* result = UiSurfaceConstruct(self, nullptr, ui, left, top, right, bottom, format, scale);
    CheckWindowSurface("UiSurfaceConstruct", self, before);
    return result;
}
EI_VERIFY(0x005FCC00, UiSurfaceConstructVerify, 0, s_uiSurface);

static void* s_uiSurfaceRect;
static char* __fastcall UiSurfaceConstructRectVerify(char* self, void*, void* ui, const int* rect, int format) {
    std::uint32_t before = *reinterpret_cast<std::uint32_t*>(0x007C7E78);
    char* result = UiSurfaceConstructRect(self, nullptr, ui, rect, format);
    CheckWindowSurface("UiSurfaceConstructRect", self, before);
    return result;
}
EI_VERIFY(0x005FCDD0, UiSurfaceConstructRectVerify, 0, s_uiSurfaceRect);
