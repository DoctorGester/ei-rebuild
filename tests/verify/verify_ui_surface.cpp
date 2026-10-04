// Checks for the surfaces' pixel work (ui_surface_pixels.cpp).
//
// CaptureScreen and Greyscale run side by side on the surface's pixels: they are read (DirectDraw
// lock), the original runs, its pixels are read and the old ones put back, ours runs, and both
// pixel sets must be equal. Rgb565Copy runs twice on the same surface and the two blocks must be
// equal (ours is freed, the original's returned).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/gameapi.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

IDirectDrawSurface7* DirectDrawSurface(CUiSurface* surface) {
    std::uint8_t* inner = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(surface) + 0x10);
    return inner ? *reinterpret_cast<IDirectDrawSurface7**>(inner + 0x10) : nullptr;
}

struct Pixels {
    int height = 0;
    int rowBytes = 0;
    std::vector<std::uint8_t> bytes;
};

CRITICAL_SECTION* SurfaceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// Reads the surface into `pixels`, or with `write` stores them back. False when it cannot be locked.
bool AccessPixels(CUiSurface* surface, Pixels& pixels, bool write) {
    IDirectDrawSurface7* dd = surface ? DirectDrawSurface(surface) : nullptr;
    if (!dd) return false;
    gameapi::EnterCriticalSection(SurfaceLock());
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    bool ok = dd->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK;
    if (ok) {
        auto* base = static_cast<std::uint8_t*>(desc.lpSurface);
        int rowBytes = static_cast<int>(desc.dwWidth * (desc.ddpfPixelFormat.dwRGBBitCount / 8));
        if (!write) {
            pixels.height = static_cast<int>(desc.dwHeight);
            pixels.rowBytes = rowBytes;
            pixels.bytes.resize(static_cast<std::size_t>(rowBytes) * desc.dwHeight);
            for (DWORD y = 0; y < desc.dwHeight; ++y)
                std::memcpy(&pixels.bytes[y * rowBytes], base + y * desc.lPitch, rowBytes);
        } else if (pixels.rowBytes == rowBytes && pixels.height == static_cast<int>(desc.dwHeight)) {
            for (int y = 0; y < pixels.height; ++y)
                std::memcpy(base + y * desc.lPitch, &pixels.bytes[y * rowBytes], rowBytes);
        }
        dd->Unlock(nullptr);
    }
    gameapi::LeaveCriticalSection(SurfaceLock());
    return ok;
}

int FirstDifferentRow(const Pixels& a, const Pixels& b) {
    if (a.bytes.size() != b.bytes.size() || a.rowBytes != b.rowBytes) return -2;
    for (int y = 0; y < a.height; ++y)
        if (std::memcmp(&a.bytes[y * a.rowBytes], &b.bytes[y * b.rowBytes], a.rowBytes) != 0) return y;
    return -1;
}

long ResultOf(CUiSurface* surface) {
    std::uint8_t* inner = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(surface) + 0x10);
    long result = 0;
    if (inner) std::memcpy(&result, inner + 8, 4);
    return result;
}

template <typename Original, typename Ours>
void CompareDrawing(const char* name, CUiSurface* surface, Original original, Ours ours) {
    Pixels before;
    bool pixels = AccessPixels(surface, before, false);
    original();
    long expectedResult = ResultOf(surface);
    Pixels expected;
    if (pixels) pixels = AccessPixels(surface, expected, false) && AccessPixels(surface, before, true);
    ours();
    int row = -1;
    if (pixels) {
        Pixels actual;
        if (AccessPixels(surface, actual, false)) row = FirstDifferentRow(expected, actual);
    }
    long actualResult = ResultOf(surface);
    ei::verify::Check(name, true, row == -1 && expectedResult == actualResult,
                      "pixel row %d (rows %d, row bytes %d, compared %d), result %ld/%ld", row, expected.height,
                      expected.rowBytes, pixels, expectedResult, actualResult);
}

} // namespace

// 005FD3D0: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5).
static void(__thiscall* s_captureScreen)(CUiSurface*);
static void __fastcall CaptureScreenVerify(CUiSurface* self, void*) {
    CompareDrawing("CUiSurface::CaptureScreen", self, [&] { s_captureScreen(self); }, [&] { self->CaptureScreen(); });
}
EI_VERIFY(0x005FD3D0, CaptureScreenVerify, 5, s_captureScreen);

// 005FE440: SUB ESP,0xB8 (6).
static void(__thiscall* s_greyscale)(CUiSurface*);
static void __fastcall GreyscaleVerify(CUiSurface* self, void*) {
    CompareDrawing("CUiSurface::Greyscale", self, [&] { s_greyscale(self); }, [&] { self->Greyscale(); });
}
EI_VERIFY(0x005FE440, GreyscaleVerify, 6, s_greyscale);

// 005FD440: MOV EAX,FS:[0] (6). The surface itself is only read.
static std::uint16_t*(__thiscall* s_rgb565Copy)(CUiSurface*, int, int);
static std::uint16_t* __fastcall Rgb565CopyVerify(CUiSurface* self, void*, int width, int height) {
    std::uint16_t* expected = s_rgb565Copy(self, width, height);
    std::uint16_t* actual = self->Rgb565Copy(width, height);
    int count = width * height;
    int first = -1;
    if ((expected == nullptr) != (actual == nullptr)) {
        first = -2;
    } else if (expected) {
        for (int i = 0; i < count; ++i)
            if (expected[i] != actual[i]) {
                first = i;
                break;
            }
    }
    ei::verify::Check("CUiSurface::Rgb565Copy", true, first == -1, "%d x %d: first different pixel %d", width, height,
                      first);
    if (actual) gamecrt::operator_delete(actual);
    return expected;
}
EI_VERIFY(0x005FD440, Rgb565CopyVerify, 6, s_rgb565Copy);
