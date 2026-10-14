// Checks for the unit occupancy layers (src/game/ai_unit_layer.cpp).
#include "verify.h"

#include "../../src/game/ai_unit_layer.h"

#include <cstring>

namespace {
void* s_unused;

// The 16 map bytes a layer callback may write for `block` (cell levels or the bytes at +0x1F8).
struct BlockBytes {
    std::uint8_t bytes[16];
    bool valid;
};

bool InMap(const CUnitLayer* layer, const UnitLayerNode* block, int* x, int* y) {
    std::int16_t halves[2];
    std::memcpy(halves, &block->key, sizeof(halves));
    *x = halves[0] * 4;
    *y = halves[1] * 4;
    return static_cast<unsigned>(*y) < static_cast<unsigned>(layer->map->height) &&
           static_cast<unsigned>(*x) < static_cast<unsigned>(layer->map->width);
}

BlockBytes Read(const CUnitLayer* layer, const UnitLayerNode* block, bool large) {
    BlockBytes out = {};
    int x, y;
    out.valid = InMap(layer, block, &x, &y);
    if (!out.valid) return out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.bytes[r * 4 + c] = large ? static_cast<std::uint8_t>(layer->map->waterDepth[y + r][x + c])
                                         : layer->map->cells[y + r][x + c].level;
        }
    }
    return out;
}

void Write(CUnitLayer* layer, const UnitLayerNode* block, bool large, const BlockBytes& in) {
    int x, y;
    if (!in.valid || !InMap(layer, block, &x, &y)) return;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (large) layer->map->waterDepth[y + r][x + c] = static_cast<std::int8_t>(in.bytes[r * 4 + c]);
            else layer->map->cells[y + r][x + c].level = in.bytes[r * 4 + c];
        }
    }
}

using Callback = void(__fastcall*)(CUnitLayer*, void*, const UnitLayerNode*);

void __fastcall SmallSetOurs(CUnitLayer* self, void*, const UnitLayerNode* block) { self->SetSmall(block); }
void __fastcall SmallMergeOurs(CUnitLayer* self, void*, const UnitLayerNode* block) { self->MergeSmall(block); }
void __fastcall LargeSetOurs(CUnitLayer* self, void*, const UnitLayerNode* block) { self->SetLarge(block); }
void __fastcall LargeMergeOurs(CUnitLayer* self, void*, const UnitLayerNode* block) { self->MergeLarge(block); }

// Run the original, record the block's bytes, put the old ones back, run ours, compare.
void CheckCallback(const char* name, Callback original, Callback ours, bool large, CUnitLayer* self,
                   const UnitLayerNode* block) {
    BlockBytes before = Read(self, block, large);
    original(self, nullptr, block);
    BlockBytes expected = Read(self, block, large);
    Write(self, block, large, before);
    ours(self, nullptr, block);
    BlockBytes actual = Read(self, block, large);
    ei::verify::Check(name, true, std::memcmp(&expected, &actual, sizeof(expected)) == 0, "key %08X",
                      static_cast<unsigned>(block->key));
}
} // namespace

// 005B39D0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (5 bytes).
static Callback s_smallSet;
static void __fastcall SmallSetVerify(CUnitLayer* self, void*, const UnitLayerNode* block) {
    CheckCallback("CUnitLayer::SmallSet", s_smallSet, SmallSetOurs, false, self, block);
}
EI_VERIFY(0x005B39D0, SmallSetVerify, 5, s_smallSet);

// 005B3A70: SUB ESP,8; MOV EDX,ECX (5 bytes).
static Callback s_smallMerge;
static void __fastcall SmallMergeVerify(CUnitLayer* self, void*, const UnitLayerNode* block) {
    CheckCallback("CUnitLayer::SmallMerge", s_smallMerge, SmallMergeOurs, false, self, block);
}
EI_VERIFY(0x005B3A70, SmallMergeVerify, 5, s_smallMerge);

// 005B3B20: PUSH EBX; MOV EDX,[ECX+0x1024] (7 bytes).
static Callback s_largeSet;
static void __fastcall LargeSetVerify(CUnitLayer* self, void*, const UnitLayerNode* block) {
    CheckCallback("CUnitLayer::LargeSet", s_largeSet, LargeSetOurs, true, self, block);
}
EI_VERIFY(0x005B3B20, LargeSetVerify, 7, s_largeSet);

// 005B3B90: PUSH ECX; PUSH EBP; MOV EBP,[ESP+0xC] (6 bytes).
static Callback s_largeMerge;
static void __fastcall LargeMergeVerify(CUnitLayer* self, void*, const UnitLayerNode* block) {
    CheckCallback("CUnitLayer::LargeMerge", s_largeMerge, LargeMergeOurs, true, self, block);
}
EI_VERIFY(0x005B3B90, LargeMergeVerify, 6, s_largeMerge);

namespace {
// Nodes of `unit` with block key (bx, by) in its bucket.
int CountNodes(const CUnitLayer* layer, const CUnit* unit, int bx, int by) {
    int key = (by << 16) + bx, n = 0;
    for (const UnitLayerNode* node = layer->buckets[UnitLayerBucket(bx, by)]; node && n < 100000; node = node->next) {
        if (node->key == key && node->owner == unit) ++n;
    }
    return n;
}
} // namespace

// Add and Remove change the hash, the node list and (through the callbacks) the map: ours only.
// Add: every non-empty block of the placed shape has a node of the unit with exactly its levels.
static void __fastcall AddVerify(CUnitLayer* self, void*, CUnit* unit, const std::uint8_t* shape, int x, int y) {
    self->Add(unit, shape, x, y);
    std::uint8_t grid[20 * 20] = {};
    for (int r = 0; r < 16; ++r) std::memcpy(grid + (x & 3) + ((y & 3) + r) * 20, shape + 8 + r * 16, 16);
    bool ok = self->freeNodes != nullptr;
    for (int by = 0; by < 5 && ok; ++by) {
        for (int bx = 0; bx < 5 && ok; ++bx) {
            std::int8_t levels[16];
            for (int r = 0; r < 4; ++r) std::memcpy(&levels[r * 4], grid + (by * 4 + r) * 20 + bx * 4, 4);
            bool any = false;
            for (std::int8_t v : levels) any = any || v != 0;
            if (!any) continue;
            int kx = (x >> 2) + bx, ky = (y >> 2) + by, key = (ky << 16) + kx;
            const UnitLayerNode* node = self->buckets[UnitLayerBucket(kx, ky)];
            ok = node && node->key == key && node->owner == unit && std::memcmp(node->levels, levels, 16) == 0;
        }
    }
    ei::verify::Check("CUnitLayer::Add", true, ok, "(%d,%d)", x, y);
}
EI_VERIFY(0x005B36B0, AddVerify, 0, s_unused);

// Remove: none of the unit's nodes is left in the 5x5 blocks.
static void __fastcall RemoveVerify(CUnitLayer* self, void*, CUnit* unit, int x, int y) {
    self->Remove(unit, x, y);
    bool ok = true;
    for (int by = y >> 2; by < (y >> 2) + 5; ++by) {
        for (int bx = x >> 2; bx < (x >> 2) + 5; ++bx) ok = ok && CountNodes(self, unit, bx, by) == 0;
    }
    ei::verify::Check("CUnitLayer::Remove", true, ok, "(%d,%d)", x, y);
}
EI_VERIFY(0x005B3890, RemoveVerify, 0, s_unused);
