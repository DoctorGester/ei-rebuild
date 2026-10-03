#pragma once

// Helpers shared by the verify wrappers of the order planning and animation code (verify build):
// the random generators' state, deep copies of animation step lists and their comparison.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/combat_plan.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>

namespace combatverify {

// The CRT's per-thread data (_getptd, 006F00D1); +0x14 is rand()'s seed. Defined in
// verify_combat_steps.cpp.
char* __cdecl GamePtd();

// Everything the planner draws random numbers from: the ISAAC generator 007A9D40 and rand().
struct RandomState {
    IsaacRandom isaac;
    std::uint32_t holdrand;

    void Save() {
        std::memcpy(&isaac, reinterpret_cast<const void*>(0x007A9D40), sizeof(isaac));
        std::memcpy(&holdrand, GamePtd() + 0x14, sizeof(holdrand));
    }
    void Restore() const {
        std::memcpy(reinterpret_cast<void*>(0x007A9D40), &isaac, sizeof(isaac));
        std::memcpy(GamePtd() + 0x14, &holdrand, sizeof(holdrand));
    }
    bool SameAs(const RandomState& other) const {
        return holdrand == other.holdrand && std::memcmp(&isaac, &other.isaac, sizeof(isaac)) == 0;
    }
};

// A deep copy of a step list (its own array of the same capacity).
inline void CopySteps(AnimationSteps* copy, const AnimationSteps* steps) {
    *copy = *steps;
    copy->steps = nullptr;
    if (steps->steps && steps->capacity > 0) {
        copy->steps = static_cast<AnimationStep*>(
            gamecrt::operator_new(static_cast<std::size_t>(steps->capacity) * sizeof(AnimationStep)));
        std::memcpy(copy->steps, steps->steps, static_cast<std::size_t>(steps->count) * sizeof(AnimationStep));
    }
}

inline void FreeSteps(AnimationSteps* steps) {
    if (steps->steps) gamecrt::operator_delete(steps->steps);
    steps->steps = nullptr;
}

// Steps compared without byte 3, which the original never initialises (00550A60).
inline int StepsDifference(const AnimationStep* a, const AnimationStep* b, int count) {
    for (int i = 0; i < count; ++i) {
        if (a[i].animation != b[i].animation || a[i].unknown_01 != b[i].unknown_01 ||
            a[i].unknown_02 != b[i].unknown_02 || a[i].duration != b[i].duration || a[i].unknown_08 != b[i].unknown_08)
            return i;
    }
    return -1;
}

// Lists compared without the array pointer: count, capacity, growth, extra and the steps.
inline bool SameSteps(const AnimationSteps* a, const AnimationSteps* b) {
    return a->count == b->count && a->capacity == b->capacity && a->growBy == b->growBy && a->extra == b->extra &&
           StepsDifference(a->steps, b->steps, a->count) < 0;
}

inline int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

} // namespace combatverify
