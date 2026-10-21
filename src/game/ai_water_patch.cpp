// CAIMap::StampWaterPatch (005B7EF0, RET 8): adds to (or lifts from) the small-unit layer a
// pseudo unit made of the water depths around a point: the 15x15 cells from (x - 7, y - 7),
// clipped to the map (the last row and column excluded), copied into a shared 16x16 shape (a
// function-local static reference-counted object at 007C2F64, guard 007C2F68 bit 1, released at
// exit by 005B80C0; recreated when missing or detached). The pseudo unit's key is
// ((y + 0x800) << 16) + x; the point's cell is truncated from position * 1 / (0073FD18 * 0.5).
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "originals_ai.h"
#include "originals_library.h"
#include "game_data.h"

namespace {

CUnitShape*& Patch() { return ei::GameData<CUnitShape*>(0x007C2F64); }
std::uint8_t& Guard() { return ei::GameData<std::uint8_t>(0x007C2F68); }

float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }

// An empty unit shape (as CUnitShape::Empty leaves one), built inline. The counted header (count
// and flags) is cleared with one dword store, as the original does.
CUnitShape* NewPatch() {
    auto* shape = static_cast<CUnitShape*>(gamecrt::operator_new(sizeof(CUnitShape)));
    if (!shape) return nullptr;
    const std::uint32_t noReferences = 0;
    std::memcpy(&shape->refs_, &noReferences, sizeof(noReferences));
    static_assert(offsetof(CUnitShape, flags7_) - offsetof(CUnitShape, refs_) == 3);
    ei::SetVtable(shape, kUnitShapeVtable);
    std::memset(shape->levels, 0, sizeof(shape->levels));
    shape->radius = -1.0f;
    return shape;
}

} // namespace

void CAIMap::StampWaterPatch(const Vector3* position, bool add) {
    double scale = static_cast<double>(GameFloat(0x0073B84C)) /
                   (static_cast<double>(GameFloat(0x0073FD18)) * GameFloat(0x0073B86C));
    int x = static_cast<int>(scale * position->x);
    int y = static_cast<int>(scale * position->y);
    if (!(Guard() & 1)) {
        Guard() |= 1;
        Patch() = nullptr;
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005B80C0));
    }
    if (!Patch() || (Patch()->flags7_ & CRefObject::kDetached)) {
        CUnitShape* fresh = NewPatch();
        CUnitShape* old = Patch();
        Patch() = fresh;
        if (fresh) ++fresh->refs_;
        if (old) old->Release();
    }
    int key = ((y + 0x800) << 16) + x;
    int x0 = x - 7 < 0 ? 0 : x - 7;
    int y0 = y - 7 < 0 ? 0 : y - 7;
    int x1 = width - 1 >= x + 7 ? x + 7 : width - 1;
    int y1 = height - 1 >= y + 7 ? y + 7 : height - 1;
    CUnitShape* shape = Patch();
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            shape->levels[yy - y0][xx - x0] = static_cast<std::uint8_t>(waterDepth[yy][xx]);
    // The pseudo unit is only a key to the layer (ai_unit_layer.cpp).
    CUnitLayer* layer = &unitLayerSmall;
    auto* owner = reinterpret_cast<CUnit*>(static_cast<std::intptr_t>(key));
    if (add) layer->Add(owner, Patch(), x0, y0);
    else layer->Remove(owner, x0, y0);
}

EI_REPLACE_METHOD(0x005B7EF0, CAIMap::StampWaterPatch);
