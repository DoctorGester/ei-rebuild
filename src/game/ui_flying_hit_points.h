#pragma once

// The numbers that fly up from units hit (the world client's).

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include <list>

class CTexture;
class CSceneRenderer;

struct FlyingHitPoint {
    int target;                 // nonzero: the unit's id
    int amount;                 // 0 or more
    // Bits 0, 1, 3: a label row under the number (the texture's rows at v 0.39..0.54, 0.24..0.39,
    // 0.09..0.24), bit 3 also a cyan number started 0.5 s later; bit 31: drawn as a marker over the unit (no
    // number), erased after one drawing.
    std::uint32_t flags;
    std::uint32_t time;         // timeGetTime when it starts
};
static_assert(sizeof(FlyingHitPoint) == 0x10);

class CFlyingHitPoints {
public:
    // Constructor: no object, an empty list. Returns this.
    CFlyingHitPoints* Construct();
    // Destructor: the object released, the list's nodes and sentinel freed.
    void Destruct();
    // Appends a number for `target` (nonzero) and `amount` (not negative),
    // starting now, or 0.5 s from now with flag 8; nothing otherwise.
    void Add(int target, int amount, std::uint32_t flags);
    // No texture object: 0. Binds it (failure is fatal), erases the entries older than 3 s, and with
    // ShowHitNumbersOption draws each started entry (0..3 s) over its unit (shown, on screen, pickable; the
    // screen rectangle of its "hd", "bd" or "hp" part): the amount in 8-pixel digits rising 100 pixels and
    // drifting 120 to the right while it fades (green, red for a player's unit, cyan with flag 8) with the
    // flags' label rows, or a 32x10 marker 20 pixels above the middle for flag 31, kept inside the render
    // target. Then entries with flag 31 are erased. Returns 1.
    int Draw(CSceneRenderer* renderer);

    CTexture* texture_;   // the digits texture (counted)
    std::list<FlyingHitPoint> points_;
};
static_assert(offsetof(CFlyingHitPoints, points_) == 4);
