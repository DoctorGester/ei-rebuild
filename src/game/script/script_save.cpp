// Saving the mission script into a savegame (Script::SaveState) and the savers of its parts:
// the definitions, the four kinds of variables, the scheduler's state in the old format's fields and the areas. The
// program is the scheduler's state; the old format's blocks are written empty. See script.h and docs/notes/script.md.
// Every piece goes through the stream's Write in the save's order and in its pieces.
#include "engine/core/core_streams.h"
#include "game/script/script.h"
#include "game/script/script_lua_scheduler.h"
#include "game/console/console.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace {

// ScriptNamedValues::Save writes a value by the overload for its type.
void WriteOne(ByteStream* stream, float value) { stream->WriteValue(value); }
void WriteOne(ByteStream* stream, const std::string& value) { stream->WriteString(value); }
void WriteOne(ByteStream* stream, const ScriptObjectRef& value) { value.Save(stream); }
void WriteOne(ByteStream* stream, const ScriptGroup& value) { value.Save(stream); }

// A list's count, then each element's Save.
template <typename Container>
void WriteList(ByteStream* stream, const Container& list) {
    stream->WriteCount(list.size());
    for (const auto& element : list) element.Save(stream);
}

} // namespace

template <typename T>
void ScriptNamedValues<T>::Save(ByteStream* stream) const {
    stream->WriteCount(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        WriteOne(stream, values[i]);
        stream->WriteString(i < names.size() ? std::string_view(names[i]) : std::string_view());
    }
}
template void ScriptNamedValues<float>::Save(ByteStream*) const;
template void ScriptNamedValues<std::string>::Save(ByteStream*) const;
template void ScriptNamedValues<ScriptObjectRef>::Save(ByteStream*) const;
template void ScriptNamedValues<ScriptGroup>::Save(ByteStream*) const;

void ScriptVars::Save(ByteStream* stream) const {
    floats.Save(stream);
    strings.Save(stream);
    objects.Save(stream);
    groups.Save(stream);
}

// A definition (Script::definitions) or an instance (the scheduler's, as its definition). The pairs' numbers only tie
// the saved runs to their rules (tools/savefile.py names a run by them), and their count is the rule count a later
// text's redeclaration is checked against.
void ScriptDefinition::Save(ByteStream* stream, int firstRule) const {
    stream->WriteValue<std::int32_t>(killed);
    stream->WriteString(name);
    stream->WriteString(params);
    stream->WriteCount(static_cast<std::size_t>(rules));
    for (int rule = 0; rule < rules; ++rule) {
        stream->WriteValue<std::int32_t>(0);
        stream->WriteValue<std::int32_t>(firstRule + rule);
    }
    vars.Save(stream);
}

namespace {

// A definition and the save's number of its first rule (ScriptDefinition::Save), by name.
struct NumberedDefinition {
    const ScriptDefinition* definition = nullptr;
    int firstRule = 0;
};
using NumberedDefinitions = std::unordered_map<std::string_view, NumberedDefinition>;

// A run's fields: block id, command index, ticks, sleep start, its instance's index (-1: none), the variables.
void WriteRun(ByteStream* stream, int block, int index, int ticks, int sleepStart, int instance, const ScriptVars& vars) {
    stream->WriteValue(block);
    stream->WriteValue(index);
    stream->WriteValue(ticks);
    stream->WriteValue(sleepStart);
    stream->WriteValue(instance);
    vars.Save(stream);
}

// The scheduler's globals, instances and runs in the old format's fields, as far as they go (ScriptInstanceView):
// the globals the parser declared with their Lua values; each instance as its definition with its kill flag and
// values (a definition the program lacks: its name alone); each run with its rule's number as the block (a
// WorldScript's: -1), command -1 (Lua has none). The fields the original game wrote its state in, so
// tools/savefile.py reads them as it reads an old-format save; LoadState drops them again (the Lua state after the
// areas is the script's).
void WriteLuaState(ByteStream* stream, Script* script, const NumberedDefinitions& definitions) {
    script->GlobalValues(true).Save(stream);
    const std::vector<ScriptInstanceView> instances = script->InstanceViews();
    stream->WriteCount(instances.size());
    for (const ScriptInstanceView& view : instances) {
        ScriptDefinition record;
        int firstRule = 0;
        if (const auto found = definitions.find(view.name); found != definitions.end()) {
            record = *found->second.definition;
            firstRule = found->second.firstRule;
        }
        record.name = view.name;
        record.killed = view.killed;
        record.vars = view.vars;
        record.Save(stream, firstRule);
    }
    const std::vector<ScriptRunView> runs = script->RunViews();
    stream->WriteCount(runs.size());
    for (const ScriptRunView& run : runs) {
        int block = -1;
        if (const auto found = definitions.find(run.script);
            found != definitions.end() && run.rule >= 0 && run.rule < found->second.definition->rules)
            block = found->second.firstRule + run.rule;
        WriteRun(stream, block, -1, run.ticks, run.sleepStart, run.instance, run.vars);
    }
}

} // namespace

void Script::SaveState(ByteStream* stream) {
    WriteList(stream, pointLights);
    WriteList(stream, particles);
    WriteList(stream, fxSources);
    WriteList(stream, lightning);
    stream->WriteValue(nextParticleId);
    stream->WriteValue(nextFxId);

    // No command blocks or WorldScript blocks: the scheduler's state holds the program. The definitions with their
    // rules numbered from 1 in this order.
    stream->WriteCount(0);
    stream->WriteCount(0);
    NumberedDefinitions numbered;
    int nextRule = 1;
    stream->WriteCount(definitions.size());
    for (const ScriptDefinition& definition : definitions) {
        definition.Save(stream, nextRule);
        numbered.emplace(definition.name, NumberedDefinition{&definition, nextRule});
        nextRule += definition.rules;
    }
    WriteLuaState(stream, this, numbered);
    stream->WriteValue(loaded + kScriptLoadedOnLua);
    stream->WriteValue(fileCount);

    // The areas: count, then per area its id, part count and parts.
    stream->WriteCount(areas.size());
    for (const ScriptArea& area : areas) {
        stream->WriteValue(area.id);
        stream->WriteCount(area.parts.size());
        for (const ScriptAreaPart& part : area.parts) stream->WriteValue(part);
    }

    // The scheduler's whole state (Eris; docs/notes/lua-scripting.md, "Saves"). One that does not persist (a value
    // Eris cannot write) saves as none, and the loaded game's script runs nothing.
    std::string data, error;
    if (!Runner().Save(&data, &error)) {
        console::EndLine(console::g_consoleOut << "Script error: the Lua state did not save: " << error);
        data.clear();
    }
    stream->WriteCount(data.size());
    stream->Write(data.data(), static_cast<std::uint32_t>(data.size()));
}
