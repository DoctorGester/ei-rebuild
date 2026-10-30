// The map object manager ("map objects", class id 0x2D): its lifetime, the objects it makes complete with
// their halves, the animation database lookup and the defaults of an object without a prototype. The object
// factory is unit_create_object.cpp's.
#include "base_manager.h"
#include "figure_manager.h"   // FigureObjectType
#include "game_strings.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "resource_manager.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"
#include "world_data.h"

#include "lib/crt.h"

#include <new>
#include <optional>
#include <string>

namespace {

constexpr const char* kFolderName = "map objects";
constexpr const char* kClassName = "CMapObjectManager";
constexpr const char* kNoDatabaseText = "Can't find animation database file for \"%s\".\n";

constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly

// The third half an object of type `type` gets (the world's specific manager makes the client's halves: the 3D
// object's, the map object's and the unit's; the server's theirs), or null for none.
std::optional<SideObjectType> ThirdHalfOf(MapObjectType type) {
    if (IsUnitType(type)) return SideObjectType::Unit;
    if (type == MapObjectType::Torch) return SideObjectType::Torch;
    return std::nullopt;
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

MapObjectManager::MapObjectManager() {}

MapObjectManager::~MapObjectManager() {}

const char* MapObjectManager::GetFolderName() { return kFolderName; }
const char* MapObjectManager::ClassName() { return kClassName; }
ClassId MapObjectManager::GetClassId() { return ClassId::MapObjectManager; }
EngineObject* MapObjectManager::CreateObject(int newType, int newId) { return CreateMapObject(newType, newId); }

void MapObjectManager::Create() {
    std::string folder = g_textFolder;
    BaseManager::Create();
    AddArchive((folder + "database.res"));
}

MapObject* MapObjectManager::FindMapObject(unsigned objectId) const { return FindObjectAs<MapObject>(objectId); }

EngineObject* MapObjectManager::CreateMapObject(int newType, int newId) {
    ResourceManager* manager = this;
    const auto kind = static_cast<MapObjectType>(newType);
    // The base's CreateObject, not the virtual (that one is this function's caller).
    auto* object = static_cast<MapObject*>(BaseManager::CreateObject(newType, newId));
    if (!object) return nullptr;
    auto* figures = manager->ContextAsWorld()->GetFigureManager();
    object->SetAnimationState(figures->CreateObjectAs<FigureAnimationState>(FigureObjectType::AnimationState, kNoObjectId));
    BaseManager* halves = manager->ContextAsWorld()->SpecificManager();
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::Object3D, kNoObjectId));
    object->AttachObject(halves->CreateObjectAs<EngineObject>(SideObjectType::MapObject, kNoObjectId));
    if (auto half = ThirdHalfOf(kind)) object->AttachObject(halves->CreateObjectAs<EngineObject>(*half, kNoObjectId));
    return object;
}

FigureAnimation* MapObjectManager::FindAnimation(const std::string* figure, std::uint32_t key) {
    auto* database = static_cast<AnimationDatabase*>(GetFile((*figure + ".adb"), nullptr));
    if (!database) {
        game::DebugLog(kNoDatabaseText, *figure);
        return nullptr;
    }
    const std::uint32_t variant = key & kAnimationVariantBits;
    for (int i = 0; i < static_cast<int>(database->count); ++i) {
        const std::uint32_t entry = database->animations[i].key;
        const bool variantFits =
            variant == 0 ? (entry & kAnimationVariantBits) == 0 : (~entry & key & kAnimationVariantBits) == 0;
        if (variantFits && ((entry ^ key) & kKeyKindBits) == 0) return &database->animations[i];
    }
    return nullptr;
}

void MapObjectManager::GiveDefaults(MapObject* object) {
    // The vitals and statistics start zeroed, so the fields Reset and SetDefaults leave (the shares and factors,
    // fixedMaxHitPoints, builtFlags, the layers' spare values, ...) are 0, not what the stack held: a determinism
    // fix. The original's were WorldServer::CreateObject's earlier callees' leftovers, heap addresses among them,
    // and gave the main menu's unmoco2 (the one unit added without a prototype) hit points that followed the
    // heap.
    UnitVitals vitals{};
    vitals.Reset();
    if (object->GetClassId() != ClassId::Unit) {
        SetDefaults(&vitals, nullptr);
        object->SetServerData(&vitals);
    } else {
        UnitStats stats{};
        stats.Reset();
        SetDefaults(&vitals, &stats);
        object->SetStats(&stats);
    }
    object->ScaleEffectChanged(false);
    object->prototypeId = 0;
}
