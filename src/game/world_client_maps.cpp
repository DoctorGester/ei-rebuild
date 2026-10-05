// The client world's object maps (0056E000-00574000 helpers and the CWorldClient methods that keep
// the maps). See docs/notes/world.md ("Client maps").
//
// The client files every object in a sector grid (+0x120: STLport lists of counted pointers, one
// per 16x16 cell) and the second grid at +0x150; most of 0056E000-00574000 are the STLport and MFC
// instantiations behind those maps (config/library.csv). Rewritten here: the grids' Move (two
// instantiations), the window hit collectors, the radius marker, the pending-map lookup and the
// bounds-checked record accessors.
#include "world.h"

#include "base_manager.h"
#include "console.h"
#include "object_tables.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_units.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr int kSlotRelease = 4;

// _hypot (the game's CRT), through its address.

// The cell index of a coordinate: FISTP (round to nearest), then an unsigned shift by 4, so a
// negative coordinate gives a huge index that fails the bounds tests.
std::uint32_t CellOf(float coordinate) { return static_cast<std::uint32_t>(x87::fistp(coordinate)) >> 4; }

// The list operations of the two grids' instantiations (thiscall on the cell's list):
// erase(out iterator, node), end(out iterator) and insert(out iterator, position, value).
struct CellListA {
    SectorListNode* head;
    SectorListNode** Erase(SectorListNode** out, SectorListNode* node);                                  // 00572270
    SectorListNode** End(SectorListNode** out);                                                          // 0056DEC0
    SectorListNode** Insert(SectorListNode** out, SectorListNode* position, CEngineObject* const& value);   // 0056E8A0
};
__declspec(naked) SectorListNode** CellListA::Erase(SectorListNode**, SectorListNode*) EI_ORIGINAL(0x00572270)
__declspec(naked) SectorListNode** CellListA::End(SectorListNode**) EI_ORIGINAL(0x0056DEC0)
__declspec(naked) SectorListNode** CellListA::Insert(SectorListNode**, SectorListNode*, CEngineObject* const&)
    EI_ORIGINAL(0x0056E8A0)

struct CellListB {
    SectorListNode* head;
    SectorListNode** Erase(SectorListNode** out, SectorListNode* node);                                  // 005755E0
    SectorListNode** End(SectorListNode** out);                                                          // 00572AF0
    SectorListNode** Insert(SectorListNode** out, SectorListNode* position, CEngineObject* const& value);   // 00572B00
};
__declspec(naked) SectorListNode** CellListB::Erase(SectorListNode**, SectorListNode*) EI_ORIGINAL(0x005755E0)
__declspec(naked) SectorListNode** CellListB::End(SectorListNode**) EI_ORIGINAL(0x00572AF0)
__declspec(naked) SectorListNode** CellListB::Insert(SectorListNode**, SectorListNode*, CEngineObject* const&)
    EI_ORIGINAL(0x00572B00)

// The counted pointer's copy and destruction, as the original inlines them.
void HoldRef(CEngineObject* object) {
    if (object) ++object->refCount_;
}
void DropRef(CEngineObject* object) {
    if (object) ei::VCall<void>(object, kSlotRelease);
}

template <typename List>
void MoveInGrid(SectorGrid* grid, CEngineObject* object, const float* position) {
    float* stored = &At<float>(object, 0x1C);
    std::uint32_t oldX = CellOf(stored[0]);
    std::uint32_t oldY = CellOf(stored[1]);
    std::uint32_t newX = CellOf(position[0]);
    std::uint32_t newY = CellOf(position[1]);
    if (oldX == newX && oldY == newY) {
        std::memcpy(stored, position, 12);
        return;
    }
    if (oldX < grid->width_ && oldY < grid->height_) {
        // list::remove(value) with a counted copy of the object as the value.
        HoldRef(object);
        List* list = reinterpret_cast<List*>(&grid->rows_[oldY][oldX]);
        SectorListNode* head = list->head;
        SectorListNode* node = head->next;
        while (node != head) {
            SectorListNode* next = node->next;
            if (node->value == object) {
                SectorListNode* ignored;
                list->Erase(&ignored, node);
            }
            node = next;
        }
        DropRef(object);
    }
    std::memcpy(stored, position, 12);
    if (newX < grid->width_ && newY < grid->height_) {
        // push_back of a counted copy.
        CEngineObject* value = object;
        HoldRef(value);
        List* list = reinterpret_cast<List*>(&grid->rows_[newY][newX]);
        SectorListNode* end;
        SectorListNode* inserted;
        list->Insert(&inserted, *list->End(&end), value);
        DropRef(value);
    }
}

} // namespace

// ---- Sector grids --------------------------------------------------------------------------------

void SectorGrid::Move(CEngineObject* object, const float* position) {
    MoveInGrid<CellListA>(this, object, position);
}

void SectorGrid::Move2(CEngineObject* object, const float* position) {
    MoveInGrid<CellListB>(this, object, position);
}

// ---- Functors ------------------------------------------------------------------------------------

float* __fastcall MarkWithinRadius(float* out, void*, std::uint8_t* first, std::uint8_t* last, float x, float y,
                                   float scale) {
    for (std::uint8_t* node = first; node != last; node = *reinterpret_cast<std::uint8_t**>(node)) {
        double dx = At<float>(node, 0x10) - x;
        double dy = At<float>(node, 0x14) - y;
        double distance = Hypot(dx, dy);
        float limit = At<float>(node, 0x94) * scale;
        // FCOMPP + TEST AH,0x41: marked unless the distance is greater (a NaN marks too).
        bool unordered = x87::IsNaN(static_cast<float>(distance)) || x87::IsNaN(limit);
        if (unordered || !(distance > static_cast<double>(limit))) At<std::uint8_t>(node, 0xD) = 1;
    }
    out[0] = x;
    out[1] = y;
    out[2] = scale;
    return out;
}

namespace {

// vector<0x18-byte entry>::_M_insert_overflow (00573350) and push_back as the collectors inline it.
struct HitVector {
    std::uint8_t* start;
    std::uint8_t* finish;
    std::uint8_t* eos;
    void InsertOverflow(std::uint8_t* position, const std::uint8_t* value, int count);
    void PushBack(const std::uint8_t* entry) {
        if (finish != eos) {
            if (finish) std::memcpy(finish, entry, 0x18);
            finish += 0x18;
        } else {
            InsertOverflow(finish, entry, 1);
        }
    }
};
__declspec(naked) void HitVector::InsertOverflow(std::uint8_t*, const std::uint8_t*, int) EI_ORIGINAL(0x00573350)

// The list entries (node + 8, 0x18 bytes): +4 object id, +8 RECT. The hit test is slot `slot` of
// the object's +0x94 (the point).
void** CollectHits(void** out, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x, LONG y,
                   CBaseManager* manager, int slot) {
    HitVector* vector = static_cast<HitVector*>(hits);
    for (std::uint8_t* node = first; node != last; node = *reinterpret_cast<std::uint8_t**>(node)) {
        std::uint8_t* entry = node + 8;
        POINT pt = {x, y};
        if (!PtInRect(reinterpret_cast<const RECT*>(entry + 8), pt)) continue;
        CEngineObject* object = manager->FindObject(At<unsigned>(entry, 4));
        if (!object) continue;
        LONG point[2] = {x, y};
        if (!ei::VCall<int>(At<void*>(object, 0x94), slot, point)) continue;
        vector->PushBack(entry);
    }
    *out = hits;
    return out;
}

} // namespace

void** __fastcall CollectWindowsAt(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x,
                                   LONG y, CBaseManager* manager) {
    return CollectHits(out, first, last, hits, x, y, manager, 0x58 / 4);
}

void** __fastcall CollectWindowsAt2(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x,
                                    LONG y, CBaseManager* manager) {
    return CollectHits(out, first, last, hits, x, y, manager, 0x60 / 4);
}

// ---- Lookups -------------------------------------------------------------------------------------

int __fastcall PendingMapLookup(const PendingMap* map, void*, const std::uint32_t* key, void** value) {
    const PendingMap::Node* node = map->Find(*key);
    if (!node) return 0;
    *value = node->value;
    return 1;
}

const std::uint8_t* __fastcall RecordAt50(const RecordVector* records, void*, std::uint32_t index) {
    int count = static_cast<int>((records->finish - records->start) / 0x50);
    if (index < static_cast<std::uint32_t>(count)) return records->start + index * 0x50;
    return nullptr;
}

const std::uint8_t* __fastcall RecordAtB4(const RecordVector* records, void*, std::uint32_t index) {
    int count = static_cast<int>((records->finish - records->start) / 0xB4);
    if (index < static_cast<std::uint32_t>(count)) return records->start + index * 0xB4;
    return nullptr;
}

// ---- The client's object maps --------------------------------------------------------------------

namespace {

// A plain STLport list of pointers (+0xB4 counted, +0xB8 referenced by hand, +0x7BC, +0xF0).
struct PlainList {
    SectorListNode* head;
    SectorListNode** End(SectorListNode** out);                                                     // 0056DFA0
    SectorListNode** Insert(SectorListNode** out, SectorListNode* position, CEngineObject* const& value);   // 0056EA80
    SectorListNode** Begin(SectorListNode** out);                                                   // 0056DF90
    SectorListNode** Erase(SectorListNode** out, SectorListNode* node);                             // 0056DFB0
};
__declspec(naked) SectorListNode** PlainList::End(SectorListNode**) EI_ORIGINAL(0x0056DFA0)
__declspec(naked) SectorListNode** PlainList::Insert(SectorListNode**, SectorListNode*, CEngineObject* const&)
    EI_ORIGINAL(0x0056EA80)
__declspec(naked) SectorListNode** PlainList::Begin(SectorListNode**) EI_ORIGINAL(0x0056DF90)
__declspec(naked) SectorListNode** PlainList::Erase(SectorListNode**, SectorListNode*) EI_ORIGINAL(0x0056DFB0)

// The sector grid's cell list: remove(counted value) [00570400], push_back [0056DED0], clear
// [005703C0]; the second grid's clear [00573660].
struct CellList {
    SectorListNode* head;
    void Remove(CEngineObject* const* value);      // 00570400
    void PushBack(CEngineObject* const* value);    // 0056DED0
    void Clear();                                  // 005703C0
    void Clear2();                                 // 00573660
};
__declspec(naked) void CellList::Remove(CEngineObject* const*) EI_ORIGINAL(0x00570400)
__declspec(naked) void CellList::PushBack(CEngineObject* const*) EI_ORIGINAL(0x0056DED0)
__declspec(naked) void CellList::Clear() EI_ORIGINAL(0x005703C0)
__declspec(naked) void CellList::Clear2() EI_ORIGINAL(0x00573660)

// An MFC CList<CEngineObject*> / CList<int>: head, tail, count, free list, blocks.
struct MfcNode {
    MfcNode* next;
    MfcNode* prev;
    void* data;
};
struct MfcList {
    void* vtable;
    MfcNode* head;
    MfcNode* tail;
    int count;
    MfcNode* free;
    mfc::CPlex* blocks;
    MfcNode* NewNode(MfcNode* prev, MfcNode* next);   // 00573290
    void FreeNode(MfcNode* node);                     // 00573300
    void RemoveAt(MfcNode* node);                     // 0056FDD0
};
__declspec(naked) MfcNode* MfcList::NewNode(MfcNode*, MfcNode*) EI_ORIGINAL(0x00573290)
__declspec(naked) void MfcList::FreeNode(MfcNode*) EI_ORIGINAL(0x00573300)
__declspec(naked) void MfcList::RemoveAt(MfcNode*) EI_ORIGINAL(0x0056FDD0)

// The id map +0xD8 (hash_map<unsigned, MfcNode*>, then the highest key at +0x14): operator[]
// [00406A10], erase(iterator) [00405720], clear [00406E80].
__declspec(naked) void** __fastcall IdMapAt(void*, void*, const int*) EI_ORIGINAL(0x00406A10)
void __fastcall IdMapErase(void* a0, void*, const void* a2, const void* a3) { HashEraseOriginal(a0, nullptr, (void*)a2, a3); }
__declspec(naked) void __fastcall IdMapClear(void*) EI_ORIGINAL(0x00406E80)

// Item `index` of an object's two item arrays [00564AF0, on the object]; a platform item added to
// the client's platform set [00560820: id, item, position, orientation].
__declspec(naked) std::uint32_t* __fastcall ObjectItem(void*, void*, int) EI_ORIGINAL(0x00564AF0)
__declspec(naked) void __fastcall PlatformsAdd(void*, void*, int, std::uint32_t*, float*, float*) EI_ORIGINAL(0x00560820)
// 00538EA0 on a unit's +0x240 (a hero leaving a session of kind 3).
__declspec(naked) void __fastcall HeroLeft(void*) EI_ORIGINAL(0x00538EA0)
// ClearMaps: a counted node value's destructor [00575510 (flags)], the node's release to the pool
// [0056FB40 (node, count)], the session's player data [00665200], a manager's records [00404410].
__declspec(naked) void __fastcall SessionPlayerClear(void*) EI_ORIGINAL(0x00665200)
__declspec(naked) void __fastcall ManagerFreeRecords(void*) EI_ORIGINAL(0x00404410)

// The STLport sort over the 0x18-byte screen entries with DepthLess {plane, manager}:
// __introsort_loop [00573740] (its heap fallback is partial_sort 005769C0), __insertion_sort
// [00575BC0], __unguarded_insertion_sort [00576D40].
__declspec(naked) void __fastcall IntroSortLoop(std::uint8_t*, std::uint8_t*, void*, int, const float*, void*)
    EI_ORIGINAL(0x00573740)
__declspec(naked) void __fastcall InsertionSort(std::uint8_t*, std::uint8_t*, const float*, void*) EI_ORIGINAL(0x00575BC0)
__declspec(naked) void __fastcall UnguardedInsertionSort(std::uint8_t*, std::uint8_t*, void*, const float*, void*)
    EI_ORIGINAL(0x00576D40)

constexpr int kSlotKeepInMaps = 0x74 / 4;    // object: () -> AL
constexpr int kSlotClassId = 0x30 / 4;       // object: ()
constexpr int kSlotSessionJoin = 0xAC / 4;   // unit +0x240: ()
constexpr int kSlotObjects = 0x4C / 4;       // client: () -> the object manager
constexpr int kSlotReset = 0x14 / 4;         // client +0x34: ()
constexpr int kClassPlatform54 = 0x54;
constexpr int kClassPlatform5D = 0x5D;
constexpr int kTypeHero = 0x32;

PlainList& ListAt(void* client, int offset) { return *reinterpret_cast<PlainList*>(static_cast<std::uint8_t*>(client) + offset); }
MfcList& MfcListAt(void* client, int offset) { return *reinterpret_cast<MfcList*>(static_cast<std::uint8_t*>(client) + offset); }
SectorGrid& GridAt(void* client, int offset) { return *reinterpret_cast<SectorGrid*>(static_cast<std::uint8_t*>(client) + offset); }
PendingMap& IdMap(void* client) { return *reinterpret_cast<PendingMap*>(static_cast<std::uint8_t*>(client) + 0xD8); }

// The object's grid cell list, or null outside the grid.
CellList* CellFor(SectorGrid& grid, CEngineObject* object) {
    std::uint32_t x = CellOf(At<float>(object, 0x1C));
    std::uint32_t y = CellOf(At<float>(object, 0x20));
    if (x >= grid.width_ || y >= grid.height_) return nullptr;
    return reinterpret_cast<CellList*>(&grid.rows_[y][x]);
}

} // namespace

int CWorldClient::AddObject(CEngineObject* object) {
    PlainList& objects = ListAt(this, 0xB8);
    for (SectorListNode* node = objects.head->next; node != objects.head; node = node->next) {
        if (node->value->id_ == object->id_) {
            ++object->refCount_;
            ei::VCall<void>(object, kSlotRelease);
            return 1;
        }
    }
    const PendingMap::Node* entry = IdMap(this).Find(static_cast<std::uint32_t>(object->id_));
    if (!entry && ei::VCall<bool>(object, kSlotKeepInMaps)) {
        MfcList& kept = MfcListAt(this, 0xBC);
        MfcNode* node = kept.NewNode(kept.tail, nullptr);
        node->data = object;
        if (kept.tail)
            kept.tail->next = node;
        else
            kept.head = node;
        kept.tail = node;
        MfcNode* tail = kept.tail;
        int key = object->id_;
        *IdMapAt(&IdMap(this), nullptr, &key) = tail;
    }
    int kind = ei::VCall<int>(object, kSlotClassId);
    if (kind == kClassPlatform54 || ei::VCall<int>(object, kSlotClassId) == kClassPlatform5D) {
        for (int i = 0; i < At<int>(object, 0xD4); ++i) {
            if ((*ObjectItem(object, nullptr, i) >> 10) & 1) {
                int id = object->id_;
                PlatformsAdd(&platforms_, nullptr, id, ObjectItem(object, nullptr, i), &At<float>(object, 0x1C),
                             &At<float>(object, 0x34));
            }
        }
    }
    if (ei::VCall<int>(object, kSlotClassId) == world::kClassUnit && At<int>(session_, 0x100) == 1 &&
        At<int>(object, 0x24C) == 0 && At<int>(this, 0x6EC) >= 1)
        ei::VCall<void>(At<void*>(object, 0x240), kSlotSessionJoin);

    ++object->refCount_;
    SectorListNode* end;
    SectorListNode* inserted;
    objects.Insert(&inserted, *objects.End(&end), object);

    SectorGrid& grid = GridAt(this, 0x120);
    if (CellList* cell = CellFor(grid, object)) {
        CEngineObject* value = object;
        HoldRef(value);
        cell->Remove(&value);
        DropRef(value);
    }
    if (CellList* cell = CellFor(grid, object)) {
        CEngineObject* value = object;
        HoldRef(value);
        cell->PushBack(&value);
        DropRef(value);
    }
    if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF)) {
        CEngineObject* value = object;
        HoldRef(value);
        CellListA& heroes = *reinterpret_cast<CellListA*>(&ListAt(this, 0xB4));
        SectorListNode* heroEnd;
        heroes.Insert(&inserted, *heroes.End(&heroEnd), value);
        DropRef(value);
    }
    return 1;
}

void CWorldClient::RemoveObject(int id) {
    MfcList& pending = MfcListAt(this, 0x1F0);
    for (MfcNode* node = pending.head; node;) {
        MfcNode* next = node->next;
        if (reinterpret_cast<std::intptr_t>(node->data) == id) pending.RemoveAt(node);
        node = next;
    }
    PendingMap& ids = IdMap(this);
    if (const PendingMap::Node* entry = ids.Find(static_cast<std::uint32_t>(id))) {
        MfcList& kept = MfcListAt(this, 0xBC);
        MfcNode* node = static_cast<MfcNode*>(entry->value);
        if (node == kept.head)
            kept.head = node->next;
        else
            node->prev->next = node->next;
        if (node == kept.tail)
            kept.tail = node->prev;
        else
            node->next->prev = node->prev;
        kept.FreeNode(node);
        if (const PendingMap::Node* again = ids.Find(static_cast<std::uint32_t>(id))) IdMapErase(&ids, nullptr, again, &ids);
    }
    PlainList& objects = ListAt(this, 0xB8);
    SectorListNode* node = objects.head->next;
    for (;; node = node->next) {
        if (node == objects.head) return;
        if (node->value->id_ == id) break;
    }
    CEngineObject* object = node->value;
    if (object->type_ == kTypeHero && (IsTextKey(id) & 0xFF)) {
        CEngineObject* value = object;
        HoldRef(value);
        CellListA& heroes = *reinterpret_cast<CellListA*>(&ListAt(this, 0xB4));
        for (SectorListNode* hero = heroes.head->next; hero != heroes.head;) {
            SectorListNode* next = hero->next;
            if (hero->value == value) {
                SectorListNode* ignored;
                heroes.Erase(&ignored, hero);
            }
            hero = next;
        }
        DropRef(value);
        if (At<int>(session_, 0x100) == 3) HeroLeft(At<void*>(object, 0x240));
    }
    if (CellList* cell = CellFor(GridAt(this, 0x120), object)) {
        CEngineObject* value = object;
        HoldRef(value);
        cell->Remove(&value);
        DropRef(value);
    }
    SectorListNode* ignored;
    objects.Erase(&ignored, node);
    ei::VCall<void>(object, kSlotRelease);
}

bool __fastcall ReleaseIfIdentified(void*, void*, CEngineObject* object) {
    if (object->id_ > 0) {
        ei::VCall<void>(object, kSlotRelease);
        return true;
    }
    return false;
}

void CWorldClient::ClearMaps() {
    SectorGrid& second = GridAt(this, 0x150);
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            reinterpret_cast<CellList*>(&second.rows_[y][x])->Clear2();
    SectorGrid& grid = GridAt(this, 0x120);
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            reinterpret_cast<CellList*>(&grid.rows_[y][x])->Clear();

    // MFC RemoveAll of the two lists (the elements need no destruction).
    MfcList* lists[] = {&MfcListAt(this, 0x1F0), &MfcListAt(this, 0xBC)};
    for (MfcList* list : lists) {
        list->count = 0;
        list->free = nullptr;
        list->tail = nullptr;
        list->head = nullptr;
        mfc::PlexFreeDataChain(list->blocks);
        list->blocks = nullptr;
    }
    IdMapClear(&IdMap(this));
    At<int>(this, 0xD8 + 0x14) = 0;

    PlainList& records = ListAt(this, 0x7BC);
    for (SectorListNode* node = records.head->next; node != records.head;) {
        SectorListNode* next = node->next;
        if (node) stlp::deallocate(node, 0xC);
        node = next;
    }
    records.head->next = records.head;
    records.head->prev = records.head;

    PlainList& heroes = ListAt(this, 0xB4);
    for (SectorListNode* node = heroes.head->next; node != heroes.head;) {
        SectorListNode* next = node->next;
        CountedRelease(&node->value, nullptr, 0);
        NodeFree12Fast(node, 1);
        node = next;
    }
    heroes.head->next = heroes.head;
    heroes.head->prev = heroes.head;

    PlainList& objects = ListAt(this, 0xB8);
    SectorListNode* node;
    objects.Begin(&node);
    SectorListNode* end = objects.head;
    std::uint8_t functor = 0;
    while (node != end) {
        SectorListNode* next = node->next;
        if (ReleaseIfIdentified(&functor, nullptr, node->value)) {
            SectorListNode* ignored;
            objects.Erase(&ignored, node);
        }
        node = next;
    }
    SessionPlayerClear(session_);
    if (void* owner = At<void*>(this, 0x34)) ei::VCall<void>(owner, kSlotReset);
    ManagerFreeRecords(At<void*>(this, 0x3C));
    ManagerFreeRecords(At<void*>(this, 0x40));
    ManagerFreeRecords(At<void*>(this, 0x44));
}

namespace {

CEngineObject* Pick(CWorldClient* client, const long* point,
                    void** (__fastcall* collect)(void**, void*, std::uint8_t*, std::uint8_t*, void*, long, long,
                                                  CBaseManager*)) {
    HitVector& hits = At<HitVector>(client, 0xF4);
    hits.finish = hits.start;   // erase(begin, end): __copy of nothing
    CBaseManager* manager = ei::VCall<CBaseManager*>(client, kSlotObjects);
    PlainList& screen = At<PlainList>(client, 0xF0);
    void* functor;
    collect(&functor, nullptr, reinterpret_cast<std::uint8_t*>(screen.head->next),
            reinterpret_cast<std::uint8_t*>(screen.head), &hits, point[0], point[1], manager);
    if (hits.start == hits.finish) return nullptr;

    // sort(begin, end, DepthLess{top of the view stack, objects}), the introsort loop's first level
    // inlined in the original (the same steps as the out-of-line loop).
    std::uint8_t* device = *reinterpret_cast<std::uint8_t**>(0x007AF068);
    const float* plane = At<float*>(device, 0x5C) + (At<int>(device, 0x60) - 1) * 16;
    void* objects = ei::VCall<void*>(client, kSlotObjects);
    std::uint8_t* first = hits.start;
    std::uint8_t* last = hits.finish;
    if (first != last) {
        int count = static_cast<int>((last - first) / 0x18);
        int depth = 0;
        for (int n = count; n != 1; n >>= 1) ++depth;
        if (count > 16) {
            IntroSortLoop(first, last, nullptr, depth * 2, plane, objects);
            InsertionSort(first, first + 16 * 0x18, plane, objects);
            UnguardedInsertionSort(first + 16 * 0x18, last, nullptr, plane, objects);
        } else {
            InsertionSort(first, last, plane, objects);
        }
    }
    std::uint8_t*& cursor = At<std::uint8_t*>(client, 0x100);
    for (cursor = hits.start; cursor != hits.finish; cursor += 0x18) {
        if (CEngineObject* object = manager->FindObject(At<unsigned>(cursor, 4))) return object;
    }
    return nullptr;
}

} // namespace

CEngineObject* CWorldClient::PickAt(const long* point) { return Pick(this, point, CollectWindowsAt); }
CEngineObject* CWorldClient::PickAt2(const long* point) { return Pick(this, point, CollectWindowsAt2); }

// ---- The party's units (00566F70) ----------------------------------------------------------------

namespace {

// The prototypes' name map (007AF364, hash_map<string, record*>): find or insert a {name, 0} pair
// [00514880], returning the stored pair; the prototype table's lookup by name [00514930].
struct NamePair {
    stlp::string first;
    int second;
};
__declspec(naked) std::uint8_t* __fastcall NameMapFindOrInsert(void*, void*, const NamePair*) EI_ORIGINAL(0x00514880)
__declspec(naked) std::uint8_t* __fastcall FindPrototypeByName(void*, void*, const char*) EI_ORIGINAL(0x00514930)
// CUnit: the second prototype by name [005308D0], the figure made from a model and its part names
// [00516DA0], 00518840 / 00518860 after it, 0050EA40, the position [00517700], the items
// [005315B0 release all, 00531070 weapon, 00530730 armour, 00531180 quick item].
__declspec(naked) void __fastcall UnitSetPrototype2(void*, void*, const char*) EI_ORIGINAL(0x005308D0)
__declspec(naked) int __fastcall UnitMakeFigure(void*, void*, const char*, console::CStringArray*, int) EI_ORIGINAL(0x00516DA0)
__declspec(naked) void __fastcall Unit518840(void*, void*, const char*, int) EI_ORIGINAL(0x00518840)
__declspec(naked) void __fastcall Unit518860(void*) EI_ORIGINAL(0x00518860)
__declspec(naked) void __fastcall Unit50EA40(void*) EI_ORIGINAL(0x0050EA40)
__declspec(naked) void __fastcall UnitSetPosition(void*, void*, const float*) EI_ORIGINAL(0x00517700)
__declspec(naked) void __fastcall UnitReleaseAllItems(void*) EI_ORIGINAL(0x005315B0)
void __fastcall UnitAddWeapon(void* a0, void*, void* a2) { UnitAddWeaponOriginal(a0, nullptr, a2); }
void __fastcall UnitEquipArmour(void* a0, void*, void* a2) { UnitEquipArmourOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall UnitAddQuickItem(void*, void*, void*) EI_ORIGINAL(0x00531180)
// The figure's 0051C970 and its texture swap [0051C250: from, to (null: none)]; an armour's type
// [006A39E0, on the item].
__declspec(naked) void __fastcall Figure51C970(void*) EI_ORIGINAL(0x0051C970)
__declspec(naked) void __fastcall FigureSwapTexture(void*, void*, const char*, const char*) EI_ORIGINAL(0x0051C250)
__declspec(naked) int __fastcall ArmourType(void*) EI_ORIGINAL(0x006A39E0)

constexpr int kSlotCreate = 0x5C / 4;        // object manager: (type, id) -> object
constexpr int kSlotSetStats = 0x8C / 4;      // unit: (stats block)
constexpr int kSlotRefresh = 0x78 / 4;       // unit and its controller: ()
constexpr int kSlotSetPlayer = 0xF4 / 4;     // unit: (player)
constexpr int kSlotEquipped = 0x64 / 4;      // controller: (0)
constexpr int kSlotItemsChanged = 0x74 / 4;  // controller: ()

const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

bool SameText(const char* a, const char* b) {   // the inlined strcmp(a, b) == 0
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

// The figure's part names: "hd", "bd", "hp", "hr" (the hair: "hr.%.2d" with the prototype's
// number, or left out at -1), "lh1".."rl3".
constexpr std::uintptr_t kPartNames[] = {0x0078AD74, 0x0078AD38, 0x007900E4, 0x0078FB38, 0x007900E0, 0x0078AD70,
                                         0x007900DC, 0x007900D8, 0x0078AD6C, 0x0078F95C, 0x007900D4, 0x0078AD68,
                                         0x0078B008, 0x007900D0, 0x0078AD64, 0x0078B004};
constexpr std::uintptr_t kHair = 0x0078FB38;        // "hr"
constexpr std::uintptr_t kHairFormat = 0x0078FB0C;  // "hr.%.2d"
constexpr std::uintptr_t kNone = 0x007846F0;        // "none"

} // namespace

void CWorldClient::CreatePartyUnits(const PartyRecordArray* party, char allArmour) {
    mfc::CString hair;
    for (int i = 0; i < party->count; ++i) {
        std::uint8_t* record = static_cast<std::uint8_t*>(party->data[i]);
        console::CStringArray parts;
        for (std::uintptr_t name : kPartNames) {
            if (SameText(GameString(name), GameString(kHair))) {
                std::uint8_t* stored;
                {
                    stlp::string key(At<const char*>(record, 0x14));
                    NamePair pair{key, 0};
                    stored = NameMapFindOrInsert(reinterpret_cast<void*>(0x007AF364), nullptr, &pair);
                }
                int number = At<int>(At<void*>(stored, 0xC), 0x10);
                if (number != -1) {
                    hair.Format(GameString(kHairFormat), number);
                    parts.SetAtGrow(parts.GetSize(), hair);
                }
            } else {
                parts.SetAtGrow(parts.GetSize(), GameString(name));
            }
        }
        void* manager = ei::VCall<void*>(this, kSlotObjects);
        CEngineObject* unit = ei::VCall<CEngineObject*>(manager, kSlotCreate, kTypeHero, At<int>(record, 0x1C));
        if (!unit) return;   // (the original leaves the loop: the same as returning here)
        std::uint8_t* prototype =
            FindPrototypeByName(reinterpret_cast<void*>(0x007AF358), nullptr, At<const char*>(record, 0x14));
        At<int>(unit, 0x18) =
            prototype ? static_cast<int>(prototype - *reinterpret_cast<std::uint8_t**>(0x007AF358)) / 0x120 : -1;
        UnitSetPrototype2(unit, nullptr, At<const char*>(record, 0x18));
        unit->flags_ |= 0x4000000;
        if (!UnitMakeFigure(unit, nullptr, At<const char*>(record, 8), &parts, 1)) return;
        void* figure = At<void*>(unit, 0x94);
        Figure51C970(figure);
        Unit518840(unit, nullptr, At<const char*>(record, 8), 0);
        Unit518860(unit);
        const char* skin = At<const char*>(record, 0x10);
        FigureSwapTexture(figure, nullptr, GameString(kNone), SameText(skin, GameString(kNone)) ? nullptr : skin);
        Unit50EA40(unit);
        ei::VCall<void>(unit, kSlotSetStats, record + 0x20);
        UnitSetPosition(unit, nullptr, &At<float>(record, 0x724));
        std::memcpy(&At<float>(unit, 0x1AC), &At<float>(record, 0x724), 12);
        ei::VCall<void>(unit, kSlotRefresh);
        UnitReleaseAllItems(unit);
        for (int j = 0; j < At<int>(record, 0x734); ++j)
            UnitAddWeapon(unit, nullptr, At<void**>(record, 0x730)[j]);
        void* controller = At<void*>(unit, 0x240);
        if (At<int>(record, 0x734) != 0) ei::VCall<void>(controller, kSlotEquipped, 0);
        for (int j = 0; j < At<int>(record, 0x754); ++j) {
            void* armour = At<void**>(record, 0x750)[j];
            if (allArmour || ArmourType(armour) != 0) UnitEquipArmour(unit, nullptr, armour);
        }
        for (int j = 0; j < At<int>(record, 0x744); ++j)
            UnitAddQuickItem(unit, nullptr, At<void**>(record, 0x740)[j]);
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotItemsChanged);
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotRefresh);
        ei::VCall<void>(unit, kSlotSetPlayer, At<void*>(record, 0));
        AddObject(unit);
    }
}

// ---- Objects in range (0056DAF0) -----------------------------------------------------------------

namespace {

// The original's branch-free selects after FCOMP + FNSTSW (C0 shifted into a mask): `a < b`
// (true when unordered) picks one side.
float MaxZero(float b) { return (x87::IsNaN(b) || 0.0f < b) ? b : 0.0f; }   // (0 < b) ? b : 0
float MinOf(float a, float b) { return (x87::IsNaN(a) || x87::IsNaN(b) || a < b) ? a : b; }

// _ftol: truncation to a 64-bit integer, the low word kept.
std::uint32_t Ftol(float v) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(v)); }

} // namespace

void CWorldClient::ObjectsInRange(const float* point, float radius, void* out) {
    float maxX = At<float>(terrain_, 0x10C) - 1.0f;
    float maxY = At<float>(terrain_, 0x110) - 1.0f;
    float y1 = MinOf(MaxZero(point[1] + radius), maxY);
    float x1 = MinOf(MaxZero(point[0] + radius), maxX);
    float y0 = MaxZero(MinOf(MaxZero(point[1] - radius), maxY));
    float x0 = MaxZero(MinOf(MaxZero(point[0] - radius), maxX));
    std::uint32_t& firstX = At<std::uint32_t>(this, 0x138);
    std::uint32_t& firstY = At<std::uint32_t>(this, 0x13C);
    std::uint32_t& lastX = At<std::uint32_t>(this, 0x140);
    std::uint32_t& lastY = At<std::uint32_t>(this, 0x144);
    std::uint32_t& x = At<std::uint32_t>(this, 0x148);
    std::uint32_t& y = At<std::uint32_t>(this, 0x14C);
    firstX = x = Ftol(x0) >> 4;
    firstY = y = Ftol(y0) >> 4;
    lastX = Ftol(x1) >> 4;
    lastY = Ftol(y1) >> 4;

    SectorGrid& grid = GridAt(this, 0x120);
    PlainList* list = static_cast<PlainList*>(out);
    for (SectorListNode** cell = &grid.rows_[y][x]; cell;) {
        for (SectorListNode* node = (*cell)->next; node != *cell; node = node->next) {
            CEngineObject* object = node->value;
            float pz = point[2];
            float ox = At<float>(object, 0x1C) + At<float>(object, 0x128);
            float oy = At<float>(object, 0x20) + At<float>(object, 0x12C);
            double reach = Hypot(At<float>(object, 0x144), At<float>(object, 0x148)) + radius;
            float dx = ox - point[0];
            float dy = oy - point[1];
            float dz = pz - point[2];
            double distance = (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
            double limit = reach * reach;
            // FCOMPP + TEST AH,1: skipped when the limit is below (or unordered).
            bool unordered = x87::IsNaN(static_cast<float>(distance)) || x87::IsNaN(static_cast<float>(limit));
            if (!unordered && !(limit < distance)) {
                SectorListNode* end;
                SectorListNode* inserted;
                list->Insert(&inserted, *list->End(&end), object);
            }
        }
        if (++x > lastX) {
            x = firstX;
            if (++y > lastY) return;
        }
        cell = &grid.rows_[y][x];
    }
}

// ---- Platforms (00560650, 00560820) --------------------------------------------------------------

namespace {
// The record list's node allocation [00573000, on the set's list at +0x1C: (next, prev)].
__declspec(naked) std::uint8_t* __fastcall PlatformNewNode(void*, void*, void*, void*) EI_ORIGINAL(0x00573000)
constexpr float kSteepest = 0.8f;   // 0073CCA8: the smallest normal z of a face added
} // namespace

int PlatformSet::AddFace(const float* a, const float* b, const float* c, const float* normal, Platform** out) {
    *out = nullptr;
    // FCOMP + TEST AH,1: steeper faces (and a NaN z) are left out.
    if (x87::IsNaN(normal[2]) || normal[2] < kSteepest) return 0;
    std::uint8_t* list = reinterpret_cast<std::uint8_t*>(this) + 0x1C;
    PlatformNode* last = head;
    std::uint8_t* node = PlatformNewNode(list, nullptr, nullptr, last);
    // (The original first copies 0x4C bytes of uninitialised stack over the record; every field is
    // written below or by the caller.)
    if (last)
        At<std::uint8_t*>(last, 4) = node;
    else
        At<std::uint8_t*>(this, 0x24) = node;
    head = reinterpret_cast<PlatformNode*>(node);
    Platform* record = reinterpret_cast<Platform*>(node + 8);
    *out = record;
    float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
    float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    float lengthU = std::sqrt((uz * uz + uy * uy) + ux * ux);
    record->sizeU = lengthU;
    float lengthV = std::sqrt((vz * vz + vy * vy) + vx * vx);
    record->sizeV = lengthV;
    std::memcpy(record->normal, normal, 12);
    std::memcpy(record->origin, a, 12);
    record->axisU[0] = ux / lengthU;
    record->axisU[1] = uy / lengthU;
    record->axisU[2] = uz / lengthU;
    record->axisV[0] = vx / lengthV;
    record->axisV[1] = vy / lengthV;
    record->axisV[2] = vz / lengthV;
    return 1;
}

void PlatformSet::AddBox(int id, const std::uint8_t* item, const float* position, const float* rotation) {
    const float* offset = reinterpret_cast<const float*>(item + 4);
    const float* low = reinterpret_cast<const float*>(item + 0x14);
    const float* high = reinterpret_cast<const float*>(item + 0x20);
    const float* pivot = reinterpret_cast<const float*>(item + 0x2C);

    // The rotation matrix of the quaternion (w, x, y, z), as the original builds it.
    float q0 = rotation[0], q1 = rotation[1], q2 = rotation[2], q3 = rotation[3];
    float t1 = q1 + q1, t2 = q2 + q2, t3 = q3 + q3;
    float t1q0 = t1 * q0, t2q0 = t2 * q0, t3q0 = t3 * q0;
    float t1q1 = t1 * q1, t2q1 = t2 * q1, t3q1 = t3 * q1;
    float t2q2 = t2 * q2, t3q2 = t3 * q2, t3q3 = t3 * q3;
    MatrixRows m{};
    m.m[0] = 1.0f - (t3q3 + t2q2);
    m.m[1] = t2q1 - t3q0;
    m.m[2] = t3q1 + t2q0;
    m.m[4] = t2q1 + t3q0;
    m.m[5] = 1.0f - (t3q3 + t1q1);
    m.m[6] = t3q2 - t1q0;
    m.m[8] = t3q1 - t2q0;
    m.m[9] = t3q2 + t1q0;
    m.m[10] = 1.0f - (t2q2 + t1q1);

    // The box's corners (two of them repeated, as the faces walk them), moved by the pivot and the
    // offset, rotated and placed.
    float corners[8][3] = {{high[0], low[1], low[2]},  {low[0], low[1], low[2]},   {low[0], high[1], low[2]},
                           {low[0], high[1], high[2]}, {high[0], high[1], high[2]}, {high[0], low[1], high[2]},
                           {high[0], low[1], low[2]},  {low[0], low[1], low[2]}};
    for (auto& corner : corners) {
        float x = (pivot[0] + corner[0]) + offset[0];
        float y = (pivot[1] + corner[1]) + offset[1];
        float z = (pivot[2] + corner[2]) + offset[2];
        float rx = (m.m[2] * z + m.m[1] * y) + m.m[0] * x;
        float ry = (m.m[6] * z + m.m[5] * y) + m.m[4] * x;
        float rz = (m.m[10] * z + m.m[9] * y) + m.m[8] * x;
        corner[0] = position[0] + rx;
        corner[1] = position[1] + ry;
        corner[2] = rz + position[2];
    }
    float normals[6][3] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
    for (auto& n : normals) {
        float nx = n[0], ny = n[1], nz = n[2];
        n[0] = (m.m[2] * nz + m.m[0] * nx) + m.m[1] * ny;
        n[1] = (m.m[6] * nz + m.m[4] * nx) + m.m[5] * ny;
        n[2] = (m.m[10] * nz + m.m[8] * nx) + m.m[9] * ny;
    }
    for (int i = 0; i < 6; ++i) {
        Platform* record;
        if (!AddFace(corners[i + 1], corners[i], corners[i + 2], normals[i], &record)) continue;
        At<int>(record, 0) = id;
        Vector3 moved = {pivot[0] + offset[0], pivot[1] + offset[1], pivot[2] + offset[2]};
        Vector3 rotated;
        m.Transform(&rotated, &moved);
        record->x = rotated.x + position[0];
        record->y = rotated.y + position[1];
        At<float>(record, 0xC) = rotated.z + position[2];
        At<std::uint32_t>(record, 0x10) = *reinterpret_cast<const std::uint32_t*>(item + 0x10);
    }
}

// ---- Teardown (00563290) -------------------------------------------------------------------------

namespace {

__declspec(naked) void __fastcall CameraSetTarget(void*, void*, void*) EI_ORIGINAL(0x0040D3B0)
__declspec(naked) void __fastcall Destroy1B4(void*) EI_ORIGINAL(0x005E17E0)
__declspec(naked) void __fastcall Destroy1AC(void*) EI_ORIGINAL(0x005636A0)
__declspec(naked) void __fastcall Teardown563190(void*) EI_ORIGINAL(0x00563190)
__declspec(naked) void __fastcall ClearA0(void*, void*, int, int) EI_ORIGINAL(0x00570000)
// The lists' element destructors [0056D080 (node + 8); 00575560 / 00575580 (node + 8, flags)] and
// node releases [0056FB80 / 00498C40 / 0056FB00 (node, count)].
__declspec(naked) void __fastcall Element1B8Destroy(void*) EI_ORIGINAL(0x0056D080)
__declspec(naked) void __fastcall Node1B8Free(void*, int) EI_ORIGINAL(0x0056FB80)
__declspec(naked) void __fastcall Element1BCDestroy(void*, void*, int) EI_ORIGINAL(0x00575560)
__declspec(naked) void __fastcall Element1C0Destroy(void*, void*, int) EI_ORIGINAL(0x00575580)
__declspec(naked) void __fastcall Node7BCFree(void*, int) EI_ORIGINAL(0x00498C40)
__declspec(naked) void __fastcall NodeB8Free(void*, int) EI_ORIGINAL(0x0056FB00)
// for_each(first, last, functor) releasing every value [00571780, RET 0xC].
__declspec(naked) char __stdcall ForEachRelease(SectorListNode*, SectorListNode*, char) EI_ORIGINAL(0x00571780)
// CList RemoveAll [0056FDA0]; the counted handle's Shutdown [00417DF0], Release [0040A3A0], Share
// [00418BE0]; the base world's teardown [0055F8E0].
__declspec(naked) void __fastcall MfcRemoveAll(void*) EI_ORIGINAL(0x0056FDA0)
__declspec(naked) void __fastcall RefShutdown(void*) EI_ORIGINAL(0x00417DF0)
void __fastcall RefRelease(void* a0) { RefObjectRelease(a0); }
__declspec(naked) void __fastcall WorldTeardown(void*) EI_ORIGINAL(0x0055F8E0)

constexpr int kSlotDeletingDestructor = 0;
constexpr int kSlotDestroy = 3;

// The list's nodes walked from the head's next, each given to `free` after the next is read; the
// list is left empty.
template <typename F>
void EmptyList(PlainList& list, F free) {
    for (SectorListNode* node = list.head->next; node != list.head;) {
        SectorListNode* next = node->next;
        free(node);
        node = next;
    }
    list.head->next = list.head;
    list.head->prev = list.head;
}

void DeleteOwned(void*& slot) {
    if (slot) {
        ei::VCall<void>(slot, kSlotDeletingDestructor, 1);
        slot = nullptr;
    }
}

void DropAndDelete(void*& slot) {
    if (slot) {
        --static_cast<CEngineObject*>(slot)->refCount_;
        if (slot) ei::VCall<void>(slot, kSlotDeletingDestructor, 1);
        slot = nullptr;
    }
}

} // namespace

void CWorldClient::Teardown() {
    CameraSetTarget(reinterpret_cast<void*>(0x0079B2A8), nullptr, nullptr);
    if (void* p = At<void*>(this, 0x1B4)) {
        Destroy1B4(p);
        gamecrt::operator_delete(p);
        At<void*>(this, 0x1B4) = nullptr;
    }
    if (void* p = At<void*>(this, 0x1AC)) {
        Destroy1AC(p);
        gamecrt::operator_delete(p);
        At<void*>(this, 0x1AC) = nullptr;
    }
    DeleteOwned(At<void*>(this, 0x1B0));
    Teardown563190(this);
    ClearA0(&At<std::uint8_t>(this, 0xA0), nullptr, 0, -1);
    EmptyList(ListAt(this, 0x1B8), [](SectorListNode* node) {
        Element1B8Destroy(&node->value);
        Node1B8Free(node, 1);
    });
    EmptyList(ListAt(this, 0x1BC), [](SectorListNode* node) {
        Element1BCDestroy(&node->value, nullptr, 0);
        if (node) stlp::deallocate(node, 0x9C);
    });
    EmptyList(ListAt(this, 0x1C0), [](SectorListNode* node) {
        Element1C0Destroy(&node->value, nullptr, 0);
        if (node) stlp::deallocate(node, 0x94);
    });
    ei::VCall<void>(&At<std::uint8_t>(this, 0x6F4), kSlotDestroy);
    ei::VCall<void>(&platforms_, kSlotDestroy);
    MfcRemoveAll(&At<std::uint8_t>(this, 0x1F0));
    SectorGrid& grid = GridAt(this, 0x120);
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            reinterpret_cast<CellList*>(&grid.rows_[y][x])->Clear();
    EmptyList(ListAt(this, 0x7BC), [](SectorListNode* node) { Node7BCFree(node, 1); });
    EmptyList(ListAt(this, 0xB4), [](SectorListNode* node) {
        CountedRelease(&node->value, nullptr, 0);
        NodeFree12Fast(node, 1);
    });
    PlainList& objects = ListAt(this, 0xB8);
    ForEachRelease(objects.head->next, objects.head, 0);
    EmptyList(objects, [](SectorListNode* node) { NodeB8Free(node, 1); });
    if (session_) {
        ei::VCall<void>(session_, kSlotRelease);
        session_ = nullptr;
    }
    if (void* handle = At<void*>(this, 0x1C8)) {
        if (At<std::uint16_t>(handle, 4) != 1) {
            At<std::uint8_t>(handle, 7) |= 0x80;
            RefShutdown(handle);
        }
        RefRelease(At<void*>(this, 0x1C8));
        RefShare(&At<void*>(this, 0x1C8), nullptr, nullptr);
    }
    WorldTeardown(this);
    DeleteOwned(At<void*>(this, 0x1A0));
    DeleteOwned(At<void*>(this, 0x1A4));
    DropAndDelete(At<void*>(this, 0x19C));
    DropAndDelete(At<void*>(this, 0x180));
    DropAndDelete(At<void*>(this, 0x190));
    DropAndDelete(At<void*>(this, 0x188));
    DropAndDelete(At<void*>(this, 0x18C));
    DeleteOwned(At<void*>(this, 0x1A8));
    DropAndDelete(At<void*>(this, 0x1CC));
    DropAndDelete(At<void*>(this, 0x1D0));
}

// ---- Set up (00561E50) ---------------------------------------------------------------------------

int __fastcall WorldCreateManagers(std::uint8_t* world);   // 0055EE40 (world_create.cpp)

namespace {

// MFC: operator+(const CString&, LPCTSTR) [00700C05, __stdcall, the result constructed in `out`],
// CString::operator=(const CString&) [00700AA9], operator=(LPCTSTR) [00700AF9], ~CString [00700970].
__declspec(naked) void* __stdcall CStringConcat(void* /*out*/, const void* /*left*/, const char*) EI_ORIGINAL(0x00700C05)
void __fastcall CStringAssign(void* a0, void*, const void* a2) { CStringAssignOriginal(a0, nullptr, a2); }
inline void __fastcall CStringAssignText(void* s, void*, const char* t) { CStringAssignTextOriginal(s, nullptr, t); }
// The lights table loader [00563930 (file name)], the text resources [004DEDB0 on +0x1D0 (path)].
__declspec(naked) void __fastcall LoadLights(void*, void*, const char*) EI_ORIGINAL(0x00563930)
__declspec(naked) void __fastcall LoadTexts(void*, void*, const char*) EI_ORIGINAL(0x004DEDB0)
// Constructors (thiscall; some take a block size or count).
__declspec(naked) void* __fastcall Construct6B6040(void*) EI_ORIGINAL(0x006B6040)
__declspec(naked) void __fastcall Construct562A80(void*) EI_ORIGINAL(0x00562A80)
__declspec(naked) void __fastcall Construct4DED20(void*) EI_ORIGINAL(0x004DED20)
__declspec(naked) void __fastcall TransformObjectBase(void*) EI_ORIGINAL(0x00453400)
__declspec(naked) void __fastcall Construct570E10(void*) EI_ORIGINAL(0x00570E10)
__declspec(naked) void __fastcall Construct570E40(void*) EI_ORIGINAL(0x00570E40)
__declspec(naked) void __fastcall Construct56FD30(void*) EI_ORIGINAL(0x0056FD30)
__declspec(naked) void __fastcall Construct6721D0(void*) EI_ORIGINAL(0x006721D0)
__declspec(naked) void __fastcall Construct570E70(void*) EI_ORIGINAL(0x00570E70)
__declspec(naked) void __fastcall Construct570EA0(void*, void*, int) EI_ORIGINAL(0x00570EA0)
__declspec(naked) void __fastcall Construct571050(void*) EI_ORIGINAL(0x00571050)
__declspec(naked) void __fastcall Construct570DE0(void*) EI_ORIGINAL(0x00570DE0)
__declspec(naked) void __fastcall Construct5711C0(void*) EI_ORIGINAL(0x005711C0)
__declspec(naked) void __fastcall Construct6C5620(void*) EI_ORIGINAL(0x006C5620)
__declspec(naked) void* __fastcall Construct42D690(void*) EI_ORIGINAL(0x0042D690)
__declspec(naked) void* __fastcall Construct691140(void*) EI_ORIGINAL(0x00691140)
__declspec(naked) void __fastcall Attach4031B0(void*, void*, void*) EI_ORIGINAL(0x004031B0)
__declspec(naked) void* __fastcall Construct68E3F0(void*) EI_ORIGINAL(0x0068E3F0)
__declspec(naked) void* __fastcall Construct68D850(void*) EI_ORIGINAL(0x0068D850)
__declspec(naked) void __fastcall ManagerBase(void*) EI_ORIGINAL(0x00402B20)
__declspec(naked) void __fastcall Construct56FD40(void*, void*, int) EI_ORIGINAL(0x0056FD40)
__declspec(naked) void __fastcall Construct5725E0(void*) EI_ORIGINAL(0x005725E0)
__declspec(naked) void __fastcall Construct4981A0(void*) EI_ORIGINAL(0x004981A0)
__declspec(naked) void* __fastcall SessionFor(void*) EI_ORIGINAL(0x0066C180)
__declspec(naked) void __fastcall Construct419720(void*) EI_ORIGINAL(0x00419720)
__declspec(naked) void __fastcall Construct5715D0(void*, void*, int) EI_ORIGINAL(0x005715D0)
__declspec(naked) void* __fastcall Construct6B4350(void*) EI_ORIGINAL(0x006B4350)
__declspec(naked) void __fastcall Construct4E3C20(void*) EI_ORIGINAL(0x004E3C20)
// The player list walk (begin [00404920: list], next [00404960]).
__declspec(naked) CEngineObject* __fastcall PlayersBegin(void*, void*, void*) EI_ORIGINAL(0x00404920)
__declspec(naked) CEngineObject* __fastcall PlayersNext(void*) EI_ORIGINAL(0x00404960)
// vector<0x18 entry>::_M_allocate_and_copy [00573520: count, first, last].
__declspec(naked) std::uint8_t* __fastcall EntriesAllocateCopy(void*, void*, int, std::uint8_t*, std::uint8_t*)
    EI_ORIGINAL(0x00573520)
// The sector grids: resize [00572450 / 005724F0 (width, height)], the cells' vector deleting
// destructor [00572B30 / 00572C50 (flags)], the allocation [00572820 / 005728D0].
__declspec(naked) void __fastcall GridResize(void*, void*, int, int) EI_ORIGINAL(0x00572450)
__declspec(naked) void __fastcall GridResize2(void*, void*, int, int) EI_ORIGINAL(0x005724F0)
__declspec(naked) void __fastcall CellsDelete(void*, void*, int) EI_ORIGINAL(0x00572B30)
__declspec(naked) void __fastcall CellsDelete2(void*, void*, int) EI_ORIGINAL(0x00572C50)
__declspec(naked) void __fastcall GridAllocate(void*) EI_ORIGINAL(0x00572820)
__declspec(naked) void __fastcall GridAllocate2(void*) EI_ORIGINAL(0x005728D0)

constexpr int kSlotCreateSelf = 8 / 4;     // owned objects: ()
constexpr int kSlotAttachWorld = 0x34 / 4; // managers: (world)
constexpr int kSlotPlayers = 0x58 / 4;     // the world: () -> the player list

const void* const kDataPath = reinterpret_cast<const void*>(0x007AF460);   // CString
const void* const kTextPath = reinterpret_cast<const void*>(0x007AF45C);   // CString
std::uintptr_t EmptyString() { return *reinterpret_cast<const std::uintptr_t*>(0x00796E48); }

std::uint8_t* New(std::size_t size) { return static_cast<std::uint8_t*>(gamecrt::operator_new(size)); }

// A heap object made by its constructor (null without memory), stored and referenced (the
// original does not test the pointer before counting).
CEngineObject* MakeCounted(std::size_t size, void* (__fastcall* construct)(void*)) {
    std::uint8_t* memory = New(size);
    auto* object = static_cast<CEngineObject*>(memory ? construct(memory) : nullptr);
    ++object->refCount_;
    return object;
}

// The grid set to 4 x 4 cells, reallocated unless its storage already has that size.
void GridToFour(SectorGrid& grid, void (__fastcall* resize)(void*, void*, int, int),
                void (__fastcall* deleteCells)(void*, void*, int), void (__fastcall* allocate)(void*)) {
    resize(&grid, nullptr, 1, 1);
    grid.width_ = 4;
    grid.height_ = 4;
    if (grid.unknown08_ != 4 || grid.unknown0C_ != 4) {
        if (grid.cells_) deleteCells(grid.cells_, nullptr, 3);
        gamecrt::operator_delete(grid.rows_);
        grid.unknown08_ = 4;
        grid.unknown0C_ = 4;
        allocate(&grid);
    }
}

} // namespace

int CWorldClient::Setup() {
    WorldCreateManagers(reinterpret_cast<std::uint8_t*>(this));
    void* path;
    CStringConcat(&path, kDataPath, reinterpret_cast<const char*>(0x0078FFE4));   // "textures\"
    CStringAssign(&At<std::uint8_t>(this, 0x224), nullptr, &path);
    CStringDestroy(&path);
    LoadLights(this, nullptr, reinterpret_cast<const char*>(0x0078FFD4));          // "LightsGipat.ini"
    At<int>(this, 0x6DC) = 0;
    At<CEngineObject*>(this, 0x1CC) = MakeCounted(0x8C0, Construct6B6040);

    std::uint8_t* texts = New(0x2C);
    if (texts) {
        At<std::uintptr_t>(texts, 0) = 0x0073B8E0;
        At<int>(texts, 4) = 0;
        At<int>(texts, 0xC) = -1;
        At<int>(texts, 0x18) = 0;
        At<int>(texts, 0x10) = 0;
        At<int>(texts, 8) = 0;
        At<int>(texts, 0x14) = 0;
        Construct562A80(texts + 0x1C);
        At<std::uintptr_t>(texts, 0) = 0x0073F6F0;
        Construct4DED20(texts);
    }
    At<std::uint8_t*>(this, 0x1D0) = texts;
    ++At<int>(texts, 4);

    std::uint8_t* terrain = New(0xAFC);
    if (terrain) {
        TransformObjectBase(terrain);
        At<int>(terrain, 0xD8) = 0;
        At<int>(terrain, 0xE4) = 0;
        At<int>(terrain, 0xE0) = 0;
        At<int>(terrain, 0xDC) = 0;
        Construct570E10(terrain + 0xE8);
        At<std::uintptr_t>(terrain, 0xF8) = EmptyString();
        At<std::uintptr_t>(terrain, 0xFC) = EmptyString();
        Construct570E40(terrain + 0x12C);
        Construct56FD30(terrain + 0x13C);
        Construct56FD30(terrain + 0x14C);
        Construct6721D0(terrain + 0x15C);
        Construct570E70(terrain + 0x1E8);
        Construct570EA0(terrain + 0x1FC, nullptr, 10);
        Construct571050(terrain + 0x25C);
        Construct570DE0(terrain + 0x290);
        Construct56FD30(terrain + 0xAA8);
        Construct56FD30(terrain + 0xAB8);
        At<std::uintptr_t>(terrain, 0xAE0) = EmptyString();
        Construct5711C0(terrain + 0xAE8);
        At<std::uintptr_t>(terrain, 0) = 0x0073F6A8;
        Construct6C5620(terrain);
    }
    At<std::uint8_t*>(this, 0x180) = terrain;
    ++At<int>(terrain, 4);
    CStringAssignText(&At<std::uint8_t>(this, 0x184), nullptr, reinterpret_cast<const char*>(0x0078FFCC));   // "gipath"

    CEngineObject* object = MakeCounted(0xB0, Construct42D690);
    At<CEngineObject*>(this, 0x44) = object;
    ei::VCall<void>(object, kSlotAttachWorld, this);
    object = MakeCounted(0xF0, Construct691140);
    At<CEngineObject*>(this, 0x18C) = object;
    Attach4031B0(object, nullptr, this);
    object = MakeCounted(0xD0, Construct68E3F0);
    At<CEngineObject*>(this, 0x188) = object;
    ei::VCall<void>(object, kSlotAttachWorld, this);
    object = MakeCounted(0xD8, Construct68D850);
    At<CEngineObject*>(this, 0x190) = object;
    ei::VCall<void>(object, kSlotAttachWorld, this);

    std::uint8_t* manager = New(0xE8);
    if (manager) {
        ManagerBase(manager);
        Construct56FD40(manager + 0xB0, nullptr, 10);
        Construct5725E0(manager + 0xCC);
        At<int>(manager, 0xE0) = 0;
        At<std::uintptr_t>(manager, 0) = 0x0073F644;
        Construct4981A0(manager);
    }
    At<std::uint8_t*>(this, 0x19C) = manager;
    ++At<int>(manager, 4);
    ei::VCall<void>(manager, kSlotAttachWorld, this);

    session_ = static_cast<std::uint8_t*>(SessionFor(players_));

    std::uint8_t* handle = New(8);
    At<int>(handle, 4) = 0;   // (written before the pointer is tested)
    void* created = nullptr;
    if (handle) {
        Construct419720(handle);
        At<std::uintptr_t>(handle, 0) = 0x0073F628;
        created = handle;
    }
    void*& slot = At<void*>(this, 0x1C8);
    if (created != slot) {
        if (void* old = slot) {
            if (At<std::uint16_t>(old, 4) != 1) {
                At<std::uint8_t>(old, 7) |= 0x80;
                RefShutdown(old);
            }
            RefRelease(slot);
        }
        RefShare(&slot, nullptr, created);
    }
    ei::VCall<void>(At<void*>(this, 0x180), kSlotCreateSelf);
    ei::VCall<void>(At<void*>(this, 0x1CC), kSlotCreateSelf);
    ei::VCall<void>(At<void*>(this, 0x1D0), kSlotCreateSelf);
    CStringConcat(&path, kTextPath, reinterpret_cast<const char*>(0x0078FFC0));   // "texts.res"
    LoadTexts(At<void*>(this, 0x1D0), nullptr, static_cast<const char*>(path));
    CStringDestroy(&path);
    CStringConcat(&path, kTextPath, reinterpret_cast<const char*>(0x0078FFB0));   // "textsLmp.res"
    LoadTexts(At<void*>(this, 0x1D0), nullptr, static_cast<const char*>(path));
    CStringDestroy(&path);

    std::uint8_t* lists = New(0xCC);
    if (lists) {
        ManagerBase(lists);
        At<std::uintptr_t>(lists, 0xB0) = 0x0073F61C;
        At<int>(lists, 0xBC) = 0;
        At<int>(lists, 0xC0) = 0;
        At<int>(lists, 0xB8) = 0;
        At<int>(lists, 0xB4) = 0;
        At<int>(lists, 0xC4) = 0;
        At<int>(lists, 0xC8) = 10;
        At<std::uintptr_t>(lists, 0) = 0x0073F5B8;
    }
    At<std::uint8_t*>(this, 0x1A0) = lists;
    ei::VCall<void>(lists, kSlotAttachWorld, this);
    std::uint8_t* arrays = New(0xD0);
    if (arrays) {
        ManagerBase(arrays);
        At<int>(arrays, 0xB0) = 0;
        Construct5715D0(arrays + 0xB4, nullptr, 10);
        At<std::uintptr_t>(arrays, 0) = 0x0073F554;
    }
    At<std::uint8_t*>(this, 0x1A4) = arrays;
    ei::VCall<void>(arrays, kSlotAttachWorld, this);
    std::uint8_t* memory = New(0x58);
    void* owned = memory ? Construct6B4350(memory) : nullptr;
    At<void*>(this, 0x1A8) = owned;
    ei::VCall<void>(owned, kSlotCreateSelf);

    Construct4E3C20(&At<std::uint8_t>(this, 0x6F4));
    At<CWorldClient*>(this, 0x6F4 + 0x20) = this;
    ei::VCall<void>(&platforms_, kSlotCreateSelf);
    if (*reinterpret_cast<const int*>(0x0079BD0C) > 0)
        flags_ |= 0x200;
    else
        flags_ &= ~0x200u;
    CStringConcat(&path, kDataPath, reinterpret_cast<const char*>(0x0078FF9C));   // "wincurs\Df0000.cur"
    At<HCURSOR>(this, 0x7D0) = LoadCursorFromFileA(static_cast<const char*>(path));
    CStringDestroy(&path);

    if (ei::VCall<void*>(this, kSlotPlayers)) {
        void** walk = reinterpret_cast<void**>(New(8));
        if (walk) *walk = nullptr;
        for (CEngineObject* player = PlayersBegin(walk, nullptr, ei::VCall<void*>(this, kSlotPlayers)); player;
             player = PlayersNext(walk))
            ++player->refCount_;
        if (walk) {
            if (auto* held = static_cast<CEngineObject*>(*walk)) {
                --held->refCount_;
                *walk = nullptr;
            }
            gamecrt::operator_delete(walk);
        }
    }

    HitVector& entries = At<HitVector>(this, 0xF4);
    if (static_cast<std::uint32_t>((entries.eos - entries.start) / 0x18) < 400) {
        int count = static_cast<int>((entries.finish - entries.start) / 0x18);
        std::uint8_t* block;
        if (entries.start) {
            block = EntriesAllocateCopy(&entries, nullptr, 400, entries.start, entries.finish);
            if (std::uint8_t* old = entries.start)
                stlp::deallocate(old, static_cast<std::size_t>((entries.eos - old) / 0x18) * 0x18);
        } else {
            block = New(0x2580);
        }
        entries.start = block;
        entries.finish = block + count * 0x18;
        entries.eos = block + 0x2580;
    }
    At<int>(this, 0x84) = -100000;
    At<int>(this, 0x6E0) = 0;
    At<int>(this, 0x6E4) = 0;
    GridToFour(GridAt(this, 0x150), GridResize2, CellsDelete2, GridAllocate2);
    GridToFour(GridAt(this, 0x120), GridResize, CellsDelete, GridAllocate);
    At<int>(this, 0x6EC) = 0;
    At<int>(this, 0x1C4) = 0;
    return 1;
}

EI_REPLACE_METHOD(0x00561E50, CWorldClient::Setup);
EI_REPLACE_METHOD(0x00560650, PlatformSet::AddFace);
EI_REPLACE_METHOD(0x00560820, PlatformSet::AddBox);
EI_REPLACE_METHOD(0x00563290, CWorldClient::Teardown);
EI_REPLACE_METHOD(0x0056DAF0, CWorldClient::ObjectsInRange);
EI_REPLACE_METHOD(0x00566F70, CWorldClient::CreatePartyUnits);
EI_REPLACE_METHOD(0x005647E0, CWorldClient::AddObject);
EI_REPLACE_METHOD(0x00564BE0, CWorldClient::RemoveObject);
EI_REPLACE_METHOD(0x00564E70, CWorldClient::ClearMaps);
EI_REPLACE(0x005726B0, ReleaseIfIdentified);
EI_REPLACE_METHOD(0x00565790, CWorldClient::PickAt);
EI_REPLACE_METHOD(0x005659D0, CWorldClient::PickAt2);
EI_REPLACE_METHOD(0x00570860, SectorGrid::Move);
EI_REPLACE_METHOD(0x00570BB0, SectorGrid::Move2);
EI_REPLACE(0x00571D50, MarkWithinRadius);
EI_REPLACE(0x00571800, CollectWindowsAt);
EI_REPLACE(0x005718B0, CollectWindowsAt2);
EI_REPLACE(0x0056FC60, PendingMapLookup);
EI_REPLACE(0x0056E400, RecordAt50);
EI_REPLACE(0x0056E440, RecordAtB4);
