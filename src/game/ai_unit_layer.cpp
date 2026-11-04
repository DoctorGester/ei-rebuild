// The unit occupancy layers of the AI map: stamped and lifted every time a
// unit moves (AIMap::MarkFootprint through SectorMap::Move).
#include "ai_unit_layer.h"


#include <algorithm>
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
    int x, y, columns, rows;
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

void UnitLayer::Add(UnitLayerOwner owner, const UnitShape* shape, int x, int y) {
    // The shape placed in a 20x20 grid at the unit's offset within its 4-cell block.
    std::uint8_t grid[20 * 20] = {};
    std::uint8_t* row = grid + (x & 3) + (y & 3) * 20;
    for (int r = 0; r < 16; ++r, row += 20) std::memcpy(row, shape->levels[r], 16);

    UnitLayerNode* node = TakeNode(this);
    for (int by = 0; by < 5; ++by) {
        for (int bx = 0; bx < 5; ++bx) {
            for (int r = 0; r < 4; ++r) std::memcpy(&node->levels[r * 4], grid + (by * 4 + r) * 20 + bx * 4, 4);
            if (!AnyLevel(node)) continue;
            int keyY = (y >> 2) + by;
            int keyX = (x >> 2) + bx;
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

void UnitLayer::Remove(UnitLayerOwner owner, int x, int y) {
    int firstY = y >> 2, firstX = x >> 2;
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
