// Small device-state setters of the old renderer, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets their values: the renderer's fog colour (00501AB0, virtual slot 0
// of the renderer 007AF060), its blend factors (00501C60, slot 10: the particles' additive / normal
// switch) and the device object's material (004EB170). See docs/notes/renderer.md ("Pass state").
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
constexpr unsigned long kSourceBlend = 0x13, kDestBlend = 0x14;

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

// 004800E0 (cdecl): a DirectX error report (the result and a message); its result is returned.
__declspec(naked) int __cdecl DirectXError(long, const char*) EI_ORIGINAL(0x004800E0)

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

// 00501C60 (thiscall on the renderer, RET 8). Sets SRCBLEND, then DESTBLEND (D3DBLEND values: the
// particle draw 00498940 passes 5, 2 for additive sources and 5, 6 to go back). A failure is
// reported (004800E0, its result returned; DESTBLEND is then not set), else 1.
int __fastcall RendererSetBlend(char* renderer, void*, std::uint32_t source, std::uint32_t dest) {
    long hr = ComCall(At<void*>(At<char*>(renderer, 0x20), 0x54), kSetRenderState, kSourceBlend,
                      static_cast<unsigned long>(source));
    render::state::Render(kSourceBlend, source);
    if (hr != 0) return DirectXError(hr, reinterpret_cast<const char*>(0x0078EE88));   // "Can't set source alpha blend mode."
    hr = ComCall(At<void*>(At<char*>(renderer, 0x20), 0x54), kSetRenderState, kDestBlend, static_cast<unsigned long>(dest));
    render::state::Render(kDestBlend, dest);
    if (hr != 0) return DirectXError(hr, reinterpret_cast<const char*>(0x0078EE5C));   // "Can't set destination alpha blend mode."
    return 1;
}
EI_REPLACE(0x00501C60, RendererSetBlend);

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
