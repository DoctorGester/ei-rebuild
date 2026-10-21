// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

#include "ei/hook.h"

// The original builds the key, copies it into a (key, null) pair and calls the index's
// find_or_insert (00613C20 for spells, 004ACC10 for modifiers) with the pair.
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
