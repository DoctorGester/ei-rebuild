// Checks for the surfaces' pixel work (ui_surface_pixels.cpp).
//
// CaptureScreen and Greyscale run side by side on the surface's pixels: they are read (DirectDraw
// lock), the original runs, its pixels are read and the old ones put back, ours runs, and both
// pixel sets must be equal. Rgb565Copy runs twice on the same surface and the two blocks must be
// equal (ours is freed, the original's returned).
#include "verify.h"
#include "memory_texture.h"

#include "../../src/game/dx_surface.h"
#include "../../src/game/ui.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"
#include "../../src/lib/gameapi.h"
#include "../../src/render/render.h"

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

// The surface's holder.
void* HolderOf(CUiSurface* surface) {
    return *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(surface) + 0x10);
}

// The surface's holder has no DirectDraw surface (made in the switch-over with "cpuwindows", ticket
// R7): its pixels are only in the new renderer's memory, and the originals (which lock the
// DirectDraw surface) cannot run on it.
bool MemoryOnly(CUiSurface* surface) { return HolderOf(surface) && !DirectDrawSurface(surface); }

struct Pixels {
    int height = 0;
    int rowBytes = 0;
    std::vector<std::uint8_t> bytes;
};

CRITICAL_SECTION* SurfaceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// Reads the surface (or its `rect`) into `pixels`, or with `write` stores them back. False when it
// cannot be locked.
bool AccessSurface(IDirectDrawSurface7* dd, Pixels& pixels, bool write, RECT* rect = nullptr) {
    if (!dd) return false;
    gameapi::EnterCriticalSection(SurfaceLock());
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    bool ok = dd->Lock(rect, &desc, DDLOCK_WAIT, nullptr) == DD_OK;
    if (ok) {
        auto* base = static_cast<std::uint8_t*>(desc.lpSurface);
        if (rect) {
            desc.dwWidth = static_cast<DWORD>(rect->right - rect->left);
            desc.dwHeight = static_cast<DWORD>(rect->bottom - rect->top);
        }
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
        dd->Unlock(rect);
    }
    gameapi::LeaveCriticalSection(SurfaceLock());
    return ok;
}

// The same on the new renderer's memory of `holder` (a texture holder with no DirectDraw surface in
// the switch-over, ticket R8). False without such memory or for a rectangle outside it.
bool AccessMemory(const void* holder, Pixels& pixels, bool write, const RECT* rect = nullptr) {
    std::uint32_t desc[0x1F];
    if (!holder || !ClientPixels(holder, desc)) return false;
    int bytes = static_cast<int>(desc[0x15] / 8), pitch = static_cast<int>(desc[4]);
    RECT area = {0, 0, static_cast<LONG>(desc[3]), static_cast<LONG>(desc[2])};
    if (rect) {
        if (rect->left < 0 || rect->top < 0 || rect->right > area.right || rect->bottom > area.bottom ||
            rect->left >= rect->right || rect->top >= rect->bottom)
            return false;
        area = *rect;
    }
    auto* base = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[9])) + area.top * pitch +
                 area.left * bytes;
    int rowBytes = static_cast<int>(area.right - area.left) * bytes;
    int height = static_cast<int>(area.bottom - area.top);
    if (!write) {
        pixels.height = height;
        pixels.rowBytes = rowBytes;
        pixels.bytes.resize(static_cast<std::size_t>(rowBytes) * height);
        for (int y = 0; y < height; ++y) std::memcpy(&pixels.bytes[y * rowBytes], base + y * pitch, rowBytes);
    } else if (pixels.rowBytes == rowBytes && pixels.height == height) {
        for (int y = 0; y < height; ++y) std::memcpy(base + y * pitch, &pixels.bytes[y * rowBytes], rowBytes);
    }
    return true;
}

// A texture's pixels: its holder's DirectDraw surface, or the memory of a holder without one.
bool ReadTexture(const void* texture, Pixels& pixels) {
    CDXSurface* holder = memory_texture::HolderOf(texture);
    if (!holder) return false;
    if (holder->surface) return AccessSurface(static_cast<IDirectDrawSurface7*>(holder->surface), pixels, false);
    return AccessMemory(holder, pixels, false);
}

bool AccessPixels(CUiSurface* surface, Pixels& pixels, bool write) {
    return AccessSurface(surface ? DirectDrawSurface(surface) : nullptr, pixels, write);
}

int FirstDifferentRow(const Pixels& a, const Pixels& b) {
    if (a.bytes.size() != b.bytes.size() || a.rowBytes != b.rowBytes) return -2;
    for (int y = 0; y < a.height; ++y)
        if (std::memcmp(&a.bytes[y * a.rowBytes], &b.bytes[y * b.rowBytes], a.rowBytes) != 0) return y;
    return -1;
}

// 00691CF0 (thiscall on the texture manager): a loaded image by file name.
__declspec(naked) std::uint8_t* __fastcall FindImageAt(void*, void*, const char*) EI_ORIGINAL(0x00691CF0)

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
    // With the new renderer on, ours takes the copy from the new renderer's frame by design
    // (render::CopyFrameTo), not from the old render target: nothing to compare with the original.
    if (render::Active()) {
        self->CaptureScreen();
        return;
    }
    CompareDrawing("CUiSurface::CaptureScreen", self, [&] { s_captureScreen(self); }, [&] { self->CaptureScreen(); });
}
EI_VERIFY(0x005FD3D0, CaptureScreenVerify, 5, s_captureScreen);

namespace {
// The first pixel of the memory `desc` describes that is not grey as Greyscale makes it (5:6:5: red
// and blue equal, green twice them; 5:5:5 and 8:8:8: the three equal), -1 when all are (or the
// format is another).
int FirstColouredPixel(const std::uint32_t* desc) {
    std::uint32_t width = desc[3], height = desc[2], pitch = desc[4], bits = desc[0x15];
    std::uint32_t red = desc[0x16], green = desc[0x17], blue = desc[0x18];
    const auto* base = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* at = base + y * pitch + x * (bits / 8);
            bool grey = true;
            if (bits == 16) {
                std::uint32_t p = static_cast<std::uint32_t>(at[0] | (at[1] << 8));
                if (red == 0xF800 && green == 0x7E0 && blue == 0x1F)
                    grey = (p >> 11) == (p & 0x1F) && ((p >> 5) & 0x3F) == 2 * (p & 0x1F);
                else if (red == 0x7C00 && green == 0x3E0 && blue == 0x1F)
                    grey = ((p >> 10) & 0x1F) == (p & 0x1F) && ((p >> 5) & 0x1F) == (p & 0x1F);
            } else if (bits == 32 && red == 0xFF0000 && green == 0xFF00 && blue == 0xFF) {
                grey = at[0] == at[1] && at[1] == at[2];
            }
            if (!grey) return static_cast<int>(y * width + x);
        }
    }
    return -1;
}
} // namespace

// 005FE440: SUB ESP,0xB8 (6). For a surface whose holder has no DirectDraw surface (the switch-over,
// ticket R7) ours only: every pixel grey afterwards.
static void(__thiscall* s_greyscale)(CUiSurface*);
static void __fastcall GreyscaleVerify(CUiSurface* self, void*) {
    if (MemoryOnly(self)) {
        self->Greyscale();
        std::uint32_t desc[0x1F];
        int pixel = ClientPixels(HolderOf(self), desc) ? FirstColouredPixel(desc) : -2;
        ei::verify::Check("CUiSurface::Greyscale", true, pixel == -1, "memory only: pixel %d not grey", pixel);
        return;
    }
    CompareDrawing("CUiSurface::Greyscale", self, [&] { s_greyscale(self); }, [&] { self->Greyscale(); });
}
EI_VERIFY(0x005FE440, GreyscaleVerify, 6, s_greyscale);

// 005FD440: MOV EAX,FS:[0] (6). The surface itself is only read.
static std::uint16_t*(__thiscall* s_rgb565Copy)(CUiSurface*, int, int);
static std::uint16_t* __fastcall Rgb565CopyVerify(CUiSurface* self, void*, int width, int height) {
    // In the switch-over with game-owned memory ours stretches on the CPU (ui_surface_pixels.cpp
    // StretchCopy), which can pick a neighbouring pixel where DirectDraw's Blt does not (ratios
    // that are not whole numbers): nothing to compare with the original then (nor for a holder with
    // no DirectDraw surface, which the original cannot read).
    if ((render::Presenting() && render::CpuWindows()) || MemoryOnly(self)) return self->Rgb565Copy(width, height);
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

// 005FD240: SUB ESP,0xB4 (6). Only reads: both run, the results must be equal. A holder with no
// DirectDraw surface (the switch-over, ticket R7): ours only, its result a colour (0x00RRGGBB).
std::uint32_t __fastcall SurfacePixelAt(CUiSurface* self, void*, int x, int y);
static std::uint32_t(__thiscall* s_pixelAt)(CUiSurface*, int, int);
static std::uint32_t __fastcall PixelAtVerify(CUiSurface* self, void*, int x, int y) {
    if (MemoryOnly(self)) {
        std::uint32_t actual = SurfacePixelAt(self, nullptr, x, y);
        ei::verify::Check("CUiSurface::PixelAt", true, (actual >> 24) == 0, "memory only (%d, %d): %08X", x, y, actual);
        return actual;
    }
    std::uint32_t expected = s_pixelAt(self, x, y);
    std::uint32_t actual = SurfacePixelAt(self, nullptr, x, y);
    ei::verify::Check("CUiSurface::PixelAt", true, expected == actual, "(%d, %d): %06X, ours %06X", x, y, expected, actual);
    return expected;
}
EI_VERIFY(0x005FD240, PixelAtVerify, 6, s_pixelAt);

// 005EC020: PUSH -1; PUSH imm32 (7). Both make a texture of the same name; their surfaces' pixels
// must be equal. Ours is released (its slot 4, as the original does on failure), the original's
// returned. In the switch-over the texture manager's textures have only the new renderer's memory
// (ticket R8): the original's is lent a DirectDraw texture while it runs (LendNewTextures, its
// pixels then moved into the memory), ours writes the memory; both are read from the memory.
static void*(__fastcall* s_figureTexture)(const char*);
static void* __fastcall FigureTextureVerify(const char* name) {
    if (!memory_texture::CanLend()) return ItemFigureTexture(name);   // no DirectDraw: ours only
    void* expected;
    {
        memory_texture::LendNewTextures lend;
        expected = s_figureTexture(name);
    }
    void* actual = ItemFigureTexture(name);
    int row = -3;
    if ((expected == nullptr) != (actual == nullptr)) {
        row = -4;
    } else if (!expected) {
        row = -1;
    } else {
        // Only the part written: the image's rows at the bottom, its width (a new surface's other
        // pixels are whatever DirectDraw left there).
        int width = 0x100, height = 0x100;
        mfc::CString file = mfc::CString(name) + ".mmp";
        void* manager = *reinterpret_cast<void* const*>(0x007AF744);
        if (std::uint8_t* image = FindImageAt(manager, nullptr, file)) {
            std::memcpy(&width, image + 4, 4);
            std::memcpy(&height, image + 8, 4);
        }
        Pixels a, b;
        if (ReadTexture(expected, a) && ReadTexture(actual, b)) {
            row = a.bytes.size() == b.bytes.size() && a.rowBytes == b.rowBytes ? -1 : -2;
            for (int y = a.height - height; row == -1 && y < a.height; ++y)
                if (y >= 0 && std::memcmp(&a.bytes[y * a.rowBytes], &b.bytes[y * b.rowBytes], width * 2 < a.rowBytes ? width * 2 : a.rowBytes) != 0) row = y;
        }
    }
    ei::verify::Check("ItemFigureTexture", true, row == -1, "%s: pixel row %d (made %d/%d)", name, row, expected != nullptr,
                      actual != nullptr);
    if (actual) {
        using ReleaseFn = void(__thiscall*)(void*);
        reinterpret_cast<ReleaseFn>((*static_cast<void***>(actual))[4])(actual);
    }
    return expected;
}
EI_VERIFY(0x005EC020, FigureTextureVerify, 7, s_figureTexture);

// 005E5C40: SUB ESP,0x1AC (6). Side by side on the target surface's `to` rectangle (the only pixels
// written), holding the device lock across both runs (the cursor thread calls it too).
static void(__thiscall* s_cursorBlit)(CCursor*, CursorImage*, RECT*, RECT*);
static void __fastcall CursorBlitVerify(CCursor* self, void*, CursorImage* image, RECT* to, RECT* from) {
    gameapi::EnterCriticalSection(SurfaceLock());
    IDirectDrawSurface7* target =
        self->target_ ? static_cast<IDirectDrawSurface7*>(self->target_->surface) : nullptr;
    RECT area = *to;
    Pixels before;
    bool pixels = AccessSurface(target, before, false, &area);
    s_cursorBlit(self, image, to, from);
    long expectedResult = self->target_->result;
    Pixels expected;
    if (pixels) pixels = AccessSurface(target, expected, false, &area) && AccessSurface(target, before, true, &area);
    self->Blit(image, to, from);
    int row = -1;
    if (pixels) {
        Pixels actual;
        if (AccessSurface(target, actual, false, &area)) row = FirstDifferentRow(expected, actual);
    }
    long actualResult = self->target_->result;
    gameapi::LeaveCriticalSection(SurfaceLock());
    ei::verify::Check("CCursor::Blit", true, row == -1 && expectedResult == actualResult,
                      "pixel row %d (compared %d), result %ld/%ld", row, pixels, expectedResult, actualResult);
}
EI_VERIFY(0x005E5C40, CursorBlitVerify, 6, s_cursorBlit);

namespace {
// The surface's memory as the game copies it: pitch * height bytes from the top left.
bool RawBytes(IDirectDrawSurface7* dd, std::vector<std::uint8_t>& out) {
    if (!dd) return false;
    gameapi::EnterCriticalSection(SurfaceLock());
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    bool ok = dd->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK;
    if (ok) {
        std::size_t size = static_cast<std::size_t>(desc.dwHeight) * static_cast<std::uint32_t>(desc.lPitch);
        const auto* base = static_cast<const std::uint8_t*>(desc.lpSurface);
        out.assign(base, base + size);
        dd->Unlock(nullptr);
    }
    gameapi::LeaveCriticalSection(SurfaceLock());
    return ok;
}
template <typename T>
T Field(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
// The window's pixels as the game copies them (RawBytes), from the new renderer's memory when its
// holder has no DirectDraw surface (ticket R7).
bool WindowBytes(CUiSurface* surface, std::vector<std::uint8_t>& out) {
    if (!MemoryOnly(surface)) return RawBytes(DirectDrawSurface(surface), out);
    std::uint32_t desc[0x1F];
    if (!ClientPixels(HolderOf(surface), desc)) return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
    out.assign(base, base + static_cast<std::size_t>(desc[2]) * desc[4]);
    return true;
}
} // namespace

// 005FD7A0: PUSH -1; PUSH imm32 (7). Ours only (saving releases the surface, so it cannot run
// twice): saving must keep exactly the surface's bytes and release it; restoring must put the
// kept bytes back and free them; both with the memory totals moved by the same sizes.
static void(__thiscall* s_swapContents)(CUiSurface*, int);
static void __fastcall SwapContentsVerify(CUiSurface* self, void*, int restore) {
    auto& kept = *reinterpret_cast<std::uint32_t*>(0x007C7E74);
    auto& surfaceBytes = *reinterpret_cast<std::uint32_t*>(0x007C7E78);
    std::uint32_t keptBefore = kept, surfaceBefore = surfaceBytes;
    if (!restore) {
        std::vector<std::uint8_t> raw;
        bool had = HolderOf(self) != nullptr && WindowBytes(self, raw);
        self->SwapContents(restore);
        if (!had) return;
        const std::uint8_t* data = Field<const std::uint8_t*>(self, 0x1C);
        std::uint32_t size = Field<std::uint32_t>(self, 0x20);
        bool ok = size == raw.size() && data && std::memcmp(data, raw.data(), size) == 0 &&
                  Field<void*>(self, 0x10) == nullptr && kept - keptBefore == size && surfaceBefore - surfaceBytes == size;
        ei::verify::Check("CUiSurface::SwapContents", true, ok, "save: %u bytes (surface %u), totals %u/%u", size,
                          static_cast<unsigned>(raw.size()), kept - keptBefore, surfaceBefore - surfaceBytes);
        return;
    }
    bool created = Field<void*>(self, 0x10) == nullptr;
    const std::uint8_t* data = Field<const std::uint8_t*>(self, 0x1C);
    std::uint32_t size = Field<std::uint32_t>(self, 0x20);
    std::vector<std::uint8_t> keptCopy;
    if (data) keptCopy.assign(data, data + size);
    self->SwapContents(restore);
    std::vector<std::uint8_t> raw;
    bool read = HolderOf(self) != nullptr && WindowBytes(self, raw);
    bool ok = read && Field<void*>(self, 0x10) != nullptr;
    if (data) {
        ok = ok && raw.size() >= size && std::memcmp(raw.data(), keptCopy.data(), size) == 0 &&
             Field<void*>(self, 0x1C) == nullptr && Field<std::uint32_t>(self, 0x20) == 0 && keptBefore - kept == size;
    }
    if (created) ok = ok && surfaceBytes - surfaceBefore == raw.size();
    ei::verify::Check("CUiSurface::SwapContents", true, ok, "restore: kept %u bytes, surface %u, created %d", size,
                      static_cast<unsigned>(raw.size()), created);
}
EI_VERIFY(0x005FD7A0, SwapContentsVerify, 7, s_swapContents);

// 005FFFB0: PUSH -1; PUSH imm32 (7). Side by side on the written rectangle of the texture's surface
// and the result. A texture with only the new renderer's memory (the switch-over, ticket R8) is lent
// a DirectDraw texture holding the memory's pixels for the original's run (the memory itself keeps
// the pixels from before), then ours writes the memory.
static int(__thiscall* s_copyImage)(CUiWindow*, CEngineObject*, const char*, int, int, int, int, int, int, int, int);
static int __fastcall CopyImageVerify(CUiWindow* self, void*, CEngineObject* texture, const char* name, int imageWidth,
                                      int imageHeight, int width, int height, int x, int y, int unused1, int unused2) {
    CDXSurface* holder = memory_texture::HolderOf(texture);
    bool lent = memory_texture::Lend(holder);
    if (!lent && memory_texture::MemoryOnly(holder))   // DirectDraw refused (logged): the original cannot run
        return self->CopyImage(texture, name, imageWidth, imageHeight, width, height, x, y, unused1, unused2);
    IDirectDrawSurface7* dd = holder ? static_cast<IDirectDrawSurface7*>(holder->surface) : nullptr;
    RECT area = {x, y, x + width, y + height};
    Pixels before;
    bool pixels = width > 0 && height > 0 && AccessSurface(dd, before, false, &area);
    int expected = s_copyImage(self, texture, name, imageWidth, imageHeight, width, height, x, y, unused1, unused2);
    Pixels expectedPixels;
    if (pixels) pixels = AccessSurface(dd, expectedPixels, false, &area) && AccessSurface(dd, before, true, &area);
    if (lent) memory_texture::TakeBack(holder, false);
    int actual = self->CopyImage(texture, name, imageWidth, imageHeight, width, height, x, y, unused1, unused2);
    int row = -1;
    if (pixels) {
        Pixels actualPixels;
        bool read = lent ? AccessMemory(holder, actualPixels, false, &area) : AccessSurface(dd, actualPixels, false, &area);
        if (read) row = FirstDifferentRow(expectedPixels, actualPixels);
    }
    ei::verify::Check("CUiWindow::CopyImage", true, row == -1 && expected == actual,
                      "%s at (%d, %d) %dx%d: pixel row %d (compared %d), result %d/%d", name, x, y, width, height, row,
                      pixels, expected, actual);
    return expected;
}
EI_VERIFY(0x005FFFB0, CopyImageVerify, 7, s_copyImage);
