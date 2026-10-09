// Spell and spell modifier records by code (the two tables' index lookups). See item_db.h.
#include "item_db.h"

#include "ei/hook.h"

// The original builds the key, copies it into a (key, null) pair and calls the index's
// find_or_insert (00613C20 for spells, 004ACC10 for modifiers) with the pair.
char* itemdb::CodeIndexedTable::FindByCode(const char* code) {
    stlp::string key(code);
    decltype(byCode)::Pair pair{key, nullptr};
    return byCode[pair.key];
}
EI_REPLACE_METHOD(0x0068C820, itemdb::CodeIndexedTable::FindByCode);
EI_REPLACE_METHOD(0x0068C920, itemdb::CodeIndexedTable::FindByCode);
