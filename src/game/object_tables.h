#pragma once

// Small lookup tables found among the object code (00440000-00470000): text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>

using BOOL = int;

class CStream;

// 00461360. Text key of a name: h = 5 h + toupper(c) over the bytes (unsigned arithmetic), then
// h % 1000000000 + 1000000000. __fastcall (name in ECX).
int __fastcall TextKeyFromName(const char* name);

// 004613A0. Text keys are 1000000000..1999999999. __fastcall (id in ECX).
BOOL __fastcall IsTextKey(int id);

// An MFC CArray as laid out in game.exe (vtable, data, size, max size, grow by).
template <typename T>
struct MfcArray {
    void* vtable;
    T* data;
    int size;
    int maxSize;
    int growBy;
};

// One "#allod" entry of map.txt (0x2C bytes). Only the name is used here.
struct AllodRecord {
    char* name;   // CString
    std::uint8_t unknown_4[0x2C - 4];
};
static_assert(sizeof(AllodRecord) == 0x2C);

// One map/zone entry (0x5C bytes): +0 name, +4 allod name, +8 and +0xC other names.
struct MapRecord {
    char* name;   // CString
    char* allod;  // +0x04 CString
    std::uint8_t unknown_8[0x5C - 8];
};
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

    MfcArray<AllodRecord> allods;   // +0x00
    MfcArray<MapRecord> maps;       // +0x14
};
static_assert(offsetof(CMapDatabase, maps) == 0x14);
static_assert(sizeof(CMapDatabase) == 0x28);

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
