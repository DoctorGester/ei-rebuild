// The client world's object maps (helpers and the CWorldClient methods that keep the maps). See
// docs/notes/world.md ("Client maps"). The client files every object in a sector grid (STLport lists of
// counted pointers, one per 16x16 cell) and a second grid; most of the helpers are the STLport and MFC
// instantiations behind those maps (config/library.csv).
#include "ground_print.h"
#include "item_db_text.h"
#include "hash_tables_400.h"
#include "item.h"
#include "lightning.h"
#include "map_object_shadow.h"
#include "player_manager.h"
#include "player_messages.h"
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
#include "material.h"
#include "model.h"
#include "sound.h"
#include "spell.h"
#include "stl_sorts.h"
#include "terrain.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"
#include "world_deleting_destructors.h"
#include "world_effect.h"
#include "mod_folders.h"
#include "world_map_files.h"
#include "world_glue.h"
#include "unit_looks.h"
#include "bink_video.h"
#include "object_tables.h"
#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_list_refs.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "player_client.h"
#include "ui_flying_hit_points.h"
#include "unit_client_models.h"
#include "unit_create.h"
#include "game_arrays.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_string.h"
#include "game/game_slots.h"
#include "game/record_arrays.h"
#include "system_text.h"
#include "lib/str.h"

#include <new>
#include <string>
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// The cell index of a coordinate: FISTP (round to nearest), then an unsigned shift by 4, so a
// negative coordinate gives a huge index that fails the bounds tests.
std::uint32_t CellOf(float coordinate) { return static_cast<std::uint32_t>(x87::fistp(coordinate)) >> 4; }

// The list operations of the two grids' instantiations (thiscall on the cell's list): erase, end and insert
// on the held objects' list (counted values).
using HeldOps = stlp::ListAbi<CMapObject*, stlp::HeldObject>;
struct GridOpsA {
    static void Erase(SectorList*, SectorListNode** out, SectorListNode* node) {
        *out = SectorList::Erase<stlp::HeldObject>(node);
    }
};
using GridOpsB = GridOpsA;

void HoldRef(CMapObject* object) {
    if (object) ++object->refCount_;
}
void DropRef(CMapObject* object) {
    if (object) object->Release();
}

template <typename Ops>
void MoveInGrid(SectorGrid* grid, CMapObject* object, const float* position) {
    float* stored = &object->localPosition_.x;   // x, y, z
    std::uint32_t oldX = CellOf(stored[0]);
    std::uint32_t oldY = CellOf(stored[1]);
    std::uint32_t newX = CellOf(position[0]);
    std::uint32_t newY = CellOf(position[1]);
    if (oldX == newX && oldY == newY) {
        std::memcpy(stored, position, 12);
        return;
    }
    if (oldX < grid->width_ && oldY < grid->height_) {
        HoldRef(object);
        SectorList* list = &grid->rows_[oldY][oldX];
        SectorListNode* head = list->head;
        SectorListNode* node = head->next;
        while (node != head) {
            SectorListNode* next = node->next;
            if (node->data == object) {
                SectorListNode* ignored;
                Ops::Erase(list, &ignored, node);
            }
            node = next;
        }
        DropRef(object);
    }
    std::memcpy(stored, position, 12);
    if (newX < grid->width_ && newY < grid->height_) {
        CMapObject* value = object;
        HoldRef(value);
        SectorList* list = &grid->rows_[newY][newX];
        SectorList::Insert<stlp::HeldObject>(list->End(), value);
        DropRef(value);
    }
}

} // namespace

void SectorGrid::Move(CMapObject* object, const float* position) {
    MoveInGrid<GridOpsA>(this, object, position);
}

void SectorGrid::Move2(CMapObject* object, const float* position) {
    MoveInGrid<GridOpsB>(this, object, position);
}

RadiusFunctor* RadiusFunctor::Mark(stlp::list<Footprint>::Node* first, stlp::list<Footprint>::Node* last, float px,
                                   float py, float factor) {
    for (stlp::list<Footprint>::Node* node = first; node != last; node = node->next) {
        Footprint& print = node->data;
        double dx = print.position_.x - px;
        double dy = print.position_.y - py;
        double distance = libcrt::Hypot(dx, dy);
        float limit = print.scale_ * factor;
        EI_NAN_AUDIT(distance, limit);
        if (distance <= static_cast<double>(limit)) print.visible_ = 1;
    }
    x = px;
    y = py;
    scale = factor;
    return this;
}
EI_REWRITTEN(0x00571D50, RadiusFunctor::Mark);


void ScreenHits::InsertOverflow(ScreenEntry* position, const ScreenEntry* value, int count) {
    stlp::vector<ScreenEntry>::InsertOverflow<stlp::PlainElement<ScreenEntry>>(position, *value,
                                                                              static_cast<std::size_t>(count));
}

namespace {

// The hit test of the object's side half for the point: CollectBounds or CollectParts.
using HitTest = int (CObject3DSpecific::*)(const long* point);

// The entries of the screen list's nodes [first, last) whose rectangle holds (x, y) and whose
// object still exists and answers `test` go to `hits`.
HitCollector* CollectHits(HitCollector* out, ScreenEntryNode* first, ScreenEntryNode* last, ScreenHits* hits, LONG x,
                          LONG y, CMapObjectManager* manager, HitTest test) {
    for (ScreenEntryNode* node = first; node != last; node = node->next) {
        const ScreenEntry* entry = &node->data;
        POINT pt = {x, y};
        if (!PtInRect(&entry->rect, pt)) continue;
        CMapObject* object = manager->FindMapObject(entry->objectId);
        if (!object) continue;
        LONG point[2] = {x, y};
        CObject3DSpecific* half = object->Object3D()->Specific();
        if (!(half->*test)(point)) continue;
        hits->PushBack(entry);
    }
    out->hits = hits;
    return out;
}

} // namespace

HitCollector* HitCollector::CollectBounds(ScreenEntryNode* first, ScreenEntryNode* last, ScreenHits* found, LONG x,
                                          LONG y, CMapObjectManager* manager) {
    return CollectHits(this, first, last, found, x, y, manager, &CObject3DSpecific::BoundsContainPoint);
}
EI_REWRITTEN(0x00571800, HitCollector::CollectBounds);

HitCollector* HitCollector::CollectParts(ScreenEntryNode* first, ScreenEntryNode* last, ScreenHits* found, LONG x,
                                         LONG y, CMapObjectManager* manager) {
    return CollectHits(this, first, last, found, x, y, manager, &CObject3DSpecific::PartsContainPoint);
}
EI_REWRITTEN(0x005718B0, HitCollector::CollectParts);

int PendingIndex::Lookup(const std::uint32_t* key, mfc::CList<CMapObject*>::Node** value) const {
    const Node* node = Find(*key);
    if (!node) return 0;
    *value = node->value;
    return 1;
}
EI_REWRITTEN(0x0056FC60, PendingIndex::Lookup);


namespace {

// The plain STLport lists of pointers (heroes_, objects_, list7BC_, screenEntries_): end, insert, begin, erase.
using PlainList = SectorList;
using FiledNode = MapObjectList::Node;
struct PlainOps {
    static void Erase(SectorList*, SectorListNode** out, SectorListNode* node) {
        *out = SectorList::Erase(node);
    }
    static void Erase(MapObjectList*, FiledNode** out, FiledNode* node) { *out = MapObjectList::Erase(node); }
};

// The sector grid's cell list (held objects): remove(counted value), push_back, clear; the second grid's clear.
struct CellOps {
    static void Remove(SectorList* list, CMapObject* const& value) { stlp::ListRemoveHeld(list, value); }
    static void PushBack(SectorList* list, CMapObject** value) {
        SectorList::Insert<stlp::HeldObject>(list->head, *value);
    }
    static constexpr auto Clear = HeldOps::Clear;
    static constexpr auto Clear2 = HeldOps::Clear;
};

// The client's MFC CList<CMapObject*> (dynamicObjects_): NewNode, FreeNode; the platform list's RemoveAt.
using DynamicList = mfc::CList<CMapObject*>;
using DynamicNode = DynamicList::Node;

void __fastcall SessionPlayerClear(CPlayer* session) { session->Clear(); }

constexpr int kClassPlatform54 = 0x54;
constexpr int kClassPlatform5D = 0x5D;
constexpr int kTypeHero = 0x32;

SectorList* CellFor(SectorGrid& grid, CMapObject* object) {
    std::uint32_t x = CellOf(object->localPosition_.x);
    std::uint32_t y = CellOf(object->localPosition_.y);
    if (x >= grid.width_ || y >= grid.height_) return nullptr;
    return &grid.rows_[y][x];
}

} // namespace

int CWorldClient::AddObject(CMapObject* object) {
    MapObjectList& objects = objects_;
    for (FiledNode* node = objects.head->next; node != objects.head; node = node->next) {
        if (node->data->id_ == object->id_) {
            ++object->refCount_;
            object->Release();
            return 1;
        }
    }
    const PendingMap::Node* entry = dynamicNodes_.Find(static_cast<std::uint32_t>(object->id_));
    if (!entry && object->IsSynchronised()) {
        DynamicList& kept = dynamicObjects_;
        DynamicNode* node = kept.NewNode(kept.tail, nullptr);
        node->data = object;
        if (kept.tail)
            kept.tail->next = node;
        else
            kept.head = node;
        kept.tail = node;
        DynamicNode* tail = kept.tail;
        int key = object->id_;
        hash400::Entry(dynamicNodes_, dynamicMaxId_, key) = tail;
    }
    int kind = object->GetClassId();
    if (kind == kClassPlatform54 || object->GetClassId() == kClassPlatform5D) {
        CObject3D* figure = object->Object3D();
        for (int i = 0; i < figure->parts.size; ++i) {
            if ((figure->PartBox(i)->flags >> 10) & 1) {
                int id = object->id_;
                platforms_.AddBox(id, figure->PartBox(i), &object->localPosition_, &object->localRotation_);
            }
        }
    }
    CUnit* unit = UnitOrNull(object);
    if (unit && Session()->sessionKind100_ == 1) {
        if (unit->player == nullptr && ticksRun_ >= 1) unit->Controller()->RestartFadeIn();
    }

    ++object->refCount_;
    MapObjectList::Insert(objects.End(), object);

    SectorGrid& grid = objectGrid_;
    if (SectorList* cell = CellFor(grid, object)) {
        CMapObject* value = object;
        HoldRef(value);
        CellOps::Remove(cell, value);
        DropRef(value);
    }
    if (SectorList* cell = CellFor(grid, object)) {
        CMapObject* value = object;
        HoldRef(value);
        CellOps::PushBack(cell, &value);
        DropRef(value);
    }
    if (object->type_ == kTypeHero && (IsTextKey(object->id_) & 0xFF)) {
        CMapObject* value = object;
        HoldRef(value);
        SectorList& heroes = heroes_;
        SectorList::Insert<stlp::HeldObject>(heroes.End(), value);
        DropRef(value);
    }
    return 1;
}

void CWorldClient::RefreshPlatforms(CMapObject* object) {
    const int id = object->id_;
    MapObjectList& objects = objects_;
    FiledNode* filed = objects.head->next;
    while (filed != objects.head && filed->data->id_ != id) filed = filed->next;
    if (filed == objects.head) return;
    PlatformList& platforms = platforms_.list;
    for (PlatformNode* node = platforms.head; node;) {
        PlatformNode* next = node->next;
        if (node->data.id == id) platforms.RemoveAt(node);
        node = next;
    }
    if (object->GetClassId() != kClassPlatform54 && object->GetClassId() != kClassPlatform5D) return;
    CObject3D* figure = object->Object3D();
    for (int i = 0; i < figure->parts.size; ++i) {
        if (!((figure->PartBox(i)->flags >> 10) & 1)) continue;
        platforms_.AddBox(object->id_, figure->PartBox(i), &object->localPosition_, &object->localRotation_);
    }
}

bool CWorldClient::QueueSynchronised(CMapObject* object) {
    if (dynamicNodes_.Find(static_cast<std::uint32_t>(object->id_))) return false;
    DynamicList& kept = dynamicObjects_;
    DynamicNode* node = kept.NewNode(kept.tail, nullptr);
    node->data = object;
    if (kept.tail)
        kept.tail->next = node;
    else
        kept.head = node;
    kept.tail = node;
    DynamicNode* tail = kept.tail;
    int key = object->id_;
    hash400::Entry(dynamicNodes_, dynamicMaxId_, key) = tail;
    return true;
}

void CWorldClient::MarkFootprintsInSight(const float* point, float range) {
    RadiusFunctor functor;   // for_each's functor (x, y, range) comes back by value
    functor.Mark(footprints_.First(), footprints_.End(), point[0], point[1], range);
}

void CWorldClient::ApplyWind(const Vector3* direction, float strength) {
    SetWind(direction, strength);
    if (terrain_) {
        Vector3 copy = *direction;
        terrain_->SetWaves(&copy, strength);
    }
    rainSnow_.SetWind(*direction, strength);
}

int CWorldClient::CentreOn(const Vector3* point) {
    Vector3 focus = *point;
    focus.z = camera_->focus_.z;
    camera_->SetFocus(&focus);
    camera_->ApplyView();
    camera_->ClampAboveObjects();
    return 1;
}

double CWorldClient::GroundHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(GridHeightAt(x, y));
    return platforms_.SurfaceHeight(&point, height);
}

double CWorldClient::SurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(RawSurfaceHeightAt(x, y));
    return platforms_.SurfaceHeight(&point, height);
}

void CWorldClient::ShowNumber(int id, int value, std::uint32_t kind) {
    CEngineObject* object = MapObjectManager()->FindObject(static_cast<unsigned>(id));
    if (!object || object->GetClassId() != world::kClassUnit) return;
    if (numbers_) numbers_->Add(id, value, kind);
}

void CWorldClient::LoadUnitFaces() {
    for (FiledNode* node = objects_.head->next; node != objects_.head; node = node->next) {
        // The pointer is tested after the class call (the list holds no null).
        if (CUnit* unit = UnitOrNull(node->data)) unit->ClientController()->LoadFaceTexture();
    }
}

const char* CWorldClient::ExitTarget(int index) { return exits_[index].target.c_str(); }

void CWorldClient::RemoveUnitsOf(const RecordPtrArray* records) {
    for (int i = 0; i < records->size; ++i)
        RemoveObject(static_cast<int>(records->data[i]->unitId));
}

int CWorldClient::SendFlag10(int value) {
    return LocalView()->SendFlag10(static_cast<std::uint8_t>(value));
}

void CWorldClient::DropShadows() {
    MapObjectList& objects = objects_;
    for (FiledNode* node = objects.head->next; node != objects.head; node = node->next) {
        CObject3DClientSpecific* half = node->data->ClientSpecific();
        // Only the low byte is tested (ShadowsShown's "off" leaves the setting's upper bytes).
        if (!static_cast<std::uint8_t>(half->ShadowsShown())) half->ClearShadow();
    }
}

void CWorldClient::RemoveObject(int id) {
    PlatformList& platforms = platforms_.list;
    for (PlatformNode* node = platforms.head; node;) {
        PlatformNode* next = node->next;
        if (node->data.id == id) platforms.RemoveAt(node);
        node = next;
    }
    PendingMap& ids = dynamicNodes_;
    if (const PendingMap::Node* entry = ids.Find(static_cast<std::uint32_t>(id))) {
        DynamicList& kept = dynamicObjects_;
        DynamicNode* node = entry->value;
        if (node == kept.head)
            kept.head = node->next;
        else
            node->prev->next = node->next;
        if (node == kept.tail)
            kept.tail = node->prev;
        else
            node->next->prev = node->prev;
        kept.FreeNode(node);
        if (const PendingMap::Node* again = ids.Find(static_cast<std::uint32_t>(id))) ids.Erase(const_cast<PendingMap::Node*>(again));
    }
    MapObjectList& objects = objects_;
    FiledNode* node = objects.head->next;
    for (;; node = node->next) {
        if (node == objects.head) return;
        if (node->data->id_ == id) break;
    }
    CMapObject* object = node->data;
    if (object->type_ == kTypeHero && (IsTextKey(id) & 0xFF)) {
        CMapObject* value = object;
        HoldRef(value);
        SectorList& heroes = heroes_;
        for (SectorListNode* hero = heroes.head->next; hero != heroes.head;) {
            SectorListNode* next = hero->next;
            if (hero->data == value) {
                SectorListNode* ignored;
                GridOpsA::Erase(&heroes, &ignored, hero);
            }
            hero = next;
        }
        DropRef(value);
        if (Session()->sessionKind100_ == 3) static_cast<CUnit*>(object)->ClientController()->DropFaceTexture();
    }
    if (SectorList* cell = CellFor(objectGrid_, object)) {
        CMapObject* value = object;
        HoldRef(value);
        CellOps::Remove(cell, value);
        DropRef(value);
    }
    FiledNode* ignored;
    PlainOps::Erase(&objects, &ignored, node);
    object->Release();
}

bool ReleaseIdentified::Test(CMapObject* object) {
    if (object->id_ > 0) {
        object->Release();
        return true;
    }
    return false;
}
EI_REWRITTEN(0x005726B0, ReleaseIdentified::Test);

void CWorldClient::ClearMaps() {
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            CellOps::Clear2(&second.rows_[y][x]);
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            CellOps::Clear(&grid.rows_[y][x]);

    // MFC RemoveAll of the platform faces and the dynamic objects (the elements need no destruction).
    platforms_.list.RemoveAll();
    dynamicObjects_.RemoveAll();
    dynamicNodes_.Clear();
    dynamicMaxId_ = 0;

    stlp::list<int>& records = list7BC_;
    for (stlp::list<int>::Node* node = records.head->next; node != records.head;) {
        stlp::list<int>::Node* next = node->next;
        if (node) stlp::deallocate(node, 0xC);
        node = next;
    }
    records.head->next = records.head;
    records.head->prev = records.head;

    PlainList& heroes = heroes_;
    for (SectorListNode* node = heroes.head->next; node != heroes.head;) {
        SectorListNode* next = node->next;
        if (node->data) node->data->Release();
        stlp::deallocate(node, 12);
        node = next;
    }
    heroes.head->next = heroes.head;
    heroes.head->prev = heroes.head;

    MapObjectList& objects = objects_;
    FiledNode* node = objects.head->next;
    FiledNode* end = objects.head;
    ReleaseIdentified functor{};
    while (node != end) {
        FiledNode* next = node->next;
        if (functor.Test(node->data)) {
            FiledNode* ignored;
            PlainOps::Erase(&objects, &ignored, node);
        }
        node = next;
    }
    SessionPlayerClear(Session());
    if (CLinkFigureLoader* loader = figureLoader_) loader->ClearCache();
    manager3C_->FreeSpares();
    manager40_->FreeSpares();
    manager44_->FreeSpares();
}

namespace {

using Collect = HitCollector* (HitCollector::*)(ScreenEntryNode*, ScreenEntryNode*, ScreenHits*, long, long,
                                                CMapObjectManager*);
CMapObject* Pick(CWorldClient* client, const POINT* point, Collect collect) {
    ScreenHits& hits = client->hits_;
    hits.Finish() = hits.Start();
    CMapObjectManager* manager = client->MapObjectManager();
    stlp::list<ScreenEntry>& screen = client->screenEntries_;
    HitCollector functor;
    (functor.*collect)(screen.head->next, screen.head, &hits, point->x, point->y, manager);
    if (hits.begin() == hits.end()) return nullptr;

    // sort(begin, end, DepthLess{top of the view stack, objects}), the introsort loop's first level inlined.
    const float* plane = ScenePipeline()->Top(CTransformPipeline::kViewStack)->m;
    CMapObjectManager* objects = client->MapObjectManager();
    ScreenEntry* first = hits.begin();
    ScreenEntry* last = hits.end();
    if (first != last) {
        int count = static_cast<int>(last - first);
        int depth = 0;
        for (int n = count; n != 1; n >>= 1) ++depth;
        if (count > 16) {
            stl_sorts::ScreenIntroSort(first, last, nullptr, depth * 2, plane, objects);
            stl_sorts::ScreenInsertionSort(first, first + 16, plane, objects);
            stl_sorts::ScreenUnguardedInsertionSort(first + 16, last, nullptr, plane, objects);
        } else {
            stl_sorts::ScreenInsertionSort(first, last, plane, objects);
        }
    }
    ScreenEntry*& cursor = client->hitCursor_;
    for (cursor = hits.begin(); cursor != hits.end(); ++cursor) {
        if (CMapObject* object = manager->FindMapObject(cursor->objectId)) return object;
    }
    return nullptr;
}

} // namespace

CMapObject* CWorldClient::PickAt(const POINT* point) { return Pick(this, point, &HitCollector::CollectBounds); }
CMapObject* CWorldClient::PickAt2(const POINT* point) { return Pick(this, point, &HitCollector::CollectParts); }

namespace {
// From a picked object on: the first unit of the sorted hits. The cursor moves past entries whose
// object is gone or is not a unit; null at the end.
CUnit* FirstUnitHit(CWorldClient* client, CMapObject* object) {
    while (object->GetClassId() != world::kClassUnit) {
        do {
            if (++client->hitCursor_ == client->hits_.end()) return nullptr;
            object = client->manager40_->FindMapObject(client->hitCursor_->objectId);
        } while (!object);
    }
    return static_cast<CUnit*>(object);
}
} // namespace

CUnit* CWorldClient::UnitAt(const POINT* point) {
    if (CMapObject* picked = PickAt2(point)) {
        if (CUnit* unit = FirstUnitHit(this, picked)) return unit;
    }
    CMapObject* picked = PickAt(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

CUnit* CWorldClient::UnitAt2(const POINT* point) {
    CMapObject* picked = PickAt2(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

void CWorldClient::UnitsInBox(const tagRECT* rect, stlp::list<CMapObject*>* out) {
    for (ScreenEntryNode* node = screenEntries_.head->next; node != screenEntries_.head; node = node->next) {
        CMapObject* object = manager40_->FindMapObject(node->data.objectId);
        if (!object) continue;
        if (!object->Object3D()->Specific()->PartsIntersectRect(rect)) continue;
        SectorList::Insert(out->End(), object);
    }
}

namespace {

bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

} // namespace

// The figure's part names (worlddata::HeroPartText): "hd", "bd", "hp", "hr" (the hair: "hr.%.2d"
// with the prototype's number, or left out at -1), "lh1".."rl3".
void CWorldClient::CreatePartyUnits(const RecordPtrArray* party, char allArmour) {
    std::string hair;
    for (int i = 0; i < party->size; ++i) {
        auto* record = party->data[i];
        console::CStringArray parts;
        for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
            const char* name = worlddata::HeroPartText(n);
            if (SameText(name, worlddata::HairPartText())) {
                UnitPrototypeRecord* prototype = unitdb::PrototypeNamed(stlp::string(record->texts[5].c_str()));
                int number = prototype->hair;
                if (number != -1) {
                    hair = str::Format(worlddata::HairFormat(), number);
                    parts.SetAtGrow(parts.GetSize(), hair);
                }
            } else {
                parts.SetAtGrow(parts.GetSize(), name);
            }
        }
        auto* manager = MapObjectManager();
        auto* unit = manager->CreateObjectAs<CUnit>(kTypeHero, static_cast<int>(record->unitId));
        if (!unit) return;   // (leaving the loop is the same as returning here)
        UnitPrototypeRecord* prototype = unitdb::PrototypeTable().FindByName(record->texts[5].c_str());
        unit->prototypeId_ = static_cast<std::uint32_t>(prototype ? prototype - unitdb::PrototypeTable().begin : -1);
        unit->SetPrototype23CByName(record->texts[6].c_str());
        unit->flags_ |= 0x4000000;
        if (!unit->MakeFigure(record->texts[2].c_str(), &parts, 1)) return;
        CObject3DClientSpecific* figure = unit->Object3D()->ClientSpecific();
        figure->RegisterPartMaterials();
        unit->PlayAnimationSecond(0, nullptr);   // (the model name is passed as the unused argument)
        unit->RestartPose();
        const char* skin = record->texts[4].c_str();
        figure->SetSkins(game::NoneText(), (SameText(skin, game::NoneText()) ? nullptr : skin));
        unit->WriteFullState();
        CMapObjectVirtuals* slots = unit;
        slots->SetStats(&record->stats);
        unit->SetFigureScale(&record->scaleX);
        std::memcpy(&unit->MapObject()->baseScale, &record->scaleX, 12);
        slots->ApplyBaseScale();
        unit->ReleaseAllItems();
        for (int j = 0; j < record->weapons.size; ++j)
            unit->AddWeapon(record->weapons[j]);
        CUnitController* controller = unit->Controller();
        if (record->weapons.size != 0) controller->ShowWeapon(0);
        for (int j = 0; j < record->armor.size; ++j) {
            CItem* armour = record->armor[j];
            if (allArmour || AsArmor(armour)->GetType() != 0) unit->EquipArmour(armour);
        }
        for (int j = 0; j < record->quickItems.size; ++j)
            unit->AddQuickItem(record->quickItems[j]);
        unit->Controller()->RefreshEquipment();
        unit->Controller()->RefreshModel();
        slots->SetScriptName(record->texts[0].c_str());
        AddObject(unit);
    }
}

namespace {

// Branch-free selects (C0 shifted into a mask): `a < b` picks one side.
float MaxZero(float b) {
    EI_NAN_AUDIT(b);
    return 0.0f < b ? b : 0.0f;
}
float MinOf(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b ? a : b;
}

std::uint32_t Ftol(float v) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(v)); }

} // namespace

void CWorldClient::ObjectsInRange(const Vector3* point, float radius, stlp::list<CObject3D*>* out) {
    float maxX = terrain_->width - 1.0f;
    float maxY = terrain_->height - 1.0f;
    float y1 = MinOf(MaxZero(point->y + radius), maxY);
    float x1 = MinOf(MaxZero(point->x + radius), maxX);
    float y0 = MaxZero(MinOf(MaxZero(point->y - radius), maxY));
    float x0 = MaxZero(MinOf(MaxZero(point->x - radius), maxX));
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
    for (SectorList* cell = &grid.rows_[y][x]; cell;) {
        for (SectorListNode* node = cell->head->next; node != cell->head; node = node->next) {
            CMapObject* object = node->data;
            float pz = point->z;
            float ox = object->localPosition_.x + object->center.x;
            float oy = object->localPosition_.y + object->center.y;
            double reach = libcrt::Hypot(object->above.x, object->above.y) + radius;
            float dx = ox - point->x;
            float dy = oy - point->y;
            float dz = pz - point->z;
            double distance = LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
            double limit = reach * reach;
            // Skipped when the limit is below (or unordered).
            if (limit >= distance) {
                stlp::list<CObject3D*>::Insert(out->End(), object);
            }
        }
        if (++x > lastX) {
            x = firstX;
            if (++y > lastY) return;
        }
        cell = &grid.rows_[y][x];
    }
}

namespace {
constexpr float kSteepest = 0.8f;   // the smallest normal z of a face added
} // namespace

int PlatformSet::AddFace(const Vector3* a, const Vector3* b, const Vector3* c, const Vector3* normal, Platform** out) {
    *out = nullptr;
    EI_NAN_AUDIT(normal->z);
    if (normal->z < kSteepest) return 0;
    PlatformNode* last = list.head;
    PlatformNode* node = list.NewNode(nullptr, last);
    // (Every field is written below or by the caller; the record's old bytes are not read.)
    if (last)
        last->prev = node;
    else
        list.tail = node;
    list.head = node;
    Platform* record = &node->data;
    *out = record;
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
    float lengthU = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz)));
    record->sizeU = lengthU;
    float lengthV = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(vx, vy, vz)));
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

namespace {

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

// The three ground print lists emptied (Teardown and the zone load inline this): each element destroyed, its
// node returned to the pool.
void ClearGroundPrints(stlp::list<BloodPrint>& blood, stlp::list<Footprint>& feet, stlp::list<GroundPrint>& scorch) {
    EmptyList(blood, [](stlp::list<BloodPrint>::Node* node) {
        node->data.Destruct();
        stlp::deallocate(node, 164);
    });
    EmptyList(feet, [](stlp::list<Footprint>::Node* node) {
        node->data.DeletingDestruct(0);
        if (node) stlp::deallocate(node, sizeof(*node));
    });
    EmptyList(scorch, [](stlp::list<GroundPrint>::Node* node) {
        node->data.DeletingDestruct(0);
        if (node) stlp::deallocate(node, sizeof(*node));
    });
}

template <typename T>
void DeleteOwned(T*& slot) {
    if (CEngineObject* object = slot) {
        delete object;
        slot = nullptr;
    }
}

// A counted engine object: its count dropped (without a release), then deleted the same way.
template <typename T>
void DropAndDelete(T*& slot) {
    if (slot) {
        --slot->refCount_;
        CEngineObject* object = slot;
        if (object) delete object;
        slot = nullptr;
    }
}

} // namespace

void CWorldClient::Destroy() {
    TheCamera()->SetTarget(nullptr);
    if (CFlyingHitPoints* p = numbers_) {
        p->Destruct();
        gamecrt::operator_delete(p);
        numbers_ = nullptr;
    }
    if (CSky* p = skyDome_) {
        p->Destruct();
        gamecrt::operator_delete(p);
        skyDome_ = nullptr;
    }
    DeleteOwned(bird_);
    ReleaseItemsAndSpells();
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(countedA0_).SetSize(0, -1);
    ClearGroundPrints(bloodPrints_, footprints_, scorchMarks_);
    rainSnow_.Destroy();
    platforms_.Destroy();
    platforms_.list.RemoveAll();
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            CellOps::Clear(&grid.rows_[y][x]);
    EmptyList(list7BC_, [](stlp::list<int>::Node* node) { stlp::deallocate(node, 12); });
    EmptyList(heroes_, [](SectorListNode* node) {
        if (node->data) node->data->Release();
        stlp::deallocate(node, 12);
    });
    MapObjectList& objects = objects_;
    stlp::ListForEachRelease<CMapObject>(objects.head->next, objects.head, 0);
    EmptyList(objects, [](FiledNode* node) { stlp::deallocate(node, 12); });
    if (CPlayer* session = Session()) {
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
    CWorld::Destroy();
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

void CWorldClient::ResetZoneObjects() {
    mfc::impl::ArrayAs<gamearrays::ObjectPtrs>(countedA0_).SetSize(0, -1);
    // The second grid's cells: each element released (flags 0), its node freed, the cell linked to itself.
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            second.rows_[y][x].ClearWith([](CMapObject** element) {
                if (CMapObject* object = *element) object->Release();
            });
    ObjectListIterator it{nullptr, nullptr};
    for (CEngineObject* object = it.First(TimedObjectManager()); object; object = it.Next()) object->Release();
    if (bird_) bird_->Reset();
    if (it.manager) --it.manager->refCount_;
}

namespace {
// The selection highlight off: flags 0x10 and 0x100 cleared, the side half's empty virtual and SetLit
// with 0 (its light goes off).
void Unhighlight(CMapObject* object) {
    object->flags_ &= ~0x110u;
    CObject3DSpecific* half = object->attached;
    half->Unused25(0);
    half = object->attached;
    half->SetLit(0);
}
} // namespace

void CWorldClient::Deselect(CMapObject* object) {
    stlp::list<int>& selected = list7BC_;
    using Node = stlp::list<int>::Node;
    auto* objects = manager40_;
    if (!object) {
        for (Node* node = selected.head->next; node != selected.head; node = node->next) {
            CMapObject* found = objects->FindMapObject(static_cast<unsigned>(node->data));
            if (found && (found->flags_ & 0x10)) Unhighlight(found);
        }
        EmptyList(selected, [](Node* node) { stlp::deallocate(node, 12); });
        return;
    }
    const int id = object->id_;
    Node* node = selected.head->next;
    while (node != selected.head && node->data != id) node = node->next;
    if (node == selected.head) return;
    stlp::list<int>::Erase(node);
    if (object->flags_ & 0x10) Unhighlight(object);
}

void CWorldClient::Select(CMapObject* object) {
    if (!object) return;
    object->Object3D()->UpdatePartBounds();
    if (!(object->flags_ & 0x10)) {
        if (!(object->flags_ & 0x100)) object->attached->SetLit(1);
        object->flags_ |= 0x10;
        object->attached->Unused25(1);
    }
    stlp::list<int>& selected = list7BC_;
    using Node = stlp::list<int>::Node;
    const int id = object->id_;
    Node* node = selected.head->next;
    while (node != selected.head && node->data != id) node = node->next;
    if (node != selected.head) return;
    stlp::list<int>::Insert(selected.head, id);
}
EI_REWRITTEN(0x00566030, CWorldClient::Select);

namespace {


std::uint8_t* New(std::size_t size) { return gamecrt::operator_new_array<std::uint8_t>(size); }

// A heap object made by its constructor (null without memory), stored and referenced (the pointer is not tested
// before counting).
template <typename T>
T* MakeCounted() {
    std::uint8_t* memory = New(sizeof(T));
    T* object = memory ? new (memory) T() : nullptr;
    ++object->refCount_;
    return object;
}

// A new object (its class's operator new and constructor), stored and referenced.
template <typename T>
T* NewCounted() {
    T* object = new T();
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

void ReferenceEveryObject(CBaseManager* manager) {
    auto* walk = gamecrt::operator_new_as<ObjectListIterator>();
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

void DestroySectorList(stlp::list<CMapObject*>* list) {
    list->ClearWith([](CMapObject** object) {
        if (*object) (*object)->Release();
    });
    stlp::deallocate(list->head, sizeof(SectorListNode));
}

SectorList* SectorList::VectorDelete(unsigned flags) {
    if (flags & 2) {
        SectorList* cells = this;
        int* block = reinterpret_cast<int*>(cells) - 1;
        for (int i = *block; i-- > 0;) DestroySectorList(&cells[i]);
        if (flags & 1) gamecrt::operator_delete(block);
        return reinterpret_cast<SectorList*>(block);
    }
    DestroySectorList(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
void __fastcall SectorListDestruct(stlp::list<CMapObject*>* list) { DestroySectorList(list); }
EI_REWRITTEN(0x0056DEF0, SectorListDestruct);

EI_REWRITTEN(0x00572B30, SectorList::VectorDelete);
EI_REWRITTEN(0x00572C50, SectorList::VectorDelete);

void SectorGrid::Allocate() {
    const int count = unknown08_ * unknown0C_;
    auto* block = gamecrt::operator_new_bytes_as<int>(static_cast<std::size_t>(count) * sizeof(SectorList) + 4);
    SectorList* cells = nullptr;
    if (block) {
        *block = count;
        cells = reinterpret_cast<SectorList*>(block + 1);
        for (int i = 0; i < count; ++i) cells[i].Construct();
    }
    cells_ = cells;
    rows_ = gamecrt::operator_new_array<SectorList*>(static_cast<std::size_t>(unknown0C_));
    for (int y = 0; y < unknown0C_; ++y) rows_[y] = cells_ + y * unknown08_;
}
EI_REWRITTEN(0x00572820, SectorGrid::Allocate);
EI_REWRITTEN(0x005728D0, SectorGrid::Allocate);

void SectorGrid::Resize(int width, int height) {
    if (unknown08_ == width && unknown0C_ == height) return;
    if (cells_) cells_->VectorDelete(3);
    gamecrt::operator_delete(rows_);
    unknown08_ = width;
    unknown0C_ = height;
    Allocate();
}
EI_REWRITTEN(0x00572450, SectorGrid::Resize);
EI_REWRITTEN(0x005724F0, SectorGrid::Resize);

int CWorldClient::Create() {
    CWorld::Create();
    {
        std::string path = (TheDataFolder() + worlddata::TexturesFolderText()).GetString();
        texturePath_ = path;
    }
    LoadLights(worlddata::LightsTableText());
    lightingMark_ = 0;
    sounds_ = MakeCounted<CSoundManager>();

    texts_ = new CTextManager();
    ++texts_->refCount_;
    terrain_ = new Terrain();
    ++terrain_->refCount_;
    zoneName_ = worlddata::GipathText();

    manager44_ = NewCounted<CClientSpecificManager>();
    manager44_->Init(this);
    textureManager_ = NewCounted<CTextureManager>();
    textureManager_->CBaseManager::Init(this);
    manager188_ = NewCounted<CMaterialManager>();
    manager188_->Init(this);
    manager190_ = NewCounted<CLightFactory>();
    manager190_->Init(this);

    std::uint8_t* particleMemory = New(sizeof(CParticleManager));
    CParticleManager* manager = particleMemory ? new (particleMemory) CParticleManager() : nullptr;
    particleManager_ = manager;
    ++manager->refCount_;
    manager->Init(this);

    session_ = players_->CreateSessionPlayer();

    CRefObject* created = new CClientHandle();
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
    {
        std::string path = (TheTextFolder() + worlddata::TextsFileText()).GetString();
        texts_->AddSource(path.c_str());
    }
    {
        std::string path = (TheTextFolder() + worlddata::TextsLmpFileText()).GetString();
        texts_->AddSource(path.c_str());
    }

    std::uint8_t* lightningMemory = New(sizeof(CLightningManager));
    CLightningManager* lists = lightningMemory ? new (lightningMemory) CLightningManager() : nullptr;
    listManager_ = lists;
    lists->Init(this);
    std::uint8_t* fogMemory = New(sizeof(ClientArrayManager));
    ClientArrayManager* arrays = fogMemory ? new (fogMemory) ClientArrayManager() : nullptr;
    arrayManager_ = arrays;
    arrays->Init(this);
    std::uint8_t* memory = New(sizeof(CMusicSystem));
    CMusicSystem* owned = memory ? new (memory) CMusicSystem() : nullptr;
    music_ = owned;
    owned->Create();

    rainSnow_.PrepareBuffers();
    rainSnow_.world = this;
    platforms_.Create();
    if (worlddata::ShadowSetting() > 0)
        flags_ |= 0x200;
    else
        flags_ &= ~0x200u;
    {
        std::string path = (TheDataFolder() + worlddata::CursorFileText()).GetString();
        cursor_ = text::sys::LoadCursorFromFileA(path.c_str());
    }

    if (SpellManager()) ReferenceEveryObject(SpellManager());

    ScreenHits& entries = hits_;
    if (entries.Capacity() < 400) {
        int count = static_cast<int>(entries.size());
        ScreenEntry* block;
        if (entries.begin()) {
            block = stlp::vector<ScreenEntry>::AllocateAndCopy<stlp::PlainElement<ScreenEntry>>(
                400, entries.begin(), entries.end());
            if (ScreenEntry* old = entries.begin())
                stlp::deallocate(old, static_cast<std::size_t>(entries.EndOfStorage() - old) * sizeof(ScreenEntry));
        } else {
            block = gamecrt::operator_new_array<ScreenEntry>(400);
        }
        entries.Start() = block;
        entries.Finish() = block + count;
        entries.EndOfStorage() = block + 400;
    }
    nextClientObjectId_ = -100000;
    cave_ = 0;
    partyDanger_ = 0;
    GridToFour(secondGrid_);
    GridToFour(objectGrid_);
    ticksRun_ = 0;
    lightLevelsCountdown_ = 0;
    return 1;
}

void CWorldClient::ReferenceItemsAndSpells() {
    if (ItemFactory()) ReferenceEveryObject(ItemFactory());
    // The item factory is tested again before walking the spells.
    if (ItemFactory()) ReferenceEveryObject(SpellManager());
}

namespace {
void ReleaseEveryObject(CBaseManager* manager) {
    auto* walk = gamecrt::operator_new_as<ObjectListIterator>();
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
        ReleaseEveryObject(ItemFactory());
        ItemFactory()->DestroyAll();
    }
    if (SpellManager()) {
        ReleaseEveryObject(SpellManager());
        SpellManager()->DestroyAll();
    }
}

int CWorldClient::LoadItemsAndSpells(CStream* block) {
    ReleaseItemsAndSpells();
    if (block->Size() != 0) {
        manager4C_->Load(block);
        itemManager_->Load(block);
        manager4C_->LoadObjects(block);
        itemManager_->LoadObjects(block);
        ReferenceItemsAndSpells();
    }
    return 1;
}
EI_REWRITTEN(0x00567A60, CWorldClient::LoadItemsAndSpells);

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
EI_REWRITTEN(0x005679A0, LoadManagerObjects);

int CWorldClient::LoadNewItemsAndSpells(CStream* block) {
    block->Seek(0);
    block->ok_ = 1;
    if (!LoadManagerObjects(SpellManager(), block)) return 0;
    return LoadManagerObjects(ItemFactory(), block) != 0;
}
EI_REWRITTEN(0x00567950, CWorldClient::LoadNewItemsAndSpells);

int PlatformSet::Create() {
    list.RemoveAll();
    return 1;
}
EI_REWRITTEN(0x00561800, PlatformSet::Create);

void PlatformSet::Destroy() { list.RemoveAll(); }
EI_REWRITTEN(0x00561840, PlatformSet::Destroy);

PlatformSet::~PlatformSet() {
    list.vtable = mfc::impl::PlatformListClass::Vtable();
    list.RemoveAll();
}
EI_REWRITTEN_SPECIAL(0x00561A60, PlatformSet::~PlatformSet);
EI_REWRITTEN_SPECIAL(0x00561AE0, PlatformSet::~PlatformSet);

// What LoadManagerObjects reads back: per object a 1, its id, type and a field, then its own Save; a 0
// ends the list.
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
EI_REWRITTEN(0x0055E8A0, SaveListedObjects);

CLightningManager::CLightningManager() {
    newBolts_.vtable = gameslots::ClientObjectList2Class::Vtable();
    newBolts_.count = 0;
    newBolts_.free = nullptr;
    newBolts_.tail = nullptr;
    newBolts_.head = nullptr;
    newBolts_.blocks = nullptr;
    newBolts_.blockSize = 10;
}

CLightningManager::~CLightningManager() {
    newBolts_.vtable = gameslots::ClientObjectList2Class::Vtable();
    for (auto* node = newBolts_.head; node; node = node->next)
        if (node->data) node->data->Release();
    newBolts_.RemoveAll();
}

EI_REWRITTEN_SPECIAL(0x00562EC0, CLightningManager::~CLightningManager);

EI_REWRITTEN_SPECIAL(0x00562EA0, CLightningManager::~CLightningManager);

ClientArrayManager::ClientArrayManager() {
    texture = nullptr;
    mfc::impl::ConstructListHeader<mfc::impl::ClientList3Class>(&list, 10);
}

ClientArrayManager::~ClientArrayManager() {
    VisFogManagerDestroy(this);
    list.vtable = mfc::impl::ClientList3Class::Vtable();
    list.RemoveAll();
    if (CTexture* held = texture) held->Release();
}

EI_REWRITTEN_SPECIAL(0x00562DF0, ClientArrayManager::~ClientArrayManager);

EI_REWRITTEN_SPECIAL(0x00562DD0, ClientArrayManager::~ClientArrayManager);

namespace {

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

MapRecord* LocalZoneRecord() {
    mfc::CString zone(mfc::CString::kUnset);
    return TheMapDatabase()->FindMap(*LocalPlayerName(&zone));
}

bool InFinalZone() {
    mfc::CString zone(mfc::CString::kUnset);
    return gamecrt::mbscmp((*LocalPlayerName(&zone)).GetString(), worlddata::FinalZoneText()) == 0;
}

} // namespace

int CWorldClient::LoadZone(CStream* block) {
    TheGameFlow()->UpdateAndAcknowledge();
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
    exits_.SetSize(exitCount);
    for (int i = 0; i < exits_.size; ++i) exits_[i].Read(block);
    int cave;
    block->Read(&cave, 4);
    UnitLooks looks;
    ReadUnitLooks(block, &looks.figured, &looks.dressed);
    cave_ = cave;
    GameEnsureZone(map, mob);
    ShowLoadingMovie(kProgressBlockRead);

    ClearGroundPrints(bloodPrints_, footprints_, scorchMarks_);
    ResetZoneObjects();
    ClearMaps();
    ReleaseItemsAndSpells();
    ClientParticleManager()->ReleaseUnheld();
    Session()->sessionKind100_ = 0;
    ShowLoadingMovie(kProgressZoneCleared);
    Terrain* terrain = terrain_;
    if (map.GetLength() > 1 && map[1] != ':') {
        mfc::CString folder;
        modfolders::SelectMaps(map, modfolders::MapsSite::ClientLoadZone);
        MapsFolder(&folder);
        map = folder + map;
    }
    terrain->ClearMap();
    // The texture manager is fetched for both calls; the first does not use it.
    DeleteComposedTextures();
    TextureManager()->FreeCache();
    ShowLoadingMovie(kProgressMapFreed);

    // The allod of the local player's zone names the texture pages, the lights and the music.
    const MapRecord* zone = LocalZoneRecord();
    std::string& allod = zoneName_;
    allod = worlddata::DefaultAllodText();
    if (zone) allod = zone->allod;
    str::MakeLower(allod);
    if (!terrain->LoadMap(map, allod.c_str(), cave == kCave))
        return game::FatalError(worlddata::CantLoadTerrainFormat(), map.GetString());

    FitGrid(objectGrid_, terrain);
    FitGrid(secondGrid_, terrain);
    if (!worlddata::WaterWavesOption()) terrain->landscapeFlags &= ~kLandscapeWaves;
    if (cave == kCave) terrain->landscapeFlags |= kLandscapeWaves;
    if (!worlddata::SelfShadowingOption()) terrain->landscapeFlags &= ~kLandscapeSelfShadowing;
    terrain->SetLightInterval(worlddata::LightingFrequencyOption() / kFrameMilliseconds);
    if (skyDome_) {
        HeldTexture* holder = &skyDome_->texture;
        if (cave == kCave) {
            holder->Load(kSkyTextureKind, worlddata::CaveSkyText());
            skyDome_->turns = 0;
        } else {
            holder->Load(kSkyTextureKind, worlddata::OpenSkyText());
            skyDome_->turns = 1;
        }
    }
    std::string lights = worlddata::LightsPrefixText();
    const bool effects = worlddata::AudioOption() && worlddata::VolumeSettingA();
    if (cave_ == kCave) {
        lights += worlddata::CaveText();
        if (effects) sounds_->SetReverb(kCaveReverbRoom, kCaveReverbLevel, kCaveReverbTime);
    } else if (effects) {
        sounds_->reverb[0] = 0.0f;
    }
    {
        std::string table = lights + allod;
        std::string file = table + worlddata::IniSuffixText();
        LoadLights(file.c_str());
    }
    CreateExitMarkers();
    worlddata::g_lastLitHour = -1.0f;
    UpdateLighting();
    if (worlddata::AudioOption() && worlddata::MovieVolumeSetting()) {
        if (InFinalZone()) music_->SetZoneMode(worlddata::FinalMusicText(), cave_ == kCave);
        else music_->SetZoneMode(allod.c_str(), cave_ == kCave);
    }

    // The local server's pending messages follow this block in the view's stream.
    LocalView()->AppendReceived(GameObject7C2D60());
    ticksRun_ = 0;
    BuildUnitSkins(&looks.figured, &looks.dressed);
    // A wind option other than 0 sets the waves.
    const float wind = worlddata::DefaultWindForceOption();
    EI_NAN_AUDIT(wind);
    if (wind != 0.0f) TheTerrain()->SetWaveStrength(wind);
    ShowLoadingMovie(kProgressZoneReady);
    LocalView()->SendResume();
    CloseLoadingMovie();
    return 1;
}
EI_REWRITTEN(0x00567AB0, CWorldClient::LoadZone);

EI_REWRITTEN(0x00561E50, CWorldClient::Create);
EI_REWRITTEN(0x00560650, PlatformSet::AddFace);
EI_REWRITTEN(0x00560820, PlatformSet::AddBox);
EI_REWRITTEN(0x00563290, CWorldClient::Destroy);
EI_REWRITTEN(0x00565090, CWorldClient::ResetZoneObjects);
EI_REWRITTEN(0x00566100, CWorldClient::Deselect);
EI_REWRITTEN(0x005630B0, CWorldClient::ReferenceItemsAndSpells);
EI_REWRITTEN(0x00563190, CWorldClient::ReleaseItemsAndSpells);
EI_REWRITTEN(0x0056DAF0, CWorldClient::ObjectsInRange);
EI_REWRITTEN(0x00566F70, CWorldClient::CreatePartyUnits);
EI_REWRITTEN(0x005647E0, CWorldClient::AddObject);
EI_REWRITTEN(0x005651A0, CWorldClient::RefreshPlatforms);
EI_REWRITTEN(0x0056D7E0, CWorldClient::QueueSynchronised);
EI_REWRITTEN(0x0056D7A0, CWorldClient::DropShadows);
EI_REWRITTEN(0x00564BE0, CWorldClient::RemoveObject);
EI_REWRITTEN(0x00564E70, CWorldClient::ClearMaps);
EI_REWRITTEN(0x00565790, CWorldClient::PickAt);
EI_REWRITTEN(0x005659D0, CWorldClient::PickAt2);
EI_REWRITTEN(0x00565E40, CWorldClient::UnitAt);
EI_REWRITTEN(0x00565D00, CWorldClient::UnitAt2);
EI_REWRITTEN(0x005656A0, CWorldClient::UnitsInBox);
EI_REWRITTEN(0x0056D740, CWorldClient::MarkFootprintsInSight);
EI_REWRITTEN(0x00564190, CWorldClient::ApplyWind);
EI_REWRITTEN(0x005662B0, CWorldClient::CentreOn);
EI_REWRITTEN(0x00578A60, CWorldClient::ExitTarget);
EI_REWRITTEN(0x005618E0, CWorldClient::GroundHeightAt);
EI_REWRITTEN(0x00561930, CWorldClient::SurfaceHeightAt);
EI_REWRITTEN(0x0056CE40, CWorldClient::ShowNumber);
EI_REWRITTEN(0x0056DE80, CWorldClient::LoadUnitFaces);
EI_REWRITTEN(0x00567400, CWorldClient::RemoveUnitsOf);
EI_REWRITTEN(0x00566330, CWorldClient::SendFlag10);
EI_REWRITTEN(0x00570860, SectorGrid::Move);
EI_REWRITTEN(0x00570BB0, SectorGrid::Move2);
