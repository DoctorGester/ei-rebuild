// The unit occupancy layers of the AI map: stamped and lifted every time a
// unit moves (AIMap::MarkFootprint through SectorMap::Move), and the water patch stamped the same way.

#include "game/ai/ai_map.h"
#include "game/ai/unit_shape.h"
#include "engine/core/core_refcount.h"

#include "lib/crt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {

// A node from the free list, or a new one appended to the layer's list. The original copies an
// uninitialised stack element into a new node's data (a default element appended to the list); the
// fields are written before the node is used, so ours leaves NewNode's zeros.
UnitLayerNode* TakeNode(UnitLayer* layer) {
    if (UnitLayerNode* node = layer->freeNodes) {
        layer->freeNodes = node->next;
        return node;
    }
    std::list<UnitLayerNode>& nodes = layer->nodes;
    auto added = nodes.emplace(nodes.end());
    return &*added;
}

bool AnyLevel(const UnitLayerNode* node) {
    std::uint32_t rows[4];
    std::memcpy(rows, node->levels, sizeof(rows));
    return (rows[3] | rows[2] | rows[1] | rows[0]) != 0;
}

// The part of a block's 4x4 cells that lies in the map: its first cell and the columns and rows inside.
struct BlockArea {
    int x = 0, y = 0, columns = 0, rows = 0;
};

// Whether the block's first cell lies in the map (keys are signed 16-bit halves; the comparisons are unsigned, so
// negative blocks are outside); *area clips the block at the map's far edges.
bool BlockInMap(const AIMap* map, const UnitLayerNode* block, BlockArea* area) {
    std::int16_t halves[2];
    std::memcpy(halves, &block->key, sizeof(halves));
    area->x = halves[0] * 4;
    area->y = halves[1] * 4;
    if (static_cast<unsigned>(area->y) >= static_cast<unsigned>(map->cells.height) ||
        static_cast<unsigned>(area->x) >= static_cast<unsigned>(map->cells.width))
        return false;
    area->columns = std::min(4, map->cells.width - area->x);
    area->rows = std::min(4, map->cells.height - area->y);
    return true;
}

} // namespace

void UnitLayer::Clear() {
    nodes.clear();
    std::ranges::fill(buckets, nullptr);
    freeNodes = nullptr;
}

void UnitLayer::Add(UnitLayerOwner owner, const UnitShape* shape, Point topLeft) {
    // The shape placed in a 20x20 grid at the unit's offset within its 4-cell block.
    std::uint8_t grid[20 * 20] = {};
    std::uint8_t* row = grid + (topLeft.x & 3) + (topLeft.y & 3) * 20;
    for (int r = 0; r < 16; ++r, row += 20) std::memcpy(row, shape->levels[r], 16);

    UnitLayerNode* node = TakeNode(this);
    for (int by = 0; by < 5; ++by) {
        for (int bx = 0; bx < 5; ++bx) {
            for (int r = 0; r < 4; ++r) std::copy_n(grid + (by * 4 + r) * 20 + bx * 4, 4, &node->levels[r * 4]);
            if (!AnyLevel(node)) continue;
            int keyY = (topLeft.y >> 2) + by;
            int keyX = (topLeft.x >> 2) + bx;
            node->key = (keyY << 16) + keyX;
            node->owner = owner;
            UnitLayerNode*& bucket = buckets[UnitLayerBucket(keyX, keyY)];
            node->next = bucket;
            bucket = node;
            Merge(node);
            node = TakeNode(this);
        }
    }
    node->next = freeNodes;
    freeNodes = node;
}

void UnitLayer::Remove(UnitLayerOwner owner, Point topLeft) {
    int firstY = topLeft.y >> 2, firstX = topLeft.x >> 2;
    for (int by = firstY; by < firstY + 5; ++by) {
        for (int bx = firstX; bx < firstX + 5; ++bx) {
            // What is left of the block: the other units' levels, merged. (The original leaves the
            // record's first dword uninitialised.)
            UnitLayerNode record = {};
            record.key = (by << 16) + bx;
            bool found = false;
            UnitLayerNode** slot = &buckets[UnitLayerBucket(bx, by)];
            for (UnitLayerNode* node = *slot; node;) {
                if (node->key == record.key) {
                    if (node->owner == owner) {
                        found = true;
                        *slot = node->next;
                        node->next = freeNodes;
                        freeNodes = node;
                        node = *slot;
                        continue;
                    }
                    for (int i = 0; i < 16; ++i) {
                        if (record.levels[i] < node->levels[i]) record.levels[i] = node->levels[i];
                    }
                }
                slot = &node->next;
                node = node->next;
            }
            if (found) Set(&record);
        }
    }
}

void SmallUnitLayer::Set(const UnitLayerNode* block) {
    BlockArea area;
    if (!BlockInMap(map, block, &area)) return;
    for (int r = 0; r < area.rows; ++r) {
        for (int c = 0; c < area.columns; ++c) {
            map->cells[area.y + r][area.x + c].level = static_cast<std::uint8_t>(block->levels[r * 4 + c]);
        }
    }
}

void SmallUnitLayer::Merge(const UnitLayerNode* block) {
    BlockArea area;
    if (!BlockInMap(map, block, &area)) return;
    for (int r = 0; r < area.rows; ++r) {
        for (int c = 0; c < area.columns; ++c) {
            std::uint8_t& level = map->cells[area.y + r][area.x + c].level;
            std::uint8_t value = static_cast<std::uint8_t>(block->levels[r * 4 + c]);
            if (level > value) value = level;
            level = value;
        }
    }
}

void LargeUnitLayer::Set(const UnitLayerNode* block) {
    BlockArea area;
    if (!BlockInMap(map, block, &area)) return;
    for (int r = 0; r < area.rows; ++r)
        std::memcpy(map->waterDepth[area.y + r] + area.x, &block->levels[r * 4], area.columns);
}

void LargeUnitLayer::Merge(const UnitLayerNode* block) {
    BlockArea area;
    if (!BlockInMap(map, block, &area)) return;
    for (int r = 0; r < area.rows; ++r) {
        for (int c = 0; c < area.columns; ++c) {
            std::int8_t& level = map->waterDepth[area.y + r][area.x + c];
            std::int8_t value = block->levels[r * 4 + c];
            if (level > value) value = level;
            level = value;
        }
    }
}

// --- The water patch ---------------------------------------------------------------------------

// AIMap::StampWaterPatch: adds to (or lifts from) the small-unit layer a patch made of the water depths
// around a point: the 15x15 cells from (x - 7, y - 7), clipped to the map (the last row and column excluded),
// copied into a shared 16x16 shape (a function-local static reference-counted object; recreated when missing or
// detached). The patch owns its nodes by the key ((y + 0x800) << 16) + x; the point's cell is
// truncated from position * 1 / (cell size * 0.5). See ai_map.h and docs/notes/ai.md.
void AIMap::StampWaterPatch(const Vector3& position, bool add) {
    float scale = 1.0f / 0.5f;
    int x = static_cast<int>(scale * position.x);
    int y = static_cast<int>(scale * position.y);
    static Ref<UnitShape> patch;
    if (!IsLive(patch)) patch.Assign(new UnitShape());
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
