#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the
// server controller's statistics hand-over. Names provisional. See docs/notes/units.md
// ("Fourth round").

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// The unit prototype table (global 007AF358): an STLport vector of 0x120-byte records (item_db.h's
// RecordTable reads its begin / end) followed by a hash_map<string, record*> from prototype names
// to records at +0x0C.
struct UnitPrototypeTable {
    using NameIndex = stlp::hash_map<stlp::string, char*, stlp::StringHash>;

    // 00514930. The record named `name`: operator[] on the name index, so an unknown name is
    // inserted with a null record (and null is returned).
    char* FindByName(const char* name);

    char* begin;                // +0x00
    char* end;                  // +0x04
    char* endOfStorage;         // +0x08
    NameIndex byName;           // +0x0C
};
static_assert(offsetof(UnitPrototypeTable, byName) == 0x0C);

namespace unitdb {
inline UnitPrototypeTable& PrototypeTable() { return *reinterpret_cast<UnitPrototypeTable*>(0x007AF358); }
} // namespace unitdb
