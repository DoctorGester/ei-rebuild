// Checks for the off-screen surface holder (dx_surface.cpp) and the window surface's DC
// (ui_surface_dc.cpp). Ours only (DirectDraw objects, GDI), except the picture loader 004F0060,
// which runs the original on a scratch holder.
#include "verify.h"

#include "../../src/game/dx_surface.h"
#include "../../src/lib/mfc.h"
#include "../../src/render/render.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

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

// --- 004F0060 ---------------------------------------------------------------------------------------

namespace {

template <typename... Args>
long SurfaceCall(void* surface, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(surface))[slot])(surface, args...);
}

// A holder's pixels for reading: its game-owned memory (the DIB section; GDI flushed) when it has
// some, else a read-only Lock (`locked` set; Unlock afterwards).
const std::uint8_t* PixelsOf(char* holder, long* pitch, void** locked) {
    *locked = nullptr;
    if (HBITMAP bitmap = ClientBitmap(holder)) {
        DIBSECTION section{};
        ::GdiFlush();
        if (::GetObjectW(bitmap, sizeof(section), &section) != sizeof(section) || !section.dsBm.bmBits) return nullptr;
        *pitch = section.dsBm.bmWidthBytes;
        return static_cast<const std::uint8_t*>(section.dsBm.bmBits);
    }
    void* surface = *reinterpret_cast<void**>(holder + 0x10);
    if (!surface) return nullptr;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    // DDLOCK_WAIT | DDLOCK_READONLY
    if (SurfaceCall(surface, 25, static_cast<RECT*>(nullptr), static_cast<void*>(desc), 0x11ul, static_cast<void*>(nullptr)) != 0)
        return nullptr;
    *locked = surface;
    *pitch = static_cast<long>(desc[4]);
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
}

// The two holders' surfaces hold the same rows of `rowBytes` bytes.
bool SamePixels(char* expected, char* actual, int rowBytes, int height, int* firstRow) {
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
    ::EnterCriticalSection(lock);
    long expectedPitch = 0, actualPitch = 0;
    void* expectedLocked = nullptr;
    void* actualLocked = nullptr;
    const std::uint8_t* e = PixelsOf(expected, &expectedPitch, &expectedLocked);
    const std::uint8_t* a = PixelsOf(actual, &actualPitch, &actualLocked);
    bool same = e && a;
    *firstRow = same ? -1 : -2;
    for (int y = 0; same && y < height; ++y) {
        if (std::memcmp(e + static_cast<std::ptrdiff_t>(y) * expectedPitch, a + static_cast<std::ptrdiff_t>(y) * actualPitch,
                        static_cast<std::size_t>(rowBytes)) != 0) {
            same = false;
            *firstRow = y;
        }
    }
    if (expectedLocked) SurfaceCall(expectedLocked, 32, static_cast<RECT*>(nullptr));
    if (actualLocked) SurfaceCall(actualLocked, 32, static_cast<RECT*>(nullptr));
    ::LeaveCriticalSection(lock);
    return same;
}

} // namespace

// 004F0060 (dx_surface.cpp): when it makes the surface (a holder without one, a device, an 8888
// picture: the path 004EFC60 takes) against the original on a scratch copy of the holder: the
// result, the holder's +8 / +0x14 / +0x18 / +0x1C and the pixels (the original's surface through a
// read-only Lock; ours from its game-owned memory when it has some, else a Lock), then the scratch
// surface is released. With "cpuwindows" a system-memory request must have got game-owned memory.
// Other calls (errors, a holder with a surface: only the dead 004EFF30 / 004EFFF0) ours only.
int __fastcall DXSurfaceLoadPicture(char* holder, void*, const void* data, int flag);
static void* s_loadPicture;
static int __fastcall DXSurfaceLoadPictureVerify(char* holder, void*, const void* data, int flag) {
    const auto* header = static_cast<const std::uint32_t*>(data);
    bool compare = header && header[4] == 0x8888 && !*reinterpret_cast<void**>(holder + 0x10) &&
                   *reinterpret_cast<char**>(holder + 0xC);
    if (!compare) return DXSurfaceLoadPicture(holder, nullptr, data, flag);
    alignas(4) char scratch[0x20];
    std::memcpy(scratch, holder, sizeof(scratch));
    using Fn = int(__fastcall*)(char*, void*, const void*, int);
    int expected = reinterpret_cast<Fn>(s_loadPicture)(scratch, nullptr, data, flag);
    int actual = DXSurfaceLoadPicture(holder, nullptr, data, flag);
    auto field = [](const char* h, int offset) { return *reinterpret_cast<const std::uint32_t*>(h + offset); };
    bool fields = expected == actual && field(scratch, 8) == field(holder, 8) && field(scratch, 0x14) == field(holder, 0x14) &&
                  field(scratch, 0x18) == field(holder, 0x18) && field(scratch, 0x1C) == field(holder, 0x1C);
    int width = static_cast<int>(header[1]), height = static_cast<int>(header[2]);
    int bytes = static_cast<int>(field(holder, 0x1C) / 8);
    int firstRow = -1;
    bool pixels = expected != 1 || actual != 1 || (bytes != 2 && bytes != 4) ||
                  SamePixels(scratch, holder, width * bytes, height, &firstRow);
    bool wantsClient = actual == 1 && (flag & 0xFF) == 0 && render::CpuWindows();
    bool client = ClientBitmap(holder) != nullptr;
    ei::verify::Check("DXSurfaceLoadPicture", true, fields && pixels && (client || !wantsClient),
                      "%dx%d flag %d: result %d/%d, bits %u/%u, pixels %s (row %d), game memory %d/%d", width, height,
                      flag & 0xFF, expected, actual, field(scratch, 0x1C), field(holder, 0x1C), pixels ? "same" : "differ",
                      firstRow, wantsClient ? 1 : 0, client ? 1 : 0);
    if (void* surface = *reinterpret_cast<void**>(scratch + 0x10)) SurfaceCall(surface, 2);   // Release
    return actual;
}
EI_VERIFY(0x004F0060, DXSurfaceLoadPictureVerify, 6, s_loadPicture);

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

// The surface's height * pitch (GetSurfaceDesc); 0 without one.
static std::uint32_t SurfaceBytes(char* holder) {
    void* surface = holder ? *reinterpret_cast<void**>(holder + 0x10) : nullptr;
    if (!surface) return 0;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    if (reinterpret_cast<long(__stdcall*)(void*, void*)>((*static_cast<void***>(surface))[22])(surface, desc) != 0) return 0;
    return desc[2] * desc[4];
}

// 005FD120 (ui_surface_create.cpp): no holder, an empty screen rectangle, nothing kept, the totals
// down by the kept bytes and the surface's height * pitch.
void __fastcall UiSurfaceRelease(char* self);
static void* s_uiSurfaceRelease;
static void __fastcall UiSurfaceReleaseVerify(char* self) {
    auto& kept = *reinterpret_cast<std::uint32_t*>(0x007C7E74);
    auto& bytes = *reinterpret_cast<std::uint32_t*>(0x007C7E78);
    std::uint32_t keptBefore = kept, bytesBefore = bytes;
    std::uint32_t keptSize = *reinterpret_cast<void**>(self + 0x1C) ? *reinterpret_cast<std::uint32_t*>(self + 0x20) : 0;
    std::uint32_t surfaceSize = SurfaceBytes(*reinterpret_cast<char**>(self + 0x10));
    UiSurfaceRelease(self);
    const int* rect = reinterpret_cast<const int*>(self + 0x24);
    bool ok = *reinterpret_cast<void**>(self + 0x10) == nullptr && *reinterpret_cast<void**>(self + 0x1C) == nullptr &&
              rect[0] == 0 && rect[1] == 0 && rect[2] == 0 && rect[3] == 0 && keptBefore - kept == keptSize &&
              bytesBefore - bytes == surfaceSize;
    ei::verify::Check("UiSurfaceRelease", true, ok, "kept -%u (%u), surface -%u (%u)", keptBefore - kept, keptSize,
                      bytesBefore - bytes, surfaceSize);
}
EI_VERIFY(0x005FD120, UiSurfaceReleaseVerify, 0, s_uiSurfaceRelease);

// 005FCF80: the vtable, a holder of the picture's size, the screen rectangle (0, 0, width, height),
// flag 1, nothing kept, the surface-bytes total up by height * pitch.
char* __fastcall UiSurfaceFromPicture(char* self, void*, void* ui, mfc::CString name);
static void* s_uiSurfacePicture;
static char* __fastcall UiSurfaceFromPictureVerify(char* self, void*, void* ui, mfc::CString name) {
    std::uint32_t before = *reinterpret_cast<std::uint32_t*>(0x007C7E78);
    char* result = UiSurfaceFromPicture(self, nullptr, ui, name);
    char* holder = *reinterpret_cast<char**>(self + 0x10);
    const int* rect = reinterpret_cast<const int*>(self + 0x24);
    int width = holder ? *reinterpret_cast<int*>(holder + 0x14) : -1;
    int height = holder ? *reinterpret_cast<int*>(holder + 0x18) : -1;
    std::uint32_t grown = *reinterpret_cast<std::uint32_t*>(0x007C7E78) - before;
    bool ok = result == self && *reinterpret_cast<std::uintptr_t*>(self) == 0x00740F28 && holder &&
              *reinterpret_cast<int*>(self + 0x14) == width && *reinterpret_cast<int*>(self + 0x18) == height &&
              rect[0] == 0 && rect[1] == 0 && rect[2] == width && rect[3] == height &&
              (*reinterpret_cast<std::uint32_t*>(self + 0xC) & 1) && *reinterpret_cast<int*>(self + 0x1C) == 0 &&
              grown == SurfaceBytes(holder);
    ei::verify::Check("UiSurfaceFromPicture", true, ok, "%dx%d, total +%u", width, height, grown);
    return result;
}
EI_VERIFY(0x005FCF80, UiSurfaceFromPictureVerify, 0, s_uiSurfacePicture);
