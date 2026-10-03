#pragma once

// The unit occupancy layers at CAIMap + 0x27C (small units) and + 0x12A4 (large units): every unit
// on the map is stamped as a 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell
// offset within a 4-cell block), stored as 4x4-cell nodes in a 1024-bucket hash keyed by block.
// Names provisional; layout static_asserted below.

#include "ai_map.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// A 4x4-cell block of one unit's levels (0x1C bytes).
struct UnitLayerNode {
    UnitLayerNode* next;        // next in the bucket, or in the free list
    int key;                    // (block y << 16) + block x, blocks of 4 cells
    std::int8_t levels[16];     // rows of 4
    CUnit* owner;               // +0x18
};
static_assert(sizeof(UnitLayerNode) == 0x1C);

class CUnitLayer {
public:
    // MFC CList<UnitLayerNode> node: the storage of every node ever allocated.
    struct ListNode {
        ListNode* next;
        ListNode* prev;
        UnitLayerNode data;
    };

    // 005B36B0. Stamps `unit`'s shape (16x16 levels at shape + 8) with its top-left cell at
    // (x, y): one node per non-empty 4x4 block, each announced through virtual slot 1.
    void Add(CUnit* unit, const std::uint8_t* shape, int x, int y);
    // 005B3890. Removes `unit`'s nodes from the 5x5 blocks from block (x >> 2, y >> 2); for each
    // block it had, virtual slot 0 gets the block with the other units' levels merged (maximum).
    void Remove(CUnit* unit, int x, int y);

    void* vtable;
    // +0x04 CList<UnitLayerNode>
    void* listVtable;
    ListNode* head;             // +0x08
    ListNode* tail;             // +0x0C
    int count;                  // +0x10
    ListNode* free;             // +0x14
    void* blocks;               // +0x18
    int blockSize;              // +0x1C
    UnitLayerNode* buckets[1024];   // +0x20 by (key x * 17 + key y) & 0x3FF
    UnitLayerNode* freeNodes;       // +0x1020 unused nodes
    CAIMap* map;                    // +0x1024
};
static_assert(offsetof(CUnitLayer, buckets) == 0x20);
static_assert(offsetof(CUnitLayer, freeNodes) == 0x1020);
static_assert(sizeof(CUnitLayer) == 0x1028);

// The layers' virtual slots 0 (Set: the block's levels replace the map's) and 1 (Merge: the
// map keeps the larger level). The small-unit layer (vtable 0073FD74) writes AIMapCell::level
// (unsigned maximum), the large-unit one (vtable 0073FD6C) the signed bytes at CAIMap + 0x1F8.
void __fastcall SmallLayerSet(CUnitLayer* self, void*, const UnitLayerNode* block);     // 005B39D0
void __fastcall SmallLayerMerge(CUnitLayer* self, void*, const UnitLayerNode* block);   // 005B3A70
void __fastcall LargeLayerSet(CUnitLayer* self, void*, const UnitLayerNode* block);     // 005B3B20
void __fastcall LargeLayerMerge(CUnitLayer* self, void*, const UnitLayerNode* block);   // 005B3B90

// The bucket of a block key.
inline int UnitLayerBucket(int blockX, int blockY) { return (blockX * 17 + blockY) & 0x3FF; }
