// AddNewScriptCommands: addon's functions about units, objects and players (handlers 10010E4F-
// 10010F81, 10011308-100115AB, 10011AD3, 10011D8A, 10011F44-100124B8, 100126E0-100127CD,
// 10012C7C-10012CC6). A unit's data object is its +0x240 (addon's 10010E2D).
#include "script_addon_handlers.h"

#include "combat.h"
#include "map_object.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "random.h"
#include "game_singletons.h"

#include <cstdint>
#include <list>
#include <type_traits>

namespace scriptaddon {

namespace {

// The world: the player with a number.
void* GetPlayer(int number) { return g_serverWorld->GetPlayer(number); }

// A unit's prototype record: its name and, at +4, its model name (the race).
static const UnitPrototypeRecord* UnitPrototype(CUnit* unit) { return unit->GetPrototype(); }
// The unit's parameters: the 7-value block filled from the unit's +0x17C (the
// last argument: the id is a text key, >= 1000000000).

ScriptObjectRef* ObjectArg(Call* call, int i) { return call->cmd->objectArgs[i]; }

// CMapObject::IsDead of any object: 0 alive, 1 dead (addon's handler).
int DeathState(CEngineObject* unit) { return ObjectSlots(unit)->IsDead(); }

// The filter of NearestUnit and UnitsNearPoint (10011421, 10013076): units pass with flag 1,
// other objects with flag 2, and with flag 4 any living object with a text-key id.
bool PassesFilter(CEngineObject* object, bool units, bool others, bool living) {
    bool unit = IsUnit(object);
    bool alive = object->id_ >= 1000000000 && DeathState(object) == 0;
    if (unit ? units : others) return true;
    return alive && living;
}

// The objects within `radius` of (x, y) (z 0), in the sector map's order; the list is destroyed
// afterwards.
template <typename Visit>
void ForObjectsNear(float x, float y, float radius, Visit visit) {
    std::list<CUnit*> list{};
    Vector3 point = {x, y, 0.0f};
    g_serverWorld->aiMap_.sectorMap.FindUnitsInRadius(&point, &list, radius);
    for (auto node = list.begin(); node != list.end(); ++node) visit(*node);
}

// A float field of the unit's data object, 0 and invalid arguments without one.
bool DataField(Call* call, float UnitStats::* field, const char* function) {
    auto* data = UnitData(ObjectArg(call, 0));
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments(function);
    Store(call->result.number, data->stats.*field);
    return true;
}

// The server world's object map (an STLport hash table by object id) in bucket order (addon's iterator 1001B02C /
// 1001B058).
template <typename Visit>
void ForWorldObjects(Visit visit) {
    auto& objects = g_serverWorld->objects_.map;
    for (const auto& [id, object] : objects) visit(object);
}

} // namespace

// The unit's party's player number; -1 without.
bool GetPlayerOfUnit(Call* call) {
    Store(call->result.number, -1.0f);
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (!unit) return Report("GetPlayerOfUnit", "Unit doesn't exists");
    CUnit* asUnit = UnitOrNull(unit);
    if (!asUnit) return Report("GetPlayerOfUnit", "Object with ID %d must be a Unit", unit->id_);
    CPlayer* party = asUnit->player;
    if (!party) return Report("GetPlayerOfUnit", "Unit doesn't have associated player: ID = %d", unit->id_);
    Store(call->result.number, static_cast<float>(party->playerId_));
    return true;
}

// GetUnitExp(unit, [what]): "max" (default), "spent", "penalty" or "real" (max + penalty).
bool GetUnitExp(Call* call) {
    auto* data = UnitData(ObjectArg(call, 0));
    const char* what = std::ssize(call->cmd->stringArgs) == 0 ? "max" : call->cmd->stringArgs[0].c_str();
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
    Store(call->result.number, value);
    return true;
}

// GetArmor(unit, kind, part): part 0..6; kind 0..5 sums the slot's three layers' absorb values for the part; a negative kind takes the part from the block
// the unit's parameters compute.
bool GetArmor(Call* call) {
    auto* data = UnitData(ObjectArg(call, 0));
    const float* f = call->cmd->floatArgs.data();
    int kind = Ftol(f[0]);
    int part = Ftol(f[1]);
    *call->result.number = 0.0f;
    if (static_cast<unsigned>(part) > 6 || kind > 5 || !data) return InvalidArguments("GetArmor");
    if (kind >= 0) {
        float sum = 0.0f;
        const UnitStatSlot& slot = data->stats.slots[kind];
        for (int i = 0; i < 3; ++i) sum = slot.layers[i].absorb[part] + sum;
        Store(call->result.number, sum);
    } else {
        CUnit* unit = data->unit;
        float block[7];
        unit->params.ProtectionBlock(&unit->packedVitals, block, static_cast<unsigned char>(unit->id_ >= 1000000000 ? 1 : 0));
        Store(call->result.number, block[part]);
    }
    return true;
}

// NearestUnit(x, y, radius, filter): the nearest object of the sector map's ones within radius
// that passes the filter (strictly nearer each time).
bool NearestUnit(Call* call) {
    const float* f = call->cmd->floatArgs.data();
    float x, y, radius;
    Store(&x, f[0]);
    Store(&y, f[1]);
    Store(&radius, f[2]);
    int filter = TruncateX87(f[3]);
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    CEngineObject* best = nullptr;
    ForObjectsNear(x, y, radius, [&](CEngineObject* object) {
        if (!PassesFilter(object, units, others, living)) return;
        const Vector3& at = ObjectSlots(object)->localPosition_;
        float distance = Distance(at.x, at.y, x, y);
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
    const float* f = call->cmd->floatArgs.data();
    float x, y, radius;
    Store(&x, f[0]);
    Store(&y, f[1]);
    Store(&radius, f[2]);
    int filter = TruncateX87(f[3]);
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    call->GroupResult()->clear();
    ForObjectsNear(x, y, radius, [&](CEngineObject* object) {
        if (PassesFilter(object, units, others, living)) call->GroupResult()->Insert(object);
    });
    return true;
}

// A member of the group, by the game generator's next value modulo its size (an empty group
// leaves the result empty).
bool RandObj(Call* call) {
    ScriptGroup* group = call->cmd->groupArgs[0];
    if (!group->empty()) {
        std::uint32_t index = g_random.Next() % static_cast<std::uint32_t>(group->size());
        *call->ObjectResult() = (*group)[index];
    }
    return true;
}

bool PlayerExists(Call* call) {
    Store(call->result.number, GetPlayer(Ftol(call->cmd->floatArgs[0])) ? 1.0f : 0.0f);
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
    auto* data = UnitData(ObjectArg(call, 0));
    const char* name = call->cmd->stringArgs[0].c_str();
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments("GetSkill");
    static const struct {
        const char* name;
        bool attribute;   // UnitStats::attributes (else skills)
        int index;
    } kSkills[] = {{"science", true, 0}, {"melee", false, 0}, {"archery", false, 1},
                   {"elemental", false, 3}, {"sense", false, 4}, {"astral", false, 5}};
    for (const auto& skill : kSkills)
        if (Stricmp(name, skill.name) == 0) {
            Store(call->result.number, static_cast<float>(static_cast<int>(skill.attribute ? data->stats.attributes[skill.index] : data->stats.skills[skill.index])));
            return true;
        }
    return InvalidArguments("GetSkill");
}

// GetPerk(unit, name): a perk byte of the unit's data (+0x8B3 + the perk's index).
bool GetPerk(Call* call) {
    auto* data = UnitData(ObjectArg(call, 0));
    const char* name = call->cmd->stringArgs[0].c_str();
    *call->result.number = 0.0f;
    if (!data) return InvalidArguments("GetPerk");
    static const char* const kPerks[] = {"sword", "axe", "dagger", "spear", "hammer", "bow", "crossbow",
                                         "fire", "lightning", "acid", "illusion", "divination",
                                         "enchantments", "healing", "domination", "infra", "health", "mana",
                                         "vitality", "spirit", "quickness", "lift", "backstab", "strength",
                                         "dexterity", "intelligence"};
    for (int i = 0; i < static_cast<int>(sizeof(kPerks) / sizeof(kPerks[0])); ++i)
        if (Stricmp(name, kPerks[i]) == 0) {
            Store(call->result.number, static_cast<float>(static_cast<int>(data->stats.skillLevels[i])));
            return true;
        }
    return InvalidArguments("GetPerk");
}

// The unit's prototype name / race (no check that the unit has a prototype, as in addon).
bool GetPrototype(Call* call) {
    auto* data = UnitData(ObjectArg(call, 0));
    if (!data) return InvalidArguments("GetPrototype");
    const UnitPrototypeRecord* prototype = UnitPrototype(data->unit);
    *call->result.text = prototype->name.c_str();
    return true;
}

bool GetRace(Call* call) {
    auto* data = UnitData(ObjectArg(call, 0));
    if (!data) return InvalidArguments("GetRace");
    const UnitPrototypeRecord* prototype = UnitPrototype(data->unit);
    *call->result.text = prototype->modelName.c_str();
    return true;
}

// The object's class id, -1 without an object.
bool GetObjectTypeID(Call* call) {
    Store(call->result.number, -1.0f);
    CEngineObject* object = ObjectArg(call, 0)->object;
    if (object) Store(call->result.number, static_cast<float>(object->GetClassId()));
    return true;
}

// GetObjectsOnMap(-1): every object of the world's object map; GetObjectsOnMap(class, ...): those
// whose class id is one of the (truncated) arguments.
bool GetObjectsOnMap(Call* call) {
    ScriptGroup* group = call->GroupResult();
    group->clear();
    const std::vector<float>& classes = call->cmd->floatArgs;
    bool all = classes.size() == 1 && classes[0] == -1.0f;
    ForWorldObjects([&](CEngineObject* object) {
        if (!object) return;
        if (all) {
            call->GroupResult()->Insert(object);
            return;
        }
        int id = object->GetClassId();
        for (int i = 0; i < std::ssize(call->cmd->floatArgs); ++i)
            if (id == Ftol(call->cmd->floatArgs[i])) {
                call->GroupResult()->Insert(object);
                return;
            }
    });
    return true;
}

// A unit with a text-key id in a party.
bool IsPlayer(Call* call) {
    *call->result.number = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    CUnit* asUnit = UnitOrNull(unit);
    if (asUnit && asUnit->id_ >= 1000000000 && asUnit->player) Store(call->result.number, 1.0f);
    return true;
}

// IsDead / IsAlive (replace the game's): 0 for a missing object or a non-unit, else the unit's
// death state is 1 / 0.
bool IsDead(Call* call) {
    *call->result.number = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (unit && IsUnit(unit)) Store(call->result.number, DeathState(unit) == 1 ? 1.0f : 0.0f);
    return true;
}

bool IsAlive(Call* call) {
    *call->result.number = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (unit && IsUnit(unit)) Store(call->result.number, DeathState(unit) == 0 ? 1.0f : 0.0f);
    return true;
}

} // namespace scriptaddon
