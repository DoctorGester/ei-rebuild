// Checks for the quest construction (src/game/script_quest.cpp). Scenario: script_quest (QStart, the
// QObj* commands, QFinish).
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

// The CString buffer's reference count (CStringData::nRefs; negative for the shared empty string).
int References(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-3]; }

// 004BAEB0 (QStart, the objective array's SetSize / Copy): ours only. The element's words stay as they
// were (the CString destructor leaves its pointer); a shared text buffer loses one reference.
void __fastcall ObjectiveDestructVerify(ScriptQuestObjective* self) {
    std::uint32_t before[3];
    std::memcpy(before, self, sizeof(before));
    int references = References(self->text);
    const int* counter = reinterpret_cast<const int*>(static_cast<const char*>(self->text)) - 3;
    ScriptQuestObjectiveDestruct(self);
    bool ok = std::memcmp(before, self, sizeof(before)) == 0;
    if (references > 1) ok = ok && *counter == references - 1;
    ei::verify::Check("ScriptQuestObjectiveDestruct", true, ok, "objective type 0x%X, %d references before",
                      self->type, references);
}

// 004BAA40 QStart: ours only (the old objectives are freed): no objectives left, the name set.
void __fastcall QuestStartVerify(const char* name) {
    ScriptQuestStart(name);
    const mfc::CArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
    ei::verify::Check("ScriptQuestStart", true,
                      !objectives.data && objectives.size == 0 && objectives.maxSize == 0 &&
                          std::strcmp(ScriptQuestName(), name) == 0,
                      "\"%s\": %d objectives, name \"%s\"", name, objectives.size,
                      static_cast<const char*>(ScriptQuestName()));
}

// 004BAE10 (QObj*): against the original. The original appends first; its element is kept aside,
// destroyed and the size taken back; then ours appends. PUSH -1; PUSH 0x718338 (7 bytes).
void(__fastcall* s_addObjective)(int, const mfc::CString*, float);
void __fastcall AddObjectiveVerify(int type, const mfc::CString* text, float value) {
    mfc::CArray<ScriptQuestObjective>& objectives = ScriptQuestObjectives();
    int size = objectives.size;
    s_addObjective(type, text, value);
    ScriptQuestObjective& added = objectives.data[size];
    int expectedType = added.type;
    mfc::CString expectedText(added.text);
    std::uint32_t expectedValue;
    std::memcpy(&expectedValue, &added.value, 4);
    int expectedSize = objectives.size;
    added.text.~CString();
    objectives.size = size;
    ScriptQuestAddObjective(type, text, value);
    const ScriptQuestObjective& ours = objectives.data[size];
    ei::verify::Check("ScriptQuestAddObjective", true,
                      objectives.size == expectedSize && ours.type == expectedType &&
                          std::strcmp(ours.text, expectedText) == 0 &&
                          std::memcmp(&ours.value, &expectedValue, 4) == 0,
                      "objective %d: type 0x%X \"%s\"", size, ours.type, static_cast<const char*>(ours.text));
}

} // namespace

EI_VERIFY(0x004BAEB0, ObjectiveDestructVerify, 0, s_unused);
EI_VERIFY(0x004BAA40, QuestStartVerify, 0, s_unused);
EI_VERIFY(0x004BAE10, AddObjectiveVerify, 7, s_addObjective);
