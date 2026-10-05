// Interface draws that still talked to the old Direct3D device without handing anything to the new
// renderer (ticket R5, docs/notes/renderer.md section 5 point 6 and section 12):
// - 00600EF0, the container windows' base draw (CUiContainer slot 20): stage-0 state block
//   007AEDD8 applied, then the window's own sprites and models (005FEB90) and its enabled children;
// - 00602BF0 and 00632A90, the slot-20 draws of interface screens 3 (the base / briefing screen,
//   vtable 007405AC) and 2 (00740688): the menu frame step, (screen 3) a mouse move to the input
//   side, a depth clear, then 00600EF0;
// - 00622420, the hero equipment panel's draw (CHeroEquipPanel slot 20, vtable 007418E0): the two
//   highlight frames placed, the quick slots' item models, and with the body (the camp) the seven
//   equipment slots' models inside depth clears (all to 0, the slots' rectangles to 1, all to 1
//   afterwards), as the hero preview and the trade grid do;
// - 0064D970 (slot 26 of vtable 007411BC): the alpha reference set to the window's +0x1EC.
// Every state set goes to render::state too; every depth clear to render::ClearDepth. In the
// switch-over (`present`) the old device's clears are left out: nothing the old path still draws
// there reads its depth buffer (its interface and world meshes are off). The item models go through
// 005ECA10 and the mesh draws, which hand over already.
#include "ui.h"
#include "ui_camp.h"
#include "ui_item_info.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}
template <typename T>
T GameValue(std::uintptr_t address) {
    return *reinterpret_cast<const T*>(address);
}

constexpr int kD3DClear = 10;            // IDirect3DDevice7::Clear
constexpr int kD3DSetRenderState = 20;   // SetRenderState
constexpr int kD3DApplyStateBlock = 39;  // ApplyStateBlock
constexpr std::uint32_t kAlphaRef = 0x18;
constexpr std::uintptr_t kStateBlockAlpha = 0x007AEDD8;   // texture x diffuse, colour and alpha

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

// The interface's device object (CInterface3D +8; its +0x54 is the IDirect3DDevice7).
std::uint8_t* DeviceOf(CUiWindow* w) { return reinterpret_cast<std::uint8_t*>(w->interface_->renderer_); }

// IDirect3DDevice7::Clear of the depth buffer (with the stencil when the device has one: +0x90 bit
// 0x4000 and +0x9C) to `z`, over `count` rectangles (or all of it), the result in the device
// object's +8; handed to the new renderer. In the switch-over only the new renderer clears.
void ClearDepth(std::uint8_t* device, unsigned long count, RECT* rects, float z) {
    unsigned long flags = (At<std::uint32_t>(device, 0x90) & 0x4000) && At<std::uint32_t>(device, 0x9C) ? 6ul : 2ul;
    if (!render::Presenting()) {
        gameapi::EnterCriticalSection(DeviceLock());
        At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kD3DClear, count, rects, flags, 0ul, z, 0ul);
        gameapi::LeaveCriticalSection(DeviceLock());
    } else {
        At<long>(device, 8) = 0;
    }
    render::Rect r[8];
    int n = count < 8 ? static_cast<int>(count) : 8;
    for (int i = 0; i < n; ++i) r[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, rects ? r : nullptr, rects ? n : 0);
}

// The device object's two batched vertex buffers flushed (004E7840 / 004E7A40, not rewritten).
void Flush(std::uint8_t* device) {
    auto* r = reinterpret_cast<UiRenderer*>(device);
    r->FlushBatchA();
    r->FlushBatchB();
}

// Through their original addresses, as the original calls them.
__declspec(naked) void __fastcall MenuFrame(void*) EI_ORIGINAL(0x00578910)   // on 007AFEA5
__declspec(naked) int __fastcall ContainerDrawAt(CUiWindow*, void*, int) EI_ORIGINAL(0x00600EF0)
// 006A39E0 (thiscall on an armour item): its body part type (rewritten, item.cpp).
__declspec(naked) int __fastcall ArmourType(void*) EI_ORIGINAL(0x006A39E0)
struct DrawItemAt {
    void DrawItem(int pass, ItemCell* cell);   // 005ECA10 (rewritten, ui_item_view.cpp)
};
__declspec(naked) void DrawItemAt::DrawItem(int, ItemCell*) EI_ORIGINAL(0x005ECA10)

// 00602BF0 / 00632A90 share this: the depth buffer cleared to 1, then the container draw.
int ScreenDraw(CUiWindow* self, int pass, bool mouseMove) {
    MenuFrame(reinterpret_cast<void*>(0x007AFEA5));
    if (mouseMove)
        ei::VCall<void>(self->InputSide(), 4, 0, GameValue<int>(0x007C7C50), GameValue<int>(0x007C7C54));
    ClearDepth(DeviceOf(self), 0, nullptr, 1.0f);
    return ContainerDrawAt(self, nullptr, pass);
}

} // namespace

// 00600EF0. Nothing when the interface's +0x30 is clear. Returns 1.
int __fastcall UiContainerDraw(CUiWindow* self, void*, int pass) {
    if (!At<char>(self->interface_, 0x30)) return 1;
    std::uint8_t* device = DeviceOf(self);
    std::uint32_t handle = GameValue<std::uint32_t>(kStateBlockAlpha);
    At<long>(device, 8) = ComCall(At<void*>(device, 0x54), kD3DApplyStateBlock, handle);
    render::state::Block(handle);
    self->DrawBase(pass);
    for (int i = 0; i < At<int>(self, 0x14C); ++i) {
        CUiWindow* child = At<CUiWindow**>(self, 0x148)[i];
        if (At<int>(child, 0xE8)) ei::VCall<int>(child, 0x50 / 4, pass);
    }
    return 1;
}
EI_REPLACE(0x00600EF0, UiContainerDraw);

// 00602BF0 (screen 3, the base / briefing screen). Returns 00600EF0's result.
int __fastcall UiBriefingScreenDraw(CUiWindow* self, void*, int pass) { return ScreenDraw(self, pass, true); }
EI_REPLACE(0x00602BF0, UiBriefingScreenDraw);

// 00632A90 (screen 2). Returns 00600EF0's result.
int __fastcall UiScreen2Draw(CUiWindow* self, void*, int pass) { return ScreenDraw(self, pass, false); }
EI_REPLACE(0x00632A90, UiScreen2Draw);

// 00622420. The owner (+0x14C) slot 51 gives the hero's data: +0x730 / +0x734 the quick items
// (the strip's left part), +0x740 / +0x744 the other list (from the right), +0x750 / +0x754 the
// worn armour, +0x774 the selected quick slot. The panel: +0x108 the two highlight frames, +0x148
// the strip mode, +0x150 the body shown, +0x154 the selected slot of the right part. Returns 1.
int __fastcall HeroEquipPanelDraw(CHeroEquipPanel* self, void*, int pass) {
    auto* hero = ei::VCall<std::uint8_t*>(At<void*>(self, 0x14C), 0xCC / 4);
    void** frames = At<void**>(self, 0x108);
    std::uint32_t quick = At<std::uint32_t>(hero, 0x774);
    if (quick < At<std::uint32_t>(hero, 0x734)) {
        At<int>(frames[0], 0x220) = 1;
        // The original converts the unsigned slot * 100 + 50 through a 64-bit FILD.
        static_cast<CUiMesh*>(frames[0])->Place(static_cast<float>(x87::FromUnsigned(quick * 100u + 50u)), 50.0f, 3.0f);
    } else {
        At<int>(frames[0], 0x220) = 0;
    }
    int selected = self->selected_;
    if (selected >= 0 && selected < At<int>(hero, 0x744)) {
        At<int>(frames[1], 0x220) = 1;
        static_cast<CUiMesh*>(frames[1])->Place(static_cast<float>(750 - selected * 100), 50.0f, 3.0f);
    } else {
        At<int>(frames[1], 0x220) = 0;
    }
    self->DrawBase(pass);
    auto* owner = At<DrawItemAt*>(self, 0x14C);
    for (int i = 0; i < At<int>(hero, 0x734); ++i) {
        ItemCell cell{At<CItem**>(hero, 0x730)[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      At<int>(self, 0x148)};
        owner->DrawItem(pass, &cell);
    }
    for (int i = 0; i < At<int>(hero, 0x744); ++i) {
        ItemCell cell{At<CItem**>(hero, 0x740)[i], static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      At<int>(self, 0x148)};
        owner->DrawItem(pass, &cell);
    }
    if (!self->body_) return 1;

    // The seven body slots' rectangles: 70 x 70 around the centres 00741EC4 / 00741EE0 (layout).
    const int* centreX = reinterpret_cast<const int*>(0x00741EC4);
    const int* centreY = reinterpret_cast<const int*>(0x00741EE0);
    RECT slots[7];
    for (int i = 0; i < 7; ++i) {
        RECT r{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35};
        self->interface_->CallScaleRectToScreen(&r);
        slots[i] = r;
    }
    Flush(DeviceOf(self));
    ClearDepth(DeviceOf(self), 0, nullptr, 0.0f);
    ClearDepth(DeviceOf(self), 7, slots, 1.0f);
    for (int i = 0; i < At<int>(hero, 0x754); ++i) {
        CItem* item = At<CItem**>(hero, 0x750)[i];
        int type = ArmourType(item);
        ItemCell cell{item, static_cast<float>(centreX[type]), static_cast<float>(centreY[type]), 6.0f, 100.0f,
                      At<int>(self, 0x148)};
        owner->DrawItem(pass, &cell);
    }
    Flush(DeviceOf(self));
    ClearDepth(DeviceOf(self), 0, nullptr, 1.0f);
    Flush(DeviceOf(self));
    return 1;
}
EI_REPLACE(0x00622420, HeroEquipPanelDraw);

// 0064D970. Returns SetRenderState's result (not stored in the device object).
long __fastcall WindowSetAlphaRef(CUiWindow* self) {
    std::uint32_t value = At<std::uint32_t>(self, 0x1EC);
    long hr = ComCall(At<void*>(DeviceOf(self), 0x54), kD3DSetRenderState, kAlphaRef, value);
    render::state::Render(kAlphaRef, value);
    return hr;
}
EI_REPLACE(0x0064D970, WindowSetAlphaRef);
