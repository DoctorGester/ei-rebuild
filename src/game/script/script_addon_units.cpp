// Rebirth's script functions about units, objects and players. A unit's data is its server-side controller.
#include "game/combat/combat.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "engine/core/random.h"
#include "game/players/player.h"
#include "game/script/script_addon_handlers.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/rounding.h"

#include <cstdint>
#include <type_traits>

namespace scriptaddon {

namespace {

// The filter of NearestUnit and UnitsNearPoint: units pass with flag 1,
// other objects with flag 2, and with flag 4 any living object with a text-key id.
bool PassesFilter(MapObject* object, bool units, bool others, bool living) {
    bool unit = IsUnit(object);
    bool alive = object->id >= 1000000000 && object->IsDead() == 0;
    if (unit ? units : others) return true;
    return alive && living;
}

// The objects within `radius` of (x, y) (z 0), in the sector map's order; the list is destroyed
// afterwards.
template <typename Visit>
void ForObjectsNear(float x, float y, float radius, Visit visit) {
    Vector3 point = {x, y, 0.0f};
    for (Unit* unit : g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(point, radius)) visit(unit);
}

// A float field of the unit's data object, 0 and invalid arguments without one.
bool DataField(Call* call, float UnitStats::* field, const char* function) {
    UnitServer* data = ScriptArgs(call->cmd).Object().ServerController();
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments(function);
    *call->result.number = data->stats.*field;
    return true;
}

// The server world's object map in its iteration order (the order GetObjectsOnMap returns).
template <typename Visit>
void ForWorldObjects(Visit visit) {
    auto& objects = g_serverWorld.objects.map;
    for (const auto& [id, object] : objects) visit(object);
}

} // namespace

// The unit's party's player number; -1 without.
bool GetPlayerOfUnit(Call* call) {
    *call->result.number = -1.0f;
    MapObject* unit = ScriptArgs(call->cmd).Object().object;
    if (!unit) return Report("GetPlayerOfUnit", "Unit doesn't exists");
    Unit* asUnit = UnitOrNull(unit);
    if (!asUnit) return Report("GetPlayerOfUnit", "Object with ID %d must be a Unit", unit->id);
    Player* party = asUnit->player;
    if (!party) return Report("GetPlayerOfUnit", "Unit doesn't have associated player: ID = %d", unit->id);
    *call->result.number = static_cast<float>(party->playerId);
    return true;
}

// GetUnitExp(unit, [what]): "max" (default), "spent", "penalty" or "real" (max + penalty).
bool GetUnitExp(Call* call) {
    ScriptArgs args(call->cmd);
    UnitServer* data = args.Object().ServerController();
    const std::string_view what = args.TextCount() == 0 ? std::string_view("max") : std::string_view(args.Text());
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments("GetUnitExp");
    float value;
    if (Stricmp(what, "max") == 0) {
        value = data->stats.experience;
    } else if (Stricmp(what, "spent") == 0) {
        value = data->stats.spentExperience;
    } else if (Stricmp(what, "penalty") == 0) {
        value = data->stats.pendingExperience;
    } else if (Stricmp(what, "real") == 0) {
        float sum = data->stats.pendingExperience + data->stats.experience;
        value = sum;
    } else {
        return InvalidArguments("GetUnitExp");
    }
    *call->result.number = value;
    return true;
}

// GetArmor(unit, kind, part): part 0..6; kind 0..5 sums the slot's three layers' absorb values for the part; a negative kind takes the part from the block
// the unit's parameters compute.
bool GetArmor(Call* call) {
    ScriptArgs args(call->cmd);
    UnitServer* data = args.Object().ServerController();
    int kind = TruncateOrMin(args.Float());
    int part = TruncateOrMin(args.Float());
    *call->result.number = 0.0f;
    if (static_cast<unsigned>(part) > 6 || kind > 5 || !data) return InvalidArguments("GetArmor");
    if (kind >= 0) {
        float sum = 0.0f;
        const UnitStatSlot& slot = data->stats.slots[kind];
        for (int i = 0; i < 3; ++i) sum = slot.layers[i].absorb[part] + sum;
        *call->result.number = sum;
    } else {
        Unit* unit = data->unit;
        auto block = unit->params.ProtectionBlock(&unit->packedVitals, static_cast<unsigned char>(unit->id >= 1000000000 ? 1 : 0));
        *call->result.number = block[part];
    }
    return true;
}

// NearestUnit(x, y, radius, filter): the nearest object of the sector map's ones within radius
// that passes the filter (strictly nearer each time).
bool NearestUnit(Call* call) {
    ScriptArgs args(call->cmd);
    float x = args.Float();
    float y = args.Float();
    float radius = args.Float();
    int filter = lib::Truncate(args.Float());
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    MapObject* best = nullptr;
    ForObjectsNear(x, y, radius, [&](MapObject* object) {
        if (!PassesFilter(object, units, others, living)) return;
        const Vector3& at = object->localPosition;
        float distance = Length2D(Vector2{x, y} - XY(at));
        if (distance < radius) {   // ordered: a NaN distance never wins
            best = object;
            radius = distance;
        }
    });
    call->ObjectResult()->object = best;
    return true;
}

// UnitsNearPoint(x, y, radius, filter): every object of the sector map's ones within radius that
// passes the filter.
bool UnitsNearPoint(Call* call) {
    ScriptArgs args(call->cmd);
    float x = args.Float();
    float y = args.Float();
    float radius = args.Float();
    int filter = lib::Truncate(args.Float());
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    call->GroupResult()->Clear();
    ForObjectsNear(x, y, radius, [&](MapObject* object) {
        if (PassesFilter(object, units, others, living)) call->GroupResult()->Insert(object);
    });
    return true;
}

// A member of the group, by the game generator's next value modulo its size (an empty group
// leaves the result empty).
bool RandObj(Call* call) {
    ScriptGroup* group = ScriptArgs(call->cmd).Group();
    if (!group->empty()) {
        std::uint32_t index = g_random.Next() % static_cast<std::uint32_t>(group->size());
        *call->ObjectResult() = (*group)[index];
    }
    return true;
}

bool PlayerExists(Call* call) {
    *call->result.number = ScriptBool(g_serverWorld.GetPlayer(TruncateOrMin(ScriptArgs(call->cmd).Float())) != nullptr);
    return true;
}

bool GetDexterity(Call* call) { return DataField(call, &UnitStats::dexterity, "GetDexterity"); }
bool GetIntelligence(Call* call) { return DataField(call, &UnitStats::intelligence, "GetIntelligence"); }
bool GetStrength(Call* call) { return DataField(call, &UnitStats::strength, "GetStrength"); }
bool GetActions(Call* call) { return DataField(call, &UnitStats::rate, "GetActions"); }
bool GetActionsBase(Call* call) { return DataField(call, &UnitStats::rateBase, "GetActionsBase"); }
bool GetEncumbrance(Call* call) { return DataField(call, &UnitStats::load, "GetEncumbrance"); }
bool GetMaxEncumbrance(Call* call) { return DataField(call, &UnitStats::capacity, "GetMaxEncumbrance"); }

// GetSkill(unit, name): a skill byte of the unit's data.
bool GetSkill(Call* call) {
    ScriptArgs args(call->cmd);
    UnitServer* data = args.Object().ServerController();
    const std::string& name = args.Text();
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments("GetSkill");
    static const struct {
        const char* name = nullptr;
        bool attribute = false;  // UnitStats::attributes (else skills)
        int index = 0;
    } kSkills[] = {{"science", true, 0}, {"melee", false, 0}, {"archery", false, 1},
                   {"elemental", false, 3}, {"sense", false, 4}, {"astral", false, 5}};
    for (const auto& skill : kSkills)
        if (Stricmp(name, skill.name) == 0) {
            *call->result.number = static_cast<float>(static_cast<int>(skill.attribute ? data->stats.attributes[skill.index] : data->stats.skills[skill.index]));
            return true;
        }
    return InvalidArguments("GetSkill");
}

// GetPerk(unit, name): a perk byte of the unit's data.
bool GetPerk(Call* call) {
    ScriptArgs args(call->cmd);
    UnitServer* data = args.Object().ServerController();
    const std::string& name = args.Text();
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments("GetPerk");
    static const char* const kPerks[] = {"sword", "axe", "dagger", "spear", "hammer", "bow", "crossbow",
                                         "fire", "lightning", "acid", "illusion", "divination",
                                         "enchantments", "healing", "domination", "infra", "health", "mana",
                                         "vitality", "spirit", "quickness", "lift", "backstab", "strength",
                                         "dexterity", "intelligence"};
    for (int i = 0; i < static_cast<int>(sizeof(kPerks) / sizeof(kPerks[0])); ++i)
        if (Stricmp(name, kPerks[i]) == 0) {
            *call->result.number = static_cast<float>(static_cast<int>(data->stats.skillLevels[i]));
            return true;
        }
    return InvalidArguments("GetPerk");
}

// The unit's prototype name / race (no check that the unit has a prototype, as Rebirth's).
bool GetPrototype(Call* call) {
    UnitServer* data = ScriptArgs(call->cmd).Object().ServerController();
    if (!data) return InvalidArguments("GetPrototype");
    const UnitPrototypeRecord* prototype = data->unit->GetPrototype();
    *call->result.text = prototype->name;
    return true;
}

bool GetRace(Call* call) {
    UnitServer* data = ScriptArgs(call->cmd).Object().ServerController();
    if (!data) return InvalidArguments("GetRace");
    const UnitPrototypeRecord* prototype = data->unit->GetPrototype();
    *call->result.text = prototype->modelName;
    return true;
}

// The object's class id, -1 without an object.
bool GetObjectTypeID(Call* call) {
    *call->result.number = -1.0f;
    MapObject* object = ScriptArgs(call->cmd).Object().object;
    if (object) *call->result.number = static_cast<float>(object->GetClassId());
    return true;
}

// GetObjectsOnMap(-1): every object of the world's object map; GetObjectsOnMap(class, ...): those
// whose class id is one of the (truncated) arguments.
bool GetObjectsOnMap(Call* call) {
    ScriptGroup* group = call->GroupResult();
    group->Clear();
    const std::vector<float>& classes = call->cmd->floatArgs;
    bool all = classes.size() == 1 && classes[0] == -1.0f;
    ForWorldObjects([&](MapObject* object) {
        if (!object) return;
        if (all) {
            call->GroupResult()->Insert(object);
            return;
        }
        int id = static_cast<int>(object->GetClassId());
        for (float wanted : classes)
            if (id == TruncateOrMin(wanted)) {
                call->GroupResult()->Insert(object);
                return;
            }
    });
    return true;
}

// A unit with a text-key id in a party.
bool IsPlayer(Call* call) {
    *call->result.number = 0.0f;
    MapObject* unit = ScriptArgs(call->cmd).Object().object;
    Unit* asUnit = UnitOrNull(unit);
    if (asUnit && asUnit->id >= 1000000000 && asUnit->player) *call->result.number = 1.0f;
    return true;
}

// IsDead / IsAlive (replace the game's): 0 for a missing object or a non-unit, else the unit's
// death state is 1 / 0.
bool IsDead(Call* call) {
    *call->result.number = 0.0f;
    MapObject* unit = ScriptArgs(call->cmd).Object().object;
    if (unit && IsUnit(unit)) *call->result.number = ScriptBool(unit->IsDead() == 1);
    return true;
}

bool IsAlive(Call* call) {
    *call->result.number = 0.0f;
    MapObject* unit = ScriptArgs(call->cmd).Object().object;
    if (unit && IsUnit(unit)) *call->result.number = ScriptBool(unit->IsDead() == 0);
    return true;
}

} // namespace scriptaddon
