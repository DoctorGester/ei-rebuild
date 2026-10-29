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
constexpr int kNoId = -1;

constexpr std::uint32_t kKeyVariantBits = 0x7FFF;      // an entry must have every one the key asks for
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

CMapObjectManager::CMapObjectManager() {}

CMapObjectManager::~CMapObjectManager() {}

const char* CMapObjectManager::GetFolderName() { return kFolderName; }
const char* CMapObjectManager::ClassName() { return kClassName; }
ClassId CMapObjectManager::GetClassId() { return ClassId::MapObjectManager; }
void CMapObjectManager::Create() { CreateManager(); }
CEngineObject* CMapObjectManager::CreateObject(int type, int id) { return CreateMapObject(type, id); }

void CMapObjectManager::CreateManager() {
    std::string folder = g_textFolder;
    CBaseManager::Create();
    AddArchive((folder + "database.res"));
}

CMapObject* CMapObjectManager::FindMapObject(unsigned id) const { return FindObjectAs<CMapObject>(id); }

CEngineObject* CMapObjectManager::CreateMapObject(int type, int id) {
    CResourceManager* manager = this;
    const auto kind = static_cast<MapObjectType>(type);
    // The base's CreateObject, not the virtual (that one is this function's caller).
    auto* object = static_cast<CMapObject*>(CBaseManager::CreateObject(type, id));
    if (!object) return nullptr;
    auto* figures = manager->ContextAsWorld()->FigureManager();
    object->SetAnimationState(figures->CreateObjectAs<CFigureAnimationState>(FigureObjectType::AnimationState, kNoId));
    CBaseManager* halves = manager->ContextAsWorld()->SpecificManager();
    object->AttachObject(halves->CreateObjectAs<CEngineObject>(SideObjectType::Object3D, kNoId));
    object->AttachObject(halves->CreateObjectAs<CEngineObject>(SideObjectType::MapObject, kNoId));
    if (auto half = ThirdHalfOf(kind)) object->AttachObject(halves->CreateObjectAs<CEngineObject>(*half, kNoId));
    return object;
}

FigureAnimation* CMapObjectManager::FindAnimation(const std::string* figure, std::uint32_t key) {
    auto* database = static_cast<AnimationDatabase*>(GetFile((*figure + ".adb"), nullptr));
    if (!database) {
        game::DebugLog(kNoDatabaseText, *figure);
        return nullptr;
    }
    const std::uint32_t variant = key & kKeyVariantBits;
    for (int i = 0; i < static_cast<int>(database->count); ++i) {
        const std::uint32_t entry = database->animations[i].key;
        const bool variantFits = variant == 0 ? (entry & kKeyVariantBits) == 0 : (~entry & key & kKeyVariantBits) == 0;
        if (variantFits && ((entry ^ key) & kKeyKindBits) == 0) return &database->animations[i];
    }
    return nullptr;
}

int CMapObjectManager::GiveDefaults(CMapObject* object) {
    // The vitals and statistics start zeroed, so the fields Reset and SetDefaults leave (the shares and factors,
    // fixedMaxHitPoints, builtFlags, the layers' spare values, ...) are 0, not what the stack held: a determinism
    // fix. The original's were CWorldServer::CreateObject's earlier callees' leftovers, heap addresses among them,
    // and gave the main menu's unmoco2 (the one unit added without a prototype) hit points that followed the
    // heap; the harness zeroes them in the original too (tests/verify/harness_leftovers.h).
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
    object->prototypeId_ = 0;
    return 1;
}
