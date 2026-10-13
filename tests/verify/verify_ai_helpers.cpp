// Checks for small AI map functions of ticket A-W13: RestoreBridgeCell (ai_bridges.cpp; a unit or
// object leaving a bridge cell), CRegionMap::SaveBlocks (ai_region_reach.cpp; every save),
// AIPath::ToMotionOrder and CAIMap::ClearMarksAround (ai_path_cells.cpp; every move order / long
// path plan), and FinishQuestMap (world_glue.cpp; the scripts' QuestComplete in a network game).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/net_classes.h"
#include "../../src/game/world_glue.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

// The bridge map's entry for a cell (an STLport hash_map node: next, key, value).
struct BridgeEntry {
    BridgeEntry* next;
    std::uint32_t key;
    std::uint32_t value;
};
const BridgeEntry* FindBridgeEntry(const void* map, int x, int y) {
    auto* buckets = static_cast<BridgeEntry** const*>(map);
    BridgeEntry** begin = buckets[1];
    BridgeEntry** end = buckets[2];
    auto key = static_cast<std::uint32_t>((static_cast<int>(static_cast<short>(x)) << 16) + static_cast<short>(y));
    const BridgeEntry* entry = begin[key % static_cast<std::uint32_t>(end - begin)];
    while (entry && entry->key != key) entry = entry->next;
    return entry;
}
} // namespace

// 005B30B0: ours only (the entry is erased, so it cannot run twice). The cell and the occluder
// word get the entry's fields, everything else stays, and the entry is gone.
static void __fastcall RestoreBridgeVerify(void* map, void*, int x, int y, AIMapCell* cell, std::uint32_t* occluder) {
    const BridgeEntry* entry = FindBridgeEntry(map, x, y);
    bool had = entry != nullptr;
    std::uint32_t value = had ? entry->value : 0;
    AIMapCell cellBefore = *cell;
    std::uint32_t occluderBefore = *occluder;
    RestoreBridgeCell(map, nullptr, x, y, cell, occluder);
    AIMapCell expected = cellBefore;
    std::uint32_t expectedOccluder = occluderBefore;
    if (had) {
        expected.groundLevel = static_cast<std::uint16_t>(value);
        expected.flags = static_cast<std::uint8_t>((cellBefore.flags & 0xC0) | ((value >> 16) & 0x3F));
        expectedOccluder = (occluderBefore & 0xFFFFFFE0u) | ((value >> 24) & 0x1F);
    }
    bool ok = std::memcmp(&expected, cell, sizeof(expected)) == 0 && expectedOccluder == *occluder &&
              !FindBridgeEntry(map, x, y);
    ei::verify::Check("RestoreBridgeCell", true, ok, "(%d,%d) entry %d value %08X", x, y, had, value);
}
EI_VERIFY(0x005B30B0, RestoreBridgeVerify, 0, s_unused);

namespace {
// A stand-in stream whose slot 2 (Write) records the bytes.
std::vector<std::uint8_t>* s_bytes;
void __fastcall RecordWrite(void*, void*, const void* buffer, int size) {
    auto* p = static_cast<const std::uint8_t*>(buffer);
    s_bytes->insert(s_bytes->end(), p, p + size);
}
void* s_vtable[8];
struct StandIn {
    void** vtable;
};
} // namespace

// 005B1380: SUB ESP,8; PUSH EBX; PUSH EBP (5). The original, then ours, into the stand-in stream;
// then ours into the real one.
static void(__fastcall* s_saveBlocks)(CRegionMap*, void*, CStream*);
static void __fastcall SaveBlocksVerify(CRegionMap* self, void*, CStream* stream) {
    std::vector<std::uint8_t> expected, actual;
    s_vtable[2] = reinterpret_cast<void*>(&RecordWrite);
    StandIn standIn{s_vtable};
    s_bytes = &expected;
    s_saveBlocks(self, nullptr, reinterpret_cast<CStream*>(&standIn));
    s_bytes = &actual;
    self->SaveBlocks(reinterpret_cast<CStream*>(&standIn));
    ei::verify::Check("CRegionMap::SaveBlocks", true, actual == expected, "%d bytes (original %d)",
                      static_cast<int>(actual.size()), static_cast<int>(expected.size()));
    self->SaveBlocks(stream);
}
EI_VERIFY(0x005B1380, SaveBlocksVerify, 5, s_saveBlocks);

namespace {
// What ToMotionOrder leaves in `out`: the order's bytes and the steps.
struct OrderResult {
    unsigned char order[0x20];
    std::vector<PathCell> steps;

    explicit OrderResult(const AIPath* out) {
        std::memcpy(order, out->unknown_00, sizeof(order));
        for (const PathCellList::Node* node = out->steps.head; node; node = node->next) steps.push_back(node->data);
    }
    bool operator==(const OrderResult& o) const {
        return std::memcmp(order, o.order, sizeof(order)) == 0 && steps.size() == o.steps.size() &&
               (steps.empty() || std::memcmp(steps.data(), o.steps.data(), steps.size() * sizeof(PathCell)) == 0);
    }
};
} // namespace

// 005B22C0: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,ECX; PUSH EDI (6). The function overwrites the
// order and empties the steps first, so the original and then ours fill the same `out`.
static void(__fastcall* s_toMotionOrder)(const AIPath*, void*, AIPath*);
static void __fastcall ToMotionOrderVerify(const AIPath* self, void*, AIPath* out) {
    s_toMotionOrder(self, nullptr, out);
    OrderResult expected(out);
    self->ToMotionOrder(out);
    OrderResult actual(out);
    ei::verify::Check("AIPath::ToMotionOrder", true, expected == actual, "kind %d: %d steps (original %d)",
                      *reinterpret_cast<const int*>(actual.order), static_cast<int>(actual.steps.size()),
                      static_cast<int>(expected.steps.size()));
}
EI_VERIFY(0x005B22C0, ToMotionOrderVerify, 6, s_toMotionOrder);

// 005B9BC0: MOV EAX,[ESP+4]; PUSH EBX (5). The first 64 calls compare the whole marks grid: the
// original, the grid kept and put back as it was, ours. After that ours only.
static void(__fastcall* s_clearMarks)(CAIMap*, void*, const PathCellList*);
static void __fastcall ClearMarksVerify(CAIMap* self, void*, const PathCellList* chain) {
    static int s_compared;
    std::size_t size = self->marks.data && self->width > 0 && self->height > 0
                           ? static_cast<std::size_t>(self->width) * static_cast<std::size_t>(self->height)
                           : 0;
    if (s_compared >= 64 || size == 0 || self->marks.width != self->width || self->marks.height != self->height) {
        self->ClearMarksAround(chain);
        return;
    }
    ++s_compared;
    std::vector<std::int8_t> before(self->marks.data, self->marks.data + size);
    s_clearMarks(self, nullptr, chain);
    std::vector<std::int8_t> expected(self->marks.data, self->marks.data + size);
    std::memcpy(self->marks.data, before.data(), size);
    self->ClearMarksAround(chain);
    ei::verify::Check("CAIMap::ClearMarksAround", true, std::memcmp(self->marks.data, expected.data(), size) == 0,
                      "%d blocks", chain->count);
}
EI_VERIFY(0x005B9BC0, ClearMarksVerify, 5, s_clearMarks);

// 0057FB90: MOV EAX,FS:[0] (6). Without a live server both versions run (they only test it);
// with one, ours only (FinishMap drops and queues messages): 0 or 1.
static int(__stdcall* s_finishQuestMap)(const char*);
static int __stdcall FinishQuestMapVerify(const char* name) {
    net::ServerSession* server = net::GameServer();
    if (!server || (server->flags7_ & CRefCounted::kDetached)) {
        int expected = s_finishQuestMap(name);
        int actual = FinishQuestMap(name);
        ei::verify::Check("FinishQuestMap (no server)", expected, actual, "%s", name);
        return actual;
    }
    int result = FinishQuestMap(name);
    ei::verify::Check("FinishQuestMap", true, result == 0 || result == 1, "%s -> %d", name, result);
    return result;
}
EI_VERIFY(0x0057FB90, FinishQuestMapVerify, 6, s_finishQuestMap);
