// Three interface windows' draws (slot 20, thiscall with the pass, RET 4) that only set alpha
// states around the container draw, rewritten so the new renderer's state mirror
// (src/render/render_state.h) gets the values: 00600F70 (a window with child windows: alpha
// reference 1 around its region tips and children, when the interface's +0x30 is set), 0062EFC0
// (blending on, source alpha / inverse source alpha, alpha test off, alpha reference 1) and
// 0065A6D0 (alpha reference 1 around the container draw). See docs/notes/renderer.md ("Pass
// state").
#include "ui.h"

#include "ei/hook.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr std::uint32_t kAlphaBlend = 0x1B, kAlphaTest = 0x0F, kSourceBlend = 0x13, kDestBlend = 0x14, kAlphaRef = 0x18;

// The interface's Direct3D device (CInterface3D +8 -> +0x54).
void* Device(CUiWindow* w) { return At<void*>(w->interface_->renderer_, 0x54); }

void SetState(CUiWindow* w, std::uint32_t state, std::uint32_t value) {
    void* d3d = Device(w);
    using Fn = long(__stdcall*)(void*, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>((*static_cast<void***>(d3d))[20])(d3d, state, value);
    render::state::Render(state, value);
}
std::uint32_t GetState(CUiWindow* w, std::uint32_t state) {
    void* d3d = Device(w);
    std::uint32_t value = 0;
    using Fn = long(__stdcall*)(void*, std::uint32_t, std::uint32_t*);
    reinterpret_cast<Fn>((*static_cast<void***>(d3d))[21])(d3d, state, &value);
    return value;
}
void Flush(CUiWindow* w) {
    w->interface_->renderer_->FlushBatchA();
    w->interface_->renderer_->FlushBatchB();
}

// Through their original addresses (rewritten or not), as the original calls them.
__declspec(naked) void __fastcall UpdateRegionTips(CUiWindow*, void*, int) EI_ORIGINAL(0x005FEBF0)
__declspec(naked) int __fastcall ContainerDraw(CUiWindow*, void*, int) EI_ORIGINAL(0x00600EF0)

} // namespace

// 00600F70. The children: +0x148 an array of windows (+0x14C its size), each drawn (slot 21) when
// enabled (+0xE8).
void __fastcall WindowDrawChildren(CUiWindow* self, void*, int pass) {
    if (!At<char>(self->interface_, 0x30)) return;
    Flush(self);
    std::uint32_t savedRef = GetState(self, kAlphaRef);
    SetState(self, kAlphaRef, 1);
    UpdateRegionTips(self, nullptr, pass);
    for (int i = 0; i < At<int>(self, 0x14C); ++i) {
        CUiWindow* child = At<CUiWindow**>(self, 0x148)[i];
        if (!At<int>(child, 0xE8)) continue;
        using Draw = void(__fastcall*)(CUiWindow*, void*, int);
        reinterpret_cast<Draw>((*reinterpret_cast<void***>(child))[0x54 / 4])(child, nullptr, pass);
    }
    Flush(self);
    SetState(self, kAlphaRef, savedRef);
}
EI_REPLACE(0x00600F70, WindowDrawChildren);

// 0062EFC0. Returns 1.
int __fastcall WindowDrawBlended(CUiWindow* self, void*, int pass) {
    SetState(self, kAlphaBlend, 1);
    SetState(self, kAlphaTest, 0);
    SetState(self, kSourceBlend, 5);
    SetState(self, kDestBlend, 6);
    SetState(self, kAlphaRef, 1);
    ContainerDraw(self, nullptr, pass);
    return 1;
}
EI_REPLACE(0x0062EFC0, WindowDrawBlended);

// 0065A6D0. Returns 1.
int __fastcall WindowDrawAlphaRef(CUiWindow* self, void*, int pass) {
    Flush(self);
    std::uint32_t savedRef = GetState(self, kAlphaRef);
    SetState(self, kAlphaRef, 1);
    ContainerDraw(self, nullptr, pass);
    Flush(self);
    SetState(self, kAlphaRef, savedRef);
    return 1;
}
EI_REPLACE(0x0065A6D0, WindowDrawAlphaRef);
