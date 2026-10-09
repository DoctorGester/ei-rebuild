// ReadStringList (006AE740, quest_database.cpp; A-H19): side by side, the original into an empty
// scratch vector, ours into the real list; both must hold the same strings (the list is emptied
// first, so its old contents do not matter). Reached by every quest registration (the briefings'
// lists; QuestDatabaseLoad calls it through its address): every new game.
#include "verify.h"

#include "../../src/game/originals_library.h"
#include "../../src/game/quest_records.h"
#include "../../src/lib/stlp.h"

#include <cstring>

namespace {

// 006AE740: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_readList)(CFileRegistry*, const char*, const char*, CStringVector*);
void __fastcall ReadListVerify(CFileRegistry* registry, const char* section, const char* key, CStringVector* list) {
    CStringVector scratch{nullptr, nullptr, nullptr};
    s_readList(registry, section, key, &scratch);
    ReadStringList(registry, section, key, list);
    int expected = static_cast<int>(scratch.end - scratch.begin);
    int actual = static_cast<int>(list->end - list->begin);
    int bad = -1;
    for (int i = 0; i < expected && i < actual && bad < 0; ++i)
        if (std::strcmp(scratch.begin[i], list->begin[i]) != 0) bad = i;
    ei::verify::Check("ReadStringList", true, expected == actual && bad < 0, "'%s' '%s': %d / %d strings, first difference %d",
                      section, key, expected, actual, bad);
    CStringVectorClearOriginal(&scratch);
    if (scratch.begin)
        stlp::deallocate(scratch.begin, static_cast<std::size_t>(scratch.endOfStorage - scratch.begin) * sizeof(mfc::CString));
}

} // namespace

EI_VERIFY(0x006AE740, ReadListVerify, 7, s_readList);
