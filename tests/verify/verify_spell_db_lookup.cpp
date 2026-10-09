// Checks for spell_db_lookup.cpp (the spell and modifier records by code). Scenarios spells (casts
// with modifiers), trader_stock (the stock's spell and modifier indices).
// Ours runs first (it files an unknown code with null, as the original would); the original then
// finds the same entry, unless its resize for one more element would rehash the index (the
// iteration order would change), when it is skipped. The results are compared.
#include "verify.h"

#include "../../src/game/item_db.h"

using itemdb::CodeIndexedTable;

namespace {

// 0068C820 / 0068C920: PUSH -1; PUSH imm32 (7).
char*(__fastcall* s_spellFind)(CodeIndexedTable*, void*, const char*);
char*(__fastcall* s_modifierFind)(CodeIndexedTable*, void*, const char*);

char* Compare(const char* name, char*(__fastcall* original)(CodeIndexedTable*, void*, const char*),
              CodeIndexedTable* self, const char* code) {
    char* actual = self->FindByCode(code);
    if (self->byCode.Size() + 1 > self->byCode.BucketCount()) return actual;
    char* expected = original(self, nullptr, code);
    ei::verify::Check(name, expected, actual, "\"%s\"", code);
    return expected;
}

char* __fastcall SpellFindVerify(CodeIndexedTable* self, void*, const char* code) {
    return Compare("CodeIndexedTable::FindByCode (spells)", s_spellFind, self, code);
}
char* __fastcall ModifierFindVerify(CodeIndexedTable* self, void*, const char* code) {
    return Compare("CodeIndexedTable::FindByCode (modifiers)", s_modifierFind, self, code);
}

} // namespace

EI_VERIFY(0x0068C820, SpellFindVerify, 7, s_spellFind);
EI_VERIFY(0x0068C920, ModifierFindVerify, 7, s_modifierFind);
