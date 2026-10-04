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
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr int kSlotRelease = 4;

// _hypot (the game's CRT), through its address.
__declspec(naked) double __cdecl ClientHypot(double, double) EI_ORIGINAL(0x006FD026)

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
void AddRef(CEngineObject* object) {
    if (object) ++object->refCount_;
}
void Release(CEngineObject* object) {
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
        AddRef(object);
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
        Release(object);
    }
    std::memcpy(stored, position, 12);
    if (newX < grid->width_ && newY < grid->height_) {
        // push_back of a counted copy.
        CEngineObject* value = object;
        AddRef(value);
        List* list = reinterpret_cast<List*>(&grid->rows_[newY][newX]);
        SectorListNode* end;
        SectorListNode* inserted;
        list->Insert(&inserted, *list->End(&end), value);
        Release(value);
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
        double distance = ClientHypot(dx, dy);
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

EI_REPLACE_METHOD(0x00570860, SectorGrid::Move);
EI_REPLACE_METHOD(0x00570BB0, SectorGrid::Move2);
EI_REPLACE(0x00571D50, MarkWithinRadius);
EI_REPLACE(0x00571800, CollectWindowsAt);
EI_REPLACE(0x005718B0, CollectWindowsAt2);
EI_REPLACE(0x0056FC60, PendingMapLookup);
EI_REPLACE(0x0056E400, RecordAt50);
EI_REPLACE(0x0056E440, RecordAtB4);
