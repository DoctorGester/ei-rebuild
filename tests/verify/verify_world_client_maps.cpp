// Checks for the client's map helpers (src/game/world_client_maps.cpp).
//
// The grid moves: a sample is run as ours on a scratch copy of the cells it touches and the
// original on the real grid, and the cells, references and position compared. The radius
// marker and the window collectors run the original first, record what it wrote, undo it and run
// ours. The lookups are pure and compared.
#include "verify.h"

#include "../../src/game/world.h"
#include "ei/x87.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// The grid's cell index of a coordinate, as the original computes it (FISTP, unsigned >> 4).
std::uint32_t CellIndex(float coordinate) { return static_cast<std::uint32_t>(x87::fistp(coordinate)) >> 4; }

// How often `object` is in `grid`'s cell for `position`, or -1 outside the grid.
int CountInCell(const SectorGrid* grid, const CEngineObject* object, const float* position) {
    std::uint32_t cx = CellIndex(position[0]);
    std::uint32_t cy = CellIndex(position[1]);
    if (cx >= grid->width_ || cy >= grid->height_) return -1;
    SectorListNode* head = grid->rows_[cy][cx].head;
    int count = 0;
    for (SectorListNode* node = head->next; node != head; node = node->next)
        if (node->data == object) ++count;
    return count;
}

// ---- Grid moves: ours on a scratch copy of the touched cells, the original on the real grid ------

// A scratch grid with the real one's size whose only lists are copies of the cells a move touches
// (nodes from the game's node pool, a reference taken per copied value, as the list's own copy
// would). The rows not touched stay null: the move reads no other cell.
struct ScratchGrid {
    SectorGrid grid{};
    std::vector<SectorList*> rows;
    std::vector<std::vector<SectorList>> rowCells;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> cells;

    explicit ScratchGrid(const SectorGrid& real) {
        grid = real;
        rows.assign(real.height_, nullptr);
        rowCells.reserve(2);
        grid.rows_ = rows.data();
    }
    void CopyCell(const SectorGrid& real, std::uint32_t x, std::uint32_t y) {
        if (x >= real.width_ || y >= real.height_) return;
        for (auto& c : cells)
            if (c.first == x && c.second == y) return;
        if (!rows[y]) {
            rowCells.emplace_back(real.width_, SectorList{nullptr});
            rows[y] = rowCells.back().data();
        }
        auto* head = static_cast<SectorListNode*>(stlp::allocate(sizeof(SectorListNode)));
        head->next = head->prev = head;
        head->data = nullptr;
        SectorListNode* source = real.rows_[y][x].head;
        for (SectorListNode* node = source->next; node != source; node = node->next) {
            auto* copy = static_cast<SectorListNode*>(stlp::allocate(sizeof(SectorListNode)));
            copy->data = node->data;
            if (copy->data) ++copy->data->refCount_;
            copy->prev = head->prev;
            copy->next = head;
            head->prev->next = copy;
            head->prev = copy;
        }
        rows[y][x].head = head;
        cells.emplace_back(x, y);
    }
    ~ScratchGrid() {
        for (auto& c : cells) {
            SectorListNode* head = rows[c.second][c.first].head;
            for (SectorListNode* node = head->next; node != head;) {
                SectorListNode* next = node->next;
                if (node->data) node->data->Release();
                stlp::deallocate(node, sizeof(SectorListNode));
                node = next;
            }
            stlp::deallocate(head, sizeof(SectorListNode));
        }
    }
};

std::vector<CEngineObject*> CellValues(const SectorGrid& grid, std::uint32_t x, std::uint32_t y) {
    std::vector<CEngineObject*> values;
    if (x >= grid.width_ || y >= grid.height_) return values;
    SectorListNode* head = grid.rows_[y][x].head;
    for (SectorListNode* node = head->next; node != head; node = node->next) values.push_back(node->data);
    return values;
}

using MoveFn = void(__fastcall*)(SectorGrid*, void*, CEngineObject*, const float*);

void MoveChecked(const char* name, MoveFn original, bool second, SectorGrid* grid, CEngineObject* object,
                 const float* position) {
    float target[3];
    std::memcpy(target, position, sizeof(target));
    float before[3];
    std::memcpy(before, &At<float>(object, 0x1C), sizeof(before));
    std::uint32_t oldX = CellIndex(before[0]), oldY = CellIndex(before[1]);
    std::uint32_t newX = CellIndex(target[0]), newY = CellIndex(target[1]);

    // Ours on the scratch copy.
    std::vector<CEngineObject*> mineOld, mineNew;
    int mineRefs;
    float mineAt[3];
    {
        ScratchGrid scratch(*grid);
        scratch.CopyCell(*grid, oldX, oldY);
        scratch.CopyCell(*grid, newX, newY);
        int refs = object->refCount_;
        if (second)
            scratch.grid.Move2(object, target);
        else
            scratch.grid.Move(object, target);
        mineRefs = object->refCount_ - refs;
        std::memcpy(mineAt, &At<float>(object, 0x1C), sizeof(mineAt));
        mineOld = CellValues(scratch.grid, oldX, oldY);
        mineNew = CellValues(scratch.grid, newX, newY);
    }
    std::memcpy(&At<float>(object, 0x1C), before, sizeof(before));

    // The original on the real grid.
    int refs = object->refCount_;
    original(grid, nullptr, object, target);
    int theirsRefs = object->refCount_ - refs;
    bool ok = std::memcmp(&At<float>(object, 0x1C), mineAt, sizeof(mineAt)) == 0 && theirsRefs == mineRefs &&
              CellValues(*grid, oldX, oldY) == mineOld && CellValues(*grid, newX, newY) == mineNew;
    ei::verify::Check(name, true, ok, "object %d, (%g, %g) -> (%g, %g), cells (%u, %u) -> (%u, %u), refs %+d vs %+d",
                      object->id_, before[0], before[1], target[0], target[1], oldX, oldY, newX, newY, theirsRefs,
                      mineRefs);
}

// The first 256 moves and then every 32nd are compared: cells can hold hundreds of particles, and
// the copies and walks would slow the game down (the rest run ours alone).
bool Sampled(unsigned& counter) {
    unsigned n = counter++;
    return n < 256 || (n & 31) == 0;
}
} // namespace

// 00570860 / 00570BB0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
static MoveFn s_move;
static MoveFn s_move2;
static unsigned s_moveCalls;
static unsigned s_move2Calls;

static void __fastcall MoveVerify(SectorGrid* grid, void*, CEngineObject* object, const float* position) {
    if (Sampled(s_moveCalls))
        MoveChecked("SectorGrid::Move", s_move, false, grid, object, position);
    else
        grid->Move(object, position);
}
EI_VERIFY(0x00570860, MoveVerify, 5, s_move);

static void __fastcall Move2Verify(SectorGrid* grid, void*, CEngineObject* object, const float* position) {
    if (Sampled(s_move2Calls))
        MoveChecked("SectorGrid::Move2", s_move2, true, grid, object, position);
    else
        grid->Move2(object, position);
}
EI_VERIFY(0x00570BB0, Move2Verify, 5, s_move2);

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

// ---- The client's object maps (ours only, except the picking) ------------------------------------

namespace {
SectorListNode* ListHead(void* client, int offset) { return At<SectorListNode*>(client, offset); }

int CountIdInList(void* client, int offset, int id) {
    SectorListNode* head = ListHead(client, offset);
    int count = 0;
    for (SectorListNode* node = head->next; node != head; node = node->next)
        if (node->data->id_ == id) ++count;
    return count;
}

bool ListEmpty(void* client, int offset) {
    SectorListNode* head = ListHead(client, offset);
    return head->next == head && head->prev == head;
}

bool GridEmpty(const SectorGrid& grid) {
    for (std::uint32_t y = 0; y < grid.height_; ++y)
        for (std::uint32_t x = 0; x < grid.width_; ++x) {
            SectorListNode* head = grid.rows_[y][x].head;
            if (head->next != head) return false;
        }
    return true;
}
} // namespace

// 005647E0: ours only. Returns 1 and the object is in the list +0xB8 (and in its grid cell).
static int __fastcall AddObjectVerify(CWorldClient* client, void*, CEngineObject* object) {
    int id = object->id_;
    int listed = CountIdInList(client, 0xB8, id);
    int result = client->AddObject(object);
    bool ok = result == 1 && CountIdInList(client, 0xB8, id) >= 1;
    // An object already listed was only referenced and released: it may be gone.
    int cell = listed ? -2 : CountInCell(&At<SectorGrid>(client, 0x120), object, &At<float>(object, 0x1C));
    if (!listed) ok = ok && cell != 0;
    ei::verify::Check("CWorldClient::AddObject", true, ok, "object %d, listed before %d, cell %d", id, listed, cell);
    return result;
}
EI_VERIFY(0x005647E0, AddObjectVerify, 0, s_unused);

// 00564BE0: ours only. Afterwards no object with the id is in +0xB8 and the id map lacks it.
static void __fastcall RemoveObjectVerify(CWorldClient* client, void*, int id) {
    int listed = CountIdInList(client, 0xB8, id);
    client->RemoveObject(id);
    const PendingMap& ids = At<PendingMap>(client, 0xD8);
    int after = CountIdInList(client, 0xB8, id);
    bool ok = after == (listed > 0 ? listed - 1 : 0) && !ids.Find(static_cast<std::uint32_t>(id));
    ei::verify::Check("CWorldClient::RemoveObject", true, ok, "id %d, listed %d -> %d", id, listed, after);
}
EI_VERIFY(0x00564BE0, RemoveObjectVerify, 0, s_unused);

// 005651A0 RefreshPlatforms: ours only (it edits the platform list; reached by moving levers, the
// rewritten 0052B490 calls it through its address: `levers`). Checked: the list's links and count
// agree, and an object of another class than 0x54 / 0x5D has no faces left when it is filed (an
// unfiled one leaves the count as it was).
static void __fastcall RefreshPlatformsVerify(CWorldClient* client, void*, CEngineObject* object) {
    const int id = object->id_;
    const int listed = CountIdInList(client, 0xB8, id);
    const int before = client->platforms_.list.count;
    client->RefreshPlatforms(object);
    int nodes = 0, faces = 0;
    for (PlatformNode* node = client->platforms_.list.head; node; node = node->next) {
        ++nodes;
        if (node->data.id == id) ++faces;
    }
    const int kind = object->GetClassId();
    bool ok = nodes == client->platforms_.list.count;
    if (!listed) ok = ok && nodes == before;
    else if (kind != 0x54 && kind != 0x5D) ok = ok && faces == 0;
    ei::verify::Check("CWorldClient::RefreshPlatforms", true, ok, "object %d class %#x listed %d: %d faces, %d -> %d", id,
                      kind, listed, faces, before, nodes);
}
EI_VERIFY(0x005651A0, RefreshPlatformsVerify, 0, s_unused);

// 0056D7E0 QueueSynchronised: ours only (it edits the list and the id map; reached by a lever's
// network state, 0052B650 through its address: `levers`). Checked: the id maps to a node holding
// the object afterwards, and the result is whether it was absent before.
static bool __fastcall QueueSynchronisedVerify(CWorldClient* client, void*, CEngineObject* object) {
    const auto key = static_cast<std::uint32_t>(object->id_);
    const bool absent = client->dynamicNodes_.Find(key) == nullptr;
    const bool result = client->QueueSynchronised(object);
    const PendingMap::Node* entry = client->dynamicNodes_.Find(key);
    const bool ok = result == absent && entry && (!absent || static_cast<mfc::CList<CEngineObject*>::Node*>(entry->value)->data == object);
    ei::verify::Check("CWorldClient::QueueSynchronised", true, ok, "object %d: absent %d, result %d", object->id_, absent,
                      result);
    return result;
}
EI_VERIFY(0x0056D7E0, QueueSynchronisedVerify, 0, s_unused);

// 0056D7A0 DropShadows: ours only (reached by the options' display mode change, 006524A0; no
// scenario changes it). Nothing is compared: it only calls 0051E200 / 0051E280 per filed object.
static void __fastcall DropShadowsVerify(CWorldClient* client) {
    client->DropShadows();
    ei::verify::Check("CWorldClient::DropShadows", true, true, "");
}
EI_VERIFY(0x0056D7A0, DropShadowsVerify, 0, s_unused);

// 00564E70: ours only. The grids, +0xB4, +0x7BC, the MFC lists and the id map are empty; what is
// left in +0xB8 has no positive id.
static void __fastcall ClearMapsVerify(CWorldClient* client, void*) {
    client->ClearMaps();
    bool ok = GridEmpty(At<SectorGrid>(client, 0x120)) && GridEmpty(At<SectorGrid>(client, 0x150)) &&
              ListEmpty(client, 0xB4) && ListEmpty(client, 0x7BC) && At<PendingMap>(client, 0xD8).Size() == 0 &&
              At<void*>(client, 0xC0) == nullptr && At<void*>(client, 0x1F4) == nullptr && At<int>(client, 0xEC) == 0;
    SectorListNode* head = ListHead(client, 0xB8);
    for (SectorListNode* node = head->next; node != head; node = node->next)
        if (node->data->id_ > 0) ok = false;
    ei::verify::Check("CWorldClient::ClearMaps", true, ok, "");
}
EI_VERIFY(0x00564E70, ClearMapsVerify, 0, s_unused);

// 00566F70: ours only. No listing is lost, and +0xB8 grows by at most one per record.
static void __fastcall CreatePartyUnitsVerify(CWorldClient* client, void*, const RecordPtrArray* party, char allArmour) {
    auto size = [client] {
        SectorListNode* head = ListHead(client, 0xB8);
        int n = 0;
        for (SectorListNode* node = head->next; node != head; node = node->next) ++n;
        return n;
    };
    int count = party->size;
    std::vector<int> before;
    for (int i = 0; i < count; ++i)
        before.push_back(CountIdInList(client, 0xB8, At<int>(party->data[i], 0x1C)));
    int sizeBefore = size();
    client->CreatePartyUnits(party, allArmour);
    int sizeAfter = size();
    bool ok = sizeAfter >= sizeBefore && sizeAfter - sizeBefore <= count;
    for (int i = 0; i < count; ++i)
        if (CountIdInList(client, 0xB8, At<int>(party->data[i], 0x1C)) < before[i]) ok = false;
    ei::verify::Check("CWorldClient::CreatePartyUnits", true, ok, "%d records, objects %d -> %d", count, sizeBefore,
                      sizeAfter);
}
EI_VERIFY(0x00566F70, CreatePartyUnitsVerify, 0, s_unused);

// 005726B0: ours only. True exactly for a positive id.
static bool __fastcall ReleaseIfIdentifiedVerify(void* functor, void*, CEngineObject* object) {
    bool positive = object->id_ > 0;
    bool result = ReleaseIfIdentified(functor, nullptr, object);
    ei::verify::Check("ReleaseIfIdentified", positive, result, "");
    return result;
}
EI_VERIFY(0x005726B0, ReleaseIfIdentifiedVerify, 0, s_unused);

// 00565790 / 005659D0: SUB ESP,0x1C; PUSH EBX; PUSH EBP (5 bytes). The original runs first; the
// object, the sorted entries and the cursor's position are compared with ours.
using PickFn = CEngineObject*(__fastcall*)(CWorldClient*, void*, const long*);
static PickFn s_pick;
static PickFn s_pick2;

namespace {
CEngineObject* PickChecked(const char* name, PickFn original, bool second, CWorldClient* client, const long* point) {
    long pt[2] = {point[0], point[1]};
    CEngineObject* theirs = original(client, nullptr, pt);
    Hits& hits = At<Hits>(client, 0xF4);
    std::vector<std::uint8_t> theirsHits(hits.start, hits.finish);
    std::ptrdiff_t theirsCursor = At<std::uint8_t*>(client, 0x100) - hits.start;
    CEngineObject* mine = second ? client->PickAt2(pt) : client->PickAt(pt);
    std::vector<std::uint8_t> mineHits(hits.start, hits.finish);
    std::ptrdiff_t mineCursor = At<std::uint8_t*>(client, 0x100) - hits.start;
    bool ok = theirs == mine && theirsHits == mineHits && (theirsHits.empty() || theirsCursor == mineCursor);
    ei::verify::Check(name, true, ok, "(%ld, %ld): %zu vs %zu entries, cursor %td vs %td", pt[0], pt[1],
                      theirsHits.size() / 0x18, mineHits.size() / 0x18, theirsCursor, mineCursor);
    return mine;
}
} // namespace

static CEngineObject* __fastcall PickAtVerify(CWorldClient* client, void*, const long* point) {
    return PickChecked("CWorldClient::PickAt", s_pick, false, client, point);
}
EI_VERIFY(0x00565790, PickAtVerify, 5, s_pick);

static CEngineObject* __fastcall PickAt2Verify(CWorldClient* client, void*, const long* point) {
    return PickChecked("CWorldClient::PickAt2", s_pick2, true, client, point);
}
EI_VERIFY(0x005659D0, PickAt2Verify, 5, s_pick2);

// ---- Platforms: the original on the real set, ours on a scratch set; the records compared --------

namespace {
// An empty platform set: its record list (+0x1C) takes nodes from its own CPlex blocks, sized like
// the real set's (+0x34), and frees them when done.
struct ScratchPlatforms {
    alignas(4) std::uint8_t bytes[0x38] = {};
    explicit ScratchPlatforms(const PlatformSet* real) {
        std::memcpy(bytes + 0x1C, reinterpret_cast<const std::uint8_t*>(real) + 0x1C, 4);   // the list's vtable
        std::memcpy(bytes + 0x34, reinterpret_cast<const std::uint8_t*>(real) + 0x34, 4);   // block size
    }
    PlatformSet* set() { return reinterpret_cast<PlatformSet*>(bytes); }
    ~ScratchPlatforms() { mfc::PlexFreeDataChain(*reinterpret_cast<mfc::CPlex**>(bytes + 0x30)); }
};

// Floats within 8 ULPs (or `absEps` near zero); NaN matches NaN.
bool NearFloat(float expected, float actual, float absEps) {
    return ei::verify::NearlyEqual<float>(expected, actual, 8, absEps);
}

// The records added after `last` (nodes link backwards from head), oldest first.
std::vector<const Platform*> RecordsAfter(const PlatformSet* set, const PlatformNode* last) {
    std::vector<const Platform*> records;
    for (const PlatformNode* node = set->list.head; node && node != last; node = node->next)
        records.insert(records.begin(), &node->data);
    return records;
}

bool SameRecord(const char* name, const Platform* theirs, const Platform* mine) {
    const float* a = reinterpret_cast<const float*>(theirs);
    const float* b = reinterpret_cast<const float*>(mine);
    bool ok = std::memcmp(a, b, 4) == 0 && std::memcmp(a + 4, b + 4, 4) == 0;   // id, radius
    for (int i : {1, 2, 3}) ok = ok && NearFloat(a[i], b[i], 1e-4f);
    for (int i = 5; i < 19; ++i) ok = ok && NearFloat(a[i], b[i], 1e-5f);
    if (!ok) {
        int first = 0;
        while (first < 19 && std::memcmp(a + first, b + first, 4) == 0) ++first;
        ei::verify::Check(name, a[first < 19 ? first : 0], b[first < 19 ? first : 0], "record word %d", first);
    }
    return ok;
}
} // namespace

// 00560650: MOV EAX,[ESP+0x14]; SUB ESP,0x64 (7 bytes).
using AddFaceFn = int(__fastcall*)(PlatformSet*, void*, const float*, const float*, const float*, const float*, Platform**);
static AddFaceFn s_addFace;
static int __fastcall AddFaceVerify(PlatformSet* set, void*, const float* a, const float* b, const float* c,
                                    const float* normal, Platform** out) {
    int result = s_addFace(set, nullptr, a, b, c, normal, out);
    Platform* theirs = *out;
    ScratchPlatforms scratch(set);
    Platform* mine;
    int mineResult = scratch.set()->AddFace(a, b, c, normal, &mine);
    bool ok = result == mineResult && (theirs == nullptr) == (mine == nullptr);
    // The caller fills id, centre and radius: compare the face's own fields only.
    if (ok && theirs) {
        const float* x = reinterpret_cast<const float*>(theirs);
        const float* y = reinterpret_cast<const float*>(mine);
        for (int i = 5; i < 19; ++i) ok = ok && NearFloat(x[i], y[i], 1e-5f);
    }
    ei::verify::Check("PlatformSet::AddFace", true, ok, "normal (%g, %g, %g), result %d vs %d", normal[0], normal[1],
                      normal[2], result, mineResult);
    return result;
}
EI_VERIFY(0x00560650, AddFaceVerify, 7, s_addFace);

// 00560820: SUB ESP,0x13C (6 bytes).
using AddBoxFn = void(__fastcall*)(PlatformSet*, void*, int, const FootprintPart*, const Vector3*, const Quaternion*);
static AddBoxFn s_addBox;
static void __fastcall AddBoxVerify(PlatformSet* set, void*, int id, const FootprintPart* item, const Vector3* position,
                                    const Quaternion* rotation) {
    PlatformNode* last = set->list.head;
    s_addBox(set, nullptr, id, item, position, rotation);
    std::vector<const Platform*> theirs = RecordsAfter(set, last);
    ScratchPlatforms scratch(set);
    scratch.set()->AddBox(id, item, position, rotation);
    std::vector<const Platform*> mine = RecordsAfter(scratch.set(), nullptr);
    bool ok = theirs.size() == mine.size();
    for (std::size_t i = 0; ok && i < theirs.size(); ++i) ok = SameRecord("PlatformSet::AddBox record", theirs[i], mine[i]);
    ei::verify::Check("PlatformSet::AddBox", true, ok, "id %d: %zu vs %zu faces", id, theirs.size(), mine.size());
}
EI_VERIFY(0x00560820, AddBoxVerify, 6, s_addBox);

// 00561E50: ours only. Returns 1 with the owned objects made, both grids at 4 x 4 and room for 400
// screen entries.
static int __fastcall CreateVerify(CWorldClient* client, void*) {
    int result = client->Setup();
    bool ok = result == 1;
    for (int offset : {0x1CC, 0x1D0, 0x180, 0x44, 0x18C, 0x188, 0x190, 0x19C, 0x1A0, 0x1A4, 0x1A8})
        ok = ok && At<void*>(client, offset) != nullptr;
    for (int grid : {0x120, 0x150})
        ok = ok && At<SectorGrid>(client, grid).width_ == 4 && At<SectorGrid>(client, grid).height_ == 4;
    const Hits& entries = At<Hits>(client, 0xF4);
    ok = ok && (entries.eos - entries.start) / 0x18 >= 400;
    ei::verify::Check("CWorldClient::Setup", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x00561E50, CreateVerify, 0, s_unused);

// 00563290: ours only. Afterwards the owned pointers are cleared, the lists and grid cells empty.
static void __fastcall DestroyVerify(CWorldClient* client, void*) {
    client->Teardown();
    bool ok = GridEmpty(At<SectorGrid>(client, 0x120));
    for (int offset : {0x1B8, 0x1BC, 0x1C0, 0x7BC, 0xB4, 0xB8}) ok = ok && ListEmpty(client, offset);
    for (int offset : {0x1B4, 0x1AC, 0x1B0, 0x20C, 0x1A0, 0x1A4, 0x19C, 0x180, 0x190, 0x188, 0x18C, 0x1A8, 0x1CC, 0x1D0})
        ok = ok && At<void*>(client, offset) == nullptr;
    ei::verify::Check("CWorldClient::Teardown", true, ok, "");
}
EI_VERIFY(0x00563290, DestroyVerify, 0, s_unused);

// 0056DAF0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x20 (6 bytes). The original appends first; its
// entries are taken back off the list (erase, 0056DFB0) and ours appends; the entries and the
// cell range and cursor (+0x138..+0x14C) are compared. The distance test is plain C++: an object
// exactly on the range's edge may rarely differ (rule 7).
using InRangeFn = void(__fastcall*)(CWorldClient*, void*, const float*, float, void*);
static InRangeFn s_inRange;
__declspec(naked) static void __fastcall PlainListErase(void*, void*, SectorListNode**, SectorListNode*)
    EI_ORIGINAL(0x0056DFB0)

static void __fastcall ObjectsInRangeVerify(CWorldClient* client, void*, const float* point, float radius, void* out) {
    float p[3] = {point[0], point[1], point[2]};
    SectorListNode* head = *static_cast<SectorListNode**>(out);
    SectorListNode* last = head->prev;   // the entries after it are the call's
    auto appended = [head](SectorListNode* from) {
        std::vector<CEngineObject*> values;
        for (SectorListNode* node = from->next; node != head; node = node->next) values.push_back(node->data);
        return values;
    };
    s_inRange(client, nullptr, p, radius, out);
    std::vector<CEngineObject*> theirs = appended(last);
    std::uint32_t theirsCells[6];
    std::memcpy(theirsCells, &At<std::uint32_t>(client, 0x138), sizeof(theirsCells));
    while (last->next != head) {
        SectorListNode* ignored;
        PlainListErase(out, nullptr, &ignored, last->next);
    }
    client->ObjectsInRange(p, radius, out);
    std::vector<CEngineObject*> mine = appended(last);
    bool ok = theirs == mine && std::memcmp(theirsCells, &At<std::uint32_t>(client, 0x138), sizeof(theirsCells)) == 0;
    ei::verify::Check("CWorldClient::ObjectsInRange", true, ok, "(%g, %g, %g) r %g: %zu vs %zu objects", p[0], p[1],
                      p[2], radius, theirs.size(), mine.size());
}
EI_VERIFY(0x0056DAF0, ObjectsInRangeVerify, 6, s_inRange);
