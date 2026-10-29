// The interface's depth clear (ClearUiDepth) and the draws of the container windows' base, the base / briefing screen,
// the menu frame step and the hero equipment panel. Every state set goes to render::g_renderState; every depth clear to
// render::ClearDepth. See docs/notes/ui.md, "Container draws", and docs/notes/renderer.md, sections 5 and 8.
#include "item.h"
#include "world_glue.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "texture.h"   // kAlphaPassStage
#include "unit.h"
#include "ui_cursor.h"

#include "ei/x87.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>

void ClearUiDepth(float z, const RECT* rects, int count) {
    render::Rect cleared[8];
    const int n = !rects ? 0 : count < 8 ? count : 8;
    for (int i = 0; i < n; ++i) cleared[i] = render::Rect{rects[i].left, rects[i].top, rects[i].right, rects[i].bottom};
    render::ClearDepth(z, n ? cleared : nullptr, n);
}

namespace {

// The screens' draws share this: the depth buffer cleared to 1, then the container draw.
void ScreenDraw(CUiContainer* self, CSceneRenderer* pass, bool mouseMove) {
    GameRender();
    if (mouseMove) self->Input()->MouseMove(0, g_cursor.position_.x, g_cursor.position_.y);
    ClearUiDepth(1.0f);
    self->CUiContainer::Draw(pass);
}

} // namespace

void CUiContainer::Draw(CSceneRenderer* pass) {
    if (!interface_->drawing_) return;
    render::g_renderState.stages[0].combiner = kAlphaPassStage;
    DrawBase(pass);
    // By index: a child's draw may change the list.
    for (int i = 0; i < std::ssize(children_); ++i) {
        CUiWindow* child = children_[i];
        if (child->enabled_) child->Draw(pass);
    }
}

void CBriefingScreen::Draw(CSceneRenderer* pass) { ScreenDraw(this, pass, true); }

void CMainMenu::Draw(CSceneRenderer* pass) { ScreenDraw(this, pass, false); }

// The hero's weapons (the strip's left part), quick items (from the right) and worn armour, with the frames of
// the shown weapon and the selected quick item.
void CHeroEquipPanel::Draw(CSceneRenderer* pass) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    auto frame = [this](int i) { return SceneMesh(i); };
    std::uint32_t shown = hero->weaponIndex;
    if (shown < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
        frame(0)->visible_ = true;
        // The unsigned slot * 100 + 50 is converted as a 64-bit integer.
        frame(0)->Place(static_cast<float>(x87::FromUnsigned(shown * 100u + 50u)), 50.0f, 3.0f);
    } else {
        frame(0)->visible_ = false;
    }
    int selected = selected_;
    if (selected >= 0 && selected < std::ssize(hero->quickItems)) {
        frame(1)->visible_ = true;
        frame(1)->Place(static_cast<float>(750 - selected * 100), 50.0f, 3.0f);
    } else {
        frame(1)->visible_ = false;
    }
    DrawBase(pass);
    CItemScreen* owner = owner_;
    for (int i = 0; i < std::ssize(hero->weapons); ++i) {
        ItemCell cell{hero->weapons[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, cell);
    }
    for (int i = 0; i < std::ssize(hero->quickItems); ++i) {
        ItemCell cell{hero->quickItems[i], static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, cell);
    }
    if (!body_) return;

    // The seven body slots' rectangles: 70 x 70 around their centres.
    const int* centreX = kBodySlotX;
    const int* centreY = kBodySlotY;
    RECT slots[7];
    for (int i = 0; i < 7; ++i) {
        RECT r{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35};
        interface_->ScaleRectToScreenEntry(&r);
        slots[i] = r;
    }
    ClearUiDepth(0.0f);
    ClearUiDepth(1.0f, slots, 7);
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        CItem* item = hero->armor[i];
        int type = AsArmor(item)->GetType();
        ItemCell cell{item, static_cast<float>(centreX[type]), static_cast<float>(centreY[type]), 6.0f, 100.0f,
                      mode_};
        owner->DrawItem(pass, cell);
    }
    ClearUiDepth(1.0f);
}
