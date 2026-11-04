// Saving the mission script into a savegame (Script::SaveState) and the savers of its parts:
// command blocks, the four kinds of variables and the areas. See script.h and docs/notes/script.md.
// Every piece goes through the stream's Write in the save's order and in its pieces.
#include "core_streams.h"
#include "script.h"

#include <cstdint>

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

// A run (Script::runs). Its instance is found by the address of the killed flag
// in the list of the script being saved; the WorldScript's runs (no flag) write -1.
void ScriptRun::Save(ByteStream* stream) const {
    stream->WriteValue(position.block->id);
    stream->WriteValue(position.index);
    stream->WriteValue(position.ticks);
    stream->WriteValue(position.sleepStart);
    int instance = 0;
    bool found = false;
    for (const ScriptDefinition& candidate : g_streamedScript->instances) {
        if (killFlag == &candidate.killed) {
            found = true;
            break;
        }
        ++instance;
    }
    stream->WriteValue(found ? instance : -1);
    vars.Save(stream);
}

void Script::SaveState(ByteStream* stream) {
    g_streamedScript = this;

    WriteList(stream, pointLights);
    WriteList(stream, particles);
    WriteList(stream, fxSources);
    WriteList(stream, lightning);
    stream->WriteValue(nextParticleId);
    stream->WriteValue(nextFxId);
    WriteList(stream, blocks);
    WriteList(stream, worldBlocks);
    WriteList(stream, definitions);

    globals.Save(stream);

    // Instances (copies of definitions) and runs.
    WriteList(stream, instances);
    WriteList(stream, runs);

    stream->WriteValue(loaded);
    stream->WriteValue(fileCount);
    g_streamedScript = nullptr;

    // The areas: count, then per area its id, part count and parts.
    stream->WriteCount(areas.size());
    for (const ScriptArea& area : areas) {
        stream->WriteValue(area.id);
        stream->WriteCount(area.parts.size());
        for (const ScriptAreaPart& part : area.parts) stream->WriteValue(part);
    }
}
