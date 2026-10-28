// The map object manager ("map objects", class id 0x2D): its lifetime, the objects it makes complete with
// their halves, the animation database lookup and the defaults of an object without a prototype. The object
// factory is unit_create_object.cpp's.
#include "unit_create.h"
#include "game_strings.h"

#include "base_manager.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "resource_manager.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "world_data.h"

#include "lib/crt.h"

#include <new>
#include <string>



namespace {

constexpr const char* kFolderName = "map objects";
constexpr const char* kClassName = "CMapObjectManager";
constexpr const char* kNoDatabaseText = "Can't find animation database file for \"%s\".\n";
constexpr int kNoId = -1;

// Types the managers make: the animation state (the figure manager) and the side halves (the
// world's specific manager; the client's are the 3D object's, the map object's and the unit's
// client halves, the server's theirs).
constexpr int kAnimationStateClass = 0x300D;
constexpr int kObject3DHalf = 0x46;
constexpr int kMapObjectHalf = 0x47;
constexpr int kUnitHalf = 0x48;
constexpr int kTorchHalf = 0x4B;

constexpr std::uint32_t kKeyVariantBits = 0x7FFF;      // an entry must have every one the key asks for
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly

// The third half an object of type `type` gets (a switch over a table for types 0x32-0x3A), or 0 for
// none.
int ThirdHalfOf(int type) {
    switch (type) {
    case 0x32: case 0x33: case 0x34:
        return kUnitHalf;
    case 0x3A:
        return kTorchHalf;
    default:
        return 0;
    }
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

CMapObjectManager::CMapObjectManager() {}

CMapObjectManager::~CMapObjectManager() {}

const char* CMapObjectManager::GetFolderName() { return kFolderName; }
const char* CMapObjectManager::ClassName() { return kClassName; }
ClassId CMapObjectManager::GetClassId() { return kClassMapObjectManager; }
int CMapObjectManager::Create() { return CreateManager(); }
CEngineObject* CMapObjectManager::CreateObject(int type, int id) { return CreateMapObject(type, id); }

int CMapObjectManager::CreateManager() {
    std::string folder = *g_textFolder;
    if (!CBaseManager::Create()) return 0;
    AddArchive((folder + "database.res"));
    return 1;
}

CMapObject* CMapObjectManager::FindMapObject(unsigned id) const { return FindObjectAs<CMapObject>(id); }

CEngineObject* CMapObjectManager::CreateMapObject(int type, int id) {
    CResourceManager* manager = this;
    // The base's CreateObject, not the virtual (that one is this function's caller).
    auto* object = static_cast<CMapObject*>(CBaseManager::CreateObject(type, id));
    if (!object) return nullptr;
    auto* figures = manager->ContextAsWorld()->FigureManager();
    object->SetAnimationState(figures->CreateObjectAs<CFigureAnimationState>(kAnimationStateClass, kNoId));
    object->AttachObject(manager->ContextAsWorld()->SpecificManager()->CreateObject(kObject3DHalf, kNoId));
    object->AttachObject(manager->ContextAsWorld()->SpecificManager()->CreateObject(kMapObjectHalf, kNoId));
    if (int half = ThirdHalfOf(type))
        object->AttachObject(manager->ContextAsWorld()->SpecificManager()->CreateObject(half, kNoId));
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
    if (object->GetClassId() != kClassUnit) {
        SetDefaults(&vitals, nullptr);
        object->SetServerData(&vitals);
    } else {
        UnitStats stats{};
        stats.Reset();
        SetDefaults(&vitals, &stats);
        object->SetStats(&stats);
    }
    object->ScaleEffectChanged(0);
    object->prototypeId_ = 0;
    return 1;
}
