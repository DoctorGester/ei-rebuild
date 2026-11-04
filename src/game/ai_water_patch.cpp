// AIMap::StampWaterPatch: adds to (or lifts from) the small-unit layer a patch made of the water depths
// around a point: the 15x15 cells from (x - 7, y - 7), clipped to the map (the last row and column excluded),
// copied into a shared 16x16 shape (a function-local static reference-counted object; recreated when missing or
// detached). The patch owns its nodes by the key ((y + 0x800) << 16) + x; the point's cell is
// truncated from position * 1 / (cell size * 0.5). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "core_refcount.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

void AIMap::StampWaterPatch(const Vector3& position, bool add) {
    float scale = 1.0f / 0.5f;
    int x = static_cast<int>(scale * position.x);
    int y = static_cast<int>(scale * position.y);
    static Ref<UnitShape> patch;
    if (!patch || (patch->flags7 & RefObject::kDetached)) patch.Assign(new UnitShape());
    int key = ((y + 0x800) << 16) + x;
    int x0 = x - 7 < 0 ? 0 : x - 7;
    int y0 = y - 7 < 0 ? 0 : y - 7;
    int x1 = cells.width - 1 >= x + 7 ? x + 7 : cells.width - 1;
    int y1 = cells.height - 1 >= y + 7 ? y + 7 : cells.height - 1;
    UnitShape* shape = patch;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            shape->levels[yy - y0][xx - x0] = static_cast<std::uint8_t>(waterDepth[yy][xx]);
    if (add) unitLayerSmall.Add(key, patch, {x0, y0});
    else unitLayerSmall.Remove(key, {x0, y0});
}
