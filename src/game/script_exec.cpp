// ScriptRun::Execute, the switch over every world script command. See docs/notes/script.md.
// Rebirth patches Execute (it adds commands), CalcParameters and FatalError.
#include "ai_map.h"
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
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

CUnitServer* UnitState(CUnit* unit) { return unit->ServerController(); }
AIBrain* BrainOf(CUnitServer* state) { return &state->brain; }
PatrolMode* PatrolOf(CUnitServer* state) { return BrainOf(state)->FindPatrolMode(); }
UnitStats* StatsOf(CUnitServer* state) { return &state->stats; }
Vector3* Position(CEngineObject* object) { return &ObjectSlots(object)->localPosition_; }

CEngineObject* Arg(const SSCCmd* cmd, int i) { return cmd->objectArgs[i]->object; }
CMapObject* MapObjectArg(const SSCCmd* cmd, int i) { return ObjectSlots(Arg(cmd, i)); }
const char* Text(const SSCCmd* cmd, int i) { return cmd->stringArgs[i].c_str(); }

CUnitServer* UnitStateArg(const SSCCmd* cmd) {
    CUnit* unit = cmd->objectArgs[0]->AsUnit();
    return unit ? UnitState(unit) : nullptr;
}

Lever* LeverArg(const SSCCmd* cmd) {
    CEngineObject* object = Arg(cmd, 0);
    return object && object->GetClassId() == kClassLever ? static_cast<Lever*>(object) : nullptr;
}

ScriptParticleSource* FindParticle(CScript* script, int id) {
    for (ScriptParticleSource& source : script->particles_)
        if (source.id == id) return &source;
    return nullptr;
}

// For the quest objective commands that take no text.
void QuestObjectiveNoText(int id, float value) { ScriptQuestAddObjective(id, game::kEmptyText, value); }

// Runs a For body in the same ScriptRun (the variables are shared).
void RunBody(ScriptRun* run, CScript* script, ScriptDecl* body) {
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

int& ForDepth(CScript* script) { return script->forDepth_; }

void SetCP(const SSCCmd* cmd, bool fast) {
    CMapObject* object = ObjectSlots(Arg(cmd, 0));   // a unit or another map object
    if (!object) return;
    const float* f = cmd->floatArgs.data();
    CAIMap* map = &g_serverWorld->aiMap_;
    if (fast) {
        map->SetRefreshUnits(0);
        Vector3 point = {f[0], f[1], f[2]};
        map->MoveInSectors(object, &point);
        if (CUnit* unit = UnitOrNull(object)) UnitState(unit)->ResyncWithUnit();
        map->SetRefreshUnits(1);
        object->MarkChanged(2);
        return;
    }
    float oldZ = Position(object)->z;
    Vector3 point = {f[0], f[1], f[2]};
    map->MoveInSectors(object, &point);
    if (CUnit* unit = UnitOrNull(object)) {
        UnitState(unit)->ResyncWithUnit();
        EI_NAN_AUDIT(oldZ, Position(object)->z);
        if (oldZ != Position(object)->z) object->MarkChanged(2);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    std::list<CUnit*> units;
    float radius = object->bounds.radius + object->bounds.radius;
    map->FindUnitsInRadius(Position(object), &units, radius);
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
            map->FindFreeCellNear(unit, Position(unit), &free);
            *Position(unit) = free;
            UnitState(unit)->ResyncWithUnit();
        }
        map->AddToSectors(unit);
    }
    object->MarkChanged(2);
}

void GiveSkill(const SSCCmd* cmd) {
    CUnitServer* state = cmd->objectArgs[0]->GetUnitPerception();
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
    CPlayer* player = g_serverWorld->GetPlayer(Ftol(cmd->floatArgs[0]));
    if (!player) return;
    CPlayerServer* state = player->Server();
    std::vector<UnitPlacement> slots;
    g_serverWorld->CollectParty(player, &slots);
    CParty* party = player->Party();
    g_partyManager->BeginRedeploy(party);
    g_partyManager->EndRedeploy(party);
    // One slot and one party unit: the slot gets the unit's id.
    const std::vector<PartyUnitRecord>& members = player->Party()->members_;
    if (slots.size() == 1 && std::ssize(members) == 1) slots[0].id = static_cast<int>(members[0].unitId);
    state->SendParty();
    player->Server()->lastPosition_.z = -1313.0f;
    g_serverWorld->Redeploy(player, &slots, 0);
}

void CopyLoot(const SSCCmd* cmd, bool replace) {
    if (!g_serverWorld->GetPlayer(Ftol(cmd->floatArgs[0]))) return;
    CParty* from = g_partyManager->records.Find(Text(cmd, 0));
    CParty* to = g_partyManager->records.Find(Text(cmd, 1));
    if (!from || !to) return;
    if (replace) {
        while (!to->items_.empty()) to->RemoveItem(to->items_[0], 1);
    }
    for (int i = 0; i < std::ssize(from->items_); ++i)
        to->AddItem(from->items_[i], 1, 1);
    if (replace) to->money_ = from->money_;
    else to->money_ += from->money_;
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
    std::string path = *g_discFolder + MoviesFolder();
    std::string file = path + cmd->stringArgs[0];
    ::PlayMovie(file, 0, 1, 1, 1, 1);
}

void CastSpell(const SSCCmd* cmd, bool atUnit) {
    CSpell* spell = g_serverWorld->spellManager_->CreateSpellFromText(Text(cmd, 0));
    if (spell) ++spell->refCount_;
    if (spell) {
        const float* f = cmd->floatArgs.data();
        Vector3 from = {f[0], f[1], 0.0f};
        if (atUnit) {
            CMapObject* target = MapObjectArg(cmd, 0);
            if (target) g_serverWorld->CastSpell(spell, 0, &from, target, &target->localPosition_, 0);
        } else {
            Vector3 to = {f[2], f[3], 0.0f};
            g_serverWorld->CastSpell(spell, 0, &from, nullptr, &to, 0);
        }
    }
    if (spell) spell->Release();
}

void UnitModes(const SSCCmd* cmd) {
    // UMAg: aggression set 0..3 replaces modes 3, 4, 5 and 7.
    CUnitServer* state = UnitStateArg(cmd);
    if (!state) return;
    AIBrain* brain = BrainOf(state);
    brain->RemoveModesOfType(3);
    brain->RemoveModesOfType(4);
    brain->RemoveModesOfType(5);
    brain->RemoveModesOfType(7);
    switch (Round(cmd->floatArgs[0])) {
    case 0:
        brain->AddMode(new FearMode(1));
        brain->AddMode(new SuspectionMode());
        brain->AddMode(new CorpseWatcherMode());
        break;
    case 1:
        brain->AddMode(new FearMode(0));
        brain->AddMode(new SuspectionMode());
        brain->AddMode(new CorpseWatcherMode());
        break;
    case 2:
        brain->AddMode(new AggressionMode(1));
        brain->AddMode(new FearMode(0));
        brain->AddMode(new SuspectionMode());
        brain->AddMode(new CorpseWatcherMode());
        break;
    case 3:
        brain->AddMode(new AggressionMode());
        brain->AddMode(new FearMode(0));
        brain->AddMode(new SuspectionMode());
        brain->AddMode(new CorpseWatcherMode());
        break;
    default:
        break;
    }
}

} // namespace

BOOL ScriptRun::Execute(CScript* script) {
    ++ticks;
    if (index >= std::ssize(block->commands)) return 0;
    vars.RemoveDeletedObjects();
    int cont = 1;
    while (index < std::ssize(block->commands)) {
        if (!cont) return 1;
        SSCCmd* cmd = &block->commands[index];
        cmd->SetParameters(cmd->localBindings, &vars);
        cmd->SetParameters(cmd->globalBindings, &script->globals_);
        for (int i = 0; i < std::ssize(cmd->subCalls); ++i)
            cmd->CalcParameters(script, &vars, &cmd->subCalls[i]);
        cont = 1;
        bool done = false;
        if (scriptaddon::Enabled()) {
            scriptaddon::Call call{cmd, script, nullptr, &cont, this};
            done = scriptaddon::Dispatch(&call);
        }

        const float* f = cmd->floatArgs.data();
        switch (done ? -1 : cmd->id) {
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
        case 0xB9: {   // SleepUntilIdle(unit): while the brain has an action
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (unit && UnitState(unit)->brain.action != 0) cont = 0;
            break;
        }
        case 0x08: {   // a declared script: start an instance with the call's arguments
            int which = Ftol(f[0]);
            ScriptDefinition* instance = &script->instances_.emplace_front(script->definitions_[static_cast<std::size_t>(which)]);
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
            for (int i = static_cast<int>(cmd->groupArgs[0]->size()) - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case kCmdForIf: {   // ForIf(counter, group, condition)(body)
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
        case kCmdAssign: {   // global = value: float 0 variable index, float 1 type - 1
            ScriptVars* globals = &script->globals_;
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

        case 0x22:   // AddObject(group, object)
            cmd->groupArgs[0]->Insert(Arg(cmd, 0));
            break;
        case 0x23: {   // RemoveObject(group, object)
            ScriptGroup* group = cmd->groupArgs[0];
            int at = group->IndexOf(*cmd->objectArgs[0]);
            if (at >= 0) group->erase(group->begin() + at);
            break;
        }

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
            auto& list = script->particles_;
            auto at = std::find_if(list.begin(), list.end(), [id](const ScriptParticleSource& source) { return source.id == id; });
            if (at != list.end()) list.erase(at);
            break;
        }
        case 0x12: {   // AttachParticleSource(id, objectId)
            int objectId = Round(f[1]);
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetAttachedTo(objectId);
            break;
        }
        case 0xE7: {   // AttachParticles(id, object)
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            int objectId = object->id_;
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetAttachedTo(objectId);
            break;
        }
        case 0xBA: {   // SetParticleSourceSize(id, size)
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) entry->object->SetSize(f[1]);
            break;
        }
        case 0x1A: {   // DeleteLightning(id): every lightning with the id
            std::erase_if(script->lightning_, [f](const ScriptLightning& bolt) { return bolt.id == Ftol(f[0]); });
            break;
        }
        case 0x19: {   // DeleteFXSource(id): the first one with the id
            int id = Round(f[0]);
            auto& list = script->fxSources_;
            auto at = std::find_if(list.begin(), list.end(), [id](const ScriptFxSource& source) { return source.id == id; });
            if (at != list.end()) list.erase(at);
            break;
        }

        case 0x39:   // SendEvent(type, value)
            g_serverWorld->players_->SendValueToAll(Ftol(f[1]));
            break;
        case 0x3A:   // SendStringEvent(type, text)
            g_serverWorld->players_->SendTextToAll(Text(cmd, 0));
            break;
        case 0x97: {   // GodMode(player, on)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (player) player->godMode_ = static_cast<std::uint8_t>(Ftol(f[1]));
            break;
        }
        case 0x37: {   // SetDiplomacy(a, b, value): > 0 allied (0), < 0 enemies (2), else (0 or NaN) neutral (1)
            int relation;
            if (f[2] > 0.0f) relation = 0;
            else relation = f[2] < 0.0f ? 2 : 1;
            g_serverWorld->diplomacy_.SetRelation(Ftol(f[0]), Ftol(f[1]), relation, 1);
            break;
        }
        case 0x43: {   // InvokeAlarm(type, x, y)
            Vector3 point = {f[1], f[2], 0.0f};
            g_serverWorld->InvokeAlarm(Ftol(f[0]), &point.x, 1);
            break;
        }
        case 0x88: {   // AddUnitToServer(name, prototype, ..., x, y, z)
            Vector3 point = {f[0], f[1], f[2]};
            g_serverWorld->AddUnitToServer(static_cast<unsigned>(TextKeyFromName(Text(cmd, 0))), Text(cmd, 1), Text(cmd, 2), &point);
            break;
        }
        case 0x89:     // RemoveUnitFromServer(object)
        case 0x96: {   // RemoveObjectFromServer(object)
            CEngineObject* object = Arg(cmd, 0);
            if (object) g_serverWorld->RemoveObject(object->id_);   // Rebirth patches it
            break;
        }
        case 0xD2:   // RecalcMercBriefings
            g_serverWorld->UpdateMercBriefings();
            break;
        case 0x1E: {   // SetWind(x, y, z, strength)
            Vector3 direction = {f[0], f[1], f[2]};
            g_serverWorld->ChangeWind(&direction, f[3]);
            break;
        }
        case 0x1F: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255
            const float scale = kColourScale;
            float b = f[2] * scale;
            float g = f[1] * scale;
            float r = f[0] * scale;
            g_sceneRenderer->SetFogColour(r, g, b);
            break;
        }
        case 0xB6:   // RunWorldTime(time): the configured speed (int)
            g_serverWorld->SetTimeSpeed(static_cast<float>(options::g_options->defaultTimeScale_));
            g_serverWorld->SetTime(f[0]);
            break;
        case 0xB7:   // FixWorldTime(time)
            g_serverWorld->SetTimeSpeed(0.0f);
            g_serverWorld->SetTime(f[0]);
            break;
        case 0xB8:   // SetWaterLevel(a, level, b)
            g_serverWorld->SetWaterLevel(Round(f[0]), f[1], Round(f[2]));
            break;
        case 0xA0:   // AddMob(name)
            g_serverWorld->LoadMobFile(Text(cmd, 0));
            break;
        case 0xA1:   // PlayCamera(name)
            GamePlayCameraPath(Text(cmd, 0), 0);
            break;
        case 0xC1:   // PlayMovie(name)
            PlayMovie(cmd);
            break;
        case 0x7D:   // ConsoleFloat(value)
            console::EndLine(console::g_consoleOut << f[0]);
            break;
        case 0x7E:   // ConsoleString(text)
            console::PrintLine(Text(cmd, 0));
            break;
        case 0xC3: {   // AddRectToArea(area, x1, y1, x2, y2)
            script->FindArea(Round(f[0]))->AddRect(f[1], f[2], f[3], f[4]);
            break;
        }
        case 0xC4: {   // AddRoundToArea(area, x, y, radius)
            script->FindArea(Round(f[0]))->AddRound(f[1], f[2], f[3]);
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

        case 0x91:     // SwitchLeverState(lever, state)
        case 0x9E: {   // SwitchLeverStateEx(lever, state, time)
            Lever* lever = LeverArg(cmd);
            if (!lever) break;
            float time;
            if (cmd->id == 0x91) time = -10.0f;
            else time = f[1];
            lever->SetState(Ftol(f[0]), time);
            break;
        }
        case 0xCC: {   // EnableLever(lever, on)
            Lever* lever = LeverArg(cmd);
            if (!lever) break;
            lever->Enable(!IsZero(f[0]));
            break;
        }
        case 0xAC: {   // SetScience(lever, a, b, four flags)
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

        case 0x32: {   // BlockUnit(unit, on): flags_ bit 0x800000
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            if (IsZero(f[0])) unit->flags_ &= ~0x800000u;
            else unit->flags_ |= 0x800000u;
            break;
        }
        case 0x36: {   // SetPlayer(unit, player)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            unit->SetHostileMask(g_serverWorld->diplomacy_.GetHostileMask(Ftol(f[0])));
            unit->SetSide(Ftol(f[0]));
            break;
        }
        case 0x38: {   // SetEnemy(unit, player): adds the player to the hostile mask
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            int player = Ftol(f[0]);
            std::uint32_t mask = (1u << (player & 31)) | unit->relationsA;
            unit->AddHostileMask(mask);
            break;
        }
        case 0x35: {   // KillUnit(object)
            CEngineObject* object = Arg(cmd, 0);
            DamageInfo damage;
            ResetDamageInfo(&damage);
            if (object) ObjectSlots(object)->ReceiveHit(&damage, nullptr, nullptr);
            break;
        }
        case 0x44: {   // InflictDamage(unit, amount)
            CUnitServer* state = UnitStateArg(cmd);
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
        case 0x83: {   // AddUnitUnderControl(player, unit)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (player && unit) unit->SetPlayer(player);
            break;
        }
        case 0x84: {   // RemoveUnitFromControl(player, unit)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (unit) unit->SetPlayer(nullptr);
            break;
        }
        case 0x42:   // SetSpellAggression(unit, value): does nothing
            break;
        case 0xAE: {   // RemoveQuestItem(unit, id): the first quest item with the id
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            int count = std::ssize(unit->carried);
            int id = Ftol(f[0]);
            for (int i = 0; i < count; ++i) {
                CItemQuest* item = AsQuest(unit->carried[i]);
                if (static_cast<int>(item->questItemId_) == id) {
                    unit->RemoveCarried(i);
                    break;
                }
            }
            break;
        }
        case 0xAF: {   // GiveUnitQuestItem(unit, item)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            CItem* item = g_serverWorld->CreateItemNamed(Text(cmd, 0));
            if (item && item->GetClassId() == kClassItemQuest) unit->AddCarried(item);
            break;
        }
        case 0xB3: {   // GiveUnitSpell(unit, spell)
            CUnit* unit = cmd->objectArgs[0]->AsUnit();
            if (!unit) break;
            CItem* spell = g_serverWorld->CreateSpellNamed(Text(cmd, 0));
            if (!spell) break;
            unit->AddSpell(spell);
            CUnitServer* state = UnitState(unit);
            state->RefreshEquipment();
            break;
        }
        case 0xCD:   // GiveSkill(unit, skill, value)
            GiveSkill(cmd);
            break;
        case 0xCE:     // GiveStrength(unit, value)
        case 0xCF:     // GiveDexterity
        case 0xD0: {   // GiveIntelligence
            CUnitServer* state = cmd->objectArgs[0]->GetUnitPerception();
            if (!state) break;
            CUnit* owner = state->unit;
            int value = Ftol(f[0]);
            UnitStats* stats = StatsOf(state);
            if (cmd->id == 0xCE) stats->RaiseStrength(value, owner);
            else if (cmd->id == 0xCF) stats->RaiseDexterity(value, owner);
            else stats->RaiseIntelligence(value, owner);
            break;
        }

        case 0x02: {   // MoveToPoint(unit, x, y)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x03: {   // RotateTo(unit, x, y)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            const Vector3* at = Position(state->unit);
            Vector3 direction = {f[0] - at->x, f[1] - at->y, -at->z};
            BrainOf(state)->TurnTowards(&direction, 0);
            break;
        }
        case 0x04: {   // MoveToObject(unit, target)
            CUnitServer* state = UnitStateArg(cmd);
            CEngineObject* target = Arg(cmd, 1);
            if (!state || !target) break;
            Vector3 point = {Position(target)->x, Position(target)->y, 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x3B: {   // Follow(unit, leader)
            CUnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->Follow(MapObjectArg(cmd, 1));
            break;
        }
        case 0x3C: {   // Attack(unit, target)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            CMapObject* target = MapObjectArg(cmd, 1);
            if (target) BrainOf(state)->AttackTarget(target);
            break;
        }
        case 0xE8: {   // ResetTarget(unit)
            CUnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ResetBehaviour();
            break;
        }
        case 0x3D: {   // Cast(unit, x, y): at the ground height there
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            float x = f[0];
            float y = f[1];
            float z = g_serverWorld->GridHeight(x, y);
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
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->ClearModes();
            BrainOf(state)->StopAction();
            break;
        }
        case 0x33: {   // Guard(unit, x, y, radius)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardGuard(&point, f[2]);
            break;
        }
        case 0x94: {   // Sentry(unit, x, y)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->StandardSentry(&point);
            break;
        }
        case 0x1B: {   // PlayAnimation(unit, name)
            CUnitServer* state = UnitStateArg(cmd);
            if (state) state->PlayAnimation(Text(cmd, 0));
            break;
        }
        case 0xC9:     // Lie
        case 0x3E:     // Crawl
        case 0x3F:     // Stand
        case 0x40:     // Walk
        case 0x41: {   // Run
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            int style = cmd->id == 0xC9 ? 0 : cmd->id == 0x3E ? 1 : cmd->id == 0x41 ? 3 : 2;
            state->SetMoveStyle(style);   // 0 lie, 1 crawl, 2 walk, 3 run
            break;
        }
        case 0xB0: {   // Rest(unit)
            CUnitServer* state = UnitStateArg(cmd);
            if (state) state->StopQueued();
            break;
        }

        case 0x24: {   // UMClear
            CUnitServer* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ClearModes();
            break;
        }
        case 0x25: {   // UMSentry(unit, x, y)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->AddMode(new SentryMode(&point));
            break;
        }
        case 0x26:     // UMGuard(unit, x, y, radius)
        case 0x27: {   // UMGuardEx(unit, x, y, radius, radius2)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            float radius = f[2];
            float radius2;
            if (cmd->id == 0x26) radius2 = -1.0f;
            else radius2 = f[3];
            BrainOf(state)->AddMode(new GuardMode(&point, radius, radius2));
            break;
        }
        case 0xCA: {   // UMFollow(unit, leader)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            CMapObject* leader = MapObjectArg(cmd, 1);
            BrainOf(state)->AddMode(new FollowMode(leader));
            break;
        }
        case 0x28: {   // UMPatrol
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(new PatrolMode());
            break;
        }
        case 0x29: {   // UMPatrolClear
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = BrainOf(state)->FindPatrolMode();
            if (patrol) patrol->points.clear();
            break;
        }
        case 0x2A: {   // UMPatrolAddPoint(unit, x, y)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            Vector3 point = {f[0], f[1], 0.0f};
            patrol->AppendPoint(&point);
            break;
        }
        case 0x2B: {   // UMPatrolAddPointLook(unit, x, y, z): a look at the last point
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            PatrolMode* patrol = PatrolOf(state);
            if (!patrol) break;
            PatrolPoint* last = patrol->LastPoint();
            if (!last) break;
            AILook look;
            AILook* value = look.ScriptAILookConstruct(f[0], f[1], f[2], 0);
            lib::SetAtGrow(last->looks, std::ssize(last->looks), *value);
            break;
        }
        case 0x31: {   // UMPlayer
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            brain->ClearModes();
            brain->AddMode(new PlayerMode());
            brain->StopAction();
            break;
        }
        case 0xC8:   // UMAg(unit, level)
            UnitModes(cmd);
            break;
        case 0x2C: {   // UMStandard: aggression, suspicion, corpse watcher
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            AIBrain* brain = BrainOf(state);
            brain->AddMode(new AggressionMode());
            brain->AddMode(new SuspectionMode());
            brain->AddMode(new CorpseWatcherMode());
            break;
        }
        case 0x2D: {   // UMAggression
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(new AggressionMode());
            break;
        }
        case 0x2E: {   // UMRevenge
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(new AggressionMode(1));
            break;
        }
        case 0xC7: {   // UMFear(unit, on)
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            int on = IsZero(f[0]) ? 0 : 1;
            BrainOf(state)->AddMode(new FearMode(static_cast<std::uint8_t>(on)));
            break;
        }
        case 0x2F: {   // UMSuspection
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(new SuspectionMode());
            break;
        }
        case 0x30: {   // UMCorpseWatcher
            CUnitServer* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->AddMode(new CorpseWatcherMode());
            break;
        }

        case 0x7F: {   // GSSetVar(player, name, value)
            if (IsMultiplayerClient() && ServerActive()) {
                GSSetVarMultiplayer(cmd);
                break;
            }
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (player) player->Server()->SetVariable(Text(cmd, 0), std::bit_cast<std::uint32_t>(f[1]));   // the float bits
            break;
        }
        case 0x98:     // GSSetVarMax(player, name, value)
        case 0x82: {   // GSDelVar(player, name)
            bool setMax = cmd->id == 0x98;
            if (IsMultiplayerClient() && ServerActive()) {
                GSVarMultiplayer(cmd, setMax);
                break;
            }
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            if (setMax) player->Server()->SetVariableMax(Text(cmd, 0), f[1]);
            else player->Server()->DeleteVariable(Text(cmd, 0));
            break;
        }
        case 0x1D: {   // PlayMusic(player, name)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (player) player->Server()->SendText0D(Text(cmd, 0));
            break;
        }
        case 0x8A: {   // LeaveToZone(player, zone, point)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (player) player->Server()->LeaveToZone(Text(cmd, 0), Ftol(f[1]));
            break;
        }
        case 0xD3:   // RedeployParty(player)
            RedeployParty(cmd);
            break;
        case 0x86:     // AddUnitToParty(player, party.unit, prototype)
        case 0x87: {   // RemoveUnitFromParty(player, party.unit)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::string name = cmd->stringArgs[0];
            CParty* records = PartyForScript(player, &name);
            if (records) {
                if (cmd->id == 0x86)
                    records->AddMember(name, Text(cmd, 1), game::kEmptyText);
                else
                    records->RemoveMember(static_cast<unsigned>(TextKeyFromName(name)));
            }
            break;
        }
        case 0xD4:     // CopyItems(player, from, to)
        case 0x9D: {   // CopyStats(player, from, to)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::string from = cmd->stringArgs[0];
            std::string to = cmd->stringArgs[1];
            CParty* fromRecords = PartyForScript(player, &from);
            CParty* toRecords = PartyForScript(player, &to);
            PartyUnitRecord* source = fromRecords->Find(TextKeyFromName(from));
            PartyUnitRecord* target = toRecords->Find(TextKeyFromName(to));
            if (source && target) {
                if (cmd->id == 0xD4) {
                    target->CopyItemsFrom(source, toRecords);
                } else {
                    std::memcpy(&target->stats, &source->stats, sizeof(target->stats));
                    target->texts[0] = source->texts[0];
                }
            }
            break;
        }
        case 0x9A: {   // CreateParty(player, name)
            if (g_serverWorld->GetPlayer(Ftol(f[0]))) g_partyManager->records.Add(Text(cmd, 0));
            break;
        }
        case 0x9B: {   // RemoveParty(player, name)
            if (g_serverWorld->GetPlayer(Ftol(f[0]))) g_partyManager->records.Remove(Text(cmd, 0));
            break;
        }
        case 0x9C: {   // SetCurrentParty(player, name)
            if (g_serverWorld->GetPlayer(Ftol(f[0])))
                g_partyManager->records.requestedName = cmd->stringArgs[0];
            break;
        }
        case 0xE0:   // AddLoot(player, from, to)
            CopyLoot(cmd, false);
            break;
        case 0xCB:   // CopyLoot(player, from, to)
            CopyLoot(cmd, true);
            break;
        case 0xA7: {   // GiveMoney(player, amount)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::list<CItem*> items;
            player->Party()->money_ += Ftol(f[1]);
            player->GiveItems(&items, 0);
            break;
        }
        case 0x92:     // GiveQuestItem(player, item)
        case 0xA6: {   // GiveItem(player, item)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            std::list<CItem*> items;
            CItem* item = g_serverWorld->CreateItemNamed(Text(cmd, 0));
            if (item) {
                items.push_back(item);
                player->GiveItems(&items, 0);
            }
            break;
        }
        case 0x93: {   // EraseQuestItem(player, id)
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (player) player->TakeLoot(Ftol(f[1]));
            break;
        }
        case 0xDF: {   // FixItems: every item repaired
            auto* list = g_serverWorld->itemManager_;
            ObjectListIteratorOf<CItem> it;
            it.manager = nullptr;
            for (CItem* item = it.First(list); item; item = it.Next()) item->Repair();
            if (it.manager) {   // destructor
                --it.manager->refCount_;
                it.manager = nullptr;
            }
            break;
        }

        case 0x9F: {   // QuestComplete(player, quest)
            if (IsMultiplayerClient()) {
                if (FinishQuestMap(Text(cmd, 0)) != 0) {
                    QuestRecord* quest = g_questDatabase->FindQuest(Text(cmd, 0));
                    if (quest) g_serverWorld->AwardExperience(nullptr, quest->exp);
                }
                break;
            }
            CPlayer* player = g_serverWorld->GetPlayer(Ftol(f[0]));
            if (!player) break;
            QuestRecord* quest = g_questDatabase->FindQuest(Text(cmd, 0));
            if (!quest) break;
            g_serverWorld->AwardExperience(player, quest->exp);
            ClassTemplatesQuestCompleted(&g_partyManager.Get());
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
            ScriptQuestAddObjective(cmd->id, cmd->stringArgs[0], 0.0f);
            break;
        case 0xDC:   // QObjUse(name, value)
            ScriptQuestAddObjective(cmd->id, cmd->stringArgs[0], f[0]);
            break;

        default:   // functions, sections and commands without effect here
            break;
        }

        ScriptFreeTemporaries(script);
        if (cont) ++index;
    }
    return 1;
}
