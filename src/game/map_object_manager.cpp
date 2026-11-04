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

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>

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

void DeleteAnimationDatabases(std::unordered_map<std::string, AnimationDatabase*>& databases) {
    for (auto& entry : databases) delete entry.second;
    databases.clear();
}

} // namespace

// --- Lifetime and class slots ---------------------------------------------------------------------

MapObjectManager::MapObjectManager() {}

MapObjectManager::~MapObjectManager() { DeleteAnimationDatabases(animationDatabases); }

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

void MapObjectManager::Destroy() {
    ResourceManager::Destroy();
    DeleteAnimationDatabases(animationDatabases);
}

// --- Animation databases --------------------------------------------------------------------------

namespace {

// The header of "<figure>.adb"; `count` FigureAnimationRecords follow.
struct AdbHeader {
    char magic[4];                  // "ADB"
    std::uint32_t count;
    char unreadName[0x18];          // the database's name ("Zombie00")
    float start;
    float length;
    float end;
};
static_assert(sizeof(AdbHeader) == 0x2C);

// `out` from the file's bytes at `offset`; what lies beyond the file's `size` reads as zero.
template <class T>
void LoadWithinFile(T& out, const std::uint8_t* data, std::uint32_t size, std::size_t offset) {
    std::uint8_t bytes[sizeof(T)] = {};
    if (offset < size) std::memcpy(bytes, data + offset, std::min<std::size_t>(sizeof(T), size - offset));
    lib::LoadBytes(out, bytes);
}

// The database of an .adb file. Four of the game's (unmozozo, unmoco3, unmoco4, inmm1) are of an older, shorter
// entry and end before `count` full entries: their entries are read at the current stride, the bytes past the file as
// zero (the original read past the file).
std::unique_ptr<AnimationDatabase> ReadAnimationDatabase(const std::uint8_t* data, std::uint32_t size) {
    AdbHeader header;
    LoadWithinFile(header, data, size, 0);
    auto database = std::make_unique<AnimationDatabase>();
    database->start = header.start;
    database->length = header.length;
    database->end = header.end;
    database->animations.reserve(header.count);
    for (std::size_t i = 0; i < header.count; ++i) {
        FigureAnimationRecord record;
        LoadWithinFile(record, data, size, sizeof(AdbHeader) + i * sizeof(FigureAnimationRecord));
        database->animations.push_back(FigureAnimation{record, database.get()});
    }
    return database;
}

} // namespace

AnimationDatabase* MapObjectManager::AnimationDatabaseOf(const std::string& figure) {
    std::string file = figure + ".adb";
    if (auto found = animationDatabases.find(file); found != animationDatabases.end()) return found->second;
    std::uint32_t size = 0;
    void* data = GetFile(file, &size);
    if (!data) return nullptr;
    auto database = ReadAnimationDatabase(static_cast<const std::uint8_t*>(data), size);
    CloseFile(data);
    return animationDatabases.emplace(std::move(file), database.release()).first->second;
}

FigureAnimation* MapObjectManager::FindAnimation(const std::string* figure, std::uint32_t key) {
    AnimationDatabase* database = AnimationDatabaseOf(*figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, *figure);
        return nullptr;
    }
    const std::uint32_t variant = key & kAnimationVariantBits;
    for (FigureAnimation& entry : database->animations) {
        const bool variantFits = variant == 0 ? (entry.key & kAnimationVariantBits) == 0
                                              : (~entry.key & key & kAnimationVariantBits) == 0;
        if (variantFits && ((entry.key ^ key) & kKeyKindBits) == 0) return &entry;
    }
    return nullptr;
}

FigureAnimation* MapObjectManager::FindFigureAnimation(const std::string& figure, const char* name) {
    AnimationDatabase* database = AnimationDatabaseOf(figure);
    if (!database) {
        game::DebugLog(kNoDatabaseText, figure);
        return nullptr;
    }
    for (FigureAnimation& entry : database->animations)
        if (text::CompareIgnoringCase(entry.name, name) == 0) return &entry;
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
