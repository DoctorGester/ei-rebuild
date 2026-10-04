// Frame capture for the harness (verify build only): "capture <name>" writes the next presented
// frame to <output folder>\frames\<name>.bmp. See harness_capture.h and docs/notes/renderer.md.
//
// Where: the frame function 00458010 draws the scene (00503F40 BeginScene ... 005040A0 EndScene),
// the escape menu, then, when it presents, enters the software cursor's lock (005E65F0, called at
// 004580D7), blits the software cursor into the render target (005E54A0), flips (004EF610: Flip in
// full screen, BltFast to the primary in a window) and leaves the cursor lock (005E6610). The call
// at 004580D7 is redirected here: the render target then holds the finished frame without the
// software cursor (the hardware cursor is never in a surface), and no scene is open, so it may be
// locked.
//
// How: the render target (device object 007AF080 +0x18, its IDirectDrawSurface7 at +0x10) is
// locked read-only under the device lock 007AEE78 (the cursor thread blits under it too) and its
// pixels are converted from the surface's own format (any RGB masks: 5:6:5, 5:5:5, X8R8G8B8, ...)
// to 8-bit BGR. If the lock fails, GetDC + BitBlt into a DIB section is tried. A screen or window
// capture through GDI is not used: with dgVoodoo the window shows a DXGI swap chain, which GDI
// reads back as black, and in full screen the window is not on the desktop at all.
#include "harness_capture.h"

#include "../../src/hook/log.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ei::verify::capture {

namespace {

constexpr std::uintptr_t kCursorLockCall = 0x004580D7;   // CALL 005E65F0 in 00458010
constexpr std::uintptr_t kCursorLock = 0x005E65F0;       // CCursor::Lock (__fastcall, ECX = 007C7BD8)
constexpr std::uintptr_t kDeviceObject = 0x007AF080;     // the renderer's (007AF060) +0x20

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

volatile unsigned g_frames = 0;
State g_state = State::Idle;
int g_skip = 0;
std::string g_name;
std::string g_detail;

// One colour channel of a surface format: its mask, the position of its lowest bit and its width.
struct Channel {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
};

Channel MakeChannel(std::uint32_t mask) {
    Channel c;
    c.mask = mask;
    if (!mask) return c;
    while (!((mask >> c.shift) & 1)) ++c.shift;
    while (c.shift + c.bits < 32 && ((mask >> (c.shift + c.bits)) & 1)) ++c.bits;
    return c;
}

// The channel's value as 8 bits: wider channels keep their top 8 bits, narrower ones repeat their
// bit pattern (5 bits: abcde -> abcdeabc), so white stays 255 and black 0.
std::uint8_t Expand(std::uint32_t pixel, const Channel& c) {
    if (!c.bits) return 0;
    std::uint32_t v = (pixel & c.mask) >> c.shift;
    if (c.bits >= 8) return static_cast<std::uint8_t>(v >> (c.bits - 8));
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += c.bits) out |= (v << (8 - c.bits)) >> filled;
    return static_cast<std::uint8_t>(out);
}

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgr;   // top-down rows of width * 3 bytes
};

// Reads the surface through a read-only lock. False (with `why`) when it cannot be locked or has
// a format that is not RGB.
bool ReadLocked(IDirectDrawSurface7* surface, Image& image, std::string& how, std::string& why) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    HRESULT hr = surface->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_READONLY, nullptr);
    if (FAILED(hr)) {
        char text[64];
        std::snprintf(text, sizeof(text), "Lock failed (%08lX)", static_cast<unsigned long>(hr));
        why = text;
        return false;
    }
    const DDPIXELFORMAT& pf = desc.ddpfPixelFormat;
    int bytes = static_cast<int>(pf.dwRGBBitCount / 8);
    bool ok = (pf.dwFlags & DDPF_RGB) && bytes >= 2 && bytes <= 4 && desc.lpSurface && desc.dwWidth && desc.dwHeight;
    if (ok) {
        Channel r = MakeChannel(pf.dwRBitMask), g = MakeChannel(pf.dwGBitMask), b = MakeChannel(pf.dwBBitMask);
        image.width = static_cast<int>(desc.dwWidth);
        image.height = static_cast<int>(desc.dwHeight);
        image.bgr.resize(static_cast<std::size_t>(image.width) * image.height * 3);
        for (int y = 0; y < image.height; ++y) {
            const std::uint8_t* row = static_cast<const std::uint8_t*>(desc.lpSurface) + static_cast<std::size_t>(y) * desc.lPitch;
            std::uint8_t* out = &image.bgr[static_cast<std::size_t>(y) * image.width * 3];
            for (int x = 0; x < image.width; ++x) {
                std::uint32_t pixel = 0;
                std::memcpy(&pixel, row + x * bytes, bytes);
                out[x * 3 + 0] = Expand(pixel, b);
                out[x * 3 + 1] = Expand(pixel, g);
                out[x * 3 + 2] = Expand(pixel, r);
            }
        }
        char text[128];
        std::snprintf(text, sizeof(text), "lock, %d-bit R %08lX G %08lX B %08lX", bytes * 8,
                      static_cast<unsigned long>(pf.dwRBitMask), static_cast<unsigned long>(pf.dwGBitMask),
                      static_cast<unsigned long>(pf.dwBBitMask));
        how = text;
    } else {
        char text[96];
        std::snprintf(text, sizeof(text), "unsupported format (flags %08lX, %lu bits)", static_cast<unsigned long>(pf.dwFlags),
                      static_cast<unsigned long>(pf.dwRGBBitCount));
        why = text;
    }
    surface->Unlock(nullptr);
    return ok;
}

// Reads the surface through its DC into a 24-bit DIB section.
bool ReadThroughDc(IDirectDrawSurface7* surface, Image& image, std::string& how, std::string& why) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    if (FAILED(surface->GetSurfaceDesc(&desc)) || !desc.dwWidth || !desc.dwHeight) {
        why += "; GetSurfaceDesc failed";
        return false;
    }
    HDC dc = nullptr;
    HRESULT hr = surface->GetDC(&dc);
    if (FAILED(hr) || !dc) {
        char text[64];
        std::snprintf(text, sizeof(text), "; GetDC failed (%08lX)", static_cast<unsigned long>(hr));
        why += text;
        return false;
    }
    image.width = static_cast<int>(desc.dwWidth);
    image.height = static_cast<int>(desc.dwHeight);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 24;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP dib = memory ? CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    bool ok = false;
    if (dib && bits) {
        HGDIOBJ old = SelectObject(memory, dib);
        ok = BitBlt(memory, 0, 0, image.width, image.height, dc, 0, 0, SRCCOPY) != FALSE;
        GdiFlush();
        SelectObject(memory, old);
        if (ok) {
            int stride = (image.width * 3 + 3) & ~3;
            image.bgr.resize(static_cast<std::size_t>(image.width) * image.height * 3);
            for (int y = 0; y < image.height; ++y)
                std::memcpy(&image.bgr[static_cast<std::size_t>(y) * image.width * 3],
                            static_cast<const std::uint8_t*>(bits) + static_cast<std::size_t>(y) * stride, image.width * 3);
            how = "GetDC";
        } else {
            why += "; BitBlt failed";
        }
    } else {
        why += "; no DIB section";
    }
    if (dib) DeleteObject(dib);
    if (memory) DeleteDC(memory);
    surface->ReleaseDC(dc);
    return ok;
}

// A 24-bit bottom-up BMP.
bool WriteBmp(const char* path, const Image& image) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    int stride = (image.width * 3 + 3) & ~3;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4D42;   // "BM"
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(stride) * image.height;
    info.biSize = sizeof(info);
    info.biWidth = image.width;
    info.biHeight = image.height;
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biCompression = BI_RGB;
    info.biSizeImage = static_cast<DWORD>(stride) * image.height;
    bool ok = std::fwrite(&file, sizeof(file), 1, f) == 1 && std::fwrite(&info, sizeof(info), 1, f) == 1;
    std::vector<std::uint8_t> row(stride, 0);
    for (int y = image.height - 1; ok && y >= 0; --y) {
        std::memcpy(row.data(), &image.bgr[static_cast<std::size_t>(y) * image.width * 3], image.width * 3);
        ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    return std::fclose(f) == 0 && ok;
}

void Capture() {
    char* device = *reinterpret_cast<char**>(kDeviceObject);
    char* target = device ? *reinterpret_cast<char**>(device + 0x18) : nullptr;
    auto* surface = target ? *reinterpret_cast<IDirectDrawSurface7**>(target + 0x10) : nullptr;
    if (!surface) {
        g_state = State::Failed;
        g_detail = "no render target";
        return;
    }
    Image image;
    std::string how, why;
    EnterCriticalSection(DeviceLock());
    bool ok = ReadLocked(surface, image, how, why) || ReadThroughDc(surface, image, how, why);
    LeaveCriticalSection(DeviceLock());
    if (!ok) {
        g_state = State::Failed;
        g_detail = why;
        return;
    }
    char path[MAX_PATH];
    ei::OutputPath("frames", path);
    CreateDirectoryA(path, nullptr);
    std::size_t used = std::strlen(path);
    std::snprintf(path + used, MAX_PATH - used, "\\%s.bmp", g_name.c_str());
    if (!WriteBmp(path, image)) {
        g_state = State::Failed;
        g_detail = std::string("cannot write ") + path;
        return;
    }
    char text[MAX_PATH + 96];
    std::snprintf(text, sizeof(text), "%s (%dx%d, %s, frame %u)", path, image.width, image.height, how.c_str(), g_frames);
    g_detail = text;
    g_state = State::Done;
    // With the new renderer on (src/render), its frame of the same moment goes next to it.
    if (render::Active()) {
        render::EndFrame();
        Image next;
        std::snprintf(path + used, MAX_PATH - used, "\\%s.new.bmp", g_name.c_str());
        if (!render::ReadFrame(&next.width, &next.height, &next.bgr)) g_detail += "; the new renderer has no frame";
        else if (!WriteBmp(path, next)) g_detail += std::string("; cannot write ") + path;
        else g_detail += "; new renderer: " + std::string(path);
    }
}

// Replaces the call of 005E65F0 at 004580D7 (ECX = the cursor object, preserved for it).
void __fastcall CaptureThenCursorLock(void* cursor) {
    g_frames = g_frames + 1;
    if (g_state == State::Pending) {
        if (g_skip > 0) --g_skip;
        else Capture();
    }
    reinterpret_cast<void(__fastcall*)(void*)>(kCursorLock)(cursor);
}

} // namespace

bool Install() {
    static int installed = -1;
    if (installed >= 0) return installed != 0;
    auto* p = reinterpret_cast<unsigned char*>(kCursorLockCall);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || kCursorLockCall + 5 + rel != kCursorLock) {
        Log("harness: capture: unexpected code at %08X; frames cannot be captured", unsigned(kCursorLockCall));
        installed = 0;
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&CaptureThenCursorLock) - (kCursorLockCall + 5));
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    installed = 1;
    return true;
}

void Request(const char* name, int skip) {
    g_name = name;
    g_skip = skip;
    g_detail.clear();
    g_state = Install() ? State::Pending : State::Failed;
    if (g_state == State::Failed) g_detail = "the present point could not be hooked";
}

State Poll(const char** detail) {
    if (detail) *detail = g_detail.c_str();
    return g_state;
}

unsigned FramesPresented() { return g_frames; }

namespace {
// Reads a BMP this file wrote (24-bit, bottom-up) into `image` (top-down rows).
bool ReadBmp(const char* name, Image& image) {
    char path[MAX_PATH];
    ei::OutputPath("frames", path);
    std::size_t used = std::strlen(path);
    std::snprintf(path + used, MAX_PATH - used, "\\%s.bmp", name);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    bool ok = std::fread(&file, sizeof(file), 1, f) == 1 && std::fread(&info, sizeof(info), 1, f) == 1 &&
              file.bfType == 0x4D42 && info.biBitCount == 24 && info.biCompression == BI_RGB && info.biWidth > 0 &&
              info.biHeight > 0;
    if (ok) {
        image.width = info.biWidth;
        image.height = info.biHeight;
        int stride = (image.width * 3 + 3) & ~3;
        image.bgr.resize(static_cast<std::size_t>(image.width) * image.height * 3);
        std::vector<std::uint8_t> row(stride);
        ok = std::fseek(f, static_cast<long>(file.bfOffBits), SEEK_SET) == 0;
        for (int y = image.height - 1; ok && y >= 0; --y) {
            ok = std::fread(row.data(), 1, row.size(), f) == row.size();
            if (ok) std::memcpy(&image.bgr[static_cast<std::size_t>(y) * image.width * 3], row.data(), image.width * 3);
        }
    }
    std::fclose(f);
    return ok;
}
} // namespace

bool Compare(const char* a, const char* b, int tolerance, double* differing, int* largest, const char** why) {
    Image ia, ib;
    if (!ReadBmp(a, ia) || !ReadBmp(b, ib)) {
        *why = "a frame is missing or unreadable";
        return false;
    }
    if (ia.width != ib.width || ia.height != ib.height) {
        *why = "the frames differ in size";
        return false;
    }
    std::size_t count = 0;
    int worst = 0;
    for (std::size_t i = 0; i < ia.bgr.size(); i += 3) {
        int d = 0;
        for (int c = 0; c < 3; ++c) {
            int v = ia.bgr[i + c] - ib.bgr[i + c];
            if (v < 0) v = -v;
            if (v > d) d = v;
        }
        if (d > worst) worst = d;
        if (d > tolerance) ++count;
    }
    *differing = static_cast<double>(count) / (static_cast<double>(ia.width) * ia.height);
    *largest = worst;
    return true;
}

} // namespace ei::verify::capture
