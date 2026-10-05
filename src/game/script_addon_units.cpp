// AddNewScriptCommands: addon's functions about units, objects and players (handlers 10010E4F-
// 10010F81, 10011308-100115AB, 10011AD3, 10011D8A, 10011F44-100124B8, 100126E0-100127CD,
// 10012C7C-10012CC6). A unit's data object is its +0x240 (addon's 10010E2D).
#include "script_addon_handlers.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/mfc.h"
#include "random.h"
#include "originals_ai.h"
#include "originals_units.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace scriptaddon {

namespace {

// The world (007AFF60): the player with a number (00594180).
__declspec(naked) void* __fastcall WorldGetPlayer(void*, void*, int) EI_ORIGINAL(0x00594180)
void* GetPlayer(int number) { return WorldGetPlayer(reinterpret_cast<void*>(0x007AFF60), nullptr, number); }

// The sector map (007B01F4): every object within `radius` of `point` (005C0BD0), into a pointer
// list (vtable 0073D454, block size 10; destructor 00427830).
__declspec(naked) void __fastcall PtrListDestruct(mfc::CTypedPtrList<CEngineObject>*) EI_ORIGINAL(0x00427830)
void* const kSectorMap = reinterpret_cast<void*>(0x007B01F4);

// A unit's prototype record (00530930): +0 its name, +4 its race (char pointers).
__declspec(naked) char* __fastcall UnitPrototype(void*) EI_ORIGINAL(0x00530930)
// The unit's parameters (+0x330): the 7-value block 005270E0 fills from the unit's +0x17C (the
// last argument: the id is a text key, >= 1000000000).

IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

float* FloatResult(Call* call) { return static_cast<float*>(call->result); }
ScriptObjectRef* ObjectArg(Call* call, int i) { return call->cmd->objectArgs.data[i]; }

// Virtual slot 55 (+0xDC) of a unit: 0 alive, 1 dead (addon's 10013192).
int DeathState(CEngineObject* unit) { return ei::VCall<int>(unit, 0xDC / 4); }

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
    mfc::CTypedPtrList<CEngineObject> list{};
    list.vtable = reinterpret_cast<void*>(0x0073D454);
    list.blockSize = 10;
    float point[3] = {x, y, 0.0f};
    SectorMapFindUnitsInRadius(kSectorMap, nullptr, point, &list, radius);
    for (auto* node = list.head; node; node = node->next) visit(node->data);
    PtrListDestruct(&list);
}

// A float field of the unit's data object, 0 and invalid arguments without one.
bool DataField(Call* call, std::size_t offset, const char* function) {
    void* data = UnitData(ObjectArg(call, 0));
    *FloatResult(call) = 0.0f;
    if (!data) return InvalidArguments(function);
    Store(FloatResult(call), Field<float>(data, offset));
    return true;
}

// The world's object map (007C2CD0, an STLport hash table: buckets at +4..+8, nodes {next, key,
// object}) in bucket order (addon's iterator 1001B02C / 1001B058).
struct ObjectNode {
    ObjectNode* next;
    int key;
    CEngineObject* object;
};
template <typename Visit>
void ForWorldObjects(Visit visit) {
    char* table = reinterpret_cast<char*>(0x007C2CD0);
    ObjectNode** bucket = Field<ObjectNode**>(table, 4);
    ObjectNode** end = Field<ObjectNode**>(table, 8);
    for (; bucket < end; ++bucket)
        for (ObjectNode* node = *bucket; node; node = node->next) visit(node->object);
}

} // namespace

// The unit's party's player number (party +0x24); -1 without.
bool GetPlayerOfUnit(Call* call) {
    Store(FloatResult(call), -1.0f);
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (!unit) return Report("GetPlayerOfUnit", "Unit doesn't exists");
    if (!IsUnit(unit)) return Report("GetPlayerOfUnit", "Object with ID %d must be a Unit", unit->id_);
    void* party = Field<void*>(unit, 0x24C);
    if (!party) return Report("GetPlayerOfUnit", "Unit doesn't have associated player: ID = %d", unit->id_);
    Store(FloatResult(call), static_cast<float>(Field<int>(party, 0x24)));
    return true;
}

// GetUnitExp(unit, [what]): "max" (default), "spent", "penalty" or "real" (max + penalty).
bool GetUnitExp(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    const char* what = call->cmd->stringArgs.size == 0 ? "max" : static_cast<const char*>(call->cmd->stringArgs.data[0]);
    *FloatResult(call) = 0.0f;
    if (!data) return InvalidArguments("GetUnitExp");
    float value;
    if (Stricmp(what, "max") == 0) {
        value = Field<float>(data, 0x1E4);
    } else if (Stricmp(what, "spent") == 0) {
        value = Field<float>(data, 0x1E8);
    } else if (Stricmp(what, "penalty") == 0) {
        value = Field<float>(data, 0x8E0);
    } else if (Stricmp(what, "real") == 0) {
        volatile float sum = Field<float>(data, 0x8E0) + Field<float>(data, 0x1E4);
        value = sum;
    } else {
        return InvalidArguments("GetUnitExp");
    }
    Store(FloatResult(call), value);
    return true;
}

// GetArmor(unit, kind, part): part 0..6; kind 0..5 sums three values of the unit's data (+0x2FC,
// kind * 0x3D + part floats in, every 0x12 floats); a negative kind takes the part from the block
// the unit's parameters compute.
bool GetArmor(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    const float* f = call->cmd->floatArgs.data;
    int kind = Ftol(f[0]);
    int part = Ftol(f[1]);
    *FloatResult(call) = 0.0f;
    if (static_cast<unsigned>(part) > 6 || kind > 5 || !data) return InvalidArguments("GetArmor");
    if (kind >= 0) {
        volatile float sum = 0.0f;
        const float* value = &Field<float>(data, 0x2FC + (kind * 0x3D + part) * 4);
        for (int i = 0; i < 3; ++i, value += 0x12) sum = *value + sum;
        Store(FloatResult(call), sum);
    } else {
        char* unit = Field<char*>(data, 0x1C);
        float block[7];
        UnitParameterBlock(unit + 0x330, nullptr, unit + 0x17C, block, Field<int>(unit, 0xC) >= 1000000000 ? 1 : 0);
        Store(FloatResult(call), block[part]);
    }
    return true;
}

// NearestUnit(x, y, radius, filter): the nearest object of the sector map's ones within radius
// that passes the filter (strictly nearer each time).
bool NearestUnit(Call* call) {
    const float* f = call->cmd->floatArgs.data;
    float x, y, radius;
    Store(&x, f[0]);
    Store(&y, f[1]);
    Store(&radius, f[2]);
    int filter = TruncateX87(f[3]);
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    CEngineObject* best = nullptr;
    ForObjectsNear(x, y, radius, [&](CEngineObject* object) {
        if (!PassesFilter(object, units, others, living)) return;
        float distance = Distance(Field<float>(object, 0x1C), Field<float>(object, 0x20), x, y);
        if (Status(radius, distance) & 0x41) return;
        best = object;
        radius = distance;
    });
    (*static_cast<ScriptObjectRef**>(call->result))->Assign(best);
    return true;
}

// UnitsNearPoint(x, y, radius, filter): every object of the sector map's ones within radius that
// passes the filter.
bool UnitsNearPoint(Call* call) {
    const float* f = call->cmd->floatArgs.data;
    float x, y, radius;
    Store(&x, f[0]);
    Store(&y, f[1]);
    Store(&radius, f[2]);
    int filter = TruncateX87(f[3]);
    bool units = (filter & 1) != 0, others = ((filter >> 1) & 1) != 0, living = ((filter >> 2) & 1) != 0;
    (*static_cast<ScriptGroup**>(call->result))->SetSize(0, -1);
    ForObjectsNear(x, y, radius, [&](CEngineObject* object) {
        if (PassesFilter(object, units, others, living)) (*static_cast<ScriptGroup**>(call->result))->Insert(object);
    });
    return true;
}

// A member of the group, by the game generator's next value modulo its size (an empty group
// leaves the result empty).
bool RandObj(Call* call) {
    ScriptGroup* group = call->cmd->groupArgs.data[0];
    int size = group->size;
    if (size) {
        std::uint32_t index = GameRandom()->Next() % static_cast<std::uint32_t>(size);
        (*static_cast<ScriptObjectRef**>(call->result))->Assign(group->data[index].object);
    }
    return true;
}

bool PlayerExists(Call* call) {
    Store(FloatResult(call), GetPlayer(Ftol(call->cmd->floatArgs.data[0])) ? 1.0f : 0.0f);
    return true;
}

bool GetDexterity(Call* call) { return DataField(call, 0x1F8, "GetDexterity"); }
bool GetIntelligence(Call* call) { return DataField(call, 0x200, "GetIntelligence"); }
bool GetStrength(Call* call) { return DataField(call, 0x1F0, "GetStrength"); }
bool GetActions(Call* call) { return DataField(call, 0x210, "GetActions"); }
bool GetActionsBase(Call* call) { return DataField(call, 0x20C, "GetActionsBase"); }
bool GetEncumbrance(Call* call) { return DataField(call, 0x230, "GetEncumbrance"); }
bool GetMaxEncumbrance(Call* call) { return DataField(call, 0x234, "GetMaxEncumbrance"); }

// GetSkill(unit, name): a skill byte of the unit's data.
bool GetSkill(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    const char* name = call->cmd->stringArgs.data[0];
    *FloatResult(call) = 0.0f;
    if (!data) return InvalidArguments("GetSkill");
    static const struct {
        const char* name;
        std::size_t offset;
    } kSkills[] = {{"science", 0x8A8}, {"melee", 0x8AB}, {"archery", 0x8AC},
                   {"elemental", 0x8AE}, {"sense", 0x8AF}, {"astral", 0x8B0}};
    for (const auto& skill : kSkills)
        if (Stricmp(name, skill.name) == 0) {
            Store(FloatResult(call), static_cast<float>(static_cast<int>(Field<std::uint8_t>(data, skill.offset))));
            return true;
        }
    return InvalidArguments("GetSkill");
}

// GetPerk(unit, name): a perk byte of the unit's data (+0x8B3 + the perk's index).
bool GetPerk(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    const char* name = call->cmd->stringArgs.data[0];
    *FloatResult(call) = 0.0f;
    if (!data) return InvalidArguments("GetPerk");
    static const char* const kPerks[] = {"sword", "axe", "dagger", "spear", "hammer", "bow", "crossbow",
                                         "fire", "lightning", "acid", "illusion", "divination",
                                         "enchantments", "healing", "domination", "infra", "health", "mana",
                                         "vitality", "spirit", "quickness", "lift", "backstab", "strength",
                                         "dexterity", "intelligence"};
    for (int i = 0; i < static_cast<int>(sizeof(kPerks) / sizeof(kPerks[0])); ++i)
        if (Stricmp(name, kPerks[i]) == 0) {
            Store(FloatResult(call), static_cast<float>(static_cast<int>(Field<std::uint8_t>(data, 0x8B3 + i))));
            return true;
        }
    return InvalidArguments("GetPerk");
}

// The unit's prototype name / race (no check that the unit has a prototype, as in addon).
bool GetPrototype(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    if (!data) return InvalidArguments("GetPrototype");
    char* prototype = UnitPrototype(Field<void*>(data, 0x1C));
    *static_cast<mfc::CString*>(call->result) = Field<const char*>(prototype, 0);
    return true;
}

bool GetRace(Call* call) {
    void* data = UnitData(ObjectArg(call, 0));
    if (!data) return InvalidArguments("GetRace");
    char* prototype = UnitPrototype(Field<void*>(data, 0x1C));
    *static_cast<mfc::CString*>(call->result) = Field<const char*>(prototype, 4);
    return true;
}

// The object's class id, -1 without an object.
bool GetObjectTypeID(Call* call) {
    Store(FloatResult(call), -1.0f);
    CEngineObject* object = ObjectArg(call, 0)->object;
    if (object) Store(FloatResult(call), static_cast<float>(object->GetClassId()));
    return true;
}

// GetObjectsOnMap(-1): every object of the world's object map; GetObjectsOnMap(class, ...): those
// whose class id is one of the (truncated) arguments.
bool GetObjectsOnMap(Call* call) {
    ScriptGroup* group = *static_cast<ScriptGroup**>(call->result);
    group->SetSize(0, -1);
    const MfcArray<float>& classes = call->cmd->floatArgs;
    bool all = classes.size == 1 && (Status(-1.0f, classes.data[0]) & 0x45) == 0x40;
    ForWorldObjects([&](CEngineObject* object) {
        if (!object) return;
        if (all) {
            (*static_cast<ScriptGroup**>(call->result))->Insert(object);
            return;
        }
        int id = object->GetClassId();
        for (int i = 0; i < call->cmd->floatArgs.size; ++i)
            if (id == Ftol(call->cmd->floatArgs.data[i])) {
                (*static_cast<ScriptGroup**>(call->result))->Insert(object);
                return;
            }
    });
    return true;
}

// A unit with a text-key id in a party.
bool IsPlayer(Call* call) {
    *FloatResult(call) = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (unit && IsUnit(unit) && unit->id_ >= 1000000000 && Field<void*>(unit, 0x24C)) Store(FloatResult(call), 1.0f);
    return true;
}

// IsDead / IsAlive (replace the game's): 0 for a missing object or a non-unit, else the unit's
// death state is 1 / 0.
bool IsDead(Call* call) {
    *FloatResult(call) = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (unit && IsUnit(unit)) Store(FloatResult(call), DeathState(unit) == 1 ? 1.0f : 0.0f);
    return true;
}

bool IsAlive(Call* call) {
    *FloatResult(call) = 0.0f;
    CEngineObject* unit = ObjectArg(call, 0)->object;
    if (unit && IsUnit(unit)) Store(FloatResult(call), DeathState(unit) == 0 ? 1.0f : 0.0f);
    return true;
}

} // namespace scriptaddon
