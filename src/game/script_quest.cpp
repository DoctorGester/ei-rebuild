// The quest under construction: QStart names it and drops the old objectives, the QObj* commands
// add objectives, QFinish (004BAAB0, script_exec_helpers.cpp) turns them into a script. See script.h
// and docs/notes/script.md.
#include "script.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// 004CDF40: CArray<ScriptQuestObjective>::SetSize(size, growBy) (library; new elements get an empty
// text).
__declspec(naked) void __fastcall ObjectivesSetSize(mfc::CArray<ScriptQuestObjective>*, void*, int, int)
    EI_ORIGINAL(0x004CDF40)
// 004CE110: the quest table's hashtable find_or_insert(pair) (library, RET 4): the pair in the table, a
// copy of `pair` inserted when its key is missing.
__declspec(naked) QuestTable::Map::Pair* __fastcall QuestFindOrInsert(QuestTable::Map*, void*,
                                                                       const QuestTable::Map::Pair*)
    EI_ORIGINAL(0x004CE110)

} // namespace

// 004BAEB0: an objective's destructor (its text).
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective) { objective->text.~CString(); }

// 004BAA40 (QStart; __fastcall with the name in ECX). The old objectives are destroyed and freed, the
// array emptied, and the quest named.
void __fastcall ScriptQuestStart(const char* name) {
    mfc::CArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
    if (objectives.data) {
        ScriptQuestObjective* objective = objectives.data;
        for (int n = objectives.size; n != 0; --n, ++objective) ScriptQuestObjectiveDestruct(objective);
        gamecrt::operator_delete(objectives.data);
        objectives.data = nullptr;
    }
    objectives.maxSize = 0;
    objectives.size = 0;
    ScriptQuestName() = name;
}

// 004BAE10 (the QObj* commands; __fastcall (type, text) with the value on the stack). Appends
// {type, text, value}; the text goes through a temporary copy.
void __fastcall ScriptQuestAddObjective(int type, const mfc::CString* text, float value) {
    mfc::CString copy;
    copy = *text;
    mfc::CArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
    int at = objectives.size;
    ObjectivesSetSize(&objectives, nullptr, at + 1, -1);
    ScriptQuestObjective& objective = objectives.data[at];
    objective.type = type;
    objective.text = copy;
    std::memcpy(&objective.value, &value, 4);
}

EI_REPLACE(0x004BAEB0, ScriptQuestObjectiveDestruct);
EI_REPLACE(0x004BAA40, ScriptQuestStart);
EI_REPLACE(0x004BAE10, ScriptQuestAddObjective);

// 004C9C90: the key, then a {key, null} pair copied from it; the pair is destroyed before the key.
void* QuestTable::Find(const char* name) {
    stlp::string key(name);
    Map::Pair pair{key, nullptr};
    return QuestFindOrInsert(&byName, nullptr, &pair)->value;
}
EI_REPLACE_METHOD(0x004C9C90, QuestTable::Find);
// 0064F550: a second copy of the same code (the objectives window's quest sort, __median 0064F400,
// orders quests by their record's +0x1C through it).
EI_REPLACE_METHOD(0x0064F550, QuestTable::Find);
