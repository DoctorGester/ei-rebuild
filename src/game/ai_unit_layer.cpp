// The unit occupancy layers of the AI map (005B36B0, 005B3890): stamped and lifted every time a
// unit moves (CAIMap::MarkFootprint through CSectorMap::Move).
#include "ai_unit_layer.h"

#include "ei/hook.h"

#include <cstring>

namespace {
using NodeList = mfc::CList<UnitLayerNode>;
// 005BE7B0: CList<UnitLayerNode>::NewNode(prev, next) on the layer's node list (data zero-filled,
// count + 1).
__declspec(naked) NodeList::Node* __fastcall NewListNode(NodeList*, void*, NodeList::Node*, NodeList::Node*)
    EI_ORIGINAL(0x005BE7B0)

// A node from the free list, or a new one appended to the layer's list. The original copies an
// uninitialised stack element into a new node's data (MFC AddTail of a default element); the
// fields are written before the node is used, so ours leaves NewNode's zeros.
UnitLayerNode* TakeNode(CUnitLayer* layer) {
    if (UnitLayerNode* node = layer->freeNodes) {
        layer->freeNodes = node->next;
        return node;
    }
    NodeList& nodes = layer->nodes;
    NodeList::Node* added = NewListNode(&nodes, nullptr, nodes.tail, nullptr);
    if (nodes.tail) nodes.tail->next = added;
    else nodes.head = added;
    nodes.tail = added;
    return &added->data;
}

bool AnyLevel(const UnitLayerNode* node) {
    std::uint32_t rows[4];
    std::memcpy(rows, node->levels, sizeof(rows));
    return (rows[3] | rows[2] | rows[1] | rows[0]) != 0;
}
} // namespace

void CUnitLayer::Add(CUnit* unit, const std::uint8_t* shape, int x, int y) {
    // The shape placed in a 20x20 grid at the unit's offset within its 4-cell block.
    std::uint8_t grid[20 * 20] = {};
    std::uint8_t* row = grid + (x & 3) + (y & 3) * 20;
    for (int r = 0; r < 16; ++r, row += 20) std::memcpy(row, shape + 8 + r * 16, 16);

    UnitLayerNode* node = TakeNode(this);
    for (int by = 0; by < 5; ++by) {
        for (int bx = 0; bx < 5; ++bx) {
            for (int r = 0; r < 4; ++r) std::memcpy(&node->levels[r * 4], grid + (by * 4 + r) * 20 + bx * 4, 4);
            if (!AnyLevel(node)) continue;
            int keyY = (y >> 2) + by;
            int keyX = (x >> 2) + bx;
            node->key = (keyY << 16) + keyX;
            node->owner = unit;
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
EI_REPLACE_METHOD(0x005B36B0, CUnitLayer::Add);

void CUnitLayer::Remove(CUnit* unit, int x, int y) {
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
                    if (node->owner == unit) {
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
EI_REPLACE_METHOD(0x005B3890, CUnitLayer::Remove);

namespace {
// The block's 4x4 cells when the block lies in the map (keys are signed 16-bit halves; the
// comparisons are unsigned, so negative blocks are outside).
bool BlockInMap(const CAIMap* map, const UnitLayerNode* block, int* x, int* y) {
    std::int16_t halves[2];
    std::memcpy(halves, &block->key, sizeof(halves));
    *x = halves[0] * 4;
    *y = halves[1] * 4;
    return static_cast<unsigned>(*y) < static_cast<unsigned>(map->height) &&
           static_cast<unsigned>(*x) < static_cast<unsigned>(map->width);
}
} // namespace

void __fastcall SmallLayerSet(CUnitLayer* self, void*, const UnitLayerNode* block) {
    int x, y;
    if (!BlockInMap(self->map, block, &x, &y)) return;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            self->map->cells[y + r][x + c].level = static_cast<std::uint8_t>(block->levels[r * 4 + c]);
        }
    }
}
EI_REPLACE(0x005B39D0, SmallLayerSet);

void __fastcall SmallLayerMerge(CUnitLayer* self, void*, const UnitLayerNode* block) {
    int x, y;
    if (!BlockInMap(self->map, block, &x, &y)) return;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            std::uint8_t& level = self->map->cells[y + r][x + c].level;
            std::uint8_t value = static_cast<std::uint8_t>(block->levels[r * 4 + c]);
            if (level > value) value = level;
            level = value;
        }
    }
}
EI_REPLACE(0x005B3A70, SmallLayerMerge);

void __fastcall LargeLayerSet(CUnitLayer* self, void*, const UnitLayerNode* block) {
    int x, y;
    if (!BlockInMap(self->map, block, &x, &y)) return;
    for (int r = 0; r < 4; ++r) std::memcpy(self->map->waterDepth[y + r] + x, &block->levels[r * 4], 4);
}
EI_REPLACE(0x005B3B20, LargeLayerSet);

void __fastcall LargeLayerMerge(CUnitLayer* self, void*, const UnitLayerNode* block) {
    int x, y;
    if (!BlockInMap(self->map, block, &x, &y)) return;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            std::int8_t& level = self->map->waterDepth[y + r][x + c];
            std::int8_t value = block->levels[r * 4 + c];
            if (level > value) value = level;
            level = value;
        }
    }
}
EI_REPLACE(0x005B3B90, LargeLayerMerge);
