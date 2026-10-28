// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

// operator[]: an unknown code is filed with null.
template <typename Record>
Record* itemdb::CodeIndexedTable<Record>::FindByCode(const char* code) {
    return byCode[std::string(code)];
}
template itemdb::SpellRecord* itemdb::SpellTable::FindByCode(const char*);
template itemdb::SpellModifierRecord* itemdb::SpellModifierTable::FindByCode(const char*);
