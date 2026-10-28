// Script variables and the SSCCmd helpers that bind and save call trees. See script.h.
#include "script.h"
#include "game_strings.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "game_arrays.h"
#include "ei/x87.h"

#include <cstdint>
#include <memory>
#include <string>

namespace {

// The empty string constant the game assigns to clear string variables.


int FindName(const mfc::CArray<std::string>& names, const char* name) {
    for (int i = 0; i < names.size; ++i)
        if (gamecrt::mbscmp(names.data[i].c_str(), name) == 0) return i;
    return -1;
}

} // namespace


void ScriptVars::ResetValues() {
    for (int i = 0; i < floats.values.size; ++i) floats.values.data[i] = 0.0f;
    for (int i = 0; i < strings.values.size; ++i) strings.values.data[i] = game::EmptyText();
    for (int i = 0; i < objects.values.size; ++i) {
        ScriptObjectRef* slot = &objects.values.data[i];
        if (slot->object) slot->object->Release();
        slot->object = nullptr;
    }
    for (int g = 0; g < groups.values.size; ++g) {
        ScriptGroup& group = groups.values.data[g];
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

BOOL ScriptVars::HasVariable(const char* name) const {
    if (FindName(floats.names, name) >= 0) return 1;
    if (FindName(strings.names, name) >= 0) return 1;
    if (FindName(objects.names, name) >= 0) return 1;
    if (FindName(groups.names, name) >= 0) return 1;
    return 0;
}

BOOL ScriptVars::FindVariable(const char* name, int* index, int* type) const {
    *type = kScriptFloat;
    *index = FindName(floats.names, name);
    if (*index >= 0) return 1;
    *type = kScriptString;
    *index = FindName(strings.names, name);
    if (*index >= 0) return 1;
    *type = kScriptObject;
    *index = FindName(objects.names, name);
    if (*index >= 0) return 1;
    *type = kScriptGroup;
    int found = FindName(groups.names, name);
    *index = found;
    return found >= 0;
}

void ScriptVars::RemoveDeletedObjects() {
    for (int i = 0; i < objects.values.size; ++i) objects.values.data[i].ReleaseIfDeleted();
    for (int g = 0; g < groups.values.size; ++g) groups.values.data[g].RemoveDeleted();
}

// ---- ScriptDefinition::AddParameter -------------------------------------------------------------

// The new name goes in through a temporary CString: SetAtGrow at the end of the names for floats,
// strings and objects; for groups the names are grown with SetSize and the slot assigned.
BOOL ScriptDefinition::AddParameter(const std::string* parameterName, int type) {
    const char* letter = "";   // the type letter: "f", "s", "o", "g"
    BOOL result;
    const char* text = (*parameterName).c_str();
    switch (type) {
    case kScriptFloat:
        if (FindName(vars.floats.names, text) >= 0) {
            result = 0;
        } else {
            vars.floats.values.SetSize(vars.floats.values.size + 1, -1);
            std::string temp = text;
            vars.floats.names.SetAtGrow(vars.floats.names.size, temp);
            result = 1;
        }
        letter = "f";
        break;
    case kScriptString:
        if (FindName(vars.strings.names, text) >= 0) {
            result = 0;
        } else {
            vars.strings.values.SetSize(vars.strings.values.size + 1, -1);
            std::string temp = text;
            vars.strings.names.SetAtGrow(vars.strings.names.size, temp);
            result = 1;
        }
        letter = "s";
        break;
    case kScriptObject:
        if (FindName(vars.objects.names, text) >= 0) {
            result = 0;
        } else {
            mfc::impl::ArrayAs<gamearrays::ObjectRefs>(vars.objects.values).SetSize(vars.objects.values.size + 1, -1);
            std::string temp = text;
            vars.objects.names.SetAtGrow(vars.objects.names.size, temp);
            result = 1;
        }
        letter = "o";
        break;
    case kScriptGroup:
        if (FindName(vars.groups.names, text) >= 0) {
            result = 0;
        } else {
            mfc::impl::ArrayAs<gamearrays::Groups>(vars.groups.values).SetSize(vars.groups.values.size + 1, -1);
            std::string temp = text;
            int at = vars.groups.names.size;
            vars.groups.names.SetSize(at + 1, -1);
            vars.groups.names.data[at] = temp;
            result = 1;
        }
        letter = "g";
        break;
    default:
        // The name argument is left in the result register: nonzero, and `params`
        // gets the empty letter appended.
        result = static_cast<BOOL>(reinterpret_cast<std::uintptr_t>(parameterName));
        break;
    }
    if (result) params += letter;
    return result;
}

void SSCCmd::SetParameters(const ScriptBindings* bindings, ScriptVars* vars) {
    for (int k = 0; k < bindings->size; ++k) {
        const ScriptBinding& b = bindings->data[k];
        switch (b.type) {
        case kScriptFloat:
            std::memcpy(&floatArgs.data[b.argIndex], &vars->floats.values.data[b.varIndex], sizeof(float));
            break;
        case kScriptString:
            vars->strings.values.CopyTo(b.varIndex, &stringArgs.data[b.argIndex]);
            break;
        case kScriptObject:
            objectArgs.data[b.argIndex] = &vars->objects.values.data[b.varIndex];
            break;
        case kScriptGroup:
            groupArgs.data[b.argIndex] = &vars->groups.values.data[b.varIndex];
            break;
        default:
            game::FatalError("SSCCmd::SetParameters, unknown parameter type");
        }
    }
}

void SSCCmd::Save(ScriptStream* stream) const {
    stream->Write(&id, 4);
    int count = floatArgs.size;
    stream->Write(&count, 4);
    for (int i = 0; i < floatArgs.size; ++i) stream->Write(&floatArgs.data[i], 4);

    count = stringArgs.size;
    stream->Write(&count, 4);
    for (int i = 0; i < stringArgs.size; ++i) {
        stream->WriteString(stringArgs.data[i]);
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
