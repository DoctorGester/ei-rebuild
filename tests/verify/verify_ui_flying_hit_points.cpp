// Checks for the flying hit points' list (ui_flying_hit_points.cpp). Ours only (they allocate and
// free nodes). Constructed with the world client (every scenario's zone), Add on every hit
// (combat scenarios, e.g. magic_traps), destroyed at exit.
#include "verify.h"

#include "../../src/game/ui_flying_hit_points.h"

#include <cstdint>

namespace {
void* s_unused;

int PointCount(const CFlyingHitPoints* self) {
    int n = 0;
    for (auto* node = self->points_.First(); node != self->points_.End(); node = node->next) ++n;
    return n;
}
} // namespace

static CFlyingHitPoints* __fastcall HitPointsConstructVerify(CFlyingHitPoints* self, void*) {
    CFlyingHitPoints* result = self->Construct();
    ei::verify::Check("CFlyingHitPoints::Construct", true,
                      result == self && !self->object_ && self->points_.Empty() && self->points_.head->prev == self->points_.head, "");
    return result;
}
EI_VERIFY(0x005E1710, HitPointsConstructVerify, 0, s_unused);

static void __fastcall HitPointsDestructVerify(CFlyingHitPoints* self, void*) {
    self->Destruct();
    ei::verify::Check("CFlyingHitPoints::Destruct", true, true, "");
}
EI_VERIFY(0x005E17E0, HitPointsDestructVerify, 0, s_unused);

// 005E1870: one more entry, the last, with the arguments, when the target is set and the amount
// not negative; otherwise nothing.
static void __fastcall HitPointsAddVerify(CFlyingHitPoints* self, void*, int target, int amount, std::uint32_t flags) {
    int before = PointCount(self);
    self->Add(target, amount, flags);
    bool added = target && amount >= 0;
    bool ok = PointCount(self) == before + (added ? 1 : 0);
    if (ok && added) {
        const FlyingHitPoint& last = self->points_.head->prev->data;
        ok = last.target == target && last.amount == amount && last.flags == flags;
    }
    ei::verify::Check("CFlyingHitPoints::Add", true, ok, "target %d amount %d flags %X", target, amount, flags);
}
EI_VERIFY(0x005E1870, HitPointsAddVerify, 0, s_unused);
