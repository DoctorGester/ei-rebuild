// Script variables and the SSCCmd helpers that bind and save call trees. See script.h.
#include "script.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstdint>

namespace {


// The empty string constant the game assigns to clear string variables.

// CStringArray-like helper of the variable set (004C2F80): *dst = strings[index], through a
// temporary copy (GetAt returns by value).
struct StringValues {
    void CopyTo(int index, mfc::CString* dst);
};
__declspec(naked) void StringValues::CopyTo(int, mfc::CString*) EI_ORIGINAL(0x004C2F80)

// A float copy through the FPU, like the original's FLD/FSTP pair (not a plain dword move).
void CopyFloat(float* dst, const float* src) {
    __asm {
        mov eax, src
        fld dword ptr [eax]
        mov eax, dst
        fstp dword ptr [eax]
    }
}

int FindName(const mfc::CArray<mfc::CString>& names, const char* name) {
    for (int i = 0; i < names.size; ++i)
        if (GameMbscmp(names.data[i], name) == 0) return i;
    return -1;
}

} // namespace

// ---- ScriptVars ---------------------------------------------------------------------------------

void ScriptVars::ResetValues() {
    for (int i = 0; i < floats.size; ++i) reinterpret_cast<std::uint32_t*>(floats.data)[i] = 0;
    for (int i = 0; i < strings.size; ++i) strings.data[i] = game::EmptyText();
    for (int i = 0; i < objects.size; ++i) {
        ScriptObjectRef* slot = &objects.data[i];
        if (slot->object) slot->object->Release();
        slot->object = nullptr;
    }
    for (int g = 0; g < groups.size; ++g) {
        ScriptGroup& group = groups.data[g];
        if (group.data) {
            ScriptObjectRef* slot = group.data;
            for (int n = group.size; n != 0; --n, ++slot) {
                if (slot->object) slot->object->Release();
                slot->object = nullptr;
            }
            gamecrt::operator_delete(group.data);
            group.data = nullptr;
        }
        group.maxSize = 0;
        group.size = 0;
    }
}
EI_REPLACE_METHOD(0x004B35E0, ScriptVars::ResetValues);

BOOL ScriptVars::HasVariable(const char* name) const {
    if (FindName(floatNames, name) >= 0) return 1;
    if (FindName(stringNames, name) >= 0) return 1;
    if (FindName(objectNames, name) >= 0) return 1;
    if (FindName(groupNames, name) >= 0) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x004B36D0, ScriptVars::HasVariable);

BOOL ScriptVars::FindVariable(const char* name, int* index, int* type) const {
    *type = kScriptFloat;
    *index = FindName(floatNames, name);
    if (*index >= 0) return 1;
    *type = kScriptString;
    *index = FindName(stringNames, name);
    if (*index >= 0) return 1;
    *type = kScriptObject;
    *index = FindName(objectNames, name);
    if (*index >= 0) return 1;
    *type = kScriptGroup;
    int found = FindName(groupNames, name);
    *index = found;
    return found >= 0;
}
EI_REPLACE_METHOD(0x004B37D0, ScriptVars::FindVariable);

void ScriptVars::RemoveDeletedObjects() {
    for (int i = 0; i < objects.size; ++i) objects.data[i].ReleaseIfDeleted();
    for (int g = 0; g < groups.size; ++g) groups.data[g].RemoveDeleted();
}
EI_REPLACE_METHOD(0x004B3940, ScriptVars::RemoveDeletedObjects);

// ---- ScriptDefinition::AddParameter -------------------------------------------------------------

namespace {
void __fastcall RefsSetSize(void* a0, void*, int a2, int a3) { PtrArraySetSize(a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall NamesSetAtGrow(void*, void*, int, const mfc::CString*) EI_ORIGINAL(0x004CE5F0)
// 00700B9F operator+(const CString&, const CString&): constructs the result in `out`.
mfc::CString* __stdcall ConcatString(mfc::CString* a0, const mfc::CString* a1, const mfc::CString* a2) { return CStringConcatOriginal(a0, a1, a2); }

// The type letters (0078BBD8 "f", 0078BBD4 "s", 0078BBD0 "o", 0078BBCC "g").
const char* Letter(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
} // namespace

// The new name goes in through a temporary CString: SetAtGrow at the end of the names for floats,
// strings and objects; for groups the names are grown with SetSize and the slot assigned.
BOOL __fastcall ScriptAddParameter(ScriptDefinition* definition, void*, const mfc::CString* name, int type) {
    ScriptVars& vars = definition->vars;
    mfc::CString letter;
    BOOL result;
    const char* text = *name;
    switch (type) {
    case kScriptFloat:
        if (FindName(vars.floatNames, text) >= 0) {
            result = 0;
        } else {
            FloatsSetSize(&vars.floats, nullptr, vars.floats.size + 1, -1);
            mfc::CString temp(text);
            NamesSetAtGrow(&vars.floatNames, nullptr, vars.floatNames.size, &temp);
            result = 1;
        }
        letter = Letter(0x0078BBD8);
        break;
    case kScriptString:
        if (FindName(vars.stringNames, text) >= 0) {
            result = 0;
        } else {
            NamesSetSizeOriginal(&vars.strings, nullptr, vars.strings.size + 1, -1);
            mfc::CString temp(text);
            NamesSetAtGrow(&vars.stringNames, nullptr, vars.stringNames.size, &temp);
            result = 1;
        }
        letter = Letter(0x0078BBD4);
        break;
    case kScriptObject:
        if (FindName(vars.objectNames, text) >= 0) {
            result = 0;
        } else {
            RefsSetSize(&vars.objects, nullptr, vars.objects.size + 1, -1);
            mfc::CString temp(text);
            NamesSetAtGrow(&vars.objectNames, nullptr, vars.objectNames.size, &temp);
            result = 1;
        }
        letter = Letter(0x0078BBD0);
        break;
    case kScriptGroup:
        if (FindName(vars.groupNames, text) >= 0) {
            result = 0;
        } else {
            GroupValuesSetSize(&vars.groups, nullptr, vars.groups.size + 1, -1);
            mfc::CString temp(text);
            int at = vars.groupNames.size;
            NamesSetSizeOriginal(&vars.groupNames, nullptr, at + 1, -1);
            vars.groupNames.data[at] = temp;
            result = 1;
        }
        letter = Letter(0x0078BBCC);
        break;
    default:
        // The original leaves the name argument in the result register: nonzero, and `params`
        // gets the empty letter appended.
        result = static_cast<BOOL>(reinterpret_cast<std::uintptr_t>(name));
        break;
    }
    if (result) {
        alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
        mfc::CString* sum = ConcatString(reinterpret_cast<mfc::CString*>(raw), &definition->params, &letter);
        definition->params = *sum;
        sum->~CString();
    }
    return result;
}
EI_REPLACE(0x004BB800, ScriptAddParameter);

// ---- SSCCmd -------------------------------------------------------------------------------------

void SSCCmd::SetParameters(const mfc::CArray<ScriptBinding>* bindings, ScriptVars* vars) {
    for (int k = 0; k < bindings->size; ++k) {
        const ScriptBinding& b = bindings->data[k];
        switch (b.type) {
        case kScriptFloat:
            CopyFloat(&floatArgs.data[b.argIndex], &vars->floats.data[b.varIndex]);
            break;
        case kScriptString:
            reinterpret_cast<StringValues*>(&vars->strings)->CopyTo(b.varIndex, &stringArgs.data[b.argIndex]);
            break;
        case kScriptObject:
            objectArgs.data[b.argIndex] = &vars->objects.data[b.varIndex];
            break;
        case kScriptGroup:
            groupArgs.data[b.argIndex] = &vars->groups.data[b.varIndex];
            break;
        default:
            game::FatalError("SSCCmd::SetParameters, unknown parameter type");
        }
    }
}
EI_REPLACE_METHOD(0x004B3F60, SSCCmd::SetParameters);

void SSCCmd::Save(ScriptStream* stream) const {
    stream->Write(&id, 4);
    int count = floatArgs.size;
    stream->Write(&count, 4);
    for (int i = 0; i < floatArgs.size; ++i) stream->Write(&floatArgs.data[i], 4);

    count = stringArgs.size;
    stream->Write(&count, 4);
    for (int i = 0; i < stringArgs.size; ++i) {
        const char* text = stringArgs.data[i];
        int length = reinterpret_cast<const int*>(text)[-2];   // CStringData::nDataLength
        stream->Write(text, length + 1);
    }

    // Object and group arguments are only counted: they are re-bound on load.
    count = objectArgs.size;
    stream->Write(&count, 4);
    count = groupArgs.size;
    stream->Write(&count, 4);

    count = localBindings.size;
    stream->Write(&count, 4);
    for (int i = 0; i < localBindings.size; ++i) stream->Write(&localBindings.data[i], 12);
    count = globalBindings.size;
    stream->Write(&count, 4);
    for (int i = 0; i < globalBindings.size; ++i) stream->Write(&globalBindings.data[i], 12);

    count = subCalls.size;
    stream->Write(&count, 4);
    for (int i = 0; i < subCalls.size; ++i) {
        const ScriptSubCall& call = subCalls.data[i];
        stream->Write(&call.type, 4);
        stream->Write(&call.argIndex, 4);
        call.cmd->Save(stream);
    }

    int scriptIndex = -1;
    if (calledScript) scriptIndex = calledScript->id;
    stream->Write(&scriptIndex, 4);
}
EI_REPLACE_METHOD(0x004B6360, SSCCmd::Save);
