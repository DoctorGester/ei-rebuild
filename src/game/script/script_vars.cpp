// Script variables (lookups, resets, the loader's appends of a file's GlobalVars to the globals) and the
// ScriptCall's reset. See script.h and docs/notes/script.md.
#include "game/script/script.h"
#include "game/objects/map_object.h"
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
    for (std::string& value : strings.values) value.clear();
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
    if (index >= 0) return {index, ScriptType::Float};
    index = strings.Find(name);
    if (index >= 0) return {index, ScriptType::String};
    index = objects.Find(name);
    if (index >= 0) return {index, ScriptType::Object};
    return {groups.Find(name), ScriptType::Group};
}

int ScriptVars::FindOfType(ScriptType type, std::string_view name) const {
    switch (type) {
    case ScriptType::Float: return floats.Find(name);
    case ScriptType::String: return strings.Find(name);
    case ScriptType::Object: return objects.Find(name);
    default: return groups.Find(name);
    }
}

// ---- ScriptDefinition::AddParameter -------------------------------------------------------------

// An unknown type is accepted and adds no letter.
bool ScriptDefinition::AddParameter(const std::string& parameterName, ScriptType type) {
    const std::string& text = parameterName;
    bool added;
    switch (type) {
    case ScriptType::Float: added = AddVariable(vars.floats, text); break;
    case ScriptType::String: added = AddVariable(vars.strings, text); break;
    case ScriptType::Object: added = AddVariable(vars.objects, text); break;
    case ScriptType::Group: added = AddVariable(vars.groups, text); break;
    default: return true;
    }
    if (added) params += ScriptTypeLetter(type);
    return added;
}

void ScriptCall::Clear() {
    id = ScriptFunction{};
    target = 0;
    targetType = ScriptType::None;
    targetName.clear();
    floatArgs.clear();
    stringArgs.clear();
    objectArgs.clear();
    groupArgs.clear();
    localBindings.clear();
    globalBindings.clear();
    subCalls.clear();
    body = nullptr;
}
