// AddNewScriptCommands: addon's commands, plus GSGetPlayerVar and IsFileExists (handlers
// 100115AC-10011703, 10011CC5-10011F43, 100124F4, 10012813-10012C7B, 10012D0F-10012EFB). The game
// functions they call are the rewritten members, or their original addresses where not rewritten.
#include "script_addon_handlers.h"

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
#include "lib/mfc.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_engine.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#pragma warning(disable : 4740) // naked forwarders

namespace scriptaddon {

namespace {

// The camera script player (the client glue object, not read): start (005787F0) and stop
// (005787D0) saving a camera path.
__declspec(naked) void __fastcall CameraStartSave(void*, void*, const char*) EI_ORIGINAL(0x005787F0)
__declspec(naked) void __fastcall CameraStopSave(void*) EI_ORIGINAL(0x005787D0)

// 0079BE6C: the "draw all units" flag (the console's spectator mode).
int& DrawAllUnits() { return *reinterpret_cast<int*>(0x0079BE6C); }
// 007C554C: the loading screen's movie is off (LoadingMovie::off, bink_video.cpp).
std::uint8_t& LoadingMovieOff() { return *reinterpret_cast<std::uint8_t*>(0x007C554C); }

WorldPlayer* GetPlayer(int number) { return WorldServer()->GetPlayer(number); }
const float* Floats(Call* call) { return call->cmd->floatArgs.data; }
const char* Text(Call* call, int i) { return call->cmd->stringArgs.data[i]; }
CEngineObject* ObjectArg(Call* call, int i) { return call->cmd->objectArgs.data[i]->object; }

CUnit* AsCUnit(CEngineObject* unit) { return static_cast<CUnit*>(unit); }
CUnitServer* ServerOf(CEngineObject* unit) { return static_cast<CUnitServer*>(AsCUnit(unit)->controller); }
const UnitPrototypeRecord* PrototypeOf(CEngineObject* unit) {
    return static_cast<const UnitPrototypeRecord*>(UnitPrototypeOf(unit));
}

// The float bits of `value` as the dword the player messages take.
std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// FUCOMPP of 1 (or 0) against the argument: only an ordered equal sets C3 alone.
bool Equals(float constant, float value) { return (Status(constant, value) & 0x45) == 0x40; }

// The server-side object of player 0 (game over), or null.
CPlayerServer* FirstPlayerState() {
    WorldPlayer* player = GetPlayer(0);
    return player ? player->server : nullptr;
}

// The folder IsFileExists looks in: addon's mod folder (100BD184). Without a mod (as here) it is
// HKCU\Software\Gipat.Ru\EI_Starter\Settings "AddonPath", with a '\' added; read once, like addon
// reads it when it loads.
const std::string& ModFolder() {
    static const std::string folder = [] {
        std::string path;
        HKEY key;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Gipat.Ru\\EI_Starter\\Settings", 0, KEY_READ, &key) == 0) {
            char value[MAX_PATH * 2];
            DWORD size = sizeof(value) - 1, type = 0;
            if (RegQueryValueExA(key, "AddonPath", nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == 0 &&
                type == REG_SZ) {
                value[size] = 0;
                path = value;
            }
            RegCloseKey(key);
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
    WorldPlayer* player = GetPlayer(number);
    if (!player) return Report("GSSetPlayerVar", "There is no player with number %d", number);
    player->server->SetVariable(name, Bits(value));
    return true;
}

// GSGetPlayerVar(player, name): the player's variable; the result is left alone without the
// player.
bool GSGetPlayerVar(Call* call) {
    int number = Ftol(Floats(call)[0]);
    const char* name = Text(call, 0);
    WorldPlayer* player = GetPlayer(number);
    if (!player) return Report("GSGetPlayerVar", "There is no player with number %d", number);
    *static_cast<float*>(call->result) = static_cast<float>(static_cast<double>(NamedTableGet(player->variables, nullptr, name)));
    return true;
}

// GiveExp(player, value): the world's experience award of `value` to the player (no check that
// the player exists: null takes the multiplayer path).
bool GiveExp(Call* call) {
    WorldPlayer* player = GetPlayer(Ftol(Floats(call)[0]));
    WorldServer()->AwardExperience(player, Floats(call)[1]);
    return true;
}

// GiveUnitExp(unit, value): the unit's data gains the experience; a unit in a party with a
// server-side player also sends it (the value's bits) with the unit's id.
bool GiveUnitExp(Call* call) {
    auto* data = static_cast<CUnitServer*>(UnitData(call->cmd->objectArgs.data[0]));
    if (!data) return InvalidArguments("GiveUnitExp");
    static_cast<CUnitServer*>(data)->AddExperience(Floats(call)[0]);
    CUnit* unit = data->unit;
    if (auto* party = reinterpret_cast<WorldPlayer*>(unit->player))
        if (CPlayerServer* state = party->server)
            state->SendPair18(static_cast<std::uint32_t>(unit->id_), Bits(Floats(call)[0]));
    return true;
}

bool StartCameraSave(Call* call) {
    CameraStartSave(ClientGlueObject(), nullptr, Text(call, 0));
    return true;
}

bool StopCameraSave(Call*) {
    CameraStopSave(ClientGlueObject());
    return true;
}

// ResetTarget(unit) (replaces the game's): the brain's alarm record at the brain's alarm slot
// byte (+0x36) is applied again, after the perception memory is emptied.
bool ResetTarget(Call* call) {
    CEngineObject* unit = ObjectArg(call, 0);
    if (!unit || !IsUnit(unit)) return InvalidArguments("ResetTarget");
    AIBrain* brain = ServerOf(unit)->Brain();
    int index = brain->alarmSlot;
    brain->ResetMemory();
    if (index >= brain->alarmRecords.size) return Report("ResetTarget", "Failed to reset unit %d", unit->id_);
    brain->ApplyBehaviour(&brain->alarmRecords.data[index], index);
    return true;
}

bool SendCurrentParty(Call* call) {
    int number = Ftol(Floats(call)[0]);
    WorldPlayer* player = GetPlayer(number);
    if (!player) return Report("SendCurrentParty", "There is no player with number %d", number);
    static_cast<CPlayerServer*>(player->server)->ResendParty();
    return true;
}

// Loot(player, unit): the player loots the dead unit, which is then removed.
bool Loot(Call* call) {
    int number = Ftol(Floats(call)[0]);
    WorldPlayer* player = GetPlayer(number);
    if (!player) return Report("Loot", "There is no player with number %d", number);
    CEngineObject* unit = ObjectArg(call, 0);
    if (!unit) return Report("Loot", "Object doesn't exist");
    if (!IsUnit(unit)) return Report("Loot", "Object must be a unit");
    if (ObjectSlots(unit)->IsDead() == 0) return Report("Loot", "Object must be dead");
    WorldServer()->Loot(unit, player);
    WorldRemoveObject(WorldServer(), nullptr, unit->id_);
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
        state->SendText13(0, text.c_str());
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
    DWORD attributes = GetFileAttributesA(path.c_str());
    Store(static_cast<float*>(call->result), attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) ? 1.0f : 0.0f);
    return true;
}

// GSListPlayerVars(player): "<name> = <value>" for each of the player's variables, in the
// table's order (no check for the player).
bool GSListPlayerVars(Call* call) {
    WorldPlayer* player = GetPlayer(Ftol(Floats(call)[0]));
    player->Party()->variables_.ForEach([](const PartyVariableMap::Node* node) {
        mfc::CString line;
        line.Format("%s = %f", static_cast<const char*>(node->key), static_cast<double>(node->value));
        (console::Out() << static_cast<const char*>(line) << "\n").Flush();
    });
    return true;
}

// GiveUnitWeapon(unit, item, override) / GiveUnitArmor: the item made from its name goes to the
// unit; with override 1 (only for units without a text-key id) the unit's stats are set from it.
bool GiveUnitWeapon(Call* call) {
    CEngineObject* unit = ObjectArg(call, 0);
    bool override = Equals(1.0f, Floats(call)[0]);
    if (!unit) return Report("GiveUnitWeapon", "Unit doesn't exists");
    if (!IsUnit(unit)) return Report("GiveUnitWeapon", "Object with ID %d must be a Unit", unit->id_);
    const char* name = Text(call, 0);
    auto* item = static_cast<CItemWeapon*>(WorldServer()->CreateItemNamed(name));
    if (override && unit->id_ >= 1000000000) override = false;   // (a warning with EnableShowingScriptErrors)
    if (!item) return Report("GiveUnitWeapon", "Can't create item \"%s\"", name);
    int slot = UnitAddWeaponOriginal(unit, nullptr, item);
    if (slot == -1) return true;
    AsCUnit(unit)->pendingWeapon = slot;
    if (override) {
        auto* weapon = reinterpret_cast<const itemdb::WeaponRecord*>(item->GetPrototype());
        CombatStats& stats = ServerOf(unit)->Combat();
        const UnitPrototypeRecord* prototype = PrototypeOf(unit);
        Store(&stats.damage, item->damage_);
        Store(&stats.damageSpread, item->damageSpread_);
        float sum = prototype->combat[3] + weapon->defenceBonus;
        Store(&stats.defence, sum);
        float reach;
        std::memcpy(&reach, &weapon->reach, 4);
        Store(&stats.reach, reach);
        sum = prototype->combat[1] + weapon->attackBonus;
        Store(&stats.attack, sum);
        stats.attackCategory = static_cast<float>(x87::FromUnsigned(weapon->category));
        stats.naturalWeapon = item->GetType();
        for (int i = 0; i < 7; ++i) Store(&stats.damageWeights[i], weapon->damageTypes[i]);
        UnitMarkChanged(unit, nullptr, 0);
    }
    ServerSlots(ServerOf(unit))->RefreshModel();
    ServerSlots(ServerOf(unit))->RefreshEquipment();
    return true;
}

// Override: per value c (7), 0.05 * the head slot's + 0.3 * (the slots 1 and 3) + 0.3 * (the slots
// 1, 3 and 6 second block) + 0.3 * (the slots 2, 4, 5), each plus the prototype's protection
// scale, into both halves of the unit's server-side protection pair c.
bool GiveUnitArmor(Call* call) {
    CEngineObject* unit = ObjectArg(call, 0);
    bool override = Equals(1.0f, Floats(call)[0]);
    if (!unit) return Report("GiveUnitArmor", "Unit doesn't exists");
    if (!IsUnit(unit)) return Report("GiveUnitArmor", "Object with ID %d must be a Unit", unit->id_);
    const char* name = Text(call, 0);
    CEngineObject* item = WorldServer()->CreateItemNamed(name);
    if (override && unit->id_ >= 1000000000) override = false;
    if (!item) return Report("GiveUnitArmor", "Can't create item \"%s\"", name);
    UnitEquipArmourOriginal(unit, nullptr, item);
    if (override) {
        UnitStatPair* protection = static_cast<CMapObjectServer*>(AsCUnit(unit)->companion)->vitals.protection;
        const UnitPrototypeRecord* prototype = PrototypeOf(unit);
        // Four blocks of seven values (A, C, B, D in memory order) and their weights.
        float blocks[4][7] = {};
        const float weights[4] = {0.05f, 0.3f, 0.3f, 0.3f};
        auto addInto = [&](int block, const float* from) {
            for (int c = 0; c < 7; ++c) blocks[block][c] = from[c] + blocks[block][c];
        };
        for (int slot = 0; slot < 7; ++slot) {
            auto* equipped = static_cast<CItemArmor*>(AsCUnit(unit)->armor[slot]);
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
        UnitMarkChanged(unit, nullptr, 0);
    }
    ServerSlots(ServerOf(unit))->RefreshEquipment();
    return true;
}

// ShowAllUnits(0) sets the "draw all" flag, anything else clears it.
bool ShowAllUnits(Call* call) {
    DrawAllUnits() = Equals(0.0f, Floats(call)[0]) ? 1 : 0;
    return true;
}

// PauseGame(value): outside network games, the client's pause request with (value == 0).
bool PauseGame(Call* call) {
    if (!IsNetworkClientOriginal()) WorldClient()->SendFlag10(Equals(0.0f, Floats(call)[0]) ? 1 : 0);
    return true;
}

// SetWeather("rain" | "clear" | "snow"): kind 1 / 0 / 2 with strength 20 / 20 / 40; anything
// else is clear at 20.
bool SetWeather(Call* call) {
    mfc::CString name;
    name = Text(call, 0);
    int kind = 0;
    float strength = 20.0f;
    if (std::strcmp(name, "rain") == 0) kind = 1;
    else if (std::strcmp(name, "clear") == 0) kind = 0;
    else if (std::strcmp(name, "snow") == 0) {
        kind = 2;
        strength = 40.0f;
    }
    WorldServer()->SetWeather(kind, strength);
    return true;
}

// AddMobEx(file, [keep ids]): loads a .mob file's objects with the loading movie off meanwhile.
// addon pushes its byte flag as a dword whose upper bytes are left over on its stack; here it is
// 0 or 1.
bool AddMobEx(Call* call) {
    mfc::CString file;
    file = Text(call, 0);
    bool keepIds = call->cmd->floatArgs.size != 0 && Equals(1.0f, Floats(call)[0]);
    LoadingMovieOff() = 1;
    int loaded = WorldServer()->LoadMapObjects(file, keepIds ? 1 : 0);
    LoadingMovieOff() = 0;
    if (!loaded) return Report("AddMobEx", "Can't load .mob file \"%s\"", static_cast<const char*>(file));
    return true;
}

} // namespace scriptaddon
