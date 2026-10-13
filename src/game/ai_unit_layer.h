#pragma once

// The unit occupancy layers at CAIMap + 0x27C (small units) and + 0x12A4 (large units): every unit
// on the map is stamped as a 20x20-cell block of levels (its UnitShape, 16x16, at the unit's cell
// offset within a 4-cell block), stored as 4x4-cell nodes in a 1024-bucket hash keyed by block.
// Names provisional; layout static_asserted below.

#include "ai_map.h"
#include "core_refcount.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

// A unit's footprint on the layers (0x10C bytes, vtable 0073FD5C): 16 x 16 cell levels around the
// unit, shared by radius through the shape cache (007C2F70, ai_shape_cache.cpp; its handles are
// UnitShapeHandle). Methods in ai_unit_shape.cpp. Names provisional.
class __declspec(novtable) CUnitShape : public CRefObject {
public:
    // 005B3440 (RET 4; the constructor). The levels of a disc: per cell (2 + radius - half its
    // distance from the middle cell (8, 8)) x 16, rounded; cells where that is not above 0 stay 0.
    // Returns this.
    CUnitShape* Construct(float radius);
    // 005B3540 (slot 2, OnShutdown). Constructed again empty: no levels, radius -1. Returns 0.
    int Empty();
    // 005B34F0 (slot 3, Clone). A new shape with the same levels and radius (no references yet).
    CUnitShape* Copy();

    std::uint8_t levels[16][16];    // +0x008 rows of 16
    float radius;                   // +0x108
};
static_assert(offsetof(CUnitShape, levels) == 8);
static_assert(sizeof(CUnitShape) == 0x10C);
constexpr std::uintptr_t kUnitShapeVtable = std::uintptr_t(0x0073FD5C);

struct UnitShapeHandle {
    void* cache;
    CRefObject* ref;
    std::uint32_t key;
    float radius;
};
// 005B33D0 (the shape cache's slot 0, RET 4). A new shape for the handle's radius.
CUnitShape* __fastcall CreateUnitShape(void* cache, void*, const UnitShapeHandle* handle);

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
    // 005B4110 (the base constructor; ai_grids.cpp). The empty node list (blocks of 0x400), the
    // base vtable 0073FD7C, no buckets, no free nodes. `map` is the owner's to set. Returns this.
    CUnitLayer* Construct();
    // 0058A8A0 / 0058A900 (the two layer classes' destructors, reached by the AI map's unwinding
    // only; ai_grids.cpp). The node list emptied and its blocks freed.
    void Destruct();

    static constexpr std::uintptr_t kNodeListVtable = std::uintptr_t(0x0073F9F8);

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
