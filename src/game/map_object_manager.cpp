// The map object manager ("map objects", class id 0x2D, vtable 0073DCD8; A-U12): its lifetime, the
// objects it makes complete with their halves, the animation database lookup and the defaults of
// an object without a prototype. The object factory (slot 14) is unit_create_object.cpp's.
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

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_engine.h"


namespace {

constexpr std::uintptr_t kManagerVtable = std::uintptr_t(0x0073DCD8);
constexpr std::uintptr_t kFolderName = std::uintptr_t(0x0078F1D4);      // "map objects"
constexpr std::uintptr_t kClassName = std::uintptr_t(0x0078F1E0);       // "CMapObjectManager"
constexpr std::uintptr_t kNoDatabaseText = std::uintptr_t(0x0078F1F4);  // Can't find animation database file for "%s".
constexpr std::uintptr_t kNoPrototypeText = std::uintptr_t(0x0078F330); // Can't find prototype "%s".
constexpr int kManagerClassId = 0x2D;
constexpr int kNoId = -1;

// Classes the managers make: the animation state (the figure manager) and the side halves (the
// world's specific manager; the client's are the 3D object's, the map object's and the unit's
// client halves, the server's theirs).
constexpr int kAnimationStateClass = 0x300D;
constexpr int kObject3DHalf = 0x46;
constexpr int kMapObjectHalf = 0x47;
constexpr int kUnitHalf = 0x48;
constexpr int kTorchHalf = 0x4B;

constexpr std::uint32_t kKeyVariantBits = 0x7FFF;      // an entry must have every one the key asks for
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly

CResourceManager* AsResourceManager(CMapObjectManager* manager) { return reinterpret_cast<CResourceManager*>(manager); }

// The third half an object of class `classId` gets (the switch of 00512A30: the table at 00512B08
// for classes 0x32-0x3A), or 0 for none.
int ThirdHalfOf(int classId) {
    switch (classId) {
    case 0x32: case 0x33: case 0x34: case 0x39:
        return kUnitHalf;
    case 0x3A:
        return kTorchHalf;
    default:
        return 0;
    }
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

CMapObjectManager* CMapObjectManager::Construct() {
    AsResourceManager(this)->Construct();
    ei::SetVtable(this, kManagerVtable);
    return this;
}
EI_REPLACE_METHOD(0x005125E0, CMapObjectManager::Construct);

void CMapObjectManager::Destruct() {
    ei::SetVtable(this, kManagerVtable);
    AsResourceManager(this)->Destruct();
}
EI_REPLACE_METHOD(0x00512650, CMapObjectManager::Destruct);

CMapObjectManager* CMapObjectManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00512630, CMapObjectManager::DeletingDestruct);

// 00512600 / 00512610 / 00512620 (slots 25, 11, 12).
const char* __fastcall MapObjectManagerFolder(CMapObjectManager*) { return game::Text(kFolderName); }
EI_REPLACE(0x00512600, MapObjectManagerFolder);
const char* __fastcall MapObjectManagerClassName(CMapObjectManager*) { return game::Text(kClassName); }
EI_REPLACE(0x00512610, MapObjectManagerClassName);
int __fastcall MapObjectManagerClassId(CMapObjectManager*) { return kManagerClassId; }
EI_REPLACE(0x00512620, MapObjectManagerClassId);

int CMapObjectManager::CreateManager() {
    CResourceManager* manager = AsResourceManager(this);
    mfc::CString folder(*static_cast<const mfc::CString*>(worlddata::TextFolderString()));
    if (!manager->CBaseManager::Create()) return 0;
    manager->AddArchive(folder + "database.res");   // the exe's 00784818
    return 1;
}
EI_REPLACE_METHOD(0x00512660, CMapObjectManager::CreateManager);

// --- Objects ----------------------------------------------------------------------------------------

CEngineObject* CMapObjectManager::CreateMapObject(int classId, int id) {
    CResourceManager* manager = AsResourceManager(this);
    CEngineObject* object = manager->CBaseManager::CreateObjectImpl(classId, id);
    if (!object) return nullptr;
    auto* figures = static_cast<CBaseManager*>(static_cast<CWorld*>(manager->context_)->FigureManager());
    static_cast<CMapObject*>(object)->SetAnimationState(
        static_cast<CFigureAnimationState*>(figures->CreateObject(kAnimationStateClass, kNoId)));
    CMapObjectVirtuals* slots = ObjectSlots(object);
    slots->AttachObject(static_cast<CWorld*>(manager->context_)->SpecificManager()->CreateObject(kObject3DHalf, kNoId));
    slots->AttachObject(static_cast<CWorld*>(manager->context_)->SpecificManager()->CreateObject(kMapObjectHalf, kNoId));
    if (int half = ThirdHalfOf(classId))
        slots->AttachObject(static_cast<CWorld*>(manager->context_)->SpecificManager()->CreateObject(half, kNoId));
    return object;
}
EI_REPLACE_METHOD(0x00512A30, CMapObjectManager::CreateMapObject);

FigureAnimation* CMapObjectManager::FindAnimation(const mfc::CString* figure, std::uint32_t key) {
    auto* database = static_cast<AnimationDatabase*>(AsResourceManager(this)->GetFile(*figure + ".adb", nullptr));
    if (!database) {
        game::DebugLog(game::Text(kNoDatabaseText), static_cast<const char*>(*figure));
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
EI_REPLACE_METHOD(0x00512B20, CMapObjectManager::FindAnimation);

int CMapObjectManager::GiveDefaults(CEngineObject* object) {
    auto* builder = reinterpret_cast<UnitStatsBuilder*>(this);
    // The vitals and statistics start zeroed, so the fields Reset and SetDefaults leave (the slots'
    // shares and factors, fixedMaxHitPoints, builtFlags, the layers' spare values, ...) are 0, not
    // what the stack held: a determinism fix (SV7b3). The original's were CWorldServer::CreateObject's
    // earlier callees' leftovers, heap addresses among them, and gave the main menu's unmoco2 (the
    // one unit added without a prototype) hit points that followed the heap; the harness zeroes them
    // in the original too (tests/verify/harness_leftovers.h).
    UnitVitals vitals{};
    vitals.Reset();
    if (object->GetClassId() != kClassUnit) {
        builder->SetDefaults(&vitals, nullptr);
        ObjectSlots(object)->SetServerData(&vitals);
    } else {
        UnitStats stats{};
        stats.Reset();
        builder->SetDefaults(&vitals, &stats);
        ObjectSlots(object)->SetStats(&stats);
    }
    ObjectSlots(object)->ScaleEffectChanged(0);
    object->prototypeId_ = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00513630, CMapObjectManager::GiveDefaults);

int CMapObjectManager::PrototypeEquipment(const char* name, mfc::CStringArray* armour, mfc::CString* weapon,
                                          mfc::CString* secondWeapon, mfc::CStringArray* spells) {
    auto* prototype = reinterpret_cast<UnitPrototypeRecord*>(unitdb::PrototypeTable().FindByName(name));
    if (!prototype) return game::FatalError(game::Text(kNoPrototypeText), name);
    armour->SetSize(static_cast<int>(prototype->armourNames.size()), -1);
    for (int i = 0; i < armour->size; ++i) armour->data[i] = prototype->armourNames.begin()[i];
    spells->SetSize(static_cast<int>(prototype->spellNames.size()), -1);
    for (int i = 0; i < spells->size; ++i) spells->data[i] = prototype->spellNames.begin()[i];
    *weapon = prototype->weaponName;
    *secondWeapon = prototype->secondWeaponName;
    return 1;
}
EI_REPLACE_METHOD(0x005143E0, CMapObjectManager::PrototypeEquipment);
