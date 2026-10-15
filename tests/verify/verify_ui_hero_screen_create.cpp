// Check for the multiplayer hero screen's Create 00613EA0 (A-I22,
// src/game/ui_hero_screen_create.cpp). Rebirth patches it (WideScreen, reproduced; EI_Plugin's
// EiPluginFixes, not), so the installer hooks it in the vanilla launch only and this wrapper runs
// there.
//
// Ours only: it builds the screen's children and reloads the saved heroes, which cannot be done
// twice. Read back: mode 9; the regions (9 kept, 10 hero rows), 19 sprites (21 with WideScreen: the
// curtains, hidden, tint 0.3), 4 surfaces, the menu
// model; the selection (the first hero, or -1 without one); both figure lists not empty of null
// names, the men's ascending and the women's descending by _mbscmp; no more voices than figures,
// each with a name. Reached by scenarios ui_hero_screen, ui_hero_stats and ui_hero_delete (the
// main menu's Multiplayer button); the worker that wrote it could not run the game.
#include "verify.h"

#include "../../src/game/ui_screens.h"
#include "../../src/game/ui_widescreen.h"
#include "../../src/lib/crt.h"

namespace {

void* s_unused;

int Compare(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// Whether the names are all set and in order (`direction` 1 ascending, -1 descending).
bool Ordered(const char* const* begin, const char* const* end, int direction) {
    for (const char* const* at = begin; at != end; ++at) {
        if (!*at) return false;
        if (at + 1 != end && at[1] && Compare(at[0], at[1]) * direction > 0) return false;
    }
    return true;
}

bool VoicesOk(const MpVoiceVector& voices, std::ptrdiff_t figures) {
    if (voices.end() - voices.begin() > figures) return false;
    for (const MpVoiceEntry* at = voices.begin(); at != voices.end(); ++at)
        if (!at->file || !at->name) return false;
    return true;
}

constexpr int kSprites = 19;

bool CurtainsHidden(const CUiWindow& window) {
    for (int i = kSprites; i < kSprites + 2; ++i) {
        const CUiMesh* curtain = window.SceneMesh(i);
        if (curtain->visible_ != 0 || curtain->tint_[0] != 0.3f || curtain->tint_[1] != 0.3f || curtain->tint_[2] != 0.3f)
            return false;
    }
    return true;
}

} // namespace

static void __fastcall HeroScreenCreateVerify(CMpHeroScreen* self) {
    self->Create();
    const int heroes = static_cast<int>(reinterpret_cast<const MpHeroRoster*>(0x007C82A0)->records.size());
    ei::verify::Check("CMpHeroScreen::Create layout", true,
                      self->mode_ == 9 && self->window.regions_.size == 19 &&
                          self->window.sceneObjects_.size == kSprites + (widescreen::Active() ? 2 : 0) &&
                          (!widescreen::Active() || CurtainsHidden(self->window)) &&
                          self->window.surfaces_.size == 4 && self->window.models_.size == 1,
                      "mode %d regions %d sprites %d surfaces %d models %d", self->mode_, self->window.regions_.size,
                      self->window.sceneObjects_.size, self->window.surfaces_.size, self->window.models_.size);
    ei::verify::Check("CMpHeroScreen::Create selection", heroes == 0 ? -1 : 0, self->heroIndex_, "heroes %d", heroes);
    auto* men = reinterpret_cast<const char* const*>(self->defaults_);
    auto* menEnd = reinterpret_cast<const char* const*>(self->defaultsEnd_);
    const bool figures = Ordered(men, menEnd, 1) && Ordered(self->names_.begin(), self->names_.end(), -1) &&
                         VoicesOk(self->voiceLists_[0], menEnd - men) &&
                         VoicesOk(self->voiceLists_[1], self->names_.end() - self->names_.begin());
    ei::verify::Check("CMpHeroScreen::Create figures", true, figures, "men %d women %d voices %d %d",
                      static_cast<int>(menEnd - men), static_cast<int>(self->names_.end() - self->names_.begin()),
                      static_cast<int>(self->voiceLists_[0].end() - self->voiceLists_[0].begin()),
                      static_cast<int>(self->voiceLists_[1].end() - self->voiceLists_[1].begin()));
}
EI_VERIFY(0x00613EA0, HeroScreenCreateVerify, 0, s_unused);
