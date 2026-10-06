// Checks for the battle HUD panels' destructor bodies (ui_hud_destroy.cpp): ours only (they free
// memory); afterwards the window base holds no arrays (005FE930 ran) and the quick items' and
// action column's arrays carry their own vtable. Reached when the battle screen closes (ui_camp:
// the camp and the global map replace it).
#include "verify.h"

#include "../../src/game/ui.h"

namespace {
void* s_unused;

bool BaseEmpty(const CUiWindow* window) {
    return !window->sceneObjects_.data_ && !window->surfaces_.data_ && !window->regions_.data_ && !window->models_.data_;
}
} // namespace

static void __fastcall WeaponBarDestroyVerify(CHudWeaponBar* self) {
    self->DestroyBody();
    ei::verify::Check("CHudWeaponBar::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0063BC70, WeaponBarDestroyVerify, 0, s_unused);

static void __fastcall QuickItemsDestroyVerify(CHudQuickItems* self) {
    self->DestroyBody();
    ei::verify::Check("CHudQuickItems::DestroyBody", true,
                      BaseEmpty(self) && self->entriesVtable_ == reinterpret_cast<void*>(0x007428A0), "");
}
EI_VERIFY(0x0063CA70, QuickItemsDestroyVerify, 0, s_unused);

static void __fastcall SpellColumnDestroyVerify(CHudSpellColumn* self) {
    self->DestroyBody();
    ei::verify::Check("CHudSpellColumn::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0063E460, SpellColumnDestroyVerify, 0, s_unused);

static void __fastcall ActionColumnDestroyVerify(CHudActionColumn* self) {
    self->DestroyBody();
    ei::verify::Check("CHudActionColumn::DestroyBody", true,
                      BaseEmpty(self) && self->actions_.vtable_ == reinterpret_cast<void*>(0x0073C3D0), "");
}
EI_VERIFY(0x0063F490, ActionColumnDestroyVerify, 0, s_unused);
