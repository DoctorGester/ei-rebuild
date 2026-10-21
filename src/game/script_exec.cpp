// Script command execution: ScriptRun::Execute (004B6760), the switch over every world script
// command. See script.h and docs/notes/script.md.
//
// Rebirth patches Execute in memory (it adds commands), so this replacement installs only in the
// vanilla game. Every callee is reached through its original address (forwarders below): several
// are patched by Rebirth (CalcParameters, FatalError, 005935C0, Execute itself for For bodies), and
// the rest still run original code or their own replacement's verify wrapper.
#include "ai_map.h"
#include "object_tables.h"
#include "script.h"
#include "game_arrays.h"
#include "game_strings.h"
#include "script_addon.h"

#include "combat.h"
#include "console.h"
#include "item.h"
#include "map_object.h"
#include "net_classes.h"
#include "object_diplomacy.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"
#include "quest_records.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "class_templates.h"
#include "lever.h"
#include "model.h"
#include "player_manager.h"
#include "world_effect.h"
#include "world_glue.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_render.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "game_lists.h"
#include "bink_video.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <bit>

namespace {

// ---- x87 helpers --------------------------------------------------------------------------------

// The game's _ftol (006E853C): truncating conversion, low 32 bits of the 64-bit result.
int Ftol(float value) { return static_cast<int>(gamecrt::Ftol(value)); }

// FCOMP against the constant 0.0 at 0073B858, the test every command uses for "flag argument is zero".
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

int Round(float value) { return x87::fistp(value); }

// ---- Original code reached by the commands ------------------------------------------------------
//
// The commands call the game's classes directly; what is left here are the container entry points
// (MFC lists and arrays of script records) the original reaches out of line.

constexpr int kClassLever = 0x5D;
constexpr int kClassQuestItem = 0x14;

// Game texts.
const char* const kKillScriptNotAllowed = "KillScript command not allowed here";   // 0078B904
const char* MoviesFolder() { return "Movies\\"; }
// 0073C36C: 1 / 255, the background colour scale.
float ColourScale() { return 0.003921569f /*0073C36C*/; }
// 0079BE5C: the configured world time speed (an int).
int ConfiguredTimeSpeed() { return ei::GameData<const int>(0x0079BE5C); }

// Script helpers (script.h types, original addresses).
__declspec(naked) void __fastcall CalcParametersOrig(SSCCmd*, void*, CScript*, ScriptVars*, const ScriptSubCall*)
    EI_ORIGINAL(0x004B4030)
// CList<ScriptDefinition> AddHead (CScript::instances_; despite the AddTail-like shape it links the
// new node before the head): returns the new node.
using InstanceList = mfc::CList<ScriptDefinition>;
// CList RemoveAt for the particle, FX source and lightning lists.
using ParticleList = mfc::CList<ScriptParticleSource>;
using FxSourceList = mfc::CList<ScriptFxSource>;
using LightningList = mfc::CList<ScriptLightning>;
// The party records of `player` for a "party name" string (ECX player, EDX string).
// Object id of a name (ECX = name).
// 005DB3F0: plays a movie file (ECX path, EDX 0, five flags).
// 00500960: the renderer (returns the object whose 004BA120 sets the background colour).
void* __cdecl Renderer() { return SceneRenderer(); }

// The world server.
CWorldServer* TheWorld() { return TheServerWorld(); }

// The world's AI map (CWorldServer +0x170); its sector map is at +0x124.
CAIMap* TheAIMap() { return TheServerWorld()->AIMap(); }

// Unit modes (the brain's behaviour list): operator new for a mode, then its script constructor.
// The command zeroes the reference dword (+4) before testing the pointer.
template <typename Mode>
Mode* NewMode(std::size_t size) {
    auto* mode = static_cast<Mode*>(gamecrt::operator_new(size));
    mode->refs = 0;
    mode->refsHigh = 0;
    return mode;
}
// Fear and Suspicion zero the mode's fields first (SV7b2, SV7d4: FearMode / SuspectionMode::ClearFields).
FearMode* ConstructFear(FearMode* mode, int on) {
    mode->ClearFields();
    return mode->Construct(static_cast<std::uint8_t>(on));
}
SuspectionMode* ConstructSuspicion(SuspectionMode* mode) {
    mode->ClearFields();
    return mode->ScriptSuspectionModeConstruct();
}

// A player (CWorldServer::GetPlayer): the world's view of the CPlayer it is.
CPlayer* PlayerOf(WorldPlayer* player) { return reinterpret_cast<CPlayer*>(player); }


// The named party records (party_records.h), through the originals: find 004A5E10 (a party),
// add 004A5E90, remove 004A5FA0.
NamedPartyRecords* Parties() { return &ThePartyManager()->records; }

// A party's items (00661DB0 add, 00661F30 remove).
int PartyAddItem(CParty* party, CEngineObject* item, int a, int b) { return (int)party->AddItem(static_cast<CItem*>(item), a, b); }
void PartyRemoveItem(CParty* party, CEngineObject* item, int a) { party->RemoveItem(static_cast<CItem*>(item), a); }

CDiplomacy* TheDiplomacy() { return GameDiplomacy(); }


// The multiplayer game server's variables, named by STLport strings (the original builds them
// two ways, (004217E0 + 00428F10) and 0040C5A0, with an empty allocator; the same string).
net::ServerSession* ServerPointer() { return net::GameServer(); }
// 004B9FD0: the server exists and is not shutting down.
bool ServerActive() {
    net::ServerSession* server = net::GameServer();
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

// The world's player list (CWorld::players_): the event dispatch.
CPlayerManager* TheEvents() { return static_cast<CPlayerManager*>(TheServerWorld()->players_); }

// The world's spell manager (CWorld::manager4C_).
CSpellManager* TheSpellManager() { return static_cast<CSpellManager*>(TheServerWorld()->manager4C_); }

// A particle source's effect (ScriptParticleSource::object).
CEffectParticle* ParticleOf(ScriptParticleSource* source) { return static_cast<CEffectParticle*>(source->object); }

// 004BA9F0: the manipulator that ends a console line.
void* EndLine() { return reinterpret_cast<void*>(0x004BA9F0); }


// MFC containers the commands keep on the stack, built and destroyed through the originals: the
// units found (CPtrList: constructor 004C96A0, destructor 004C96F0), the items given to a player
// (constructor 004C98C0, AddTail 004C98F0, destructor 00427830) and the party slots (CArray of
// 40-byte slots: constructor 004BB000, destructor 004BB020).
using PtrList = mfc::CList<CUnit*>;
struct PartySlot {
    int unitId;
    std::uint8_t unused_04[36];
};
using PartyArray = mfc::CArray<PartySlot>;
using ObjectIterator = ObjectListIterator;

// Damage description for KillUnit (vtable +0x7C; 0x34 bytes used, the original reserves 0x4C).
__declspec(naked) void __fastcall ResetDamageInfoAt(void*) EI_ORIGINAL(0x00522B40)

// ---- Small accessors the original calls out of line (004B9F20-004BAA30) -------------------------

// The unit's server half (CUnit::controller).
void* UnitState(CUnit* unit) { return unit->controller; }
CUnitServer* Controller(void* state) { return static_cast<CUnitServer*>(state); }
AIBrain* BrainOf(void* state) { return reinterpret_cast<AIBrain*>(Controller(state)->Brain()); }
// The brain's patrol mode, null when it has none.
PatrolMode* PatrolOf(void* state) { return static_cast<PatrolMode*>(BrainOf(state)->FindPatrolMode()); }
UnitStats* StatsOf(void* state) { return &Controller(state)->stats; }
Vector3* Position(CEngineObject* object) { return &static_cast<CMapObject*>(object)->position; }

CEngineObject* Arg(const SSCCmd* cmd, int i) { return cmd->objectArgs.data[i]->object; }
const char* Text(const SSCCmd* cmd, int i) { return cmd->stringArgs.data[i]; }

// The unit's server half when argument 0 is a unit that has one, else null.
void* UnitStateArg(const SSCCmd* cmd) {
    CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
    return unit ? UnitState(unit) : nullptr;
}

// Finds the particle source with `id`.
ScriptParticleSource* FindParticle(CScript* script, int id) {
    for (auto* node = script->particles_.head; node; node = node->next)
        if (node->data.id == id) return &node->data;
    return nullptr;
}

// The quest objective commands' text argument when they take none: an empty CString.
void QuestObjectiveNoText(int id, float value) {
    mfc::CString text(game::EmptyText());
    ScriptQuestAddObjective(id, &text, value);
}

// Runs a For body in the same ScriptRun: the first four fields are saved, the body block run from
// its start, and the fields restored (the variables are shared).
void RunBody(ScriptRun* run, CScript* script, ScriptDecl* body) {
    ScriptDecl* block = run->block;
    int index = run->index;
    int ticks = run->ticks;
    int sleepStart = run->sleepStart;
    run->block = body;
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    ExecuteOrig(run, nullptr, script);
    run->block = block;
    run->index = index;
    run->ticks = ticks;
    run->sleepStart = sleepStart;
}

// Copies group element `i` into the loop counter (a global object variable).
void SetLoopCounter(const SSCCmd* cmd, int i) {
    ScriptObjectRef* element = &cmd->groupArgs.data[0]->data[i];
    ScriptObjectRef* counter = cmd->objectArgs.data[0];
    counter->Reset();
    counter->Construct(*element);
}

int& ForDepth(CScript* script) { return script->forDepth_; }

// The commands that need more than a few lines.

void SetCP(const SSCCmd* cmd, bool fast) {
    CEngineObject* object = Arg(cmd, 0);
    if (!object) return;
    const float* f = cmd->floatArgs.data;
    CAIMap* map = TheAIMap();
    if (fast) {
        map->SetRefreshUnits(0);
        Vector3 point = {f[0], f[1], f[2]};
        map->MoveInSectors(object, &point);
        if (object->GetClassId() == kClassUnit) Controller(UnitState(static_cast<CUnit*>(object)))->ResyncWithUnit();
        map->SetRefreshUnits(1);
        static_cast<CMapObject*>(object)->MarkChanged(2);
        return;
    }
    float oldZ = Position(object)->z;
    Vector3 point = {f[0], f[1], f[2]};
    map->MoveInSectors(object, &point);
    if (object->GetClassId() == kClassUnit) {
        Controller(UnitState(static_cast<CUnit*>(object)))->ResyncWithUnit();
        EI_NAN_AUDIT(oldZ, Position(object)->z);
        if (oldZ != Position(object)->z) static_cast<CMapObject*>(object)->MarkChanged(2);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    PtrList units;
    mfc::impl::ConstructListHeader<mfc::impl::PtrListClass>(&units, 10);
    float radius = static_cast<CMapObject*>(object)->radius + static_cast<CMapObject*>(object)->radius;
    map->FindUnitsInRadius(Position(object), &units, radius);
    for (PtrList::Node* pos = units.head; pos;) {
        CUnit* unit = pos->data;
        pos = pos->next;
        map->RemoveFromSectors(unit);
    }
    for (PtrList::Node* pos = units.head; pos;) {
        Vector3 free;
        CUnit* unit = pos->data;
        pos = pos->next;
        if (!map->CanLeaveCell(unit)) {
            free = *Position(unit);
            map->FindFreeCellNear(unit, Position(unit), &free);
            *Position(unit) = free;
            Controller(UnitState(unit))->ResyncWithUnit();
        }
        map->AddToSectors(unit);
    }
    static_cast<CMapObject*>(object)->MarkChanged(2);
    mfc::impl::DestructList<mfc::impl::PtrListClass>(units);
}

void GiveSkill(const SSCCmd* cmd) {
    void* state = cmd->objectArgs.data[0]->GetUnitPerception();
    if (!state) return;
    // The skill names (0078B8FC..0078B8B0) in the order the original tests them; science goes to
    // SetScience, the others to SetSkill with their index.
    static const char* const kNames[] = {"science", "melee", "archery", "backstab", "elemental",
                                         "sense", "astral", "stealth", "awareness"};
    static const int kIndex[] = {0, 0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 9; ++i) {
        if (gamecrt::mbsicmp(Text(cmd, 0), kNames[i]) != 0) continue;
        CUnit* owner = Controller(state)->unit;
        int value = Ftol(cmd->floatArgs.data[0]);
        UnitStats* stats = StatsOf(state);
        if (i == 0) stats->RaiseAttribute(kIndex[i], value, owner);
        else stats->RaiseSkill(kIndex[i], value, owner);
    }
}

void RedeployParty(const SSCCmd* cmd) {
    WorldPlayer* player = TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]));
    if (!player) return;
    CPlayerServer* state = player->server;
    PartyArray slots;
    mfc::impl::ConstructArrayOf<mfc::impl::PartySlotArrayClass>(slots);
    TheWorld()->CollectParty(player, &slots);
    void* party = player->Party();
    ThePartyManager()->BeginRedeploy(party);
    ThePartyManager()->EndRedeploy(party);
    // One slot and one party unit: the slot gets the unit's id.
    const PartyMemberArray& members = player->Party()->members_;
    if (slots.size == 1 && members.size == 1) slots.data[0].unitId = static_cast<int>(members.data[0].unitId);
    state->SendParty();
    player->server->lastPosition_.z = -1313.0f;
    TheWorld()->Redeploy(player, &slots, 0);
    mfc::impl::DestructArrayOf<mfc::impl::PartySlotArrayClass>(slots);
}

void CopyLoot(const SSCCmd* cmd, bool replace) {
    if (!TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]))) return;
    CParty* from = static_cast<CParty*>(Parties()->Find(Text(cmd, 0)));
    CParty* to = static_cast<CParty*>(Parties()->Find(Text(cmd, 1)));
    if (!from || !to) return;
    if (replace) {
        while (to->items_.size != 0) PartyRemoveItem(to, static_cast<CEngineObject*>(to->items_.data[0]), 1);
    }
    for (int i = 0; i < from->items_.size; ++i)
        PartyAddItem(to, static_cast<CEngineObject*>(from->items_.data[i]), 1, 1);
    if (replace) to->Money() = from->Money();
    else to->Money() += from->Money();
}

void GSSetVarMultiplayer(const SSCCmd* cmd) {
    stlp::string text(Text(cmd, 0));
    ServerPointer()->SetVariable(&text, cmd->floatArgs.data[1]);
}

void GSVarMultiplayer(const SSCCmd* cmd, bool setMax) {
    stlp::string text(Text(cmd, 0));
    if (setMax) ServerPointer()->RaiseVariable(&text, cmd->floatArgs.data[1]);
    else ServerPointer()->RemoveVariable(&text);
}

void PlayMovie(const SSCCmd* cmd) {
    mfc::CString path = *GameBasePathString() + MoviesFolder();
    mfc::CString file = path + cmd->stringArgs.data[0];
    ::PlayMovie(file, 0, 1, 1, 1, 1, 1);
}

void CastSpell(const SSCCmd* cmd, bool atUnit) {
    CEngineObject* spell = TheSpellManager()->CreateSpellFromText(Text(cmd, 0));
    if (spell) ++spell->refCount_;
    if (spell) {
        const float* f = cmd->floatArgs.data;
        Vector3 from = {f[0], f[1], 0.0f};
        if (atUnit) {
            CEngineObject* target = Arg(cmd, 0);
            if (target) TheWorld()->CastSpell(spell, 0, &from, target, Position(target), 0);
        } else {
            Vector3 to = {f[2], f[3], 0.0f};
            TheWorld()->CastSpell(spell, 0, &from, nullptr, &to, 0);
        }
    }
    if (spell) spell->Release();
}

void UnitModes(const SSCCmd* cmd) {
    // UMAg: aggression set 0..3 replaces modes 3, 4, 5 and 7.
    void* state = UnitStateArg(cmd);
    if (!state) return;
    AIBrain* brain = BrainOf(state);
    brain->RemoveModesOfType(3);
    brain->RemoveModesOfType(4);
    brain->RemoveModesOfType(5);
    brain->RemoveModesOfType(7);
    switch (Round(cmd->floatArgs.data[0])) {
    case 0:
        brain->AddMode(ConstructFear(NewMode<FearMode>(0x2C), 1));
        brain->AddMode(ConstructSuspicion(NewMode<SuspectionMode>(0x18)));
        brain->AddMode(NewMode<CorpseWatcherMode>(0x08)->ScriptCorpseWatcherModeConstruct());
        break;
    case 1:
        brain->AddMode(ConstructFear(NewMode<FearMode>(0x2C), 0));
        brain->AddMode(ConstructSuspicion(NewMode<SuspectionMode>(0x18)));
        brain->AddMode(NewMode<CorpseWatcherMode>(0x08)->ScriptCorpseWatcherModeConstruct());
        break;
    case 2:
        brain->AddMode(NewMode<AggressionMode>(0x18)->ScriptRevengeModeConstruct(1));
        brain->AddMode(ConstructFear(NewMode<FearMode>(0x2C), 0));
        brain->AddMode(ConstructSuspicion(NewMode<SuspectionMode>(0x18)));
        brain->AddMode(NewMode<CorpseWatcherMode>(0x08)->ScriptCorpseWatcherModeConstruct());
        break;
    case 3:
        brain->AddMode(NewMode<AggressionMode>(0x18)->ScriptAggressionModeConstruct());
        brain->AddMode(ConstructFear(NewMode<FearMode>(0x2C), 0));
        brain->AddMode(ConstructSuspicion(NewMode<SuspectionMode>(0x18)));
        brain->AddMode(NewMode<CorpseWatcherMode>(0x08)->ScriptCorpseWatcherModeConstruct());
        break;
    default:
        break;
    }
}

} // namespace

// ---- ScriptRun::Execute -------------------------------------------------------------------------

BOOL ScriptRun::Execute(CScript* script) {
    ++ticks;
    if (index >= block->commands.size) return 0;
    vars.RemoveDeletedObjects();
    int cont = 1;
    while (index < block->commands.size) {
        if (!cont) return 1;
        SSCCmd* cmd = &block->commands.data[index];
        cmd->SetParameters(&cmd->localBindings, &vars);
        cmd->SetParameters(&cmd->globalBindings, script->Globals());
        for (int i = 0; i < cmd->subCalls.size; ++i)
            CalcParametersOrig(cmd, nullptr, script, &vars, &cmd->subCalls.data[i]);
        cont = 1;
        bool done = false;
        if (scriptaddon::Enabled()) {   // Rebirth's AddNewScriptCommands (hook at 004B684E)
            scriptaddon::Call call{cmd, script, nullptr, &cont, this};
            done = scriptaddon::Dispatch(&call);
        }

        const float* f = cmd->floatArgs.data;
        switch (done ? -1 : cmd->id) {
        // ---- Flow -------------------------------------------------------------------------------
        case 0x07: {   // Sleep(ticks): counts this run's Execute calls from the first one
            if (sleepStart < 0) sleepStart = ticks;
            int n = Round(f[0]);
            if (ticks - sleepStart < n) cont = 0;
            else sleepStart = -1;
            break;
        }
        case 0x09:     // WaitSegment(ticks): until this run has been executed that often
            if (ticks < Round(f[0])) cont = 0;
            break;
        case 0xDE:     // SleepUntil(condition)
            if (IsZero(f[0])) cont = 0;
            break;
        case 0xB9: {   // SleepUntilIdle(unit): while the brain has an action (+0x984)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (unit && Controller(UnitState(unit))->Brain()->action != 0) cont = 0;
            break;
        }
        case 0x08: {   // a declared script: start an instance with the call's arguments
            int which = Ftol(f[0]);
            auto* definition = &script->Scripts().data[which];
            auto* node = script->instances_.PushHead(definition);
            ScriptDefinition* instance = &node->data;
            instance->BindCall(cmd);
            instance->index = 0;   // the killed flag
            break;
        }
        case 0x79:     // KillScript
            if (!killFlag) game::FatalError(kKillScriptNotAllowed);
            *killFlag = 1;
            break;
        case kCmdFor: {   // For(counter, group)(body): the group's members from last to first
            ++ForDepth(script);
            for (int i = cmd->groupArgs.data[0]->size - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case kCmdForIf: {   // ForIf(counter, group, condition)(body)
            ++ForDepth(script);
            const ScriptSubCall* condition = nullptr;
            for (int i = 0; i < cmd->subCalls.size; ++i) {
                if (cmd->subCalls.data[i].type == kScriptFloat) {
                    condition = &cmd->subCalls.data[i];
                    break;
                }
            }
            for (int i = cmd->groupArgs.data[0]->size - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                if (condition) CalcParametersOrig(cmd, nullptr, script, &vars, condition);
                if (!IsZero(cmd->floatArgs.data[0])) RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case kCmdAssign: {   // global = value: float 0 variable index, float 1 type - 1
            ScriptVars* globals = script->Globals();
            switch (Ftol(f[1])) {
            case 0:
                std::memcpy(&globals->floats.data[Ftol(f[0])], &f[2], 4);
                break;
            case 1:
                globals->strings.data[Ftol(f[0])] = cmd->stringArgs.data[0];
                break;
            case 2: {
                ScriptObjectRef* value = cmd->objectArgs.data[0];
                ScriptObjectRef* slot = &globals->objects.data[Ftol(f[0])];
                slot->Reset();
                slot->Construct(*value);
                break;
            }
            case 3: {
                ScriptGroup* value = cmd->groupArgs.data[0];
                globals->groups.data[Ftol(f[0])].CopyFrom(*value);
                break;
            }
            default:
                break;
            }
            break;
        }

        // ---- Groups -----------------------------------------------------------------------------
        case 0x22:   // AddObject(group, object)
            cmd->groupArgs.data[0]->Insert(Arg(cmd, 0));
            break;
        case 0x23: {   // RemoveObject(group, object)
            ScriptGroup* group = cmd->groupArgs.data[0];
            int at = group->IndexOf(*cmd->objectArgs.data[0]);
            if (at >= 0) mfc::impl::ArrayAs<gamearrays::ObjectRefs>(*group).RemoveAt(at, 1);   // 004C2200
            break;
        }

        // ---- Lights, particles, effects (CScript helpers) ---------------------------------------
        case 0x0B: script->CreatePointLight(cmd); break;
        case 0x0C: script->MovePointLight(cmd); break;
        case 0x0D: script->DeletePointLight(cmd); break;
        case 0x0E: script->ScriptCreateParticleSource(cmd); break;
        case 0x10: script->ScriptMoveParticleSource(cmd); break;
        case 0x0F: script->ScriptCreateLightning(cmd); break;
        case 0x17: script->ScriptCreateFx(cmd); break;
        case 0x16:     // CreateFXSource
        case 0x18:     // CreateRandomizedFXSource
            script->ScriptCreateFxSource(cmd);
            break;
        case 0x11: {   // DeleteParticleSource(id)
            int id = Round(f[0]);
            for (ParticleList::Node* node = script->particles_.head; node;) {
                ParticleList::Node* current = node;
                node = node->next;
                if (current->data.id == id) {
                    mfc::impl::RemoveAtAs<gamelists::ParticleSources>(&script->particles_, current);   // 004C6040
                    break;
                }
            }
            break;
        }
        case 0x12: {   // AttachParticleSource(id, objectId)
            int objectId = Round(f[1]);
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) ParticleOf(entry)->SetAttachedTo(objectId);
            break;
        }
        case 0xE7: {   // AttachParticles(id, object)
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            int objectId = object->id_;
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) ParticleOf(entry)->SetAttachedTo(objectId);
            break;
        }
        case 0xBA: {   // SetParticleSourceSize(id, size)
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) static_cast<CEffectParticle*>(entry->object)->SetSize(f[1]);
            break;
        }
        case 0x1A: {   // DeleteLightning(id): every lightning with the id (list +0x4C, head +0x50)
            for (LightningList::Node* node = script->lightning_.head; node;) {
                LightningList::Node* current = node;
                node = node->next;
                if (current->data.id == Ftol(f[0])) mfc::impl::RemoveAtAs<gamelists::Lightnings>(&script->lightning_, current);   // 004C6680
            }
            break;
        }
        case 0x19: {   // DeleteFXSource(id) (list +0x30, head +0x34; the id at node +0x0C)
            int id = Round(f[0]);
            for (FxSourceList::Node* node = script->fxSources_.head; node; node = node->next) {
                if (node->data.id == id) {
                    mfc::impl::RemoveAtAs<gamelists::FxSources>(&script->fxSources_, node);   // 004C6370
                    break;
                }
            }
            break;
        }

        // ---- World ------------------------------------------------------------------------------
        case 0x39:   // SendEvent(type, value)
            TheEvents()->SendValueToAll(Ftol(f[0]), Ftol(f[1]));
            break;
        case 0x3A:   // SendStringEvent(type, text)
            TheEvents()->SendTextToAll(Ftol(f[0]), Text(cmd, 0));
            break;
        case 0x97: {   // GodMode(player, on): player +0x108
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) player->godMode = static_cast<std::uint8_t>(Ftol(f[1]));
            break;
        }
        case 0x37: {   // SetDiplomacy(a, b, value): > 0 allied (0), < 0 enemies (2), else (0 or NaN) neutral (1)
            int relation;
            if (f[2] > 0.0f) relation = 0;
            else relation = f[2] < 0.0f ? 2 : 1;
            TheDiplomacy()->SetRelation(Ftol(f[0]), Ftol(f[1]), relation, 1);
            break;
        }
        case 0x43: {   // InvokeAlarm(type, x, y)
            Vector3 point = {f[1], f[2], 0.0f};
            TheWorld()->InvokeAlarm(Ftol(f[0]), &point.x, 1);
            break;
        }
        case 0x88: {   // AddUnitToServer(name, prototype, ..., x, y, z)
            Vector3 point = {f[0], f[1], f[2]};
            TheWorld()->AddUnitToServer(static_cast<unsigned>(TextKeyFromName(Text(cmd, 0))), Text(cmd, 1), Text(cmd, 2), &point);
            break;
        }
        case 0x89:     // RemoveUnitFromServer(object)
        case 0x96: {   // RemoveObjectFromServer(object)
            CEngineObject* object = Arg(cmd, 0);
            if (object) WorldRemoveObject(TheWorld(), nullptr, object->id_);   // Rebirth patches it
            break;
        }
        case 0xD2:   // RecalcMercBriefings
            TheWorld()->UpdateMercBriefings();
            break;
        case 0x1E: {   // SetWind(x, y, z, strength)
            Vector3 direction = {f[0], f[1], f[2]};
            TheWorld()->ChangeWind(&direction, f[3]);
            break;
        }
        case 0x1F: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255 (0073C36C)
            const float scale = ColourScale();
            float b = f[2] * scale;
            float g = f[1] * scale;
            float r = f[0] * scale;
            SceneRenderer()->SetFogColour(r, g, b);
            break;
        }
        case 0xB6:   // RunWorldTime(time): the configured speed (int at 0079BE5C)
            TheWorld()->SetTimeSpeedImpl(static_cast<float>(ConfiguredTimeSpeed()));
            TheWorld()->SetTimeImpl(f[0]);
            break;
        case 0xB7:   // FixWorldTime(time)
            TheWorld()->SetTimeSpeedImpl(0.0f);
            TheWorld()->SetTimeImpl(f[0]);
            break;
        case 0xB8:   // SetWaterLevel(a, level, b)
            TheWorld()->SetWaterLevel(Round(f[0]), f[1], Round(f[2]));
            break;
        case 0xA0:   // AddMob(name)
            TheServerWorld()->LoadMobFile(Text(cmd, 0));
            break;
        case 0xA1:   // PlayCamera(name)
            GamePlayCameraPath(Text(cmd, 0), 0);
            break;
        case 0xC1:   // PlayMovie(name)
            PlayMovie(cmd);
            break;
        case 0x7D:   // ConsoleFloat(value)
            console::Out().PutFloatArgument(f[0]).Apply(EndLine());
            break;
        case 0x7E:   // ConsoleString(text)
            (console::Out() << Text(cmd, 0)).Apply(EndLine());
            break;
        case 0xC3: {   // AddRectToArea(area, x1, y1, x2, y2)
            auto* area = static_cast<mfc::CArray<ScriptAreaPart>*>(script->FindArea(Round(f[0])));
            ScriptAreaAddRect(area, nullptr, f[1], f[2], f[3], f[4]);
            break;
        }
        case 0xC4: {   // AddRoundToArea(area, x, y, radius)
            auto* area = static_cast<mfc::CArray<ScriptAreaPart>*>(script->FindArea(Round(f[0])));
            ScriptAreaAddRound(area, nullptr, f[1], f[2], f[3]);
            break;
        }
        case 0xC6:   // DeleteArea(area)
            script->DeleteArea(Round(f[0]));
            break;
        case 0xC2: {   // HideObject(object, hide): flags_ bit 0x10000
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (IsZero(f[0])) object->flags_ &= ~0x10000u;
            else object->flags_ |= 0x10000u;
            break;
        }
        case 0x95: {   // ActivateTrap(object, on): flags_ bit 0x80000
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (IsZero(f[0])) object->flags_ &= ~0x80000u;
            else object->flags_ |= 0x80000u;
            break;
        }
        case 0x8E:   // SetCP(object, x, y, z)
            SetCP(cmd, false);
            break;
        case 0xAD:   // SetCPFast(object, x, y, z)
            SetCP(cmd, true);
            break;

        // ---- Levers -----------------------------------------------------------------------------
        case 0x91:     // SwitchLeverState(lever, state)
        case 0x9E: {   // SwitchLeverStateEx(lever, state, time)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            float time;
            if (cmd->id == 0x91) std::memcpy(&time, "\x00\x00\x20\xC1", 4);   // -10.0f
            else time = f[1];
            static_cast<Lever*>(lever)->SetState(Ftol(f[0]), time);
            break;
        }
        case 0xCC: {   // EnableLever(lever, on)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            static_cast<Lever*>(lever)->Enable(!IsZero(f[0]));
            break;
        }
        case 0xAC: {   // SetScience(lever, a, b, four flags)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            LockKey lock = {1, 0, 0};
            lock.id = static_cast<std::uint32_t>(Ftol(f[0]));
            lock.level = static_cast<std::uint32_t>(Ftol(f[1]));
            lock.mask = 0;
            if (!IsZero(f[2])) lock.mask = 1;
            if (!IsZero(f[3])) lock.mask |= 2;
            if (!IsZero(f[4])) lock.mask |= 4;
            if (!IsZero(f[5])) lock.mask |= 8;
            static_cast<Lever*>(lever)->SetLock(&lock);
            break;
        }

        // ---- Units ------------------------------------------------------------------------------
        case 0x32: {   // BlockUnit(unit, on): flags_ bit 0x800000
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            if (IsZero(f[0])) unit->flags_ &= ~0x800000u;
            else unit->flags_ |= 0x800000u;
            break;
        }
        case 0x36: {   // SetPlayer(unit, player)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            unit->SetHostileMask(reinterpret_cast<CDiplomacy*>(TheDiplomacy())->GetHostileMask(Ftol(f[0])));
            unit->SetSide(Ftol(f[0]));
            break;
        }
        case 0x38: {   // SetEnemy(unit, player): adds the player to the hostile mask (+0x244)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            int player = Ftol(f[0]);
            std::uint32_t mask = (1u << (player & 31)) | unit->relationsA;
            unit->AddHostileMask(mask);
            break;
        }
        case 0x35: {   // KillUnit(object)
            CEngineObject* object = Arg(cmd, 0);
            alignas(4) std::uint8_t damage[0x4C];
            ResetDamageInfoAt(damage);
            if (object) ObjectSlots(object)->ReceiveHit(reinterpret_cast<DamageInfo*>(damage), nullptr, nullptr);
            break;
        }
        case 0x44: {   // InflictDamage(unit, amount)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            std::uint32_t damage[15] = {};
            damage[5] = 0x3F800000u;    // +0x14 1.0
            damage[7] = 0;              // +0x1C
            damage[8] = 0x42C80000u;    // +0x20 100.0
            std::memcpy(&damage[9], &f[0], 4);   // +0x24 amount
            damage[10] = 0x3F800000u;   // +0x28 1.0
            damage[11] = 1;             // +0x2C
            // TakeHit(hit, attacker, source): three arguments (the original pushes the first 0 early).
            ServerSlots(Controller(state))->TakeHitSlot(reinterpret_cast<DamageInfo*>(damage), nullptr, nullptr);
            break;
        }
        case 0x83: {   // AddUnitUnderControl(player, unit)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (player && unit) unit->SetPlayer(reinterpret_cast<char*>(player));
            break;
        }
        case 0x84: {   // RemoveUnitFromControl(player, unit)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (unit) unit->SetPlayer(nullptr);
            break;
        }
        case 0x42:   // SetSpellAggression(unit, value): reads the unit's +0x170 and does nothing
            break;
        case 0xAE: {   // RemoveQuestItem(unit, id): the first quest item (+0x4A0, count +0x4A4) with the id
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            int count = unit->carriedCount;
            int id = Ftol(f[0]);
            for (int i = 0; i < count; ++i) {
                auto* item = static_cast<CItemQuest*>(unit->carried[i]);
                if (static_cast<int>(item->questItemId_) == id) {
                    unit->RemoveCarried(i);
                    break;
                }
            }
            break;
        }
        case 0xAF: {   // GiveUnitQuestItem(unit, item)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            CEngineObject* item = TheWorld()->CreateItemNamed(Text(cmd, 0));
            if (item && item->GetClassId() == kClassQuestItem) unit->AddCarried(item);
            break;
        }
        case 0xB3: {   // GiveUnitSpell(unit, spell)
            CUnit* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            CItem* spell = TheWorld()->CreateSpellNamed(Text(cmd, 0));
            if (!spell) break;
            unit->AddSpell(spell);
            void* state = UnitState(unit);
            ServerSlots(Controller(state))->RefreshEquipment();
            break;
        }
        case 0xCD:   // GiveSkill(unit, skill, value)
            GiveSkill(cmd);
            break;
        case 0xCE:     // GiveStrength(unit, value)
        case 0xCF:     // GiveDexterity
        case 0xD0: {   // GiveIntelligence
            void* state = cmd->objectArgs.data[0]->GetUnitPerception();
            if (!state) break;
            CUnit* owner = Controller(state)->unit;
            int value = Ftol(f[0]);
            UnitStats* stats = StatsOf(state);
            if (cmd->id == 0xCE) stats->RaiseStrength(value, owner);
            else if (cmd->id == 0xCF) stats->RaiseDexterity(value, owner);
            else stats->RaiseIntelligence(value, owner);
            break;
        }

        // ---- Unit orders (the brain at the unit's +0x240 object +0x980) -------------------------
        case 0x02: {   // MoveToPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x03: {   // RotateTo(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            const Vector3* at = Position(Controller(state)->unit);
            Vector3 direction = {f[0] - at->x, f[1] - at->y, -at->z};
            BrainOf(state)->TurnTowards(&direction, 0);
            break;
        }
        case 0x04: {   // MoveToObject(unit, target)
            void* state = UnitStateArg(cmd);
            CEngineObject* target = Arg(cmd, 1);
            if (!state || !target) break;
            Vector3 point = {Position(target)->x, Position(target)->y, 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x3B: {   // Follow(unit, leader)
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->Follow(Arg(cmd, 1));
            break;
        }
        case 0x3C: {   // Attack(unit, target)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            CEngineObject* target = Arg(cmd, 1);
            if (target) BrainOf(state)->AttackTarget(target);
            break;
        }
        case 0xE8: {   // ResetTarget(unit)
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ResetBehaviour();
            break;
        }
        case 0x3D: {   // Cast(unit, x, y): at the ground height there
            void* state = UnitStateArg(cmd);
            if (!state) break;
            float x = f[0];
            float y = f[1];
            float z = TheWorld()->GridHeight(x, y);
            Vector3 point = {x, y, z};
            BrainOf(state)->CastAt(0, &point, 0);
            break;
        }
        case 0xA8:   // CastSpellPoint(spell, x, y, x2, y2)
            CastSpell(cmd, false);
            break;
        case 0xA9:   // CastSpellUnit(spell, x, y, target)
            CastSpell(cmd, true);
            break;
        case 0x34: {   // Idle(unit)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->ClearModes();
            BrainOf(state)->StopAction();
            break;
        }
        case 0x33: {   // Guard(unit, x, y, radius)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardGuard(&point, f[2]);
            break;
        }
        case 0x94: {   // Sentry(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardSentry(&point);
            break;
        }
        case 0x1B: {   // PlayAnimation(unit, name)
            void* state = UnitStateArg(cmd);
            if (state) Controller(state)->PlayAnimation(Text(cmd, 0));
            break;
        }
        case 0xC9:     // Lie
        case 0x3E:     // Crawl
        case 0x3F:     // Stand
        case 0x40:     // Walk
        case 0x41: {   // Run
            void* state = UnitStateArg(cmd);
            if (!state) break;
            int style = cmd->id == 0xC9 ? 0 : cmd->id == 0x3E ? 1 : cmd->id == 0x41 ? 3 : 2;
            Controller(state)->SetMoveStyle(style);   // 0 lie, 1 crawl, 2 walk, 3 run
            break;
        }
        case 0xB0: {   // Rest(unit)
            void* state = UnitStateArg(cmd);
            if (state) Controller(state)->StopQueued();
            break;
        }

        // ---- Unit modes -------------------------------------------------------------------------
        case 0x24: {   // UMClear
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ClearModes();
            break;
        }
        case 0x25: {   // UMSentry(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            auto* mode = NewMode<SentryMode>(0x14);
            BrainOf(state)->AddMode(mode ? mode->ScriptSentryModeConstruct(&point) : nullptr);
            break;
        }
        case 0x26:     // UMGuard(unit, x, y, radius)
        case 0x27: {   // UMGuardEx(unit, x, y, radius, radius2)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            float radius = f[2];
            float radius2;
            if (cmd->id == 0x26) std::memcpy(&radius2, "\x00\x00\x80\xBF", 4);   // -1.0f
            else radius2 = f[3];
            auto* mode = NewMode<GuardMode>(0x20);
            BrainOf(state)->AddMode(mode ? mode->ScriptGuardModeConstruct(&point, radius, radius2) : nullptr);
            break;
        }
        case 0xCA: {   // UMFollow(unit, leader)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            CEngineObject* leader = Arg(cmd, 1);
            auto* mode = NewMode<FollowMode>(0x0C);
            BrainOf(state)->AddMode(mode ? mode->ScriptFollowModeConstruct(leader) : nullptr);
            break;
        }
        case 0x28: {   // UMPatrol
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* mode = NewMode<PatrolMode>(0x20);
            BrainOf(state)->AddMode(mode ? mode->Construct() : nullptr);
            break;
        }
        case 0x29: {   // UMPatrolClear
            void* state = UnitStateArg(cmd);
            if (!state) break;
            void* patrol = BrainOf(state)->FindPatrolMode();
            if (patrol) static_cast<PatrolMode*>(patrol)->points.SetSize(0, -1);
            break;
        }
        case 0x2A: {   // UMPatrolAddPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            Vector3 point = {f[0], f[1], 0.0f};
            patrol->AppendPoint(&point);
            break;
        }
        case 0x2B: {   // UMPatrolAddPointLook(unit, x, y, z): a look at the last point
            void* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            PatrolPoint* last = patrol->LastPoint();
            if (!last) break;
            AILook look;
            AILook* value = look.ScriptAILookConstruct(f[0], f[1], f[2], 0);
            last->looks.SetAtGrow(last->looks.size, *value);
            break;
        }
        case 0x31: {   // UMPlayer
            void* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            brain->ClearModes();
            auto* mode = NewMode<PlayerMode>(0x24);
            brain->AddMode(mode ? mode->Construct() : nullptr);
            brain->StopAction();
            break;
        }
        case 0xC8:   // UMAg(unit, level)
            UnitModes(cmd);
            break;
        case 0x2C: {   // UMStandard: aggression, suspicion, corpse watcher
            void* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            auto* aggression = NewMode<AggressionMode>(0x18);
            brain->AddMode(aggression ? aggression->ScriptAggressionModeConstruct() : nullptr);
            auto* suspicion = NewMode<SuspectionMode>(0x18);
            brain->AddMode(suspicion ? ConstructSuspicion(suspicion) : nullptr);
            auto* watcher = NewMode<CorpseWatcherMode>(0x08);
            brain->AddMode(watcher ? watcher->ScriptCorpseWatcherModeConstruct() : nullptr);
            break;
        }
        case 0x2D: {   // UMAggression
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* mode = NewMode<AggressionMode>(0x18);
            BrainOf(state)->AddMode(mode ? mode->ScriptAggressionModeConstruct() : nullptr);
            break;
        }
        case 0x2E: {   // UMRevenge
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* mode = NewMode<AggressionMode>(0x18);
            BrainOf(state)->AddMode(mode ? mode->ScriptRevengeModeConstruct(1) : nullptr);
            break;
        }
        case 0xC7: {   // UMFear(unit, on)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            int on = IsZero(f[0]) ? 0 : 1;
            auto* mode = NewMode<FearMode>(0x2C);
            BrainOf(state)->AddMode(mode ? ConstructFear(mode, on) : nullptr);
            break;
        }
        case 0x2F: {   // UMSuspection
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* mode = NewMode<SuspectionMode>(0x18);
            BrainOf(state)->AddMode(mode ? ConstructSuspicion(mode) : nullptr);
            break;
        }
        case 0x30: {   // UMCorpseWatcher
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* mode = NewMode<CorpseWatcherMode>(0x08);
            BrainOf(state)->AddMode(mode ? mode->ScriptCorpseWatcherModeConstruct() : nullptr);
            break;
        }

        // ---- Players: variables, parties, items -------------------------------------------------
        case 0x7F: {   // GSSetVar(player, name, value)
            if (IsMultiplayerClient() && ServerActive()) {
                GSSetVarMultiplayer(cmd);
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) player->server->SetVariable(Text(cmd, 0), std::bit_cast<std::uint32_t>(f[1]));   // the float bits
            break;
        }
        case 0x98:     // GSSetVarMax(player, name, value)
        case 0x82: {   // GSDelVar(player, name)
            bool setMax = cmd->id == 0x98;
            if (IsMultiplayerClient() && ServerActive()) {
                GSVarMultiplayer(cmd, setMax);
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            if (setMax) player->server->SetVariableMax(Text(cmd, 0), f[1]);
            else player->server->DeleteVariable(Text(cmd, 0));
            break;
        }
        case 0x1D: {   // PlayMusic(player, name)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) player->server->SendText0D(Text(cmd, 0));
            break;
        }
        case 0x8A: {   // LeaveToZone(player, zone, point)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) player->server->LeaveToZone(Text(cmd, 0), Ftol(f[1]));
            break;
        }
        case 0xD3:   // RedeployParty(player)
            RedeployParty(cmd);
            break;
        case 0x86:     // AddUnitToParty(player, party.unit, prototype)
        case 0x87: {   // RemoveUnitFromParty(player, party.unit)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString name(cmd->stringArgs.data[0]);
            auto* records = static_cast<CParty*>(PartyForScript(player, &name));
            if (records) {
                if (cmd->id == 0x86)
                    records->AddMember(name, Text(cmd, 1), game::EmptyText());
                else
                    records->RemoveMember(static_cast<unsigned>(TextKeyFromName(name)));
            }
            break;
        }
        case 0xD4:     // CopyItems(player, from, to)
        case 0x9D: {   // CopyStats(player, from, to)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString from(cmd->stringArgs.data[0]);
            mfc::CString to(cmd->stringArgs.data[1]);
            auto* fromRecords = static_cast<CParty*>(PartyForScript(player, &from));
            auto* toRecords = static_cast<CParty*>(PartyForScript(player, &to));
            PartyUnitRecord* source = reinterpret_cast<PartyRecords*>(fromRecords)->Find(TextKeyFromName(from));
            PartyUnitRecord* target = reinterpret_cast<PartyRecords*>(toRecords)->Find(TextKeyFromName(to));
            if (source && target) {
                if (cmd->id == 0xD4) {
                    target->CopyItemsFrom(source, toRecords);
                } else {
                    std::memcpy(target->stats, source->stats, sizeof(target->stats));
                    target->texts[0] = source->texts[0];
                }
            }
            break;
        }
        case 0x9A: {   // CreateParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Add(Text(cmd, 0));
            break;
        }
        case 0x9B: {   // RemoveParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Remove(Text(cmd, 0));
            break;
        }
        case 0x9C: {   // SetCurrentParty(player, name): the party list's +0x0C
            if (TheWorld()->GetPlayer(Ftol(f[0])))
                Parties()->text0C = cmd->stringArgs.data[0];
            break;
        }
        case 0xE0:   // AddLoot(player, from, to)
            CopyLoot(cmd, false);
            break;
        case 0xCB:   // CopyLoot(player, from, to)
            CopyLoot(cmd, true);
            break;
        case 0xA7: {   // GiveMoney(player, amount): player +0xC0
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CTypedPtrList<CEngineObject> items;
            mfc::impl::ConstructListHeader<mfc::impl::ItemListClass>(&items, 10);
            player->Party()->Money() += Ftol(f[1]);
            PlayerOf(player)->GiveItems(&items, 0);
            mfc::impl::DestructList<mfc::impl::ItemListClass>(items);
            break;
        }
        case 0x92:     // GiveQuestItem(player, item)
        case 0xA6: {   // GiveItem(player, item)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CTypedPtrList<CEngineObject> items;
            mfc::impl::ConstructListHeader<mfc::impl::ItemListClass>(&items, 10);
            CEngineObject* item = TheWorld()->CreateItemNamed(Text(cmd, 0));
            if (item) {
                items.AddTail(item);
                PlayerOf(player)->GiveItems(&items, 0);
            }
            mfc::impl::DestructList<mfc::impl::ItemListClass>(items);
            break;
        }
        case 0x93: {   // EraseQuestItem(player, id)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) PlayerOf(player)->TakeLoot(Ftol(f[1]));
            break;
        }
        case 0xDF: {   // FixItems: every object of the world's list (+0x54) copies +0x24 to +0x20
            auto* list = TheServerWorld()->itemManager_;
            ObjectIterator it;
            it.manager = nullptr;
            for (CEngineObject* object = it.First(list); object; object = it.Next()) static_cast<CItem*>(object)->Repair();
            if (it.manager) {   // destructor (004BB050)
                --it.manager->refCount_;
                it.manager = nullptr;
            }
            break;
        }

        // ---- Quests -----------------------------------------------------------------------------
        case 0x9F: {   // QuestComplete(player, quest)
            if (IsMultiplayerClient()) {
                if (FinishQuestMap(Text(cmd, 0)) != 0) {
                    void* quest = TheQuestTable()->Find(Text(cmd, 0));
                    if (quest) TheWorld()->AwardExperience(nullptr, static_cast<QuestRecord*>(quest)->exp);
                }
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            void* quest = TheQuestTable()->Find(Text(cmd, 0));
            if (!quest) break;
            TheWorld()->AwardExperience(player, static_cast<QuestRecord*>(quest)->exp);
            ClassTemplatesQuestCompleted(ThePartyManager());
            break;
        }
        case 0xD5:   // QStart(name)
            ScriptQuestStart(Text(cmd, 0));
            break;
        case 0xD6:   // QFinish
            ScriptQuestFinish(script);
            break;
        case 0xD7:   // QObjArea(area)
        case 0xDD:   // QObjGetItem(item)
            QuestObjectiveNoText(cmd->id, f[0]);
            break;
        case 0xD8:   // QObjSeeObject(name)
        case 0xD9:   // QObjSeeUnit
        case 0xDA:   // QObjKillUnit
        case 0xDB:   // QObjKillGroup
            ScriptQuestAddObjective(cmd->id, &cmd->stringArgs.data[0], 0.0f);
            break;
        case 0xDC:   // QObjUse(name, value)
            ScriptQuestAddObjective(cmd->id, &cmd->stringArgs.data[0], f[0]);
            break;

        default:   // functions, sections and commands without effect here
            break;
        }

        ScriptFreeTemporaries(script);
        if (cont) ++index;
    }
    return 1;
}
EI_REPLACE_METHOD(0x004B6760, ScriptRun::Execute);
