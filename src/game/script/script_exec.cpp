// ScriptRun::RunCommand, the switch over every world script command (the Lua bindings' commands). See
// docs/notes/script.md.
#include "game/ai/ai_map.h"
#include "game/items/item_manager.h"
#include "game/app/options.h"
#include "engine/data/text_manager.h"
#include "game/script/script.h"
#include "game/script/script_addon.h"

#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/items/item.h"
#include "game/objects/map_object.h"
#include "game/net/net_session.h"
#include "game/world/diplomacy.h"
#include "game/players/party.h"
#include "game/players/party_manager.h"
#include "game/players/party_records.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/world/quest_records.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "game/players/class_templates.h"
#include "game/objects/lever.h"
#include "engine/scene/scene_renderer.h"
#include "game/players/player_manager.h"
#include "game/effects/world_effect.h"

#include "lib/scales.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "game/app/movies.h"
#include "game/app/singletons.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <bit>
#include <string>
#include <vector>
#include <memory>

namespace {

const char* const kKillScriptNotAllowed = "KillScript command not allowed here";

PatrolMode* PatrolOf(UnitServer* state) { return state->brain.FindModeOf<PatrolMode>(); }

ScriptParticleSource* FindParticle(Script* script, int id) {
    for (ScriptParticleSource& source : script->particles)
        if (source.id == id) return &source;
    return nullptr;
}

// SetCP / SetCPFast(object, x, y, z).
void SetCP(ScriptArgs args, bool fast) {
    MapObject* object = args.Object().object;   // a unit or another map object
    if (!object) return;
    const Vector3 point = args.Point();
    AIMap* map = &g_serverWorld.aiMap;
    if (fast) {
        map->SetRefreshUnits(0);
        map->MoveInSectors(object, point);
        if (Unit* unit = UnitOrNull(object)) unit->ServerController()->ResyncWithUnit();
        map->SetRefreshUnits(1);
        object->MarkChanged(NetField::Placement);
        return;
    }
    float oldZ = object->localPosition.z;
    map->MoveInSectors(object, point);
    if (Unit* unit = UnitOrNull(object)) {
        unit->ServerController()->ResyncWithUnit();
        if (oldZ != object->localPosition.z) object->MarkChanged(NetField::Placement);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    float radius = object->bounds.radius + object->bounds.radius;
    std::vector<Unit*> units = map->FindUnitsInRadius(object->localPosition, radius);
    for (Unit* unit : units) map->RemoveFromSectors(unit);
    for (Unit* unit : units) {
        Vector3 free;
        if (!map->CanLeaveCell(unit)) {
            free = unit->localPosition;
            map->FindFreeCellNear(unit, unit->localPosition, &free);
            unit->localPosition = free;
            unit->ServerController()->ResyncWithUnit();
        }
        map->AddToSectors(unit);
    }
    object->MarkChanged(NetField::Placement);
}

// GiveSkill(unit, skill, value).
void GiveSkill(ScriptArgs args) {
    UnitServer* state = args.Object().ServerController();
    if (!state) return;
    const std::string& skill = args.Text();
    const int value = lib::Truncate(args.Float());
    // The skill names in the order they are tested; science goes to
    // SetScience, the others to SetSkill with their index.
    static const char* const kNames[] = {"science", "melee", "archery", "backstab", "elemental",
                                         "sense", "astral", "stealth", "awareness"};
    static const int kIndex[] = {0, 0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 9; ++i) {
        if (text::CompareIgnoringCase(skill, kNames[i]) != 0) continue;
        Unit* owner = state->unit;
        UnitStats* stats = &state->stats;
        if (i == 0) stats->RaiseAttribute(kIndex[i], value, owner);
        else stats->RaiseSkill(kIndex[i], value, owner);
    }
}

// RedeployParty(player).
void RedeployParty(ScriptArgs args) {
    Player* player = args.FindPlayer();
    if (!player) return;
    PlayerServer* state = player->Server();
    std::vector<UnitPlacement> slots;
    g_serverWorld.CollectParty(player, &slots);
    Party* party = player->GetParty();
    g_partyManager.records.BeginRedeploy(party);
    g_partyManager.records.EndRedeploy(party);
    // One slot and one party unit: the slot gets the unit's id.
    const std::vector<PartyUnitRecord>& members = player->GetParty()->members;
    if (slots.size() == 1 && std::ssize(members) == 1) slots[0].id = static_cast<int>(members[0].unitId);
    state->SendParty();
    player->Server()->lastPosition.z = -1313.0f;
    g_serverWorld.Redeploy(player, &slots, 0);
}

// AddLoot / CopyLoot(player, from, to).
void CopyLoot(ScriptArgs args, bool replace) {
    if (!args.FindPlayer()) return;
    Party* from = g_partyManager.records.Find(args.Text());
    Party* to = g_partyManager.records.Find(args.Text());
    if (!from || !to) return;
    if (replace) {
        while (!to->items.empty()) to->RemoveItem(to->items[0], true);
    }
    for (int i = 0; i < std::ssize(from->items); ++i)
        to->AddItem(from->items[i], true, true);
    if (replace) to->money = from->money;
    else to->money += from->money;
}

// PlayMovie(name).
void PlayMovie(ScriptArgs args) {
    std::string path = g_dataFolder + "Movies\\";
    std::string file = path + args.Text();
    ::PlayMovie(file, true, true, true);
}

// CastSpellPoint(spell, x, y, x2, y2) / CastSpellUnit(spell, x, y, target).
void CastSpell(ScriptArgs args, bool atUnit) {
    Spell* spell = g_serverWorld.spellManager->CreateSpellFromText(args.Text());
    if (spell) spell->AddRef();
    if (spell) {
        const float x = args.Float();
        const float y = args.Float();
        Vector3 from = {x, y, 0.0f};
        if (atUnit) {
            MapObject* target = args.Object().object;
            if (target) g_serverWorld.CastSpell(spell, 0, from, target, target->localPosition, 0);
        } else {
            const float toX = args.Float();
            const float toY = args.Float();
            Vector3 to = {toX, toY, 0.0f};
            g_serverWorld.CastSpell(spell, 0, from, nullptr, to, 0);
        }
    }
    if (spell) spell->Release();
}

// UMAg(unit, level): aggression set 0..3 replaces modes 3, 4, 5 and 7.
void UnitModes(ScriptArgs args) {
    UnitServer* state = args.Object().ServerController();
    if (!state) return;
    const int level = lib::RoundEven(args.Float());
    AIBrain* brain = &state->brain;
    brain->RemoveModesOfType(AIModeType::Suspection);
    brain->RemoveModesOfType(AIModeType::Aggression);
    brain->RemoveModesOfType(AIModeType::CorpseWatcher);
    brain->RemoveModesOfType(AIModeType::Fear);
    switch (level) {
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

bool ScriptRun::RunCommand(Script* script, ScriptCall* cmd) {
    bool goOn = true;
    scriptaddon::Call call{cmd, script, nullptr};
    const bool done = scriptaddon::Dispatch(&call);

    ScriptArgs args(cmd);
    const ScriptFunction function = done ? ScriptFunction::None : cmd->id;
    switch (function) {
    case ScriptFunction::Sleep: {   // Sleep(ticks): counts this run's ticks from the first one
        if (sleepStart < 0) sleepStart = ticks;
        int n = lib::RoundEven(args.Float());
        if (ticks - sleepStart < n) goOn = false;
        else sleepStart = -1;
        break;
    }
    case ScriptFunction::WaitSegment:     // WaitSegment(ticks): until this run has run that many ticks
        if (ticks < lib::RoundEven(args.Float())) goOn = false;
        break;
    case ScriptFunction::SleepUntil:     // SleepUntil(condition)
        if (IsZero(args.Float())) goOn = false;
        break;
    case ScriptFunction::SleepUntilIdle: {   // SleepUntilIdle(unit): while the brain has an action
        Unit* unit = args.Object().AsUnit();
        if (unit && unit->ServerController()->brain.action != AIAction::None) goOn = false;
        break;
    }
    case ScriptFunction::KillScript:
        if (!killFlag) {   // the WorldScript's: the call is left out
            game::Warning("%s", kKillScriptNotAllowed);
            break;
        }
        *killFlag = true;
        break;

    case ScriptFunction::AddObject: {   // AddObject(group, object)
        ScriptGroup* group = args.Group();
        group->Insert(args.Object().object);
        break;
    }
    case ScriptFunction::RemoveObject: {   // RemoveObject(group, object)
        ScriptGroup* group = args.Group();
        group->Remove(args.Object().object);
        break;
    }

    case ScriptFunction::CreatePointLight: script->CreatePointLight(args); break;
    case ScriptFunction::MovePointLight: script->MovePointLight(args); break;
    case ScriptFunction::DeletePointLight: script->DeletePointLight(args); break;
    case ScriptFunction::CreateParticleSource: script->CreateParticleSource(args); break;
    case ScriptFunction::MoveParticleSource: script->MoveParticleSource(args); break;
    case ScriptFunction::CreateLightning: script->CreateLightning(args); break;
    case ScriptFunction::CreateFX: script->CreateFx(args); break;
    case ScriptFunction::CreateFXSource:
    case ScriptFunction::CreateRandomizedFXSource:
        script->CreateFxSource(args, function == ScriptFunction::CreateRandomizedFXSource);
        break;
    case ScriptFunction::DeleteParticleSource: {   // DeleteParticleSource(id)
        int id = lib::RoundEven(args.Float());
        auto& list = script->particles;
        auto at = std::find_if(list.begin(), list.end(), [id](const ScriptParticleSource& source) { return source.id == id; });
        if (at != list.end()) list.erase(at);
        break;
    }
    case ScriptFunction::AttachParticleSource: {   // AttachParticleSource(id, objectId)
        const int id = lib::RoundEven(args.Float());
        const int objectId = lib::RoundEven(args.Float());
        if (ScriptParticleSource* entry = FindParticle(script, id)) entry->object->SetAttachedTo(objectId);
        break;
    }
    case ScriptFunction::AttachParticles: {   // AttachParticles(id, object)
        const float id = args.Float();
        MapObject* object = args.Object().object;
        if (!object) break;
        int objectId = object->id;
        if (ScriptParticleSource* entry = FindParticle(script, lib::RoundEven(id))) entry->object->SetAttachedTo(objectId);
        break;
    }
    case ScriptFunction::SetParticleSourceSize: {   // SetParticleSourceSize(id, size)
        const int id = lib::RoundEven(args.Float());
        const float size = args.Float();
        if (ScriptParticleSource* entry = FindParticle(script, id)) entry->object->SetSize(size);
        break;
    }
    case ScriptFunction::DeleteLightning: {   // DeleteLightning(id): every lightning with the id
        const int id = lib::Truncate(args.Float());
        std::erase_if(script->lightning, [id](const ScriptLightning& bolt) { return bolt.id == id; });
        break;
    }
    case ScriptFunction::DeleteFXSource: {   // DeleteFXSource(id): the first one with the id
        int id = lib::RoundEven(args.Float());
        auto& list = script->fxSources;
        auto at = std::find_if(list.begin(), list.end(), [id](const ScriptFxSource& source) { return source.id == id; });
        if (at != list.end()) list.erase(at);
        break;
    }

    case ScriptFunction::SendEvent: {   // SendEvent(type, value): the type is not used
        args.Float();
        g_serverWorld.players->SendValueToAll(lib::Truncate(args.Float()));
        break;
    }
    case ScriptFunction::SendStringEvent:   // SendStringEvent(type, text)
        g_serverWorld.players->SendTextToAll(args.Text());
        break;
    case ScriptFunction::GodMode: {   // GodMode(player, on)
        Player* player = args.FindPlayer();
        const float on = args.Float();
        if (player) player->godMode = static_cast<std::uint8_t>(lib::Truncate(on));
        break;
    }
    case ScriptFunction::SetDiplomacy: {   // SetDiplomacy(a, b, value): > 0 allied, < 0 hostile, else (0 or NaN) neutral
        const int a = lib::Truncate(args.Float());
        const int b = lib::Truncate(args.Float());
        const float value = args.Float();
        DiplomacyRelation relation;
        if (value > 0.0f) relation = DiplomacyRelation::Allied;
        else relation = value < 0.0f ? DiplomacyRelation::Hostile : DiplomacyRelation::Neutral;
        g_serverWorld.diplomacy.SetRelation(a, b, relation);
        break;
    }
    case ScriptFunction::InvokeAlarm: {   // InvokeAlarm(type, x, y)
        const int type = lib::Truncate(args.Float());
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        g_serverWorld.InvokeAlarm(type, &point.x, 1);
        break;
    }
    case ScriptFunction::AddUnitToServer: {   // AddUnitToServer(name, prototype, ..., x, y, z)
        const std::string& name = args.Text();
        const std::string& prototype = args.Text();
        const std::string& third = args.Text();
        const float x = args.Float();
        const float y = args.Float();
        const float z = args.Float();
        Vector3 point = {x, y, z};
        g_serverWorld.AddUnitToServer(static_cast<unsigned>(TextKeyFromName(name)), prototype, third, point);
        break;
    }
    case ScriptFunction::RemoveUnitFromServer:     // RemoveUnitFromServer(object)
    case ScriptFunction::RemoveObjectFromServer: {   // RemoveObjectFromServer(object)
        MapObject* object = args.Object().object;
        if (object) g_serverWorld.RemoveObject(object->id);
        break;
    }
    case ScriptFunction::RecalcMercBriefings:
        g_serverWorld.UpdateMercBriefings();
        break;
    case ScriptFunction::SetWind: {   // SetWind(x, y, z, strength)
        const float x = args.Float();
        const float y = args.Float();
        const float z = args.Float();
        const float strength = args.Float();
        Vector3 direction = {x, y, z};
        g_serverWorld.ChangeWind(direction, strength);
        break;
    }
    case ScriptFunction::SetBackGroundColor: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255
        const Vector3 colour = args.Colour();
        g_sceneRenderer.SetFogColour(colour.x, colour.y, colour.z);
        break;
    }
    case ScriptFunction::RunWorldTime:   // RunWorldTime(time): the configured speed (int)
        g_serverWorld.SetTimeSpeed(static_cast<float>(options::kDefaultTimeScale));
        g_serverWorld.SetTime(args.Float());
        break;
    case ScriptFunction::FixWorldTime:   // FixWorldTime(time)
        g_serverWorld.SetTimeSpeed(0.0f);
        g_serverWorld.SetTime(args.Float());
        break;
    case ScriptFunction::SetWaterLevel: {   // SetWaterLevel(a, level, b)
        const int a = lib::RoundEven(args.Float());
        const float level = args.Float();
        const int b = lib::RoundEven(args.Float());
        g_serverWorld.SetWaterLevel(a, level, b);
        break;
    }
    case ScriptFunction::AddMob:   // AddMob(name)
        g_serverWorld.LoadMobFile(args.Text());
        break;
    case ScriptFunction::PlayCamera:   // PlayCamera(name)
        g_clientWorld.PlayCameraPath(args.Text());
        break;
    case ScriptFunction::PlayMovie:   // PlayMovie(name)
        PlayMovie(args);
        break;
    case ScriptFunction::ConsoleFloat:   // ConsoleFloat(value)
        console::EndLine(console::g_consoleOut << args.Float());
        break;
    case ScriptFunction::ConsoleString:   // ConsoleString(text)
        console::PrintLine(args.Text());
        break;
    case ScriptFunction::AddRectToArea: {   // AddRectToArea(area, x1, y1, x2, y2)
        const int area = lib::RoundEven(args.Float());
        const float x1 = args.Float();
        const float y1 = args.Float();
        const float x2 = args.Float();
        const float y2 = args.Float();
        script->FindOrAddArea(area)->AddRect(x1, y1, x2, y2);
        break;
    }
    case ScriptFunction::AddRoundToArea: {   // AddRoundToArea(area, x, y, radius)
        const int area = lib::RoundEven(args.Float());
        const float x = args.Float();
        const float y = args.Float();
        const float radius = args.Float();
        script->FindOrAddArea(area)->AddRound(x, y, radius);
        break;
    }
    case ScriptFunction::DeleteArea:   // DeleteArea(area)
        script->DeleteArea(lib::RoundEven(args.Float()));
        break;
    case ScriptFunction::HideObject: {   // HideObject(object, hide): MapObjectFlag::Invisible
        MapObject* object = args.Object().object;
        if (!object) break;
        if (IsZero(args.Float())) object->flags &= ~MapObjectFlag::Invisible;
        else object->flags |= MapObjectFlag::Invisible;
        break;
    }
    case ScriptFunction::ActivateTrap: {   // ActivateTrap(object, on): MapObjectFlag::Armed
        MapObject* object = args.Object().object;
        if (!object) break;
        if (IsZero(args.Float())) object->flags &= ~MapObjectFlag::Armed;
        else object->flags |= MapObjectFlag::Armed;
        break;
    }
    case ScriptFunction::SetCP:   // SetCP(object, x, y, z)
        SetCP(args, false);
        break;
    case ScriptFunction::SetCPFast:   // SetCPFast(object, x, y, z)
        SetCP(args, true);
        break;

    case ScriptFunction::SwitchLeverState:     // SwitchLeverState(lever, state)
    case ScriptFunction::SwitchLeverStateEx: {   // SwitchLeverStateEx(lever, state, time)
        Lever* lever = LeverOrNull(args.Object().object);
        if (!lever) break;
        const int state = lib::Truncate(args.Float());
        float time;
        if (function == ScriptFunction::SwitchLeverState) time = -10.0f;
        else time = args.Float();
        lever->SetState(state, time);
        break;
    }
    case ScriptFunction::EnableLever: {   // EnableLever(lever, on)
        Lever* lever = LeverOrNull(args.Object().object);
        if (!lever) break;
        lever->Enable(!IsZero(args.Float()));
        break;
    }
    case ScriptFunction::SetScience: {   // SetScience(lever, a, b, four flags)
        Lever* lever = LeverOrNull(args.Object().object);
        if (!lever) break;
        LockKey lock = {1, 0, 0};
        lock.id = static_cast<std::uint32_t>(lib::Truncate(args.Float()));
        lock.level = static_cast<std::uint32_t>(lib::Truncate(args.Float()));
        lock.mask = 0;
        if (!IsZero(args.Float())) lock.mask = 1;
        if (!IsZero(args.Float())) lock.mask |= 2;
        if (!IsZero(args.Float())) lock.mask |= 4;
        if (!IsZero(args.Float())) lock.mask |= 8;
        lever->SetLock(&lock);
        break;
    }

    case ScriptFunction::BlockUnit: {   // BlockUnit(unit, on): MapObjectFlag::Blocked
        MapObject* unit = args.Object().object;
        if (!unit || unit->GetClassId() != ClassId::Unit) break;
        if (IsZero(args.Float())) unit->flags &= ~MapObjectFlag::Blocked;
        else unit->flags |= MapObjectFlag::Blocked;
        break;
    }
    case ScriptFunction::SetPlayer: {   // SetPlayer(unit, player)
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        const int player = lib::Truncate(args.Float());
        unit->SetHostileMask(g_serverWorld.diplomacy.GetHostileMask(player));
        unit->SetSide(player);
        break;
    }
    case ScriptFunction::SetEnemy: {   // SetEnemy(unit, player): adds the player to the hostile mask
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        int player = lib::Truncate(args.Float());
        std::uint32_t mask = (1u << (player & 31)) | unit->relationsA;
        unit->AddHostileMask(mask);
        break;
    }
    case ScriptFunction::KillUnit: {   // KillUnit(object)
        MapObject* object = args.Object().object;
        DamageInfo damage;
        ResetDamageInfo(&damage);
        if (object) object->ReceiveHit(&damage, nullptr, nullptr);
        break;
    }
    case ScriptFunction::InflictDamage: {   // InflictDamage(unit, amount)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        DamageInfo damage = {};
        damage.Weight(DamageType::Lightning) = 1.0f;
        damage.weaponValue = 0;
        damage.attackRoll = 100.0f;
        damage.amount = args.Float();
        damage.multiplier = 1.0f;
        damage.location = 1;
        // TakeHit(hit, attacker, source): three arguments.
        state->TakeHit(&damage, nullptr, nullptr);
        break;
    }
    case ScriptFunction::AddUnitUnderControl: {   // AddUnitUnderControl(player, unit)
        Player* player = args.FindPlayer();
        Unit* unit = args.Object().AsUnit();
        if (player && unit) unit->SetPlayer(player);
        break;
    }
    case ScriptFunction::RemoveUnitFromControl: {   // RemoveUnitFromControl(player, unit): the player is not used
        Unit* unit = args.Object().AsUnit();
        if (unit) unit->SetPlayer(nullptr);
        break;
    }
    case ScriptFunction::SetSpellAggression:   // SetSpellAggression(unit, value): does nothing
        break;
    case ScriptFunction::RemoveQuestItem: {   // RemoveQuestItem(unit, id): the first quest item with the id
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        int count = lib::Count(unit->carried);
        int id = lib::Truncate(args.Float());
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(unit->carried[i]->QuestWord()) == id) {
                unit->RemoveCarried(i);
                break;
            }
        }
        break;
    }
    case ScriptFunction::GiveUnitQuestItem: {   // GiveUnitQuestItem(unit, item)
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        Item* item = g_serverWorld.CreateItemNamed(args.Text());
        if (item && item->GetClassId() == ClassId::ItemQuest) unit->AddCarried(item);
        break;
    }
    case ScriptFunction::GiveUnitSpell: {   // GiveUnitSpell(unit, spell)
        Unit* unit = args.Object().AsUnit();
        if (!unit) break;
        Item* spell = g_serverWorld.CreateSpellNamed(args.Text());
        if (!spell) break;
        unit->AddSpell(spell);
        UnitServer* state = unit->ServerController();
        state->RefreshEquipment();
        break;
    }
    case ScriptFunction::GiveSkill:   // GiveSkill(unit, skill, value)
        GiveSkill(args);
        break;
    case ScriptFunction::GiveStrength:     // GiveStrength(unit, value)
    case ScriptFunction::GiveDexterity:
    case ScriptFunction::GiveIntelligence: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        Unit* owner = state->unit;
        int value = lib::Truncate(args.Float());
        UnitStats* stats = &state->stats;
        if (function == ScriptFunction::GiveStrength) stats->RaiseStrength(value, owner);
        else if (function == ScriptFunction::GiveDexterity) stats->RaiseDexterity(value, owner);
        else stats->RaiseIntelligence(value, owner);
        break;
    }

    case ScriptFunction::MoveToPoint: {   // MoveToPoint(unit, x, y)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        state->brain.Walk(point, false);
        break;
    }
    case ScriptFunction::RotateTo: {   // RotateTo(unit, x, y)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        const Vector3* at = &state->unit->localPosition;
        Vector3 direction = {x - at->x, y - at->y, -at->z};
        state->brain.TurnTowards(direction, 0);
        break;
    }
    case ScriptFunction::MoveToObject: {   // MoveToObject(unit, target)
        UnitServer* state = args.Object().ServerController();
        MapObject* target = args.Object().object;
        if (!state || !target) break;
        Vector3 point = {target->localPosition.x, target->localPosition.y, 0.0f};
        state->brain.Walk(point, false);
        break;
    }
    case ScriptFunction::Follow: {   // Follow(unit, leader)
        UnitServer* state = args.Object().ServerController();
        MapObject* leader = args.Object().object;
        if (state) state->brain.Follow(leader);
        break;
    }
    case ScriptFunction::Attack: {   // Attack(unit, target)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        MapObject* target = args.Object().object;
        if (target) state->brain.AttackTarget(target);
        break;
    }
    case ScriptFunction::ResetTarget: {   // ResetTarget(unit)
        UnitServer* state = args.Object().ServerController();
        if (state) state->brain.ResetBehaviour();
        break;
    }
    case ScriptFunction::Cast: {   // Cast(unit, x, y): at the ground height there
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        float x = args.Float();
        float y = args.Float();
        float z = g_serverWorld.GridHeight(x, y);
        Vector3 point = {x, y, z};
        state->brain.CastAt(0, point, false);
        break;
    }
    case ScriptFunction::CastSpellPoint:   // CastSpellPoint(spell, x, y, x2, y2)
        CastSpell(args, false);
        break;
    case ScriptFunction::CastSpellUnit:   // CastSpellUnit(spell, x, y, target)
        CastSpell(args, true);
        break;
    case ScriptFunction::Idle: {   // Idle(unit)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.ClearModes();
        state->brain.StopAction();
        break;
    }
    case ScriptFunction::Guard: {   // Guard(unit, x, y, radius)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        const float radius = args.Float();
        Vector3 point = {x, y, 0.0f};
        state->brain.StandardGuard(point, radius);
        break;
    }
    case ScriptFunction::Sentry: {   // Sentry(unit, x, y)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        state->brain.StandardSentry(point);
        break;
    }
    case ScriptFunction::PlayAnimation: {   // PlayAnimation(unit, name)
        UnitServer* state = args.Object().ServerController();
        if (state) state->PlayAnimation(args.Text());
        break;
    }
    case ScriptFunction::Lie:
    case ScriptFunction::Crawl:
    case ScriptFunction::Stand:
    case ScriptFunction::Walk:
    case ScriptFunction::Run: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        MoveMode style = function == ScriptFunction::Lie ? MoveMode::Lie
                       : function == ScriptFunction::Crawl ? MoveMode::Crawl
                       : function == ScriptFunction::Run ? MoveMode::Run : MoveMode::Walk;
        state->SetMoveStyle(style);
        break;
    }
    case ScriptFunction::Rest: {   // Rest(unit)
        UnitServer* state = args.Object().ServerController();
        if (state) state->StopQueued();
        break;
    }

    case ScriptFunction::UMClear: {
        UnitServer* state = args.Object().ServerController();
        if (state) state->brain.ClearModes();
        break;
    }
    case ScriptFunction::UMSentry: {   // UMSentry(unit, x, y)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        state->brain.AddMode(std::make_unique<SentryMode>(point));
        break;
    }
    case ScriptFunction::UMGuard:     // UMGuard(unit, x, y, radius)
    case ScriptFunction::UMGuardEx: {   // UMGuardEx(unit, x, y, radius, radius2)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        float radius = args.Float();
        float radius2;
        if (function == ScriptFunction::UMGuard) radius2 = -1.0f;
        else radius2 = args.Float();
        state->brain.AddMode(std::make_unique<GuardMode>(point, radius, radius2));
        break;
    }
    case ScriptFunction::UMFollow: {   // UMFollow(unit, leader)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        MapObject* leader = args.Object().object;
        state->brain.AddMode(std::make_unique<FollowMode>(leader));
        break;
    }
    case ScriptFunction::UMPatrol: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<PatrolMode>());
        break;
    }
    case ScriptFunction::UMPatrolClear: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        PatrolMode* patrol = PatrolOf(state);
        if (patrol) patrol->points.clear();
        break;
    }
    case ScriptFunction::UMPatrolAddPoint: {   // UMPatrolAddPoint(unit, x, y)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        PatrolMode* patrol = PatrolOf(state);
        if (!patrol) break;
        const float x = args.Float();
        const float y = args.Float();
        Vector3 point = {x, y, 0.0f};
        patrol->AppendPoint(point);
        break;
    }
    case ScriptFunction::UMPatrolAddPointLook: {   // UMPatrolAddPointLook(unit, angle, wait, turn time): a look at the last point
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        PatrolMode* patrol = PatrolOf(state);
        if (!patrol) break;
        PatrolPoint* last = patrol->LastPoint();
        if (!last) break;
        const float angle = args.Float();
        const float wait = args.Float();
        const float turnTime = args.Float();
        last->looks.push_back(AILook{angle, turnTime, wait, false});
        break;
    }
    case ScriptFunction::UMPlayer: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        AIBrain* brain = &state->brain;
        brain->ClearModes();
        brain->AddMode(std::make_unique<PlayerMode>());
        brain->StopAction();
        break;
    }
    case ScriptFunction::UMAg:   // UMAg(unit, level)
        UnitModes(args);
        break;
    case ScriptFunction::UMStandard: {   // UMStandard: aggression, suspicion, corpse watcher
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        AIBrain* brain = &state->brain;
        brain->AddMode(std::make_unique<AggressionMode>());
        brain->AddMode(std::make_unique<SuspectionMode>());
        brain->AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    }
    case ScriptFunction::UMAggression: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<AggressionMode>());
        break;
    }
    case ScriptFunction::UMRevenge: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<AggressionMode>(true));
        break;
    }
    case ScriptFunction::UMFear: {   // UMFear(unit, on)
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<FearMode>(!IsZero(args.Float())));
        break;
    }
    case ScriptFunction::UMSuspection: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<SuspectionMode>());
        break;
    }
    case ScriptFunction::UMCorpseWatcher: {
        UnitServer* state = args.Object().ServerController();
        if (!state) break;
        state->brain.AddMode(std::make_unique<CorpseWatcherMode>());
        break;
    }

    case ScriptFunction::GSSetVar: {   // GSSetVar(player, name, value): in a network game the game server's
        if (IsMultiplayerClient() && IsLive(net::g_gameServer)) {
            args.Float();   // the player is not used
            std::string name = args.Text();
            net::g_gameServer->SetVariable(name, args.Float());
            break;
        }
        Player* player = args.FindPlayer();
        const std::string& name = args.Text();
        const float value = args.Float();
        if (player) player->Server()->SetVariable(name, value);
        break;
    }
    case ScriptFunction::GSSetVarMax:     // GSSetVarMax(player, name, value)
    case ScriptFunction::GSDelVar: {   // GSDelVar(player, name)
        bool setMax = function == ScriptFunction::GSSetVarMax;
        if (IsMultiplayerClient() && IsLive(net::g_gameServer)) {   // GSDelVar's branch is unreached in the shipped game
            args.Float();   // the player is not used
            std::string name = args.Text();
            if (setMax) net::g_gameServer->RaiseVariable(name, args.Float());
            else net::g_gameServer->RemoveVariable(name);
            break;
        }
        Player* player = args.FindPlayer();
        if (!player) break;
        const std::string& name = args.Text();
        if (setMax) player->Server()->SetVariableMax(name, args.Float());
        else player->Server()->DeleteVariable(name);
        break;
    }
    case ScriptFunction::PlayMusic: {   // PlayMusic(player, name)
        Player* player = args.FindPlayer();
        if (player) player->Server()->SendJingle(args.Text());
        break;
    }
    case ScriptFunction::LeaveToZone: {   // LeaveToZone(player, zone, point)
        Player* player = args.FindPlayer();
        const std::string& zone = args.Text();
        const int entrance = lib::Truncate(args.Float());
        if (player) player->Server()->LeaveToZone(zone, entrance);
        break;
    }
    case ScriptFunction::RedeployParty:   // RedeployParty(player)
        RedeployParty(args);
        break;
    case ScriptFunction::AddUnitToParty:     // AddUnitToParty(player, party.unit, prototype)
    case ScriptFunction::RemoveUnitFromParty: {   // RemoveUnitFromParty(player, party.unit)
        Player* player = args.FindPlayer();
        if (!player) break;
        std::string name = args.Text();
        Party* records = PartyForScript(player, &name);
        if (records) {
            if (function == ScriptFunction::AddUnitToParty)
                g_serverWorld.TakeLookFromUnit(records->AddMember(name, args.Text(), ""));
            else
                records->RemoveMember(static_cast<unsigned>(TextKeyFromName(name)));
        }
        break;
    }
    case ScriptFunction::CopyItems:     // CopyItems(player, from, to)
    case ScriptFunction::CopyStats: {   // CopyStats(player, from, to)
        Player* player = args.FindPlayer();
        if (!player) break;
        std::string from = args.Text();
        std::string to = args.Text();
        Party* fromRecords = PartyForScript(player, &from);
        Party* toRecords = PartyForScript(player, &to);
        if (!fromRecords || !toRecords) break;   // a party name no record list has
        PartyUnitRecord* source = fromRecords->Find(TextKeyFromName(from));
        PartyUnitRecord* target = toRecords->Find(TextKeyFromName(to));
        if (source && target) {
            if (function == ScriptFunction::CopyItems) {
                target->CopyItemsFrom(source, toRecords);
            } else {
                std::memcpy(&target->stats, &source->stats, sizeof(target->stats));
                target->scriptName = source->scriptName;
            }
        }
        break;
    }
    case ScriptFunction::CreateParty: {   // CreateParty(player, name)
        if (args.FindPlayer()) g_partyManager.records.Add(args.Text());
        break;
    }
    case ScriptFunction::RemoveParty: {   // RemoveParty(player, name)
        if (args.FindPlayer()) g_partyManager.records.Remove(args.Text());
        break;
    }
    case ScriptFunction::SetCurrentParty: {   // SetCurrentParty(player, name)
        if (args.FindPlayer()) g_partyManager.records.requestedName = args.Text();
        break;
    }
    case ScriptFunction::AddLoot:   // AddLoot(player, from, to)
        CopyLoot(args, false);
        break;
    case ScriptFunction::CopyLoot:   // CopyLoot(player, from, to)
        CopyLoot(args, true);
        break;
    case ScriptFunction::GiveMoney: {   // GiveMoney(player, amount)
        Player* player = args.FindPlayer();
        if (!player) break;
        std::vector<Item*> items;
        player->GetParty()->money += lib::Truncate(args.Float());
        player->GiveItems(items, 0);
        break;
    }
    case ScriptFunction::GiveQuestItem:     // GiveQuestItem(player, item)
    case ScriptFunction::GiveItem: {   // GiveItem(player, item)
        Player* player = args.FindPlayer();
        if (!player) break;
        std::vector<Item*> items;
        Item* item = g_serverWorld.CreateItemNamed(args.Text());
        if (item) {
            items.push_back(item);
            player->GiveItems(items, 0);
        }
        break;
    }
    case ScriptFunction::EraseQuestItem: {   // EraseQuestItem(player, id)
        Player* player = args.FindPlayer();
        const int id = lib::Truncate(args.Float());
        if (player) player->TakeLoot(id);
        break;
    }
    case ScriptFunction::FixItems: {   // FixItems: every item repaired
        ObjectListIteratorOf<Item> it;
        for (Item* item = it.First(g_serverWorld.itemManager.get()); item; item = it.Next()) item->Repair();
        break;
    }

    case ScriptFunction::QuestComplete: {   // QuestComplete(player, quest)
        if (IsMultiplayerClient()) {
            args.Float();   // the player is not used
            const std::string map = args.Text();
            if (IsLive(net::g_gameServer) && net::g_gameServer->FinishMap(map)) {
                QuestRecord* quest = g_questDatabase.FindQuest(map);
                if (quest) g_serverWorld.AwardExperience(nullptr, quest->exp);
            }
            break;
        }
        Player* player = args.FindPlayer();
        if (!player) break;
        QuestRecord* quest = g_questDatabase.FindQuest(args.Text());
        if (!quest) break;
        g_serverWorld.AwardExperience(player, quest->exp);
        ClassTemplatesQuestCompleted(&g_partyManager);
        break;
    }
    case ScriptFunction::QStart:   // QStart(name)
        script->quest.Start(args.Text());
        break;
    case ScriptFunction::QFinish: {
        std::string text = script->quest.Finish();
        script->Load(&text, true);
        break;
    }
    case ScriptFunction::QObjArea:   // QObjArea(area)
    case ScriptFunction::QObjGetItem:   // QObjGetItem(item)
        script->quest.AddObjective(cmd->id, "", args.Float());
        break;
    case ScriptFunction::QObjSeeObject:   // QObjSeeObject(name)
    case ScriptFunction::QObjSeeUnit:
    case ScriptFunction::QObjKillUnit:
    case ScriptFunction::QObjKillGroup:
        script->quest.AddObjective(cmd->id, args.Text(), 0.0f);
        break;
    case ScriptFunction::QObjUse: {   // QObjUse(name, value)
        const std::string& name = args.Text();
        script->quest.AddObjective(cmd->id, name, args.Float());
        break;
    }

    default:   // functions, sections and commands without effect here
        break;
    }
    return goOn;
}
