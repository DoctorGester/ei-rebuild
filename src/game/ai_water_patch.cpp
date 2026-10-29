// CAIMap::StampWaterPatch: adds to (or lifts from) the small-unit layer a pseudo unit made of the water depths
// around a point: the 15x15 cells from (x - 7, y - 7), clipped to the map (the last row and column excluded),
// copied into a shared 16x16 shape (a function-local static reference-counted object; recreated when missing or
// detached). The pseudo unit's key is ((y + 0x800) << 16) + x; the point's cell is
// truncated from position * 1 / (cell size * 0.5). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "core_refcount.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

void CAIMap::StampWaterPatch(const Vector3& position, bool add) {
    double scale = 1.0 /
                   (0.5f);
    int x = static_cast<int>(scale * position.x);
    int y = static_cast<int>(scale * position.y);
    static Ref<CUnitShape> patch;
    if (!patch || (patch->flags7_ & CRefObject::kDetached)) patch.Assign(new CUnitShape());
    int key = ((y + 0x800) << 16) + x;
    int x0 = x - 7 < 0 ? 0 : x - 7;
    int y0 = y - 7 < 0 ? 0 : y - 7;
    int x1 = cells.width - 1 >= x + 7 ? x + 7 : cells.width - 1;
    int y1 = cells.height - 1 >= y + 7 ? y + 7 : cells.height - 1;
    CUnitShape* shape = patch;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            shape->levels[yy - y0][xx - x0] = static_cast<std::uint8_t>(waterDepth[yy][xx]);
    // The pseudo unit is only a key to the layer.
    CUnitLayer* layer = &unitLayerSmall;
    auto* owner = reinterpret_cast<CUnit*>(static_cast<std::intptr_t>(key));
    if (add) layer->Add(owner, patch, x0, y0);
    else layer->Remove(owner, x0, y0);
}
