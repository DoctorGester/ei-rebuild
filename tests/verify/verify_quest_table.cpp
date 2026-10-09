// QuestTable::Find (script_quest.cpp) against the original: the original runs first (a missing name
// gets its null entry there), then ours, which must find the same record and leave the map's size
// as the original left it. 004C9C90 is reached by QuestComplete (`script_quest`); its copy 0064F550
// by the objectives window's quest sort with more than 16 quests (`ui_objectives_sort`).
#include "verify.h"

#include "../../src/game/script.h"

namespace {

using FindFn = void*(__fastcall*)(QuestTable*, void*, const char*);

void* RunFind(const char* check, FindFn original, QuestTable* self, const char* name) {
    void* expected = original(self, nullptr, name);
    std::size_t size = self->byName.Size();
    void* actual = self->Find(name);
    ei::verify::Check(check, true, expected == actual && size == self->byName.Size(), "'%s'", name);
    return actual;
}

// 004C9C90 / 0064F550: PUSH -1 (2), PUSH imm32 (5).
FindFn s_find;
void* __fastcall FindVerify(QuestTable* self, void*, const char* name) {
    return RunFind("QuestTable::Find", s_find, self, name);
}
FindFn s_findCopy;
void* __fastcall FindCopyVerify(QuestTable* self, void*, const char* name) {
    return RunFind("QuestTable::Find (0064F550)", s_findCopy, self, name);
}

} // namespace

EI_VERIFY(0x004C9C90, FindVerify, 7, s_find);
EI_VERIFY(0x0064F550, FindCopyVerify, 7, s_findCopy);
