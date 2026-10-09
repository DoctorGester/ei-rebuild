#pragma once

// The numbers that fly up from units hit (the world client's; constructed by 00563710, destroyed by
// 00563290, drawn by 005E18E0 (not rewritten: float-heavy vertex building)). Names provisional.

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;

struct FlyingHitPoint {
    int target;                 // +0x00 nonzero
    int amount;                 // +0x04 0 or more
    std::uint32_t flags;        // +0x08 bit 3: starts 0.5 s later
    std::uint32_t time;         // +0x0C timeGetTime when it starts
};
static_assert(sizeof(FlyingHitPoint) == 0x10);

class CFlyingHitPoints {
public:
    // 005E1710. Constructor: no object, an empty list. Returns this.
    CFlyingHitPoints* Construct();
    // 005E17E0. Destructor: the object released (its slot 4), the list's nodes and sentinel freed.
    void Destruct();
    // 005E1870 (RET 0xC). Appends a number for `target` (nonzero) and `amount` (not negative),
    // starting now, or 0.5 s from now with flag 8; nothing otherwise.
    void Add(int target, int amount, std::uint32_t flags);

    CEngineObject* object_;                 // +0x00
    stlp::list<FlyingHitPoint> points_;     // +0x04
};
static_assert(offsetof(CFlyingHitPoints, points_) == 4);
