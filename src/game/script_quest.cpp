// The quest under construction: QStart names it and drops the old objectives, the QObj* commands
// add objectives, QFinish (script_exec_helpers.cpp) turns them into a script. See script.h
// and docs/notes/script.md.
#include "quest_records.h"
#include "script.h"

#include "lib/crt.h"

#include "game_arrays.h"

#include <cstring>
#include <memory>
#include <string>

// An objective's destructor (its text).
void ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective) { std::destroy_at(&objective->text); }

// QStart (the name comes in ECX). The old objectives are destroyed and freed, the
// array emptied, and the quest named.
void ScriptQuestStart(const char* name) {
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

// The QObj* commands (type, text, with the value on the stack). Appends
// {type, text, value}; the text goes through a temporary copy.
void ScriptQuestAddObjective(int type, const std::string& text, float value) {
    std::string copy = text;
    mfc::CArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
    int at = objectives.size;
    mfc::impl::ArrayAs<gamearrays::Objectives>(objectives).SetSize(at + 1, -1);   // new ones an empty text
    ScriptQuestObjective& objective = objectives.data[at];
    objective.type = type;
    objective.text = copy;
    std::memcpy(&objective.value, &value, 4);
}

// operator[]: an unknown name is filed with null.
QuestRecord* QuestDatabase::FindQuest(const char* name) { return questsByName[std::string(name)]; }
