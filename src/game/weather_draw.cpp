// The weather's draw (004E5680, thiscall on the weather object, RET 4; called by the world view's
// effects pass 00569E40 with the renderer 007AF060). Mode +0xB4 (0 none, 1 rain, 2 snow): the
// weather is stepped (004E40B0, with the world's time +0x64 of the world +0x20), its texture
// (+0x1C) bound on stage 0, and the rain (004E4240) or snow (004E4C80) drawn. Those two build
// their particles' pre-transformed vertices (+0x70, count +0x74; indices +0x84) and draw them with
// the device directly; the new renderer (src/render) gets the same triangles after they return.
// See docs/notes/effects.md and docs/notes/renderer.md.
#include "ei/hook.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

// Not rewritten (float-heavy particle placement), reached through their addresses.
__declspec(naked) void __fastcall WeatherStep(void*, void*, float /*time*/) EI_ORIGINAL(0x004E40B0)
void __fastcall TextureTouch(void* a0) { TextureEnsureLoadedOriginal(a0); }
__declspec(naked) int __fastcall DrawRain(void*, void*, void* /*renderer*/) EI_ORIGINAL(0x004E4240)
__declspec(naked) int __fastcall DrawSnow(void*, void*, void* /*renderer*/) EI_ORIGINAL(0x004E4C80)

constexpr int kRendererSetTexture = 11;   // renderer slot 11 (stage, texture)

// The triangles the rain or snow just drew (if any) to the new renderer.
void HandOver(void* weather, void* renderer) {
    int count = At<int>(weather, 0x74);
    void* vertices = At<void*>(weather, 0x70);
    auto* indices = At<std::uint16_t*>(weather, 0x84);
    if (count <= 0 || !vertices || !indices) return;
    render::TlDraw(At<void*>(renderer, 0x20), vertices, 0x1C4, count, indices, count, 4);
}

} // namespace

// 004E5680. Returns what the rain / snow draw returned, 1 when there is nothing to draw, 0 without
// a world or with an unknown mode.
int __fastcall WeatherDraw(void* self, void*, void* renderer) {
    void* world = At<void*>(self, 0x20);
    if (!world) return 0;
    if (At<int>(self, 0xB4) == 0) return 1;
    WeatherStep(self, nullptr, static_cast<float>(At<int>(world, 0x64)));
    if (At<int>(self, 0xB4) == 0) return 1;
    TextureTouch(static_cast<std::uint8_t*>(self) + 0x1C);
    using SetTexture = void(__fastcall*)(void*, void*, int, void*);
    reinterpret_cast<SetTexture>((*static_cast<void***>(renderer))[kRendererSetTexture])(renderer, nullptr, 0,
                                                                                         At<void*>(self, 0x1C));
    int mode = At<int>(self, 0xB4);
    int result;
    if (mode == 1) result = DrawRain(self, nullptr, renderer);
    else if (mode == 2) result = DrawSnow(self, nullptr, renderer);
    else return 0;
    if (render::Active()) HandOver(self, renderer);
    return result;
}
EI_REPLACE(0x004E5680, WeatherDraw);
