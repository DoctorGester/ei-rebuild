// Saving the mission script into a savegame (Script::SaveState) and the savers of its parts:
// command blocks, the four kinds of variables and the areas. See script.h and docs/notes/script.md.
// Every piece goes through the stream's Write in the save's order and in its
// pieces (a verify wrapper compares the pieces as well as the bytes).
#include "core_streams.h"
#include "script.h"

#include <cstdint>

namespace {

// Variable i's name, or a single zero byte when the name array is shorter.
void WriteName(ByteStream* stream, const std::vector<std::string>& names, std::size_t i) {
    if (i < names.size()) {
        stream->WriteString(names[i]);
    } else {
        int zero = 0;
        stream->Write(&zero, 1);
    }
}

// A ScriptVars: floats, strings, objects, groups.
void WriteVars(ByteStream* stream, const ScriptVars* vars) {
    vars->floats.Save(stream);
    vars->strings.Save(stream);
    vars->objects.Save(stream);
    vars->groups.Save(stream);
}

// A list's count, then each element's Save.
template <typename Container>
void WriteList(ByteStream* stream, const Container& list) {
    stream->WriteValue(static_cast<int>(list.size()));
    for (const auto& element : list) element.Save(stream);
}

} // namespace

void ScriptDecl::Save(ByteStream* stream) const {
    stream->WriteValue(id);
    WriteList(stream, commands);
}

template <> void ScriptNamedValues<float>::Save(ByteStream* stream) const {
    stream->WriteValue(static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        stream->WriteValue(values[i]);
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<std::string>::Save(ByteStream* stream) const {
    stream->WriteValue(static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        stream->WriteString(values[i]);
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<ScriptObjectRef>::Save(ByteStream* stream) const {
    stream->WriteValue(static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i].Save(stream);
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<ScriptGroup>::Save(ByteStream* stream) const {
    stream->WriteValue(static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const ScriptGroup& group = values[i];
        stream->WriteValue(static_cast<int>(group.size()));
        for (const ScriptObjectRef& member : group) member.Save(stream);
        WriteName(stream, names, i);
    }
}

// A definition (Script::definitions) or an instance (instances). The pairs' blocks are not null-checked.
void ScriptDefinition::Save(ByteStream* stream) const {
    stream->WriteValue(index);
    stream->WriteString(name);
    stream->WriteString(params);
    stream->WriteValue(static_cast<int>(ifThen.size()));
    for (const ScriptIfThen& pair : ifThen) {
        stream->WriteValue(pair.conditions->id);
        stream->WriteValue(pair.commands->id);
    }
    WriteVars(stream, &vars);
}

// A run (Script::runs). Its instance is found by the address of the killed flag (the instance's
// index) in the list of the script being saved; the WorldScript's runs (no flag) write -1.
void ScriptRun::Save(ByteStream* stream) const {
    stream->WriteValue(block->id);
    stream->WriteValue(index);
    stream->WriteValue(ticks);
    stream->WriteValue(sleepStart);
    int instance = 0;
    bool found = false;
    for (const ScriptDefinition& candidate : g_streamedScript->instances) {
        if (killFlag == &candidate.index) {
            found = true;
            break;
        }
        ++instance;
    }
    stream->WriteValue(found ? instance : -1);
    WriteVars(stream, &vars);
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

    WriteVars(stream, &globals);

    // Instances (copies of definitions) and runs.
    WriteList(stream, instances);
    WriteList(stream, runs);

    stream->WriteValue(loaded);
    stream->WriteValue(fileCount);
    g_streamedScript = nullptr;

    // The areas: count, then per area its id, part count and parts.
    stream->WriteValue(static_cast<int>(areas.size()));
    for (const ScriptArea& area : areas) {
        stream->WriteValue(area.id);
        stream->WriteValue(static_cast<int>(area.parts.size()));
        for (const ScriptAreaPart& part : area.parts) stream->WriteValue(part);
    }
}
