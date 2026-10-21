// The map object manager ("map objects", class id 0x2D; A-U12): its lifetime, the
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

#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)


namespace {

constexpr const char* kFolderName = "map objects";
constexpr const char* kClassName = "CMapObjectManager";
constexpr const char* kNoDatabaseText = "Can't find animation database file for \"%s\".\n";
constexpr const char* kNoPrototypeText = "Can't find prototype \"%s\".\n";
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

CMapObjectManager::CMapObjectManager() {}

CMapObjectManager* CMapObjectManager::Construct() { return new (this) CMapObjectManager(); }
EI_REWRITTEN(0x005125E0, CMapObjectManager::Construct);

CMapObjectManager::~CMapObjectManager() {}

void CMapObjectManager::Destruct() { this->CMapObjectManager::~CMapObjectManager(); }
EI_REWRITTEN(0x00512650, CMapObjectManager::Destruct);

CMapObjectManager* CMapObjectManager::DeletingDestruct(unsigned flags) {
    this->CMapObjectManager::~CMapObjectManager();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00512630, CMapObjectManager::DeletingDestruct);

const char* CMapObjectManager::GetFolderName() { return kFolderName; }
const char* CMapObjectManager::GetClassName() { return kClassName; }
int CMapObjectManager::GetClassId() { return kManagerClassId; }
int CMapObjectManager::Create() { return CreateManager(); }
CEngineObject* CMapObjectManager::NewObject(int classId) { return NewObjectImpl(classId); }
CEngineObject* CMapObjectManager::CreateObject(int classId, int id) { return CreateMapObject(classId, id); }

// 00512600 / 00512610 / 00512620 (slots 25, 11, 12).
const char* __fastcall MapObjectManagerFolder(CMapObjectManager* self) { return self->CMapObjectManager::GetFolderName(); }
EI_REPLACE(0x00512600, MapObjectManagerFolder);
const char* __fastcall MapObjectManagerClassName(CMapObjectManager* self) { return self->CMapObjectManager::GetClassName(); }
EI_REPLACE(0x00512610, MapObjectManagerClassName);
int __fastcall MapObjectManagerClassId(CMapObjectManager* self) { return self->CMapObjectManager::GetClassId(); }
EI_REPLACE(0x00512620, MapObjectManagerClassId);

int CMapObjectManager::CreateManager() {
    mfc::CString folder(*static_cast<const mfc::CString*>(worlddata::TextFolderString()));
    if (!CBaseManager::Create()) return 0;
    AddArchive(folder + "database.res");   // the exe's 00784818
    return 1;
}
EI_REWRITTEN(0x00512660, CMapObjectManager::CreateManager);

// --- Objects ----------------------------------------------------------------------------------------

CEngineObject* CMapObjectManager::CreateMapObject(int classId, int id) {
    CResourceManager* manager = this;
    CEngineObject* object = CBaseManager::CreateObjectImpl(classId, id);
    if (!object) return nullptr;
    auto* figures = static_cast<CWorld*>(manager->context_)->FigureManager();
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
    auto* database = static_cast<AnimationDatabase*>(GetFile(*figure + ".adb", nullptr));
    if (!database) {
        game::DebugLog(kNoDatabaseText, static_cast<const char*>(*figure));
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
EI_REWRITTEN(0x00512B20, CMapObjectManager::FindAnimation);

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
    if (!prototype) return game::FatalError(kNoPrototypeText, name);
    armour->SetSize(static_cast<int>(prototype->armourNames.size()), -1);
    for (int i = 0; i < armour->size; ++i) armour->data[i] = prototype->armourNames.begin()[i];
    spells->SetSize(static_cast<int>(prototype->spellNames.size()), -1);
    for (int i = 0; i < spells->size; ++i) spells->data[i] = prototype->spellNames.begin()[i];
    *weapon = prototype->weaponName;
    *secondWeapon = prototype->secondWeaponName;
    return 1;
}
EI_REWRITTEN(0x005143E0, CMapObjectManager::PrototypeEquipment);
