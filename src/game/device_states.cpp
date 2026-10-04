// Small device-state setters of the old renderer, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets their values: the renderer's fog colour (00501AB0, virtual slot 0
// of the renderer 007AF060) and the device object's material (004EB170). See
// docs/notes/renderer.md ("Pass state").
#include "ei/hook.h"
#include "lib/crt.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kSetRenderState = 20, kSetTextureStageState = 37;
constexpr unsigned long kFogColour = 0x22, kColourOp = 1, kAlphaOp = 4, kOpDisable = 1;

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

} // namespace

// 00501AB0 (thiscall on the renderer, RET 4). Sets FOGCOLOR; a failure is a fatal error (its
// result returned), else 1.
int __fastcall RendererSetFogColour(char* renderer, void*, std::uint32_t colour) {
    void* d3d = At<void*>(At<char*>(renderer, 0x20), 0x54);
    long hr = ComCall(d3d, kSetRenderState, kFogColour, static_cast<unsigned long>(colour));
    render::state::Render(kFogColour, colour);
    if (hr != 0) return game::FatalError(reinterpret_cast<const char*>(0x0078EE40), colour);   // "Can't set fog color 0x%X"
    return 1;
}
EI_REPLACE(0x00501AB0, RendererSetFogColour);

// 004EB170 (thiscall on the device object, RET 4). The current material (+0x14C, counted; the old
// one released through its slot 0 when its count reaches 0); without a material stage 1's colour and
// alpha operations are turned off. Returns 1.
int __fastcall DeviceSetMaterial(char* device, void*, void* material) {
    void* old = At<void*>(device, 0x14C);
    At<void*>(device, 0x14C) = material;
    if (material) ++At<int>(material, 4);
    if (old && --At<int>(old, 4) == 0) {
        using Delete = void(__fastcall*)(void*, void*, int);
        reinterpret_cast<Delete>((*static_cast<void***>(old))[0])(old, nullptr, 1);
    }
    if (!material) {
        void* d3d = At<void*>(device, 0x54);
        ComCall(d3d, kSetTextureStageState, 1ul, kColourOp, kOpDisable);
        ComCall(At<void*>(device, 0x54), kSetTextureStageState, 1ul, kAlphaOp, kOpDisable);
        render::state::Stage(1, kColourOp, kOpDisable);
        render::state::Stage(1, kAlphaOp, kOpDisable);
    }
    return 1;
}
EI_REPLACE(0x004EB170, DeviceSetMaterial);
