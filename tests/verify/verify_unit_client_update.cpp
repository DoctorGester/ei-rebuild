// Checks for unit_client_update.cpp and the weapon / arrow parts in unit_client_models.cpp (A-U).
// - FirstDrawnPart (0051B6D0) / NextDrawnPart (0051B6F0) change only the cursor: the original
//   runs, the cursor is put back, ours runs; result and cursor compared.
// - UpdateClient (005341A0; plays sounds, draws random numbers, changes parts): ours only. It
//   returns 1, flags_ 8 follows the rule (computed before the call), the fade-in is at most 1 (or
//   NaN) and the blink timer stays within 0..9.
// - ShowWeaponParts (00537CB0): ours only; +0x74 and +0x75 hold the new state afterwards.
// - UpdateArrow (00538C20): ours only; when it changed +0x75, the arrow part's flag 0x80 agrees.
// Reached by: every unit on the client every world update (UpdateClient is the controller's slot
// 6; all scenarios with units: units, combat, movement, unit_ai); the drawn parts by the fade-in of
// new units (unit_create, units: AddUnitToServer); UpdateArrow does its work for heroes with a bow
// (combat_more's archer is not a hero: the early returns only).
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"
#include "../../src/game/world.h"
#include "../../src/game/originals_engine.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unusedUpdate;
void* s_unusedShow;
void* s_unusedArrow;
} // namespace

// 0051B6D0: MOV EAX,[ECX+0x3C]; TEST EAX,EAX (5 bytes).
static FigurePart*(__fastcall* s_firstDrawnPart)(CObject3DClientSpecific*);
static FigurePart* __fastcall FirstDrawnPartVerify(CObject3DClientSpecific* self) {
    int cursor = self->drawnPartCursor;
    FigurePart* expected = s_firstDrawnPart(self);
    int expectedCursor = self->drawnPartCursor;
    self->drawnPartCursor = cursor;
    FigurePart* actual = self->FirstDrawnPart();
    ei::verify::Check("CObject3DClientSpecific::FirstDrawnPart", true,
                      expected == actual && expectedCursor == self->drawnPartCursor, "%d parts", self->drawnPartCount);
    return actual;
}
EI_VERIFY(0x0051B6D0, FirstDrawnPartVerify, 5, s_firstDrawnPart);

// 0051B6F0: MOV EAX,[ECX+0x48]; INC EAX; MOV [ECX+0x48],EAX (7 bytes).
static FigurePart*(__fastcall* s_nextDrawnPart)(CObject3DClientSpecific*);
static FigurePart* __fastcall NextDrawnPartVerify(CObject3DClientSpecific* self) {
    int cursor = self->drawnPartCursor;
    FigurePart* expected = s_nextDrawnPart(self);
    int expectedCursor = self->drawnPartCursor;
    self->drawnPartCursor = cursor;
    FigurePart* actual = self->NextDrawnPart();
    ei::verify::Check("CObject3DClientSpecific::NextDrawnPart", true,
                      expected == actual && expectedCursor == self->drawnPartCursor, "cursor %d of %d", cursor,
                      self->drawnPartCount);
    return actual;
}
EI_VERIFY(0x0051B6F0, NextDrawnPartVerify, 7, s_nextDrawnPart);

static int __fastcall UpdateClientVerify(CUnitClientSpecific* self) {
    CUnit* unit = self->unit;
    const FigureAnimation* current = unit->Object3D()->animation->current;
    bool longDead = unit->dead && static_cast<CWorld*>(ObjectWorld(self))->tick_ >
                                      static_cast<std::uint8_t>(unit->unknown_254) + 50;
    bool drawn = !longDead && current &&
                 ((current->key & 0x38000) != 0x30000 || (current->key & 0x3C0000) == 0x240000);
    int result = self->UpdateClient();
    bool ok = result == 1 && ((unit->flags_ & 8) == 0) == drawn && !(self->fadeIn > 1.0f) &&
              (!self->blinks || (self->blinkTimer >= 0 && self->blinkTimer <= 9));
    ei::verify::Check("CUnitClientSpecific::UpdateClient", true, ok, "unit %d: result %d, drawn %d, flags %08X, fade %g",
                      unit->id_, result, drawn ? 1 : 0, unit->flags_, self->fadeIn);
    return result;
}
EI_VERIFY(0x005341A0, UpdateClientVerify, 0, s_unusedUpdate);

static void __fastcall ShowWeaponPartsVerify(CUnitClientSpecific* self, void*, bool hidden) {
    self->ShowWeaponParts(hidden);
    bool ok = self->weaponHidden == static_cast<std::uint8_t>(hidden);
    ei::verify::Check("CUnitClientSpecific::ShowWeaponParts", true, ok, "unit %d hidden %d", self->unit->id_,
                      hidden ? 1 : 0);
}
EI_VERIFY(0x00537CB0, ShowWeaponPartsVerify, 0, s_unusedShow);

static void __fastcall UpdateArrowVerify(CUnitClientSpecific* self) {
    std::uint8_t before = self->arrowHidden;
    self->UpdateArrow();
    bool ok = true;
    if (self->arrowHidden != before) {
        const FigurePart* arrow = self->unit->Object3D()->FindPart("rh3.arrow00");
        ok = arrow && ((arrow->flags_ & 0x80) != 0) == (self->arrowHidden != 0);
    }
    ei::verify::Check("CUnitClientSpecific::UpdateArrow", true, ok, "unit %d arrow hidden %d -> %d", self->unit->id_,
                      before, self->arrowHidden);
}
EI_VERIFY(0x00538C20, UpdateArrowVerify, 0, s_unusedArrow);

// --- Fade-in, level and wounds slots ----------------------------------------------------------
// FiguresLoaded (005344E0) / RestartFadeIn (005344A0): ours only; every drawn part's alpha is the
// fade-in (0 after the restart), results 1 / 0. Level (00539160): pure, compared (a level whose
// ratio lies within a float ULP of .5 could round differently: std::log instead of FYL2X).
// RefreshWounds (00537360): ours only, result 1. Reached: slot 13 when a unit's figures load (every
// spawn through CUnit::LoadUnitFigures: units, unit_create), slot 24 (CUnit::Level of a hero on the
// client: the interface's level display, ui_hud), slot 31 (the network state reader after a
// statistics change: every hit in combat, combat_more). Slot 43's callers are not traced (a virtual
// slot); its counts show whether a scenario reaches it.

namespace {
void* s_unusedFiguresLoaded;
void* s_unusedRestartFade;
void* s_unusedWounds;

bool PartsTake(CUnitClientSpecific* self, float alpha) {
    const auto* half = static_cast<const CObject3DClientSpecific*>(self->unit->Object3D()->attached);
    for (int i = 0; i < half->drawnPartCount; ++i)
        if (std::memcmp(&half->drawnParts[i]->alpha, &alpha, sizeof(float)) != 0) return false;
    return true;
}
} // namespace

static int __fastcall FiguresLoadedVerify(CUnitClientSpecific* self) {
    int result = self->FiguresLoaded();
    ei::verify::Check("CUnitClientSpecific::FiguresLoaded", true, result == 1 && PartsTake(self, self->fadeIn),
                      "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x005344E0, FiguresLoadedVerify, 0, s_unusedFiguresLoaded);

static int __fastcall RestartFadeInVerify(CUnitClientSpecific* self) {
    int result = self->RestartFadeIn();
    ei::verify::Check("CUnitClientSpecific::RestartFadeIn", true, result == 0 && self->fadeIn == 0.0f &&
                      PartsTake(self, 0.0f), "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x005344A0, RestartFadeInVerify, 0, s_unusedRestartFade);

// 00539160: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
static int(__fastcall* s_level)(CUnitClientSpecific*);
static int __fastcall LevelVerify(CUnitClientSpecific* self) {
    int expected = s_level(self);
    int actual = self->Level();
    ei::verify::Check("CUnitClientSpecific::Level", expected, actual, "unit %d experience %d", self->unit->id_,
                      self->unit->Params()->experience);
    return actual;
}
EI_VERIFY(0x00539160, LevelVerify, 6, s_level);

static int __fastcall RefreshWoundsVerify(CUnitClientSpecific* self) {
    int result = self->RefreshWounds();
    ei::verify::Check("CUnitClientSpecific::RefreshWounds", 1, result, "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x00537360, RefreshWoundsVerify, 0, s_unusedWounds);
