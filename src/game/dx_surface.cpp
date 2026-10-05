// The off-screen surface holder (CDXSurface, 0x20 bytes, vtable 0073D774): its constructor for a
// system-memory surface of the screen's pixel format (004EF7B0, thiscall: device object, width,
// height; RET 0xC), its constructor from an .mmp picture (004EFC60) with the picture loader it
// calls (004F0060) and its destructor body (004EFD20). Used for the interface windows' surfaces
// (005FCC00 / 005FCDD0 / 005FD7A0), the software cursor's (005E60B0) and its images (005E4EB0 ->
// 004EFC60), and the movies' (005DA400, 005DB3F0, 005DBA50).
//
// With the new renderer's "cpuwindows" option the surface's pixels live in a DIB section the game
// code owns (DirectDraw's client-memory surface: DDSD_LPSURFACE / DDSD_PITCH): GDI through the
// surface's DC, the pixel code through Lock and the old path's Blt all work on that memory, and the
// new renderer reads it directly instead of locking the surface (render::ClientSurface). Picture
// surfaces (004F0060) get the same memory when they are asked for in system memory; in the
// switch-over ("present") the picture is written into it without DirectDraw. See
// docs/notes/renderer.md ("Interface windows").
#include "dx_surface.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include "../hook/log.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
int& HolderCount() { return *reinterpret_cast<int*>(0x007AEE94); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// 004800E0 / 00480080 (cdecl): DirectX error / fatal error reports (the second printf-like);
// nonzero to go on.
__declspec(naked) int __cdecl DirectXError(long, const char*) EI_ORIGINAL(0x004800E0)
__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
// The thrown error object: operator new (007005AF), its constructor (00480250, cdecl: object,
// format, width, height) and _CxxThrowException (006E7819) with its throw info 00759908.
__declspec(naked) void* __cdecl GameNew(unsigned) EI_ORIGINAL(0x007005AF)
__declspec(naked) void* __cdecl ErrorObject(void*, const char*, int, int) EI_ORIGINAL(0x00480250)
__declspec(naked) void __stdcall ThrowException(void*, const void*) EI_ORIGINAL(0x006E7819)

constexpr int kRelease = 2, kGetPixelFormat = 21, kGetSurfaceDesc = 22, kLock = 25, kUnlock = 32;   // IDirectDrawSurface7
constexpr int kCreateSurface = 6;                                          // IDirectDraw7
constexpr std::uint32_t kDescFlags = 0x1007;    // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
constexpr std::uint32_t kPixelFormatFlag = 0x1000;   // DDSD_PIXELFORMAT
constexpr std::uint32_t kClientMemory = 0x808;  // DDSD_LPSURFACE | DDSD_PITCH
constexpr std::uint32_t kCaps = 0x840;          // DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY

// DDSURFACEDESC2 as dwords: +0x04 flags, +0x08 height, +0x0C width, +0x10 pitch, +0x24 memory,
// +0x48 the pixel format (+0x54 bits, +0x58..+0x64 R, G, B, A masks), +0x68 caps.
enum : int { kFlags = 1, kHeight = 2, kWidth = 3, kPitch = 4, kSurface = 9, kFormat = 18, kBits = 21, kRed = 22,
             kGreen = 23, kBlue = 24, kAlpha = 25, kCapsWord = 26 };

// --- Client memory ("cpuwindows") ------------------------------------------------------------------

struct ClientMemory {
    HBITMAP bitmap;
    void* bits;
    std::uint32_t width, height, pitch;
    std::uint32_t format[8];   // the DDPIXELFORMAT
};
std::unordered_map<const void*, ClientMemory> g_memory;   // by holder
int g_clientFailures = 0;

// A top-down DIB section for `desc` (16 or 32-bit RGB): its pitch and memory go into the
// description.
bool AttachClientMemory(std::uint32_t* desc, ClientMemory* out) {
    std::uint32_t bits = desc[kBits];
    if ((bits != 16 && bits != 32) || !(desc[kFormat + 1] & 0x40)) return false;   // DDPF_RGB
    int width = static_cast<int>(desc[kWidth]), height = static_cast<int>(desc[kHeight]);
    if (width <= 0 || height <= 0) return false;
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info{};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = static_cast<WORD>(bits);
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = desc[kRed];
    info.masks[1] = desc[kGreen];
    info.masks[2] = desc[kBlue];
    void* memory = nullptr;
    HBITMAP bitmap = gameapi::CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &memory,
                                                nullptr, 0);
    if (!bitmap || !memory) return false;
    desc[kFlags] |= kClientMemory;
    desc[kPitch] = static_cast<std::uint32_t>(((width * static_cast<int>(bits) / 8) + 3) & ~3);
    desc[kSurface] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory));
    *out = ClientMemory{bitmap, memory, desc[kWidth], desc[kHeight], desc[kPitch], {}};
    std::memcpy(out->format, &desc[kFormat], sizeof(out->format));
    return true;
}

[[noreturn]] void ThrowCreateFailed(int width, int height) {
    void* object = GameNew(0xC);
    if (object) object = ErrorObject(object, Text(0x0078E89C), width, height);   // "Can't create DDSurface %d %d ..."
    ThrowException(&object, reinterpret_cast<const void*>(0x00759908));
    for (;;) {
    }
}

// The surface's width, height and bits per pixel into +0x14 / +0x18 / +0x1C (GetSurfaceDesc, the
// result in +8); false when that failed and the report said stop.
bool ReadSize(char* holder) {
    std::uint32_t d[0x1F] = {};
    At<int>(holder, 0x14) = 0;
    At<int>(holder, 0x18) = 0;
    At<int>(holder, 0x1C) = 0;
    d[0] = 0x7C;
    long hr = ComCall(At<void*>(holder, 0x10), kGetSurfaceDesc, static_cast<void*>(d));
    At<long>(holder, 8) = hr;
    if (hr != 0 && !DirectXError(hr, Text(0x0078E6C4))) return false;   // "Can't get surface description"
    At<std::uint32_t>(holder, 0x14) = d[kWidth];
    At<std::uint32_t>(holder, 0x18) = d[kHeight];
    At<std::uint32_t>(holder, 0x1C) = d[kBits];
    return true;
}

} // namespace

HBITMAP ClientBitmap(const void* holder) {
    auto it = g_memory.find(holder);
    return it == g_memory.end() ? nullptr : it->second.bitmap;
}

bool DirectPixels(const void* holder, std::uint32_t* desc) {
    if (!render::Presenting()) return false;
    auto it = g_memory.find(holder);
    if (it == g_memory.end()) return false;
    const ClientMemory& m = it->second;
    ::GdiFlush();   // GDI may still hold drawing for the memory (not imported by the game)
    std::memset(desc, 0, 0x7C);
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags | kClientMemory;
    desc[kWidth] = m.width;
    desc[kHeight] = m.height;
    desc[kPitch] = m.pitch;
    desc[kSurface] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m.bits));
    std::memcpy(&desc[kFormat], m.format, sizeof(m.format));
    desc[kCapsWord] = kCaps;
    return true;
}

// 004EF7B0. Returns the holder; when the surface cannot be made a C++ exception is thrown (as the
// original: an error object by pointer).
char* __fastcall DXSurfaceConstruct(char* holder, void*, char* device, int width, int height) {
    At<int>(holder, 4) = 0;
    At<int>(holder, 8) = 0;
    std::uint32_t desc[0x1F] = {};
    desc[kWidth] = static_cast<std::uint32_t>(width);
    desc[kHeight] = static_cast<std::uint32_t>(height);
    At<char*>(holder, 0xC) = device;
    At<void*>(holder, 0x10) = nullptr;
    At<int>(holder, 0x14) = 0;
    At<int>(holder, 0x18) = 0;
    At<int>(holder, 0x1C) = 0;
    At<std::uintptr_t>(holder, 0) = 0x0073D774;
    desc[0] = 0x7C;
    desc[kFlags] = kDescFlags;
    desc[kCapsWord] = kCaps;
    // The screen's pixel format (the primary's holder +0x14 of the device object).
    char* primary = At<char*>(device, 0x14);
    desc[kFormat] = 0x20;
    long hr = ComCall(At<void*>(primary, 0x10), kGetPixelFormat, static_cast<void*>(&desc[kFormat]));
    At<long>(primary, 8) = hr;
    if (hr != 0) DirectXError(hr, Text(0x0078E714));   // "Can't get pixel format from surface"

    if (!At<char*>(holder, 0xC)) {
        if (!FatalError(Text(0x0078E8CC))) ThrowCreateFailed(width, height);
        // (The original goes on with no surface and fails in GetSurfaceDesc.)
        ReadSize(holder);
        ++HolderCount();
        return holder;
    }
    ClientMemory memory{};
    bool client = render::CpuWindows() && AttachClientMemory(desc, &memory);
    char* owner = At<char*>(holder, 0xC);
    void* surface = nullptr;
    gameapi::EnterCriticalSection(DeviceLock());
    hr = ComCall(At<void*>(owner, 0xC), kCreateSurface, static_cast<void*>(desc), static_cast<void*>(&surface),
                 static_cast<void*>(nullptr));
    At<long>(owner, 8) = hr;
    if (hr != 0 && client) {
        // DirectDraw refused the client memory: the surface keeps its own.
        if (g_clientFailures++ < 5)
            ei::Log("cpuwindows: client-memory surface %dx%d refused (%08lX); a plain one instead", width, height,
                    static_cast<unsigned long>(hr));
        gameapi::DeleteObject(memory.bitmap);
        client = false;
        desc[kFlags] &= ~kClientMemory;
        desc[kPitch] = 0;
        desc[kSurface] = 0;
        surface = nullptr;
        hr = ComCall(At<void*>(owner, 0xC), kCreateSurface, static_cast<void*>(desc), static_cast<void*>(&surface),
                     static_cast<void*>(nullptr));
        At<long>(owner, 8) = hr;
    }
    gameapi::LeaveCriticalSection(DeviceLock());
    if (hr != 0) surface = nullptr;
    At<void*>(holder, 0x10) = surface;
    if (!surface) {
        if (client) gameapi::DeleteObject(memory.bitmap);
        ThrowCreateFailed(width, height);
    }
    if (!ReadSize(holder)) {
        if (client) {
            ComCall(surface, kRelease);
            At<void*>(holder, 0x10) = nullptr;
            gameapi::DeleteObject(memory.bitmap);
        }
        ThrowCreateFailed(width, height);
    }
    ReadSize(holder);
    if (client) {
        g_memory[holder] = memory;
        render::ClientSurface(holder, memory.bits, static_cast<int>(desc[kPitch]), width, height,
                              static_cast<int>(desc[kBits]), desc[kRed], desc[kGreen], desc[kBlue],
                              (desc[kFormat + 1] & 1) ? desc[kAlpha] : 0);
    }
    ++HolderCount();
    return holder;
}
EI_REPLACE(0x004EF7B0, DXSurfaceConstruct);

namespace {

// The .mmp file's header (0x4C bytes); the pixels follow after `extra` more bytes.
struct MmpHeader {
    std::uint32_t magic, width, height, mipCount, format, bitsPerPixel;
    std::uint32_t channels[12];   // mask, shift, bits for alpha, red, green, blue
    std::uint32_t extra;          // +0x48
};
static_assert(sizeof(MmpHeader) == 0x4C);
constexpr std::uint32_t kFormat8888 = 0x8888;

// 004EDAA0 (thiscall, RET 4): a DDPIXELFORMAT's channels as mask, bit count and shift (0x34 bytes,
// zeroed first; it stops early for a format without DDPF_RGB or a channel wider than 8 bits).
struct ChannelLayout {
    std::uint32_t unknown_00;
    std::uint32_t alphaMask, alphaBits, alphaShift;   // +0x04
    std::uint32_t redMask, redBits, redShift;         // +0x10
    std::uint32_t greenMask, greenBits, greenShift;   // +0x1C
    std::uint32_t blueMask, blueBits, blueShift;      // +0x28
    int Split(const void* format);
};
static_assert(sizeof(ChannelLayout) == 0x34);
__declspec(naked) int ChannelLayout::Split(const void*) EI_ORIGINAL(0x004EDAA0)

// The device's pixel converter (device object +0x4C, 2DintMMX's graphics processor): slot 1 takes
// the target format (0 5:6:5, 1 5:5:5, 2 1:5:5:5, -1 another) and its alpha, red, green and blue
// masks; slot 2 converts one row of 8888 pixels (out, in, format, count).
void* Converter(char* holder) { return At<void*>(At<char*>(holder, 0xC), 0x4C); }

// The picture's pixels into the memory `desc` describes (locked, or the game-owned memory), of
// the surface's pixel format `format` (a DDPIXELFORMAT).
void FillPicture(char* holder, const MmpHeader* picture, const std::uint32_t* desc, const std::uint32_t* format) {
    const auto* source = reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->extra;
    auto* out = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[kSurface]));
    std::uint32_t bits = format[kBits - kFormat];
    if (bits == 32) {
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            std::memcpy(out, source, picture->width * 4);
            out += static_cast<std::int32_t>(desc[kPitch]);
            source += picture->width * 4;
        }
    } else if (bits == 16) {
        ChannelLayout layout;
        layout.Split(format);
        int target = -1;
        if (layout.redBits == 5) {
            if (layout.greenBits == 5) {
                if (layout.blueBits == 5) {
                    if (layout.alphaBits == 1) target = 2;
                    else if (layout.alphaBits == 0) target = 1;
                }
            } else if (layout.greenBits == 6 && layout.blueBits == 5 && layout.alphaBits == 0) {
                target = 0;
            }
        }
        ei::VCall<void>(Converter(holder), 1, target, layout.alphaMask, layout.redMask, layout.greenMask,
                        layout.blueMask);
        for (std::uint32_t row = 0; row < picture->height; ++row) {
            ei::VCall<void>(Converter(holder), 2, static_cast<void*>(out), source, target, picture->width);
            out += static_cast<std::int32_t>(desc[kPitch]);
            source += picture->width * 4;
        }
    } else if (bits == 24) {
        FatalError(Text(0x0078EA10));   // "Can't load picture to 24bit surface! ..."
        // (The surface cleared as if it had no row padding, as the original.)
        std::memset(out, 0, (bits >> 3) * desc[kHeight] * desc[kWidth]);
    }
}

} // namespace

// 004F0060 (thiscall on the holder, RET 8): makes the holder's surface (when it has none yet) of
// the size of an 8888 .mmp picture, in system memory, or in video memory when the low byte of
// `flag` is set, with the screen's pixel format, and loads the picture into it: copied to a 32-bit
// surface, converted row by row through the device's converter to a 16-bit one. 1 when done;
// otherwise the result of the fatal error report (nonzero when the player goes on).
//
// With "cpuwindows" a system-memory surface gets game-owned memory (a DIB section, as 004EF7B0;
// refused -> a plain surface, logged), and in the switch-over ("present") the picture is written
// straight into it: no Lock / Unlock, its size and pixel format from that memory (DirectPixels).
// Original details kept: a holder that already has a surface (only the dead loaders 004EFF30 /
// 004EFFF0 pass one) must have the picture's width in both +0x14 and +0x18; a failed Lock whose
// report says stop leaves the device lock held; a 24-bit surface is reported, then cleared.
int __fastcall DXSurfaceLoadPicture(char* holder, void*, const void* data, int flag) {
    const auto* picture = static_cast<const MmpHeader*>(data);
    if (!picture) return FatalError(Text(0x0078EB10));                  // "Can't load picture from NULL pointer."
    if (picture->format != kFormat8888) return FatalError(Text(0x0078EAE4));   // "... from non-8888 format."
    if (!At<void*>(holder, 0x10)) {
        std::uint32_t desc[0x1F] = {};
        desc[0] = 0x7C;
        // The screen's pixel format (the primary's holder +0x14 of the device object). Without a
        // device the original reads through a null pointer here; 004EFC60 always passes one.
        gameapi::EnterCriticalSection(DeviceLock());
        char* primary = At<char*>(At<char*>(holder, 0xC), 0x14);
        desc[kFormat] = 0x20;
        long hr = ComCall(At<void*>(primary, 0x10), kGetPixelFormat, static_cast<void*>(&desc[kFormat]));
        At<long>(primary, 8) = hr;
        if (hr != 0) DirectXError(hr, Text(0x0078E714));   // "Can't get pixel format from surface."
        gameapi::LeaveCriticalSection(DeviceLock());
        // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH: the format (the primary's) is not passed.
        desc[kFlags] = 7;
        desc[kWidth] = picture->width;
        desc[kHeight] = picture->height;
        bool video = (flag & 0xFF) != 0;
        desc[kCapsWord] = video ? 0x4040 : kCaps;   // DDSCAPS_VIDEOMEMORY | DDSCAPS_OFFSCREENPLAIN, or system memory
        // (The original's "no DXDD" report for a holder without a device is unreachable after the
        // read above, as is its release of an earlier surface: there is none on this path.)
        ClientMemory memory{};
        bool client = !video && render::CpuWindows() && AttachClientMemory(desc, &memory);
        if (client) desc[kFlags] |= kPixelFormatFlag;   // the DIB's format, the one DirectDraw would pick
        char* device = At<char*>(holder, 0xC);
        void* surface = nullptr;
        gameapi::EnterCriticalSection(DeviceLock());
        hr = ComCall(At<void*>(device, 0xC), kCreateSurface, static_cast<void*>(desc), static_cast<void*>(&surface),
                     static_cast<void*>(nullptr));
        At<long>(device, 8) = hr;
        if (hr != 0 && client) {
            // DirectDraw refused the client memory: the surface keeps its own.
            if (g_clientFailures++ < 5)
                ei::Log("cpuwindows: client-memory surface %ux%u refused (%08lX) for a picture; a plain one instead",
                        picture->width, picture->height, static_cast<unsigned long>(hr));
            gameapi::DeleteObject(memory.bitmap);
            client = false;
            desc[kFlags] = 7;
            desc[kPitch] = 0;
            desc[kSurface] = 0;
            surface = nullptr;
            hr = ComCall(At<void*>(device, 0xC), kCreateSurface, static_cast<void*>(desc), static_cast<void*>(&surface),
                         static_cast<void*>(nullptr));
            At<long>(device, 8) = hr;
        }
        gameapi::LeaveCriticalSection(DeviceLock());
        if (At<long>(device, 8) != 0) surface = nullptr;
        At<void*>(holder, 0x10) = surface;
        bool described = false;
        if (surface) {
            if (client) {
                g_memory[holder] = memory;
                render::ClientSurface(holder, memory.bits, static_cast<int>(desc[kPitch]), static_cast<int>(desc[kWidth]),
                                      static_cast<int>(desc[kHeight]), static_cast<int>(desc[kBits]), desc[kRed],
                                      desc[kGreen], desc[kBlue], (desc[kFormat + 1] & 1) ? desc[kAlpha] : 0);
            }
            // 004EFEA0's description of the surface; in the switch-over the memory describes it.
            described = (client && render::Presenting()) || ReadSize(holder);
        } else if (client) {
            gameapi::DeleteObject(memory.bitmap);
        }
        if (!described)   // "Can't create surface to load picture %d:%d:%d."
            return FatalError(Text(0x0078EAB4), picture->width, picture->height, picture->bitsPerPixel);
        At<std::uint32_t>(holder, 0x14) = desc[kWidth];
        At<std::uint32_t>(holder, 0x18) = desc[kHeight];
        At<std::uint32_t>(holder, 0x1C) = desc[kBits];
    } else if (picture->width != At<std::uint32_t>(holder, 0x14) || picture->width != At<std::uint32_t>(holder, 0x18)) {
        return FatalError(Text(0x0078E99C));   // "Dimentions of the DD surface are differ from ..."
    }

    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    std::uint32_t format[8] = {};
    bool direct = DirectPixels(holder, desc);
    if (direct) {
        std::memcpy(format, &desc[kFormat], sizeof(format));
        gameapi::EnterCriticalSection(DeviceLock());
    } else {
        format[0] = 0x20;
        long hr = ComCall(At<void*>(holder, 0x10), kGetPixelFormat, static_cast<void*>(format));
        At<long>(holder, 8) = hr;
        if (hr != 0 && !DirectXError(hr, Text(0x0078E714)))   // "Can't get pixel format from surface."
            return FatalError(Text(0x0078EA80));               // "Can't get surface pixel format to load picture."
        gameapi::EnterCriticalSection(DeviceLock());
        // DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK
        hr = ComCall(At<void*>(holder, 0x10), kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), 0x821ul,
                     static_cast<void*>(nullptr));
        At<long>(holder, 8) = hr;
        if (hr != 0 && !DirectXError(hr, Text(0x0078E30C)))   // "Can't lock surface."
            return FatalError(Text(0x0078EA58));               // "Can't lock surface to load picture." (lock held)
    }
    FillPicture(holder, picture, desc, format);
    gameapi::LeaveCriticalSection(DeviceLock());
    if (!direct) {
        long hr = ComCall(At<void*>(holder, 0x10), kUnlock, static_cast<void*>(nullptr));
        At<long>(holder, 8) = hr;
        if (hr != 0 && !DirectXError(hr, Text(0x0078E2F4)))   // "Can't unlock surface."
            return FatalError(Text(0x0078E9E4));               // "Can't unlock surface after load picture."
    }
    return 1;
}
EI_REPLACE(0x004F0060, DXSurfaceLoadPicture);

namespace {
// 004F0060 through its address (the verify build's wrapper runs there).
__declspec(naked) int __fastcall LoadPicture(char*, void*, const void*, int) EI_ORIGINAL(0x004F0060)
__declspec(naked) void* __cdecl ErrorObjectText(void*, const char*) EI_ORIGINAL(0x00480250)
} // namespace

// 004EFC60 (thiscall, RET 0xC): a holder whose surface shows an .mmp picture (the software cursor's
// images 005E4EB0, windows made from a picture 005FCF80). The new renderer keeps the picture's
// pixels from the file data (render::PictureFromMmp) instead of reading the surface back. Throws
// the game's error object when the picture cannot be loaded.
char* __fastcall DXSurfaceFromPicture(char* holder, void*, char* device, const void* picture, int flag) {
    At<int>(holder, 4) = 0;
    At<int>(holder, 8) = 0;
    At<char*>(holder, 0xC) = device;
    At<void*>(holder, 0x10) = nullptr;
    At<int>(holder, 0x14) = 0;
    At<int>(holder, 0x18) = 0;
    At<int>(holder, 0x1C) = 0;
    At<std::uintptr_t>(holder, 0) = 0x0073D774;
    if (!LoadPicture(holder, nullptr, picture, flag)) {
        void* object = GameNew(0xC);
        if (object) object = ErrorObjectText(object, Text(0x0078E918));   // "Can't load picture to surface"
        ThrowException(&object, reinterpret_cast<const void*>(0x00759908));
    }
    if (render::Active()) render::PictureFromMmp(holder, picture);
    ++HolderCount();
    return holder;
}
EI_REPLACE(0x004EFC60, DXSurfaceFromPicture);

// 004EFD20. Releases the surface (and its client memory), clears the device, counts the holder out
// and leaves the base class's vtable (0073D768).
void __fastcall DXSurfaceDestroy(char* holder) {
    At<std::uintptr_t>(holder, 0) = 0x0073D774;
    if (void* surface = At<void*>(holder, 0x10)) ComCall(surface, kRelease);
    render::ClientSurfaceGone(holder);   // also forgets a picture kept for it
    auto it = g_memory.find(holder);
    if (it != g_memory.end()) {
        gameapi::DeleteObject(it->second.bitmap);
        g_memory.erase(it);
    }
    At<char*>(holder, 0xC) = nullptr;
    --HolderCount();
    At<std::uintptr_t>(holder, 0) = 0x0073D768;
}
EI_REPLACE(0x004EFD20, DXSurfaceDestroy);
