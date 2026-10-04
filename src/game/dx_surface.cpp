// The off-screen surface holder (CDXSurface, 0x20 bytes, vtable 0073D774): its constructor for a
// system-memory surface of the screen's pixel format (004EF7B0, thiscall: device object, width,
// height; RET 0xC), its constructor from an .mmp picture (004EFC60) and its destructor body
// (004EFD20). Used for the interface windows' surfaces
// (005FCC00 / 005FCDD0 / 005FD7A0), the software cursor's (005E60B0) and the movies' (005DA400,
// 005DB3F0, 005DBA50).
//
// With the new renderer's "cpuwindows" option the surface's pixels live in a DIB section the game
// code owns (DirectDraw's client-memory surface: DDSD_LPSURFACE / DDSD_PITCH): GDI through the
// surface's DC, the pixel code through Lock and the old path's Blt all work on that memory, and the
// new renderer reads it directly instead of locking the surface (render::ClientSurface). See
// docs/notes/renderer.md ("Interface windows").
#include "dx_surface.h"

#include "ei/hook.h"
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

// 004800E0 / 00480080 (cdecl): DirectX error / fatal error reports; nonzero to go on.
__declspec(naked) int __cdecl DirectXError(long, const char*) EI_ORIGINAL(0x004800E0)
__declspec(naked) int __cdecl FatalErrorText(const char*) EI_ORIGINAL(0x00480080)
// The thrown error object: operator new (007005AF), its constructor (00480250, cdecl: object,
// format, width, height) and _CxxThrowException (006E7819) with its throw info 00759908.
__declspec(naked) void* __cdecl GameNew(unsigned) EI_ORIGINAL(0x007005AF)
__declspec(naked) void* __cdecl ErrorObject(void*, const char*, int, int) EI_ORIGINAL(0x00480250)
__declspec(naked) void __stdcall ThrowException(void*, const void*) EI_ORIGINAL(0x006E7819)

constexpr int kGetPixelFormat = 21, kGetSurfaceDesc = 22, kRelease = 2;   // IDirectDrawSurface7
constexpr int kCreateSurface = 6;                                          // IDirectDraw7
constexpr std::uint32_t kDescFlags = 0x1007;    // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
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
    *out = ClientMemory{bitmap, memory};
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
        if (!FatalErrorText(Text(0x0078E8CC))) ThrowCreateFailed(width, height);
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
// 004F0060 (thiscall on the holder, RET 8): makes the surface and loads an 8888 .mmp picture into
// it (`flag` asks for a texture surface); 0 on failure (reported).
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
