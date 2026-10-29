#pragma once

// Small lookup tables found among the object code: text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lib/hash_map.h"


class CStream;

// Text key of a name: h = 5 h + toupper(c) over the bytes (unsigned arithmetic), then
// h % 1000000000 + 1000000000.
int TextKeyFromName(std::string_view name);

// Text keys are 1000000000..1999999999.
bool IsTextKey(int id);

// One "#allod" entry of map.txt.
struct AllodRecord {
    std::string name;           // the "#allod" line's second word
    std::string otherNames[5];  // one word on each of the next five lines
    std::vector<int> maps;    // the indices of its maps
};
// One "#exit" of a map entry (map_database.cpp loads it): the map database's
// record, which the world server copies (SetZoneExits) and sends to the client world.
struct ZoneExit {
    float passTime = 0.0f;          // "#passtime": the time the way takes
    float deploy[4] = {};           // "#deploy": the rectangle the arriving party stands in
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;   // "#remove" (and "#deploy"): the area that leads out
    float angle = 0.0f;             // the area's rotation about its centre (radians)
    std::string target;            // the zone it leads to ("" = none)
    int entrance = 0;               // the target zone's exit the party arrives at
    float view = 0.0f;              // "#view" (radians)
    std::uint8_t hasDeployAngle = 0;   // "#deployangle" given; saved from version 0x68
    float deployAngle = 0.0f;       // saved from version 0x68

    // Whether (x, y) lies inside the exit area (rotated by `angle` about
    // the rectangle's centre when it is not 0).
    bool Contains(float x, float y) const;
    // From / to a stream (core_streams.h): +0x00..+0x20
    // as dwords, the target with its terminator, +0x2C, +0x30, then +0x34 (a byte), +0x38 and the
    // angle; Read takes the last three only from version 0x68 on (else 0).
    void Read(CStream* stream);
    void Write(CStream* stream) const;
};

// One map/zone entry: +0 name, +4 allod name, +8 and +0xC other names.
struct MapRecord {
    std::string name;
    std::string allod;     // (SaveGame keeps the visited zones of the current one's)
    std::string mob;       // the .mob file without extension
    std::string map;       // the .mpr map without extension ("" = none)
    std::string figure;    // the "#figure" line
    std::string picture2;  // the "#maps" second line's second word
    std::string picture;   // its first word, the HUD map's texture
    std::string camera;    // the "#camera" line
    int sizeX = 0;          // "#maps" first line: the zone's size (times 32: the HUD map's world extent)
    int sizeY = 0;
    int kind = 0;           // 0 game, 1 edge, 2 brief (2: the zone has a script folder)
    int climate = 0;      // CWorldServer::SetClimate when the zone starts
    int cave = 0;      // CWorldServer::SetCave
    bool restricted = false;   // "#restrict x y radius": the camera stays in that circle
    float restrictX = 0.0f;
    float restrictY = 0.0f;
    float restrictRadius = 0.0f;
    bool cage = false;      // "#cage": a base where the party does not walk to the people
    std::vector<ZoneExit> exits;
};

// The map database loaded from map.txt / map_LMP.txt (a global).
struct CMapDatabase {
    // Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(std::string_view name);
    // Allod named `name` (_mbsicmp), or null.
    AllodRecord* FindAllod(std::string_view name);
    // Adds the allods and maps of a map.txt-style text file. Returns false if the file is not found.
    bool Load(const char* fileName);
    // Loads "map.txt", then "map-LMP.txt"; returns the second Load's result (WinMain does not read it).
    bool LoadAll();

    std::vector<AllodRecord> allods;
    std::vector<MapRecord> maps;
};

// The game's hash_map<string, float> (the party's and players' variables, the briefing's copies) with std::string
// keys, in game.exe's hash and bucket order (lib::HashMap: the variables are saved and listed in it), with the
// named-variable members the game adds.
struct NamedFloatTable : lib::HashMap<std::string, float, lib::TextHash> {

    // Value stored under `name`, 0 if absent. Builds a string of the name first, as the game does.
    float Get(std::string_view name);
    // Stores `value` under `name`; a zero value (or NaN: the game's equality
    // test) removes the entry instead. Nothing for an empty name.
    void Set(std::string_view name, float value);
    // Removes the entry stored under `name`.
    void Remove(std::string_view name);
    // Empties `out`, then copies into it every entry whose name starts with
    // `prefix` (strncmp over the prefix's length).
    void CopyPrefixed(std::string_view prefix, NamedFloatTable* out);
    // Empties the table, then reads {zero-terminated name, float} pairs from the
    // stream up to an empty name (compared with the exe's empty string).
    void Load(CStream* stream);
    // Writes each entry (name with its zero, then the value), in bucket order, then an empty name.
    void Save(CStream* stream);
};

// Rebirth's PersistentGlobalVars (persistent_globals.cpp): empties `table` but keeps
// its "gvar." entries, set again in the order addon's own container gives them.
void ClearVariablesKeepingGlobals(NamedFloatTable* table);

// The in-game key bindings (config\keyboard.ini; key_bindings.cpp): the action of each scan code
// (0x200 entries, the WM_KEYDOWN flags & 0x1FF; the options window's OK stores its copy there).
inline std::int8_t g_keyActions[512] = /*0079BF70*/ {};

// The keys (0x48 bytes each, 88 of them; key_bindings.cpp's g_keyNames): the options window lists them, and
// the bindings file names them.
struct UiKeyName {
    int scan_;                       // scan code, the index into g_keyActions
    const char* file_;               // the bindings file's name ("TAB", "Q", ...)
    char name_[0x40];                // GetKeyNameTextA's text (up to 31 bytes; LoadKeyBindings)
};
constexpr int kUiKeyNameCount = 88;
extern UiKeyName g_keyNames[kUiKeyNameCount];

// The command bound to `key`: the text of the first key of g_keyNames whose scan code i (0..0x1FF)
// has `key` in g_keyActions, or null.
void* FindCommandByKey(int key);
