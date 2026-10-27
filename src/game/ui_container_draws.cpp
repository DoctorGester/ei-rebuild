// Interface draws that still talked to the old Direct3D device without handing anything to the new
// renderer (ticket R5, docs/notes/renderer.md sections 5 and 8):
// - the container windows' base draw (CUiContainer): stage-0 state block
//   applied, then the window's own sprites and models and its enabled children;
// - the draws of interface screens 3 the base / briefing screen,
// and 2: the menu frame step, (screen 3) a mouse move to the input
//   side, a depth clear, then the children;
// - the hero equipment panel's draw (CHeroEquipPanel): the two
//   highlight frames placed, the quick slots' item models, and with the body (the camp) the seven
//   equipment slots' models inside depth clears (all to 0, the slots' rectangles to 1, all to 1
//   afterwards), as the hero preview and the trade grid do;
// - the alpha reference set to the window's +0x1EC.
// Every state set goes to render::state too; every depth clear to render::ClearDepth. In the
// switch-over (`present`) the old device's clears are left out: nothing the old path still draws
// there reads its depth buffer (its interface and world meshes are off). The item models go through
// and the mesh draws, which hand over already.
#include "item.h"
#include "world_glue.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "texture.h"   // g_stageBlocks
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"
#include "game_data.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kAlphaRef = 0x18;

// The interface's device object (CInterface3D +8; its +0x54 is the IDirect3DDevice7).
UiRenderer* DeviceOf(CUiWindow* w) { return w->interface_->renderer_; }

// IDirect3DDevice7::Clear of the depth buffer to `z`, over `count` rectangles (or all of it), the result in
// the device object's +8: only the new renderer clears (the switch-over has no depth buffer), the result 0.
void ClearDepth(UiRenderer* device, unsigned long count, RECT* rects, float z) {
    device->result_ = 0;
    render::Rect r[8];
    int n = count < 8 ? static_cast<int>(count) : 8;
    for (int i = 0; i < n; ++i) r[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, rects ? r : nullptr, rects ? n : 0);
}

// share this: the depth buffer cleared to 1, then the container draw.
int ScreenDraw(CUiWindow* self, int pass, bool mouseMove) {
    GameRender();
    if (mouseMove) self->Input()->MouseMove(0, MouseScreenX(), MouseScreenY());
    ClearDepth(DeviceOf(self), 0, nullptr, 1.0f);
    return static_cast<CUiContainer*>(self)->CUiContainer::Draw(pass);
}

} // namespace

// Nothing when the interface's +0x30 is clear. Returns 1.
BOOL CUiContainer::Draw(int pass) {
    if (!interface_->drawing_) return 1;
    UiRenderer* device = DeviceOf(this);
    std::uint32_t handle = g_stageBlocks[kAlphaPassBlock];
    device->result_ = device->device_->ApplyStateBlock(handle);
    render::state::Block(handle);
    DrawBase(pass);
    CUiContainer* container = this;
    for (int i = 0; i < container->children_.size; ++i) {
        CUiWindow* child = container->children_.data[i];
        if (child->enabled_) child->Draw(pass);
    }
    return 1;
}
EI_REWRITTEN(0x00600EF0, CUiContainer::Draw);

// Returns ScreenDraw's result.
int CBriefingScreen::Draw(int pass) { return ScreenDraw(this, pass, true); }
EI_REWRITTEN(0x00602BF0, CBriefingScreen::Draw);

// Returns ScreenDraw's result.
int CMainMenu::Draw(int pass) { return ScreenDraw(this, pass, false); }
EI_REWRITTEN(0x00632A90, CMainMenu::Draw);

// The owner slot 51 gives the hero's data: +0x730 / +0x734 the quick items
// (the strip's left part), +0x740 / +0x744 the other list (from the right), +0x750 / +0x754 the
// worn armour, +0x774 the selected quick slot. The panel: +0x108 the two highlight frames, +0x148
// the strip mode, +0x150 the body shown, +0x154 the selected slot of the right part. Returns 1.
int CHeroEquipPanel::Draw(int pass) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    auto frame = [this](int i) { return SceneMesh(i); };
    std::uint32_t quick = hero->weaponIndex;
    if (quick < hero->weaponCount) {
        frame(0)->visible_ = 1;
        // The unsigned slot * 100 + 50 is converted through a 64-bit FILD.
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
    CItemScreen* owner = owner_;
    for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) {
        ItemCell cell{hero->weapons[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    for (int i = 0; i < static_cast<int>(hero->quickItemCount); ++i) {
        ItemCell cell{hero->quickItems[i], static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    if (!body_) return 1;

    // The seven body slots' rectangles: 70 x 70 around their centres.
    const int* centreX = kBodySlotX;
    const int* centreY = kBodySlotY;
    RECT slots[7];
    for (int i = 0; i < 7; ++i) {
        RECT r{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35};
        interface_->ScaleRectToScreenEntry(&r);
        slots[i] = r;
    }
    ClearDepth(DeviceOf(this), 0, nullptr, 0.0f);
    ClearDepth(DeviceOf(this), 7, slots, 1.0f);
    for (int i = 0; i < static_cast<int>(hero->armorCount); ++i) {
        CItem* item = hero->armor[i];
        int type = AsArmor(item)->GetType();
        ItemCell cell{item, static_cast<float>(centreX[type]), static_cast<float>(centreY[type]), 6.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    ClearDepth(DeviceOf(this), 0, nullptr, 1.0f);
    return 1;
}
EI_REWRITTEN(0x00622420, CHeroEquipPanel::Draw);

long CObjectivesWindow::RestoreAlphaRef() {
    std::uint32_t value = savedAlphaRef_;
    long hr = DeviceOf(this)->device_->SetRenderState(kAlphaRef, value);
    render::state::Render(kAlphaRef, value);
    return hr;
}
EI_REWRITTEN(0x0064D970, CObjectivesWindow::RestoreAlphaRef);
