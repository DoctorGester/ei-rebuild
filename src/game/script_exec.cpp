// ScriptRun::Execute, the switch over every world script command. See docs/notes/script.md.
// Rebirth patches Execute (it adds commands), CalcParameters and FatalError.
#include "ai_map.h"
#include "item_manager.h"
#include "options.h"
#include "object_tables.h"
#include "script.h"
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

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "bink_video.h"
#include "game_singletons.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bit>
#include <list>
#include <string>
#include <vector>
#include <memory>

namespace {

// The game's _ftol: truncating conversion, low 32 bits of the 64-bit result.
int Ftol(float value) { return static_cast<int>(libcrt::Ftol(value)); }

bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

int Round(float value) { return x87::fistp(value); }

const char* const kKillScriptNotAllowed = "KillScript command not allowed here";
const char* MoviesFolder() { return "Movies\\"; }
constexpr float kColourScale = 0.003921569f;


bool ServerActive() {
    net::ServerSession* server = net::g_gameServer;
    return server && !(server->flags7 & RefCounted::kDetached);
}

AIBrain* BrainOf(UnitServer* state) { return &state->brain; }
PatrolMode* PatrolOf(UnitServer* state) { return BrainOf(state)->FindPatrolMode(); }
UnitStats* StatsOf(UnitServer* state) { return &state->stats; }
Vector3* Position(EngineObject* object) { return &ObjectSlots(object)->localPosition; }

EngineObject* Arg(const SSCCmd* cmd, int i) { return cmd->objectArgs[i]->object; }
const char* Text(const SSCCmd* cmd, int i) { return cmd->stringArgs[i].c_str(); }

UnitServer* UnitStateArg(const SSCCmd* cmd) {
    CUnit* unit = cmd->objectArgs[0]->AsUnit();
    return unit ? unit->ServerController() : nullptr;
}

Lever* LeverArg(const SSCCmd* cmd) {
    EngineObject* object = Arg(cmd, 0);
    return object && object->GetClassId() == ClassId::Lever ? static_cast<Lever*>(object) : nullptr;
}

ScriptParticleSource* FindParticle(Script* script, int id) {
    for (ScriptParticleSource& source : script->particles)
        if (source.id == id) return &source;
    return nullptr;
}

// For the quest objective commands that take no text.
void QuestObjectiveNoText(int id, float value) { ScriptQuestAddObjective(id, game::kEmptyText, value); }

// Runs a For body in the same ScriptRun (the variables are shared).
void RunBody(ScriptRun* run, Script* script, ScriptDecl* body) {
    ScriptDecl* block = run->block;
    int index = run->index;
    int ticks = run->ticks;
    int sleepStart = run->sleepStart;
    run->block = body;
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    run->Execute(script);
    run->block = block;
    run->index = index;
    run->ticks = ticks;
    run->sleepStart = sleepStart;
}

void SetLoopCounter(const SSCCmd* cmd, int i) { *cmd->objectArgs[0] = (*cmd->groupArgs[0])[static_cast<std::size_t>(i)]; }

int& ForDepth(Script* script) { return script->forDepth; }

void SetCP(const SSCCmd* cmd, bool fast) {
    CMapObject* object = ObjectSlots(Arg(cmd, 0));   // a unit or another map object
    if (!object) return;
    const float* f = cmd->floatArgs.data();
    AIMap* map = &g_serverWorld.aiMap;
    if (fast) {
        map->SetRefreshUnits(0);
        Vector3 point = {f[0], f[1], f[2]};
        map->MoveInSectors(object, point);
        if (CUnit* unit = UnitOrNull(object)) unit->ServerController()->ResyncWithUnit();
        map->SetRefreshUnits(1);
        object->MarkChanged(2);
        return;
    }
    float oldZ = Position(object)->z;
    Vector3 point = {f[0], f[1], f[2]};
    map->MoveInSectors(object, point);
    if (CUnit* unit = UnitOrNull(object)) {
        unit->ServerController()->ResyncWithUnit();
        EI_NAN_AUDIT(oldZ, Position(object)->z);
        if (oldZ != Position(object)->z) object->MarkChanged(2);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    float radius = object->bounds.radius + object->bounds.radius;
    std::list<CUnit*> units = map->FindUnitsInRadius(*Position(object), radius);
    for (auto pos = units.begin(); pos != units.end();) {
        CUnit* unit = *pos;
        ++pos;
        map->RemoveFromSectors(unit);
    }
    for (auto pos = units.begin(); pos != units.end();) {
        Vector3 free;
        CUnit* unit = *pos;
        ++pos;
        if (!map->CanLeaveCell(unit)) {
            free = *Position(unit);
            map->FindFreeCellNear(unit, *Position(unit), &free);
            *Position(unit) = free;
            unit->ServerController()->ResyncWithUnit();
        }
        map->AddToSectors(unit);
    }
    object->MarkChanged(2);
}

void GiveSkill(const SSCCmd* cmd) {
    UnitServer* state = cmd->objectArgs[0]->GetUnitPerception();
    if (!state) return;
    // The skill names in the order they are tested; science goes to
    // SetScience, the others to SetSkill with their index.
    static const char* const kNames[] = {"science", "melee", "archery", "backstab", "elemental",
                                         "sense", "astral", "stealth", "awareness"};
    static const int kIndex[] = {0, 0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 9; ++i) {
        if (text::CompareIgnoringCase(Text(cmd, 0), kNames[i]) != 0) continue;
        CUnit* owner = state->unit;
        int value = Ftol(cmd->floatArgs[0]);
        UnitStats* stats = StatsOf(state);
        if (i == 0) stats->RaiseAttribute(kIndex[i], value, owner);
        else stats->RaiseSkill(kIndex[i], value, owner);
    }
}

void RedeployParty(const SSCCmd* cmd) {
    CPlayer* player = g_serverWorld.GetPlayer(Ftol(cmd->floatArgs[0]));
    if (!player) return;
    PlayerServer* state = player->Server();
    std::vector<UnitPlacement> slots;
    g_serverWorld.CollectParty(player, &slots);
    CParty* party = player->Party();
    g_partyManager.BeginRedeploy(party);
    g_partyManager.EndRedeploy(party);
    // One slot and one party unit: the slot gets the unit's id.
    const std::vector<PartyUnitRecord>& members = player->Party()->members;
    if (slots.size() == 1 && std::ssize(members) == 1) slots[0].id = static_cast<int>(members[0].unitId);
    state->SendParty();
    player->Server()->lastPosition.z = -1313.0f;
    g_serverWorld.Redeploy(player, &slots, 0);
}

void CopyLoot(const SSCCmd* cmd, bool replace) {
    if (!g_serverWorld.GetPlayer(Ftol(cmd->floatArgs[0]))) return;
    CParty* from = g_partyManager.records.Find(Text(cmd, 0));
    CParty* to = g_partyManager.records.Find(Text(cmd, 1));
    if (!from || !to) return;
    if (replace) {
        while (!to->items.empty()) to->RemoveItem(to->items[0], 1);
    }
    for (int i = 0; i < std::ssize(from->items); ++i)
        to->AddItem(from->items[i], 1, 1);
    if (replace) to->money = from->money;
    else to->money += from->money;
}

void GSSetVarMultiplayer(const SSCCmd* cmd) {
    std::string text(Text(cmd, 0));
    net::g_gameServer->SetVariable(&text, cmd->floatArgs[1]);
}

void GSVarMultiplayer(const SSCCmd* cmd, bool setMax) {
    std::string text(Text(cmd, 0));
    if (setMax) net::g_gameServer->RaiseVariable(&text, cmd->floatArgs[1]);
    else net::g_gameServer->RemoveVariable(&text);
}

void PlayMovie(const SSCCmd* cmd) {
    std::string path = g_discFolder + MoviesFolder();
    std::string file = path + cmd->stringArgs[0];
    ::PlayMovie(file, 0, true, true, true, true);
}

void CastSpell(const SSCCmd* cmd, bool atUnit) {
    CSpell* spell = g_serverWorld.spellManager->CreateSpellFromText(Text(cmd, 0));
    if (spell) spell->AddRef();
    if (spell) {
        const float* f = cmd->floatArgs.data();
        Vector3 from = {f[0], f[1], 0.0f};
        if (atUnit) {
            CMapObject* target = ObjectSlots(Arg(cmd, 0));
            if (target) g_serverWorld.CastSpell(spell, 0, from, target, target->localPosition, 0);
        } else {
            Vector3 to = {f[2], f[3], 0.0f};
            g_serverWorld.CastSpell(spell, 0, from, nullptr, to, 0);
        }
    }
    if (spell) spell->Release();
}

void UnitModes(const SSCCmd* cmd) {
    // UMAg: aggression set 0..3 replaces modes 3, 4, 5 and 7.
    UnitServer* state = UnitStateArg(cmd);
    if (!state) return;
    AIBrain* brain = BrainOf(state);
    brain->RemoveModesOfType(AIModeType::Suspection);
    brain->RemoveModesOfType(AIModeType::Aggression);
    brain->RemoveModesOfType(AIModeType::CorpseWatcher);
    brain->RemoveModesOfType(AIModeType::Fear);
    switch (Round(cmd->floatArgs[0])) {
    case 0:
        brain->AddMode(std::make_unique<FearMode>(true));
        brain->AddMode(std::make_unique<SuspectionMode>());
        brain->AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    case 1:
        brain->AddMode(std::make_unique<FearMode>(false));
        brain->AddMode(std::make_unique<SuspectionMode>());
        brain->AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    case 2:
        brain->AddMode(std::make_unique<AggressionMode>(true));
        brain->AddMode(std::make_unique<FearMode>(false));
        brain->AddMode(std::make_unique<SuspectionMode>());
        brain->AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    case 3:
        brain->AddMode(std::make_unique<AggressionMode>());
        brain->AddMode(std::make_unique<FearMode>(false));
        brain->AddMode(std::make_unique<SuspectionMode>());
        brain->AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    default:
        break;
    }
}

} // namespace

bool ScriptRun::Execute(Script* script) {
    ++ticks;
    if (index >= std::ssize(block->commands)) return false;
    vars.RemoveDeletedObjects();
    int cont = 1;
    while (index < std::ssize(block->commands)) {
        if (!cont) return true;
        SSCCmd* cmd = &block->commands[index];
        cmd->SetParameters(cmd->localBindings, &vars);
        cmd->SetParameters(cmd->globalBindings, &script->globals);
        for (int i = 0; i < std::ssize(cmd->subCalls); ++i)
            cmd->CalcParameters(script, &vars, &cmd->subCalls[i]);
        cont = 1;
        bool done = false;
        if (scriptaddon::Enabled()) {
            scriptaddon::Call call{cmd, script, nullptr, &cont, this};
            done = scriptaddon::Dispatch(&call);
        }

        const float* f = cmd->floatArgs.data();
        const ScriptFunction function = done ? ScriptFunction::None : static_cast<ScriptFunction>(cmd->id);
        switch (function) {
        case ScriptFunction::Sleep: {   // Sleep(ticks): counts this run's Execute calls from the first one
            if (sleepStart < 0) sleepStart = ticks;
            int n = Round(f[0]);
            if (ticks - sleepStart < n) cont = 0;
            else sleepStart = -1;
            break;
        }
        case ScriptFunction::WaitSegment:     // WaitSegment(ticks): until this run has been executed that often
            if (ticks < Round(f[0])) cont = 0;
            break;
        case ScriptFunction::SleepUntil:     // SleepUntil(condition)
            if (IsZero(f[0])) cont = 0;
            break;
        case ScriptFunction::SleepUntilIdle: {   // SleepUntilIdle(unit): while the brain has an action
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (unit && unit->ServerController()->brain.action != 0) cont = 0;
            break;
        }
        case ScriptFunction::CallScript: {   // a declared script: start an instance with the call's arguments
            int which = Ftol(f[0]);
            ScriptDefinition* instance = &script->instances.emplace_front(script->definitions[static_cast<std::size_t>(which)]);
            instance->BindCall(cmd);
            instance->index = 0;   // the killed flag
            break;
        }
        case ScriptFunction::KillScript:
            if (!killFlag) game::FatalError(kKillScriptNotAllowed);
            *killFlag = 1;
            break;
        case ScriptFunction::For: {   // For(counter, group)(body): the group's members from last to first
            ++ForDepth(script);
            for (int i = static_cast<int>(cmd->groupArgs[0]->size()) - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case ScriptFunction::ForIf: {   // ForIf(counter, group, condition)(body)
            ++ForDepth(script);
            const ScriptSubCall* condition = nullptr;
            for (int i = 0; i < std::ssize(cmd->subCalls); ++i) {
                if (cmd->subCalls[i].type == kScriptFloat) {
                    condition = &cmd->subCalls[i];
                    break;
                }
            }
            for (int i = static_cast<int>(cmd->groupArgs[0]->size()) - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                if (condition) cmd->CalcParameters(script, &vars, condition);
                if (!IsZero(cmd->floatArgs[0])) RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case ScriptFunction::Assign: {   // global = value: float 0 variable index, float 1 type - 1
            ScriptVars* globals = &script->globals;
            switch (Ftol(f[1])) {
            case 0:
                x87::CopyBits(globals->floats.values[Ftol(f[0])], f[2]);
                break;
            case 1:
                globals->strings.values[Ftol(f[0])] = cmd->stringArgs[0];
                break;
            case 2: {
                globals->objects.values[static_cast<std::size_t>(Ftol(f[0]))] = *cmd->objectArgs[0];
                break;
            }
            case 3: {
                globals->groups.values[static_cast<std::size_t>(Ftol(f[0]))] = *cmd->groupArgs[0];
                break;
            }
            default:
                break;
            }
            break;
        }

        case ScriptFunction::AddObject:   // AddObject(group, object)
            cmd->groupArgs[0]->Insert(Arg(cmd, 0));
            break;
        case ScriptFunction::RemoveObject: {   // RemoveObject(group, object)
            ScriptGroup* group = cmd->groupArgs[0];
            int at = group->IndexOf(*cmd->objectArgs[0]);
            if (at >= 0) group->erase(group->begin() + at);
            break;
        }

        case ScriptFunction::CreatePointLight: script->CreatePointLight(cmd); break;
        case ScriptFunction::MovePointLight: script->MovePointLight(cmd); break;
        case ScriptFunction::DeletePointLight: script->DeletePointLight(cmd); break;
        case ScriptFunction::CreateParticleSource: script->ScriptCreateParticleSource(cmd); break;
        case ScriptFunction::MoveParticleSource: script->ScriptMoveParticleSource(cmd); break;
        case ScriptFunction::CreateLightning: script->ScriptCreateLightning(cmd); break;
        case ScriptFunction::CreateFX: script->ScriptCreateFx(cmd); break;
        case ScriptFunction::CreateFXSource:
        case ScriptFunction::CreateRandomizedFXSource:
            script->ScriptCreateFxSource(cmd);
            break;
        case ScriptFunction::DeleteParticleSource: {   // DeleteParticleSource(id)
            int id = Round(f[0]);
            auto& list = script->particles;
            auto at = std::find_if(list.begin(), list.end(), [id](const ScriptParticleSource& source) { return source.id == id; });
            if (at != list.end()) list.erase(at);
            break;
        }
        case ScriptFunction::AttachParticleSource: {   // AttachParticleSource(id, objectId)
            int objectId = Round(f[1]);
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetAttachedTo(objectId);
            break;
        }
        case ScriptFunction::AttachParticles: {   // AttachParticles(id, object)
            EngineObject* object = Arg(cmd, 0);
            if (!object) break;
            int objectId = object->id;
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetAttachedTo(objectId);
            break;
        }
        case ScriptFunction::SetParticleSourceSize: {   // SetParticleSourceSize(id, size)
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetSize(f[1]);
            break;
        }
        case ScriptFunction::DeleteLightning: {   // DeleteLightning(id): every lightning with the id
            std::erase_if(script->lightning, [f](const ScriptLightning& bolt) { return bolt.id == Ftol(f[0]); });
            break;
        }
        case ScriptFunction::DeleteFXSource: {   // DeleteFXSource(id): the first one with the id
            int id = Round(f[0]);
            auto& list = script->fxSources;
            auto at = std::find_if(list.begin(), list.end(), [id](const ScriptFxSource& source) { return source.id == id; });
            if (at != list.end()) list.erase(at);
            break;
        }

        case ScriptFunction::SendEvent:   // SendEvent(type, value)
            g_serverWorld.players->SendValueToAll(Ftol(f[1]));
            break;
        case ScriptFunction::SendStringEvent:   // SendStringEvent(type, text)
            g_serverWorld.players->SendTextToAll(Text(cmd, 0));
            break;
        case ScriptFunction::GodMode: {   // GodMode(player, on)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (player) player->godMode = static_cast<std::uint8_t>(Ftol(f[1]));
            break;
        }
        case ScriptFunction::SetDiplomacy: {   // SetDiplomacy(a, b, value): > 0 allied (0), < 0 enemies (2), else (0 or NaN) neutral (1)
            int relation;
            if (f[2] > 0.0f) relation = 0;
            else relation = f[2] < 0.0f ? 2 : 1;
            g_serverWorld.diplomacy.SetRelation(Ftol(f[0]), Ftol(f[1]), relation);
            break;
        }
        case ScriptFunction::InvokeAlarm: {   // InvokeAlarm(type, x, y)
            Vector3 point = {f[1], f[2], 0.0f};
            g_serverWorld.InvokeAlarm(Ftol(f[0]), &point.x, 1);
            break;
        }
        case ScriptFunction::AddUnitToServer: {   // AddUnitToServer(name, prototype, ..., x, y, z)
            Vector3 point = {f[0], f[1], f[2]};
            g_serverWorld.AddUnitToServer(static_cast<unsigned>(TextKeyFromName(Text(cmd, 0))), Text(cmd, 1), Text(cmd, 2), point);
            break;
        }
        case ScriptFunction::RemoveUnitFromServer:     // RemoveUnitFromServer(object)
        case ScriptFunction::RemoveObjectFromServer: {   // RemoveObjectFromServer(object)
            EngineObject* object = Arg(cmd, 0);
            if (object) g_serverWorld.RemoveObject(object->id);    // Rebirth patches it
            break;
        }
        case ScriptFunction::RecalcMercBriefings:
            g_serverWorld.UpdateMercBriefings();
            break;
        case ScriptFunction::SetWind: {   // SetWind(x, y, z, strength)
            Vector3 direction = {f[0], f[1], f[2]};
            g_serverWorld.ChangeWind(direction, f[3]);
            break;
        }
        case ScriptFunction::SetBackGroundColor: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255
            const float scale = kColourScale;
            float b = f[2] * scale;
            float g = f[1] * scale;
            float r = f[0] * scale;
            g_sceneRenderer.SetFogColour(r, g, b);
            break;
        }
        case ScriptFunction::RunWorldTime:   // RunWorldTime(time): the configured speed (int)
            g_serverWorld.SetTimeSpeed(static_cast<float>(options::g_options.defaultTimeScale));
            g_serverWorld.SetTime(f[0]);
            break;
        case ScriptFunction::FixWorldTime:   // FixWorldTime(time)
            g_serverWorld.SetTimeSpeed(0.0f);
            g_serverWorld.SetTime(f[0]);
            break;
        case ScriptFunction::SetWaterLevel:   // SetWaterLevel(a, level, b)
            g_serverWorld.SetWaterLevel(Round(f[0]), f[1], Round(f[2]));
            break;
        case ScriptFunction::AddMob:   // AddMob(name)
            g_serverWorld.LoadMobFile(Text(cmd, 0));
            break;
        case ScriptFunction::PlayCamera:   // PlayCamera(name)
            GamePlayCameraPath(Text(cmd, 0), 0);
            break;
        case ScriptFunction::PlayMovie:   // PlayMovie(name)
            PlayMovie(cmd);
            break;
        case ScriptFunction::ConsoleFloat:   // ConsoleFloat(value)
            console::EndLine(console::g_consoleOut << f[0]);
            break;
        case ScriptFunction::ConsoleString:   // ConsoleString(text)
            console::PrintLine(Text(cmd, 0));
            break;
        case ScriptFunction::AddRectToArea: {   // AddRectToArea(area, x1, y1, x2, y2)
            script->FindArea(Round(f[0]))->AddRect(f[1], f[2], f[3], f[4]);
            break;
        }
        case ScriptFunction::AddRoundToArea: {   // AddRoundToArea(area, x, y, radius)
            script->FindArea(Round(f[0]))->AddRound(f[1], f[2], f[3]);
            break;
        }
        case ScriptFunction::DeleteArea:   // DeleteArea(area)
            script->DeleteArea(Round(f[0]));
            break;
        case ScriptFunction::HideObject: {   // HideObject(object, hide): MapObjectFlag::Invisible
            EngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (IsZero(f[0])) object->flags &= ~MapObjectFlag::Invisible;
            else object->flags |= MapObjectFlag::Invisible;
            break;
        }
        case ScriptFunction::ActivateTrap: {   // ActivateTrap(object, on): MapObjectFlag::Armed
            EngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (IsZero(f[0])) object->flags &= ~MapObjectFlag::Armed;
            else object->flags |= MapObjectFlag::Armed;
            break;
        }
        case ScriptFunction::SetCP:   // SetCP(object, x, y, z)
            SetCP(cmd, false);
            break;
        case ScriptFunction::SetCPFast:   // SetCPFast(object, x, y, z)
            SetCP(cmd, true);
            break;

        case ScriptFunction::SwitchLeverState:     // SwitchLeverState(lever, state)
        case ScriptFunction::SwitchLeverStateEx: {   // SwitchLeverStateEx(lever, state, time)
            Lever* lever = LeverArg(cmd);
            if (!lever) break;
            float time;
            if (function == ScriptFunction::SwitchLeverState) time = -10.0f;
            else time = f[1];
            lever->SetState(Ftol(f[0]), time);
            break;
        }
        case ScriptFunction::EnableLever: {   // EnableLever(lever, on)
            Lever* lever = LeverArg(cmd);
            if (!lever) break;
            lever->Enable(!IsZero(f[0]));
            break;
        }
        case ScriptFunction::SetScience: {   // SetScience(lever, a, b, four flags)
            Lever* lever = LeverArg(cmd);
            if (!lever) break;
            LockKey lock = {1, 0, 0};
            lock.id = static_cast<std::uint32_t>(Ftol(f[0]));
            lock.level = static_cast<std::uint32_t>(Ftol(f[1]));
            lock.mask = 0;
            if (!IsZero(f[2])) lock.mask = 1;
            if (!IsZero(f[3])) lock.mask |= 2;
            if (!IsZero(f[4])) lock.mask |= 4;
            if (!IsZero(f[5])) lock.mask |= 8;
            lever->SetLock(&lock);
            break;
        }

        case ScriptFunction::BlockUnit: {   // BlockUnit(unit, on): MapObjectFlag::Blocked
            EngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != ClassId::Unit) break;
            if (IsZero(f[0])) unit->flags &= ~MapObjectFlag::Blocked;
            else unit->flags |= MapObjectFlag::Blocked;
            break;
        }
        case ScriptFunction::SetPlayer: {   // SetPlayer(unit, player)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            unit->SetHostileMask(g_serverWorld.diplomacy.GetHostileMask(Ftol(f[0])));
            unit->SetSide(Ftol(f[0]));
            break;
        }
        case ScriptFunction::SetEnemy: {   // SetEnemy(unit, player): adds the player to the hostile mask
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            int player = Ftol(f[0]);
            std::uint32_t mask = (1u << (player & 31)) | unit->relationsA;
            unit->AddHostileMask(mask);
            break;
        }
        case ScriptFunction::KillUnit: {   // KillUnit(object)
            EngineObject* object = Arg(cmd, 0);
            DamageInfo damage;
            ResetDamageInfo(&damage);
            if (object) ObjectSlots(object)->ReceiveHit(&damage, nullptr, nullptr);
            break;
        }
        case ScriptFunction::InflictDamage: {   // InflictDamage(unit, amount)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            DamageInfo damage = {};
            damage.typeWeights[5] = 1.0f;
            damage.weaponValue = 0;
            damage.attackRoll = 100.0f;
            damage.amount = f[0];
            damage.multiplier = 1.0f;
            damage.location = 1;
            // TakeHit(hit, attacker, source): three arguments.
            state->TakeHit(&damage, nullptr, nullptr);
            break;
        }
        case ScriptFunction::AddUnitUnderControl: {   // AddUnitUnderControl(player, unit)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (player && unit) unit->SetPlayer(player);
            break;
        }
        case ScriptFunction::RemoveUnitFromControl: {   // RemoveUnitFromControl(player, unit)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (unit) unit->SetPlayer(nullptr);
            break;
        }
        case ScriptFunction::SetSpellAggression:   // SetSpellAggression(unit, value): does nothing
            break;
        case ScriptFunction::RemoveQuestItem: {   // RemoveQuestItem(unit, id): the first quest item with the id
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            int count = std::ssize(unit->carried);
            int id = Ftol(f[0]);
            for (int i = 0; i < count; ++i) {
                ItemQuest* item = AsQuest(unit->carried[i]);
                if (static_cast<int>(item->questItemId) == id) {
                    unit->RemoveCarried(i);
                    break;
                }
            }
            break;
        }
        case ScriptFunction::GiveUnitQuestItem: {   // GiveUnitQuestItem(unit, item)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            Item* item = g_serverWorld.CreateItemNamed(Text(cmd, 0));
            if (item && item->GetClassId() == ClassId::ItemQuest) unit->AddCarried(item);
            break;
        }
        case ScriptFunction::GiveUnitSpell: {   // GiveUnitSpell(unit, spell)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            Item* spell = g_serverWorld.CreateSpellNamed(Text(cmd, 0));
            if (!spell) break;
            unit->AddSpell(spell);
            UnitServer* state = unit->ServerController();
            state->RefreshEquipment();
            break;
        }
        case ScriptFunction::GiveSkill:   // GiveSkill(unit, skill, value)
            GiveSkill(cmd);
            break;
        case ScriptFunction::GiveStrength:     // GiveStrength(unit, value)
        case ScriptFunction::GiveDexterity:
        case ScriptFunction::GiveIntelligence: {
            UnitServer* state = cmd->objectArgs[0]->GetUnitPerception();
            if (!state) break;
            CUnit* owner = state->unit;
            int value = Ftol(f[0]);
            UnitStats* stats = StatsOf(state);
            if (function == ScriptFunction::GiveStrength) stats->RaiseStrength(value, owner);
            else if (function == ScriptFunction::GiveDexterity) stats->RaiseDexterity(value, owner);
            else stats->RaiseIntelligence(value, owner);
            break;
        }

        case ScriptFunction::MoveToPoint: {   // MoveToPoint(unit, x, y)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Walk(point, 0);
            break;
        }
        case ScriptFunction::RotateTo: {   // RotateTo(unit, x, y)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            const Vector3* at = Position(state->unit);
            Vector3 direction = {f[0] - at->x, f[1] - at->y, -at->z};
            BrainOf(state)->TurnTowards(direction, 0);
            break;
        }
        case ScriptFunction::MoveToObject: {   // MoveToObject(unit, target)
            UnitServer* state = UnitStateArg(cmd);
            EngineObject* target = Arg(cmd, 1);
            if (!state || !target) break;
            Vector3 point = {Position(target)->x, Position(target)->y, 0.0f};
            BrainOf(state)->Walk(point, 0);
            break;
        }
        case ScriptFunction::Follow: {   // Follow(unit, leader)
            UnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->Follow(ObjectSlots(Arg(cmd, 1)));
            break;
        }
        case ScriptFunction::Attack: {   // Attack(unit, target)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            CMapObject* target = ObjectSlots(Arg(cmd, 1));
            if (target) BrainOf(state)->AttackTarget(target);
            break;
        }
        case ScriptFunction::ResetTarget: {   // ResetTarget(unit)
            UnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ResetBehaviour();
            break;
        }
        case ScriptFunction::Cast: {   // Cast(unit, x, y): at the ground height there
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            float x = f[0];
            float y = f[1];
            float z = g_serverWorld.GridHeight(x, y);
            Vector3 point = {x, y, z};
            BrainOf(state)->CastAt(0, point, 0);
            break;
        }
        case ScriptFunction::CastSpellPoint:   // CastSpellPoint(spell, x, y, x2, y2)
            CastSpell(cmd, false);
            break;
        case ScriptFunction::CastSpellUnit:   // CastSpellUnit(spell, x, y, target)
            CastSpell(cmd, true);
            break;
        case ScriptFunction::Idle: {   // Idle(unit)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->ClearModes();
            BrainOf(state)->StopAction();
            break;
        }
        case ScriptFunction::Guard: {   // Guard(unit, x, y, radius)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardGuard(point, f[2]);
            break;
        }
        case ScriptFunction::Sentry: {   // Sentry(unit, x, y)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardSentry(point);
            break;
        }
        case ScriptFunction::PlayAnimation: {   // PlayAnimation(unit, name)
            UnitServer* state = UnitStateArg(cmd);
            if (state) state->PlayAnimation(Text(cmd, 0));
            break;
        }
        case ScriptFunction::Lie:
        case ScriptFunction::Crawl:
        case ScriptFunction::Stand:
        case ScriptFunction::Walk:
        case ScriptFunction::Run: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            int style = function == ScriptFunction::Lie ? 0 : function == ScriptFunction::Crawl ? 1 : function == ScriptFunction::Run ? 3 : 2;
            state->SetMoveStyle(style);   // 0 lie, 1 crawl, 2 walk, 3 run
            break;
        }
        case ScriptFunction::Rest: {   // Rest(unit)
            UnitServer* state = UnitStateArg(cmd);
            if (state) state->StopQueued();
            break;
        }

        case ScriptFunction::UMClear: {
            UnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ClearModes();
            break;
        }
        case ScriptFunction::UMSentry: {   // UMSentry(unit, x, y)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->AddMode(std::make_unique<SentryMode>(point));
            break;
        }
        case ScriptFunction::UMGuard:     // UMGuard(unit, x, y, radius)
        case ScriptFunction::UMGuardEx: {   // UMGuardEx(unit, x, y, radius, radius2)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            float radius = f[2];
            float radius2;
            if (function == ScriptFunction::UMGuard) radius2 = -1.0f;
            else radius2 = f[3];
            BrainOf(state)->AddMode(std::make_unique<GuardMode>(point, radius, radius2));
            break;
        }
        case ScriptFunction::UMFollow: {   // UMFollow(unit, leader)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            CMapObject* leader = ObjectSlots(Arg(cmd, 1));
            BrainOf(state)->AddMode(std::make_unique<FollowMode>(leader));
            break;
        }
        case ScriptFunction::UMPatrol: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(std::make_unique<PatrolMode>());
            break;
        }
        case ScriptFunction::UMPatrolClear: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = BrainOf(state)->FindPatrolMode();
            if (patrol) patrol->points.clear();
            break;
        }
        case ScriptFunction::UMPatrolAddPoint: {   // UMPatrolAddPoint(unit, x, y)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            Vector3 point = {f[0], f[1], 0.0f};
            patrol->AppendPoint(point);
            break;
        }
        case ScriptFunction::UMPatrolAddPointLook: {   // UMPatrolAddPointLook(unit, x, y, z): a look at the last point
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            PatrolPoint* last = patrol->LastPoint();
            if (!last) break;
            // The arguments are the angle, the wait and the turning time.
            last->looks.push_back(AILook{f[0], f[2], f[1], false});
            break;
        }
        case ScriptFunction::UMPlayer: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            brain->ClearModes();
            brain->AddMode(std::make_unique<PlayerMode>());
            brain->StopAction();
            break;
        }
        case ScriptFunction::UMAg:   // UMAg(unit, level)
            UnitModes(cmd);
            break;
        case ScriptFunction::UMStandard: {   // UMStandard: aggression, suspicion, corpse watcher
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            brain->AddMode(std::make_unique<AggressionMode>());
            brain->AddMode(std::make_unique<SuspectionMode>());
            brain->AddMode(std::make_unique<CorpseWatcherMode>());
            break;
        }
        case ScriptFunction::UMAggression: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(std::make_unique<AggressionMode>());
            break;
        }
        case ScriptFunction::UMRevenge: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(std::make_unique<AggressionMode>(true));
            break;
        }
        case ScriptFunction::UMFear: {   // UMFear(unit, on)
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            int on = IsZero(f[0]) ? 0 : 1;
            BrainOf(state)->AddMode(std::make_unique<FearMode>(on != 0));
            break;
        }
        case ScriptFunction::UMSuspection: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(std::make_unique<SuspectionMode>());
            break;
        }
        case ScriptFunction::UMCorpseWatcher: {
            UnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(std::make_unique<CorpseWatcherMode>());
            break;
        }

        case ScriptFunction::GSSetVar: {   // GSSetVar(player, name, value)
            if (IsMultiplayerClient() && ServerActive()) {
                GSSetVarMultiplayer(cmd);
                break;
            }
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (player) player->Server()->SetVariable(Text(cmd, 0), std::bit_cast<std::uint32_t>(f[1]));   // the float bits
            break;
        }
        case ScriptFunction::GSSetVarMax:     // GSSetVarMax(player, name, value)
        case ScriptFunction::GSDelVar: {   // GSDelVar(player, name)
            bool setMax = function == ScriptFunction::GSSetVarMax;
            if (IsMultiplayerClient() && ServerActive()) {
                GSVarMultiplayer(cmd, setMax);
                break;
            }
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            if (setMax) player->Server()->SetVariableMax(Text(cmd, 0), f[1]);
            else player->Server()->DeleteVariable(Text(cmd, 0));
            break;
        }
        case ScriptFunction::PlayMusic: {   // PlayMusic(player, name)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (player) player->Server()->SendText0D(Text(cmd, 0));
            break;
        }
        case ScriptFunction::LeaveToZone: {   // LeaveToZone(player, zone, point)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (player) player->Server()->LeaveToZone(Text(cmd, 0), Ftol(f[1]));
            break;
        }
        case ScriptFunction::RedeployParty:   // RedeployParty(player)
            RedeployParty(cmd);
            break;
        case ScriptFunction::AddUnitToParty:     // AddUnitToParty(player, party.unit, prototype)
        case ScriptFunction::RemoveUnitFromParty: {   // RemoveUnitFromParty(player, party.unit)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::string name = cmd->stringArgs[0];
            CParty* records = PartyForScript(player, &name);
            if (records) {
                if (function == ScriptFunction::AddUnitToParty)
                    records->AddMember(name, Text(cmd, 1), game::kEmptyText);
                else
                    records->RemoveMember(static_cast<unsigned>(TextKeyFromName(name)));
            }
            break;
        }
        case ScriptFunction::CopyItems:     // CopyItems(player, from, to)
        case ScriptFunction::CopyStats: {   // CopyStats(player, from, to)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::string from = cmd->stringArgs[0];
            std::string to = cmd->stringArgs[1];
            CParty* fromRecords = PartyForScript(player, &from);
            CParty* toRecords = PartyForScript(player, &to);
            PartyUnitRecord* source = fromRecords->Find(TextKeyFromName(from));
            PartyUnitRecord* target = toRecords->Find(TextKeyFromName(to));
            if (source && target) {
                if (function == ScriptFunction::CopyItems) {
                    target->CopyItemsFrom(source, toRecords);
                } else {
                    std::memcpy(&target->stats, &source->stats, sizeof(target->stats));
                    target->texts[0] = source->texts[0];
                }
            }
            break;
        }
        case ScriptFunction::CreateParty: {   // CreateParty(player, name)
            if (g_serverWorld.GetPlayer(Ftol(f[0]))) g_partyManager.records.Add(Text(cmd, 0));
            break;
        }
        case ScriptFunction::RemoveParty: {   // RemoveParty(player, name)
            if (g_serverWorld.GetPlayer(Ftol(f[0]))) g_partyManager.records.Remove(Text(cmd, 0));
            break;
        }
        case ScriptFunction::SetCurrentParty: {   // SetCurrentParty(player, name)
            if (g_serverWorld.GetPlayer(Ftol(f[0])))
                g_partyManager.records.requestedName = cmd->stringArgs[0];
            break;
        }
        case ScriptFunction::AddLoot:   // AddLoot(player, from, to)
            CopyLoot(cmd, false);
            break;
        case ScriptFunction::CopyLoot:   // CopyLoot(player, from, to)
            CopyLoot(cmd, true);
            break;
        case ScriptFunction::GiveMoney: {   // GiveMoney(player, amount)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::list<Item*> items;
            player->Party()->money += Ftol(f[1]);
            player->GiveItems(&items, 0);
            break;
        }
        case ScriptFunction::GiveQuestItem:     // GiveQuestItem(player, item)
        case ScriptFunction::GiveItem: {   // GiveItem(player, item)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::list<Item*> items;
            Item* item = g_serverWorld.CreateItemNamed(Text(cmd, 0));
            if (item) {
                items.push_back(item);
                player->GiveItems(&items, 0);
            }
            break;
        }
        case ScriptFunction::EraseQuestItem: {   // EraseQuestItem(player, id)
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (player) player->TakeLoot(Ftol(f[1]));
            break;
        }
        case ScriptFunction::FixItems: {   // FixItems: every item repaired
            auto* list = g_serverWorld.itemManager;
            ObjectListIteratorOf<Item> it;
            it.manager = nullptr;
            for (Item* item = it.First(list); item; item = it.Next()) item->Repair();
            if (it.manager) {   // destructor
                --it.manager->refCount;
                it.manager = nullptr;
            }
            break;
        }

        case ScriptFunction::QuestComplete: {   // QuestComplete(player, quest)
            if (IsMultiplayerClient()) {
                if (FinishQuestMap(Text(cmd, 0)) != 0) {
                    QuestRecord* quest = g_questDatabase.FindQuest(Text(cmd, 0));
                    if (quest) g_serverWorld.AwardExperience(nullptr, quest->exp);
                }
                break;
            }
            CPlayer* player = g_serverWorld.GetPlayer(Ftol(f[0]));
            if (!player) break;
            QuestRecord* quest = g_questDatabase.FindQuest(Text(cmd, 0));
            if (!quest) break;
            g_serverWorld.AwardExperience(player, quest->exp);
            ClassTemplatesQuestCompleted(&g_partyManager);
            break;
        }
        case ScriptFunction::QStart:   // QStart(name)
            ScriptQuestStart(Text(cmd, 0));
            break;
        case ScriptFunction::QFinish:
            ScriptQuestFinish(script);
            break;
        case ScriptFunction::QObjArea:   // QObjArea(area)
        case ScriptFunction::QObjGetItem:   // QObjGetItem(item)
            QuestObjectiveNoText(cmd->id, f[0]);
            break;
        case ScriptFunction::QObjSeeObject:   // QObjSeeObject(name)
        case ScriptFunction::QObjSeeUnit:
        case ScriptFunction::QObjKillUnit:
        case ScriptFunction::QObjKillGroup:
            ScriptQuestAddObjective(cmd->id, cmd->stringArgs[0], 0.0f);
            break;
        case ScriptFunction::QObjUse:   // QObjUse(name, value)
            ScriptQuestAddObjective(cmd->id, cmd->stringArgs[0], f[0]);
            break;

        default:   // functions, sections and commands without effect here
            break;
        }

        ScriptFreeTemporaries(script);
        if (cont) ++index;
    }
    return true;
}
