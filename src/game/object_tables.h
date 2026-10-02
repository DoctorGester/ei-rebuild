#pragma once

// Small lookup tables found among the object code (00440000-00470000): text keys, the map/allod
// database, a name -> float table and the key -> command table. Names provisional.

#include <cstddef>
#include <cstdint>

using BOOL = int;

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
    std::uint8_t unknown_4[0x5C - 4];
};
static_assert(sizeof(MapRecord) == 0x5C);

// The map database loaded from map.txt / map_LMP.txt (global at 0079BF00; constructor 0045E820,
// loader 0045EA20).
struct CMapDatabase {
    // 0045E850. Map named `name` (_mbsicmp), or null.
    MapRecord* FindMap(const char* name);
    // 0045E910. Allod named `name` (_mbsicmp), or null.
    AllodRecord* FindAllod(const char* name);

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

    std::uint32_t functors;
    Node** buckets;       // +0x04
    Node** finish;        // +0x08
    Node** endOfStorage;
    std::uint32_t count;  // +0x10
};
static_assert(sizeof(NamedFloatTable) == 0x14);

// 00462F70. The command bound to `key`: the first command slot i (0..0x1FF) whose key in the table
// at 0079BF70 equals `key` and that has an entry in the command table at 007857B0 (0x48-byte
// entries, slot number first). Returns the entry + 8, or null. __fastcall (key in ECX).
void* __fastcall FindCommandByKey(int key);
