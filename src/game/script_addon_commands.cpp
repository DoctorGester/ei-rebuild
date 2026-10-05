// AddNewScriptCommands: addon's commands, plus GSGetPlayerVar and IsFileExists (handlers
// 100115AC-10011703, 10011CC5-10011F43, 100124F4, 10012813-10012C7B, 10012D0F-10012EFB). The game
// functions they call are reached through their original addresses.
#include "script_addon_handlers.h"

#include "console.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "originals_world.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#pragma warning(disable : 4740) // naked forwarders

namespace scriptaddon {

namespace {

void* const kWorld = reinterpret_cast<void*>(0x007AFF60);         // the world server
void* const kClient = reinterpret_cast<void*>(0x007AF5B8);        // the world client
void* const kCamera = reinterpret_cast<void*>(0x007AFEA5);        // the camera script player
void* const kNetwork = reinterpret_cast<void*>(0x007AFE20);
void* const kFlushManipulator = reinterpret_cast<void*>(0x00435000);

__declspec(naked) void* __fastcall WorldGetPlayer(void*, void*, int) EI_ORIGINAL(0x00594180)
// 005911C0: the world's experience award to a player (null: the multiplayer path).
__declspec(naked) void __fastcall WorldAwardExperience(void*, void*, void* /*player*/, float) EI_ORIGINAL(0x005911C0)
// 005942D0 (Loot: the player takes what the body holds) and 005935C0 (removes an object by id).
__declspec(naked) int __fastcall WorldLoot(void*, void*, CEngineObject*, void*) EI_ORIGINAL(0x005942D0)
// 00594AB0: an item made from its name (null when it cannot be).
__declspec(naked) void* __fastcall WorldMakeItem(void*, void*, const char*) EI_ORIGINAL(0x00594AB0)
// 00596030: loads a .mob file's objects (second argument: keep their ids).
__declspec(naked) int __fastcall WorldLoadMapObjects(void*, void*, const char*, int) EI_ORIGINAL(0x00596030)
// 0058EE00: the weather (kind 0 clear, 1 rain, 2 snow, and a strength).
__declspec(naked) void __fastcall WorldSetWeather(void*, void*, int, float) EI_ORIGINAL(0x0058EE00)
// 00566330 on the client: the pause request through the client's session.
__declspec(naked) void __fastcall ClientPause(void*, void*, int) EI_ORIGINAL(0x00566330)
// The camera script player: start (005787F0) and stop (005787D0) saving a camera path.
__declspec(naked) void __fastcall CameraStartSave(void*, void*, const char*) EI_ORIGINAL(0x005787F0)
__declspec(naked) void __fastcall CameraStopSave(void*) EI_ORIGINAL(0x005787D0)
// The player's server side (player +0x20): set a variable (0066EE10, the value's bits), send a
// pair (0066F2C0), send a text (0066D770; the first argument unused), resend the party (0066F450).
__declspec(naked) void __fastcall PlayerSetVariable(void*, void*, const char*, float) EI_ORIGINAL(0x0066EE10)
__declspec(naked) void __fastcall PlayerSendText(void*, void*, int, const char*) EI_ORIGINAL(0x0066D770)
__declspec(naked) void __fastcall PlayerResendParty(void*) EI_ORIGINAL(0x0066F450)
// 00460AF0: a variable of a table (the player's at +0xC4), in ST0.
__declspec(naked) double __fastcall VariablesGet(void*, void*, const char*) EI_ORIGINAL(0x00460AF0)
// 00553380: the unit's data gains experience.
// The unit's brain (data +0x980): 005CB0F0 on its +0xD0, then 005D6630 (entry, index).
__declspec(naked) void __fastcall BrainReset(void*) EI_ORIGINAL(0x005CB0F0)
__declspec(naked) void __fastcall BrainSelect(void*, void*, void*, int) EI_ORIGINAL(0x005D6630)
// Units: add a weapon (00531070; slot or -1), equip armour (00530730), the prototype (00530930),
// mark changed (0050E9F0). Items: the prototype (006A0110), the weapon type (006A07F0).
__declspec(naked) int __fastcall UnitAddWeapon(void*, void*, void*) EI_ORIGINAL(0x00531070)
__declspec(naked) void* __fastcall UnitEquipArmour(void*, void*, void*) EI_ORIGINAL(0x00530730)
__declspec(naked) char* __fastcall UnitPrototype(void*) EI_ORIGINAL(0x00530930)
__declspec(naked) char* __fastcall ItemPrototype(void*) EI_ORIGINAL(0x006A0110)
__declspec(naked) int __fastcall ItemWeaponType(void*) EI_ORIGINAL(0x006A07F0)

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

void* GetPlayer(int number) { return WorldGetPlayer(kWorld, nullptr, number); }
const float* Floats(Call* call) { return call->cmd->floatArgs.data; }
const char* Text(Call* call, int i) { return call->cmd->stringArgs.data[i]; }
CEngineObject* ObjectArg(Call* call, int i) { return call->cmd->objectArgs.data[i]->object; }

// FUCOMPP of 1 (or 0) against the argument: only an ordered equal sets C3 alone.
bool Equals(float constant, float value) { return (Status(constant, value) & 0x45) == 0x40; }

// The player's server-side object of player 0 (game over), or null.
void* FirstPlayerState() {
    void* player = GetPlayer(0);
    return player ? Field<void*>(player, 0x20) : nullptr;
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

// A variable table (NamedFloatTable: STLport hash table, nodes {next, name, value}).
struct VariableNode {
    VariableNode* next;
    char* key;
    float value;
};
struct VariableTable {
    std::uint32_t functors;
    VariableNode** buckets;
    VariableNode** finish;
};

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
    void* player = GetPlayer(number);
    if (!player) return Report("GSSetPlayerVar", "There is no player with number %d", number);
    PlayerSetVariable(Field<void*>(player, 0x20), nullptr, name, value);
    return true;
}

// GSGetPlayerVar(player, name): the player's variable (+0xC4); the result is left alone without
// the player.
bool GSGetPlayerVar(Call* call) {
    int number = Ftol(Floats(call)[0]);
    const char* name = Text(call, 0);
    void* player = GetPlayer(number);
    if (!player) return Report("GSGetPlayerVar", "There is no player with number %d", number);
    *static_cast<float*>(call->result) = static_cast<float>(VariablesGet(static_cast<char*>(player) + 0xC4, nullptr, name));
    return true;
}

// GiveExp(player, value): the world's experience award of `value` to the player (no check that
// the player exists: null takes the multiplayer path).
bool GiveExp(Call* call) {
    void* player = GetPlayer(Ftol(Floats(call)[0]));
    WorldAwardExperience(kWorld, nullptr, player, Floats(call)[1]);
    return true;
}

// GiveUnitExp(unit, value): the unit's data gains the experience; a unit in a party with a
// server-side player also sends it (the value's bits) with the unit's id.
bool GiveUnitExp(Call* call) {
    void* data = UnitData(call->cmd->objectArgs.data[0]);
    if (!data) return InvalidArguments("GiveUnitExp");
    DataGainExperience(data, nullptr, Floats(call)[0]);
    void* unit = Field<void*>(data, 0x1C);
    if (void* party = Field<void*>(unit, 0x24C))
        if (void* state = Field<void*>(party, 0x20)) PlayerSendPair(state, nullptr, Field<int>(unit, 0xC), Floats(call)[0]);
    return true;
}

bool StartCameraSave(Call* call) {
    CameraStartSave(kCamera, nullptr, Text(call, 0));
    return true;
}

bool StopCameraSave(Call*) {
    CameraStopSave(kCamera);
    return true;
}

// ResetTarget(unit) (replaces the game's): the brain's entry at the unit's index byte (+0x9B6)
// is selected again, after 005CB0F0 on the brain's +0xD0.
bool ResetTarget(Call* call) {
    CEngineObject* unit = ObjectArg(call, 0);
    if (!unit || !IsUnit(unit)) return InvalidArguments("ResetTarget");
    char* data = Field<char*>(unit, 0x240);
    int index = Field<signed char>(data, 0x9B6);
    char* brain = data + 0x980;
    BrainReset(brain + 0xD0);
    if (index >= Field<int>(brain, 0x98)) return Report("ResetTarget", "Failed to reset unit %d", unit->id_);
    BrainSelect(brain, nullptr, Field<char*>(brain, 0x94) + index * 0x4C, index);
    return true;
}

bool SendCurrentParty(Call* call) {
    int number = Ftol(Floats(call)[0]);
    void* player = GetPlayer(number);
    if (!player) return Report("SendCurrentParty", "There is no player with number %d", number);
    PlayerResendParty(Field<void*>(player, 0x20));
    return true;
}

// Loot(player, unit): the player loots the dead unit, which is then removed.
bool Loot(Call* call) {
    int number = Ftol(Floats(call)[0]);
    void* player = GetPlayer(number);
    if (!player) return Report("Loot", "There is no player with number %d", number);
    CEngineObject* unit = ObjectArg(call, 0);
    if (!unit) return Report("Loot", "Object doesn't exist");
    if (!IsUnit(unit)) return Report("Loot", "Object must be a unit");
    if (ei::VCall<int>(unit, 0xDC / 4) == 0) return Report("Loot", "Object must be dead");
    WorldLoot(kWorld, nullptr, unit, player);
    WorldRemoveObject(kWorld, nullptr, unit->id_);
    return true;
}

// GameOver / GameOverMSG(title, text): the "gameover" string event to player 0's client.
bool GameOver(Call*) {
    if (void* state = FirstPlayerState()) PlayerSendText(state, nullptr, 0, "gameover");
    return true;
}

bool GameOverMsg(Call* call) {
    if (void* state = FirstPlayerState()) {
        std::string text = std::string("gameover ") + Text(call, 0) + " " + Text(call, 1);
        PlayerSendText(state, nullptr, 0, text.c_str());
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
    void* player = GetPlayer(Ftol(Floats(call)[0]));
    auto* table = reinterpret_cast<VariableTable*>(static_cast<char*>(player) + 0xC4);
    for (auto** bucket = table->buckets; bucket < table->finish; ++bucket)
        for (auto* node = *bucket; node; node = node->next) {
            mfc::CString line;
            line.Format("%s = %f", node->key, static_cast<double>(node->value));
            (console::Out() << static_cast<const char*>(line) << "\n").Apply(kFlushManipulator);
        }
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
    char* item = static_cast<char*>(WorldMakeItem(kWorld, nullptr, name));
    if (override && unit->id_ >= 1000000000) override = false;   // (a warning with EnableShowingScriptErrors)
    if (!item) return Report("GiveUnitWeapon", "Can't create item \"%s\"", name);
    int slot = UnitAddWeapon(unit, nullptr, item);
    if (slot == -1) return true;
    Field<int>(unit, 0x270) = slot;
    if (override) {
        char* weapon = ItemPrototype(item);
        char* data = Field<char*>(unit, 0x240);
        char* prototype = UnitPrototype(unit);
        Store(&Field<float>(data, 0x264), Field<float>(item, 0x50));
        Store(&Field<float>(data, 0x268), Field<float>(item, 0x54));
        volatile float sum = Field<float>(prototype, 0x70) + Field<float>(weapon, 0x88);
        Store(&Field<float>(data, 0x25C), sum);
        Store(&Field<float>(data, 0x250), Field<float>(weapon, 0x8C));
        sum = Field<float>(prototype, 0x68) + Field<float>(weapon, 0x84);
        Store(&Field<float>(data, 0x254), sum);
        Field<float>(data, 0x26C) = static_cast<float>(x87::FromUnsigned(Field<std::uint32_t>(weapon, 0x6C)));
        Field<int>(data, 0x28C) = ItemWeaponType(item);
        for (int i = 0; i < 7; ++i) Store(&Field<float>(data, 0x270 + i * 4), Field<float>(weapon, 0x98 + i * 4));
        UnitMarkChanged(unit, nullptr, 0);
    }
    void* data = Field<void*>(unit, 0x240);
    ei::VCall<void>(data, 0x78 / 4);
    ei::VCall<void>(Field<void*>(unit, 0x240), 0x74 / 4);
    return true;
}

// Override: per value c (7), 0.05 * the head slot's + 0.3 * (the slots 1 and 3) + 0.3 * (the slots
// 1, 3 and 6 second block) + 0.3 * (the slots 2, 4, 5), each plus the prototype's +0x58, into the
// unit's +0x170 block (+0x40 + c * 8 and +0x44 + c * 8).
bool GiveUnitArmor(Call* call) {
    CEngineObject* unit = ObjectArg(call, 0);
    bool override = Equals(1.0f, Floats(call)[0]);
    if (!unit) return Report("GiveUnitArmor", "Unit doesn't exists");
    if (!IsUnit(unit)) return Report("GiveUnitArmor", "Object with ID %d must be a Unit", unit->id_);
    const char* name = Text(call, 0);
    char* item = static_cast<char*>(WorldMakeItem(kWorld, nullptr, name));
    if (override && unit->id_ >= 1000000000) override = false;
    if (!item) return Report("GiveUnitArmor", "Can't create item \"%s\"", name);
    UnitEquipArmour(unit, nullptr, item);
    if (override) {
        char* target = Field<char*>(unit, 0x170) + 0x20;
        char* prototype = UnitPrototype(unit);
        // Four blocks of seven values (A, C, B, D in memory order) and their weights.
        volatile float blocks[4][7] = {};
        const float weights[4] = {0.05f, 0.3f, 0.3f, 0.3f};
        auto addInto = [&](int block, const char* from) {
            for (int c = 0; c < 7; ++c) blocks[block][c] = reinterpret_cast<const float*>(from)[c] + blocks[block][c];
        };
        for (int slot = 0; slot < 7; ++slot) {
            char* equipped = Field<char*>(unit, 0x480 + slot * 4);
            if (!equipped) continue;
            switch (slot) {
            case 0: addInto(0, equipped + 0x4C); break;
            case 1:
            case 3:
                addInto(1, equipped + 0x4C);
                addInto(2, equipped + 0x68);
                break;
            case 2:
            case 4:
            case 5: addInto(3, equipped + 0x4C); break;
            case 6: addInto(2, equipped + 0x4C); break;
            }
        }
        for (int c = 0; c < 7; ++c) {
            volatile float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum = weights[k] * blocks[k][c] + sum;
            volatile float value = Field<float>(prototype, 0x58) + sum;
            Store(&Field<float>(target, 0x20 + c * 8), value);
            value = Field<float>(prototype, 0x58) + sum;
            Store(&Field<float>(target, 0x24 + c * 8), value);
        }
        UnitMarkChanged(unit, nullptr, 0);
    }
    ei::VCall<void>(Field<void*>(unit, 0x240), 0x74 / 4);
    return true;
}

// ShowAllUnits(0) sets the "draw all" flag (0079BE6C), anything else clears it.
bool ShowAllUnits(Call* call) {
    *reinterpret_cast<int*>(0x0079BE6C) = Equals(0.0f, Floats(call)[0]) ? 1 : 0;
    return true;
}

// PauseGame(value): outside network games, the client's pause request with (value == 0).
bool PauseGame(Call* call) {
    if (!IsNetworkClientOriginal()) ClientPause(kClient, nullptr, Equals(0.0f, Floats(call)[0]) ? 1 : 0);
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
    WorldSetWeather(kWorld, nullptr, kind, strength);
    return true;
}

// AddMobEx(file, [keep ids]): loads a .mob file's objects with 007C554C set meanwhile. addon pushes
// its byte flag as a dword whose upper bytes are left over on its stack; here it is 0 or 1.
bool AddMobEx(Call* call) {
    mfc::CString file;
    file = Text(call, 0);
    bool keepIds = call->cmd->floatArgs.size != 0 && Equals(1.0f, Floats(call)[0]);
    *reinterpret_cast<std::uint8_t*>(0x007C554C) = 1;
    int loaded = WorldLoadMapObjects(kWorld, nullptr, file, keepIds ? 1 : 0);
    *reinterpret_cast<std::uint8_t*>(0x007C554C) = 0;
    if (!loaded) return Report("AddMobEx", "Can't load .mob file \"%s\"", static_cast<const char*>(file));
    return true;
}

} // namespace scriptaddon
