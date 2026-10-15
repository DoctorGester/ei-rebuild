// Check for the multiplayer hero screen's Create 00613EA0 (A-I22,
// src/game/ui_hero_screen_create.cpp). Rebirth patches it (WideScreen and EI_Plugin's
// EiPluginFixes, both reproduced), so the installer hooks it in a Rebirth launch too and this
// wrapper runs there. EiPluginFixes (W4a) replaces the two model-name comparisons ("unhuma",
// "unhufe") by a test of the word at +4 of the name: the figure lists' sizes are checked against
// a count over the creature table made here with the same rule (flag on: the women are the models
// with "fe" at +4, the men every other one; flag off: the models equal to the two names).
//
// Ours only: it builds the screen's children and reloads the saved heroes, which cannot be done
// twice. Read back: mode 9; the regions (9 kept, 10 hero rows), 19 sprites (21 with WideScreen: the
// curtains, hidden, tint 0.3), 4 surfaces, the menu
// model; the selection (the first hero, or -1 without one); both figure lists not empty of null
// names, the men's ascending and the women's descending by _mbscmp; no more voices than figures,
// each with a name. Reached by scenarios ui_hero_screen, ui_hero_stats and ui_hero_delete (the
// main menu's Multiplayer button); the worker that wrote it could not run the game.
#include "verify.h"

#include "../../src/game/originals_library.h"
#include "../../src/game/originals_units.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/ui_widescreen.h"
#include "../../src/game/unit_db.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"
#include "../../src/lib/stlp.h"

#include "ei/rebirth.h"

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

// The figure lists' sizes the creature table gives: every creature whose unit prototype and figure
// exist, the models sorted as the screen's rule does.
void CountFigures(int& men, int& women) {
    men = women = 0;
    const NpcPrototypeTable& creatures = unitdb::NpcTable();
    const auto* first = reinterpret_cast<const UnitNpcRecord*>(creatures.begin);
    const auto* last = reinterpret_cast<const UnitNpcRecord*>(creatures.end);
    for (const UnitNpcRecord* creature = first; creature != last; ++creature) {
        stlp::string key(static_cast<const char*>(creature->name));
        const auto* prototype = static_cast<const UnitPrototypeRecord*>(
            *PrototypeMapAtOriginal(&unitdb::PrototypeTable().byName, nullptr, &key));
        if (!prototype) continue;
        stlp::string figureKey(static_cast<const char*>(prototype->modelName));
        const auto* figure = reinterpret_cast<const FigurePrototypeRecord*>(
            *FigureMapAtOriginal(reinterpret_cast<void*>(0x007AF3A4), nullptr, &figureKey));
        const auto* model = reinterpret_cast<const unsigned char*>(
            static_cast<const char*>(*reinterpret_cast<const mfc::CString*>(&figure->model)));
        auto is = [model](const char* name) {
            return gamecrt::mbsicmp(model, reinterpret_cast<const unsigned char*>(name)) == 0;
        };
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) {
            const bool female = model[4] == 'f' && model[5] == 'e';
            ++(female ? women : men);
        } else {
            if (is("unhuma")) ++men;
            if (is("unhufe")) ++women;
        }
    }
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
    int expectedMen, expectedWomen;
    CountFigures(expectedMen, expectedWomen);
    ei::verify::Check("CMpHeroScreen::Create figure counts", true,
                      menEnd - men == expectedMen && self->names_.end() - self->names_.begin() == expectedWomen,
                      "men %d (expected %d) women %d (expected %d), EiPluginFixes %d", static_cast<int>(menEnd - men),
                      expectedMen, static_cast<int>(self->names_.end() - self->names_.begin()), expectedWomen,
                      ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes) ? 1 : 0);
    ei::verify::Check("CMpHeroScreen::Create figures", true, figures, "men %d women %d voices %d %d",
                      static_cast<int>(menEnd - men), static_cast<int>(self->names_.end() - self->names_.begin()),
                      static_cast<int>(self->voiceLists_[0].end() - self->voiceLists_[0].begin()),
                      static_cast<int>(self->voiceLists_[1].end() - self->voiceLists_[1].begin()));
}
EI_VERIFY(0x00613EA0, HeroScreenCreateVerify, 0, s_unused);
