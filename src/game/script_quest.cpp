// The quest under construction: QStart names it and drops the old objectives, the QObj* commands
// add objectives, QFinish (script_exec_helpers.cpp) turns them into a script. See script.h
// and docs/notes/script.md.
#include "quest_records.h"
#include "script.h"

#include <string>

// QStart: the old objectives dropped, the quest named.
void ScriptQuestStart(const char* name) {
    ScriptQuestObjectives().clear();
    ScriptQuestName() = name;
}

// The QObj* commands (type, text, with the value on the stack). Appends {type, text, value}.
void ScriptQuestAddObjective(int type, const std::string& text, float value) {
    ScriptQuestObjectives().push_back({type, text, value});
}

// operator[]: an unknown name is filed with null.
QuestRecord* QuestDatabase::FindQuest(const char* name) { return questsByName[std::string(name)]; }
