// Saving the mission script into a savegame (Script::SaveState) and the savers of its parts:
// command blocks, the four kinds of variables, the scheduler's state in the old format's fields and the areas. See
// script.h and docs/notes/script.md.
// Every piece goes through the stream's Write in the save's order and in its pieces.
#include "game/core/core_streams.h"
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

void ScriptBlock::Save(ByteStream* stream) const {
    stream->WriteValue(id);
    WriteList(stream, commands);
}

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

// A definition (Script::definitions) or an instance (instances). The pairs' blocks are not null-checked.
void ScriptDefinition::Save(ByteStream* stream) const {
    stream->WriteValue<std::int32_t>(killed);
    stream->WriteString(name);
    stream->WriteString(params);
    stream->WriteCount(ifThen.size());
    for (const ScriptIfThen& pair : ifThen) {
        stream->WriteValue(pair.conditions->id);
        stream->WriteValue(pair.commands->id);
    }
    vars.Save(stream);
}

namespace {

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
// values (a definition the program lacks: its name alone); each run with its rule's then-section (a WorldScript's: -1,
// no block), command -1 (Lua has none). The fields the game's executor wrote its state in, so tools/savefile.py reads
// them as it reads an old-format save; LoadState drops them again (the Lua state after the areas is the script's).
void WriteLuaState(ByteStream* stream, Script* script) {
    script->GlobalValues(true).Save(stream);
    std::unordered_map<std::string_view, const ScriptDefinition*> definitions;
    for (const ScriptDefinition& definition : script->definitions) definitions.emplace(definition.name, &definition);
    const std::vector<ScriptInstanceView> instances = script->InstanceViews();
    stream->WriteCount(instances.size());
    for (const ScriptInstanceView& view : instances) {
        ScriptDefinition record;
        if (const auto found = definitions.find(view.name); found != definitions.end()) record = *found->second;
        record.name = view.name;
        record.killed = view.killed;
        record.vars = view.vars;
        record.Save(stream);
    }
    const std::vector<ScriptRunView> runs = script->RunViews();
    stream->WriteCount(runs.size());
    for (const ScriptRunView& run : runs) {
        int block = -1;
        if (const auto found = definitions.find(run.script); found != definitions.end() && run.rule >= 0 &&
                                                              run.rule < std::ssize(found->second->ifThen))
            block = found->second->ifThen[static_cast<std::size_t>(run.rule)].commands->id;
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
    WriteList(stream, blocks);
    WriteList(stream, worldBlocks);
    WriteList(stream, definitions);
    WriteLuaState(stream, this);
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
