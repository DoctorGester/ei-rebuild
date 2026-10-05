// The renderer's rectangle (005041F0, virtual slot 19 of the renderer at 007AF060, vtable 0073D93C,
// RET 0x18): a black rectangle with the given alpha, filled as two pre-transformed triangles over
// the current blend state, and with `outline` a white one-pixel frame drawn as a line strip around
// it (the fill then stays a pixel inside). Used for the rubber-band selection (00636020) and the
// interface's fades (005E69C0). The new renderer (src/render) takes both draws; with "nointerface"
// the old path skips them. See docs/notes/renderer.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"
#include "originals_render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// IDirect3DDevice7 slots and the states this draw touches.
constexpr int kSetRenderState = 20;
constexpr int kDrawPrimitive = 25;
constexpr int kDrawIndexedPrimitive = 26;
constexpr int kSetTexture = 35;
constexpr int kSetTextureStageState = 37;
constexpr unsigned long kShadeMode = 9, kZBias = 0x2F;
constexpr unsigned long kColourOp = 1, kAlphaOp = 4;
constexpr unsigned long kOpDisable = 1, kOpModulate = 4;
constexpr unsigned long kShadeFlat = 1, kShadeGouraud = 2;
constexpr unsigned long kTriangleList = 4, kLineStrip = 3, kFvf = 0xC4;   // XYZRHW | DIFFUSE | SPECULAR

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 004E7840 (thiscall on the device object): draws the batched vertices.
void __fastcall FlushBatch(char* a0) { DeviceFlushBatchAOriginal((void*)a0); }

void Count(char* device, int vertices, int primitives) {
    At<int>(device, 0x16C) += vertices;
    At<int>(device, 0x170) += primitives;
    At<int>(device, 0x174) += 1;
}

} // namespace

// 005041F0. Returns 0 when the rectangle is inverted (right < left or bottom < top), else 1.
int __fastcall RendererRectangle(char* renderer, void*, int left, int top, int right, int bottom, char alpha,
                                 char outline) {
    if (right < left || bottom < top) return 0;
    char* device = At<char*>(renderer, 0x20);
    std::uint32_t colour = static_cast<std::uint32_t>(static_cast<std::uint8_t>(alpha)) << 24;
    // With the outline the fill stays a pixel inside it.
    float inset = outline ? 1.0f : 0.0f;
    float x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    float x1 = static_cast<float>(right), y1 = static_cast<float>(bottom);
    render::ScreenVertex v[5] = {
        {x0 + inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x0 + inset, y1 - inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x1 - inset, y1 - inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x1 - inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
        {x0 + inset, y0 + inset, 0.0f, 1.0f, colour, 0xFF000000u},
    };
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    if (At<void*>(device, 0x12C)) {   // no texture on stage 0
        At<void*>(device, 0x12C) = nullptr;
        FlushBatch(device);
        At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTexture, 0ul, static_cast<void*>(nullptr));
        ++At<int>(device, 0x178);
    }
    ComCall(At<void*>(device, 0x54), kSetRenderState, kZBias, 0ul);
    render::state::Render(kZBias, 0ul);
    if (render::ScreenDraw(device, v, 4, indices, 6)) {
        gameapi::EnterCriticalSection(DeviceLock());
        At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kDrawIndexedPrimitive, kTriangleList, kFvf,
                                      static_cast<void*>(v), 4ul, indices, 6ul, 0ul);
        gameapi::LeaveCriticalSection(DeviceLock());
    }
    Count(device, 4, 2);
    if (!outline) return 1;

    // The frame: white, on the rectangle's own edges, flat shaded, with a depth bias, the texture
    // stage off.
    for (auto& p : v) p.colour = 0xFFFFFFFFu;
    v[0].x = x0, v[0].y = y0;
    v[1].x = x0, v[1].y = y1;
    v[2].x = x1, v[2].y = y1;
    v[3].x = x1, v[3].y = y0;
    v[4].x = x0, v[4].y = y0;
    ComCall(At<void*>(device, 0x54), kSetRenderState, kShadeMode, kShadeFlat);
    render::state::Render(kShadeMode, kShadeFlat);
    ComCall(At<void*>(device, 0x54), kSetRenderState, kZBias, 1ul);
    render::state::Render(kZBias, 1ul);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTextureStageState, 0ul, kColourOp, kOpDisable);
    render::state::Stage(0, kColourOp, kOpDisable);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTextureStageState, 0ul, kAlphaOp, kOpDisable);
    render::state::Stage(0, kAlphaOp, kOpDisable);
    if (render::ScreenDraw(device, v, 5, nullptr, 0)) {
        gameapi::EnterCriticalSection(DeviceLock());
        At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kDrawPrimitive, kLineStrip, kFvf, static_cast<void*>(v), 5ul, 0ul);
        gameapi::LeaveCriticalSection(DeviceLock());
    }
    Count(device, 5, 1);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTextureStageState, 0ul, kColourOp, kOpModulate);
    render::state::Stage(0, kColourOp, kOpModulate);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kSetTextureStageState, 0ul, kAlphaOp, kOpModulate);
    render::state::Stage(0, kAlphaOp, kOpModulate);
    ComCall(At<void*>(device, 0x54), kSetRenderState, kShadeMode, kShadeGouraud);
    render::state::Render(kShadeMode, kShadeGouraud);
    ComCall(At<void*>(device, 0x54), kSetRenderState, kZBias, 0ul);
    render::state::Render(kZBias, 0ul);
    return 1;
}
EI_REPLACE(0x005041F0, RendererRectangle);
