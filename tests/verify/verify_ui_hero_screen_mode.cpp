// Check for the vanilla body of the multiplayer hero screen's mode 00615D30 (A-I22,
// src/game/ui_hero_screen_mode.cpp). Rebirth patches it, so the installer hooks it in the vanilla
// launch only and this wrapper runs there.
//
// Ours only: it shows the selected hero (the party copied, the preview rebuilt, the equipment
// sent), draws the lists and replaces every tip, so a second run is not the same call. Read back:
// mode_; the regions (9 kept, plus 10 hero rows or 11 voice rows); the sprites shown (all but 7-9
// on the hero list, 3-9 on the new hero, 7-9 on its statistics; not the selection frame, sprite
// 2, which the lists show only with a selection); which of the surfaces 0-2 are shown (0 and 1,
// or 2). The screen opens in mode 9 from the main menu's Multiplayer button; no scenario is known to
// open it (ui_main_menu stays on the menu), so no scenario reaches this wrapper.
#include "verify.h"

#include "../../src/game/ui_screens.h"

namespace {

void* s_unused;

bool SpritesAre(CMpHeroScreen* screen, int first, int end, int visible) {
    for (int i = first; i < end && i < screen->window.sceneObjects_.size; ++i) {
        if (reinterpret_cast<CUiMesh*>(screen->window.sceneObjects_.data[i])->visible_ != visible) return false;
    }
    return true;
}

bool SurfacesAre(CMpHeroScreen* screen, unsigned shownMask) {
    for (int i = 0; i < screen->window.surfaces_.size && i < 3; ++i) {
        if (screen->window.surfaces_.data[i]->Shown() != (((shownMask >> i) & 1u) != 0)) return false;
    }
    return true;
}

} // namespace

static void __fastcall HeroScreenSetModeVerify(CMpHeroScreen* self, void*, int mode) {
    const int before = self->window.regions_.size;
    self->SetModeVanilla(mode);
    const int count = self->window.sceneObjects_.size;
    int regions = before < 9 ? before : 9;
    bool sprites = true;
    bool surfaces = true;
    if (mode == 9) {
        regions += 10;
        sprites = SpritesAre(self, 0, 2, 1) && SpritesAre(self, 3, 7, 1) && SpritesAre(self, 7, 10, 0) &&
                  SpritesAre(self, 10, count, 1);
        surfaces = SurfacesAre(self, 3u);
    } else if (mode == 10) {
        regions += 11;
        sprites = SpritesAre(self, 0, 2, 0) && SpritesAre(self, 3, 10, 1) && SpritesAre(self, 10, count, 0);
        surfaces = SurfacesAre(self, 4u);
    } else if (mode == 11) {
        sprites = SpritesAre(self, 0, 7, 0) && SpritesAre(self, 7, 10, 1) && SpritesAre(self, 10, count, 0);
        surfaces = SurfacesAre(self, 4u);
    } else {
        sprites = SpritesAre(self, 0, count, 0);
        surfaces = SurfacesAre(self, 0u);
    }
    ei::verify::Check("CMpHeroScreen::SetModeVanilla", true,
                      self->mode_ == mode && self->window.regions_.size == regions && sprites && surfaces,
                      "mode %d (%d) regions %d of %d sprites %d surfaces %d", mode, self->mode_, self->window.regions_.size,
                      regions, sprites ? 1 : 0, surfaces ? 1 : 0);
}
EI_VERIFY(0x00615D30, HeroScreenSetModeVerify, 0, s_unused);
