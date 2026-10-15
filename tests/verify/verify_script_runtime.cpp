// Checks for QFinish (src/game/script_exec_helpers.cpp), the CScript reset (script_reset.cpp) and
// ScriptDefinition::AddParameter (script_vars.cpp) and the ScriptDefinition reset (script_reset.cpp).
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace scriptverify {
// verify_script_parse.cpp: the CScript::Load wrapper copies the next loaded text here (and clears
// the pointer); with g_loadSkip it returns 0 without loading.
extern std::string* g_loadTextCapture;
extern bool g_loadSkip;
} // namespace scriptverify

namespace {
void* s_unused;

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

mfc::CString& QuestName() { return *reinterpret_cast<mfc::CString*>(0x007AA61C); }

// ---- QFinish ----------------------------------------------------------------------------------------

// 004BAAB0: PUSH EBP; MOV EBP,ESP; AND ESP,-8 (6 bytes).
// The original runs first with its load skipped (the Load wrapper only records the text), the quest
// name it empties is put back, then ours runs and really loads. Both texts must be equal, and the
// name empty afterwards.
void(__fastcall* s_questFinish)(CScript*);
void __fastcall QuestFinishVerify(CScript* script) {
    mfc::CString quest = QuestName();
    std::string expected;
    std::string actual;
    scriptverify::g_loadTextCapture = &expected;
    scriptverify::g_loadSkip = true;
    s_questFinish(script);
    scriptverify::g_loadSkip = false;
    scriptverify::g_loadTextCapture = &actual;
    QuestName() = quest;
    ScriptQuestFinish(script);
    scriptverify::g_loadTextCapture = nullptr;
    bool ok = !expected.empty() && expected == actual && QuestName().GetLength() == 0;
    std::size_t at = 0;
    while (at < expected.size() && at < actual.size() && expected[at] == actual[at]) ++at;
    ei::verify::Check("ScriptQuestFinish", true, ok, "quest \"%s\": %u vs %u chars, first difference at %u",
                      static_cast<const char*>(quest), unsigned(expected.size()), unsigned(actual.size()),
                      unsigned(at));
}

// ---- CScript reset -----------------------------------------------------------------------------------

bool EmptyList(CScript* script, std::size_t offset) {
    const ScriptList& l = Field<ScriptList>(script, offset);
    return !l.head && !l.tail && l.count == 0 && !l.free && !l.blocks;
}
template <typename T>
bool EmptyArray(const mfc::CArray<T>& a) { return !a.data && a.size == 0 && a.maxSize == 0; }

// Ours only (frees and releases): the state the reset leaves.
void __fastcall ResetVerify(CScript* script) {
    ScriptReset(script);
    const char* failed = nullptr;
    auto require = [&](bool condition, const char* what) {
        if (!condition && !failed) failed = what;
    };
    require(script->LoadedFlag() == 0 && script->FileCount() == 0 && script->ErrorLine() == 0, "flags");
    require(std::strcmp(script->ErrorText(), reinterpret_cast<const char*>(0x0078BBDC)) == 0, "error text");
    const std::size_t lists[] = {0x14, 0x30, 0x4C, 0x70, 0x140, 0x15C, 0x178};
    for (std::size_t offset : lists) require(EmptyList(script, offset), "a list is not empty");
    const ScriptVars* g = script->Globals();
    require(EmptyArray(g->floats) && EmptyArray(g->floatNames) && EmptyArray(g->strings) &&
                EmptyArray(g->stringNames) && EmptyArray(g->objects) && EmptyArray(g->objectNames) &&
                EmptyArray(g->groups) && EmptyArray(g->groupNames),
            "globals");
    require(Field<int>(script, 0x1E8) == 0, "For depth");
    const ScriptList& groups = Field<ScriptList>(script, 0x1B0);
    const ScriptList& objects = Field<ScriptList>(script, 0x1CC);
    require(groups.count == 1 && groups.head == groups.tail &&
                EmptyArray(*reinterpret_cast<const ScriptGroup*>(groups.tail->data.bytes)),
            "temporary groups");
    require(objects.count == 1 && objects.head == objects.tail &&
                reinterpret_cast<const ScriptObjectRef*>(objects.tail->data.bytes)->object == nullptr,
            "temporary objects");
    const char* lights = Field<char*>(script, 0x04);
    for (int i = 0; i < Field<int>(script, 0x08) * 0x20; ++i) require(lights[i] == 0, "point lights");
    require(Field<int>(script, 0x19C) == 0 && Field<int>(script, 0x94) == 0, "areas or definitions");
    require(Field<int>(script, 0x68) == -2 && Field<int>(script, 0x6C) == -2, "+0x68 / +0x6C");
    ei::verify::Check("ScriptReset", true, failed == nullptr, "%s", failed ? failed : "");
}

// ---- AddParameter ------------------------------------------------------------------------------------

int FindName(const mfc::CArray<mfc::CString>& names, const char* name) {
    for (int i = 0; i < names.size; ++i)
        if (std::strcmp(names.data[i], name) == 0) return i;
    return -1;
}

// Ours only (grows the arrays): the counts before; afterwards a new name and value of the type
// and the letter appended, or nothing changed when the name was there.
BOOL __fastcall AddParameterVerify(ScriptDefinition* self, void*, const mfc::CString* name, int type) {
    ScriptVars& v = self->vars;
    mfc::CArray<mfc::CString>* namesOf[] = {&v.floatNames, &v.stringNames, &v.objectNames, &v.groupNames};
    const int* valuesOf[] = {&v.floats.size, &v.strings.size, &v.objects.size, &v.groups.size};
    bool known = type >= 1 && type <= 4;
    std::string text = static_cast<const char*>(*name);
    std::string params = static_cast<const char*>(self->params);
    int names = known ? namesOf[type - 1]->size : 0;
    int values = known ? *valuesOf[type - 1] : 0;
    bool existed = known && FindName(*namesOf[type - 1], text.c_str()) >= 0;

    BOOL result = self->AddParameterImpl(name, type);

    bool ok;
    std::string now = static_cast<const char*>(self->params);
    if (!known) {
        ok = result == static_cast<BOOL>(reinterpret_cast<std::uintptr_t>(name)) && now == params;
    } else if (existed) {
        ok = result == 0 && now == params && namesOf[type - 1]->size == names && *valuesOf[type - 1] == values;
    } else {
        const mfc::CArray<mfc::CString>& n = *namesOf[type - 1];
        ok = result == 1 && n.size == names + 1 && *valuesOf[type - 1] == values + 1 &&
             text == static_cast<const char*>(n.data[names]) && now == params + "fsog"[type - 1];
    }
    ei::verify::Check("ScriptAddParameter", true, ok, "\"%s\" type %d: result %d, params \"%s\"", text.c_str(), type,
                      result, now.c_str());
    return result;
}

// ---- ScriptDefinition reset ---------------------------------------------------------------------------

__declspec(naked) void __fastcall VarsConstructAt(void*) EI_ORIGINAL(0x004C00D0)
__declspec(naked) void __fastcall DefinitionAssign(void*, void*, const void*) EI_ORIGINAL(0x004CB460)
__declspec(naked) void __fastcall DefinitionDestruct(void*) EI_ORIGINAL(0x004BE9D0)

template <typename T>
bool SameEmptied(const mfc::CArray<T>& a, const mfc::CArray<T>& b) {
    return a.vtable == b.vtable && a.data == b.data && a.size == b.size && a.maxSize == b.maxSize;
}

// The growBy words of the nine arrays (SetSize(0, -1) keeps them; the scratch's are its own).
std::vector<int> GrowBys(const ScriptDefinition& d) {
    const ScriptVars& v = d.vars;
    return {d.ifThen.growBy, v.floats.growBy, v.floatNames.growBy, v.strings.growBy, v.stringNames.growBy,
            v.objects.growBy, v.objectNames.growBy, v.groups.growBy, v.groupNames.growBy};
}

// 004D3320: PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes). Against the original: a scratch
// definition is built as CScript::Load builds its local one (empty strings, the if-then vtable
// 0073D430, the ScriptVars constructor) and assigned from the real one (004CB460, a deep copy);
// the original resets the scratch, ours the real one. Compared: index, name, parameters and every
// array's vtable, data, size and capacity (all written by the reset); the real one's growBy words
// unchanged. The scratch is destroyed with 004BE9D0.
void(__fastcall* s_definitionReset)(ScriptDefinition*);
void __fastcall DefinitionResetVerify(ScriptDefinition* self) {
    alignas(ScriptDefinition) unsigned char raw[sizeof(ScriptDefinition)];
    auto* scratch = reinterpret_cast<ScriptDefinition*>(raw);
    new (&scratch->name) mfc::CString;
    new (&scratch->params) mfc::CString;
    scratch->ifThen.vtable = reinterpret_cast<void*>(0x0073D430);
    scratch->ifThen.data = nullptr;
    scratch->ifThen.size = 0;
    scratch->ifThen.maxSize = 0;
    scratch->ifThen.growBy = 0;
    VarsConstructAt(&scratch->vars);
    scratch->index = 0;
    DefinitionAssign(scratch, nullptr, self);
    int pairs = self->ifThen.size;
    std::vector<int> growBys = GrowBys(*self);

    s_definitionReset(scratch);
    ScriptDefinitionReset(self);

    const ScriptVars& a = scratch->vars;
    const ScriptVars& b = self->vars;
    bool ok = scratch->index == self->index && std::strcmp(scratch->name, self->name) == 0 &&
              std::strcmp(scratch->params, self->params) == 0 && SameEmptied(scratch->ifThen, self->ifThen) &&
              SameEmptied(a.floats, b.floats) && SameEmptied(a.floatNames, b.floatNames) &&
              SameEmptied(a.strings, b.strings) && SameEmptied(a.stringNames, b.stringNames) &&
              SameEmptied(a.objects, b.objects) && SameEmptied(a.objectNames, b.objectNames) &&
              SameEmptied(a.groups, b.groups) && SameEmptied(a.groupNames, b.groupNames) && GrowBys(*self) == growBys;
    ei::verify::Check("ScriptDefinitionReset", true, ok, "definition with %d pairs: name \"%s\" params \"%s\"", pairs,
                      static_cast<const char*>(self->name), static_cast<const char*>(self->params));
    DefinitionDestruct(scratch);
}

} // namespace

EI_VERIFY(0x004D3320, DefinitionResetVerify, 5, s_definitionReset);
EI_VERIFY(0x004BAAB0, QuestFinishVerify, 6, s_questFinish);
EI_VERIFY(0x004BE6E0, ResetVerify, 0, s_unused);
EI_VERIFY(0x004BB800, AddParameterVerify, 0, s_unused);
