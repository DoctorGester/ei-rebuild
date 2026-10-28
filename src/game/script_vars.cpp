// Script variables (lookups, resets, the loader's appends of a file's GlobalVars to the globals) and the
// SSCCmd helpers that bind and save call trees. See script.h and docs/notes/script.md.
#include "ei/x87.h"
#include "script.h"
#include "game_strings.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

int FindName(const std::vector<std::string>& names, std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

// A new variable named `name` at the end of `vars` unless the name is taken.
template <typename T>
bool AddVariable(ScriptNamedValues<T>& vars, std::string_view name) {
    if (::FindName(vars.names, name) >= 0) return false;
    vars.values.resize(vars.values.size() + 1);
    vars.names.emplace_back(name);
    return true;
}

} // namespace

// The values grow by the other set's value count, the names to the old value count plus the other set's name
// count; then, for every one of the other set's names, the name and the value with the same index go into the
// new slots.
template <typename T>
void ScriptNamedValues<T>::Append(const ScriptNamedValues<T>& other) {
    const std::size_t old = values.size();
    values.resize(old + other.values.size());
    names.resize(old + other.names.size());
    for (std::size_t i = 0; i < other.names.size(); ++i) {
        names[old + i] = other.names[i];
        values[old + i] = other.values[i];
    }
}
template void ScriptNamedValues<float>::Append(const ScriptNamedValues<float>&);
template void ScriptNamedValues<std::string>::Append(const ScriptNamedValues<std::string>&);
template void ScriptNamedValues<ScriptObjectRef>::Append(const ScriptNamedValues<ScriptObjectRef>&);
template void ScriptNamedValues<ScriptGroup>::Append(const ScriptNamedValues<ScriptGroup>&);

// The loader's GlobalVars: floats, strings, objects, groups in that order.
void ScriptVars::AppendFrom(const ScriptVars& other) {
    floats.Append(other.floats);
    strings.Append(other.strings);
    objects.Append(other.objects);
    groups.Append(other.groups);
}

void ScriptVars::ResetValues() {
    for (float& value : floats.values) value = 0.0f;
    for (std::string& value : strings.values) value = game::kEmptyText;
    for (ScriptObjectRef& value : objects.values) value.object = nullptr;
    for (ScriptGroup& group : groups.values) group.clear();
}

void ScriptVars::Clear() {
    floats.values.clear();
    floats.names.clear();
    strings.values.clear();
    strings.names.clear();
    objects.values.clear();
    objects.names.clear();
    groups.values.clear();
    groups.names.clear();
}

BOOL ScriptVars::HasVariable(std::string_view name) const {
    if (::FindName(floats.names, name) >= 0) return 1;
    if (::FindName(strings.names, name) >= 0) return 1;
    if (::FindName(objects.names, name) >= 0) return 1;
    if (::FindName(groups.names, name) >= 0) return 1;
    return 0;
}

BOOL ScriptVars::FindVariable(std::string_view name, int* index, int* type) const {
    *type = kScriptFloat;
    *index = ::FindName(floats.names, name);
    if (*index >= 0) return 1;
    *type = kScriptString;
    *index = ::FindName(strings.names, name);
    if (*index >= 0) return 1;
    *type = kScriptObject;
    *index = ::FindName(objects.names, name);
    if (*index >= 0) return 1;
    *type = kScriptGroup;
    int found = ::FindName(groups.names, name);
    *index = found;
    return found >= 0;
}

void ScriptVars::RemoveDeletedObjects() {
    for (ScriptObjectRef& value : objects.values) value.ReleaseIfDeleted();
    for (ScriptGroup& group : groups.values) group.RemoveDeleted();
}

// ---- ScriptDefinition::AddParameter -------------------------------------------------------------

// An unknown type is accepted and adds no letter.
BOOL ScriptDefinition::AddParameter(const std::string* parameterName, int type) {
    const std::string& text = *parameterName;
    switch (type) {
    case kScriptFloat:
        if (!AddVariable(vars.floats, text)) return 0;
        params += "f";
        return 1;
    case kScriptString:
        if (!AddVariable(vars.strings, text)) return 0;
        params += "s";
        return 1;
    case kScriptObject:
        if (!AddVariable(vars.objects, text)) return 0;
        params += "o";
        return 1;
    case kScriptGroup:
        if (!AddVariable(vars.groups, text)) return 0;
        params += "g";
        return 1;
    default:
        return 1;
    }
}

void SSCCmd::SetParameters(const std::vector<ScriptBinding>& bindings, ScriptVars* vars) {
    for (const ScriptBinding& b : bindings) {
        switch (b.type) {
        case kScriptFloat:
            x87::CopyBits(floatArgs[b.argIndex], vars->floats.values[b.varIndex]);
            break;
        case kScriptString:
            stringArgs[b.argIndex] = vars->strings.values[b.varIndex];
            break;
        case kScriptObject:
            objectArgs[b.argIndex] = &vars->objects.values[b.varIndex];
            break;
        case kScriptGroup:
            groupArgs[b.argIndex] = &vars->groups.values[b.varIndex];
            break;
        default:
            game::FatalError("SSCCmd::SetParameters, unknown parameter type");
        }
    }
}

void SSCCmd::Clear() {
    id = 0;
    floatArgs.clear();
    stringArgs.clear();
    objectArgs.clear();
    groupArgs.clear();
    localBindings.clear();
    globalBindings.clear();
    subCalls.clear();
    calledScript = nullptr;
}

void SSCCmd::Save(CStream* stream) const {
    stream->WriteValue(id);
    int count = static_cast<int>(floatArgs.size());
    stream->WriteValue(count);
    for (const float& value : floatArgs) stream->WriteValue(value);

    count = static_cast<int>(stringArgs.size());
    stream->WriteValue(count);
    for (const std::string& text : stringArgs) stream->WriteString(text);

    // Object and group arguments are only counted: they are re-bound on load.
    count = static_cast<int>(objectArgs.size());
    stream->WriteValue(count);
    count = static_cast<int>(groupArgs.size());
    stream->WriteValue(count);

    count = static_cast<int>(localBindings.size());
    stream->WriteValue(count);
    for (const ScriptBinding& binding : localBindings) stream->WriteValue(binding);
    count = static_cast<int>(globalBindings.size());
    stream->WriteValue(count);
    for (const ScriptBinding& binding : globalBindings) stream->WriteValue(binding);

    count = static_cast<int>(subCalls.size());
    stream->WriteValue(count);
    for (const ScriptSubCall& call : subCalls) {
        stream->WriteValue(call.type);
        stream->WriteValue(call.argIndex);
        call.cmd->Save(stream);
    }

    int scriptIndex = -1;
    if (calledScript) scriptIndex = calledScript->id;
    stream->WriteValue(scriptIndex);
}
