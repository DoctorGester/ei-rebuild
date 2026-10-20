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
#include "harness_hooks.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "ei/main_loop.h"
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
    // The image (the new renderer presents by itself): the old render target holds nothing worth
    // keeping; the new frame is the capture.
    if (render::Active()) {
        render::EndFrame();
        Image frame;
        char path[MAX_PATH];
        ei::OutputPath("frames", path);
        CreateDirectoryA(path, nullptr);
        std::size_t used = std::strlen(path);
        std::snprintf(path + used, MAX_PATH - used, "\\%s.bmp", g_name.c_str());
        if (!render::ReadFrame(&frame.width, &frame.height, &frame.bgr)) {
            g_state = State::Failed;
            g_detail = "the new renderer has no frame";
            return;
        }
        if (!WriteBmp(path, frame)) {
            g_state = State::Failed;
            g_detail = std::string("cannot write ") + path;
            return;
        }
        char text[MAX_PATH + 96];
        std::snprintf(text, sizeof(text), "%s (%dx%d, the new renderer's frame, frame %u)", path, frame.width, frame.height,
                      g_frames);
        g_detail = text;
        g_state = State::Done;
        return;
    }
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
    // The rewritten frame step's hook point (harness_hooks.h), and in game.exe the original's CALL.
    if (HookPointsOnly()) {
        ei::mainloop::g_harnessHooks.cursorLock = &CaptureThenCursorLock;
        installed = 1;
        return true;
    }
    auto* p = reinterpret_cast<unsigned char*>(kCursorLockCall);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || kCursorLockCall + 5 + rel != kCursorLock) {
        Log("harness: capture: unexpected code at %08X; frames cannot be captured", unsigned(kCursorLockCall));
        installed = 0;
        return false;
    }
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&CaptureThenCursorLock) - (kCursorLockCall + 5));
    ei::NoteHarnessPatch(kCursorLockCall, 5, "the frame capture's CALL");
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    ei::mainloop::g_harnessHooks.cursorLock = &CaptureThenCursorLock;
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

bool WriteFrame(const char* name, int width, int height, const std::vector<std::uint8_t>& bgr, std::string* written) {
    char path[MAX_PATH];
    ei::OutputPath("frames", path);
    CreateDirectoryA(path, nullptr);
    std::size_t used = std::strlen(path);
    std::snprintf(path + used, MAX_PATH - used, "\\%s.bmp", name);
    Image image;
    image.width = width;
    image.height = height;
    image.bgr = bgr;
    if (written) *written = path;
    return WriteBmp(path, image);
}

namespace {
// Reads a BMP into `image` (top-down rows): the 24-bit ones this file wrote, and the game's own
// (CDib::Write: 16 or 32 bits, BI_BITFIELDS with the three masks after the header, or BI_RGB).
bool ReadBmpFile(const char* path, Image& image, std::string* how = nullptr) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    bool ok = std::fread(&file, sizeof(file), 1, f) == 1 && std::fread(&info, sizeof(info), 1, f) == 1 &&
              file.bfType == 0x4D42 && info.biWidth > 0 && info.biHeight > 0 && info.biSize >= sizeof(info) &&
              (info.biBitCount == 16 || info.biBitCount == 24 || info.biBitCount == 32) &&
              (info.biCompression == BI_RGB || (info.biCompression == BI_BITFIELDS && info.biBitCount != 24));
    std::uint32_t masks[3] = {0, 0, 0};
    if (ok && info.biCompression == BI_BITFIELDS) {
        ok = std::fseek(f, static_cast<long>(sizeof(file) + info.biSize), SEEK_SET) == 0 && std::fread(masks, 4, 3, f) == 3;
    } else if (info.biBitCount == 16) {
        masks[0] = 0x7C00, masks[1] = 0x03E0, masks[2] = 0x001F;
    } else {
        masks[0] = 0xFF0000, masks[1] = 0xFF00, masks[2] = 0xFF;
    }
    if (ok) {
        image.width = info.biWidth;
        image.height = info.biHeight;
        const int bytes = info.biBitCount / 8;
        const std::size_t stride = (static_cast<std::size_t>(image.width) * bytes + 3) & ~static_cast<std::size_t>(3);
        image.bgr.resize(static_cast<std::size_t>(image.width) * image.height * 3);
        Channel r = MakeChannel(masks[0]), g = MakeChannel(masks[1]), b = MakeChannel(masks[2]);
        std::vector<std::uint8_t> row(stride);
        ok = std::fseek(f, static_cast<long>(file.bfOffBits), SEEK_SET) == 0;
        for (int y = image.height - 1; ok && y >= 0; --y) {
            ok = std::fread(row.data(), 1, row.size(), f) == row.size();
            if (!ok) break;
            std::uint8_t* out = &image.bgr[static_cast<std::size_t>(y) * image.width * 3];
            for (int x = 0; x < image.width; ++x) {
                if (bytes == 3) {
                    std::memcpy(out + x * 3, &row[static_cast<std::size_t>(x) * 3], 3);
                    continue;
                }
                std::uint32_t pixel = 0;
                std::memcpy(&pixel, &row[static_cast<std::size_t>(x) * bytes], bytes);
                out[x * 3 + 0] = Expand(pixel, b);
                out[x * 3 + 1] = Expand(pixel, g);
                out[x * 3 + 2] = Expand(pixel, r);
            }
        }
        if (ok && how) {
            char text[96];
            std::snprintf(text, sizeof(text), "%dx%d, %d-bit R %08lX G %08lX B %08lX", image.width, image.height,
                          info.biBitCount, static_cast<unsigned long>(masks[0]), static_cast<unsigned long>(masks[1]),
                          static_cast<unsigned long>(masks[2]));
            *how = text;
        }
    }
    std::fclose(f);
    return ok;
}

bool ReadBmp(const char* name, Image& image) {
    char path[MAX_PATH];
    ei::OutputPath("frames", path);
    std::size_t used = std::strlen(path);
    std::snprintf(path + used, MAX_PATH - used, "\\%s.bmp", name);
    return ReadBmpFile(path, image);
}

bool CompareImages(const Image& ia, const Image& ib, int tolerance, double* differing, int* largest, const char** why) {
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
} // namespace

bool Compare(const char* a, const char* b, int tolerance, double* differing, int* largest, const char** why) {
    Image ia, ib;
    if (!ReadBmp(a, ia) || !ReadBmp(b, ib)) {
        *why = "a frame is missing or unreadable";
        return false;
    }
    return CompareImages(ia, ib, tolerance, differing, largest, why);
}

bool CompareFile(const char* path, const char* frame, int tolerance, double* differing, int* largest, std::string* how,
                 const char** why) {
    Image shot, captured;
    if (!ReadBmpFile(path, shot, how)) {
        *why = "the file is missing or unreadable";
        return false;
    }
    if (!ReadBmp(frame, captured)) {
        *why = "the frame is missing or unreadable";
        return false;
    }
    return CompareImages(shot, captured, tolerance, differing, largest, why);
}

bool CompareSavePicture(const char* path, const char* frame, bool centre, int tolerance, double* differing, int* largest,
                        const char** why) {
    constexpr int kWidth = 256, kHeight = 192;   // shot.sav: 256x192 5:6:5 pixels, top-down, no header
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(kWidth) * kHeight);
    FILE* f = nullptr;
    bool ok = fopen_s(&f, path, "rb") == 0 && f;
    if (ok) ok = std::fread(pixels.data(), 2, pixels.size(), f) == pixels.size();
    if (f) std::fclose(f);
    if (!ok) {
        *why = "the save's picture is missing or short";
        return false;
    }
    Image captured;
    if (!ReadBmp(frame, captured)) {
        *why = "the frame is missing or unreadable";
        return false;
    }
    // Both as averages of 8x8 blocks of the picture (32x24; the frame's matching rectangles): the
    // picture went through two nearest-pixel stretches (the screen copy, 005FD440) whose samples
    // differ from any one sampling of the frame in textured places, but not on average.
    constexpr int kBlock = 8, kColumns = kWidth / kBlock, kRows = kHeight / kBlock;
    int left = 0, width = captured.width;
    if (centre && captured.width * 3 > captured.height * 4) {
        width = captured.height * 4 / 3;
        left = (captured.width - width) / 2;
    }
    if (width < kColumns || captured.height < kRows) {
        *why = "the frame is too small";
        return false;
    }
    Image shot, scaled;
    shot.width = scaled.width = kColumns;
    shot.height = scaled.height = kRows;
    shot.bgr.resize(static_cast<std::size_t>(kColumns) * kRows * 3);
    scaled.bgr.resize(shot.bgr.size());
    Channel r = MakeChannel(0xF800), g = MakeChannel(0x07E0), b = MakeChannel(0x001F);
    for (int by = 0; by < kRows; ++by) {
        for (int bx = 0; bx < kColumns; ++bx) {
            std::size_t i = (static_cast<std::size_t>(by) * kColumns + bx) * 3;
            unsigned sum[3] = {0, 0, 0};
            for (int y = by * kBlock; y < (by + 1) * kBlock; ++y) {
                for (int x = bx * kBlock; x < (bx + 1) * kBlock; ++x) {
                    std::uint32_t pixel = pixels[static_cast<std::size_t>(y) * kWidth + x];
                    sum[0] += Expand(pixel, b);
                    sum[1] += Expand(pixel, g);
                    sum[2] += Expand(pixel, r);
                }
            }
            for (int c = 0; c < 3; ++c) shot.bgr[i + c] = static_cast<std::uint8_t>(sum[c] / (kBlock * kBlock));
            int x0 = left + bx * width / kColumns, x1 = left + (bx + 1) * width / kColumns;
            int y0 = by * captured.height / kRows, y1 = (by + 1) * captured.height / kRows;
            unsigned long long frameSum[3] = {0, 0, 0};
            for (int y = y0; y < y1; ++y) {
                const std::uint8_t* row = &captured.bgr[(static_cast<std::size_t>(y) * captured.width + x0) * 3];
                for (int x = x0; x < x1; ++x, row += 3)
                    for (int c = 0; c < 3; ++c) frameSum[c] += row[c];
            }
            unsigned long long count = static_cast<unsigned long long>(x1 - x0) * static_cast<unsigned long long>(y1 - y0);
            for (int c = 0; c < 3; ++c) scaled.bgr[i + c] = static_cast<std::uint8_t>(frameSum[c] / count);
        }
    }
    return CompareImages(shot, scaled, tolerance, differing, largest, why);
}

} // namespace ei::verify::capture
