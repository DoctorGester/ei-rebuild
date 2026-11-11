// Rebirth's extra script commands, plus GSGetPlayerVar and IsFileExists.
#include "game/app/movies.h"   // g_loadingMovieOff
#include "game/scene/camera.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/objects/map_object.h"
#include "game/data/options.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/script/script_addon_handlers.h"
#include "game/mods/mod_folders.h"
#include "platform/platform.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/core/named_floats.h"

#include "lib/str.h"


#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace scriptaddon {

namespace {


// The float bits of `value` as the dword the player messages take.
std::uint32_t Bits(float value) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(value);
    return bits;
}

// 1 (or 0) against the argument: an ordered equal.
bool Equals(float constant, float value) { return constant == value; }

// The server-side object of player 0 (game over), or null.
PlayerServer* FirstPlayerState() {
    Player* player = g_serverWorld.GetPlayer(0);
    return player ? player->Server() : nullptr;
}

bool EndsWith(const std::string& text, const char* suffix) {
    std::size_t n = std::strlen(suffix);
    return n != 0 && text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

} // namespace

// GSSetPlayerVar(player, name, value): the player's server-side variable.
bool GSSetPlayerVar(Call* call) {
    ScriptArgs args(call->cmd);
    int number = TruncateOrMin(args.Float());
    const std::string& name = args.Text();
    float value = args.Float();
    Player* player = g_serverWorld.GetPlayer(number);
    if (!player) return Report("GSSetPlayerVar", "There is no player with number %d", number);
    player->Server()->SetVariable(name, value);
    return true;
}

// GSGetPlayerVar(player, name): the player's variable; the result is left alone without the
// player.
bool GSGetPlayerVar(Call* call) {
    ScriptArgs args(call->cmd);
    int number = TruncateOrMin(args.Float());
    const std::string& name = args.Text();
    Player* player = g_serverWorld.GetPlayer(number);
    if (!player) return Report("GSGetPlayerVar", "There is no player with number %d", number);
    *call->result.number = player->GetParty()->variables.Get(name);
    return true;
}

// GiveExp(player, value): the world's experience award of `value` to the player (no check that
// the player exists: null takes the multiplayer path).
bool GiveExp(Call* call) {
    ScriptArgs args(call->cmd);
    Player* player = g_serverWorld.GetPlayer(TruncateOrMin(args.Float()));
    g_serverWorld.AwardExperience(player, args.Float());
    return true;
}

// GiveUnitExp(unit, value): the unit's data gains the experience; a unit in a party with a
// server-side player also sends it (the value's bits) with the unit's id.
bool GiveUnitExp(Call* call) {
    ScriptArgs args(call->cmd);
    UnitServer* data = args.Object().ServerController();
    if (!data) return InvalidArguments("GiveUnitExp");
    const float experience = args.Float();
    data->AddExperience(experience);
    Unit* unit = data->unit;
    if (auto* party = unit->player)
        if (PlayerServer* state = party->Server())
            state->SendExperience(static_cast<std::uint32_t>(unit->id), Bits(experience));
    return true;
}

bool StartCameraSave(Call* call) {
    g_clientWorld.flags |= WorldClientFlag::RecordingCamera;
    g_clientWorld.camera->OpenFile(ScriptArgs(call->cmd).Text());
    return true;
}

bool StopCameraSave(Call*) {
    g_clientWorld.camera->CloseFile();
    g_clientWorld.flags &= ~WorldClientFlag::RecordingCamera;
    return true;
}

// ResetTarget(unit) (replaces the game's): the brain's alarm record at the brain's alarm slot
// byte is applied again, after the perception memory is emptied.
bool ResetTarget(Call* call) {
    Unit* unit = ScriptArgs(call->cmd).Object().AsUnit();   // IsUnit's test
    if (!unit) return InvalidArguments("ResetTarget");
    AIBrain* brain = &unit->ServerController()->brain;
    int index = brain->alarmSlot;
    brain->memory.Reset();
    if (index >= std::ssize(brain->alarmRecords)) return Report("ResetTarget", "Failed to reset unit %d", unit->id);
    brain->ApplyBehaviour(&brain->alarmRecords[index], index);
    return true;
}

bool SendCurrentParty(Call* call) {
    int number = TruncateOrMin(ScriptArgs(call->cmd).Float());
    Player* player = g_serverWorld.GetPlayer(number);
    if (!player) return Report("SendCurrentParty", "There is no player with number %d", number);
    player->Server()->ResendParty();
    return true;
}

// Loot(player, unit): the player loots the dead unit, which is then removed.
bool Loot(Call* call) {
    ScriptArgs args(call->cmd);
    int number = TruncateOrMin(args.Float());
    Player* player = g_serverWorld.GetPlayer(number);
    if (!player) return Report("Loot", "There is no player with number %d", number);
    MapObject* object = args.Object().object;
    if (!object) return Report("Loot", "Object doesn't exist");
    Unit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("Loot", "Object must be a unit");
    if (unit->IsDead() == 0) return Report("Loot", "Object must be dead");
    g_serverWorld.Loot(unit, player);
    g_serverWorld.RemoveObject(unit->id);
    return true;
}

// GameOver / GameOverMSG(title, text): the "gameover" string event to player 0's client.
bool GameOver(Call*) {
    if (PlayerServer* state = FirstPlayerState()) state->SendStringEvent("gameover");
    return true;
}

bool GameOverMsg(Call* call) {
    if (PlayerServer* state = FirstPlayerState()) {
        ScriptArgs args(call->cmd);
        const std::string& title = args.Text();
        const std::string& message = args.Text();
        std::string text = "gameover " + title + " " + message;
        state->SendStringEvent(text);
    }
    return true;
}

// IsFileExists(path): a file (not a folder) under the mod folder; with '\' read as '/', a path
// that is "..", contains "/../", starts with "../" or ends with "/.." is refused (the result is
// then left alone).
bool IsFileExists(Call* call) {
    std::string path = modfolders::ModPath();
    std::string name = ScriptArgs(call->cmd).Text();
    for (char& c : name)
        if (c == '\\') c = '/';
    bool forbidden = name == ".." || name.find("/../") != std::string::npos || name.compare(0, 3, "../") == 0 ||
                     EndsWith(name, "/..");
    if (forbidden) return Report("IsFileExists", "'..' string literal is forbidden");
    path += name;
    *call->result.number = ScriptBool(platform::IsFile(path));
    return true;
}

// GSListPlayerVars(player): "<name> = <value>" for each of the player's variables, in the
// table's order (no check for the player).
bool GSListPlayerVars(Call* call) {
    Player* player = g_serverWorld.GetPlayer(TruncateOrMin(ScriptArgs(call->cmd).Float()));
    const NamedFloatTable& variables = player->GetParty()->variables;
    for (auto& node : variables) {
        std::string line;
        line = str::Format("%s = %f", node.first, static_cast<double>(node.second));
        (console::g_consoleOut << line << "\n").Flush();
    }
    return true;
}

// GiveUnitWeapon(unit, item, override) / GiveUnitArmor: the item made from its name goes to the
// unit; with override 1 (only for units without a text-key id) the unit's stats are set from it.
bool GiveUnitWeapon(Call* call) {
    ScriptArgs args(call->cmd);
    MapObject* object = args.Object().object;
    const std::string& name = args.Text();
    bool override = Equals(1.0f, args.Float());
    if (!object) return Report("GiveUnitWeapon", "Unit doesn't exists");
    Unit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("GiveUnitWeapon", "Object with ID %d must be a Unit", object->id);
    Item* item = g_serverWorld.CreateItemNamed(name);
    if (override && unit->id >= 1000000000) override = false;    // (a warning with EnableShowingScriptErrors)
    if (!item) return Report("GiveUnitWeapon", "Can't create item \"%s\"", name);
    int slot = unit->AddWeapon(item);
    if (slot == -1) return true;
    unit->pendingWeapon = slot;
    // A name of another item kind fills the slot without the weapon's statistics.
    if (ItemWeapon* made = item->AsWeapon(); override && made) {
        const auto* weapon = itemdb::RecordAs<itemdb::WeaponRecord>(made->GetPrototype());
        UnitStats& stats = unit->ServerController()->stats;
        const UnitPrototypeRecord* prototype = unit->GetPrototype();
        stats.damage = made->damage;
        stats.damageSpread = made->damageSpread;
        float sum = prototype->combat[3] + weapon->defenceBonus;
        stats.combat[UnitStats::CombatIndex::Defence] = sum;
        stats.combat[UnitStats::CombatIndex::Reach] = weapon->reach;
        sum = prototype->combat[1] + weapon->attackBonus;
        stats.combat[UnitStats::CombatIndex::Attack] = sum;
        stats.attackCategory = static_cast<float>(weapon->category);
        stats.naturalWeapon = made->GetType();
        for (int i = 0; i < 7; ++i) stats.damageWeights[i] = weapon->damageTypes[i];
        unit->MarkChanged(NetField::Vitals);
    }
    unit->controller->RefreshModel();
    unit->controller->RefreshEquipment();
    return true;
}

// Override: per value c (7), 0.05 * the head slot's + 0.3 * (armour slots 1 and 3) + 0.3 * (the armour slots
// 1, 3 and 6 second block) + 0.3 * (armour slots 2, 4, 5), each plus the prototype's protection
// scale, into both halves of the unit's server-side protection pair c.
bool GiveUnitArmor(Call* call) {
    ScriptArgs args(call->cmd);
    MapObject* object = args.Object().object;
    const std::string& name = args.Text();
    bool override = Equals(1.0f, args.Float());
    if (!object) return Report("GiveUnitArmor", "Unit doesn't exists");
    Unit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("GiveUnitArmor", "Object with ID %d must be a Unit", object->id);
    Item* item = g_serverWorld.CreateItemNamed(name);
    if (override && unit->id >= 1000000000) override = false;
    if (!item) return Report("GiveUnitArmor", "Can't create item \"%s\"", name);
    unit->EquipArmour(item);
    if (override) {
        UnitStatPair* protection = unit->Vitals().protection;
        const UnitPrototypeRecord* prototype = unit->GetPrototype();
        // Four blocks of seven values (A, C, B, D in memory order) and their weights.
        float blocks[4][7] = {};
        const float weights[4] = {0.05f, 0.3f, 0.3f, 0.3f};
        auto addInto = [&](int block, const std::array<float, kDamageTypeCount>& from) {
            for (int c = 0; c < 7; ++c) blocks[block][c] = from[c] + blocks[block][c];
        };
        for (int slot = 0; slot < 7; ++slot) {
            ItemArmor* equipped = unit->armor[slot];
            if (!equipped) continue;
            switch (slot) {
            case 0: addInto(0, equipped->absorb[0]); break;
            case 1:
            case 3:
                addInto(1, equipped->absorb[0]);
                addInto(2, equipped->absorb[1]);
                break;
            case 2:
            case 4:
            case 5: addInto(3, equipped->absorb[0]); break;
            case 6: addInto(2, equipped->absorb[0]); break;
            }
        }
        for (int c = 0; c < 7; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum = weights[k] * blocks[k][c] + sum;
            float value = prototype->protectionScale + sum;
            protection[c].base = value;
            value = prototype->protectionScale + sum;
            protection[c].current = value;
        }
        unit->MarkChanged(NetField::Vitals);
    }
    unit->controller->RefreshEquipment();
    return true;
}

// ShowAllUnits(0) sets the "draw all" flag, anything else clears it.
bool ShowAllUnits(Call* call) {
    options::g_options.drawVisibleOnly = Equals(0.0f, ScriptArgs(call->cmd).Float()) ? 1 : 0;
    return true;
}

// PauseGame(value): outside network games, the client's pause request with (value == 0).
bool PauseGame(Call* call) {
    if (!IsMultiplayerClient()) g_clientWorld.SendPause(Equals(0.0f, ScriptArgs(call->cmd).Float()) ? 1 : 0);
    return true;
}

// SetWeather("rain" | "clear" | "snow"): kind 1 / 0 / 2 with strength 20 / 20 / 40; anything
// else is clear at 20. The weather's delay is set as after a roll's change, so the asked weather holds for 10 checks
// before a roll may end it (differs from the original on purpose: docs/notes/world.md, "The weather's rolls").
bool SetWeather(Call* call) {
    std::string name = ScriptArgs(call->cmd).Text();
    int kind = 0;
    float strength = 20.0f;
    if (str::Compare(name, "rain") == 0) kind = 1;
    else if (str::Compare(name, "clear") == 0) kind = 0;
    else if (str::Compare(name, "snow") == 0) {
        kind = 2;
        strength = 40.0f;
    }
    g_serverWorld.SetWeather(kind, strength);
    g_serverWorld.weatherDelay = kWeatherChangeDelay;
    return true;
}

// AddMobEx(file, [keep ids]): loads a .mob file's objects with the loading movie off meanwhile (a file that cannot be
// read ends the game, as AddMob's). addon pushes its byte flag as a 4-byte value whose upper bytes are left over on
// its stack; here it is 0 or 1.
bool AddMobEx(Call* call) {
    ScriptArgs args(call->cmd);
    std::string file = args.Text();
    bool keepIds = args.FloatCount() != 0 && Equals(1.0f, args.Float());
    g_loadingMovieOff = 1;
    g_serverWorld.LoadMapObjects(file, keepIds ? 1 : 0);
    g_loadingMovieOff = 0;
    return true;
}

} // namespace scriptaddon
