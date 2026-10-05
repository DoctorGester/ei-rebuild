// QuestTable::Find (script_quest.cpp) against the original: the original runs first (a missing name
// gets its null entry there), then ours, which must find the same record and leave the map's size
// as the original left it. Reached by QuestComplete (`script_quest`).
#include "verify.h"

#include "../../src/game/script.h"

namespace {

// 004C9C90: PUSH -1 (2), PUSH imm32 (5).
void*(__fastcall* s_find)(QuestTable*, void*, const char*);
void* __fastcall FindVerify(QuestTable* self, void*, const char* name) {
    void* expected = s_find(self, nullptr, name);
    std::size_t size = self->byName.Size();
    void* actual = self->Find(name);
    ei::verify::Check("QuestTable::Find", true, expected == actual && size == self->byName.Size(), "'%s'", name);
    return actual;
}

} // namespace

EI_VERIFY(0x004C9C90, FindVerify, 7, s_find);
