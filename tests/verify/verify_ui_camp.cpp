// Checks for the battle screen's Create, the global map, the camp pages and the hero panels
// (ui_battle_setup.cpp, ui_global_map.cpp, ui_camp.cpp, ui_hero_panels.cpp, ui_credits.cpp).
//
// These functions build windows (allocations, textures, sprites, sounds, Rebirth-patched layout
// helpers), so ours run alone and the wrappers check the structure they leave: what was added,
// the fields they set, the windows' vtables.
#include "verify.h"

#include "../../src/game/ui_camp.h"

#include <cstring>
#include <initializer_list>

namespace {
void* s_unused;

struct Counts {
    int objects, sceneObjects, surfaces, regions, models;
};
Counts CountsOf(CUiWindow* w) {
    return Counts{w->objects_.size_, w->sceneObjects_.size_, w->surfaces_.size_, w->regions_.size_,
                  w->models_.size_};
}
std::uintptr_t VtableOf(const CUiWindow* w) { return w ? reinterpret_cast<std::uintptr_t>(w->vtable_) : 0; }
int InputCount(CUiContainer* w) { return static_cast<int>(w->inputsEnd_ - w->inputsBegin_); }
} // namespace

// --- Battle screen (00635250) --------------------------------------------------------------------

// Eleven panels appended as children in their order (each with its vtable), two input handlers,
// three textures, the fields Create resets, one more hero id in a new game.
static void __fastcall BattleScreenCreateVerify(CBattleScreenSetup* self, void*) {
    Counts before = CountsOf(self);
    int children = self->children_.size_;
    int inputs = InputCount(self);
    int heroes = self->heroes_.size_;
    bool loading = *reinterpret_cast<const int*>(0x007AFE1C) != 0;
    self->Create();
    Counts after = CountsOf(self);
    CUiWindow* panels[11] = {self->weaponBar_, self->quickItems_, self->spellColumn_, self->actionColumn_,
                             self->portraits_, self->unitPanel_, self->map_, self->clock_,
                             self->movePanel_, self->messageLog_, self->topPanel_};
    bool order = self->children_.size_ == children + 11;
    for (int i = 0; order && i < 11; ++i) order = self->children_.data_[children + i] == panels[i];
    bool vtables = VtableOf(self->unitPanel_) == 0x007426F8 && VtableOf(self->map_) == 0x00742660 &&
                   VtableOf(self->clock_) == 0x007425C8 && VtableOf(self->movePanel_) == 0x00742530 &&
                   VtableOf(self->topPanel_) == 0x00742498 &&
                   reinterpret_cast<std::uintptr_t>(self->unitPanel_->inputVtable_) == 0x007426C4;
    bool owners = true;
    for (CUiWindow* p : {self->unitPanel_, self->map_, self->clock_, self->movePanel_, self->topPanel_})
        owners = owners && *reinterpret_cast<CUiWindow**>(reinterpret_cast<std::uint8_t*>(p) + 0x148) == self;
    bool fields = self->unknown_210 == -1 && self->unknown_17C == 0 && self->unknown_180 == -1 &&
                  self->portraitMode_ == 0 && self->unknown_164 == 0 && self->unknown_1FC == 0 &&
                  !self->unknown_1C0 && !self->unknown_1C1 &&
                  (*reinterpret_cast<const std::uint32_t*>(0x0079B2B0) & 0x40) == 0;
    bool heroOk = loading || self->heroes_.size_ == heroes + (self->hasHero_ ? 1 : 0);
    bool ok = order && vtables && owners && fields && heroOk && InputCount(self) == inputs + 2 &&
              after.objects >= before.objects + 3;
    ei::verify::Check("CBattleScreenSetup::Create", true, ok,
                      "order %d vtables %d owners %d fields %d heroes %d->%d inputs %d->%d objects %d->%d", order,
                      vtables, owners, fields, heroes, self->heroes_.size_, inputs, InputCount(self),
                      before.objects, after.objects);
}
EI_VERIFY(0x00635250, BattleScreenCreateVerify, 0, s_unused);
