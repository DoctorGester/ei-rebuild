// The quest under construction: QStart names it and drops the old objectives, the QObj* commands
// add objectives, QFinish (004BAAB0, script_exec_helpers.cpp) turns them into a script. See script.h
// and docs/notes/script.md.
#include "script.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "game_arrays.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

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
    mfc::impl::ArrayAs<gamearrays::Objectives>(objectives).SetSize(at + 1, -1);   // 004CDF40: new ones an empty text
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
    return byName.FindOrInsert(pair)->value;   // 004CE110
}
EI_REPLACE_METHOD(0x004C9C90, QuestTable::Find);
