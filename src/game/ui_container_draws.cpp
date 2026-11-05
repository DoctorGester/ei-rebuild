// The interface's depth clear (ClearUiDepth) and the draws of the container windows' base, the base / briefing screen,
// the menu frame step and the hero equipment panel. Every state set goes to render::g_renderState; every depth clear to
// render::ClearDepth. See docs/notes/ui.md, "Container draws", and docs/notes/renderer.md, sections 5 and 8.
#include "item.h"
#include "world.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "texture.h"   // kAlphaPassStage
#include "unit.h"
#include "ui_cursor.h"

#include "render/render.h"
#include "render/render_state.h"

#include <cstdint>

void ClearUiDepth(float z, const Rect* rects, int count) {
    render::ClearDepth(z, rects, !rects ? 0 : count < 8 ? count : 8);
}

namespace {

// The screens' draws share this: the depth buffer cleared to 1, then the container draw.
void ScreenDraw(UiContainer* self, SceneRenderer* pass, bool mouseMove) {
    g_clientWorld.Render();
    if (mouseMove) self->Input()->MouseMove(g_cursor.position);
    ClearUiDepth(1.0f);
    self->UiContainer::DrawSprites(pass);
}

} // namespace

void UiContainer::DrawSprites(SceneRenderer* pass) {
    if (!ui->drawing) return;
    render::g_renderState.stages[0].combiner = kAlphaPassStage;
    DrawBase(pass);
    // By index: a child's draw may change the list.
    for (int i = 0; i < std::ssize(children); ++i) {
        UiWindow* child = children[i];
        if (child->enabled) child->DrawSprites(pass);
    }
}

void BriefingScreen::DrawSprites(SceneRenderer* pass) { ScreenDraw(this, pass, true); }

void MainMenu::DrawSprites(SceneRenderer* pass) { ScreenDraw(this, pass, false); }

// The hero's weapons (the strip's left part), quick items (from the right) and worn armour, with the frames of
// the shown weapon and the selected quick item.
void HeroEquipPanel::DrawSprites(SceneRenderer* pass) {
    PartyUnitRecord* hero = owner->HeroRecord();
    std::uint32_t shown = hero->weaponIndex;
    if (shown < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
        weaponFrame->visible = true;
        weaponFrame->Place(static_cast<float>(shown * 100u + 50u), 50.0f, 3.0f);
    } else {
        weaponFrame->visible = false;
    }
    int currentSelected = selected;
    if (currentSelected >= 0 && currentSelected < std::ssize(hero->quickItems)) {
        quickItemFrame->visible = true;
        quickItemFrame->Place(static_cast<float>(750 - currentSelected * 100), 50.0f, 3.0f);
    } else {
        quickItemFrame->visible = false;
    }
    DrawBase(pass);
    ItemScreen* currentOwner = owner;
    for (int i = 0; i < std::ssize(hero->weapons); ++i) {
        ItemCell cell{hero->weapons[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    for (int i = 0; i < std::ssize(hero->quickItems); ++i) {
        ItemCell cell{hero->quickItems[i], static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    if (!body) return;

    // The seven body slots' rectangles: 70 x 70 around their centres.
    const int* centreX = kBodySlotX;
    const int* centreY = kBodySlotY;
    Rect slots[7];
    for (int i = 0; i < 7; ++i) {
        Rect r{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35};
        ui->ScaleRectToScreen(&r);
        slots[i] = r;
    }
    ClearUiDepth(0.0f);
    ClearUiDepth(1.0f, slots, 7);
    for (int i = 0; i < std::ssize(hero->armor); ++i) {
        Item* item = hero->armor[i];
        int armorType = static_cast<int>(item->AsArmor()->GetType());
        ItemCell cell{item, static_cast<float>(centreX[armorType]), static_cast<float>(centreY[armorType]), 6.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    ClearUiDepth(1.0f);
}
