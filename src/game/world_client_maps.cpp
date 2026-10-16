// The client world's object maps (0056E000-00574000 helpers and the CWorldClient methods that keep
// the maps). See docs/notes/world.md ("Client maps").
//
// The client files every object in a sector grid (+0x120: STLport lists of counted pointers, one
// per 16x16 cell) and the second grid at +0x150; most of 0056E000-00574000 are the STLport and MFC
// instantiations behind those maps (config/library.csv). Rewritten here: the grids' Move (two
// instantiations), the window hit collectors, the radius marker, the pending-map lookup and the
// bounds-checked record accessors.
#include "ground_print.h"
#include "item.h"
#include "map_object_shadow.h"
#include "world.h"
#include "world_data.h"

#include "sky_object.h"   // before windows.h (GetClassName)

#include "ai_map.h"
#include "base_manager.h"
#include "camera.h"
#include "console.h"
#include "core_streams.h"
#include "effects.h"
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "model.h"
#include "sound.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"
#include "world_effect.h"
#include "originals_engine.h"
#include "mod_folders.h"
#include "world_map_files.h"
#include "world_glue.h"
#include "unit_looks.h"
#include "bink_video.h"
#include "object_tables.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp_list_refs.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "player_client.h"
#include "ui_flying_hit_points.h"
#include "unit_client_models.h"
#include "unit_create.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// The objects the maps file are map objects (their position at +0x1C).
CMapObject* MapObjectOf(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// The cell index of a coordinate: FISTP (round to nearest), then an unsigned shift by 4, so a
// negative coordinate gives a huge index that fails the bounds tests.
std::uint32_t CellOf(float coordinate) { return static_cast<std::uint32_t>(x87::fistp(coordinate)) >> 4; }

// The list operations of the two grids' instantiations (thiscall on the cell's list):
// erase(out iterator, node), end(out iterator) and insert(out iterator, position, value).
struct GridOpsA {
    static SectorListNode** __fastcall Erase(SectorList*, void*, SectorListNode** out, SectorListNode* node);   // 00572270
    static SectorListNode** __fastcall End(SectorList*, void*, SectorListNode** out);                          // 0056DEC0
    static SectorListNode** __fastcall Insert(SectorList*, void*, SectorListNode** out, SectorListNode* position,
                                              CEngineObject* const& value);                                    // 0056E8A0
};
__declspec(naked) SectorListNode** __fastcall GridOpsA::Erase(SectorList*, void*, SectorListNode**, SectorListNode*)
    EI_ORIGINAL(0x00572270)
__declspec(naked) SectorListNode** __fastcall GridOpsA::End(SectorList*, void*, SectorListNode**) EI_ORIGINAL(0x0056DEC0)
SectorListNode** __fastcall GridOpsA::Insert(SectorList* list, void*, SectorListNode** out, SectorListNode* position,
                                             CEngineObject* const& value) {
    return reinterpret_cast<SectorListNode**>(ListInsert56E8A0Original(list, nullptr, reinterpret_cast<void**>(out), position, &value));
}

struct GridOpsB {
    static SectorListNode** __fastcall Erase(SectorList*, void*, SectorListNode** out, SectorListNode* node);   // 005755E0
    static SectorListNode** __fastcall End(SectorList*, void*, SectorListNode** out);                          // 00572AF0
    static SectorListNode** __fastcall Insert(SectorList*, void*, SectorListNode** out, SectorListNode* position,
                                              CEngineObject* const& value);                                    // 00572B00
};
__declspec(naked) SectorListNode** __fastcall GridOpsB::Erase(SectorList*, void*, SectorListNode**, SectorListNode*)
    EI_ORIGINAL(0x005755E0)
__declspec(naked) SectorListNode** __fastcall GridOpsB::End(SectorList*, void*, SectorListNode**) EI_ORIGINAL(0x00572AF0)
__declspec(naked) SectorListNode** __fastcall GridOpsB::Insert(SectorList*, void*, SectorListNode**, SectorListNode*,
                                                               CEngineObject* const&) EI_ORIGINAL(0x00572B00)

// The counted pointer's copy and destruction, as the original inlines them.
void HoldRef(CEngineObject* object) {
    if (object) ++object->refCount_;
}
void DropRef(CEngineObject* object) {
    if (object) object->Release();
}

template <typename Ops>
void MoveInGrid(SectorGrid* grid, CEngineObject* object, const float* position) {
    float* stored = &MapObjectOf(object)->position.x;   // x, y, z
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
        SectorList* list = &grid->rows_[oldY][oldX];
        SectorListNode* head = list->head;
        SectorListNode* node = head->next;
        while (node != head) {
            SectorListNode* next = node->next;
            if (node->data == object) {
                SectorListNode* ignored;
                Ops::Erase(list, nullptr, &ignored, node);
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
        SectorList* list = &grid->rows_[newY][newX];
        SectorListNode* end;
        SectorListNode* inserted;
        Ops::Insert(list, nullptr, &inserted, *Ops::End(list, nullptr, &end), value);
        DropRef(value);
    }
}

} // namespace

// ---- Sector grids --------------------------------------------------------------------------------

void SectorGrid::Move(CEngineObject* object, const float* position) {
    MoveInGrid<GridOpsA>(this, object, position);
}

void SectorGrid::Move2(CEngineObject* object, const float* position) {
    MoveInGrid<GridOpsB>(this, object, position);
}

// ---- Functors ------------------------------------------------------------------------------------

float* __fastcall MarkWithinRadius(float* out, void*, std::uint8_t* first, std::uint8_t* last, float x, float y,
                                   float scale) {
    using Node = stlp::list<RadiusMarker>::Node;
    for (Node* node = reinterpret_cast<Node*>(first); node != reinterpret_cast<Node*>(last); node = node->next) {
        RadiusMarker& marker = node->data;
        double dx = marker.x - x;
        double dy = marker.y - y;
        double distance = Hypot(dx, dy);
        float limit = marker.radius * scale;
        // FCOMPP + TEST AH,0x41: marked unless the distance is greater (a NaN marks too).
        bool unordered = x87::IsNaN(static_cast<float>(distance)) || x87::IsNaN(limit);
        if (unordered || !(distance > static_cast<double>(limit))) marker.marked = 1;
    }
    out[0] = x;
    out[1] = y;
    out[2] = scale;
    return out;
}


__declspec(naked) void ScreenHits::InsertOverflow(ScreenEntry*, const ScreenEntry*, int) EI_ORIGINAL(0x00573350)

namespace {

// The hit test of the object's side half (CObject3D + 0x94) for the point: slot 22 or 24.
using HitTest = int (CObject3DSpecific::*)(const long* point);

// The entries of the screen list's nodes [first, last) whose rectangle holds (x, y) and whose
// object still exists and answers `test` go to `hits`.
void** CollectHits(void** out, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x, LONG y,
                   CBaseManager* manager, HitTest test) {
    ScreenHits* vector = static_cast<ScreenHits*>(hits);
    for (ScreenEntryNode* node = reinterpret_cast<ScreenEntryNode*>(first); node != reinterpret_cast<ScreenEntryNode*>(last);
         node = node->next) {
        const ScreenEntry* entry = &node->data;
        POINT pt = {x, y};
        if (!PtInRect(reinterpret_cast<const RECT*>(entry->rect), pt)) continue;
        CEngineObject* object = manager->FindObject(entry->objectId);
        if (!object) continue;
        LONG point[2] = {x, y};
        CObject3DSpecific* half = MapObjectOf(object)->Object3D()->Specific();
        if (!(half->*test)(point)) continue;
        vector->PushBack(entry);
    }
    *out = hits;
    return out;
}

} // namespace

void** __fastcall CollectWindowsAt(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x,
                                   LONG y, CBaseManager* manager) {
    return CollectHits(out, first, last, hits, x, y, manager, &CObject3DSpecific::BoundsContainPoint);
}

void** __fastcall CollectWindowsAt2(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, LONG x,
                                    LONG y, CBaseManager* manager) {
    return CollectHits(out, first, last, hits, x, y, manager, &CObject3DSpecific::PartsContainPoint);
}

// ---- Lookups -------------------------------------------------------------------------------------

int __fastcall PendingMapLookup(const PendingMap* map, void*, const std::uint32_t* key, void** value) {
    const PendingMap::Node* node = map->Find(*key);
    if (!node) return 0;
    *value = node->value;
    return 1;
}

const std::uint8_t* __fastcall RecordAt50(const RecordVector* records, void*, std::uint32_t index) {
    int count = static_cast<int>(records->size() / 0x50);
    if (index < static_cast<std::uint32_t>(count)) return records->begin() + index * 0x50;
    return nullptr;
}

const std::uint8_t* __fastcall RecordAtB4(const RecordVector* records, void*, std::uint32_t index) {
    int count = static_cast<int>(records->size() / 0xB4);
    if (index < static_cast<std::uint32_t>(count)) return records->begin() + index * 0xB4;
    return nullptr;
}

// ---- The client's object maps --------------------------------------------------------------------

namespace {

// The plain STLport lists of pointers (+0xB4 counted, +0xB8 referenced by hand, +0x7BC, +0xF0):
// end [0056DFA0], insert [0056EA80], begin [0056DF90], erase [0056DFB0] (thiscall on the list).
using PlainList = SectorList;
struct PlainOps {
    static SectorListNode** __fastcall End(PlainList*, void*, SectorListNode** out);
    static SectorListNode** __fastcall Insert(PlainList*, void*, SectorListNode** out, SectorListNode* position,
                                              CEngineObject* const& value);
    static SectorListNode** __fastcall Begin(PlainList*, void*, SectorListNode** out);
    static SectorListNode** __fastcall Erase(PlainList*, void*, SectorListNode** out, SectorListNode* node);
};
__declspec(naked) SectorListNode** __fastcall PlainOps::End(PlainList*, void*, SectorListNode**) EI_ORIGINAL(0x0056DFA0)
__declspec(naked) SectorListNode** __fastcall PlainOps::Insert(PlainList*, void*, SectorListNode**, SectorListNode*,
                                                               CEngineObject* const&) EI_ORIGINAL(0x0056EA80)
__declspec(naked) SectorListNode** __fastcall PlainOps::Begin(PlainList*, void*, SectorListNode**) EI_ORIGINAL(0x0056DF90)
__declspec(naked) SectorListNode** __fastcall PlainOps::Erase(PlainList*, void*, SectorListNode**, SectorListNode*)
    EI_ORIGINAL(0x0056DFB0)

// The sector grid's cell list: remove(counted value) [00570400], push_back [0056DED0], clear
// [005703C0]; the second grid's clear [00573660].
struct CellOps {
    static void __fastcall Remove(SectorList*, void*, CEngineObject* const* value);
    static void __fastcall PushBack(SectorList*, void*, CEngineObject* const* value);
    static void __fastcall Clear(SectorList* list) { CountedListClearOriginal(list); }
    static void __fastcall Clear2(SectorList*);
};
__declspec(naked) void __fastcall CellOps::Remove(SectorList*, void*, CEngineObject* const*) EI_ORIGINAL(0x00570400)
__declspec(naked) void __fastcall CellOps::PushBack(SectorList*, void*, CEngineObject* const*) EI_ORIGINAL(0x0056DED0)
__declspec(naked) void __fastcall CellOps::Clear2(SectorList*) EI_ORIGINAL(0x00573660)

// The client's MFC CList<CEngineObject*> (+0xBC): NewNode [00573290], FreeNode [00573300]; the
// platform list's RemoveAt [0056FDD0] (on the platform set's list, +0x1F0).
using DynamicList = mfc::CList<CEngineObject*>;
using DynamicNode = DynamicList::Node;
DynamicNode* __fastcall DynamicNewNode(DynamicList* list, void*, DynamicNode* prev, DynamicNode* next) {
    return static_cast<DynamicNode*>(PtrListNewNode573290Original(list, nullptr, prev, next));
}
__declspec(naked) void __fastcall DynamicFreeNode(DynamicList*, void*, DynamicNode*) EI_ORIGINAL(0x00573300)
__declspec(naked) void __fastcall PlatformRemoveAt(PlatformList*, void*, PlatformNode*) EI_ORIGINAL(0x0056FDD0)

// The id map +0xD8 (hash_map<unsigned, DynamicNode*>, then the highest key): operator[]
// [00406A10], erase(iterator) [00405720], clear [00406E80].
void __fastcall IdMapErase(PendingMap* map, void*, const PendingMap::Node* node, const PendingMap* owner) {
    HashEraseOriginal(map, nullptr, const_cast<PendingMap::Node*>(node), owner);
}
void __fastcall IdMapClear(PendingMap* map) { HashClearOriginal(map); }

// 00538EA0 on a unit's controller (a hero leaving a session of kind 3).
__declspec(naked) void __fastcall HeroLeft(CEngineObject*) EI_ORIGINAL(0x00538EA0)
// ClearMaps: the session's player data [00665200].
void __fastcall SessionPlayerClear(WorldSession* session) { PlayerClearOriginal(session); }

// The STLport sort over the screen entries with DepthLess {plane, manager}: __introsort_loop
// [00573740] (its heap fallback is partial_sort 005769C0), __insertion_sort [00575BC0],
// __unguarded_insertion_sort [00576D40].
__declspec(naked) void __fastcall IntroSortLoop(ScreenEntry*, ScreenEntry*, void*, int, const float*, void*)
    EI_ORIGINAL(0x00573740)
__declspec(naked) void __fastcall InsertionSort(ScreenEntry*, ScreenEntry*, const float*, void*) EI_ORIGINAL(0x00575BC0)
__declspec(naked) void __fastcall UnguardedInsertionSort(ScreenEntry*, ScreenEntry*, void*, const float*, void*)
    EI_ORIGINAL(0x00576D40)

constexpr int kClassPlatform54 = 0x54;
constexpr int kClassPlatform5D = 0x5D;
constexpr int kTypeHero = 0x32;

// The object's grid cell list, or null outside the grid.
SectorList* CellFor(SectorGrid& grid, CEngineObject* object) {
    std::uint32_t x = CellOf(MapObjectOf(object)->position.x);
    std::uint32_t y = CellOf(MapObjectOf(object)->position.y);
    if (x >= grid.width_ || y >= grid.height_) return nullptr;
    return &grid.rows_[y][x];
}

} // namespace

int CWorldClient::AddObject(CEngineObject* object) {
    PlainList& objects = objects_;
    for (SectorListNode* node = objects.head->next; node != objects.head; node = node->next) {
        if (node->data->id_ == object->id_) {
            ++object->refCount_;
            object->Release();
            return 1;
        }
    }
    const PendingMap::Node* entry = dynamicNodes_.Find(static_cast<std::uint32_t>(object->id_));
    if (!entry && ObjectSlots(object)->IsSynchronised()) {
        DynamicList& kept = dynamicObjects_;
        DynamicNode* node = DynamicNewNode(&kept, nullptr, kept.tail, nullptr);
        node->data = object;
        if (kept.tail)
            kept.tail->next = node;
        else
            kept.head = node;
        kept.tail = node;
        DynamicNode* tail = kept.tail;
        int key = object->id_;
        *HashMapAt(&dynamicNodes_, nullptr, &key) = tail;
    }
    int kind = object->GetClassId();
    if (kind == kClassPlatform54 || object->GetClassId() == kClassPlatform5D) {
        CMapObject* mapObject = MapObjectOf(object);
        CObject3D* figure = mapObject->Object3D();
        for (int i = 0; i < figure->parts.size; ++i) {
            if ((figure->PartBox(i)->flags >> 10) & 1) {
                int id = object->id_;
                platforms_.AddBox(id, figure->PartBox(i), &mapObject->position, &mapObject->orientation);
            }
        }
    }
    if (object->GetClassId() == world::kClassUnit && Session()->gameKind == 1) {
        auto* unit = static_cast<CUnit*>(object);
        if (unit->player == nullptr && ticksRun_ >= 1) unit->Controller()->RestartFadeIn();
    }

    ++object->refCount_;
    SectorListNode* end;
    SectorListNode* inserted;
    PlainOps::Insert(&objects, nullptr, &inserted, *PlainOps::End(&objects, nullptr, &end), object);

    SectorGrid& grid = objectGrid_;
    if (SectorList* cell = CellFor(grid, object)) {
        CEngineObject* value = object;
        HoldRef(value);
        CellOps::Remove(cell, nullptr, &value);
        DropRef(value);
    }
    if (SectorList* cell = CellFor(grid, object)) {
        CEngineObject* value = object;
        HoldRef(value);
        CellOps::PushBack(cell, nullptr, &value);
        DropRef(value);
    }
    if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF)) {
        CEngineObject* value = object;
        HoldRef(value);
        SectorList& heroes = heroes_;
        SectorListNode* heroEnd;
        GridOpsA::Insert(&heroes, nullptr, &inserted, *GridOpsA::End(&heroes, nullptr, &heroEnd), value);
        DropRef(value);
    }
    return 1;
}

void CWorldClient::RefreshPlatforms(CEngineObject* object) {
    const int id = object->id_;
    PlainList& objects = objects_;
    SectorListNode* filed = objects.head->next;
    while (filed != objects.head && filed->data->id_ != id) filed = filed->next;
    if (filed == objects.head) return;
    PlatformList& platforms = platforms_.list;
    for (PlatformNode* node = platforms.head; node;) {
        PlatformNode* next = node->next;
        if (node->data.id == id) platforms.RemoveAt(node);   // the original's inlined RemoveAt
        node = next;
    }
    if (object->GetClassId() != kClassPlatform54 && object->GetClassId() != kClassPlatform5D) return;
    CMapObject* mapObject = MapObjectOf(object);
    CObject3D* figure = mapObject->Object3D();
    for (int i = 0; i < figure->parts.size; ++i) {
        if (!((figure->PartBox(i)->flags >> 10) & 1)) continue;
        platforms_.AddBox(object->id_, figure->PartBox(i), &mapObject->position, &mapObject->orientation);
    }
}

bool CWorldClient::QueueSynchronised(CEngineObject* object) {
    if (dynamicNodes_.Find(static_cast<std::uint32_t>(object->id_))) return false;
    DynamicList& kept = dynamicObjects_;
    DynamicNode* node = DynamicNewNode(&kept, nullptr, kept.tail, nullptr);
    node->data = object;
    if (kept.tail)
        kept.tail->next = node;
    else
        kept.head = node;
    kept.tail = node;
    DynamicNode* tail = kept.tail;
    int key = object->id_;
    *HashMapAt(&dynamicNodes_, nullptr, &key) = tail;
    return true;
}

void CWorldClient::MarkFootprintsInSight(const float* point, float range) {
    Vector3 functor;   // for_each's functor (x, y, range) comes back by value
    MarkWithinRadius(&functor.x, nullptr, reinterpret_cast<std::uint8_t*>(footprints_.First()),
                     reinterpret_cast<std::uint8_t*>(footprints_.End()), point[0], point[1], range);
}

void CWorldClient::ApplyWind(const Vector3* direction, float strength) {
    SetWind(direction, strength);
    if (terrain_) {
        Vector3 copy = *direction;
        terrain_->SetWaves(&copy, strength);
    }
    RainSnow()->SetWind(*direction, strength);
}

int CWorldClient::CentreOn(const Vector3* point) {
    Vector3 focus = *point;
    focus.z = camera_->focus_.z;
    camera_->SetFocus(&focus);
    camera_->ApplyView();
    camera_->ClampAboveObjects();
    return 1;
}

double CWorldClient::ClientGroundHeight(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(GridHeightAt(x, y));
    return platforms_.SurfaceHeight(&point, height);
}

double CWorldClient::ClientSurfaceHeight(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(RawSurfaceHeightAt(x, y));
    return platforms_.SurfaceHeight(&point, height);
}

void CWorldClient::ShowNumber(int id, int value, std::uint32_t kind) {
    CEngineObject* object = static_cast<CBaseManager*>(MapObjectManager())->FindObject(static_cast<unsigned>(id));
    if (!object || object->GetClassId() != world::kClassUnit) return;
    if (numbers_) numbers_->Add(id, value, kind);
}

void CWorldClient::LoadUnitFaces() {
    for (SectorListNode* node = objects_.head->next; node != objects_.head; node = node->next) {
        CEngineObject* object = node->data;
        if (object->GetClassId() != world::kClassUnit) continue;
        if (!node->data) continue;   // the original tests the pointer after the class call
        static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(node->data)->controller)->LoadFaceTexture();
    }
}

const char* CWorldClient::ExitTarget(int index) { return exits_[index].target; }

void CWorldClient::RemoveUnitsOf(const RecordPtrArray* records) {
    for (int i = 0; i < records->size; ++i)
        RemoveObject(static_cast<int>(reinterpret_cast<PartyUnitRecord*>(records->data[i])->unitId));
}

int CWorldClient::SendFlag10(int value) {
    return Session()->LocalClient()->SendFlag10(static_cast<std::uint8_t>(value));
}

void CWorldClient::DropShadows() {
    PlainList& objects = objects_;
    for (SectorListNode* node = objects.head->next; node != objects.head; node = node->next) {
        CObject3DSpecific* half = MapObjectOf(node->data)->Object3D()->Specific();   // the client half
        if (!ShadowsShownOriginal(half)) ShadowClearOriginal(half);
    }
}

void CWorldClient::RemoveObject(int id) {
    // The object's platform faces.
    PlatformList& platforms = platforms_.list;
    for (PlatformNode* node = platforms.head; node;) {
        PlatformNode* next = node->next;
        if (node->data.id == id) PlatformRemoveAt(&platforms, nullptr, node);
        node = next;
    }
    PendingMap& ids = dynamicNodes_;
    if (const PendingMap::Node* entry = ids.Find(static_cast<std::uint32_t>(id))) {
        DynamicList& kept = dynamicObjects_;
        DynamicNode* node = static_cast<DynamicNode*>(entry->value);
        if (node == kept.head)
            kept.head = node->next;
        else
            node->prev->next = node->next;
        if (node == kept.tail)
            kept.tail = node->prev;
        else
            node->next->prev = node->prev;
        DynamicFreeNode(&kept, nullptr, node);
        if (const PendingMap::Node* again = ids.Find(static_cast<std::uint32_t>(id))) IdMapErase(&ids, nullptr, again, &ids);
    }
    PlainList& objects = objects_;
    SectorListNode* node = objects.head->next;
    for (;; node = node->next) {
        if (node == objects.head) return;
        if (node->data->id_ == id) break;
    }
    CEngineObject* object = node->data;
    if (object->type_ == kTypeHero && (IsTextKey(id) & 0xFF)) {
        CEngineObject* value = object;
        HoldRef(value);
        SectorList& heroes = heroes_;
        for (SectorListNode* hero = heroes.head->next; hero != heroes.head;) {
            SectorListNode* next = hero->next;
            if (hero->data == value) {
                SectorListNode* ignored;
                GridOpsA::Erase(&heroes, nullptr, &ignored, hero);
            }
            hero = next;
        }
        DropRef(value);
        if (Session()->gameKind == 3) HeroLeft(static_cast<CUnit*>(object)->controller);
    }
    if (SectorList* cell = CellFor(objectGrid_, object)) {
        CEngineObject* value = object;
        HoldRef(value);
        CellOps::Remove(cell, nullptr, &value);
        DropRef(value);
    }
    SectorListNode* ignored;
    PlainOps::Erase(&objects, nullptr, &ignored, node);
    object->Release();
}

bool __fastcall ReleaseIfIdentified(void*, void*, CEngineObject* object) {
    if (object->id_ > 0) {
        object->Release();
        return true;
    }
    return false;
}

void CWorldClient::ClearMaps() {
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            CellOps::Clear2(&second.rows_[y][x]);
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            CellOps::Clear(&grid.rows_[y][x]);

    // MFC RemoveAll of the platform faces and the dynamic objects (the elements need no
    // destruction).
    platforms_.list.RemoveAll();
    dynamicObjects_.RemoveAll();
    IdMapClear(&dynamicNodes_);
    dynamicMaxId_ = 0;

    stlp::list<void*>& records = list7BC_;
    for (stlp::list<void*>::Node* node = records.head->next; node != records.head;) {
        stlp::list<void*>::Node* next = node->next;
        if (node) stlp::deallocate(node, 0xC);
        node = next;
    }
    records.head->next = records.head;
    records.head->prev = records.head;

    PlainList& heroes = heroes_;
    for (SectorListNode* node = heroes.head->next; node != heroes.head;) {
        SectorListNode* next = node->next;
        CountedRelease(&node->data, nullptr, 0);
        NodeFree12Fast(node, 1);
        node = next;
    }
    heroes.head->next = heroes.head;
    heroes.head->prev = heroes.head;

    PlainList& objects = objects_;
    SectorListNode* node;
    PlainOps::Begin(&objects, nullptr, &node);
    SectorListNode* end = objects.head;
    std::uint8_t functor = 0;
    while (node != end) {
        SectorListNode* next = node->next;
        if (ReleaseIfIdentified(&functor, nullptr, node->data)) {
            SectorListNode* ignored;
            PlainOps::Erase(&objects, nullptr, &ignored, node);
        }
        node = next;
    }
    SessionPlayerClear(Session());
    if (CLinkFigureLoader* loader = figureLoader_) loader->ClearCache();
    static_cast<CBaseManager*>(manager3C_)->FreeSpares();
    static_cast<CBaseManager*>(manager40_)->FreeSpares();
    static_cast<CBaseManager*>(manager44_)->FreeSpares();
}

namespace {

CEngineObject* Pick(CWorldClient* client, const long* point,
                    void** (__fastcall* collect)(void**, void*, std::uint8_t*, std::uint8_t*, void*, long, long,
                                                  CBaseManager*)) {
    ScreenHits& hits = client->hits_;
    hits.finish = hits.start;   // erase(begin, end): __copy of nothing
    CBaseManager* manager = static_cast<CBaseManager*>(client->MapObjectManager());
    stlp::list<ScreenEntry>& screen = client->screenEntries_;
    void* functor;
    collect(&functor, nullptr, reinterpret_cast<std::uint8_t*>(screen.head->next),
            reinterpret_cast<std::uint8_t*>(screen.head), &hits, point[0], point[1], manager);
    if (hits.start == hits.finish) return nullptr;

    // sort(begin, end, DepthLess{top of the view stack, objects}), the introsort loop's first level
    // inlined in the original (the same steps as the out-of-line loop).
    MatrixDevice* device = TheMatrixDevice();
    const float* plane = device->viewStack + (device->viewDepth - 1) * 16;
    void* objects = client->MapObjectManager();
    ScreenEntry* first = hits.start;
    ScreenEntry* last = hits.finish;
    if (first != last) {
        int count = static_cast<int>(last - first);
        int depth = 0;
        for (int n = count; n != 1; n >>= 1) ++depth;
        if (count > 16) {
            IntroSortLoop(first, last, nullptr, depth * 2, plane, objects);
            InsertionSort(first, first + 16, plane, objects);
            UnguardedInsertionSort(first + 16, last, nullptr, plane, objects);
        } else {
            InsertionSort(first, last, plane, objects);
        }
    }
    ScreenEntry*& cursor = client->hitCursor_;
    for (cursor = hits.start; cursor != hits.finish; ++cursor) {
        if (CEngineObject* object = manager->FindObject(cursor->objectId)) return object;
    }
    return nullptr;
}

} // namespace

CEngineObject* CWorldClient::PickAt(const long* point) { return Pick(this, point, CollectWindowsAt); }
CEngineObject* CWorldClient::PickAt2(const long* point) { return Pick(this, point, CollectWindowsAt2); }

namespace {
// From a picked object on: the first unit of the sorted hits. The cursor moves past entries whose
// object is gone or is not a unit; null at the end.
CEngineObject* FirstUnitHit(CWorldClient* client, CEngineObject* object) {
    while (object->GetClassId() != world::kClassUnit) {
        do {
            if (++client->hitCursor_ == client->hits_.finish) return nullptr;
            object = static_cast<CBaseManager*>(client->manager40_)->FindObject(client->hitCursor_->objectId);
        } while (!object);
    }
    return object;
}
} // namespace

CEngineObject* CWorldClient::UnitAt(const long* point) {
    if (CEngineObject* picked = PickAt2(point)) {
        if (CEngineObject* unit = FirstUnitHit(this, picked)) return unit;
    }
    CEngineObject* picked = PickAt(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

CEngineObject* CWorldClient::UnitAt2(const long* point) {
    CEngineObject* picked = PickAt2(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

void CWorldClient::UnitsInBox(const long* rect, SectorList* out) {
    for (ScreenEntryNode* node = screenEntries_.head->next; node != screenEntries_.head; node = node->next) {
        CEngineObject* object = static_cast<CBaseManager*>(manager40_)->FindObject(node->data.objectId);
        if (!object) continue;
        if (!MapObjectOf(object)->Object3D()->Specific()->PartsIntersectRect(rect)) continue;
        SectorListNode* end;
        PlainOps::End(out, nullptr, &end);
        SectorListNode* inserted;
        PlainOps::Insert(out, nullptr, &inserted, end, object);
    }
}

// ---- The party's units (00566F70) ----------------------------------------------------------------

namespace {

// The prototypes' name map (worlddata::PrototypeNameMap, hash_map<string, record*>): find or insert
// a {name, 0} pair [00514880], returning the stored pair; the prototype table's lookup by name
// [00514930].
struct NamePair {
    stlp::string first;
    UnitPrototypeRecord* second;
};
// CUnit: the second prototype by name [005308D0], the figure made from a model and its part names
// [00516DA0], 00518840 / 00518860 after it, 0050EA40, the position [00517700], the items
// [005315B0 release all, 00531070 weapon, 00530730 armour, 00531180 quick item].
int __fastcall UnitMakeFigure(void* a0, void*, const char* a2, console::CStringArray* a3, int a4) {
    return static_cast<CObject3D*>(a0)->MakeFigure(a2, a3, a4);
}
void __fastcall Unit518840(void* a0, void*, const char* a2, int a3) { Object3DSetAnimationsOriginal(a0, nullptr, a2, a3); }
void __fastcall Unit518860(void* a0) { static_cast<CObject3D*>(a0)->RestartPose(); }
void __fastcall Unit50EA40(void* a0) { static_cast<CMapObject*>(a0)->WriteFullState(); }
// The figure's 0051C970 and its texture swap [0051C250: from, to (null: none)]; an armour's type
// [006A39E0, on the item].
void __fastcall Figure51C970(void* a0) { static_cast<CObject3DClientSpecific*>(a0)->RegisterPartMaterials(); }

bool SameText(const char* a, const char* b) {   // the inlined strcmp(a, b) == 0
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

} // namespace

// The figure's part names (worlddata::HeroPartText): "hd", "bd", "hp", "hr" (the hair: "hr.%.2d"
// with the prototype's number, or left out at -1), "lh1".."rl3".
void CWorldClient::CreatePartyUnits(const RecordPtrArray* party, char allArmour) {
    mfc::CString hair;
    for (int i = 0; i < party->size; ++i) {
        auto* record = reinterpret_cast<PartyUnitRecord*>(party->data[i]);
        console::CStringArray parts;
        for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
            const char* name = worlddata::HeroPartText(n);
            if (SameText(name, worlddata::HairPartText())) {
                NamePair* stored;
                {
                    stlp::string key(static_cast<const char*>(record->texts[5]));
                    NamePair pair{key, nullptr};
                    stored = (NamePair*)NameMapInsertOriginal(worlddata::PrototypeNameMap(), nullptr, (const void*)&pair);
                }
                int number = stored->second->hair;
                if (number != -1) {
                    hair.Format(worlddata::HairFormat(), number);
                    parts.SetAtGrow(parts.GetSize(), hair);
                }
            } else {
                parts.SetAtGrow(parts.GetSize(), name);
            }
        }
        auto* manager = static_cast<CBaseManager*>(MapObjectManager());
        auto* unit = static_cast<CUnit*>(manager->CreateObject(kTypeHero, static_cast<int>(record->unitId)));
        if (!unit) return;   // (the original leaves the loop: the same as returning here)
        auto* prototype = reinterpret_cast<std::uint8_t*>(unitdb::PrototypeTable().FindByName(record->texts[5]));
        unit->prototypeId_ = static_cast<std::uint32_t>(
            prototype ? static_cast<int>(prototype - reinterpret_cast<std::uint8_t*>(unitdb::Prototypes().begin)) /
                            unitdb::kPrototypeSize
                      : -1);
        unit->SetPrototype23CByName(record->texts[6]);
        unit->flags_ |= 0x4000000;
        if (!UnitMakeFigure(unit, nullptr, record->texts[2], &parts, 1)) return;
        CEngineObject* figure = unit->Object3D()->attached;
        Figure51C970(figure);
        Unit518840(unit, nullptr, record->texts[2], 0);
        Unit518860(unit);
        const char* skin = record->texts[4];
        static_cast<CObject3DClientSpecific*>(figure)->SetSkins(game::NoneText(), (SameText(skin, game::NoneText()) ? nullptr : skin));
        Unit50EA40(unit);
        CMapObjectVirtuals* slots = ObjectSlots(unit);
        slots->SetStats(reinterpret_cast<const UnitStats*>(record->stats));
        ObjectSetFigureScaleOriginal(unit, nullptr, &record->scaleX);
        std::memcpy(&unit->MapObject()->baseScale, &record->scaleX, 12);
        slots->ApplyBaseScale();
        unit->ReleaseAllItems();
        for (int j = 0; j < static_cast<int>(record->weaponCount); ++j)
            UnitAddWeaponOriginal(unit, nullptr, record->weapons[j]);
        CUnitController* controller = unit->Controller();
        if (record->weaponCount != 0) controller->ShowWeapon(0);
        for (int j = 0; j < static_cast<int>(record->armorCount); ++j) {
            CEngineObject* armour = record->armor[j];
            if (allArmour || reinterpret_cast<CItemArmor*>(armour)->GetType() != 0) UnitEquipArmourOriginal(unit, nullptr, armour);
        }
        for (int j = 0; j < static_cast<int>(record->quickItemCount); ++j)
            UnitAddQuickItemOriginal(unit, nullptr, record->quickItems[j]);
        unit->Controller()->RefreshEquipment();
        unit->Controller()->RefreshModel();
        slots->SetScriptName(record->texts[0]);
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
    float maxX = terrain_->width - 1.0f;
    float maxY = terrain_->height - 1.0f;
    float y1 = MinOf(MaxZero(point[1] + radius), maxY);
    float x1 = MinOf(MaxZero(point[0] + radius), maxX);
    float y0 = MaxZero(MinOf(MaxZero(point[1] - radius), maxY));
    float x0 = MaxZero(MinOf(MaxZero(point[0] - radius), maxX));
    std::uint32_t& firstX = rangeFirstX_;
    std::uint32_t& firstY = rangeFirstY_;
    std::uint32_t& lastX = rangeLastX_;
    std::uint32_t& lastY = rangeLastY_;
    std::uint32_t& x = rangeX_;
    std::uint32_t& y = rangeY_;
    firstX = x = Ftol(x0) >> 4;
    firstY = y = Ftol(y0) >> 4;
    lastX = Ftol(x1) >> 4;
    lastY = Ftol(y1) >> 4;

    SectorGrid& grid = objectGrid_;
    PlainList* list = static_cast<PlainList*>(out);
    for (SectorList* cell = &grid.rows_[y][x]; cell;) {
        for (SectorListNode* node = cell->head->next; node != cell->head; node = node->next) {
            CMapObject* object = MapObjectOf(node->data);
            float pz = point[2];
            float ox = object->position.x + object->center.x;
            float oy = object->position.y + object->center.y;
            double reach = Hypot(object->above.x, object->above.y) + radius;
            float dx = ox - point[0];
            float dy = oy - point[1];
            float dz = pz - point[2];
            double distance = LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
            double limit = reach * reach;
            // FCOMPP + TEST AH,1: skipped when the limit is below (or unordered).
            bool unordered = x87::IsNaN(static_cast<float>(distance)) || x87::IsNaN(static_cast<float>(limit));
            if (!unordered && !(limit < distance)) {
                SectorListNode* end;
                SectorListNode* inserted;
                PlainOps::Insert(list, nullptr, &inserted, *PlainOps::End(list, nullptr, &end), node->data);
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
// The face list's NewNode [00573000: (prev, next); the element constructed by 00575660].
__declspec(naked) PlatformNode* __fastcall PlatformNewNode(PlatformList*, void*, PlatformNode*, PlatformNode*)
    EI_ORIGINAL(0x00573000)
constexpr float kSteepest = 0.8f;   // 0073CCA8: the smallest normal z of a face added
} // namespace

int PlatformSet::AddFace(const Vector3* a, const Vector3* b, const Vector3* c, const Vector3* normal, Platform** out) {
    *out = nullptr;
    // FCOMP + TEST AH,1: steeper faces (and a NaN z) are left out.
    if (x87::IsNaN(normal->z) || normal->z < kSteepest) return 0;
    // AddHead with the instantiation's NewNode.
    PlatformNode* last = list.head;
    PlatformNode* node = PlatformNewNode(&list, nullptr, nullptr, last);
    // (The original first copies 0x4C bytes of uninitialised stack over the record; every field is
    // written below or by the caller.)
    if (last)
        last->prev = node;
    else
        list.tail = node;
    list.head = node;
    Platform* record = &node->data;
    *out = record;
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
    float lengthU = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz)));
    record->sizeU = lengthU;
    float lengthV = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(vx, vy, vz)));
    record->sizeV = lengthV;
    std::memcpy(&record->normal, normal, 12);
    std::memcpy(&record->origin, a, 12);
    record->axisU.x = ux / lengthU;
    record->axisU.y = uy / lengthU;
    record->axisU.z = uz / lengthU;
    record->axisV.x = vx / lengthV;
    record->axisV.y = vy / lengthV;
    record->axisV.z = vz / lengthV;
    return 1;
}

void PlatformSet::AddBox(int id, const FootprintPart* item, const Vector3* position, const Quaternion* rotation) {
    const Vector3& offset = item->offset;
    const Vector3& low = item->boxMin;
    const Vector3& high = item->boxMax;
    const Vector3& pivot = item->pivot;

    // The rotation matrix of the quaternion (w, x, y, z), as the original builds it.
    float q0 = rotation->w, q1 = rotation->x, q2 = rotation->y, q3 = rotation->z;
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
    // offset (stored), rotated (rows summed ZYX, stored) and placed.
    Vector3 corners[8] = {{high.x, low.y, low.z},  {low.x, low.y, low.z},   {low.x, high.y, low.z},
                          {low.x, high.y, high.z}, {high.x, high.y, high.z}, {high.x, low.y, high.z},
                          {high.x, low.y, low.z},  {low.x, low.y, low.z}};
    for (Vector3& corner : corners) corner = *position + TransformColumnNormal<Sum3::ZYX>(m, (pivot + corner) + offset);
    // The face normals, rotated (rows summed ZXY).
    Vector3 normals[6] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
    for (Vector3& n : normals) n = TransformColumnNormal<Sum3::ZXY>(m, n);
    for (int i = 0; i < 6; ++i) {
        Platform* record;
        if (!AddFace(&corners[i + 1], &corners[i], &corners[i + 2], &normals[i], &record)) continue;
        record->id = id;
        Vector3 moved = pivot + offset;
        Vector3 rotated;
        m.Transform(&rotated, &moved);
        record->x = rotated.x + position->x;
        record->y = rotated.y + position->y;
        record->z = rotated.z + position->z;
        std::memcpy(&record->radius, &item->radius, 4);   // a dword move
    }
}

// ---- Teardown (00563290) -------------------------------------------------------------------------

namespace {

__declspec(naked) void __fastcall Destroy1AC(void*) EI_ORIGINAL(0x005636A0)
// The counted array +0xA0's SetSize (its own instantiation: dropped elements released).
__declspec(naked) void __fastcall CountedArraySetSize(mfc::CArray<CEngineObject*>*, void*, int, int) EI_ORIGINAL(0x00570000)
// The lists' element destructors [00575560 / 00575580 (node + 8, flags)] and
// node releases [0056FB80 / 00498C40 / 0056FB00 (node, count)].
__declspec(naked) void __fastcall Node1B8Free(void*, int) EI_ORIGINAL(0x0056FB80)
__declspec(naked) void __fastcall Element1BCDestroy(void*, void*, int) EI_ORIGINAL(0x00575560)
__declspec(naked) void __fastcall Element1C0Destroy(void*, void*, int) EI_ORIGINAL(0x00575580)
__declspec(naked) void __fastcall Node7BCFree(void*, int) EI_ORIGINAL(0x00498C40)
__declspec(naked) void __fastcall NodeB8Free(void*, int) EI_ORIGINAL(0x0056FB00)
// The platform list's RemoveAll [0056FDA0]; the base world's teardown [0055F8E0].
__declspec(naked) void __fastcall PlatformRemoveAll(PlatformList*) EI_ORIGINAL(0x0056FDA0)
__declspec(naked) void __fastcall WorldTeardown(CWorldClient*) EI_ORIGINAL(0x0055F8E0)

// The list's nodes walked from the head's next, each given to `free` after the next is read; the
// list is left empty.
template <typename T, typename F>
void EmptyList(stlp::list<T>& list, F free) {
    using Node = typename stlp::list<T>::Node;
    for (Node* node = list.head->next; node != list.head;) {
        Node* next = node->next;
        free(node);
        node = next;
    }
    list.head->next = list.head;
    list.head->prev = list.head;
}

// The three ground print lists emptied (Teardown and the zone load inline this): each element
// destroyed, its node returned to the pool.
void ClearGroundPrints(stlp::list<BloodPrint>& blood, stlp::list<Footprint>& feet, stlp::list<GroundPrint>& scorch) {
    EmptyList(blood, [](stlp::list<BloodPrint>::Node* node) {
        static_cast<GroundPrint*>(&node->data)->Destruct();
        Node1B8Free(node, 1);
    });
    EmptyList(feet, [](stlp::list<Footprint>::Node* node) {
        Element1BCDestroy(&node->data, nullptr, 0);
        if (node) stlp::deallocate(node, sizeof(*node));
    });
    EmptyList(scorch, [](stlp::list<GroundPrint>::Node* node) {
        Element1C0Destroy(&node->data, nullptr, 0);
        if (node) stlp::deallocate(node, sizeof(*node));
    });
}

// An owned engine object deleted (its slot 0, the scalar deleting destructor, with 1) and cleared.
template <typename T>
void DeleteOwned(T*& slot) {
    if (slot) {
        delete static_cast<CEngineObject*>(slot);
        slot = nullptr;
    }
}

// A counted engine object: its count dropped (without a release), then deleted the same way.
template <typename T>
void DropAndDelete(T*& slot) {
    if (slot) {
        --slot->refCount_;
        if (slot) delete static_cast<CEngineObject*>(slot);
        slot = nullptr;
    }
}

} // namespace

void CWorldClient::Teardown() {
    camera::TheCamera()->SetTarget(nullptr);
    if (CFlyingHitPoints* p = numbers_) {
        p->Destruct();
        gamecrt::operator_delete(p);
        numbers_ = nullptr;
    }
    if (void* p = skyDome_) {
        Destroy1AC(p);
        gamecrt::operator_delete(p);
        skyDome_ = nullptr;
    }
    DeleteOwned(owned1B0_);
    ReleaseItemsAndSpells();
    CountedArraySetSize(&countedA0_, nullptr, 0, -1);
    ClearGroundPrints(bloodPrints_, footprints_, scorchMarks_);
    RainSnow()->Destroy();
    platforms_.Engine()->Destroy();
    PlatformRemoveAll(&platforms_.list);
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            CellOps::Clear(&grid.rows_[y][x]);
    EmptyList(list7BC_, [](stlp::list<void*>::Node* node) { Node7BCFree(node, 1); });
    EmptyList(heroes_, [](SectorListNode* node) {
        CountedRelease(&node->data, nullptr, 0);
        NodeFree12Fast(node, 1);
    });
    PlainList& objects = objects_;
    stlp::ListForEachRelease(objects.head->next, objects.head, 0);
    EmptyList(objects, [](SectorListNode* node) { NodeB8Free(node, 1); });
    if (WorldSession* session = Session()) {
        session->Release();
        session_ = nullptr;
    }
    if (CRefObject* handle = handle_.ptr) {
        if (handle->refs_ != 1) {
            handle->flags7_ |= CRefCounted::kDetached;
            handle->Shutdown();
        }
        handle_.ptr->Release();
        handle_.Share(nullptr);
    }
    WorldTeardown(this);
    DeleteOwned(listManager_);
    DeleteOwned(arrayManager_);
    DropAndDelete(particleManager_);
    DropAndDelete(terrain_);
    DropAndDelete(manager190_);
    DropAndDelete(manager188_);
    DropAndDelete(textureManager_);
    DeleteOwned(music_);
    DropAndDelete(sounds_);
    DropAndDelete(texts_);
}

// ---- Zone reset (00565090) -----------------------------------------------------------------------

void CWorldClient::ResetZoneObjects() {
    CountedArraySetSize(&countedA0_, nullptr, 0, -1);
    // The second grid's cells: each element released (005768A0, flags 0), its node freed (00572FC0),
    // the cell linked to itself.
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            second.rows_[y][x].ClearWith([](CEngineObject** element) {
                if (CEngineObject* object = *element) object->Release();
            });
    ObjectListIterator it{nullptr, nullptr};
    for (CEngineObject* object = it.First(TimedObjectManager()); object; object = it.Next()) object->Release();
    if (owned1B0_) TimedSlots(owned1B0_)->Reset();
    // The iterator's destructor, inlined: the manager's count drops without a release.
    if (it.manager) --it.manager->refCount_;
}

// ---- The selection (00566100) --------------------------------------------------------------------

namespace {
// The selection highlight off: flags 0x10 and 0x100 cleared, the side half's slots 25 (empty) and
// 26 with 0 (its light goes off).
void Unhighlight(CEngineObject* object) {
    object->flags_ &= ~0x110u;
    auto* half = static_cast<CObject3DSpecific*>(MapObjectOf(object)->attached);
    half->Unused25(0);
    half = static_cast<CObject3DSpecific*>(MapObjectOf(object)->attached);
    half->SetLit(0);
}
} // namespace

void CWorldClient::Deselect(CEngineObject* object) {
    stlp::list<void*>& selected = list7BC_;
    using Node = stlp::list<void*>::Node;
    auto* objects = static_cast<CBaseManager*>(manager40_);
    if (!object) {
        for (Node* node = selected.head->next; node != selected.head; node = node->next) {
            CEngineObject* found = objects->FindObject(static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(node->data)));
            if (found && (found->flags_ & 0x10)) Unhighlight(found);
        }
        EmptyList(selected, [](Node* node) { Node7BCFree(node, 1); });
        return;
    }
    const auto id = reinterpret_cast<void*>(static_cast<std::intptr_t>(object->id_));
    Node* node = selected.head->next;
    while (node != selected.head && node->data != id) node = node->next;   // 00571960 find
    if (node == selected.head) return;
    void* unused;
    IdListEraseOriginal(&selected, nullptr, &unused, node);
    if (object->flags_ & 0x10) Unhighlight(object);
}

// ---- Selecting (00566030; A-W13) -----------------------------------------------------------------

void CWorldClient::Select(CEngineObject* object) {
    if (!object) return;
    MapObjectOf(object)->Object3D()->UpdatePartBounds();
    if (!(object->flags_ & 0x10)) {
        if (!(object->flags_ & 0x100)) static_cast<CObject3DSpecific*>(MapObjectOf(object)->attached)->SetLit(1);
        object->flags_ |= 0x10;
        static_cast<CObject3DSpecific*>(MapObjectOf(object)->attached)->Unused25(1);
    }
    stlp::list<void*>& selected = list7BC_;
    using Node = stlp::list<void*>::Node;
    const auto id = reinterpret_cast<void*>(static_cast<std::intptr_t>(object->id_));
    Node* node = selected.head->next;
    while (node != selected.head && node->data != id) node = node->next;   // 00571960 find
    if (node != selected.head) return;
    void* unused;
    ListInsert404A40Original(&selected, nullptr, &unused, selected.head, &id);
}
EI_REPLACE_METHOD(0x00566030, CWorldClient::Select);

// ---- Set up (00561E50) ---------------------------------------------------------------------------

namespace {

// MFC: operator+(const CString&, LPCTSTR) [00700C05, __stdcall, the result constructed in `out`],
// CString::operator=(const CString&) [00700AA9], operator=(LPCTSTR) [00700AF9], ~CString [00700970].
void* __stdcall CStringConcat(void* a0, const void* a1, const char* a2) { return CStringConcatTextOriginal((mfc::CString*)a0, (const mfc::CString*)a1, a2); }
// The lights table loader [00563930 (file name)], the text resources [004DEDB0 on +0x1D0 (path)].
__declspec(naked) int __fastcall LoadLightsAt(void*, void*, const char*) EI_ORIGINAL(0x00563930)
__declspec(naked) void __fastcall LoadTexts(void*, void*, const char*) EI_ORIGINAL(0x004DEDB0)
// Constructors (thiscall; some take a block size or count).
__declspec(naked) void* __fastcall Construct6B6040(void*) EI_ORIGINAL(0x006B6040)
__declspec(naked) void __fastcall Construct562A80(void*) EI_ORIGINAL(0x00562A80)
__declspec(naked) void __fastcall Construct4DED20(void*) EI_ORIGINAL(0x004DED20)
__declspec(naked) void __fastcall TransformObjectBase(void*) EI_ORIGINAL(0x00453400)
__declspec(naked) void __fastcall Construct570E10(void*) EI_ORIGINAL(0x00570E10)
__declspec(naked) void __fastcall Construct570E40(void*) EI_ORIGINAL(0x00570E40)
__declspec(naked) void __fastcall Construct56FD30(void*) EI_ORIGINAL(0x0056FD30)
__declspec(naked) void __fastcall Construct570E70(void*) EI_ORIGINAL(0x00570E70)
__declspec(naked) void __fastcall Construct570EA0(void*, void*, int) EI_ORIGINAL(0x00570EA0)
__declspec(naked) void __fastcall Construct571050(void*) EI_ORIGINAL(0x00571050)
__declspec(naked) void __fastcall Construct570DE0(void*) EI_ORIGINAL(0x00570DE0)
__declspec(naked) void __fastcall Construct5711C0(void*) EI_ORIGINAL(0x005711C0)
__declspec(naked) void* __fastcall Construct42D690(void*) EI_ORIGINAL(0x0042D690)
__declspec(naked) void* __fastcall Construct691140(void*) EI_ORIGINAL(0x00691140)
__declspec(naked) void* __fastcall Construct68E3F0(void*) EI_ORIGINAL(0x0068E3F0)
__declspec(naked) void* __fastcall Construct68D850(void*) EI_ORIGINAL(0x0068D850)
__declspec(naked) void __fastcall Construct56FD40(void*, void*, int) EI_ORIGINAL(0x0056FD40)
__declspec(naked) void __fastcall Construct5725E0(void*) EI_ORIGINAL(0x005725E0)
__declspec(naked) void __fastcall Construct4981A0(void*) EI_ORIGINAL(0x004981A0)
void* __fastcall SessionFor(void* a0) { return PlayerCreateOriginal(a0); }
__declspec(naked) void __fastcall Construct5715D0(void*, void*, int) EI_ORIGINAL(0x005715D0)
__declspec(naked) void* __fastcall Construct6B4350(void*) EI_ORIGINAL(0x006B4350)
// vector<ScreenEntry>::_M_allocate_and_copy [00573520: count, first, last].
__declspec(naked) ScreenEntry* __fastcall EntriesAllocateCopy(ScreenHits*, void*, int, ScreenEntry*, ScreenEntry*)
    EI_ORIGINAL(0x00573520)

std::uint8_t* New(std::size_t size) { return static_cast<std::uint8_t*>(gamecrt::operator_new(size)); }

// A heap object made by its constructor (null without memory), stored and referenced (the
// original does not test the pointer before counting).
template <typename T>
T* MakeCounted(std::size_t size, void* (__fastcall* construct)(void*)) {
    std::uint8_t* memory = New(size);
    auto* object = static_cast<T*>(memory ? construct(memory) : nullptr);
    ++object->refCount_;
    return object;
}

// The grid set to 4 x 4 cells, reallocated unless its storage already has that size (the second
// resize inlined).
void GridToFour(SectorGrid& grid) {
    grid.Resize(1, 1);
    grid.width_ = 4;
    grid.height_ = 4;
    grid.Resize(4, 4);
}

// A reference on every object of `manager` (the original keeps the walk's iterator on the heap).
void ReferenceEveryObject(CBaseManager* manager) {
    auto* walk = reinterpret_cast<ObjectListIterator*>(New(sizeof(ObjectListIterator)));
    if (walk) walk->manager = nullptr;
    for (CEngineObject* object = walk->First(manager); object; object = walk->Next()) ++object->refCount_;
    if (walk) {
        if (CBaseManager* held = walk->manager) {
            --held->refCount_;
            walk->manager = nullptr;
        }
        gamecrt::operator_delete(walk);
    }
}

} // namespace

void DestroySectorList(SectorList* list) {
    list->ClearWith([](CEngineObject** object) {
        if (*object) (*object)->Release();
    });
    stlp::deallocate(list->head, sizeof(SectorListNode));
}

SectorList* __fastcall SectorListsDelete(SectorList* cells, void*, unsigned flags) {
    if (flags & 2) {
        int* block = reinterpret_cast<int*>(cells) - 1;   // new[]'s element count
        for (int i = *block; i-- > 0;) DestroySectorList(&cells[i]);
        if (flags & 1) gamecrt::operator_delete(block);
        return reinterpret_cast<SectorList*>(block);
    }
    DestroySectorList(cells);
    if (flags & 1) gamecrt::operator_delete(cells);
    return cells;
}
void __fastcall SectorListDestruct(SectorList* list) { DestroySectorList(list); }
EI_REPLACE(0x0056DEF0, SectorListDestruct);

EI_REPLACE(0x00572B30, SectorListsDelete);
EI_REPLACE(0x00572C50, SectorListsDelete);

void SectorGrid::Allocate() {
    const int count = unknown08_ * unknown0C_;
    auto* block = static_cast<int*>(gamecrt::operator_new(static_cast<std::size_t>(count) * sizeof(SectorList) + 4));
    SectorList* cells = nullptr;
    if (block) {
        *block = count;
        cells = reinterpret_cast<SectorList*>(block + 1);
        for (int i = 0; i < count; ++i) cells[i].Construct();
    }
    cells_ = cells;
    rows_ = static_cast<SectorList**>(gamecrt::operator_new(static_cast<std::size_t>(unknown0C_) * sizeof(SectorList*)));
    for (int y = 0; y < unknown0C_; ++y) rows_[y] = cells_ + y * unknown08_;
}
EI_REPLACE_METHOD(0x00572820, SectorGrid::Allocate);
EI_REPLACE_METHOD(0x005728D0, SectorGrid::Allocate);

void SectorGrid::Resize(int width, int height) {
    if (unknown08_ == width && unknown0C_ == height) return;
    if (cells_) SectorListsDelete(cells_, nullptr, 3);
    gamecrt::operator_delete(rows_);
    unknown08_ = width;
    unknown0C_ = height;
    Allocate();
}
EI_REPLACE_METHOD(0x00572450, SectorGrid::Resize);
EI_REPLACE_METHOD(0x005724F0, SectorGrid::Resize);

int CWorldClient::Setup() {
    WorldCreateManagers(this);
    void* path;
    CStringConcat(&path, worlddata::DataPathString(), worlddata::TexturesFolderText());
    CStringAssignOriginal(&texturePath_, nullptr, &path);
    CStringDestroy(&path);
    LoadLightsAt(this, nullptr, worlddata::LightsTableText());
    lightingMark_ = 0;
    sounds_ = MakeCounted<CSoundManager>(0x8C0, Construct6B6040);

    // The text resources: the engine object's fields (in the original's order), the table, then
    // the class's constructor body.
    auto* texts = reinterpret_cast<ClientTexts*>(New(sizeof(ClientTexts)));
    if (texts) {
        ei::SetVtable(texts, CEngineObject::kVtableAddress);
        texts->refCount_ = 0;
        texts->id_ = -1;
        texts->prototypeId_ = 0;
        texts->type_ = 0;
        texts->flags_ = 0;
        texts->owner_ = nullptr;
        Construct562A80(texts->table);
        ei::SetVtable(texts, worlddata::kClientTextsVtable);
        Construct4DED20(texts);
    }
    texts_ = texts;
    ++texts->refCount_;

    // The terrain, its constructor inlined up to the class's own body (006C5620).
    auto* terrain = reinterpret_cast<Terrain*>(New(sizeof(Terrain)));
    if (terrain) {
        TransformObjectBase(terrain);
        terrain->pageTextures.data = nullptr;
        terrain->pageTextures.growBy = 0;
        terrain->pageTextures.capacity = 0;
        terrain->pageTextures.size = 0;
        Construct570E10(&terrain->pageMaterials);
        terrain->textF8 = camera::EmptyStringData();
        terrain->textFC = camera::EmptyStringData();
        Construct570E40(&terrain->materials);
        Construct56FD30(&terrain->textureClass);
        Construct56FD30(&terrain->textureFrames);
        MmioResourceConstructOriginal(&terrain->archive);
        Construct570E70(&terrain->vertexTiles);
        Construct570EA0(&terrain->sectorCache, nullptr, 10);
        Construct571050(&terrain->lights);
        Construct570DE0(&terrain->wavePhases);
        Construct56FD30(&terrain->visibleColumnFirst);
        Construct56FD30(&terrain->visibleColumnLast);
        terrain->textAE0 = camera::EmptyStringData();
        Construct5711C0(&terrain->waterLevels);
        ei::SetVtable(terrain, worlddata::kTerrainVtable);
        static_cast<Terrain*>(terrain)->Reset();
    }
    terrain_ = terrain;
    ++terrain->refCount_;
    CStringAssignTextOriginal(&zoneName_, nullptr, worlddata::GipathText());

    manager44_ = MakeCounted<CBaseManager>(0xB0, Construct42D690);
    static_cast<CBaseManager*>(manager44_)->Init(this);
    textureManager_ = MakeCounted<CTextureManager>(0xF0, Construct691140);
    textureManager_->CBaseManager::InitImpl(this);   // 004031B0 called directly
    manager188_ = MakeCounted<CBaseManager>(0xD0, Construct68E3F0);
    manager188_->Init(this);
    manager190_ = MakeCounted<CBaseManager>(0xD8, Construct68D850);
    manager190_->Init(this);

    auto* manager = reinterpret_cast<CParticleManager*>(New(sizeof(CParticleManager)));
    if (manager) {
        ManagerConstructBaseOriginal(manager);
        Construct56FD40(&manager->freeParticles, nullptr, 10);
        Construct5725E0(&manager->held_);
        manager->maxHeldId_ = 0;
        ei::SetVtable(manager, worlddata::kParticleManagerVtable);
        Construct4981A0(manager);
    }
    particleManager_ = manager;
    ++manager->refCount_;
    manager->Init(this);

    session_ = static_cast<std::uint8_t*>(SessionFor(players_));

    auto* handle = reinterpret_cast<CRefObject*>(New(sizeof(CRefObject)));
    handle->refs_ = 0;   // (refs and flags written before the pointer is tested)
    handle->flags6_ = 0;
    handle->flags7_ = 0;
    CRefObject* created = nullptr;
    if (handle) {
        RefValueConstructBase(handle);
        ei::SetVtable(handle, worlddata::kClientHandleVtable);
        created = handle;
    }
    if (created != handle_.ptr) {
        if (CRefObject* old = handle_.ptr) {
            if (old->refs_ != 1) {
                old->flags7_ |= CRefCounted::kDetached;
                old->Shutdown();
            }
            handle_.ptr->Release();
        }
        handle_.Share(created);
    }
    terrain_->Create();
    sounds_->Create();
    texts_->Create();
    CStringConcat(&path, worlddata::TextPathString(), worlddata::TextsFileText());
    LoadTexts(texts_, nullptr, static_cast<const char*>(path));
    CStringDestroy(&path);
    CStringConcat(&path, worlddata::TextPathString(), worlddata::TextsLmpFileText());
    LoadTexts(texts_, nullptr, static_cast<const char*>(path));
    CStringDestroy(&path);

    // The list manager: the base, then its CList's fields (in the original's order).
    auto* lists = reinterpret_cast<ClientListManager*>(New(sizeof(ClientListManager)));
    if (lists) {
        ManagerConstructBaseOriginal(lists);
        lists->list.vtable = reinterpret_cast<void*>(worlddata::kClientListVtable);
        lists->list.count = 0;
        lists->list.free = nullptr;
        lists->list.tail = nullptr;
        lists->list.head = nullptr;
        lists->list.blocks = nullptr;
        lists->list.blockSize = 10;
        ei::SetVtable(lists, worlddata::kClientListManagerVtable);
    }
    listManager_ = lists;
    lists->Init(this);
    auto* arrays = reinterpret_cast<ClientArrayManager*>(New(sizeof(ClientArrayManager)));
    if (arrays) {
        ManagerConstructBaseOriginal(arrays);
        arrays->current = nullptr;
        Construct5715D0(&arrays->list, nullptr, 10);
        ei::SetVtable(arrays, worlddata::kClientArrayManagerVtable);
    }
    arrayManager_ = arrays;
    arrays->Init(this);
    std::uint8_t* memory = New(0x58);
    auto* owned = static_cast<CMusicSystem*>(memory ? Construct6B4350(memory) : nullptr);
    music_ = owned;
    owned->Create();

    RainSnow()->PrepareBuffers();
    RainSnow()->world = this;
    platforms_.Engine()->Create();
    if (worlddata::ShadowSetting() > 0)
        flags_ |= 0x200;
    else
        flags_ &= ~0x200u;
    CStringConcat(&path, worlddata::DataPathString(), worlddata::CursorFileText());
    cursor_ = LoadCursorFromFileA(static_cast<const char*>(path));
    CStringDestroy(&path);

    // A reference on every object of the manager of slot 22 (the original's iterator is on the heap).
    if (SpellManager()) ReferenceEveryObject(SpellManager());

    // Room for 400 screen entries (reserve).
    ScreenHits& entries = hits_;
    if (static_cast<std::uint32_t>(entries.eos - entries.start) < 400) {
        int count = static_cast<int>(entries.finish - entries.start);
        ScreenEntry* block;
        if (entries.start) {
            block = EntriesAllocateCopy(&entries, nullptr, 400, entries.start, entries.finish);
            if (ScreenEntry* old = entries.start)
                stlp::deallocate(old, static_cast<std::size_t>(entries.eos - old) * sizeof(ScreenEntry));
        } else {
            block = reinterpret_cast<ScreenEntry*>(New(400 * sizeof(ScreenEntry)));
        }
        entries.start = block;
        entries.finish = block + count;
        entries.eos = block + 400;
    }
    unused84_ = -100000;
    cave_ = 0;
    partyDanger_ = 0;
    GridToFour(secondGrid_);
    GridToFour(objectGrid_);
    ticksRun_ = 0;
    lightLevelsCountdown_ = 0;
    return 1;
}

// ---- References for a zone load (005630B0) -------------------------------------------------------

void CWorldClient::ReferenceItemsAndSpells() {
    if (ItemFactory()) ReferenceEveryObject(static_cast<CBaseManager*>(ItemFactory()));
    // The original tests the item factory again before walking the spells.
    if (ItemFactory()) ReferenceEveryObject(SpellManager());
}

namespace {
// Every object of `manager` released (a heap iterator, as the original).
void ReleaseEveryObject(CBaseManager* manager) {
    auto* walk = reinterpret_cast<ObjectListIterator*>(New(sizeof(ObjectListIterator)));
    if (walk) walk->manager = nullptr;
    for (CEngineObject* object = walk->First(manager); object; object = walk->Next()) object->Release();
    if (walk) {
        if (CBaseManager* held = walk->manager) {
            --held->refCount_;
            walk->manager = nullptr;
        }
        gamecrt::operator_delete(walk);
    }
}
} // namespace

void CWorldClient::ReleaseItemsAndSpells() {
    if (ItemFactory()) {
        ReleaseEveryObject(static_cast<CBaseManager*>(ItemFactory()));
        static_cast<CBaseManager*>(ItemFactory())->DestroyAll();
    }
    if (SpellManager()) {
        ReleaseEveryObject(SpellManager());
        SpellManager()->DestroyAll();
    }
}

// ---- The item and spell blocks of a zone's state (00567A60, 00567950, 005679A0; A-W13) -----------

int CWorldClient::LoadItemsAndSpells(CStream* block) {
    ReleaseItemsAndSpells();
    if (block->Size() != 0) {
        manager4C_->Load(block);
        itemManager_->Load(block);
        static_cast<CBaseManager*>(manager4C_)->LoadObjects(block);
        static_cast<CBaseManager*>(itemManager_)->LoadObjects(block);
        ReferenceItemsAndSpells();
    }
    return 1;
}
EI_REPLACE_METHOD(0x00567A60, CWorldClient::LoadItemsAndSpells);

int __fastcall LoadManagerObjects(CBaseManager* manager, CStream* stream) {
    for (;;) {
        std::uint8_t more;
        stream->Read(&more, 1);
        if (more == 0) return 1;
        if (!stream->ok_) return 0;
        int id;
        int type;
        std::uint32_t extra;
        stream->Read(&id, 4);
        stream->Read(&type, 4);
        stream->Read(&extra, 4);
        if (CEngineObject* old = manager->FindObject(static_cast<unsigned>(id))) {
            if (old->refCount_ == 0) old->refCount_ = 1;
            old->Release();
        }
        CEngineObject* object = manager->CreateObject(type, id);
        object->prototypeId_ = extra;
        object->Load(stream);
        ++object->refCount_;
    }
}
EI_REPLACE(0x005679A0, LoadManagerObjects);

int CWorldClient::LoadNewItemsAndSpells(CStream* block) {
    block->Seek(0);
    block->ok_ = 1;
    if (!LoadManagerObjects(SpellManager(), block)) return 0;
    return LoadManagerObjects(static_cast<CBaseManager*>(ItemFactory()), block) != 0;
}
EI_REPLACE_METHOD(0x00567950, CWorldClient::LoadNewItemsAndSpells);

int PlatformSet::Clear() {
    list.RemoveAll();
    return 1;
}
EI_REPLACE_METHOD(0x00561800, PlatformSet::Clear);

void PlatformSet::DestructBody() {
    list.vtable = reinterpret_cast<void*>(worlddata::kPlatformListVtable);
    list.RemoveAll();
    Engine()->DestructBase();
}
EI_REPLACE_METHOD(0x00561A60, PlatformSet::DestructBody);

PlatformSet* PlatformSet::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00561AE0, PlatformSet::DeletingDestruct);

// 0055E8A0 (ECX the list, EDX the stream; the items a player is given, CPlayerServer::SendItemsGiven).
// What LoadManagerObjects reads back: per object a 1, its id, type and +0x18, then its own Save;
// a 0 ends the list.
void __fastcall SaveListedObjects(mfc::CList<CEngineObject*>* list, CStream* stream) {
    for (auto* node = list->head; node;) {
        CEngineObject* object = node->data;
        node = node->next;
        std::uint8_t more = 1;
        stream->Write(&more, 1);
        int id = object->id_;
        stream->Write(&id, 4);
        int type = object->type_;
        stream->Write(&type, 4);
        std::uint32_t extra = object->prototypeId_;
        stream->Write(&extra, 4);
        object->Save(stream);
    }
    std::uint8_t more = 0;
    stream->Write(&more, 1);
}
EI_REPLACE(0x0055E8A0, SaveListedObjects);

void ClientListManager::DestructBody() {
    list.vtable = reinterpret_cast<void*>(worlddata::kClientListVtable);
    for (auto* node = list.head; node; node = node->next)
        if (node->data) node->data->Release();
    list.RemoveAll();
    CBaseManager::DestructBody();
}
EI_REPLACE_METHOD(0x00562EC0, ClientListManager::DestructBody);

ClientListManager* ClientListManager::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00562EA0, ClientListManager::DeletingDestruct);

void ClientArrayManager::DestructBody() {
    ei::SetVtable(this, worlddata::kClientArrayManagerVtable);
    ClientArrayManagerClearOriginal(this);
    list.vtable = reinterpret_cast<void*>(worlddata::kClientArrayListVtable);
    list.RemoveAll();
    if (current) current->Release();
    CBaseManager::DestructBody();
}
EI_REPLACE_METHOD(0x00562DF0, ClientArrayManager::DestructBody);

ClientArrayManager* ClientArrayManager::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00562DD0, ClientArrayManager::DeletingDestruct);

// ---- The zone's state block (00567AB0, A-W15) ----------------------------------------------------

namespace {

// The callees only this load reaches, through their addresses: their verify wrappers keep running
// when this rewrite is installed (the vanilla launch).
__declspec(naked) void __fastcall FlowUpdateAndAcknowledgeAt(GameFlow*) EI_ORIGINAL(0x0057F150)
__declspec(naked) void __fastcall ExitReadAt(ZoneExit*, void*, CStream*) EI_ORIGINAL(0x004E5750)
__declspec(naked) void __fastcall ReadUnitLooksAt(CStream*, void* /*figured*/, void* /*dressed*/) EI_ORIGINAL(0x0055A190)
__declspec(naked) void __fastcall CreateExitMarkersAt(CWorldClient*) EI_ORIGINAL(0x00567500)
__declspec(naked) void __fastcall ViewAppendReceivedAt(CPlayerClient*, void*, void* /*source*/) EI_ORIGINAL(0x00667B40)
__declspec(naked) void __fastcall BuildUnitSkinsAt(CWorldClient*, void*, const void* /*figured*/, const void* /*dressed*/)
    EI_ORIGINAL(0x00568800)

constexpr int kSkyTextureKind = 0x3D;
// The loading movie's progress steps of the zone load.
constexpr int kProgressBlockRead = 8;
constexpr int kProgressZoneCleared = 9;
constexpr int kProgressMapFreed = 10;
constexpr int kProgressZoneReady = 11;
// A cave's reverb: room type, level and reflection time.
constexpr float kCaveReverbRoom = 0.3f;
constexpr float kCaveReverbLevel = 1.0f;
constexpr float kCaveReverbTime = 3.0f;
// The LightingFreq option counts milliseconds; the terrain counts frames of this many.
constexpr int kFrameMilliseconds = 66;
// Terrain flags the options and the cave decide.
constexpr std::uint32_t kLandscapeWaves = 0x01;
constexpr std::uint32_t kLandscapeSelfShadowing = 0x10;
constexpr int kCave = 1;

// A sector grid sized for the terrain, in cells of 16: emptied to one cell first, the new size
// stored, then allocated.
void FitGrid(SectorGrid& grid, const Terrain* terrain) {
    const std::uint32_t height = static_cast<std::uint32_t>(x87::fistp(terrain->height));
    const std::uint32_t width = static_cast<std::uint32_t>(x87::fistp(terrain->width));
    grid.Resize(1, 1);
    grid.height_ = height >> 4;
    grid.width_ = width >> 4;
    grid.Resize(static_cast<int>(width >> 4), static_cast<int>(height >> 4));
}

// The zone of the local player as the map database knows it, or null.
MapRecord* LocalZoneRecord() {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* zone = LocalPlayerName(reinterpret_cast<mfc::CString*>(storage));
    MapRecord* record = GameMapDatabase()->FindMap(*zone);
    zone->~CString();
    return record;
}

// Whether the local player is in the last zone ("gz20g", compared as it is).
bool InFinalZone() {
    alignas(mfc::CString) std::uint8_t storage[sizeof(mfc::CString)];
    mfc::CString* zone = LocalPlayerName(reinterpret_cast<mfc::CString*>(storage));
    const bool final = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*zone)),
                                       reinterpret_cast<const unsigned char*>(worlddata::FinalZoneText())) == 0;
    zone->~CString();
    return final;
}

} // namespace

int CWorldClient::LoadZone(CStream* block) {
    FlowUpdateAndAcknowledgeAt(TheGameFlow());
    mfc::CString map;
    mfc::CString mob;
    sounds_->SetEffectsVolume(0);
    sounds_->StopAll(1);
    partyDanger_ = 0;

    // The block: the tick, the map and .mob names, the exits, the cave flag, the unit looks.
    block->Read(&tick_, 4);
    block->ReadInto(&map);
    block->ReadInto(&mob);
    int exitCount;
    block->Read(&exitCount, 4);
    ZoneExitsSetSizeOriginal(&exitsVtable_, nullptr, exitCount, -1);
    for (int i = 0; i < exitCount_; ++i) ExitReadAt(&exits_[i], nullptr, block);
    int cave;
    block->Read(&cave, 4);
    UnitLooks looks;
    ReadUnitLooksAt(block, &looks.figured, &looks.dressed);
    cave_ = cave;
    GameEnsureZone(map, mob);
    ShowLoadingMovie(kProgressBlockRead);

    // The old zone goes.
    ClearGroundPrints(bloodPrints_, footprints_, scorchMarks_);
    ResetZoneObjects();
    ClearMaps();
    ReleaseItemsAndSpells();
    ClientParticleManager()->ReleaseUnheld();
    Session()->gameKind = 0;
    ShowLoadingMovie(kProgressZoneCleared);
    Terrain* terrain = terrain_;
    if (map.GetLength() > 1 && map[1] != ':') {
        mfc::CString folder;
        modfolders::SelectMaps(map, modfolders::MapsSite::ClientLoadZone);
        MapsFolderAt(&folder);
        map = folder + map;
    }
    terrain->ClearMap();
    // The original fetches the texture manager (slot 34) for both calls; the first does not use it.
    DeleteComposedTextures();
    TextureManager()->FreeCache();
    ShowLoadingMovie(kProgressMapFreed);

    // The allod of the local player's zone names the texture pages, the lights and the music.
    const MapRecord* zone = LocalZoneRecord();
    mfc::CString& allod = *reinterpret_cast<mfc::CString*>(&zoneName_);
    allod = worlddata::DefaultAllodText();
    if (zone) allod = *reinterpret_cast<const mfc::CString*>(&zone->allod);
    allod.MakeLower();
    if (!LandscapeLoadMapOriginal(terrain, nullptr, map, allod, cave == kCave))
        return game::FatalError(worlddata::CantLoadTerrainFormat(), static_cast<const char*>(map));

    FitGrid(objectGrid_, terrain);
    FitGrid(secondGrid_, terrain);
    if (!worlddata::WaterWavesOption()) terrain->landscapeFlags &= ~kLandscapeWaves;
    if (cave == kCave) terrain->landscapeFlags |= kLandscapeWaves;
    if (!worlddata::SelfShadowingOption()) terrain->landscapeFlags &= ~kLandscapeSelfShadowing;
    terrain->SetLightInterval(worlddata::LightingFrequencyOption() / kFrameMilliseconds);
    if (skyDome_) {
        auto* holder = reinterpret_cast<CTexture**>(&skyDome_->texture);
        if (cave == kCave) {
            TextureHolderLoad(holder, nullptr, kSkyTextureKind, worlddata::CaveSkyText());
            skyDome_->turns = 0;
        } else {
            TextureHolderLoad(holder, nullptr, kSkyTextureKind, worlddata::OpenSkyText());
            skyDome_->turns = 1;
        }
    }
    mfc::CString lights(worlddata::LightsPrefixText());
    const bool effects = worlddata::AudioOption() && worlddata::VolumeSettingA();
    if (cave_ == kCave) {
        lights += worlddata::CaveText();
        if (effects) sounds_->SetReverb(kCaveReverbRoom, kCaveReverbLevel, kCaveReverbTime);
    } else if (effects) {
        sounds_->reverb[0] = 0.0f;
    }
    {
        mfc::CString table = lights + allod;
        mfc::CString file = table + worlddata::IniSuffixText();
        LoadLightsAt(this, nullptr, file);
    }
    CreateExitMarkersAt(this);
    worlddata::LastLitHour() = -1.0f;
    UpdateLighting();
    if (worlddata::AudioOption() && worlddata::MovieVolumeSetting()) {
        if (InFinalZone()) music_->SetZoneMode(worlddata::FinalMusicText(), cave_ == kCave);
        else music_->SetZoneMode(allod, cave_ == kCave);
    }

    // The local server's pending messages follow this block in the view's stream.
    ViewAppendReceivedAt(Session() ? Session()->LocalClient() : nullptr, nullptr, GameObject7C2D60());
    ticksRun_ = 0;
    BuildUnitSkinsAt(this, nullptr, &looks.figured, &looks.dressed);
    // The wind option that is neither 0 nor a NaN sets the waves.
    const float wind = worlddata::DefaultWindForceOption();
    if (wind < 0.0f || wind > 0.0f) camera::TheTerrain()->SetWaveStrength(wind);
    ShowLoadingMovie(kProgressZoneReady);
    (Session() ? Session()->LocalClient() : nullptr)->SendResume();
    CloseLoadingMovie();
    return 1;
}
EI_REPLACE_METHOD(0x00567AB0, CWorldClient::LoadZone);

EI_REPLACE_METHOD(0x00561E50, CWorldClient::Setup);
EI_REPLACE_METHOD(0x00560650, PlatformSet::AddFace);
EI_REPLACE_METHOD(0x00560820, PlatformSet::AddBox);
EI_REPLACE_METHOD(0x00563290, CWorldClient::Teardown);
EI_REPLACE_METHOD(0x00565090, CWorldClient::ResetZoneObjects);
EI_REPLACE_METHOD(0x00566100, CWorldClient::Deselect);
EI_REPLACE_METHOD(0x005630B0, CWorldClient::ReferenceItemsAndSpells);
EI_REPLACE_METHOD(0x00563190, CWorldClient::ReleaseItemsAndSpells);
EI_REPLACE_METHOD(0x0056DAF0, CWorldClient::ObjectsInRange);
EI_REPLACE_METHOD(0x00566F70, CWorldClient::CreatePartyUnits);
EI_REPLACE_METHOD(0x005647E0, CWorldClient::AddObject);
EI_REPLACE_METHOD(0x005651A0, CWorldClient::RefreshPlatforms);
EI_REPLACE_METHOD(0x0056D7E0, CWorldClient::QueueSynchronised);
EI_REPLACE_METHOD(0x0056D7A0, CWorldClient::DropShadows);
EI_REPLACE_METHOD(0x00564BE0, CWorldClient::RemoveObject);
EI_REPLACE_METHOD(0x00564E70, CWorldClient::ClearMaps);
EI_REPLACE(0x005726B0, ReleaseIfIdentified);
EI_REPLACE_METHOD(0x00565790, CWorldClient::PickAt);
EI_REPLACE_METHOD(0x005659D0, CWorldClient::PickAt2);
EI_REPLACE_METHOD(0x00565E40, CWorldClient::UnitAt);
EI_REPLACE_METHOD(0x00565D00, CWorldClient::UnitAt2);
EI_REPLACE_METHOD(0x005656A0, CWorldClient::UnitsInBox);
EI_REPLACE_METHOD(0x0056D740, CWorldClient::MarkFootprintsInSight);
EI_REPLACE_METHOD(0x00564190, CWorldClient::ApplyWind);
EI_REPLACE_METHOD(0x005662B0, CWorldClient::CentreOn);
EI_REPLACE_METHOD(0x00578A60, CWorldClient::ExitTarget);
EI_REPLACE_METHOD(0x005618E0, CWorldClient::ClientGroundHeight);
EI_REPLACE_METHOD(0x00561930, CWorldClient::ClientSurfaceHeight);
EI_REPLACE_METHOD(0x0056CE40, CWorldClient::ShowNumber);
EI_REPLACE_METHOD(0x0056DE80, CWorldClient::LoadUnitFaces);
EI_REPLACE_METHOD(0x00567400, CWorldClient::RemoveUnitsOf);
EI_REPLACE_METHOD(0x00566330, CWorldClient::SendFlag10);
EI_REPLACE_METHOD(0x00570860, SectorGrid::Move);
EI_REPLACE_METHOD(0x00570BB0, SectorGrid::Move2);
EI_REPLACE(0x00571D50, MarkWithinRadius);
EI_REPLACE(0x00571800, CollectWindowsAt);
EI_REPLACE(0x005718B0, CollectWindowsAt2);
EI_REPLACE(0x0056FC60, PendingMapLookup);
EI_REPLACE(0x0056E400, RecordAt50);
EI_REPLACE(0x0056E440, RecordAtB4);
