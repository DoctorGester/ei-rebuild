// The client world's object maps (helpers and the CWorldClient methods that keep the maps). See
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
#include "item_db_text.h"
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
#include "system_text.h"
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
#include "lib/str.h"

#include "ei/x87.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <list>
#include <new>
#include <string>
#include <vector>
#include <windows.h>

namespace {

// The cell index of a coordinate: rounded to nearest, then an unsigned shift by 4, so a
// negative coordinate gives a huge index that fails the bounds tests.
std::uint32_t CellOf(float coordinate) { return static_cast<std::uint32_t>(x87::fistp(coordinate)) >> 4; }

// Every element of `list` that is `object` erased (released), the object held meanwhile.
void RemoveHeld(std::list<HeldRef<CMapObject>>& list, CMapObject* object) {
    HeldRef<CMapObject> hold(object);
    std::erase_if(list, [object](const HeldRef<CMapObject>& held) { return held.Get() == object; });
}

// The hit test of the object's side half for the point: CollectBounds or CollectParts.
using HitTest = int (CObject3DSpecific::*)(const long* point);

// The entries of the screen list's nodes [first, last) whose rectangle holds (x, y) and whose
// object still exists and answers `test` go to `hits`.
HitCollector* CollectHits(HitCollector* out, std::list<ScreenEntry>::iterator first, std::list<ScreenEntry>::iterator last, std::vector<ScreenEntry>* hits, LONG x,
                          LONG y, CMapObjectManager* manager, HitTest test) {
    for (std::list<ScreenEntry>::iterator node = first; node != last; ++node) {
        const ScreenEntry* entry = &*node;
        POINT pt = {x, y};
        if (!PtInRect(&entry->rect, pt)) continue;
        CMapObject* object = manager->FindMapObject(entry->objectId);
        if (!object) continue;
        LONG point[2] = {x, y};
        CObject3DSpecific* half = object->Object3D()->attached;
        if (!(half->*test)(point)) continue;
        hits->push_back(*entry);
    }
    out->hits = hits;
    return out;
}

void SessionPlayerClear(CPlayer* session) { session->Clear(); }


std::list<HeldRef<CMapObject>>* CellFor(SectorGrid& grid, CMapObject* object) {
    std::uint32_t x = CellOf(object->localPosition_.x);
    std::uint32_t y = CellOf(object->localPosition_.y);
    if (x >= grid.width_ || y >= grid.height_) return nullptr;
    return &grid.rows_[y][x];
}

using Collect = HitCollector* (HitCollector::*)(std::list<ScreenEntry>::iterator, std::list<ScreenEntry>::iterator, std::vector<ScreenEntry>*, long, long,
                                                CMapObjectManager*);
CMapObject* Pick(CWorldClient* client, const POINT* point, Collect collect) {
    std::vector<ScreenEntry>& hits = client->hits_;
    hits.clear();
    CMapObjectManager* manager = client->MapObjectManager();
    std::list<ScreenEntry>& screen = client->screenEntries_;
    HitCollector functor;
    (functor.*collect)(screen.begin(), screen.end(), &hits, point->x, point->y, manager);
    if (hits.empty()) return nullptr;

    // Front to back (DepthLess with the top of the view stack); hits at the same depth keep the game's sort's
    // order, and the first that takes the click wins.
    DepthLess depth{g_sceneRenderer->pipeline_->Top(CTransformPipeline::kViewStack)->m, client->MapObjectManager()};
    lib::IntroSort(hits.data(), hits.data() + hits.size(),
                   [depth](const ScreenEntry& a, const ScreenEntry& b) { return depth.Less(&a, &b); });
    ScreenEntry*& cursor = client->hitCursor_;
    for (cursor = hits.data(); cursor != hits.data() + hits.size(); ++cursor) {
        if (CMapObject* object = manager->FindMapObject(cursor->objectId)) return object;
    }
    return nullptr;
}

// From a picked object on: the first unit of the sorted hits. The cursor moves past entries whose
// object is gone or is not a unit; null at the end.
CUnit* FirstUnitHit(CWorldClient* client, CMapObject* object) {
    while (object->GetClassId() != ClassId::Unit) {
        do {
            if (++client->hitCursor_ == client->hits_.data() + client->hits_.size()) return nullptr;
            object = client->objectManager_->FindMapObject(client->hitCursor_->objectId);
        } while (!object);
    }
    return static_cast<CUnit*>(object);
}

bool SameText(std::string_view a, std::string_view b) { return a == b; }

std::uint32_t Ftol(float v) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(v)); }

constexpr float kSteepest = 0.8f;   // the smallest normal z of a face added

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

// The selection highlight off: flags 0x10 and 0x100 cleared, the side half's empty virtual and SetLit
// with 0 (its light goes off).
void Unhighlight(CMapObject* object) {
    object->flags_ &= ~0x110u;
    CObject3DSpecific* half = object->attached;
    half->Unused25(0);
    half = object->attached;
    half->SetLit(0);
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
    ObjectListIterator walk{};
    for (CEngineObject* object = walk.First(manager); object; object = walk.Next()) ++object->refCount_;
    if (CBaseManager* held = walk.manager) --held->refCount_;
}

void ReleaseEveryObject(CBaseManager* manager) {
    ObjectListIterator walk{};
    for (CEngineObject* object = walk.First(manager); object; object = walk.Next()) object->Release();
    if (CBaseManager* held = walk.manager) --held->refCount_;
}

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
    return g_mapDatabase->FindMap(LocalPlayerZone());
}

bool InFinalZone() {
    return str::Compare(LocalPlayerZone(), worlddata::FinalZoneText()) == 0;
}

} // namespace

void SectorGrid::Move(CMapObject* object, const float* position) {
    float* stored = &object->localPosition_.x;   // x, y, z
    std::uint32_t oldX = CellOf(stored[0]);
    std::uint32_t oldY = CellOf(stored[1]);
    std::uint32_t newX = CellOf(position[0]);
    std::uint32_t newY = CellOf(position[1]);
    if (oldX == newX && oldY == newY) {
        std::memcpy(stored, position, 12);
        return;
    }
    if (oldX < width_ && oldY < height_) RemoveHeld(rows_[oldY][oldX], object);
    std::memcpy(stored, position, 12);
    if (newX < width_ && newY < height_) rows_[newY][newX].emplace_back(object);
}

HitCollector* HitCollector::CollectBounds(std::list<ScreenEntry>::iterator first, std::list<ScreenEntry>::iterator last, std::vector<ScreenEntry>* found, LONG x,
                                          LONG y, CMapObjectManager* manager) {
    return CollectHits(this, first, last, found, x, y, manager, &CObject3DSpecific::BoundsContainPoint);
}

HitCollector* HitCollector::CollectParts(std::list<ScreenEntry>::iterator first, std::list<ScreenEntry>::iterator last, std::vector<ScreenEntry>* found, LONG x,
                                         LONG y, CMapObjectManager* manager) {
    return CollectHits(this, first, last, found, x, y, manager, &CObject3DSpecific::PartsContainPoint);
}

int PendingIndex::Lookup(const std::uint32_t* key, std::list<CMapObject*>::iterator* value) const {
    auto node = find(*key);
    if (node == end()) return 0;
    *value = node->second;
    return 1;
}

int CWorldClient::AddObject(CMapObject* object) {
    for (CMapObject* filed : objects_) {
        if (filed->id_ == object->id_) {
            ++object->refCount_;
            object->Release();
            return 1;
        }
    }
    auto entry = dynamicNodes_.find(static_cast<std::uint32_t>(object->id_));
    if (entry == dynamicNodes_.end() && object->IsSynchronised()) {
        std::list<CMapObject*>& kept = dynamicObjects_;
        auto node = kept.emplace(kept.end(), object);
        int key = object->id_;
        hash400::Entry(dynamicNodes_, dynamicMaxId_, key) = node;
    }
    ClassId kind = object->GetClassId();
    if (kind == ClassId::Building || object->GetClassId() == ClassId::Lever) {
        CObject3D* figure = object->Object3D();
        for (int i = 0; i < std::ssize(figure->parts); ++i) {
            if ((figure->PartBox(i)->bounds.flags >> 10) & 1) {
                int id = object->id_;
                platforms_.AddBox(id, figure->PartBox(i), &object->localPosition_, &object->localRotation_);
            }
        }
    }
    CUnit* unit = UnitOrNull(object);
    if (unit && session_->sessionKind_ == 1) {
        if (unit->player == nullptr && ticksRun_ >= 1) unit->Controller()->RestartFadeIn();
    }

    ++object->refCount_;
    objects_.push_back(object);

    if (std::list<HeldRef<CMapObject>>* cell = CellFor(objectGrid_, object)) RemoveHeld(*cell, object);
    if (std::list<HeldRef<CMapObject>>* cell = CellFor(objectGrid_, object)) cell->emplace_back(object);
    if (object->MapType() == MapObjectType::Hero && (IsTextKey(object->id_) & 0xFF)) heroes_.emplace_back(object);
    return 1;
}

void CWorldClient::RefreshPlatforms(CMapObject* object) {
    const int id = object->id_;
    if (std::ranges::none_of(objects_, [id](const CMapObject* filed) { return filed->id_ == id; })) return;
    std::list<Platform>& platforms = platforms_.list;
    for (auto node = platforms.begin(); node != platforms.end();) {
        auto next = std::next(node);
        if (node->id == id) platforms.erase(node);
        node = next;
    }
    if (object->GetClassId() != ClassId::Building && object->GetClassId() != ClassId::Lever) return;
    CObject3D* figure = object->Object3D();
    for (int i = 0; i < std::ssize(figure->parts); ++i) {
        if (!((figure->PartBox(i)->bounds.flags >> 10) & 1)) continue;
        platforms_.AddBox(object->id_, figure->PartBox(i), &object->localPosition_, &object->localRotation_);
    }
}

bool CWorldClient::QueueSynchronised(CMapObject* object) {
    if (dynamicNodes_.contains(static_cast<std::uint32_t>(object->id_))) return false;
    std::list<CMapObject*>& kept = dynamicObjects_;
    auto node = kept.emplace(kept.end(), object);
    int key = object->id_;
    hash400::Entry(dynamicNodes_, dynamicMaxId_, key) = node;
    return true;
}

void CWorldClient::MarkFootprintsInSight(const float* point, float range) {
    // A footprint is made visible when hypot(position - point) <= its scale_ * range.
    for (Footprint& print : footprints_) {
        double dx = print.position_.x - point[0];
        double dy = print.position_.y - point[1];
        double distance = libcrt::Hypot(dx, dy);
        float limit = print.scale_ * range;
        EI_NAN_AUDIT(distance, limit);
        if (distance <= static_cast<double>(limit)) print.visible_ = 1;
    }
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
    if (!object || object->GetClassId() != ClassId::Unit) return;
    if (numbers_) numbers_->Add(id, value, kind);
}

void CWorldClient::LoadUnitFaces() {
    for (CMapObject* object : objects_) {
        // The pointer is tested after the class call (the list holds no null).
        if (CUnit* unit = UnitOrNull(object)) unit->ClientController()->LoadFaceTexture();
    }
}

const char* CWorldClient::ExitTarget(int index) { return exits_[index].target.c_str(); }

void CWorldClient::RemoveUnitsOf(const std::vector<PartyUnitRecord*>* records) {
    for (int i = 0; i < std::ssize(*records); ++i)
        RemoveObject(static_cast<int>((*records)[i]->unitId));
}

int CWorldClient::SendFlag10(int value) {
    return LocalView()->SendFlag10(static_cast<std::uint8_t>(value));
}

void CWorldClient::DropShadows() {
    for (CMapObject* object : objects_) {
        CObject3DClientSpecific* half = object->ClientSpecific();
        // Only the low byte is tested (ShadowsShown's "off" leaves the setting's upper bytes).
        if (!static_cast<std::uint8_t>(half->ShadowsShown())) half->ClearShadow();
    }
}

void CWorldClient::RemoveObject(int id) {
    std::list<Platform>& platforms = platforms_.list;
    for (auto node = platforms.begin(); node != platforms.end();) {
        auto next = std::next(node);
        if (node->id == id) platforms.erase(node);
        node = next;
    }
    PendingIndex& ids = dynamicNodes_;
    if (auto entry = ids.find(static_cast<std::uint32_t>(id)); entry != ids.end()) {
        std::list<CMapObject*>& kept = dynamicObjects_;
        std::list<CMapObject*>::iterator node = entry->second;
        kept.erase(node);
        if (auto again = ids.find(static_cast<std::uint32_t>(id)); again != ids.end()) ids.erase(again);
    }
    auto filed = std::ranges::find_if(objects_, [id](const CMapObject* object) { return object->id_ == id; });
    if (filed == objects_.end()) return;
    CMapObject* object = *filed;
    if (object->MapType() == MapObjectType::Hero && (IsTextKey(id) & 0xFF)) {
        RemoveHeld(heroes_, object);
        if (session_->sessionKind_ == 3) static_cast<CUnit*>(object)->ClientController()->DropFaceTexture();
    }
    if (std::list<HeldRef<CMapObject>>* cell = CellFor(objectGrid_, object)) RemoveHeld(*cell, object);
    objects_.erase(filed);
    object->Release();
}

void CWorldClient::ClearMaps() {
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            second.rows_[y][x].clear();
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            grid.rows_[y][x].clear();

    // Both emptied: the platform faces and the dynamic objects (the elements need no destruction).
    platforms_.list.clear();
    dynamicObjects_.clear();
    dynamicNodes_.clear();
    dynamicMaxId_ = 0;

    selectedIds_.clear();

    heroes_.clear();
    // The objects with a positive id released and unfiled.
    std::erase_if(objects_, [](CMapObject* object) {
        if (object->id_ <= 0) return false;
        object->Release();
        return true;
    });
    SessionPlayerClear(session_);
    if (CLinkFigureLoader* loader = figureLoader_) loader->ClearCache();
    figureManager_->FreeSpares();
    objectManager_->FreeSpares();
    specificManager_->FreeSpares();
}

CMapObject* CWorldClient::PickAt(const POINT* point) { return Pick(this, point, &HitCollector::CollectBounds); }
CMapObject* CWorldClient::PickAt2(const POINT* point) { return Pick(this, point, &HitCollector::CollectParts); }

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

void CWorldClient::UnitsInBox(const tagRECT* rect, std::list<CMapObject*>* out) {
    for (auto& node : screenEntries_) {
        CMapObject* object = objectManager_->FindMapObject(node.objectId);
        if (!object) continue;
        if (!object->Object3D()->attached->PartsIntersectRect(rect)) continue;
        out->push_back(object);
    }
}

// The figure's part names (worlddata::HeroPartText): "hd", "bd", "hp", "hr" (the hair: "hr.%.2d"
// with the prototype's number, or left out at -1), "lh1".."rl3".
void CWorldClient::CreatePartyUnits(const std::vector<PartyUnitRecord*>* party, char allArmour) {
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
        UnitPrototypeRecord* prototype = g_unitDatabase->prototypes.FindByName(record->texts[5]);
        unit->prototypeId_ = static_cast<std::uint32_t>(prototype ? prototype - g_unitDatabase->prototypes.records.data() : -1);
        unit->SetSecondPrototypeByName(record->texts[6]);
        unit->flags_ |= 0x4000000;
        if (!unit->MakeFigure(record->texts[2], &parts, 1)) return;
        CObject3DClientSpecific* figure = unit->Object3D()->ClientSpecific();
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
        CUnitController* controller = unit->Controller();
        if (!record->weapons.empty()) controller->ShowWeapon(0);
        for (int j = 0; j < std::ssize(record->armor); ++j) {
            CItem* armour = record->armor[j];
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

void CWorldClient::ObjectsInRange(const Vector3* point, float radius, std::list<CObject3D*>* out) {
    float maxX = terrain_->width - 1.0f;
    float maxY = terrain_->height - 1.0f;
    float y1 = MinKeepB(MaxKeepA(0.0f, point->y + radius), maxY);
    float x1 = MinKeepB(MaxKeepA(0.0f, point->x + radius), maxX);
    float y0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point->y - radius), maxY));
    float x0 = MaxKeepA(0.0f, MinKeepB(MaxKeepA(0.0f, point->x - radius), maxX));
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
    for (std::list<HeldRef<CMapObject>>* cell = &grid.rows_[y][x]; cell;) {
        for (CMapObject* object : *cell) {
            float pz = point->z;
            float ox = object->localPosition_.x + object->bounds.center.x;
            float oy = object->localPosition_.y + object->bounds.center.y;
            double reach = libcrt::Hypot(object->bounds.boxMax.x, object->bounds.boxMax.y) + radius;
            float dx = ox - point->x;
            float dy = oy - point->y;
            float dz = pz - point->z;
            double distance = LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
            double limit = reach * reach;
            // Skipped when the limit is below (or unordered).
            if (limit >= distance) {
                out->insert(out->end(), object);
            }
        }
        if (++x > lastX) {
            x = firstX;
            if (++y > lastY) return;
        }
        cell = &grid.rows_[y][x];
    }
}

int PlatformSet::AddFace(const Vector3* a, const Vector3* b, const Vector3* c, const Vector3* normal, Platform** out) {
    *out = nullptr;
    EI_NAN_AUDIT(normal->z);
    if (normal->z < kSteepest) return 0;
    // (Every field is written below or by the caller.)
    Platform* record = &list.emplace_front();
    *out = record;
    float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
    float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
    float lengthU = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz)));
    record->sizeU = lengthU;
    float lengthV = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(vx, vy, vz)));
    record->sizeV = lengthV;
    record->normal = *normal;
    record->origin = *a;
    record->axisU.x = ux / lengthU;
    record->axisU.y = uy / lengthU;
    record->axisU.z = uz / lengthU;
    record->axisV.x = vx / lengthV;
    record->axisV.y = vy / lengthV;
    record->axisV.z = vz / lengthV;
    return 1;
}

void PlatformSet::AddBox(int id, const FootprintPart* item, const Vector3* position, const Quaternion* rotation) {
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
        record->radius = item->bounds.radius;   // a dword move
    }
}

void CWorldClient::Destroy() {
    g_camera->SetTarget(nullptr);
    delete numbers_;
    numbers_ = nullptr;
    delete skyDome_;
    skyDome_ = nullptr;
    DeleteOwned(bird_);
    ReleaseItemsAndSpells();
    exitMarkers_.clear();
    bloodPrints_.clear();
    footprints_.clear();
    scorchMarks_.clear();
    rainSnow_.Destroy();
    platforms_.Destroy();
    platforms_.list.clear();
    SectorGrid& grid = objectGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            grid.rows_[y][x].clear();
    selectedIds_.clear();
    heroes_.clear();
    for (CMapObject* object : objects_)
        if (object) object->Release();
    objects_.clear();
    if (CPlayer* session = session_) {
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
    DropAndDelete(lightFactory_);
    DropAndDelete(materialManager_);
    DropAndDelete(textureManager_);
    DeleteOwned(music_);
    DropAndDelete(sounds_);
    DropAndDelete(texts_);
}

void CWorldClient::ResetZoneObjects() {
    exitMarkers_.clear();
    // The second grid's cells emptied (each object released).
    SectorGrid& second = secondGrid_;
    for (int x = 0; x < static_cast<int>(second.width_); ++x)
        for (int y = 0; y < static_cast<int>(second.height_); ++y)
            second.rows_[y][x].clear();
    ObjectListIterator it{nullptr, {}};
    for (CEngineObject* object = it.First(TimedObjectManager()); object; object = it.Next()) object->Release();
    if (bird_) bird_->Reset();
    if (it.manager) --it.manager->refCount_;
}

void CWorldClient::Deselect(CMapObject* object) {
    std::list<int>& selected = selectedIds_;
    auto* objects = objectManager_;
    if (!object) {
        for (auto& node : selected) {
            CMapObject* found = objects->FindMapObject(static_cast<unsigned>(node));
            if (found && (found->flags_ & 0x10)) Unhighlight(found);
        }
        selected.clear();
        return;
    }
    const int id = object->id_;
    std::list<int>::iterator node = selected.begin();
    while (node != selected.end() && (*node) != id) ++node;
    if (node == selected.end()) return;
    selected.erase(node);
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
    std::list<int>& selected = selectedIds_;
    const int id = object->id_;
    std::list<int>::iterator node = selected.begin();
    while (node != selected.end() && (*node) != id) ++node;
    if (node != selected.end()) return;
    selected.insert(selected.end(), id);
}

void SectorGrid::Allocate() {
    cells_ = std::make_unique<std::list<HeldRef<CMapObject>>[]>(static_cast<std::size_t>(allocatedWidth_) * allocatedHeight_);
    rows_ = std::make_unique<std::list<HeldRef<CMapObject>>*[]>(static_cast<std::size_t>(allocatedHeight_));
    for (int y = 0; y < allocatedHeight_; ++y) rows_[y] = cells_.get() + y * allocatedWidth_;
}

void SectorGrid::Free() {
    cells_.reset();
    rows_.reset();
}

void SectorGrid::Resize(int width, int height) {
    if (allocatedWidth_ == width && allocatedHeight_ == height) return;
    Free();
    allocatedWidth_ = width;
    allocatedHeight_ = height;
    Allocate();
}

int CWorldClient::Create() {
    CWorld::Create();
    {
        std::string path = *g_dataFolder + worlddata::TexturesFolderText();
        texturePath_ = path;
    }
    LoadLights(worlddata::LightsTableText());
    lightingMark_ = 0;
    sounds_ = NewCounted<CSoundManager>();

    texts_ = new CTextManager();
    ++texts_->refCount_;
    terrain_ = new Terrain();
    ++terrain_->refCount_;
    zoneName_ = worlddata::GipathText();

    specificManager_ = NewCounted<CClientSpecificManager>();
    specificManager_->Init(this);
    textureManager_ = NewCounted<CTextureManager>();
    textureManager_->CBaseManager::Init(this);
    materialManager_ = NewCounted<CMaterialManager>();
    materialManager_->Init(this);
    lightFactory_ = NewCounted<CLightFactory>();
    lightFactory_->Init(this);

    auto* manager = new CParticleManager();
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
        std::string path = *g_textFolder + worlddata::TextsFileText();
        texts_->AddSource(path);
    }
    {
        std::string path = *g_textFolder + worlddata::TextsLmpFileText();
        texts_->AddSource(path);
    }

    auto* lists = new CLightningManager();
    listManager_ = lists;
    lists->Init(this);
    auto* arrays = new ClientArrayManager();
    arrayManager_ = arrays;
    arrays->Init(this);
    auto* owned = new CMusicSystem();
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
        std::string path = *g_dataFolder + worlddata::CursorFileText();
        cursor_ = text::utf8::LoadCursorFromFileA(path.c_str());
    }

    if (SpellManager()) ReferenceEveryObject(SpellManager());

    hits_.reserve(400);
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
        spellManager_->Load(block);
        itemManager_->Load(block);
        spellManager_->LoadObjects(block);
        itemManager_->LoadObjects(block);
        ReferenceItemsAndSpells();
    }
    return 1;
}

int LoadManagerObjects(CBaseManager* manager, CStream* stream) {
    for (;;) {
        std::uint8_t more;
        stream->ReadValue(more);
        if (more == 0) return 1;
        if (!stream->ok_) return 0;
        int id;
        int type;
        std::uint32_t extra;
        stream->ReadValue(id);
        stream->ReadValue(type);
        stream->ReadValue(extra);
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

int CWorldClient::LoadNewItemsAndSpells(CStream* block) {
    block->Seek(0);
    block->ok_ = 1;
    if (!LoadManagerObjects(SpellManager(), block)) return 0;
    return LoadManagerObjects(ItemFactory(), block) != 0;
}

int PlatformSet::Create() {
    list.clear();
    return 1;
}

void PlatformSet::Destroy() { list.clear(); }

PlatformSet::~PlatformSet() {
    list.clear();
}

// What LoadManagerObjects reads back: per object a 1, its id, type and a field, then its own Save; a 0
// ends the list.
void SaveListedObject(CEngineObject* object, CStream* stream) {
    std::uint8_t more = 1;
    stream->WriteValue(more);
    int id = object->id_;
    stream->WriteValue(id);
    int type = object->type_;
    stream->WriteValue(type);
    std::uint32_t extra = object->prototypeId_;
    stream->WriteValue(extra);
    object->Save(stream);
}

void SaveListedObjectsEnd(CStream* stream) {
    std::uint8_t more = 0;
    stream->WriteValue(more);
}

CLightningManager::CLightningManager() {
}

CLightningManager::~CLightningManager() {
    for (auto node = newBolts_.begin(); node != newBolts_.end(); ++node)
        if (*node) (*node)->Release();
    newBolts_.clear();
}

ClientArrayManager::ClientArrayManager() {}

ClientArrayManager::~ClientArrayManager() {
    VisFogManagerDestroy(this);
    if (CTexture* held = texture) held->Release();
}

int CWorldClient::LoadZone(CStream* block) {
    g_gameFlow->UpdateAndAcknowledge();
    std::string map;
    std::string mob;
    sounds_->SetEffectsVolume(0);
    sounds_->StopAll(1);
    partyDanger_ = 0;

    // The block: the tick, the map and .mob names, the exits, the cave flag, the unit looks.
    block->ReadValue(tick_);
    block->ReadInto(&map);
    block->ReadInto(&mob);
    int exitCount;
    block->ReadValue(exitCount);
    exits_.resize(exitCount);
    for (ZoneExit& exit : exits_) exit.Read(block);
    int cave;
    block->ReadValue(cave);
    UnitLooks looks;
    ReadUnitLooks(block, &looks.figured, &looks.dressed);
    cave_ = cave;
    GameEnsureZone(map, mob);
    ShowLoadingMovie(kProgressBlockRead);

    bloodPrints_.clear();
    footprints_.clear();
    scorchMarks_.clear();
    ResetZoneObjects();
    ClearMaps();
    ReleaseItemsAndSpells();
    ClientParticleManager()->ReleaseUnheld();
    session_->sessionKind_ = 0;
    ShowLoadingMovie(kProgressZoneCleared);
    Terrain* terrain = terrain_;
    if (static_cast<int>(map.size()) > 1 && map[1] != ':') {
        std::string folder;
        modfolders::SelectMaps(map, modfolders::MapsSite::ClientLoadZone);
        MapsFolder(&folder);
        map = folder + map;
    }
    terrain->ClearMap();
    // The texture manager is fetched for both calls; the first does not use it.
    ForgetComposedTextures();
    TextureManager()->FreeCache();
    ShowLoadingMovie(kProgressMapFreed);

    // The allod of the local player's zone names the texture pages, the lights and the music.
    const MapRecord* zone = LocalZoneRecord();
    std::string& allod = zoneName_;
    allod = worlddata::DefaultAllodText();
    if (zone) allod = zone->allod;
    str::MakeLower(allod);
    if (!terrain->LoadMap(map, allod, cave == kCave))
        return game::FatalError(worlddata::CantLoadTerrainFormat(), map);

    FitGrid(objectGrid_, terrain);
    FitGrid(secondGrid_, terrain);
    if (!worlddata::WaterWavesOption()) terrain->landscapeFlags &= ~LandscapeFlag::Waves;
    if (cave == kCave) terrain->landscapeFlags |= LandscapeFlag::Waves;
    if (!worlddata::SelfShadowingOption()) terrain->landscapeFlags &= ~LandscapeFlag::SunShadows;
    terrain->SetLightInterval(worlddata::LightingFrequencyOption() / kFrameMilliseconds);
    if (skyDome_) {
        Ref<CTexture>& sky = skyDome_->texture;
        if (cave == kCave) {
            LoadNamedTexture(sky, kSkyTextureKind, worlddata::CaveSkyText());
            skyDome_->turns = 0;
        } else {
            LoadNamedTexture(sky, kSkyTextureKind, worlddata::OpenSkyText());
            skyDome_->turns = 1;
        }
    }
    std::string lights = worlddata::LightsPrefixText();
    const bool effects = worlddata::AudioOption() && options::g_options->audioSfx_;
    if (cave_ == kCave) {
        lights += worlddata::CaveText();
        if (effects) sounds_->SetReverb(kCaveReverbRoom, kCaveReverbLevel, kCaveReverbTime);
    } else if (effects) {
        sounds_->reverb[0] = 0.0f;
    }
    {
        std::string table = lights + allod;
        std::string file = table + worlddata::IniSuffixText();
        LoadLights(file);
    }
    CreateExitMarkers();
    worlddata::g_lastLitHour = -1.0f;
    UpdateLighting();
    if (worlddata::AudioOption() && options::g_options->audioStreaming_) {
        if (InFinalZone()) music_->SetZoneMode(worlddata::FinalMusicText(), cave_ == kCave);
        else music_->SetZoneMode(allod, cave_ == kCave);
    }

    // The local server's pending messages follow this block in the view's stream.
    LocalView()->AppendReceived(&g_serverWorld->zoneHeader_);
    ticksRun_ = 0;
    BuildUnitSkins(&looks.figured, &looks.dressed);
    // A wind option other than 0 sets the waves.
    const float wind = worlddata::DefaultWindForceOption();
    EI_NAN_AUDIT(wind);
    if (wind != 0.0f) g_clientWorld->terrain_->SetWaveStrength(wind);
    ShowLoadingMovie(kProgressZoneReady);
    LocalView()->SendResume();
    CloseLoadingMovie();
    return 1;
}
