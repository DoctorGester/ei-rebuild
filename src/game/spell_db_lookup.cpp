// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

// The key is built, copied into a (key, null) pair and handed to the index's find_or_insert.
template <typename Record>
Record* itemdb::CodeIndexedTable<Record>::FindByCode(const char* code) {
    std::string key(code);
    typename decltype(byCode)::Pair pair{key, nullptr};
    return byCode[pair.key];
}
template itemdb::SpellRecord* itemdb::SpellTable::FindByCode(const char*);
template itemdb::SpellModifierRecord* itemdb::SpellModifierTable::FindByCode(const char*);
