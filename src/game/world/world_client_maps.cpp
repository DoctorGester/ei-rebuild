// The client world's object maps (helpers and the WorldClient methods that keep the maps). See
// docs/notes/world.md ("Client maps"). The client files every object in a sector grid (STLport lists of
// counted pointers, one per 16x16 cell) and a second grid; most of the helpers are the STLport and library
// instantiations behind those maps (config/library.csv).
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "game/app/movies.h"
#include "game/world/camera.h"
#include "engine/core/core_streams.h"
#include "game/effects/effects.h"
#include "engine/models/figure_manager.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/terrain/ground_print.h"
#include "engine/core/id_tables.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "engine/scene/light.h"
#include "game/effects/lightning.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/models/material.h"
#include "engine/mods/mod_folders.h"
#include "engine/models/link_loader.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/models/texture.h"
#include "game/map/map_database.h"
#include "game/app/options.h"
#include "platform/platform.h"
#include "game/players/player_client.h"
#include "game/players/player_manager.h"
#include "game/players/player.h"
#include "game/objects/side_managers.h"
#include "engine/scene/sky_object.h"
#include "game/sound/sound.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui_flying_hit_points.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"
#include "game/objects/map_object_manager.h"
#include "game/units/unit_db.h"
#include "game/units/unit_looks.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "engine/mods/maps_folder.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/intro_sort.h"
#include "lib/maths.h"
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
void RemoveHeld(List& list, MapObject* object) {
    HeldRef<MapObject> hold(object);
    std::erase_if(list, [object](const HeldRef<MapObject>& held) { return held.Get() == object; });
}

void SessionPlayerClear(Player* session) { session->Clear(); }

// The grid cell of the object's position, null outside the grid.
std::vector<HeldRef<MapObject>>* CellFor(SectorGrid& grid, MapObject* object) {
    std::uint32_t x = CellOf(object->localPosition.x);
    std::uint32_t y = CellOf(object->localPosition.y);
    if (x >= grid.width || y >= grid.height) return nullptr;
    return &grid.rows[y][x];
}

// From a picked object on: the first unit of the sorted hits. The cursor moves past entries whose
// object is gone or is not a unit; null at the end.
Unit* FirstUnitHit(WorldClient* client, MapObject* object) {
    Unit* unit;
    while (!(unit = UnitOrNull(object))) {
        do {
            if (++client->hitCursor == client->hits.data() + client->hits.size()) return nullptr;
            object = client->objectManager->FindMapObject(client->hitCursor->objectId);
        } while (!object);
    }
    return unit;
}

constexpr float kSteepest = 0.8f;   // the smallest normal z of a face added

// The selection highlight off: Selected and BoxMarked cleared, SetLit with 0 (its light goes off).
void Unhighlight(MapObject* object) {
    object->flags &= ~(MapObjectFlag::Selected | MapObjectFlag::BoxMarked);
    object->attached->SetLit(false);
}

// The grid set to 4 x 4 cells, reallocated unless its storage already has that size.
void GridToFour(SectorGrid& grid) {
    grid.Resize(1, 1);
    grid.width = 4;
    grid.height = 4;
    grid.Resize(4, 4);
}

void ReferenceEveryObject(BaseManager* manager) {
    ObjectListIterator walk;
    for (EngineObject* object = walk.First(manager); object; object = walk.Next()) object->AddRef();
}

void ReleaseEveryObject(BaseManager* manager) {
    ObjectListIterator walk;
    for (EngineObject* object = walk.First(manager); object; object = walk.Next()) object->Release();
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
    return str::Compare(LocalPlayerZone(), "gz20g") == 0;
}

} // namespace

void SectorGrid::Move(MapObject* object, const float* position) {
    Vector3& stored = object->localPosition; // x, y, z
    std::uint32_t oldX = CellOf(stored.x);
    std::uint32_t oldY = CellOf(stored.y);
    std::uint32_t newX = CellOf(position[0]);
    std::uint32_t newY = CellOf(position[1]);
    if (oldX == newX && oldY == newY) {
        std::memcpy(&stored, position, 12);
        return;
    }
    if (oldX < width && oldY < height) RemoveHeld(rows[oldY][oldX], object);
    std::memcpy(&stored, position, 12);
    if (newX < width && newY < height) rows[newY][newX].emplace_back(object);
}

void WorldClient::AddObject(MapObject* object) {
    for (MapObject* filed : objects) {
        if (filed->id == object->id) {
            object->AddRef();
            object->Release();
            return;
        }
    }
    auto entry = dynamicNodes.find(static_cast<std::uint32_t>(object->id));
    if (entry == dynamicNodes.end() && object->IsSynchronised()) {
        auto node = dynamicObjects.insert(dynamicObjects.end(), object);
        int key = object->id;
        idtable::Entry(dynamicNodes, dynamicMaxId, key) = node;
    }
    ClassId kind = object->GetClassId();
    if (kind == ClassId::Building || object->GetClassId() == ClassId::Lever) {
        Object3D* figure = object->AsObject3D();
        for (int i = 0; i < std::ssize(figure->parts); ++i) {
            if ((figure->PartBox(i)->bounds.flags >> 10) & 1) {
                int objectId = object->id;
                platforms.AddBox(objectId, figure->PartBox(i), object->localPosition, &object->localRotation);
            }
        }
    }
    Unit* unit = UnitOrNull(object);
    if (unit && session->sessionKind == 1) {
        if (unit->player == nullptr && ticksRun >= 1) unit->controller->RestartFadeIn();
    }

    object->AddRef();
    objects.push_back(object);

    if (auto* cell = CellFor(objectGrid, object)) RemoveHeld(*cell, object);
    if (auto* cell = CellFor(objectGrid, object)) cell->emplace_back(object);
    if (object->MapType() == MapObjectType::Hero && IsTextKey(object->id)) heroes.emplace_back(object);
}

void WorldClient::RefreshPlatforms(MapObject* object) {
    const int objectId = object->id;
    if (std::ranges::none_of(objects, [objectId](const MapObject* filed) { return filed->id == objectId; })) return;
    std::erase_if(platforms.list, [objectId](const Platform& face) { return face.id == objectId; });
    if (object->GetClassId() != ClassId::Building && object->GetClassId() != ClassId::Lever) return;
    Object3D* figure = object->AsObject3D();
    for (int i = 0; i < std::ssize(figure->parts); ++i) {
        if (!((figure->PartBox(i)->bounds.flags >> 10) & 1)) continue;
        platforms.AddBox(object->id, figure->PartBox(i), object->localPosition, &object->localRotation);
    }
}

bool WorldClient::QueueSynchronised(MapObject* object) {
    if (dynamicNodes.contains(static_cast<std::uint32_t>(object->id))) return false;
    auto node = dynamicObjects.insert(dynamicObjects.end(), object);
    int key = object->id;
    idtable::Entry(dynamicNodes, dynamicMaxId, key) = node;
    return true;
}

void WorldClient::MarkFootprintsInSight(const float* point, float range) {
    // A footprint is made visible when hypot(position - point) <= its scale * range.
    for (Footprint& print : footprints) {
        float dx = print.position.x - point[0];
        float dy = print.position.y - point[1];
        float distance = lib::Hypot(dx, dy);
        float limit = print.scale * range;
        if (distance <= limit) print.visible = 1;
    }
}

void WorldClient::ApplyWind(const Vector3& direction, float strength) {
    SetWind(direction, strength);
    if (terrain) terrain->SetWaves(direction, strength);
    rainSnow.SetWind(direction, strength);
}

void WorldClient::CentreOn(const Vector3& point) {
    Vector3 focus = point;
    focus.z = camera->focus.z;
    camera->SetFocus(focus);
    camera->ApplyView();
    camera->ClampAboveObjects();
}

float WorldClient::GroundHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = GridHeightAt(x, y);
    return platforms.SurfaceHeight(point, height);
}

float WorldClient::SurfaceHeightAt(float x, float y) {
    Vector3 point = {x, y, 0.0f};
    float height = RawSurfaceHeightAt(x, y);
    return platforms.SurfaceHeight(point, height);
}

void WorldClient::ShowNumber(int objectId, int value, std::uint32_t kind) {
    EngineObject* object = GetMapObjectManager()->FindObject(static_cast<unsigned>(objectId));
    if (!object || object->GetClassId() != ClassId::Unit) return;
    if (numbers) numbers->Add(objectId, value, kind);
}

void WorldClient::LoadUnitFaces() {
    for (MapObject* object : objects) {
        // The pointer is tested after the class call (the list holds no null).
        if (Unit* unit = UnitOrNull(object)) unit->ClientController()->LoadFaceTexture();
    }
}

const std::string& WorldClient::ExitTarget(int index) { return exits[index].target; }

void WorldClient::RemoveUnitsOf(const std::vector<PartyUnitRecord*>& records) {
    for (int i = 0; i < std::ssize(records); ++i)
        RemoveObject(static_cast<int>(records[i]->unitId));
}

void WorldClient::SendPause(int value) {
    LocalView()->SendPause(static_cast<std::uint8_t>(value));
}

void WorldClient::DropShadows() {
    if (options::g_options.shadows) return;
    for (MapObject* object : objects) object->ClientSpecific()->ClearShadow();
}

void WorldClient::RemoveObject(int objectId) {
    std::erase_if(platforms.list, [objectId](const Platform& face) { return face.id == objectId; });
    auto& ids = dynamicNodes;
    if (auto entry = ids.find(static_cast<std::uint32_t>(objectId)); entry != ids.end()) {
        dynamicObjects.erase(entry->second);
        if (auto again = ids.find(static_cast<std::uint32_t>(objectId)); again != ids.end()) ids.erase(again);
    }
    auto filed = std::ranges::find_if(objects, [objectId](const MapObject* object) { return object->id == objectId; });
    if (filed == objects.end()) return;
    MapObject* object = *filed;
    if (object->MapType() == MapObjectType::Hero && IsTextKey(objectId)) {
        RemoveHeld(heroes, object);
        if (session->sessionKind == 3) UnitOrNull(object)->ClientController()->DropFaceTexture();
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
    std::erase_if(objects, [](MapObject* object) {
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

MapObject* WorldClient::Pick(const Point& point, HitTest test) {
    hits.clear();
    MapObjectManager* manager = GetMapObjectManager();
    // The screen entries whose rectangle holds the point and whose object still exists and passes the test.
    for (const ScreenEntry& entry : screenEntries) {
        if (!entry.rect.Contains(point)) continue;
        MapObject* object = manager->FindMapObject(entry.objectId);
        if (!object) continue;
        const std::int32_t xy[2] = {point.x, point.y};
        Object3DSpecific* half = object->AsObject3D()->attached;
        if (!(test == HitTest::Bounds ? half->BoundsContainPoint(xy) : half->PartsContainPoint(xy))) continue;
        hits.push_back({entry.objectId, object->IsDead() != 0, half->PickDepth(point)});
    }
    if (hits.empty()) return nullptr;

    // Front to back: living objects before dead ones, each kind by the depth of its surface drawn under the point
    // (a deviation by the user's choice: the original sorted by the objects' origins, so a big platform
    // whose origin was nearer won over a lever drawn on its deck; docs/notes/renderer.md, section 3). Hits at the
    // same depth keep the screen list's order; the first that takes the click wins.
    std::ranges::stable_sort(hits, [](const PickHit& a, const PickHit& b) {
        return a.dead != b.dead ? !a.dead : a.depth < b.depth;
    });
    for (hitCursor = hits.data(); hitCursor != hits.data() + hits.size(); ++hitCursor) {
        if (MapObject* object = manager->FindMapObject(hitCursor->objectId)) return object;
    }
    return nullptr;
}

Unit* WorldClient::UnitAt(const Point& point, HitTest test) {
    MapObject* picked = Pick(point, test);
    return picked ? FirstUnitHit(this, picked) : nullptr;
}

Unit* WorldClient::UnitAt(const Point& point) {
    if (Unit* unit = UnitAt(point, HitTest::Parts)) return unit;
    return UnitAt(point, HitTest::Bounds);
}

std::vector<MapObject*> WorldClient::UnitsInBox(const Rect* rect) {
    std::vector<MapObject*> inside;
    for (auto& node : screenEntries) {
        MapObject* object = objectManager->FindMapObject(node.objectId);
        if (!object) continue;
        if (!object->AsObject3D()->attached->PartsIntersectRect(rect)) continue;
        inside.push_back(object);
    }
    return inside;
}

// The figure's part names (kHeroFigureParts): "hd", "bd", "hp", "hr" (the hair: "hr.%.2d"
// with the prototype's number, or left out at -1), "lh1".."rl3".
void WorldClient::CreatePartyUnits(const std::vector<PartyUnitRecord*>& party, char allArmour) {
    std::string hair;
    for (int i = 0; i < std::ssize(party); ++i) {
        auto* record = party[i];
        std::vector<std::string> parts;
        for (int n = 0; n < kHeroPartCount; ++n) {
            const char* name = kHeroFigureParts[n];
            if (std::string_view(name) == "hr") {
                UnitPrototypeRecord* prototype = g_unitDatabase.prototypes.FindByName(std::string(record->prototype));
                int number = prototype ? prototype->hair : 1;   // as the server's FillHeroParts
                if (number != -1) {
                    hair = str::Format("hr.%.2d", number);
                    parts.push_back(hair);
                }
            } else {
                parts.push_back(name);
            }
        }
        auto* manager = GetMapObjectManager();
        auto* unit = manager->CreateObjectAs<Unit>(MapObjectType::Hero, static_cast<int>(record->unitId));
        if (!unit) return;   // (leaving the loop is the same as returning here)
        UnitPrototypeRecord* prototype = g_unitDatabase.prototypes.FindByName(record->prototype);
        unit->prototypeId = static_cast<std::uint32_t>(prototype ? prototype - g_unitDatabase.prototypes.records.data() : -1);
        unit->SetSecondPrototypeByName(record->secondPrototype);
        unit->flags |= MapObjectFlag::Preview;
        unit->MakeFigure(record->figureName, &parts);
        Object3DClientSpecific* figure = unit->AsObject3D()->ClientSpecific();
        figure->RegisterPartMaterials();
        unit->PlayAnimation(nullptr);
        unit->RestartPose();
        const std::string& skin = record->skinTexture;
        figure->SetSkins("none", (skin == "none" ? std::nullopt : std::optional<std::string_view>(skin)));
        unit->WriteFullState();
        unit->SetStats(&record->stats);
        unit->SetFigureScale({record->scaleX, record->scaleY, record->height});
        std::memcpy(&unit->AsMapObject()->baseScale, &record->scaleX, 12);
        unit->ApplyBaseScale();
        unit->ReleaseAllItems();
        for (int j = 0; j < std::ssize(record->weapons); ++j)
            unit->AddWeapon(record->weapons[j]);
        UnitController* controller = unit->controller;
        if (!record->weapons.empty()) controller->ShowWeapon(0);
        for (int j = 0; j < std::ssize(record->armor); ++j) {
            Item* armour = record->armor[j];
            if (allArmour || armour->AsArmor()->GetType() != ArmorType::Helmet) unit->EquipArmour(armour);
        }
        for (int j = 0; j < std::ssize(record->quickItems); ++j)
            unit->AddQuickItem(record->quickItems[j]);
        unit->controller->RefreshEquipment();
        unit->controller->RefreshModel();
        unit->SetScriptName(record->scriptName);
        AddObject(unit);
    }
}

std::vector<Object3D*> WorldClient::ObjectsInRange(const Vector3& point, float radius) {
    std::vector<Object3D*> found;
    float maxX = terrain->width - 1.0f;
    float maxY = terrain->height - 1.0f;
    float y1 = MinKeepB(MaxKeepA(0.0f, point.y + radius), maxY);
    float x1 = MinKeepB(MaxKeepA(0.0f, point.x + radius), maxX);
    float y0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point.y - radius), maxY));
    float x0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point.x - radius), maxX));
    const CellRange cells = {static_cast<std::uint32_t>(static_cast<int>(x0)) >> 4, static_cast<std::uint32_t>(static_cast<int>(y0)) >> 4,
                             static_cast<std::uint32_t>(static_cast<int>(x1)) >> 4, static_cast<std::uint32_t>(static_cast<int>(y1)) >> 4};
    objectGrid.ForEachObject(cells, [&](MapObject* object) {
        float pz = point.z;
        float ox = object->localPosition.x + object->bounds.center.x;
        float oy = object->localPosition.y + object->bounds.center.y;
        float reach = lib::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y) + radius;
        float dx = ox - point.x;
        float dy = oy - point.y;
        float dz = pz - point.z;
        float distance = LengthSquared(Vector3{dx, dy, dz});
        float limit = reach * reach;
        // Skipped when the limit is below (or unordered).
        if (limit >= distance) found.push_back(object);
    });
    return found;
}

int PlatformSet::AddFace(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& normal, Platform** out) {
    *out = nullptr;
    if (normal.z < kSteepest) return 0;
    // (Every field is written below or by the caller; the record stays in place until the next face.)
    Platform* record = &*list.emplace(list.begin());
    *out = record;
    const Vector3 u = b - a;
    const Vector3 v = c - a;
    record->sizeU = Length(u);
    record->sizeV = Length(v);
    record->normal = normal;
    record->origin = a;
    record->axisU = u / record->sizeU;
    record->axisV = v / record->sizeV;
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
    // offset, rotated and placed.
    Vector3 corners[8] = {{high.x, low.y, low.z},  {low.x, low.y, low.z},   {low.x, high.y, low.z},
                          {low.x, high.y, high.z}, {high.x, high.y, high.z}, {high.x, low.y, high.z},
                          {high.x, low.y, low.z},  {low.x, low.y, low.z}};
    for (Vector3& corner : corners) corner = position + TransformColumnNormal(m, (pivot + corner) + offset);
    Vector3 normals[6] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
    for (Vector3& n : normals) n = TransformColumnNormal(m, n);
    for (int i = 0; i < 6; ++i) {
        Platform* record;
        if (!AddFace(corners[i + 1], corners[i], corners[i + 2], normals[i], &record)) continue;
        record->id = newId;
        record->centre = TransformColumnNormal(m, pivot + offset) + position;
        record->radius = item->bounds.radius;
    }
}

void WorldClient::Destroy() {
    g_camera.SetTarget(nullptr);
    numbers.reset();
    skyDome.reset();
    bird.reset();
    ReleaseItemsAndSpells();
    exitMarkers.clear();
    bloodPrints.clear();
    footprints.clear();
    scorchMarks.clear();
    rainSnow.Destroy();
    platforms.Destroy();
    platforms.list.clear();
    // Both grids, the dynamic objects, the frame's entries and the exits emptied as well: a switch of mods makes the
    // world again (Create), which starts from none.
    for (SectorGrid* grid : {&objectGrid, &secondGrid})
        for (int x = 0; x < static_cast<int>(grid->width); ++x)
            for (int y = 0; y < static_cast<int>(grid->height); ++y)
                grid->rows[y][x].clear();
    dynamicObjects.clear();
    dynamicNodes.clear();
    dynamicMaxId = 0;
    screenEntries.clear();
    hits.clear();
    hitCursor = nullptr;
    exits.clear();
    selectedIds.clear();
    heroes.clear();
    for (MapObject* object : objects)
        if (object) object->Release();
    objects.clear();
    if (Player* currentSession = session) {
        currentSession->Release();
        session = nullptr;
    }
    DestroyOwned(handle);
    World::Destroy();
    // In this order: the timed objects (World::Destroy) and the terrain stop their sounds, the music its stream, and
    // the textures and materials are released into their managers.
    listManager.reset();
    arrayManager.reset();
    particleManager.reset();
    terrain.reset();
    lightFactory.reset();
    materialManager.reset();
    textureManager.reset();
    music.reset();
    sounds.reset();
    texts.reset();
}

void WorldClient::ResetZoneObjects() {
    exitMarkers.clear();
    // The second grid's cells emptied (each object released).
    SectorGrid& second = secondGrid;
    for (int x = 0; x < static_cast<int>(second.width); ++x)
        for (int y = 0; y < static_cast<int>(second.height); ++y)
            second.rows[y][x].clear();
    ObjectListIterator it;
    for (EngineObject* object = it.First(TimedObjectManager()); object; object = it.Next()) object->Release();
    if (bird) bird->Reset();
}

void WorldClient::Deselect() {
    for (int objectId : selectedIds) {
        MapObject* found = objectManager->FindMapObject(static_cast<unsigned>(objectId));
        if (found && (found->flags & MapObjectFlag::Selected)) Unhighlight(found);
    }
    selectedIds.clear();
}

void WorldClient::Select(MapObject* object) {
    if (!object) return;
    object->AsObject3D()->UpdatePartBounds();
    if (!(object->flags & MapObjectFlag::Selected)) {
        if (!(object->flags & MapObjectFlag::BoxMarked)) object->attached->SetLit(true);
        object->flags |= MapObjectFlag::Selected;
    }
    if (std::ranges::find(selectedIds, object->id) == selectedIds.end()) selectedIds.push_back(object->id);
}

void WorldClient::MarkPerson(MapObject* object, bool marked) {
    if (object->flags & MapObjectFlag::Selected) return;
    if (((object->flags & MapObjectFlag::BoxMarked) != 0) == marked) return;
    if (marked) object->flags |= MapObjectFlag::BoxMarked;
    else object->flags &= ~MapObjectFlag::BoxMarked;
    object->attached->SetLit(marked);
}

void SectorGrid::Allocate() {
    cells = std::make_unique<std::vector<HeldRef<MapObject>>[]>(static_cast<std::size_t>(allocatedWidth) * allocatedHeight);
    rows = std::make_unique<std::vector<HeldRef<MapObject>>*[]>(static_cast<std::size_t>(allocatedHeight));
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
    World::Create();
    LoadLights("LightsGipat.ini");
    sounds = std::make_unique<SoundManager>();
    texts = std::make_unique<TextManager>();
    terrain = std::make_unique<terrain::Terrain>();
    zoneName = "gipath";

    specificManager = std::make_unique<ClientSpecificManager>();
    specificManager->Init(this);
    textureManager = std::make_unique<TextureManager>();
    textureManager->Init(this);
    materialManager = std::make_unique<MaterialManager>();
    materialManager->Init(this);
    lightFactory = std::make_unique<LightFactory>();
    lightFactory->Init(this);
    particleManager = std::make_unique<ParticleManager>();
    particleManager->Init(this);

    session = players->CreateSessionPlayer();

    ShareAssignOwned(handle, new ClientHandle());
    terrain->Create();
    sounds->Create();
    texts->Create();
    texts->AddSource(g_textFolder + "texts.res");
    texts->AddSource(g_textFolder + "textsLmp.res");

    listManager = std::make_unique<LightningManager>();
    listManager->Init(this);
    arrayManager = std::make_unique<ClientArrayManager>();
    arrayManager->Init(this);
    music = std::make_unique<MusicSystem>();
    music->Create();

    rainSnow.world = this;
    platforms.Create();

    if (GetSpellManager()) ReferenceEveryObject(GetSpellManager());

    hits.reserve(400);
    nextClientObjectId = -100000;
    cave = 0;
    partyDanger = 0;
    GridToFour(secondGrid);
    GridToFour(objectGrid);
    ticksRun = 0;
}

void WorldClient::ReferenceItemsAndSpells() {
    if (ItemFactory()) ReferenceEveryObject(ItemFactory());
    // The item factory is tested again before walking the spells.
    if (ItemFactory()) ReferenceEveryObject(GetSpellManager());
}

void WorldClient::ReleaseItemsAndSpells() {
    if (ItemFactory()) {
        ReleaseEveryObject(ItemFactory());
        ItemFactory()->DestroyAll();
    }
    if (GetSpellManager()) {
        ReleaseEveryObject(GetSpellManager());
        GetSpellManager()->DestroyAll();
    }
}

bool WorldClient::LoadItemsAndSpells(ByteStream* block) {
    ReleaseItemsAndSpells();
    if (block->Size() != 0) {
        spellManager->Load(block);
        itemManager->Load(block);
        spellManager->LoadObjects(block);
        itemManager->LoadObjects(block);
        ReferenceItemsAndSpells();
    }
    return KeepItemsAndSpells();
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

bool WorldClient::LoadNewItemsAndSpells(ByteStream* block) {
    block->Seek(0);
    block->ok = true;
    if (LoadManagerObjects(GetSpellManager(), block)) LoadManagerObjects(ItemFactory(), block);
    return KeepItemsAndSpells();
}

bool WorldClient::KeepItemsAndSpells() {
    if (HasEveryRecord(this)) return true;
    ReleaseItemsAndSpells();
    return false;
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
    sounds->StopAll(true);
    partyDanger = 0;

    // The block: the tick, the map and .mob names, the exits, the cave flag, the unit looks.
    block->ReadValue(tick);
    map = block->ReadString();
    shownMap = map;
    mob = block->ReadString();
    exits.resize(block->ReadCount());
    for (ZoneExit& exit : exits) exit.Read(block);
    int newCave;
    block->ReadValue(newCave);
    UnitLooks looks = ReadUnitLooks(block);
    cave = newCave;
    g_serverWorld.EnsureZone(map, mob);
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
    terrain::Terrain* currentTerrain = terrain.get();
    if (static_cast<int>(map.size()) > 1 && map[1] != ':') {
        modfolders::SelectMaps(map, modfolders::MapsSite::ClientLoadZone);
        map = MapsFolder() + map;
    }
    currentTerrain->ClearMap();
    ForgetComposedTextures();
    GetTextureManager()->FreeCache();
    ShowLoadingMovie(kProgressMapFreed);

    // The allod of the local player's zone names the texture pages, the lights and the music.
    const MapRecord* zone = LocalZoneRecord();
    std::string& allod = zoneName;
    allod = "gipat";
    if (zone) allod = zone->allod;
    // The zone's picture as it was kept (a zone left before, a save's load: client_visuals.h), used once.
    shownZone = zone ? zone->name : std::string();
    keptVisuals = ClientVisuals();
    if (!shownZone.empty()) {
        const std::string keptFile = ZoneVisualsPath(shownZone);
        keptVisuals = ClientVisuals::Read(keptFile);
        platform::RemoveFile(keptFile);
    }
    str::MakeLower(allod);
    if (!currentTerrain->LoadMap(map, allod, newCave == kCave))
        game::FatalExit("Can't load terrain or map file \"%s\".\n", map);

    FitGrid(objectGrid, currentTerrain);
    FitGrid(secondGrid, currentTerrain);
    if (newCave == kCave) currentTerrain->landscapeFlags |= terrain::LandscapeFlag::Waves;
    if (skyDome) {
        Ref<Texture>& skyColour = skyDome->texture;
        if (newCave == kCave) {
            LoadNamedTexture(skyColour, kSkyTextureKind, "Sky01");
            skyDome->turns = 0;
        } else {
            LoadNamedTexture(skyColour, kSkyTextureKind, "Sky00");
            skyDome->turns = 1;
        }
    }
    std::string lights = "Lights";
    const bool effects = options::g_options.audio && options::g_options.audioSfx;
    if (cave == kCave) {
        lights += "Cave";
        if (effects) sounds->SetReverb(kCaveReverbRoom, kCaveReverbLevel, kCaveReverbTime);
    } else if (effects) {
        sounds->reverb[0] = 0.0f;
    }
    LoadLights(lights + allod + ".ini");
    CreateExitMarkers();
    g_lastLitHour = -1.0f;
    UpdateLighting();
    if (options::g_options.audio && options::g_options.audioStreaming) {
        if (InFinalZone()) music->SetZoneMode("final", cave == kCave);
        else music->SetZoneMode(allod, cave == kCave);
    }

    // The local server's pending messages follow this block in the view's stream.
    LocalView()->AppendReceived(&g_serverWorld.zoneHeader);
    ticksRun = 0;
    BuildUnitSkins(&looks.figured, &looks.dressed);
    terrain->SetWaveStrength(options::kDefaultWindForce);
    keptVisuals.Restore(*this);
    ShowLoadingMovie(kProgressZoneReady);
    LocalView()->SendResume();
    CloseLoadingMovie();
    return 1;
}
