// Checks for the interface draws of ticket R5 (ui_container_draws.cpp).
//
// 00600EF0, 00602BF0, 00632A90 and 00622420 draw (children, sprites, item models, depth clears):
// ours only, the original would draw everything twice. 0064D970 sets one render state:
// differential on the real device.
#include "verify.h"

#include "../../src/game/ui_camp.h"

#include <cstdint>

class CUiWindow;

namespace {
template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}
std::uint8_t* DeviceOf(CUiWindow* w) { return reinterpret_cast<std::uint8_t*>(w->interface_->renderer_); }
std::uint32_t RenderState(void* d3d, std::uint32_t state) {
    std::uint32_t value = 0;
    using Fn = long(__stdcall*)(void*, std::uint32_t, std::uint32_t*);
    reinterpret_cast<Fn>((*static_cast<void***>(d3d))[21])(d3d, state, &value);
    return value;
}
} // namespace

// 00600EF0 (every container window: escape menu, save / load, options, battle screen, camp; all
// ui_* and render_* scenarios). Checked: result 1, the children array unchanged.
int __fastcall UiContainerDraw(CUiWindow* self, void*, int pass);
static void* s_containerDraw;
static int __fastcall UiContainerDrawVerify(CUiWindow* self, void*, int pass) {
    void* children = At<void*>(self, 0x148);
    int count = At<int>(self, 0x14C);
    int result = UiContainerDraw(self, nullptr, pass);
    bool ok = result == 1 && At<void*>(self, 0x148) == children && At<int>(self, 0x14C) == count;
    ei::verify::Check("CUiContainer::Draw", true, ok, "result %d, children %d -> %d", result, count, At<int>(self, 0x14C));
    return result;
}
EI_VERIFY(0x00600EF0, UiContainerDrawVerify, 0, s_containerDraw);

// 00602BF0 / 00632A90 (screens 3 and 2; no scenario reaches them: they need a "brief" zone or the
// server's screen 2). Checked: result 1.
int __fastcall UiBriefingScreenDraw(CUiWindow* self, void*, int pass);
static void* s_briefingDraw;
static int __fastcall UiBriefingScreenDrawVerify(CUiWindow* self, void*, int pass) {
    int result = UiBriefingScreenDraw(self, nullptr, pass);
    ei::verify::Check("UiBriefingScreenDraw", true, result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00602BF0, UiBriefingScreenDrawVerify, 0, s_briefingDraw);

int __fastcall UiScreen2Draw(CUiWindow* self, void*, int pass);
static void* s_screen2Draw;
static int __fastcall UiScreen2DrawVerify(CUiWindow* self, void*, int pass) {
    int result = UiScreen2Draw(self, nullptr, pass);
    ei::verify::Check("UiScreen2Draw", true, result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00632A90, UiScreen2DrawVerify, 0, s_screen2Draw);

// 00622420 (the camp: ui_camp, frames_basic, render_present, render_present_cpuwindows). Checked:
// result 1, the two highlight frames shown exactly when the original's conditions hold, and with
// the body the last depth clear's result a success (0 in the switch-over, where it is skipped).
int __fastcall HeroEquipPanelDraw(CHeroEquipPanel* self, void*, int pass);
static void* s_equipDraw;
static int __fastcall HeroEquipPanelDrawVerify(CHeroEquipPanel* self, void*, int pass) {
    int result = HeroEquipPanelDraw(self, nullptr, pass);
    void* owner = At<void*>(self, 0x14C);
    using HeroFn = std::uint8_t*(__fastcall*)(void*, void*);
    auto* hero = reinterpret_cast<HeroFn>((*static_cast<void***>(owner))[0xCC / 4])(owner, nullptr);
    void** frames = At<void**>(self, 0x108);
    int shown0 = At<int>(frames[0], 0x220), shown1 = At<int>(frames[1], 0x220);
    int want0 = At<std::uint32_t>(hero, 0x774) < At<std::uint32_t>(hero, 0x734) ? 1 : 0;
    int want1 = self->selected_ >= 0 && self->selected_ < At<int>(hero, 0x744) ? 1 : 0;
    long hr = At<long>(DeviceOf(self), 8);
    bool ok = result == 1 && shown0 == want0 && shown1 == want1 && (!self->body_ || hr >= 0);
    ei::verify::Check("CHeroEquipPanel::Draw", true, ok, "result %d, frames %d/%d (want %d/%d), clear %08lX", result,
                      shown0, shown1, want0, want1, static_cast<unsigned long>(hr));
    return result;
}
EI_VERIFY(0x00622420, HeroEquipPanelDrawVerify, 0, s_equipDraw);

// 0064D970 (slot 26 of vtable 007411BC; no caller found in the scenarios' code paths so far):
// the original (6 stolen bytes: MOV EAX,[ECX+0x98]), the alpha reference read back, then ours;
// results and states compared. Both set the same value, so the original's run changes nothing.
long __fastcall WindowSetAlphaRef(CUiWindow* self);
using SetAlphaRefFn = long(__fastcall*)(CUiWindow*);
static SetAlphaRefFn s_setAlphaRef;
static long __fastcall WindowSetAlphaRefVerify(CUiWindow* self) {
    void* d3d = At<void*>(DeviceOf(self), 0x54);
    long expected = s_setAlphaRef(self);
    std::uint32_t expectedRef = RenderState(d3d, 0x18);
    long result = WindowSetAlphaRef(self);
    std::uint32_t actualRef = RenderState(d3d, 0x18);
    bool ok = result == expected && actualRef == expectedRef;
    ei::verify::Check("WindowSetAlphaRef", true, ok, "result %08lX/%08lX, alpha reference %u/%u",
                      static_cast<unsigned long>(result), static_cast<unsigned long>(expected), actualRef, expectedRef);
    return result;
}
EI_VERIFY(0x0064D970, WindowSetAlphaRefVerify, 6, s_setAlphaRef);
