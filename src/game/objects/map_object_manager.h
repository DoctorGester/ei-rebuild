#pragma once

// The map object manager and the types its factory makes. Its members are all in map_object_manager.cpp. See
// docs/notes/units.md ("Creation, databases, save").

#include "game/core/manager_context.h"
#include "game/data/resource_manager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

struct AnimationDatabase;
struct FigureAnimation;
class Unit;
class MapObject;
struct UnitStats;
struct UnitVitals;

// The types the map object manager's factory (NewObject) makes, as EngineObject::type holds them (saved and sent
// as the number). Units are 0x32..0x34 (Hero: the player's own units, "mo%d" skins; 0x33 is what a prototype
// without a model gets), 0x39 is the retired fake unit (none made). The animation state is the figure manager's.
enum class MapObjectType : int {
    Hero = 0x32,
    Creature = 0x33,
    UnitSpare = 0x34,
    Tree = 0x35,
    Building = 0x36,
    Stone = 0x37,
    StuffObject = 0x38,
    Torch = 0x3A,
    MagicTrap = 0x3B,
    Lever = 0x3C,
    AnimationState = 0x300D,
    Trap = 0x3010,
};

// The type of a unit (Hero, Creature or UnitSpare).
constexpr bool IsUnitType(MapObjectType type) {
    return type >= MapObjectType::Hero && type <= MapObjectType::UnitSpare;
}

// A counted engine object's id saved (-1: none) or read back: -1 resets `ref`, any other id is looked up in
// `world`'s map object manager.
template <typename T>
void LoadRef(Ref<T>& ref, ByteStream* stream, World* world);

// The map object manager ("map objects", class id 0x2D).
struct MapObjectManager : ResourceManager {
    MapObjectManager();
    ~MapObjectManager() override;
    void Create() override;
    // ResourceManager's, then the animation databases dropped.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // A new object of the factory's type `type` (a MapObjectType), constructed; null for other ids.
    EngineObject* NewObject(int type) override;
    // The base manager's CreateObject; a new object gets an animation state (the figure manager's) and its side
    // halves from the world's specific manager: Object3D and MapObject, then Unit for the three unit types and
    // Torch for the torch. Returns the object or null.
    EngineObject* CreateObject(int type, int id) override;

    // The starting equipment of prototype `name`: see docs/notes/units.md, "Creation helpers".
    int GiveStartingEquipment(Unit* unit, const std::string& name);

    // The object registered under `id` (null for none); the manager holds only map objects.
    MapObject* FindMapObject(unsigned id) const;
    // The entry of the animation database "<figure>.adb" matching `key`: the same bits above kAnimationVariantBits
    // and, of the low bits, none of the key's missing (a key without low bits takes only an entry without). Null
    // (with a debug message when there is no database).
    FigureAnimation* FindAnimation(const std::string& figure, std::uint32_t key);
    // The animation `name` (case-insensitive) in the database "<figure>.adb", or null; a missing database is
    // reported to the debug log.
    FigureAnimation* FindFigureAnimation(const std::string& figure, std::string_view name);
    // The animation database "<figure>.adb", read from the archive on first use and kept until Destroy; null when
    // there is no such file.
    AnimationDatabase* AnimationDatabaseOf(const std::string& figure);
    // The defaults of an object without a prototype (SetDefaults): a unit (class 0x50) gets the statistics
    // (SetStats), any other object the vitals (SetServerData); then ScaleEffectChanged(false) and no prototype
    // index.
    void GiveDefaults(MapObject* object);

    // The builders of a creature's starting statistics from its prototype; the map objects call them on their
    // owner.
    // Defaults for an object without a prototype: with `stats`, the statistics (stats
    // 50, rates, values and body slots at 100, no layers); without, the vitals (1000 hit points
    // and mana, regeneration 10, protection 100).
    void SetDefaults(UnitVitals* vitals, UnitStats* stats);
    // The vitals and statistics of a creature of prototype `name` (the NPC record of
    // the same name supplies stats, attributes, experience and skills when there is one).
    // Without a prototype: SetDefaults. Returns 0 when `vitals` or `stats` is null (after the
    // lookups), else 1.
    int BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, std::string_view name);
    // Gives map object `object` the prototype `name`: units get their vitals and statistics
    // (BuildFromPrototype, SetServerData and SetStats), their height offset, scale (the base scale and
    // ApplyBaseScale) and second prototype; every object its prototype index. Without a prototype: defaults,
    // ScaleEffectChanged(false), index 0 and a debug message.
    void ApplyPrototype(MapObject* object, const std::string& name);

    // The animation databases read, by file name.
    std::unordered_map<std::string, std::unique_ptr<AnimationDatabase>> animationDatabases;
};

// The map object manager of `object`'s world: the one that made it (a world has one).
inline MapObjectManager* MapObjectManagerOf(const EngineObject* object) {
    return object->GetManagerContext()->GetMapObjectManager();
}