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
#include <vector>

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

// A step list kept in our memory before the kept side runs; CopyTo makes a deep copy (its own array
// of the same capacity) after it ran, so that the kept side's blocks land where they would without
// the check (the game reads the heap's address order, SV2f).
struct KeptSteps {
    AnimationSteps header;
    std::vector<unsigned char> bytes;
    explicit KeptSteps(const AnimationSteps* steps) : header(*steps) {
        if (steps->steps && steps->capacity > 0 && steps->count > 0) {
            const auto* p = reinterpret_cast<const unsigned char*>(steps->steps);
            bytes.assign(p, p + static_cast<std::size_t>(steps->count) * sizeof(AnimationStep));
        }
    }
    void CopyTo(AnimationSteps* copy) const {
        *copy = header;
        copy->steps = nullptr;
        if (header.steps && header.capacity > 0) {
            copy->steps = static_cast<AnimationStep*>(
                gamecrt::operator_new(static_cast<std::size_t>(header.capacity) * sizeof(AnimationStep)));
            if (!bytes.empty()) std::memcpy(copy->steps, bytes.data(), bytes.size());
        }
    }
};

inline void FreeSteps(AnimationSteps* steps) {
    if (steps->steps) gamecrt::operator_delete(steps->steps);
    steps->steps = nullptr;
}

// Steps compared without byte 3, which the original never initialises (00550A60).
inline int StepsDifference(const AnimationStep* a, const AnimationStep* b, int count) {
    for (int i = 0; i < count; ++i) {
        if (a[i].animation != b[i].animation || a[i].variant != b[i].variant ||
            a[i].flags != b[i].flags || a[i].duration != b[i].duration || a[i].kind != b[i].kind)
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
