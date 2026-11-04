// Script variables (lookups, resets, the loader's appends of a file's GlobalVars to the globals) and the
// SSCCmd helpers that bind and save call trees. See script.h and docs/notes/script.md.
#include "script.h"
#include "map_object.h"
#include "game_strings.h"
#include "core_streams.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// A new variable named `name` at the end of `vars` unless the name is taken.
template <typename T>
bool AddVariable(ScriptNamedValues<T>& vars, std::string_view name) {
    if (vars.Find(name) >= 0) return false;
    vars.values.resize(vars.values.size() + 1);
    vars.names.emplace_back(name);
    return true;
}

} // namespace

template <typename T>
int ScriptNamedValues<T>::Find(std::string_view name) const {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}
template int ScriptNamedValues<float>::Find(std::string_view) const;
template int ScriptNamedValues<std::string>::Find(std::string_view) const;
template int ScriptNamedValues<ScriptObjectRef>::Find(std::string_view) const;
template int ScriptNamedValues<ScriptGroup>::Find(std::string_view) const;

template <typename T>
void ScriptNamedValues<T>::Append(const ScriptNamedValues<T>& other) {
    values.insert(values.end(), other.values.begin(), other.values.end());
    names.insert(names.end(), other.names.begin(), other.names.end());
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
    for (ScriptGroup& group : groups.values) group.Clear();
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

bool ScriptVars::HasVariable(std::string_view name) const {
    return floats.Find(name) >= 0 || strings.Find(name) >= 0 || objects.Find(name) >= 0 || groups.Find(name) >= 0;
}

ScriptVarRef ScriptVars::FindVariable(std::string_view name) const {
    int index = floats.Find(name);
    if (index >= 0) return {index, kScriptFloat};
    index = strings.Find(name);
    if (index >= 0) return {index, kScriptString};
    index = objects.Find(name);
    if (index >= 0) return {index, kScriptObject};
    return {groups.Find(name), kScriptGroup};
}

int ScriptVars::FindOfType(int type, std::string_view name) const {
    switch (type) {
    case kScriptFloat: return floats.Find(name);
    case kScriptString: return strings.Find(name);
    case kScriptObject: return objects.Find(name);
    default: return groups.Find(name);
    }
}

void ScriptVars::RemoveDeletedObjects() {
    for (ScriptObjectRef& value : objects.values) value.ReleaseIfDeleted();
    for (ScriptGroup& group : groups.values) group.RemoveDeleted();
}

// ---- ScriptDefinition::AddParameter -------------------------------------------------------------

// An unknown type is accepted and adds no letter.
bool ScriptDefinition::AddParameter(const std::string* parameterName, int type) {
    const std::string& text = *parameterName;
    bool added;
    switch (type) {
    case kScriptFloat: added = AddVariable(vars.floats, text); break;
    case kScriptString: added = AddVariable(vars.strings, text); break;
    case kScriptObject: added = AddVariable(vars.objects, text); break;
    case kScriptGroup: added = AddVariable(vars.groups, text); break;
    default: return true;
    }
    if (added) params += ScriptTypeLetter(type);
    return added;
}

void SSCCmd::SetParameters(const std::vector<ScriptBinding>& bindings, ScriptVars* vars) {
    for (const ScriptBinding& b : bindings) {
        switch (b.type) {
        case kScriptFloat:
            floatArgs[b.argIndex] = vars->floats.values[b.varIndex];
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

void SSCCmd::Save(ByteStream* stream) const {
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
