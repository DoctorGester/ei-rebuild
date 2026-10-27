// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

#include "ei/hook.h"

// The key is built, copied into a (key, null) pair and handed to the index's find_or_insert.
template <typename Record>
Record* itemdb::CodeIndexedTable<Record>::FindByCode(const char* code) {
    stlp::string key(code);
    typename decltype(byCode)::Pair pair{key, nullptr};
    return byCode[pair.key];
}
template itemdb::SpellRecord* itemdb::SpellTable::FindByCode(const char*);
template itemdb::SpellModifierRecord* itemdb::SpellModifierTable::FindByCode(const char*);
EI_REWRITTEN(0x0068C820, itemdb::SpellTable::FindByCode);
EI_REWRITTEN(0x0068C920, itemdb::SpellModifierTable::FindByCode);

// The bounds-checked records as the spell book reads them.
EI_REWRITTEN(0x005F2EA0, itemdb::SpellTable::At);
EI_REWRITTEN(0x005F2EE0, itemdb::SpellModifierTable::At);
