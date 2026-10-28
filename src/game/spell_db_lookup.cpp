// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

// operator[]: an unknown code is filed with null.
template <typename Record>
Record* itemdb::CodeIndexedTable<Record>::FindByCode(std::string_view code) {
    return byCode[std::string(code)];
}
template itemdb::SpellRecord* itemdb::SpellTable::FindByCode(std::string_view);
template itemdb::SpellModifierRecord* itemdb::SpellModifierTable::FindByCode(std::string_view);
