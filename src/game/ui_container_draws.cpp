// Interface draws that still talked to the old Direct3D device without handing anything to the new
// renderer (ticket R5, docs/notes/renderer.md sections 5 and 8):
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
#include "item.h"
#include "world_glue.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "originals_ui.h"
#include "originals_gameplay.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"
#include "originals_world.h"

#include <cstdint>

namespace {

template <typename T>
T GameValue(std::uintptr_t address) {
    return *reinterpret_cast<const T*>(address);
}

constexpr std::uint32_t kAlphaRef = 0x18;
std::uint32_t StateBlockAlpha() { return GameValue<std::uint32_t>(0x007AEDD8); }   // texture x diffuse, colour and alpha

// The interface's device object (CInterface3D +8; its +0x54 is the IDirect3DDevice7).
UiRenderer* DeviceOf(CUiWindow* w) { return w->interface_->renderer_; }

// The original's IDirect3DDevice7::Clear of the depth buffer to `z`, over `count` rectangles (or all
// of it), the result in the device object's +8: only the new renderer clears (the switch-over has no
// depth buffer), the result 0.
void ClearDepth(UiRenderer* device, unsigned long count, RECT* rects, float z) {
    device->result_ = 0;
    render::Rect r[8];
    int n = count < 8 ? static_cast<int>(count) : 8;
    for (int i = 0; i < n; ++i) r[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, rects ? r : nullptr, rects ? n : 0);
}

// The device object's two batched vertex buffers flushed (004E7840 / 004E7A40, not rewritten).
void Flush(UiRenderer* device) {
    device->FlushBatchA();
    device->FlushBatchB();
}

// Through their original addresses, as the original calls them.
// 006A39E0 (thiscall on an armour item): its body part type (rewritten, item.cpp).

// 00602BF0 / 00632A90 share this: the depth buffer cleared to 1, then the container draw.
int ScreenDraw(CUiWindow* self, int pass, bool mouseMove) {
    GameRender();
    if (mouseMove) self->InputMouseMove(0, GameValue<int>(0x007C7C50), GameValue<int>(0x007C7C54));
    ClearDepth(DeviceOf(self), 0, nullptr, 1.0f);
    return static_cast<CUiContainer*>(self)->DrawContainerImpl(pass);
}

} // namespace

// 00600EF0. Nothing when the interface's +0x30 is clear. Returns 1.
int CUiContainer::DrawContainerImpl(int pass) {
    if (!interface_->drawing_) return 1;
    UiRenderer* device = DeviceOf(this);
    std::uint32_t handle = StateBlockAlpha();
    device->result_ = device->device_->ApplyStateBlock(handle);
    render::state::Block(handle);
    DrawBase(pass);
    CUiContainer* container = static_cast<CUiContainer*>(this);
    for (int i = 0; i < container->children_.size; ++i) {
        CUiWindow* child = container->children_.data[i];
        if (child->enabled_) child->VirtualDraw(pass);
    }
    return 1;
}
EI_REPLACE_METHOD(0x00600EF0, CUiContainer::DrawContainerImpl);

// 00602BF0 (screen 3, the base / briefing screen). Returns 00600EF0's result.
int CBriefingScreen::DrawImpl(int pass) { return ScreenDraw(this, pass, true); }
EI_REPLACE_METHOD(0x00602BF0, CBriefingScreen::DrawImpl);

// 00632A90 (screen 2). Returns 00600EF0's result.
int CMainMenu::DrawImpl(int pass) { return ScreenDraw(this, pass, false); }
EI_REPLACE_METHOD(0x00632A90, CMainMenu::DrawImpl);

// 00622420. The owner (+0x14C) slot 51 gives the hero's data: +0x730 / +0x734 the quick items
// (the strip's left part), +0x740 / +0x744 the other list (from the right), +0x750 / +0x754 the
// worn armour, +0x774 the selected quick slot. The panel: +0x108 the two highlight frames, +0x148
// the strip mode, +0x150 the body shown, +0x154 the selected slot of the right part. Returns 1.
int CHeroEquipPanel::DrawImpl(int pass) {
    PartyUnitRecord* hero = ShownHero(owner_);
    auto frame = [this](int i) { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[i]); };
    std::uint32_t quick = hero->weaponIndex;
    if (quick < hero->weaponCount) {
        frame(0)->visible_ = 1;
        // The original converts the unsigned slot * 100 + 50 through a 64-bit FILD.
        frame(0)->Place(static_cast<float>(x87::FromUnsigned(quick * 100u + 50u)), 50.0f, 3.0f);
    } else {
        frame(0)->visible_ = 0;
    }
    int selected = selected_;
    if (selected >= 0 && selected < static_cast<int>(hero->quickItemCount)) {
        frame(1)->visible_ = 1;
        frame(1)->Place(static_cast<float>(750 - selected * 100), 50.0f, 3.0f);
    } else {
        frame(1)->visible_ = 0;
    }
    DrawBase(pass);
    auto* owner = reinterpret_cast<CItemScreen*>(owner_);
    for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) {
        ItemCell cell{static_cast<CItem*>(hero->weapons[i]), static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    for (int i = 0; i < static_cast<int>(hero->quickItemCount); ++i) {
        ItemCell cell{static_cast<CItem*>(hero->quickItems[i]), static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    if (!body_) return 1;

    // The seven body slots' rectangles: 70 x 70 around the centres 00741EC4 / 00741EE0 (layout).
    const int* centreX = reinterpret_cast<const int*>(0x00741EC4);
    const int* centreY = reinterpret_cast<const int*>(0x00741EE0);
    RECT slots[7];
    for (int i = 0; i < 7; ++i) {
        RECT r{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35};
        interface_->CallScaleRectToScreen(&r);
        slots[i] = r;
    }
    Flush(DeviceOf(this));
    ClearDepth(DeviceOf(this), 0, nullptr, 0.0f);
    ClearDepth(DeviceOf(this), 7, slots, 1.0f);
    for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) {
        CItem* item = static_cast<CItem*>(hero->armor[i]);
        int type = reinterpret_cast<CItemArmor*>(item)->GetType();
        ItemCell cell{item, static_cast<float>(centreX[type]), static_cast<float>(centreY[type]), 6.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    Flush(DeviceOf(this));
    ClearDepth(DeviceOf(this), 0, nullptr, 1.0f);
    Flush(DeviceOf(this));
    return 1;
}
EI_REPLACE_METHOD(0x00622420, CHeroEquipPanel::DrawImpl);

// 0064D970 (the objectives window's). Returns SetRenderState's result (not stored in the device object).
long __fastcall WindowSetAlphaRef(CUiWindow* self) {
    std::uint32_t value = static_cast<CObjectivesWindow*>(self)->savedAlphaRef_;
    long hr = DeviceOf(self)->device_->SetRenderState(kAlphaRef, value);
    render::state::Render(kAlphaRef, value);
    return hr;
}
EI_REPLACE(0x0064D970, WindowSetAlphaRef);
