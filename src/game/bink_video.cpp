// A Bink movie's frame step (CBinkVideo, 005DBD80, thiscall, RET 4; called once per frame by the
// movie screen's draw 0062BD80, inside the game's frame). When the movie's next frame is due
// (BinkWait) it is decoded (BinkDoFrame) into the video's own surface (+0x00 its holder, made by
// 005DBA50 in the format +0x04 BinkDDSurfaceType gave) through a Lock / BinkCopyToBuffer / Unlock,
// and the movie steps on (BinkNextFrame). Every call then blits the whole movie frame into the
// rectangle +0x08 (letterboxed by 005DBA50) of the render target, or of the primary surface when
// `toPrimary`. Returns whether frames are left. The new renderer (src/render) gets the same blit as
// part of its frame, from a CPU copy of the decoded frame; when it presents by itself the video's
// surface is not filled and the old blit is skipped. See docs/notes/renderer.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <vector>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

// Bink through game.exe's import table (stdcall).
using BinkFn = int(__stdcall*)(void*);
using BinkCopyFn = int(__stdcall*)(void*, void*, int, int, int, int, unsigned);
BinkFn BinkWait() { return *reinterpret_cast<BinkFn*>(0x0073B728); }
BinkFn BinkDoFrame() { return *reinterpret_cast<BinkFn*>(0x0073B718); }
BinkFn BinkNextFrame() { return *reinterpret_cast<BinkFn*>(0x0073B724); }
BinkCopyFn BinkCopyToBuffer() { return *reinterpret_cast<BinkCopyFn*>(0x0073B740); }

// The Bink handle's first fields: frame width, height, frame count +0x08, current frame +0x10.
struct BinkHeader {
    std::uint32_t width, height, frames, unknown_0C, frame;
};

constexpr int kLock = 25, kUnlock = 32, kBlt = 5;            // IDirectDrawSurface7 slots
constexpr unsigned long kLockFlags = 0x21;                   // DDLOCK_WAIT | DDLOCK_NOSYSLOCK
constexpr unsigned long kBltWait = 0x1000000;                // DDBLT_WAIT
constexpr unsigned kBinkSurface32R = 4;                      // BinkCopyToBuffer: R, G, B, X bytes (checked offline)

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// 004800E0 (cdecl): a DirectX error report; nonzero to go on.
__declspec(naked) int __cdecl DirectXError(long, const char*) EI_ORIGINAL(0x004800E0)
// 0044C470 (cdecl): an error message; returns a value whose low byte the caller clears.
__declspec(naked) unsigned __cdecl ErrorMessage(const char*) EI_ORIGINAL(0x0044C470)
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

} // namespace

// 005DBD80.
unsigned __fastcall BinkVideoFrame(void* self, void*, char toPrimary) {
    if (!*reinterpret_cast<int*>(0x0079BCBC)) return 0;   // movies off
    void* bink = At<void*>(self, 0x18);
    if (!bink) return 0;
    if (BinkWait()(bink) == 0) {
        BinkDoFrame()(bink);
        // The new renderer gets the frame from a CPU buffer (BinkCopyToBuffer's 32-bit R, G, B, X
        // type); when it presents by itself the video's DirectDraw surface is not filled at all.
        if (render::Active()) {
            const auto* size = static_cast<const BinkHeader*>(bink);
            int width = static_cast<int>(size->width), height = static_cast<int>(size->height);
            static std::vector<std::uint8_t> frame;
            frame.resize(static_cast<std::size_t>(width) * height * 4);
            BinkCopyToBuffer()(bink, frame.data(), width * 4, height, 0, 0, kBinkSurface32R);
            render::MovieFrame(self, frame.data(), width, height);
        }
        if (render::Presenting()) {
            BinkNextFrame()(bink);
        } else {
            std::uint8_t* holder = At<std::uint8_t*>(self, 0);
            std::uint32_t desc[0x1F] = {};
            desc[0] = 0x7C;
            gameapi::EnterCriticalSection(DeviceLock());
            long hr = ComCall(At<void*>(holder, 0x10), kLock, static_cast<void*>(nullptr), static_cast<void*>(desc), kLockFlags,
                              static_cast<void*>(nullptr));
            At<long>(holder, 8) = hr;
            if (hr != 0 && !DirectXError(hr, Text(0x0078E30C))) {   // "Can't lock surface"
                // The original leaves the device lock held here.
                return ErrorMessage(Text(0x0079234C)) & 0xFFFFFF00u;
            }
            // DDSURFACEDESC2: +0x08 height, +0x10 pitch, +0x24 the pixels.
            BinkCopyToBuffer()(bink, reinterpret_cast<void*>(desc[9]), static_cast<int>(desc[4]), static_cast<int>(desc[2]), 0, 0,
                               At<std::uint32_t>(self, 4));
            holder = At<std::uint8_t*>(self, 0);
            gameapi::LeaveCriticalSection(DeviceLock());
            hr = ComCall(At<void*>(holder, 0x10), kUnlock, static_cast<void*>(nullptr));
            At<long>(holder, 8) = hr;
            if (hr != 0 && !DirectXError(hr, Text(0x0078E2F4)))   // "Can't unlock surface"
                return ErrorMessage(Text(0x00792324)) & 0xFFFFFF00u;
            BinkNextFrame()(bink);
        }
    }
    const auto* header = static_cast<const BinkHeader*>(At<void*>(self, 0x18));
    RECT source{0, 0, static_cast<LONG>(header->width), static_cast<LONG>(header->height)};
    std::uint8_t* holder = At<std::uint8_t*>(self, 0);
    RECT* destination = &At<RECT>(self, 8);
    std::uint8_t* target = At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), toPrimary ? 0x14 : 0x18);
    if (At<void*>(target, 0x10) != At<void*>(holder, 0x10)) {
        if (render::Active())
            render::MovieBlit(self, render::Rect{destination->left, destination->top, destination->right, destination->bottom});
        if (!render::Presenting()) {
            gameapi::EnterCriticalSection(DeviceLock());
            ComCall(At<void*>(target, 0x10), kBlt, destination, At<void*>(holder, 0x10), &source, kBltWait,
                    static_cast<void*>(nullptr));
            gameapi::LeaveCriticalSection(DeviceLock());
        }
    }
    header = static_cast<const BinkHeader*>(At<void*>(self, 0x18));
    return static_cast<int>(header->frame) < static_cast<int>(header->frames) ? 1u : 0u;
}
EI_REPLACE(0x005DBD80, BinkVideoFrame);
