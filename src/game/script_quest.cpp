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
__declspec(naked) void __fastcall ObjectivesSetSize(MfcArray<ScriptQuestObjective>*, void*, int, int)
    EI_ORIGINAL(0x004CDF40)

} // namespace

// 004BAEB0: an objective's destructor (its text).
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective) { objective->text.~CString(); }

// 004BAA40 (QStart; __fastcall with the name in ECX). The old objectives are destroyed and freed, the
// array emptied, and the quest named.
void __fastcall ScriptQuestStart(const char* name) {
    MfcArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
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
    MfcArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
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
