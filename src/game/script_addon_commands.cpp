// AddNewScriptCommands: addon's commands, plus GSGetPlayerVar and IsFileExists. The game
// functions they call are the rewritten members.
#include "script_addon_handlers.h"
#include "options.h"
#include "bink_video.h"   // g_loadingMovieOff
#include "world_glue.h"

#include "combat.h"
#include "console.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "game_singletons.h"
#include "settings_file.h"
#include "system_text.h"
#include "lib/str.h"

#include <bit>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace scriptaddon {

namespace {


// The "draw all units" flag (the console's spectator mode).

CPlayer* GetPlayer(int number) { return g_serverWorld->GetPlayer(number); }
const float* Floats(Call* call) { return call->cmd->floatArgs.data(); }
const char* Text(Call* call, int i) { return call->cmd->stringArgs[i].c_str(); }
CEngineObject* ObjectArg(Call* call, int i) { return call->cmd->objectArgs[i]->object; }

// The float bits of `value` as the dword the player messages take.
std::uint32_t Bits(float value) {
    std::uint32_t bits;
    bits = std::bit_cast<std::uint32_t>(value);
    return bits;
}

// 1 (or 0) against the argument: an ordered equal.
bool Equals(float constant, float value) { return constant == value; }

// The server-side object of player 0 (game over), or null.
CPlayerServer* FirstPlayerState() {
    CPlayer* player = GetPlayer(0);
    return player ? player->Server() : nullptr;
}

// The folder IsFileExists looks in: addon's mod folder (100BD184). Without a mod (as here) it is
// HKCU\Software\Gipat.Ru\EI_Starter\Settings "AddonPath", with a '\' added; read once, like addon
// reads it when it loads.
const std::string& ModFolder() {
    static const std::string folder = [] {
        std::string path;
        char value[MAX_PATH * 2];
        DWORD size = sizeof(value) - 1, type = 0;
        if (settings_file::ReadValue("Software\\Gipat.Ru\\EI_Starter\\Settings", "AddonPath", &type, value, &size) == 0 &&
            type == REG_SZ) {
            value[size] = 0;
            path = value;
        }
        if (!path.empty() && path.back() != '\\') path += '\\';
        return path;
    }();
    return folder;
}

bool EndsWith(const std::string& text, const char* suffix) {
    std::size_t n = std::strlen(suffix);
    return n != 0 && text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

} // namespace

// GSSetPlayerVar(player, name, value): the player's server-side variable.
bool GSSetPlayerVar(Call* call) {
    int number = Ftol(Floats(call)[0]);
    float value;
    Store(&value, Floats(call)[1]);
    const char* name = Text(call, 0);
    CPlayer* player = GetPlayer(number);
    if (!player) return Report("GSSetPlayerVar", "There is no player with number %d", number);
    player->Server()->SetVariable(name, Bits(value));
    return true;
}

// GSGetPlayerVar(player, name): the player's variable; the result is left alone without the
// player.
bool GSGetPlayerVar(Call* call) {
    int number = Ftol(Floats(call)[0]);
    const char* name = Text(call, 0);
    CPlayer* player = GetPlayer(number);
    if (!player) return Report("GSGetPlayerVar", "There is no player with number %d", number);
    *call->result.number = static_cast<float>(static_cast<double>(player->Party()->variables_.Get(name)));
    return true;
}

// GiveExp(player, value): the world's experience award of `value` to the player (no check that
// the player exists: null takes the multiplayer path).
bool GiveExp(Call* call) {
    CPlayer* player = GetPlayer(Ftol(Floats(call)[0]));
    g_serverWorld->AwardExperience(player, Floats(call)[1]);
    return true;
}

// GiveUnitExp(unit, value): the unit's data gains the experience; a unit in a party with a
// server-side player also sends it (the value's bits) with the unit's id.
bool GiveUnitExp(Call* call) {
    auto* data = UnitData(call->cmd->objectArgs[0]);
    if (!data) return InvalidArguments("GiveUnitExp");
    data->AddExperience(Floats(call)[0]);
    CUnit* unit = data->unit;
    if (auto* party = unit->player)
        if (CPlayerServer* state = party->Server())
            state->SendPair18(static_cast<std::uint32_t>(unit->id_), Bits(Floats(call)[0]));
    return true;
}

bool StartCameraSave(Call* call) {
    GameStartCameraRecording(Text(call, 0));
    return true;
}

bool StopCameraSave(Call*) {
    GameStopCameraRecording();
    return true;
}

// ResetTarget(unit) (replaces the game's): the brain's alarm record at the brain's alarm slot
// byte is applied again, after the perception memory is emptied.
bool ResetTarget(Call* call) {
    CUnit* unit = UnitOrNull(ObjectArg(call, 0));   // IsUnit's test
    if (!unit) return InvalidArguments("ResetTarget");
    AIBrain* brain = &unit->ServerController()->brain;
    int index = brain->alarmSlot;
    brain->memory.Reset();
    if (index >= std::ssize(brain->alarmRecords)) return Report("ResetTarget", "Failed to reset unit %d", unit->id_);
    brain->ApplyBehaviour(&brain->alarmRecords[index], index);
    return true;
}

bool SendCurrentParty(Call* call) {
    int number = Ftol(Floats(call)[0]);
    CPlayer* player = GetPlayer(number);
    if (!player) return Report("SendCurrentParty", "There is no player with number %d", number);
    player->Server()->ResendParty();
    return true;
}

// Loot(player, unit): the player loots the dead unit, which is then removed.
bool Loot(Call* call) {
    int number = Ftol(Floats(call)[0]);
    CPlayer* player = GetPlayer(number);
    if (!player) return Report("Loot", "There is no player with number %d", number);
    CEngineObject* object = ObjectArg(call, 0);
    if (!object) return Report("Loot", "Object doesn't exist");
    CUnit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("Loot", "Object must be a unit");
    if (unit->IsDead() == 0) return Report("Loot", "Object must be dead");
    g_serverWorld->Loot(unit, player);
    g_serverWorld->RemoveObject(unit->id_);
    return true;
}

// GameOver / GameOverMSG(title, text): the "gameover" string event to player 0's client.
bool GameOver(Call*) {
    if (CPlayerServer* state = FirstPlayerState()) state->SendText13(0, "gameover");
    return true;
}

bool GameOverMsg(Call* call) {
    if (CPlayerServer* state = FirstPlayerState()) {
        std::string text = std::string("gameover ") + Text(call, 0) + " " + Text(call, 1);
        state->SendText13(0, text);
    }
    return true;
}

// IsFileExists(path): a file (not a folder) under the mod folder; with '\' read as '/', a path
// that is "..", contains "/../", starts with "../" or ends with "/.." is refused (the result is
// then left alone).
bool IsFileExists(Call* call) {
    std::string path = ModFolder();
    std::string name = Text(call, 0);
    for (char& c : name)
        if (c == '\\') c = '/';
    bool forbidden = name == ".." || name.find("/../") != std::string::npos || name.compare(0, 3, "../") == 0 ||
                     EndsWith(name, "/..");
    if (forbidden) return Report("IsFileExists", "'..' string literal is forbidden");
    path += name;
    DWORD attributes = text::utf8::GetFileAttributesA(path.c_str());
    Store(call->result.number, attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1.0f : 0.0f);
    return true;
}

// GSListPlayerVars(player): "<name> = <value>" for each of the player's variables, in the
// table's order (no check for the player).
bool GSListPlayerVars(Call* call) {
    CPlayer* player = GetPlayer(Ftol(Floats(call)[0]));
    const NamedFloatTable& variables = player->Party()->variables_;
    for (auto node = variables.begin(); node != variables.end(); ++node) {
        std::string line;
        line = str::Format("%s = %f", node->first, static_cast<double>(node->second));
        (console::g_consoleOut << line << "\n").Flush();
    }
    return true;
}

// GiveUnitWeapon(unit, item, override) / GiveUnitArmor: the item made from its name goes to the
// unit; with override 1 (only for units without a text-key id) the unit's stats are set from it.
bool GiveUnitWeapon(Call* call) {
    CEngineObject* object = ObjectArg(call, 0);
    bool override = Equals(1.0f, Floats(call)[0]);
    if (!object) return Report("GiveUnitWeapon", "Unit doesn't exists");
    CUnit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("GiveUnitWeapon", "Object with ID %d must be a Unit", object->id_);
    const char* name = Text(call, 0);
    auto* item = AsWeapon(g_serverWorld->CreateItemNamed(name));
    if (override && unit->id_ >= 1000000000) override = false;   // (a warning with EnableShowingScriptErrors)
    if (!item) return Report("GiveUnitWeapon", "Can't create item \"%s\"", name);
    int slot = unit->AddWeapon(item);
    if (slot == -1) return true;
    unit->pendingWeapon = slot;
    if (override) {
        const auto* weapon = itemdb::RecordAs<itemdb::WeaponRecord>(item->GetPrototype());
        UnitStats& stats = unit->ServerController()->stats;
        const UnitPrototypeRecord* prototype = unit->GetPrototype();
        Store(&stats.damage, item->damage_);
        Store(&stats.damageSpread, item->damageSpread_);
        float sum = prototype->combat[3] + weapon->defenceBonus;
        Store(&stats.combat[UnitStats::kDefence], sum);
        float reach;
        reach = weapon->reach;
        Store(&stats.combat[UnitStats::kReach], reach);
        sum = prototype->combat[1] + weapon->attackBonus;
        Store(&stats.combat[UnitStats::kAttack], sum);
        stats.attackCategory = static_cast<float>(x87::FromUnsigned(weapon->category));
        stats.naturalWeapon = item->GetType();
        for (int i = 0; i < 7; ++i) Store(&stats.damageWeights[i], weapon->damageTypes[i]);
        unit->MarkChanged(0);
    }
    unit->Controller()->RefreshModel();
    unit->Controller()->RefreshEquipment();
    return true;
}

// Override: per value c (7), 0.05 * the head slot's + 0.3 * (armour slots 1 and 3) + 0.3 * (the armour slots
// 1, 3 and 6 second block) + 0.3 * (armour slots 2, 4, 5), each plus the prototype's protection
// scale, into both halves of the unit's server-side protection pair c.
bool GiveUnitArmor(Call* call) {
    CEngineObject* object = ObjectArg(call, 0);
    bool override = Equals(1.0f, Floats(call)[0]);
    if (!object) return Report("GiveUnitArmor", "Unit doesn't exists");
    CUnit* unit = UnitOrNull(object);   // IsUnit's test
    if (!unit) return Report("GiveUnitArmor", "Object with ID %d must be a Unit", object->id_);
    const char* name = Text(call, 0);
    CItem* item = g_serverWorld->CreateItemNamed(name);
    if (override && unit->id_ >= 1000000000) override = false;
    if (!item) return Report("GiveUnitArmor", "Can't create item \"%s\"", name);
    unit->EquipArmour(item);
    if (override) {
        UnitStatPair* protection = unit->ServerCompanion()->vitals.protection;
        const UnitPrototypeRecord* prototype = unit->GetPrototype();
        // Four blocks of seven values (A, C, B, D in memory order) and their weights.
        float blocks[4][7] = {};
        const float weights[4] = {0.05f, 0.3f, 0.3f, 0.3f};
        auto addInto = [&](int block, const float* from) {
            for (int c = 0; c < 7; ++c) blocks[block][c] = from[c] + blocks[block][c];
        };
        for (int slot = 0; slot < 7; ++slot) {
            CItemArmor* equipped = unit->armor[slot];
            if (!equipped) continue;
            switch (slot) {
            case 0: addInto(0, equipped->absorb_[0]); break;
            case 1:
            case 3:
                addInto(1, equipped->absorb_[0]);
                addInto(2, equipped->absorb_[1]);
                break;
            case 2:
            case 4:
            case 5: addInto(3, equipped->absorb_[0]); break;
            case 6: addInto(2, equipped->absorb_[0]); break;
            }
        }
        for (int c = 0; c < 7; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum = weights[k] * blocks[k][c] + sum;
            float value = prototype->protectionScale + sum;
            Store(&protection[c].base, value);
            value = prototype->protectionScale + sum;
            Store(&protection[c].current, value);
        }
        unit->MarkChanged(0);
    }
    unit->Controller()->RefreshEquipment();
    return true;
}

// ShowAllUnits(0) sets the "draw all" flag, anything else clears it.
bool ShowAllUnits(Call* call) {
    options::g_options->drawVisibleOnly_ = Equals(0.0f, Floats(call)[0]) ? 1 : 0;
    return true;
}

// PauseGame(value): outside network games, the client's pause request with (value == 0).
bool PauseGame(Call* call) {
    if (!IsMultiplayerClient()) g_clientWorld->SendFlag10(Equals(0.0f, Floats(call)[0]) ? 1 : 0);
    return true;
}

// SetWeather("rain" | "clear" | "snow"): kind 1 / 0 / 2 with strength 20 / 20 / 40; anything
// else is clear at 20.
bool SetWeather(Call* call) {
    std::string name;
    name = Text(call, 0);
    int kind = 0;
    float strength = 20.0f;
    if (str::Compare(name, "rain") == 0) kind = 1;
    else if (str::Compare(name, "clear") == 0) kind = 0;
    else if (str::Compare(name, "snow") == 0) {
        kind = 2;
        strength = 40.0f;
    }
    g_serverWorld->SetWeather(kind, strength);
    return true;
}

// AddMobEx(file, [keep ids]): loads a .mob file's objects with the loading movie off meanwhile.
// addon pushes its byte flag as a 4-byte value whose upper bytes are left over on its stack; here it is
// 0 or 1.
bool AddMobEx(Call* call) {
    std::string file;
    file = Text(call, 0);
    bool keepIds = std::ssize(call->cmd->floatArgs) != 0 && Equals(1.0f, Floats(call)[0]);
    g_loadingMovieOff = 1;
    int loaded = g_serverWorld->LoadMapObjects(file, keepIds ? 1 : 0);
    g_loadingMovieOff = 0;
    if (!loaded) return Report("AddMobEx", "Can't load .mob file \"%s\"", file.c_str());
    return true;
}

} // namespace scriptaddon
