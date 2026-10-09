// Checks for the AI's eye (src/game/unit_ai_sight.cpp). Every AI brain tick runs Update (through
// UpdateMemory), which runs Refresh and See, which runs Prepare: every scenario with AI units
// (unit_ai.txt, combat.txt, world.txt).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>

namespace {
void* s_unused;

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

// No object twice and no null entry.
bool Distinct(const AIObjectList& list) {
    std::set<CEngineObject*> seen;
    int n = 0;
    for (auto* node = list.head; node; node = node->next, ++n)
        if (!node->data || !seen.insert(node->data).second) return false;
    return n == list.count;
}
} // namespace

// 005CA410 Prepare: MOV EAX,[ECX+0x20]; MOV EDX,[ECX] (5 bytes). Output: the original on a copy
// of the viewer, ours on the real one; eye height and facing within a few ULPs, the ready flag.
static void(__fastcall* s_prepare)(AIViewer*, void*, CUnit*);
static void __fastcall PrepareVerify(AIViewer* self, void*, CUnit* unit) {
    AIViewer theirs = *self;
    s_prepare(&theirs, nullptr, unit);
    self->Prepare(unit);
    ei::verify::CheckNear("AIViewer::Prepare eye", theirs.eye.z, self->eye.z, "unit %p", unit);
    ei::verify::CheckNear("AIViewer::Prepare facing", theirs.facing.x, self->facing.x, "unit %p x", unit);
    ei::verify::CheckNear("AIViewer::Prepare facing", theirs.facing.y, self->facing.y, "unit %p y", unit);
    ei::verify::CheckNear("AIViewer::Prepare facing", theirs.facing.z, self->facing.z, "unit %p z", unit);
    ei::verify::Check("AIViewer::Prepare ready", theirs.ready, self->ready, "unit %p", unit);
}
EI_VERIFY(0x005CA410, PrepareVerify, 5, s_prepare);

// 005CA1D0 Refresh: draws rand() and rebuilds a counted list, so ours only: one update fewer
// until the next listing, or a new countdown of 8..11, the unit flagged and a nearby list without
// the unit itself, nulls or repeats.
static void __fastcall RefreshVerify(AIViewer* self, void*, CUnit* unit, std::uint8_t* memory) {
    AIBrain* brain = BrainOfMemory(memory);
    int countdown = brain->nearbyCountdown;
    int count = brain->unknown_0F4.count;
    self->Refresh(unit, brain);
    bool ok;
    if (countdown - 1 > 0) {
        ok = brain->nearbyCountdown == countdown - 1 && brain->unknown_0F4.count == count;
    } else {
        ok = brain->nearbyCountdown >= 8 && brain->nearbyCountdown <= 11 && (unit->flags_ & 0x80000) != 0 &&
             Distinct(brain->unknown_0F4) && !brain->unknown_0F4.Find(unit);
    }
    ei::verify::Check("AIViewer::Refresh", true, ok, "unit %p countdown %d -> %d, %d nearby", unit, countdown, brain->nearbyCountdown,
                      brain->unknown_0F4.count);
}
EI_VERIFY(0x005CA1D0, RefreshVerify, 0, s_unused);

// 005CA5A0 See: takes references and logs, so ours only: the visible list only grew, holds no
// repeats, and the see-all flag is cleared.
static void __fastcall SeeVerify(AIViewer* self, void*, CUnit* unit, std::uint8_t* memory) {
    AIBrain* brain = BrainOfMemory(memory);
    int visible = brain->visible.count;
    self->See(unit, brain);
    bool ok = brain->visible.count >= visible && Distinct(brain->visible) && (unit->flags_ & 0x2000000) == 0;
    ei::verify::Check("AIViewer::See", true, ok, "unit %p visible %d -> %d", unit, visible, brain->visible.count);
}
EI_VERIFY(0x005CA5A0, SeeVerify, 0, s_unused);

// 005CAA00 Update: ours only: the eye at the unit (x, y), no repeats and no dead unit in the
// visible list afterwards.
static void __fastcall UpdateVerify(AIViewer* self, void*, UnitController* owner, std::uint8_t* memory) {
    AIBrain* brain = BrainOfMemory(memory);
    self->Update(owner, brain);
    CUnit* unit = owner->unit;
    bool ok = self->eye.x == unit->position.x && self->eye.y == unit->position.y && Distinct(brain->visible);
    for (auto* node = brain->visible.head; ok && node; node = node->next)
        ok = static_cast<CMapObjectVirtuals*>(node->data)->IsDead() == 0;
    ei::verify::Check("AIViewer::Update", true, ok, "unit %p, %d visible", unit, brain->visible.count);
}
EI_VERIFY(0x005CAA00, UpdateVerify, 0, s_unused);

// 005CD210 MarkSightCells: PUSH EBP; MOV EBP,ESP; SUB ESP,0x30 (6 bytes). Output: the original
// marks a scratch grid (no cells yet), ours the real one; origin, size and every cell compared.
// Reached by the show-visibility effect's tick (0067C840).
static void(__fastcall* s_markSightCells)(CUnit*, CUnit*, VisibilityGrid*);
static void __fastcall MarkSightCellsVerify(CUnit* seer, CUnit* observer, VisibilityGrid* grid) {
    VisibilityGrid theirs{};
    s_markSightCells(seer, observer, &theirs);
    MarkSightCells(seer, observer, grid);
    bool same = theirs.originX == grid->originX && theirs.originY == grid->originY && theirs.width == grid->width &&
                theirs.height == grid->height &&
                std::memcmp(theirs.data, grid->data, static_cast<std::size_t>(grid->width * grid->height)) == 0;
    ei::verify::Check("MarkSightCells", true, same, "seer %p: %dx%d at (%d, %d), theirs %dx%d at (%d, %d)", seer, grid->width,
                      grid->height, grid->originX, grid->originY, theirs.width, theirs.height, theirs.originX, theirs.originY);
    gamecrt::operator_delete(theirs.data);
    gamecrt::operator_delete(theirs.rows);
}
EI_VERIFY(0x005CD210, MarkSightCellsVerify, 6, s_markSightCells);
