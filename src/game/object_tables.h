#pragma once

// Small lookup tables found among the object code: text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "lib/mfc.h"
#include "lib/hash_map.h"
#include "game_data.h"

using BOOL = int;

class CStream;

// Text key of a name: h = 5 h + toupper(c) over the bytes (unsigned arithmetic), then
// h % 1000000000 + 1000000000. __fastcall (name in ECX).
int TextKeyFromName(const char* name);

// Text keys are 1000000000..1999999999. __fastcall (id in ECX).
BOOL IsTextKey(int id);

// One "#allod" entry of map.txt.
struct AllodRecord {
    std::string name;           // the "#allod" line's second word
    std::string otherNames[5];  // one word on each of the next five lines
    mfc::CArray<int> maps;    // CDWordArray: the indices of its maps
};
// The allods' elements (map_database.cpp): zero-filled, the strings empty and the map indices an empty
// CDWordArray; destroyed the indices first, then the strings from the last.
struct AllodRecordElements {
    static void Construct(AllodRecord* allods, int count);
    static void Destruct(AllodRecord* allods, int count);
    static void Relocate(AllodRecord* to, AllodRecord* from, int count) { mfc::MoveElements(to, from, count); }
};

// One "#exit" of a map entry (map_database.cpp loads it): the map database's
// record, which the world server copies (SetZoneExits) and sends to the client world.
struct ZoneExit {
    float passTime;                 // "#passtime": the time the way takes
    float deploy[4];                // "#deploy": the rectangle the arriving party stands in
    float x1, y1, x2, y2;           // "#remove" (and "#deploy"): the area that leads out
    float angle;                    // the area's rotation about its centre (radians)
    std::string target;            // the zone it leads to ("" = none)
    int entrance;                   // the target zone's exit the party arrives at
    float view;                     // "#view" (radians)
    std::uint8_t hasDeployAngle;    // "#deployangle" given; saved from version 0x68
    std::uint8_t unused_35[3];
    float deployAngle;              // saved from version 0x68

    // Whether (x, y) lies inside the exit area (rotated by `angle` about
    // the rectangle's centre when it is not 0).
    bool Contains(float x, float y) const;
    // From / to a stream (core_streams.h): +0x00..+0x20
    // as dwords, the target with its terminator, +0x2C, +0x30, then +0x34 (a byte), +0x38 and the
    // angle; Read takes the last three only from version 0x68 on (else 0).
    void Read(CStream* stream);
    void Write(CStream* stream) const;
    // (zone::ExitDelete in game_arrays.h: the scalar deleting destructor.) The compiler's operator=:
    // +0x00..+0x24 as dwords, the target, +0x2C, +0x30, the byte +0x34 and +0x38; returns this.
    ZoneExit* Assign(const ZoneExit* from);
};
static_assert(offsetof(ZoneExit, x1) == 0x14 && offsetof(ZoneExit, target) == 0x28);

namespace zone {
void ExitDestruct(ZoneExit* self);   // zone_records.cpp
} // namespace zone

// The exits' ConstructElements (zero filled, the target the empty string) and
// DestructElements (each exit's destructor).
struct ZoneExitElements {
    static void Construct(ZoneExit* p, int n) {
        mfc::PlainElements<ZoneExit>::Construct(p, n);
        for (; n--; ++p) std::construct_at(&p->target);
    }
    static void Destruct(ZoneExit* p, int n) {
        for (; n--; ++p) zone::ExitDestruct(p);
    }
    static void Relocate(ZoneExit* to, ZoneExit* from, int n) { mfc::MoveElements(to, from, n); }
};

// MFC CArray<ZoneExit> (a map record's +0x48, CWorldServer + 0xEC, CWorldClient + 0x8C). Its SetSize
// is the instantiation's own (gamearrays::Exits; new records are constructed).
struct ZoneExits : mfc::CArray<ZoneExit, ZoneExitElements> {
    // By-value GetAt: element `index` copied into `out`
    // (the fields, the target string copy-constructed). Returns out.
    ZoneExit* CopyAt(ZoneExit* out, int index) const;
};
static_assert(sizeof(ZoneExits) == 0x14);

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
    int sizeX;              // "#maps" first line: the zone's size (times 32: the HUD map's world extent)
    int sizeY;
    int kind;               // 0 game, 1 edge, 2 brief (2: the zone has a script folder)
    int setting2C;          // CWorldServer::SetClimate when the zone starts
    int setting30;          // CWorldServer::SetCave
    bool restricted;        // "#restrict x y radius": the camera stays in that circle
    std::uint8_t unused_35[3];
    float restrictX;
    float restrictY;
    float restrictRadius;
    bool cage;              // "#cage": a base where the party does not walk to the people
    std::uint8_t unused_45[3];
    ZoneExits exits;
};
// The maps' elements (map_database.cpp): zero-filled, the strings empty and the exits an empty array with the
// map record items' table; destroyed by the record's destructor (zone::ZoneRecordDestruct).
struct MapRecordElements {
    static void Construct(MapRecord* maps, int count);
    static void Destruct(MapRecord* maps, int count);
    static void Relocate(MapRecord* to, MapRecord* from, int count) { mfc::MoveElements(to, from, count); }
};

// The map database loaded from map.txt / map_LMP.txt (a global).
struct CMapDatabase {
    // Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(const char* name);
    // Allod named `name` (_mbsicmp), or null.
    AllodRecord* FindAllod(const char* name);
    // Adds the allods and maps of a map.txt-style text file. Returns false if the file is not found.
    bool Load(const char* fileName);
    // Loads "map.txt", then "map-LMP.txt"; returns the second Load's result (WinMain does not read it).
    bool LoadAll();
    // The constructor: the two arrays empty with their vtables (run by a static initialiser). Returns this.
    CMapDatabase* Construct();
    // The maps' array (its vtable back, each record's destructor, the storage freed), then the allods' (each
    // allod's map indices freed, its six strings from the last), as the compiler wrote it.
    void Destruct();

    mfc::CArray<AllodRecord, AllodRecordElements> allods;
    mfc::CArray<MapRecord, MapRecordElements> maps;
};
static_assert(offsetof(CMapDatabase, maps) == 0x14);
static_assert(sizeof(CMapDatabase) == 0x28);

// The game's hash_map<CString, float> (the party's and players' variables, the briefing's copies) with std::string
// keys, in game.exe's hash and bucket order (lib::HashMap: the variables are saved and listed in it), with the
// named-variable members the game adds.
struct NamedFloatTable : lib::HashMap<std::string, float, lib::TextHash> {

    // Value stored under `name`, 0 if absent. Builds a string of the name first, as the game does.
    float Get(const char* name);
    // Stores `value` under `name`; a zero value (or NaN: the game's FCOMP
    // test) removes the entry instead. Nothing for an empty name.
    void Set(const char* name, float value);
    // Removes the entry stored under `name`.
    void Remove(const char* name);
    // Empties `out`, then copies into it every entry whose name starts with
    // `prefix` (strncmp over the prefix's length).
    void CopyPrefixed(const char* prefix, NamedFloatTable* out);
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
static_assert(sizeof(UiKeyName) == 0x48);
constexpr int kUiKeyNameCount = 88;
extern UiKeyName g_keyNames[kUiKeyNameCount];

// The command bound to `key`: the text of the first key of g_keyNames whose scan code i (0..0x1FF)
// has `key` in g_keyActions, or null. __fastcall (key in ECX).
void* FindCommandByKey(int key);
