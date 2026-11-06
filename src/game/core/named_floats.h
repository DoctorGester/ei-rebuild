#pragma once

// NamedFloatTable: the game's name -> float table (the party's and players' variables).

#include <string>
#include <string_view>

#include "lib/hash_map.h"

class ByteStream;

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
    void Load(ByteStream* stream);
    // Writes each entry (name with its zero, then the value), in bucket order, then an empty name.
    void Save(ByteStream* stream);
};

// Rebirth's PersistentGlobalVars (persistent_globals.cpp): empties `table` but keeps
// its "gvar." entries, set again in the order addon's own container gives them.
void ClearVariablesKeepingGlobals(NamedFloatTable* table);
