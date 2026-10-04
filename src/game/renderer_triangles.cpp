// The renderer's triangle draw (00501BB0, virtual slot 12 of the renderer at 007AF060, vtable
// 0073D93C; thiscall, RET 0x10): pre-transformed vertices (FVF 0x1C4) drawn as an indexed triangle
// list with the current state, fogged first by distance when the device's +0x158 is set (the
// transform object's slot 35). Used by the flying hit points (005E18E0). The new renderer
// (src/render) takes the draw; with the old path's objects off it is skipped. See
// docs/notes/renderer.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kDrawIndexedPrimitive = 26;
constexpr unsigned long kTriangleList = 4, kFvf = 0x1C4;

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

} // namespace

// 00501BB0. Returns 1.
int __fastcall RendererTriangles(char* renderer, void*, void* vertices, int vertexCount, const std::uint16_t* indices,
                                 unsigned indexCount) {
    char* device = At<char*>(renderer, 0x20);
    if (At<int>(device, 0x158)) ThisCall<void>(At<void*>(At<char*>(device, 0xAC), 0xC), 35, vertices, vertexCount);
    if (render::TlDraw(device, vertices, static_cast<int>(kFvf), vertexCount, indices, static_cast<int>(indexCount),
                       static_cast<int>(kTriangleList))) {
        gameapi::EnterCriticalSection(DeviceLock());
        At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kDrawIndexedPrimitive, kTriangleList, kFvf, vertices,
                                      static_cast<unsigned long>(vertexCount), indices, static_cast<unsigned long>(indexCount), 0ul);
        gameapi::LeaveCriticalSection(DeviceLock());
    }
    At<int>(device, 0x16C) += vertexCount;
    At<unsigned>(device, 0x170) += indexCount / 3;
    At<int>(device, 0x174) += 1;
    return 1;
}
EI_REPLACE(0x00501BB0, RendererTriangles);
