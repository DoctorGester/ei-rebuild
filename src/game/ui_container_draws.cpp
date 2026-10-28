// Interface draws that still talked to the old Direct3D device without handing anything to the new renderer: the container
// windows' base draw, the base / briefing screen's and menu frame step's draws and the hero equipment panel's draw. Every
// state set goes to render::state too; every depth clear to render::ClearDepth. In the switch-over (`present`) the old
// device's clears are left out. See docs/notes/ui.md, "Container draws", and docs/notes/renderer.md, sections 5 and 8.
#include "item.h"
#include "world_glue.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "texture.h"   // g_stageBlocks
#include "unit.h"

#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"
#include "game_data.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kAlphaRef = 0x18;

CTextureDevice* DeviceOf(CUiWindow* w) { return w->interface_->renderer_; }

// IDirect3DDevice7::Clear of the depth buffer to `z`, over `count` rectangles (or all of it): only the new
// renderer clears (the switch-over has no depth buffer); the result is 0.
void ClearDepth(CTextureDevice* device, unsigned long count, RECT* rects, float z) {
    device->lastLoadResult_ = 0;
    render::Rect r[8];
    int n = count < 8 ? static_cast<int>(count) : 8;
    for (int i = 0; i < n; ++i) r[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, rects ? r : nullptr, rects ? n : 0);
}

// The screens' draws share this: the depth buffer cleared to 1, then the container draw.
int ScreenDraw(CUiWindow* self, CSceneRenderer* pass, bool mouseMove) {
    GameRender();
    if (mouseMove) self->Input()->MouseMove(0, MouseScreenX(), MouseScreenY());
    ClearDepth(DeviceOf(self), 0, nullptr, 1.0f);
    return static_cast<CUiContainer*>(self)->CUiContainer::Draw(pass);
}

} // namespace

BOOL CUiContainer::Draw(CSceneRenderer* pass) {
    if (!interface_->drawing_) return 1;
    CTextureDevice* device = DeviceOf(this);
    std::uint32_t handle = g_stageBlocks[kAlphaPassBlock];
    device->lastLoadResult_ = uidevice::ApplyStateBlock(device, handle);
    render::state::Block(handle);
    DrawBase(pass);
    CUiContainer* container = this;
    for (int i = 0; i < std::ssize(container->children_); ++i) {
        CUiWindow* child = container->children_[i];
        if (child->enabled_) child->Draw(pass);
    }
    return 1;
}

// Returns ScreenDraw's result.
int CBriefingScreen::Draw(CSceneRenderer* pass) { return ScreenDraw(this, pass, true); }

// Returns ScreenDraw's result.
int CMainMenu::Draw(CSceneRenderer* pass) { return ScreenDraw(this, pass, false); }

// The hero's quick items (the strip's left part), the other list (from the right), the worn armour and the
// selected quick slot, with the panel's highlight frames, strip mode, body and right-part selection. Returns
// 1.
int CHeroEquipPanel::Draw(CSceneRenderer* pass) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    auto frame = [this](int i) { return SceneMesh(i); };
    std::uint32_t quick = hero->weaponIndex;
    if (quick < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
        frame(0)->visible_ = 1;
        // The unsigned slot * 100 + 50 is converted through a 64-bit FILD.
        frame(0)->Place(static_cast<float>(x87::FromUnsigned(quick * 100u + 50u)), 50.0f, 3.0f);
    } else {
        frame(0)->visible_ = 0;
    }
    int selected = selected_;
    if (selected >= 0 && selected < std::ssize(hero->quickItems)) {
        frame(1)->visible_ = 1;
        frame(1)->Place(static_cast<float>(750 - selected * 100), 50.0f, 3.0f);
    } else {
        frame(1)->visible_ = 0;
    }
    DrawBase(pass);
    CItemScreen* owner = owner_;
    for (int i = 0; i < std::ssize(hero->weapons); ++i) {
        ItemCell cell{hero->weapons[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    for (int i = 0; i < std::ssize(hero->quickItems); ++i) {
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
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        CItem* item = hero->armor[i];
        int type = AsArmor(item)->GetType();
        ItemCell cell{item, static_cast<float>(centreX[type]), static_cast<float>(centreY[type]), 6.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, &cell);
    }
    ClearDepth(DeviceOf(this), 0, nullptr, 1.0f);
    return 1;
}

long CObjectivesWindow::RestoreAlphaRef() {
    std::uint32_t value = savedAlphaRef_;
    long hr = uidevice::SetRenderState(DeviceOf(this), kAlphaRef, value);
    render::state::Render(kAlphaRef, value);
    return hr;
}
