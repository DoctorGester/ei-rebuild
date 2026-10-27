#pragma once

// Small lookup tables found among the object code (00440000-00470000): text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>

#include "lib/mfc.h"
#include "lib/stlp_hash.h"
#include "game_data.h"

using BOOL = int;

class CStream;

// Text key of a name: h = 5 h + toupper(c) over the bytes (unsigned arithmetic), then
// h % 1000000000 + 1000000000. __fastcall (name in ECX).
int __fastcall TextKeyFromName(const char* name);

// Text keys are 1000000000..1999999999. __fastcall (id in ECX).
BOOL __fastcall IsTextKey(int id);

// One "#allod" entry of map.txt (0x2C bytes; constructed by 00460720).
struct AllodRecord {
    mfc::CString name;           // the "#allod" line's second word
    mfc::CString otherNames[5];  // +0x04 one word on each of the next five lines
    mfc::CArray<int> maps;    // +0x18 CDWordArray (vtable 0073C3D0): the indices of its maps
};
static_assert(offsetof(AllodRecord, otherNames) == 0x04 && offsetof(AllodRecord, maps) == 0x18 && sizeof(AllodRecord) == 0x2C);

// One "#exit" of a map entry (0x3C bytes; map_database.cpp loads it): the map database's
// record, which the world server copies (SetZoneExits) and sends to the client world.
struct ZoneExit {
    float passTime;                 // +0x00 "#passtime": the time the way takes
    float deploy[4];                // +0x04 "#deploy": the rectangle the arriving party stands in
    float x1, y1, x2, y2;           // +0x14 "#remove" (and "#deploy"): the area that leads out
    float angle;                    // +0x24 the area's rotation about its centre (radians)
    mfc::CString target;            // +0x28 the zone it leads to ("" = none)
    int entrance;                   // +0x2C the target zone's exit the party arrives at
    float view;                     // +0x30 "#view" (radians)
    std::uint8_t hasDeployAngle;    // +0x34 "#deployangle" given; saved from version 0x68
    std::uint8_t unused_35[3];
    float deployAngle;              // +0x38 saved from version 0x68

    // 004E59F0 (exit_area.cpp). Whether (x, y) lies inside the exit area (rotated by `angle` about
    // the rectangle's centre when it is not 0).
    bool Contains(float x, float y) const;
    // 004E5750 / 004E58A0 (exit_area.cpp, RET 4). From / to a stream (core_streams.h): +0x00..+0x20
    // as dwords, the target with its terminator, +0x2C, +0x30, then +0x34 (a byte), +0x38 and the
    // angle; Read takes the last three only from version 0x68 on (else 0).
    void Read(CStream* stream);
    void Write(CStream* stream) const;
    // (004608B0, zone::ExitDelete in game_arrays.h: the scalar deleting destructor.)
    // 00583B50 (RET 4; S13, exit_area.cpp; the compiler's operator=): +0x00..+0x24 as dwords,
    // the target (CString), +0x2C, +0x30, the byte +0x34 and +0x38; returns this.
    ZoneExit* Assign(const ZoneExit* from);
};
static_assert(offsetof(ZoneExit, x1) == 0x14 && offsetof(ZoneExit, target) == 0x28);
static_assert(offsetof(ZoneExit, deployAngle) == 0x38 && sizeof(ZoneExit) == 0x3C);

namespace zone {
void __fastcall ExitDestruct(ZoneExit* self);   // 00460460 (zone_records.cpp)
} // namespace zone

// The exits' ConstructElements (004606A0: zero filled, the target the empty string) and
// DestructElements (00460670: each exit's destructor, 00460460).
struct ZoneExitElements {
    static void Construct(ZoneExit* p, int n) {
        mfc::PlainElements<ZoneExit>::Construct(p, n);
        for (; n--; ++p) ::new (&p->target) mfc::CString;
    }
    static void Destruct(ZoneExit* p, int n) {
        for (; n--; ++p) zone::ExitDestruct(p);
    }
};

// MFC CArray<ZoneExit> (a map record's +0x48, CWorldServer + 0xEC, CWorldClient + 0x8C). Its SetSize
// is the instantiation's own (00573080, gamearrays::Exits; new records are constructed).
struct ZoneExits : mfc::CArray<ZoneExit, ZoneExitElements> {
    // 00584EA0 / 00587DD0 (RET 8; record_arrays.cpp). By-value GetAt: element `index` copied into `out`
    // (the fields, the target string copy-constructed). Returns out.
    ZoneExit* CopyAt(ZoneExit* out, int index) const;
};
static_assert(sizeof(ZoneExits) == 0x14);

// One map/zone entry (0x5C bytes): +0 name, +4 allod name, +8 and +0xC other names.
struct MapRecord {
    mfc::CString name;
    mfc::CString allod;     // +0x04 (SaveGame keeps the visited zones of the current one's)
    mfc::CString mob;       // +0x08 the .mob file without extension
    mfc::CString map;       // +0x0C the .mpr map without extension ("" = none)
    mfc::CString figure;    // +0x10 the "#figure" line
    mfc::CString picture2;  // +0x14 the "#maps" second line's second word
    mfc::CString picture;   // +0x18 its first word, the HUD map's texture
    mfc::CString camera;    // +0x1C the "#camera" line
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
    ZoneExits exits;              // +0x48
};
static_assert(offsetof(MapRecord, kind) == 0x28);
static_assert(offsetof(MapRecord, map) == 0x0C && offsetof(MapRecord, camera) == 0x1C);
static_assert(offsetof(MapRecord, setting30) == 0x30);
static_assert(offsetof(MapRecord, exits) == 0x48);
static_assert(offsetof(MapRecord, restricted) == 0x34);
static_assert(offsetof(MapRecord, cage) == 0x44);
static_assert(sizeof(MapRecord) == 0x5C);

// The map database loaded from map.txt / map_LMP.txt (global at 0079BF00; constructor 0045E820,
// loader 0045EA20).
struct CMapDatabase {
    // Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(const char* name);
    // Allod named `name` (_mbsicmp), or null.
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

    mfc::CArray<AllodRecord> allods;   // +0x00
    mfc::CArray<MapRecord> maps;       // +0x14
};
static_assert(offsetof(CMapDatabase, maps) == 0x14);
static_assert(sizeof(CMapDatabase) == 0x28);

// The game's map database.

// The game's hash_map<CString, float> (the party's and players' variables, the briefing's copies):
// STLport's hashtable over pair<CString, float> (stlp_hash.h; its members are registered in
// stlp_hash.cpp), nodes {next, key, value}, with the named-variable members the game adds.
struct NamedFloatTable : stlp::hashtable<stlp::CStringFloatTraits> {
    // hash_map(): 100 buckets.
    void ConstructDefault() { Construct(100, 0, 0, 0); }
    // operator[]: the value under `key`, a new zero one when absent.
    float& operator[](const mfc::CString& key) {
        stlp::CStringFloat entry(key);
        return FindOrInsert(entry).mapped;
    }

    // Value stored under `name` (_mbscmp), 0 if absent. Builds a CString of the name
    // first, as the original does.
    float Get(const char* name);
    // Stores `value` under `name`; a zero value (or NaN: the original's FCOMP
    // test) removes the entry instead. Nothing for an empty name.
    void Set(const char* name, float value);
    // Removes the entry stored under `name`.
    void Remove(const char* name);
    // Empties `out`, then copies into it every entry whose name starts with
    // `prefix` (strncmp over the prefix's length).
    void CopyPrefixed(const char* prefix, NamedFloatTable* out);
    // Empties the table, then reads {zero-terminated name, float} pairs from the
    // stream up to an empty name (compared with the exe's empty string, 0079B298).
    void Load(CStream* stream);
    // Writes each entry (name with its zero, then the value), in bucket order,
    // then an empty name.
    void Save(CStream* stream);
};
static_assert(sizeof(NamedFloatTable) == 0x14);static_assert(sizeof(NamedFloatTable) == 0x14);

// Rebirth's PersistentGlobalVars (persistent_globals.cpp): empties `table` but keeps
// its "gvar." entries, set again in the order addon's own container gives them.
void ClearVariablesKeepingGlobals(NamedFloatTable* table);

// The in-game key bindings (config\keyboard.ini, read by 00462C10; key_bindings.cpp): the action of each scan code
// (0x200 entries, the WM_KEYDOWN flags & 0x1FF; the options window's OK stores its copy there).
inline std::int8_t g_keyActions[512] = /*0079BF70*/ {};

// The keys (0x48 bytes each, 88 of them; key_bindings.cpp's g_keyNames): the options window lists them, and
// the bindings file names them.
struct UiKeyName {
    int scan_;                       // +0x00 scan code, the index into g_keyActions
    const char* file_;               // +0x04 the bindings file's name ("TAB", "Q", ...)
    char name_[0x40];                // +0x08 GetKeyNameTextA's text (up to 31 bytes; LoadKeyBindings)
};
static_assert(sizeof(UiKeyName) == 0x48);
constexpr int kUiKeyNameCount = 88;
extern UiKeyName g_keyNames[kUiKeyNameCount];

// The command bound to `key`: the text of the first key of g_keyNames whose scan code i (0..0x1FF)
// has `key` in g_keyActions, or null. __fastcall (key in ECX).
void* __fastcall FindCommandByKey(int key);
