#pragma once

// Small lookup tables found among the object code (00440000-00470000): text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>

#include "lib/mfc.h"

using BOOL = int;

class CStream;

// 00461360. Text key of a name: h = 5 h + toupper(c) over the bytes (unsigned arithmetic), then
// h % 1000000000 + 1000000000. __fastcall (name in ECX).
int __fastcall TextKeyFromName(const char* name);

// 004613A0. Text keys are 1000000000..1999999999. __fastcall (id in ECX).
BOOL __fastcall IsTextKey(int id);

// One "#allod" entry of map.txt (0x2C bytes; constructed by 00460720).
struct AllodRecord {
    char* name;               // CString: the "#allod" line's second word
    char* otherNames[5];      // +0x04 CStrings: one word on each of the next five lines
    mfc::CArray<int> maps;    // +0x18 CDWordArray (vtable 0073C3D0): the indices of its maps
};
static_assert(offsetof(AllodRecord, maps) == 0x18 && sizeof(AllodRecord) == 0x2C);

// One "#exit" of a map entry (0x3C bytes; map_database.h has the loader's view).
struct MapExit {
    float passTime;         // +0x00 "#passtime": the time the way takes
    float deploy[4];        // +0x04
    float remove[4];        // +0x14
    float angle;            // +0x24
    char* target;           // +0x28 CString: the zone it leads to
    int targetExit;         // +0x2C the target zone's exit the party arrives at
    float view;             // +0x30
    bool hasDeployAngle;    // +0x34
    std::uint8_t unused_35[3];
    float deployAngle;      // +0x38
};
static_assert(offsetof(MapExit, target) == 0x28);
static_assert(sizeof(MapExit) == 0x3C);

// One map/zone entry (0x5C bytes): +0 name, +4 allod name, +8 and +0xC other names.
struct MapRecord {
    char* name;   // CString
    char* allod;  // +0x04 CString (SaveGame keeps the visited zones of the current one's)
    char* mob;    // +0x08 CString: the .mob file without extension
    char* map;    // +0x0C CString: the .mpr map without extension ("" = none)
    char* figure;           // +0x10 CString: the "#figure" line
    char* picture2;         // +0x14 CString: the "#maps" second line's second word
    char* picture;          // +0x18 CString: its first word, the HUD map's texture (00641F20)
    char* camera;           // +0x1C CString: the "#camera" line
    int sizeX;              // +0x20 "#maps" first line: the zone's size (times 32: the HUD map's world extent)
    int sizeY;              // +0x24
    int kind;               // +0x28 0 game, 1 edge, 2 brief (2: the zone has a script folder, 00596350)
    int setting2C;          // +0x2C CWorldServer::SetClimate when the zone starts
    int setting30;          // +0x30 CWorldServer::SetCave
    bool restricted;        // +0x34 "#restrict x y radius": the camera stays in that circle
    std::uint8_t unused_35[3];
    float restrictX;        // +0x38
    float restrictY;        // +0x3C
    float restrictRadius;   // +0x40
    bool cage;              // +0x44 "#cage": a base where the party does not walk to the people
    std::uint8_t unused_45[3];
    mfc::CArray<MapExit> exits;   // +0x48
};
static_assert(offsetof(MapRecord, kind) == 0x28);
static_assert(offsetof(MapRecord, map) == 0x0C);
static_assert(offsetof(MapRecord, setting30) == 0x30);
static_assert(offsetof(MapRecord, exits) == 0x48);
static_assert(offsetof(MapRecord, restricted) == 0x34);
static_assert(offsetof(MapRecord, cage) == 0x44);
static_assert(sizeof(MapRecord) == 0x5C);

// The map database loaded from map.txt / map_LMP.txt (global at 0079BF00; constructor 0045E820,
// loader 0045EA20).
struct CMapDatabase {
    // 0045E850. Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(const char* name);
    // 0045E910. Allod named `name` (_mbsicmp), or null.
    AllodRecord* FindAllod(const char* name);
    // 0045EA20 (map_database.cpp). Adds the allods and maps of a map.txt-style text file. Returns
    // false if the file is not found.
    bool Load(const char* fileName);
    // 0045EA00 (map_database.cpp, A-E1). Loads "map.txt", then "map-LMP.txt"; returns the second
    // Load's result (WinMain does not read it).
    bool LoadAll();
    // 0045E820 (map_database.cpp, A-E1). The constructor: the two arrays empty with their vtables.
    // It runs from the static initialiser 0045E6C0 on the database at 0079BF00, before the hooks
    // are installed, so the hook on it never fires. Returns this.
    CMapDatabase* Construct();
    // 0045E6F0 (database_destruct.cpp, S3c; the exit function 0045E6E0). The maps' array (its
    // vtable back, each record's destructor 00460970, the storage freed), then the allods' (each
    // allod's map indices freed, its six strings from the last), as the compiler wrote it.
    void Destruct();
    static constexpr std::uintptr_t kAllodArrayVtable = std::uintptr_t(0x0073C3DC);
    static constexpr std::uintptr_t kMapArrayVtable = std::uintptr_t(0x0073C3E8);

    mfc::CArray<AllodRecord> allods;   // +0x00
    mfc::CArray<MapRecord> maps;       // +0x14
};
static_assert(offsetof(CMapDatabase, maps) == 0x14);
static_assert(sizeof(CMapDatabase) == 0x28);

// The game's map database (0079BF00).
inline CMapDatabase* GameMapDatabase() { return reinterpret_cast<CMapDatabase*>(0x0079BF00); }

// An STLport hash_map<CString, float> (hash __stl_hash_string, nodes {next, key, value}).
struct NamedFloatTable {
    struct Node {
        Node* next;
        char* key;    // CString
        float value;
    };

    // 00460AF0. Value stored under `name` (_mbscmp), 0 if absent. Builds a CString of the name
    // first, as the original does.
    float Get(const char* name);
    // 00460BC0 (RET 8). Stores `value` under `name`; a zero value (or NaN: the original's FCOMP
    // test) removes the entry instead. Nothing for an empty name.
    void Set(const char* name, float value);
    // 00460DB0 (RET 4). Removes the entry stored under `name`.
    void Remove(const char* name);
    // 00460EE0 (RET 8). Empties `out`, then copies into it every entry whose name starts with
    // `prefix` (strncmp over the prefix's length).
    void CopyPrefixed(const char* prefix, NamedFloatTable* out);
    // 00460FE0 (RET 4). Empties the table, then reads {zero-terminated name, float} pairs from the
    // stream up to an empty name (compared with the exe's empty string, 0079B298).
    void Load(CStream* stream);
    // 00461260 (RET 4). Writes each entry (name with its zero, then the value), in bucket order,
    // then an empty name.
    void Save(CStream* stream);

    std::uint32_t functors;
    Node** buckets;       // +0x04
    Node** finish;        // +0x08
    Node** endOfStorage;
    std::uint32_t count;  // +0x10
};
static_assert(sizeof(NamedFloatTable) == 0x14);

// Rebirth's PersistentGlobalVars (persistent_globals.cpp): empties `table` (00461540) but keeps
// its "gvar." entries, set again in the order addon's own container gives them.
void ClearVariablesKeepingGlobals(NamedFloatTable* table);

// 00462F70. The command bound to `key`: the first command slot i (0..0x1FF) whose key in the table
// at 0079BF70 equals `key` and that has an entry in the command table at 007857B0 (0x48-byte
// entries, slot number first). Returns the entry + 8, or null. __fastcall (key in ECX).
void* __fastcall FindCommandByKey(int key);
