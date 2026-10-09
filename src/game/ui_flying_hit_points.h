#pragma once

// The numbers that fly up from units hit (the world client's; constructed by 00563710, destroyed by
// 00563290, drawn by 005E18E0). Names provisional.

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CSceneRenderer;

struct FlyingHitPoint {
    int target;                 // +0x00 nonzero: the unit's id
    int amount;                 // +0x04 0 or more
    // +0x08 bits 0, 1, 3: a label row under the number (the texture's rows at v 0.39..0.54,
    // 0.24..0.39, 0.09..0.24), bit 3 also a cyan number started 0.5 s later; bit 31: drawn as a
    // marker over the unit (no number), erased after one drawing.
    std::uint32_t flags;
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
    // 005E18E0 (RET 4). No texture object: 0. Binds it (renderer slot 11; failure is fatal), erases
    // the entries older than 3 s, and with option 17 (ShowHitNumbersOption) draws each started
    // entry (0..3 s) over its unit (shown, on screen, pickable; the screen rectangle of its "hd",
    // "bd" or "hp" part): the amount in 8-pixel digits rising 100 pixels and drifting 120 to the
    // right while it fades (green, red for a player's unit, cyan with flag 8) with the flags'
    // label rows, or a 32x10 marker 20 pixels above the middle for flag 31; each through renderer
    // slot 12, kept inside the render target. Then entries with flag 31 are erased. Returns 1.
    int Draw(CSceneRenderer* renderer);

    CEngineObject* object_;                 // +0x00
    stlp::list<FlyingHitPoint> points_;     // +0x04
};
static_assert(offsetof(CFlyingHitPoints, points_) == 4);
