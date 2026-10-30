// The client world's object maps (helpers and the WorldClient methods that keep the maps). See
// docs/notes/world.md ("Client maps"). The client files every object in a sector grid (STLport lists of
// counted pointers, one per 16x16 cell) and a second grid; most of the helpers are the STLport and library
// instantiations behind those maps (config/library.csv).
#include "ai_map.h"
#include "base_manager.h"
#include "bink_video.h"
#include "camera.h"
#include "core_streams.h"
#include "effects.h"
#include "figure_manager.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "ground_print.h"
#include "hash_tables_400.h"
#include "item.h"
#include "item_manager.h"
#include "text_manager.h"
#include "lightning.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "material.h"
#include "mod_folders.h"
#include "model.h"
#include "object_tables.h"
#include "player_client.h"
#include "player_manager.h"
#include "player_messages.h"
#include "sky_object.h"
#include "sound.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"
#include "ui_flying_hit_points.h"
#include "unit.h"
#include "unit_client_models.h"
#include "unit_create.h"
#include "unit_db.h"
#include "unit_looks.h"
#include "world.h"
#include "world_data.h"
#include "world_effect.h"
#include "world_glue.h"
#include "world_map_files.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rect.h"
#include "lib/rounding.h"
#include "lib/str.h"


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <new>
#include <string>
#include <vector>

namespace {

// The cell index of a coordinate: rounded to nearest, then an unsigned shift by 4, so a
// negative coordinate gives a huge index that fails the bounds tests.
std::uint32_t CellOf(float coordinate) { return static_cast<std::uint32_t>(lib::RoundEven(coordinate)) >> 4; }

// Every element of `list` (a sector cell or the heroes) that is `object` erased (released), the object held
// meanwhile.
template <typename List>
void RemoveHeld(List& list, CMapObject* object) {
    HeldRef<CMapObject> hold(object);
    std::erase_if(list, [object](const HeldRef<CMapObject>& held) { return held.Get() == object; });
}

// The hit test of the object's side half for the point: CollectBounds or CollectParts.
using HitTest = int (Object3DSpecific::*)(const std::int32_t* point);

// The screen entries whose rectangle holds (x, y) and whose object still exists and answers `test` go
// to `hits`.
HitCollector* CollectHits(HitCollector* out, const std::vector<ScreenEntry>& entries, std::vector<ScreenEntry>* hits, std::int32_t x,
                          std::int32_t y, CMapObjectManager* manager, HitTest test) {
    for (const ScreenEntry& entry : entries) {
        Point pt = {x, y};
        if (!rects::Contains(&entry.rect, pt)) continue;
        CMapObject* object = manager->FindMapObject(entry.objectId);
        if (!object) continue;
        std::int32_t point[2] = {x, y};
        Object3DSpecific* half = object->Object3D()->attached;
        if (!(half->*test)(point)) continue;
        hits->push_back(entry);
    }
    out->hits = hits;
    return out;
}

void SessionPlayerClear(CPlayer* session) { session->Clear(); }

// The grid cell of the object's position, null outside the grid.
std::vector<HeldRef<CMapObject>>* CellFor(SectorGrid& grid, CMapObject* object) {
    std::uint32_t x = CellOf(object->localPosition.x);
    std::uint32_t y = CellOf(object->localPosition.y);
    if (x >= grid.width || y >= grid.height) return nullptr;
    return &grid.rows[y][x];
}

using Collect = HitCollector* (HitCollector::*)(const std::vector<ScreenEntry>&, std::vector<ScreenEntry>*, std::int32_t, std::int32_t, CMapObjectManager*);
CMapObject* Pick(WorldClient* client, const Point* point, Collect collect) {
    std::vector<ScreenEntry>& hits = client->hits;
    hits.clear();
    CMapObjectManager* manager = client->MapObjectManager();
    HitCollector functor;
    (functor.*collect)(client->screenEntries, &hits, point->x, point->y, manager);
    if (hits.empty()) return nullptr;

    // Front to back (DepthLess with the top of the view stack); hits at the same depth keep the game's sort's
    // order, and the first that takes the click wins.
    DepthLess depth{g_sceneRenderer.pipeline->Top(TransformPipeline::kViewStack)->m, client->MapObjectManager()};
    lib::IntroSort(hits.data(), hits.data() + hits.size(),
                   [depth](const ScreenEntry& a, const ScreenEntry& b) { return depth.Less(&a, &b); });
    ScreenEntry*& cursor = client->hitCursor;
    for (cursor = hits.data(); cursor != hits.data() + hits.size(); ++cursor) {
        if (CMapObject* object = manager->FindMapObject(cursor->objectId)) return object;
    }
    return nullptr;
}

// From a picked object on: the first unit of the sorted hits. The cursor moves past entries whose
// object is gone or is not a unit; null at the end.
CUnit* FirstUnitHit(WorldClient* client, CMapObject* object) {
    while (object->GetClassId() != ClassId::Unit) {
        do {
            if (++client->hitCursor == client->hits.data() + client->hits.size()) return nullptr;
            object = client->objectManager->FindMapObject(client->hitCursor->objectId);
        } while (!object);
    }
    return static_cast<CUnit*>(object);
}

bool SameText(std::string_view a, std::string_view b) { return a == b; }

std::uint32_t Ftol(float v) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(v)); }

constexpr float kSteepest = 0.8f;   // the smallest normal z of a face added

template <typename T>
void DeleteOwned(T*& slot) {
    if (EngineObject* object = slot) {
        delete object;
        slot = nullptr;
    }
}

// A counted engine object: its count dropped (without a release), then deleted the same way.
template <typename T>
void DropAndDelete(T*& slot) {
    if (slot) {
        --slot->refCount;
        EngineObject* object = slot;
        if (object) delete object;
        slot = nullptr;
    }
}

// The selection highlight off: Selected and BoxMarked cleared, the side half's empty virtual and SetLit
// with 0 (its light goes off).
void Unhighlight(CMapObject* object) {
    object->flags &= ~(MapObjectFlag::Selected | MapObjectFlag::BoxMarked);
    Object3DSpecific* half = object->attached;
    half->Unused25(0);
    half = object->attached;
    half->SetLit(false);
}

// A new object (its class's operator new and constructor), stored and referenced.
template <typename T>
T* NewCounted() {
    T* object = new T();
    object->AddRef();
    return object;
}

// The grid set to 4 x 4 cells, reallocated unless its storage already has that size (the second
// resize inlined).
void GridToFour(SectorGrid& grid) {
    grid.Resize(1, 1);
    grid.width = 4;
    grid.height = 4;
    grid.Resize(4, 4);
}

void ReferenceEveryObject(BaseManager* manager) {
    ObjectListIterator walk{};
    for (EngineObject* object = walk.First(manager); object; object = walk.Next()) object->AddRef();
    if (BaseManager* held = walk.manager) --held->refCount;
}

void ReleaseEveryObject(BaseManager* manager) {
    ObjectListIterator walk{};
    for (EngineObject* object = walk.First(manager); object; object = walk.Next()) object->Release();
    if (BaseManager* held = walk.manager) --held->refCount;
}

constexpr int kSkyTextureKind = static_cast<int>(TextureKind::SkyFace);
// The loading movie's progress steps of the zone load.
constexpr int kProgressBlockRead = 8;
constexpr int kProgressZoneCleared = 9;
constexpr int kProgressMapFreed = 10;
constexpr int kProgressZoneReady = 11;
// A cave's reverb: room type, level and reflection time.
constexpr float kCaveReverbRoom = 0.3f;
constexpr float kCaveReverbLevel = 1.0f;
constexpr float kCaveReverbTime = 3.0f;
// Terrain flags the options and the cave decide.
constexpr int kCave = 1;

// A sector grid sized for the terrain, in cells of 16: emptied to one cell first, the new size
// stored, then allocated.
void FitGrid(SectorGrid& grid, const terrain::Terrain* terrain) {
    const std::uint32_t height = static_cast<std::uint32_t>(lib::RoundEven(terrain->height));
    const std::uint32_t width = static_cast<std::uint32_t>(lib::RoundEven(terrain->width));
    grid.Resize(1, 1);
    grid.height = height >> 4;
    grid.width = width >> 4;
    grid.Resize(static_cast<int>(width >> 4), static_cast<int>(height >> 4));
}

MapRecord* LocalZoneRecord() {
    return g_mapDatabase.FindMap(LocalPlayerZone());
}

bool InFinalZone() {
    return str::Compare(LocalPlayerZone(), worlddata::FinalZoneText()) == 0;
}

} // namespace

void SectorGrid::Move(CMapObject* object, const float* position) {
    float* stored = &object->localPosition.x;    // x, y, z
    std::uint32_t oldX = CellOf(stored[0]);
    std::uint32_t oldY = CellOf(stored[1]);
    std::uint32_t newX = CellOf(position[0]);
    std::uint32_t newY = CellOf(position[1]);
    if (oldX == newX && oldY == newY) {
        std::memcpy(stored, position, 12);
        return;
    }
    if (oldX < width && oldY < height) RemoveHeld(rows[oldY][oldX], object);
    std::memcpy(stored, position, 12);
    if (newX < width && newY < height) rows[newY][newX].emplace_back(object);
}

HitCollector* HitCollector::CollectBounds(const std::vector<ScreenEntry>& entries, std::vector<ScreenEntry>* found, std::int32_t x, std::int32_t y,
                                          CMapObjectManager* manager) {
    return CollectHits(this, entries, found, x, y, manager, &Object3DSpecific::BoundsContainPoint);
}

HitCollector* HitCollector::CollectParts(const std::vector<ScreenEntry>& entries, std::vector<ScreenEntry>* found, std::int32_t x, std::int32_t y,
                                         CMapObjectManager* manager) {
    return CollectHits(this, entries, found, x, y, manager, &Object3DSpecific::PartsContainPoint);
}

int WorldClient::AddObject(CMapObject* object) {
    for (CMapObject* filed : objects) {
        if (filed->id == object->id) {
            object->AddRef();
            object->Release();
            return 1;
        }
    }
    auto entry = dynamicNodes.find(static_cast<std::uint32_t>(object->id));
    if (entry == dynamicNodes.end() && object->IsSynchronised()) {
        auto node = dynamicObjects.insert(dynamicObjects.end(), object);
        int key = object->id;
        hash400::Entry(dynamicNodes, dynamicMaxId, key) = node;
    }
    ClassId kind = object->GetClassId();
    if (kind == ClassId::Building || object->GetClassId() == ClassId::Lever) {
        CObject3D* figure = object->Object3D();
        for (int i = 0; i < std::ssize(figure->parts); ++i) {
            if ((figure->PartBox(i)->bounds.flags >> 10) & 1) {
                int objectId = object->id;
                platforms.AddBox(objectId, figure->PartBox(i), object->localPosition, &object->localRotation);
            }
        }
    }
    CUnit* unit = UnitOrNull(object);
    if (unit && session->sessionKind == 1) {
        if (unit->player == nullptr && ticksRun >= 1) unit->Controller()->RestartFadeIn();
    }

    object->AddRef();
    objects.push_back(object);

    if (auto* cell = CellFor(objectGrid, object)) RemoveHeld(*cell, object);
    if (auto* cell = CellFor(objectGrid, object)) cell->emplace_back(object);
    if (object->MapType() == MapObjectType::Hero && IsTextKey(object->id)) heroes.emplace_back(object);
    return 1;
}

void WorldClient::RefreshPlatforms(CMapObject* object) {
    const int objectId = object->id;
    if (std::ranges::none_of(objects, [objectId](const CMapObject* filed) { return filed->id == objectId; })) return;
    std::erase_if(platforms.list, [objectId](const Platform& face) { return face.id == objectId; });
    if (object->GetClassId() != ClassId::Building && object->GetClassId() != ClassId::Lever) return;
    CObject3D* figure = object->Object3D();
    for (int i = 0; i < std::ssize(figure->parts); ++i) {
        if (!((figure->PartBox(i)->bounds.flags >> 10) & 1)) continue;
        platforms.AddBox(object->id, figure->PartBox(i), object->localPosition, &object->localRotation);
    }
}

bool WorldClient::QueueSynchronised(CMapObject* object) {
    if (dynamicNodes.contains(static_cast<std::uint32_t>(object->id))) return false;
    auto node = dynamicObjects.insert(dynamicObjects.end(), object);
    int key = object->id;
    hash400::Entry(dynamicNodes, dynamicMaxId, key) = node;
    return true;
}

void WorldClient::MarkFootprintsInSight(const float* point, float range) {
    // A footprint is made visible when hypot(position - point) <= its scale * range.
    for (Footprint& print : footprints) {
        float dx = print.position.x - point[0];
        float dy = print.position.y - point[1];
        double distance = libcrt::Hypot(dx, dy);
        float limit = print.scale * range;
        EI_NAN_AUDIT(distance, limit);
        if (distance <= static_cast<double>(limit)) print.visible = 1;
    }
}

void WorldClient::ApplyWind(const Vector3& direction, float strength) {
    SetWind(direction, strength);
    if (terrain) terrain->SetWaves(direction, strength);
    rainSnow.SetWind(direction, strength);
}

int WorldClient::CentreOn(const Vector3& point) {
    Vector3 focus = point;
    focus.z = camera->focus.z;
    camera->SetFocus(focus);
    camera->ApplyView();
    camera->ClampAboveObjects();
    return 1;
}

double WorldClient::GroundHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(GridHeightAt(x, y));
    return platforms.SurfaceHeight(point, height);
}

double WorldClient::SurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = static_cast<float>(RawSurfaceHeightAt(x, y));
    return platforms.SurfaceHeight(point, height);
}

void WorldClient::ShowNumber(int objectId, int value, std::uint32_t kind) {
    EngineObject* object = MapObjectManager()->FindObject(static_cast<unsigned>(objectId));
    if (!object || object->GetClassId() != ClassId::Unit) return;
    if (numbers) numbers->Add(objectId, value, kind);
}

void WorldClient::LoadUnitFaces() {
    for (CMapObject* object : objects) {
        // The pointer is tested after the class call (the list holds no null).
        if (CUnit* unit = UnitOrNull(object)) unit->ClientController()->LoadFaceTexture();
    }
}

const char* WorldClient::ExitTarget(int index) { return exits[index].target.c_str(); }

void WorldClient::RemoveUnitsOf(const std::vector<PartyUnitRecord*>* records) {
    for (int i = 0; i < std::ssize(*records); ++i)
        RemoveObject(static_cast<int>((*records)[i]->unitId));
}

int WorldClient::SendFlag10(int value) {
    return LocalView()->SendFlag10(static_cast<std::uint8_t>(value));
}

void WorldClient::DropShadows() {
    for (CMapObject* object : objects) {
        Object3DClientSpecific* half = object->ClientSpecific();
        // Only the low byte is tested (ShadowsShown's "off" leaves the setting's upper bytes).
        if (!static_cast<std::uint8_t>(half->ShadowsShown())) half->ClearShadow();
    }
}

void WorldClient::RemoveObject(int objectId) {
    std::erase_if(platforms.list, [objectId](const Platform& face) { return face.id == objectId; });
    auto& ids = dynamicNodes;
    if (auto entry = ids.find(static_cast<std::uint32_t>(objectId)); entry != ids.end()) {
        dynamicObjects.erase(entry->second);
        if (auto again = ids.find(static_cast<std::uint32_t>(objectId)); again != ids.end()) ids.erase(again);
    }
    auto filed = std::ranges::find_if(objects, [objectId](const CMapObject* object) { return object->id == objectId; });
    if (filed == objects.end()) return;
    CMapObject* object = *filed;
    if (object->MapType() == MapObjectType::Hero && IsTextKey(objectId)) {
        RemoveHeld(heroes, object);
        if (session->sessionKind == 3) static_cast<CUnit*>(object)->ClientController()->DropFaceTexture();
    }
    if (auto* cell = CellFor(objectGrid, object)) RemoveHeld(*cell, object);
    objects.erase(filed);
    object->Release();
}

void WorldClient::ClearMaps() {
    SectorGrid& second = secondGrid;
    for (int x = 0; x < static_cast<int>(second.width); ++x)
        for (int y = 0; y < static_cast<int>(second.height); ++y)
            second.rows[y][x].clear();
    SectorGrid& grid = objectGrid;
    for (int x = 0; x < static_cast<int>(grid.width); ++x)
        for (int y = 0; y < static_cast<int>(grid.height); ++y)
            grid.rows[y][x].clear();

    // Both emptied: the platform faces and the dynamic objects (the elements need no destruction).
    platforms.list.clear();
    dynamicObjects.clear();
    dynamicNodes.clear();
    dynamicMaxId = 0;

    selectedIds.clear();

    heroes.clear();
    // The objects with a positive id released and unfiled.
    std::erase_if(objects, [](CMapObject* object) {
        if (object->id <= 0) return false;
        object->Release();
        return true;
    });
    SessionPlayerClear(session);
    if (LinkFigureLoader* loader = figureLoader) loader->ClearCache();
    figureManager->FreeSpares();
    objectManager->FreeSpares();
    specificManager->FreeSpares();
}

CMapObject* WorldClient::PickAt(const Point* point) { return Pick(this, point, &HitCollector::CollectBounds); }
CMapObject* WorldClient::PickAt2(const Point* point) { return Pick(this, point, &HitCollector::CollectParts); }

CUnit* WorldClient::UnitAt(const Point* point) {
    if (CMapObject* picked = PickAt2(point)) {
        if (CUnit* unit = FirstUnitHit(this, picked)) return unit;
    }
    CMapObject* picked = PickAt(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

CUnit* WorldClient::UnitAt2(const Point* point) {
    CMapObject* picked = PickAt2(point);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

std::vector<CMapObject*> WorldClient::UnitsInBox(const Rect* rect) {
    std::vector<CMapObject*> inside;
    for (auto& node : screenEntries) {
        CMapObject* object = objectManager->FindMapObject(node.objectId);
        if (!object) continue;
        if (!object->Object3D()->attached->PartsIntersectRect(rect)) continue;
        inside.push_back(object);
    }
    return inside;
}

// The figure's part names (worlddata::HeroPartText): "hd", "bd", "hp", "hr" (the hair: "hr.%.2d"
// with the prototype's number, or left out at -1), "lh1".."rl3".
void WorldClient::CreatePartyUnits(const std::vector<PartyUnitRecord*>* party, char allArmour) {
    std::string hair;
    for (int i = 0; i < std::ssize(*party); ++i) {
        auto* record = (*party)[i];
        std::vector<std::string> parts;
        for (int n = 0; n < worlddata::kHeroPartCount; ++n) {
            const char* name = worlddata::HeroPartText(n);
            if (SameText(name, worlddata::HairPartText())) {
                UnitPrototypeRecord* prototype = unitdb::PrototypeNamed(std::string(record->texts[5]));
                int number = prototype->hair;
                if (number != -1) {
                    hair = str::Format(worlddata::HairFormat(), number);
                    parts.push_back(hair);
                }
            } else {
                parts.push_back(name);
            }
        }
        auto* manager = MapObjectManager();
        auto* unit = manager->CreateObjectAs<CUnit>(MapObjectType::Hero, static_cast<int>(record->unitId));
        if (!unit) return;   // (leaving the loop is the same as returning here)
        UnitPrototypeRecord* prototype = g_unitDatabase.prototypes.FindByName(record->texts[5]);
        unit->prototypeId = static_cast<std::uint32_t>(prototype ? prototype - g_unitDatabase.prototypes.records.data() : -1);
        unit->SetSecondPrototypeByName(record->texts[6]);
        unit->flags |= MapObjectFlag::Preview;
        if (!unit->MakeFigure(record->texts[2], &parts, 1)) return;
        Object3DClientSpecific* figure = unit->Object3D()->ClientSpecific();
        figure->RegisterPartMaterials();
        unit->PlayAnimation(nullptr);
        unit->RestartPose();
        const std::string& skin = record->texts[4];
        figure->SetSkins(game::NoneText(), (SameText(skin, game::NoneText()) ? std::nullopt : std::optional<std::string_view>(skin)));
        unit->WriteFullState();
        CMapObject* slots = unit;
        slots->SetStats(&record->stats);
        unit->SetFigureScale(&record->scaleX);
        std::memcpy(&unit->MapObject()->baseScale, &record->scaleX, 12);
        slots->ApplyBaseScale();
        unit->ReleaseAllItems();
        for (int j = 0; j < std::ssize(record->weapons); ++j)
            unit->AddWeapon(record->weapons[j]);
        UnitController* controller = unit->Controller();
        if (!record->weapons.empty()) controller->ShowWeapon(0);
        for (int j = 0; j < std::ssize(record->armor); ++j) {
            Item* armour = record->armor[j];
            if (allArmour || AsArmor(armour)->GetType() != 0) unit->EquipArmour(armour);
        }
        for (int j = 0; j < std::ssize(record->quickItems); ++j)
            unit->AddQuickItem(record->quickItems[j]);
        unit->Controller()->RefreshEquipment();
        unit->Controller()->RefreshModel();
        slots->SetScriptName(record->texts[0]);
        AddObject(unit);
    }
}

std::vector<CObject3D*> WorldClient::ObjectsInRange(const Vector3& point, float radius) {
    std::vector<CObject3D*> found;
    float maxX = terrain->width - 1.0f;
    float maxY = terrain->height - 1.0f;
    float y1 = MinKeepB(MaxKeepA(0.0f, point.y + radius), maxY);
    float x1 = MinKeepB(MaxKeepA(0.0f, point.x + radius), maxX);
    float y0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point.y - radius), maxY));
    float x0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point.x - radius), maxX));
    std::uint32_t& firstX = rangeFirstX;
    std::uint32_t& firstY = rangeFirstY;
    std::uint32_t& lastX = rangeLastX;
    std::uint32_t& lastY = rangeLastY;
    std::uint32_t& x = rangeX;
    std::uint32_t& y = rangeY;
    firstX = x = Ftol(x0) >> 4;
    firstY = y = Ftol(y0) >> 4;
    lastX = Ftol(x1) >> 4;
    lastY = Ftol(y1) >> 4;

    SectorGrid& grid = objectGrid;
    for (auto* cell = &grid.rows[y][x]; cell;) {
        for (CMapObject* object : *cell) {
            float pz = point.z;
            float ox = object->localPosition.x + object->bounds.center.x;
            float oy = object->localPosition.y + object->bounds.center.y;
            float reach = pc24::Add(libcrt::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y), radius);
            float dx = ox - point.x;
            float dy = oy - point.y;
            float dz = pz - point.z;
            float distance = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
            float limit = reach * reach;
            // Skipped when the limit is below (or unordered).
            if (limit >= distance) found.push_back(object);
        }
        if (++x > lastX) {
            x = firstX;
            if (++y > lastY) return found;
        }
        cell = &grid.rows[y][x];
    }
    return found;
}

int PlatformSet::AddFace(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& normal, Platform** out) {
    *out = nullptr;
    EI_NAN_AUDIT(normal.z);
    if (normal.z < kSteepest) return 0;
    // (Every field is written below or by the caller; the record stays in place until the next face.)
    Platform* record = &*list.emplace(list.begin());
    *out = record;
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float lengthU = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz)));
    record->sizeU = lengthU;
    float lengthV = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(vx, vy, vz)));
    record->sizeV = lengthV;
    record->normal = normal;
    record->origin = a;
    record->axisU.x = ux / lengthU;
    record->axisU.y = uy / lengthU;
    record->axisU.z = uz / lengthU;
    record->axisV.x = vx / lengthV;
    record->axisV.y = vy / lengthV;
    record->axisV.z = vz / lengthV;
    return 1;
}

void PlatformSet::AddBox(int newId, const FootprintPart* item, const Vector3& position, const Quaternion* rotation) {
    const Vector3& offset = item->bounds.center;
    const Vector3& low = item->bounds.boxMin;
    const Vector3& high = item->bounds.boxMax;
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
    for (Vector3& corner : corners) corner = position + TransformColumnNormal<Sum3::ZYX>(m, (pivot + corner) + offset);
    Vector3 normals[6] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
    for (Vector3& n : normals) n = TransformColumnNormal<Sum3::ZXY>(m, n);
    for (int i = 0; i < 6; ++i) {
        Platform* record;
        if (!AddFace(corners[i + 1], corners[i], corners[i + 2], normals[i], &record)) continue;
        record->id = newId;
        Vector3 rotated = m.Transform(pivot + offset);
        record->x = rotated.x + position.x;
        record->y = rotated.y + position.y;
        record->z = rotated.z + position.z;
        record->radius = item->bounds.radius;   // a dword move
    }
}

void WorldClient::Destroy() {
    g_camera.SetTarget(nullptr);
    delete numbers;
    numbers = nullptr;
    delete skyDome;
    skyDome = nullptr;
    DeleteOwned(bird);
    ReleaseItemsAndSpells();
    exitMarkers.clear();
    bloodPrints.clear();
    footprints.clear();
    scorchMarks.clear();
    rainSnow.Destroy();
    platforms.Destroy();
    platforms.list.clear();
    SectorGrid& grid = objectGrid;
    for (int x = 0; x < static_cast<int>(grid.width); ++x)
        for (int y = 0; y < static_cast<int>(grid.height); ++y)
            grid.rows[y][x].clear();
    selectedIds.clear();
    heroes.clear();
    for (CMapObject* object : objects)
        if (object) object->Release();
    objects.clear();
    if (CPlayer* currentSession = session) {
        currentSession->Release();
        session = nullptr;
    }
    DestroyOwned(handle);
    CWorld::Destroy();
    DeleteOwned(listManager);
    DeleteOwned(arrayManager);
    DropAndDelete(particleManager);
    DropAndDelete(terrain);
    DropAndDelete(lightFactory);
    DropAndDelete(materialManager);
    DropAndDelete(textureManager);
    DeleteOwned(music);
    DropAndDelete(sounds);
    DropAndDelete(texts);
}

void WorldClient::ResetZoneObjects() {
    exitMarkers.clear();
    // The second grid's cells emptied (each object released).
    SectorGrid& second = secondGrid;
    for (int x = 0; x < static_cast<int>(second.width); ++x)
        for (int y = 0; y < static_cast<int>(second.height); ++y)
            second.rows[y][x].clear();
    ObjectListIterator it{nullptr, {}};
    for (EngineObject* object = it.First(TimedObjectManager()); object; object = it.Next()) object->Release();
    if (bird) bird->Reset();
    if (it.manager) --it.manager->refCount;
}

void WorldClient::Deselect() {
    for (int objectId : selectedIds) {
        CMapObject* found = objectManager->FindMapObject(static_cast<unsigned>(objectId));
        if (found && (found->flags & MapObjectFlag::Selected)) Unhighlight(found);
    }
    selectedIds.clear();
}

void WorldClient::Select(CMapObject* object) {
    if (!object) return;
    object->Object3D()->UpdatePartBounds();
    if (!(object->flags & MapObjectFlag::Selected)) {
        if (!(object->flags & MapObjectFlag::BoxMarked)) object->attached->SetLit(true);
        object->flags |= MapObjectFlag::Selected;
        object->attached->Unused25(1);
    }
    if (std::ranges::find(selectedIds, object->id) == selectedIds.end()) selectedIds.push_back(object->id);
}

void SectorGrid::Allocate() {
    cells = std::make_unique<std::vector<HeldRef<CMapObject>>[]>(static_cast<std::size_t>(allocatedWidth) * allocatedHeight);
    rows = std::make_unique<std::vector<HeldRef<CMapObject>>*[]>(static_cast<std::size_t>(allocatedHeight));
    for (int y = 0; y < allocatedHeight; ++y) rows[y] = cells.get() + y * allocatedWidth;
}

void SectorGrid::Free() {
    cells.reset();
    rows.reset();
}

void SectorGrid::Resize(int newWidth, int newHeight) {
    if (allocatedWidth == newWidth && allocatedHeight == newHeight) return;
    Free();
    allocatedWidth = newWidth;
    allocatedHeight = newHeight;
    Allocate();
}

void WorldClient::Create() {
    CWorld::Create();
    {
        std::string path = g_dataFolder + worlddata::TexturesFolderText();
        texturePath = path;
    }
    LoadLights(worlddata::LightsTableText());
    lightingMark = 0;
    sounds = NewCounted<SoundManager>();

    texts = new TextManager();
    texts->AddRef();
    terrain = new terrain::Terrain();
    terrain->AddRef();
    zoneName = worlddata::GipathText();

    specificManager = NewCounted<ClientSpecificManager>();
    specificManager->Init(this);
    textureManager = NewCounted<CTextureManager>();
    textureManager->BaseManager::Init(this);
    materialManager = NewCounted<CMaterialManager>();
    materialManager->Init(this);
    lightFactory = NewCounted<LightFactory>();
    lightFactory->Init(this);

    auto* manager = new ParticleManager();
    particleManager = manager;
    manager->AddRef();
    manager->Init(this);

    session = players->CreateSessionPlayer();

    ShareAssignOwned(handle, new ClientHandle());
    terrain->Create();
    sounds->Create();
    texts->Create();
    {
        std::string path = g_textFolder + worlddata::TextsFileText();
        texts->AddSource(path);
    }
    {
        std::string path = g_textFolder + worlddata::TextsLmpFileText();
        texts->AddSource(path);
    }

    auto* lists = new LightningManager();
    listManager = lists;
    lists->Init(this);
    auto* arrays = new ClientArrayManager();
    arrayManager = arrays;
    arrays->Init(this);
    auto* owned = new MusicSystem();
    music = owned;
    owned->Create();

    rainSnow.PrepareBuffers();
    rainSnow.world = this;
    platforms.Create();
    if (worlddata::ShadowSetting() > 0)
        flags |= WorldClientFlag::DrawShadows;
    else
        flags &= ~WorldClientFlag::DrawShadows;

    if (SpellManager()) ReferenceEveryObject(SpellManager());

    hits.reserve(400);
    nextClientObjectId = -100000;
    cave = 0;
    partyDanger = 0;
    GridToFour(secondGrid);
    GridToFour(objectGrid);
    ticksRun = 0;
    lightLevelsCountdown = 0;
}

void WorldClient::ReferenceItemsAndSpells() {
    if (ItemFactory()) ReferenceEveryObject(ItemFactory());
    // The item factory is tested again before walking the spells.
    if (ItemFactory()) ReferenceEveryObject(SpellManager());
}

void WorldClient::ReleaseItemsAndSpells() {
    if (ItemFactory()) {
        ReleaseEveryObject(ItemFactory());
        ItemFactory()->DestroyAll();
    }
    if (SpellManager()) {
        ReleaseEveryObject(SpellManager());
        SpellManager()->DestroyAll();
    }
}

int WorldClient::LoadItemsAndSpells(ByteStream* block) {
    ReleaseItemsAndSpells();
    if (block->Size() != 0) {
        spellManager->Load(block);
        itemManager->Load(block);
        spellManager->LoadObjects(block);
        itemManager->LoadObjects(block);
        ReferenceItemsAndSpells();
    }
    return 1;
}

int LoadManagerObjects(BaseManager* manager, ByteStream* stream) {
    for (;;) {
        std::uint8_t more;
        stream->ReadValue(more);
        if (more == 0) return 1;
        if (!stream->ok) return 0;
        int id;
        int type;
        std::uint32_t extra;
        stream->ReadValue(id);
        stream->ReadValue(type);
        stream->ReadValue(extra);
        if (EngineObject* old = manager->FindObject(static_cast<unsigned>(id))) {
            if (old->refCount == 0) old->refCount = 1;
            old->Release();
        }
        EngineObject* object = manager->CreateObject(type, id);
        object->prototypeId = extra;
        object->Load(stream);
        object->AddRef();
    }
}

int WorldClient::LoadNewItemsAndSpells(ByteStream* block) {
    block->Seek(0);
    block->ok = 1;
    if (!LoadManagerObjects(SpellManager(), block)) return 0;
    return LoadManagerObjects(ItemFactory(), block) != 0;
}

void PlatformSet::Create() {
    list.clear();
}

void PlatformSet::Destroy() { list.clear(); }

PlatformSet::~PlatformSet() {
    list.clear();
}

// What LoadManagerObjects reads back: per object a 1, its id, type and a field, then its own Save; a 0
// ends the list.
void SaveListedObject(EngineObject* object, ByteStream* stream) {
    std::uint8_t more = 1;
    stream->WriteValue(more);
    int id = object->id;
    stream->WriteValue(id);
    int type = object->type;
    stream->WriteValue(type);
    std::uint32_t extra = object->prototypeId;
    stream->WriteValue(extra);
    object->Save(stream);
}

void SaveListedObjectsEnd(ByteStream* stream) {
    std::uint8_t more = 0;
    stream->WriteValue(more);
}

LightningManager::LightningManager() {
}

LightningManager::~LightningManager() {
    for (auto node = newBolts.begin(); node != newBolts.end(); ++node)
        if (*node) (*node)->Release();
    newBolts.clear();
}

ClientArrayManager::ClientArrayManager() {}

ClientArrayManager::~ClientArrayManager() {
    VisFogManagerDestroy(this);
    texture.Reset();
}

int WorldClient::LoadZone(ByteStream* block) {
    g_gameFlow.UpdateAndAcknowledge();
    std::string map;
    std::string mob;
    sounds->SetEffectsVolume(0);
    sounds->StopAll(1);
    partyDanger = 0;

    // The block: the tick, the map and .mob names, the exits, the cave flag, the unit looks.
    block->ReadValue(tick);
    map = block->ReadString();
    mob = block->ReadString();
    int exitCount;
    block->ReadValue(exitCount);
    exits.resize(exitCount);
    for (ZoneExit& exit : exits) exit.Read(block);
    int newCave;
    block->ReadValue(newCave);
    UnitLooks looks = ReadUnitLooks(block);
    cave = newCave;
    GameEnsureZone(map, mob);
    ShowLoadingMovie(kProgressBlockRead);

    bloodPrints.clear();
    footprints.clear();
    scorchMarks.clear();
    ResetZoneObjects();
    ClearMaps();
    ReleaseItemsAndSpells();
    ClientParticleManager()->ReleaseUnheld();
    session->sessionKind = 0;
    ShowLoadingMovie(kProgressZoneCleared);
    terrain::Terrain* currentTerrain = terrain;
    if (static_cast<int>(map.size()) > 1 && map[1] != ':') {
        modfolders::SelectMaps(map, modfolders::MapsSite::ClientLoadZone);
        map = MapsFolder() + map;
    }
    currentTerrain->ClearMap();
    // The texture manager is fetched for both calls; the first does not use it.
    ForgetComposedTextures();
    TextureManager()->FreeCache();
    ShowLoadingMovie(kProgressMapFreed);

    // The allod of the local player's zone names the texture pages, the lights and the music.
    const MapRecord* zone = LocalZoneRecord();
    std::string& allod = zoneName;
    allod = worlddata::DefaultAllodText();
    if (zone) allod = zone->allod;
    str::MakeLower(allod);
    if (!currentTerrain->LoadMap(map, allod, newCave == kCave))
        return game::FatalError(worlddata::CantLoadTerrainFormat(), map);

    FitGrid(objectGrid, currentTerrain);
    FitGrid(secondGrid, currentTerrain);
    if (!worlddata::WaterWavesOption()) currentTerrain->landscapeFlags &= ~terrain::LandscapeFlag::Waves;
    if (newCave == kCave) currentTerrain->landscapeFlags |= terrain::LandscapeFlag::Waves;
    if (!worlddata::SelfShadowingOption()) currentTerrain->landscapeFlags &= ~terrain::LandscapeFlag::SunShadows;
    if (skyDome) {
        Ref<CTexture>& skyColour = skyDome->texture;
        if (newCave == kCave) {
            LoadNamedTexture(skyColour, kSkyTextureKind, worlddata::CaveSkyText());
            skyDome->turns = 0;
        } else {
            LoadNamedTexture(skyColour, kSkyTextureKind, worlddata::OpenSkyText());
            skyDome->turns = 1;
        }
    }
    std::string lights = worlddata::LightsPrefixText();
    const bool effects = worlddata::AudioOption() && options::g_options.audioSfx;
    if (cave == kCave) {
        lights += worlddata::CaveText();
        if (effects) sounds->SetReverb(kCaveReverbRoom, kCaveReverbLevel, kCaveReverbTime);
    } else if (effects) {
        sounds->reverb[0] = 0.0f;
    }
    {
        std::string table = lights + allod;
        std::string file = table + worlddata::IniSuffixText();
        LoadLights(file);
    }
    CreateExitMarkers();
    worlddata::g_lastLitHour = -1.0f;
    UpdateLighting();
    if (worlddata::AudioOption() && options::g_options.audioStreaming) {
        if (InFinalZone()) music->SetZoneMode(worlddata::FinalMusicText(), cave == kCave);
        else music->SetZoneMode(allod, cave == kCave);
    }

    // The local server's pending messages follow this block in the view's stream.
    LocalView()->AppendReceived(&g_serverWorld.zoneHeader);
    ticksRun = 0;
    BuildUnitSkins(&looks.figured, &looks.dressed);
    // A wind option other than 0 sets the waves.
    const float wind = worlddata::DefaultWindForceOption();
    EI_NAN_AUDIT(wind);
    if (wind != 0.0f) g_clientWorld.terrain->SetWaveStrength(wind);
    ShowLoadingMovie(kProgressZoneReady);
    LocalView()->SendResume();
    CloseLoadingMovie();
    return 1;
}
