// Checks for the client's map helpers (src/game/world_client_maps.cpp).
//
// The grid moves change the lists: ours only, with checks on the lists afterwards. The radius
// marker and the window collectors run the original first, record what it wrote, undo it and run
// ours. The lookups are pure and compared.
#include "verify.h"

#include "../../src/game/world.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// How often `object` is in `grid`'s cell for `position`, or -1 outside the grid.
int CountInCell(const SectorGrid* grid, const CEngineObject* object, const float* position) {
    std::uint32_t cx = static_cast<std::uint32_t>(static_cast<int>(std::floor(position[0] + 0.5f))) >> 4;
    std::uint32_t cy = static_cast<std::uint32_t>(static_cast<int>(std::floor(position[1] + 0.5f))) >> 4;
    if (cx >= grid->width_ || cy >= grid->height_) return -1;
    SectorListNode* head = grid->rows_[cy][cx];
    int count = 0;
    for (SectorListNode* node = head->next; node != head; node = node->next)
        if (node->value == object) ++count;
    return count;
}

template <bool Second>
void MoveChecked(SectorGrid* grid, CEngineObject* object, const float* position) {
    float target[3];
    std::memcpy(target, position, sizeof(target));
    int refs = object->refCount_;
    float before[3];
    std::memcpy(before, &At<float>(object, 0x1C), sizeof(before));
    int oldCount = CountInCell(grid, object, before);
    if (Second)
        grid->Move2(object, position);
    else
        grid->Move(object, position);
    bool ok = std::memcmp(&At<float>(object, 0x1C), target, sizeof(target)) == 0;
    int newCount = CountInCell(grid, object, target);
    // The rounding of the test's cell is plain C++: positions within a hair of a cell edge are not
    // judged (rule 7).
    bool edge = false;
    auto cell = [&edge](float coordinate) {
        float scaled = (coordinate + 0.5f) / 16.0f;   // FISTP's cell edges are at 16k - 0.5
        float f = std::floor(scaled);
        float rest = scaled - f;
        if (rest < 0.01f || rest > 0.99f) edge = true;
        return f;
    };
    float tx = cell(target[0]), ty = cell(target[1]), bx = cell(before[0]), by = cell(before[1]);
    bool sameCell = tx == bx && ty == by;
    if (!edge) {
        if (sameCell) {
            ok = ok && refs == object->refCount_;
        } else {
            // Listed in the new cell; one reference more for it, one less per old listing.
            if (newCount >= 0) ok = ok && newCount >= 1;
            int expected = refs + (newCount >= 0 ? 1 : 0) - (oldCount > 0 ? oldCount : 0);
            ok = ok && object->refCount_ == expected;
        }
    }
    ei::verify::Check(Second ? "SectorGrid::Move2" : "SectorGrid::Move", true, ok,
                      "object %d, (%g, %g) -> (%g, %g), refs %d -> %d, cells %d -> %d", object->id_, before[0], before[1],
                      target[0], target[1], refs, object->refCount_, oldCount, newCount);
}
} // namespace

static void __fastcall MoveVerify(SectorGrid* grid, void*, CEngineObject* object, const float* position) {
    MoveChecked<false>(grid, object, position);
}
EI_VERIFY(0x00570860, MoveVerify, 0, s_unused);

static void __fastcall Move2Verify(SectorGrid* grid, void*, CEngineObject* object, const float* position) {
    MoveChecked<true>(grid, object, position);
}
EI_VERIFY(0x00570BB0, Move2Verify, 0, s_unused);

// 00571D50: PUSH EBP; MOV EBP,ESP; AND ESP,-8 (6 bytes). The marks and the functor compared.
using MarkFn = float*(__fastcall*)(float*, void*, std::uint8_t*, std::uint8_t*, float, float, float);
static MarkFn s_mark;
static float* __fastcall MarkWithinRadiusVerify(float* out, void*, std::uint8_t* first, std::uint8_t* last, float x,
                                                float y, float scale) {
    std::vector<std::uint8_t*> nodes;
    std::vector<std::uint8_t> before;
    for (std::uint8_t* node = first; node != last; node = *reinterpret_cast<std::uint8_t**>(node)) {
        nodes.push_back(node);
        before.push_back(At<std::uint8_t>(node, 0xD));
    }
    float theirsOut[3];
    s_mark(theirsOut, nullptr, first, last, x, y, scale);
    std::vector<std::uint8_t> theirs;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        theirs.push_back(At<std::uint8_t>(nodes[i], 0xD));
        At<std::uint8_t>(nodes[i], 0xD) = before[i];
    }
    float* result = MarkWithinRadius(out, nullptr, first, last, x, y, scale);
    for (std::size_t i = 0; i < nodes.size(); ++i)
        ei::verify::Check("MarkWithinRadius", theirs[i], At<std::uint8_t>(nodes[i], 0xD), "node %zu of %zu, (%g, %g) x %g",
                          i, nodes.size(), x, y, scale);
    ei::verify::Check("MarkWithinRadius out", 0, std::memcmp(theirsOut, out, sizeof(theirsOut)), "");
    return result;
}
EI_VERIFY(0x00571D50, MarkWithinRadiusVerify, 6, s_mark);

// 00571800 / 005718B0: PUSH ECX; MOV EAX,[ESP+0xC] (5 bytes). The entries pushed compared.
using CollectFn = void**(__fastcall*)(void**, void*, std::uint8_t*, std::uint8_t*, void*, long, long, CBaseManager*);
static CollectFn s_collect;
static CollectFn s_collect2;

namespace {
struct Hits {
    std::uint8_t* start;
    std::uint8_t* finish;
    std::uint8_t* eos;
};

void** CollectChecked(const char* name, CollectFn original, CollectFn ours, void** out, std::uint8_t* first,
                      std::uint8_t* last, void* hits, long x, long y, CBaseManager* manager) {
    Hits* vector = static_cast<Hits*>(hits);
    std::size_t size = vector->finish - vector->start;
    void* theirsOut = nullptr;
    original(&theirsOut, nullptr, first, last, hits, x, y, manager);
    std::vector<std::uint8_t> theirs(vector->start + size, vector->finish);
    vector->finish = vector->start + size;
    void** result = ours(out, nullptr, first, last, hits, x, y, manager);
    std::vector<std::uint8_t> mine(vector->start + size, vector->finish);
    ei::verify::Check(name, true, theirs == mine && theirsOut == *out, "(%ld, %ld): %zu vs %zu bytes", x, y,
                      theirs.size(), mine.size());
    return result;
}
} // namespace

static void** __fastcall CollectWindowsAtVerify(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits,
                                                long x, long y, CBaseManager* manager) {
    return CollectChecked("CollectWindowsAt", s_collect, CollectWindowsAt, out, first, last, hits, x, y, manager);
}
EI_VERIFY(0x00571800, CollectWindowsAtVerify, 5, s_collect);

static void** __fastcall CollectWindowsAt2Verify(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits,
                                                 long x, long y, CBaseManager* manager) {
    return CollectChecked("CollectWindowsAt2", s_collect2, CollectWindowsAt2, out, first, last, hits, x, y, manager);
}
EI_VERIFY(0x005718B0, CollectWindowsAt2Verify, 5, s_collect2);

// 0056FC60: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
using LookupFn = int(__fastcall*)(const PendingMap*, void*, const std::uint32_t*, void**);
static LookupFn s_lookup;
static int __fastcall PendingMapLookupVerify(const PendingMap* map, void*, const std::uint32_t* key, void** value) {
    void* theirs = nullptr;
    int theirsResult = s_lookup(map, nullptr, key, &theirs);
    void* mine = nullptr;
    int result = PendingMapLookup(map, nullptr, key, &mine);
    ei::verify::Check("PendingMapLookup", theirsResult, result, "key %u", *key);
    ei::verify::Check("PendingMapLookup value", theirs, mine, "key %u", *key);
    if (result) *value = mine;
    return result;
}
EI_VERIFY(0x0056FC60, PendingMapLookupVerify, 5, s_lookup);

// 0056E400 / 0056E440: PUSH ESI; MOV ESI,[ECX]; MOV ECX,[ECX+4] (6 bytes).
using RecordFn = const std::uint8_t*(__fastcall*)(const RecordVector*, void*, std::uint32_t);
static RecordFn s_record50;
static RecordFn s_recordB4;
static const std::uint8_t* __fastcall RecordAt50Verify(const RecordVector* records, void*, std::uint32_t index) {
    const std::uint8_t* result = RecordAt50(records, nullptr, index);
    ei::verify::Check("RecordAt50", s_record50(records, nullptr, index), result, "index %u", index);
    return result;
}
EI_VERIFY(0x0056E400, RecordAt50Verify, 6, s_record50);

static const std::uint8_t* __fastcall RecordAtB4Verify(const RecordVector* records, void*, std::uint32_t index) {
    const std::uint8_t* result = RecordAtB4(records, nullptr, index);
    ei::verify::Check("RecordAtB4", s_recordB4(records, nullptr, index), result, "index %u", index);
    return result;
}
EI_VERIFY(0x0056E440, RecordAtB4Verify, 6, s_recordB4);
