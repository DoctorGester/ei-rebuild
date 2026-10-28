// Saving the mission script into a savegame (CScript::SaveState) and the savers of its parts:
// command blocks, the four kinds of variables and the areas. See script.h and docs/notes/script.md.
// Every piece goes through the stream's Write in the save's order and in its
// pieces (a verify wrapper compares the pieces as well as the bytes).
#include "script.h"

#include <cstdint>

namespace {

void WriteInt(ScriptStream* stream, int value) { stream->Write(&value, 4); }

// Variable i's name, or a single zero byte when the name array is shorter.
void WriteName(ScriptStream* stream, const std::vector<std::string>& names, std::size_t i) {
    if (i < names.size()) {
        stream->WriteString(names[i]);
    } else {
        int zero = 0;
        stream->Write(&zero, 1);
    }
}

int ObjectId(const ScriptObjectRef& ref) { return ref.object ? ref.object->id_ : -1; }

// A ScriptVars: floats, strings, objects, groups.
void WriteVars(ScriptStream* stream, const ScriptVars* vars) {
    vars->floats.Save(stream);
    vars->strings.Save(stream);
    vars->objects.Save(stream);
    vars->groups.Save(stream);
}

// A list's count, then each element's Save.
template <typename Container>
void WriteList(ScriptStream* stream, const Container& list) {
    WriteInt(stream, static_cast<int>(list.size()));
    for (const auto& element : list) element.Save(stream);
}

} // namespace

void ScriptDecl::Save(ScriptStream* stream) const {
    stream->Write(&id, 4);
    WriteList(stream, commands);
}

template <> void ScriptNamedValues<float>::Save(ScriptStream* stream) const {
    WriteInt(stream, static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        stream->Write(&values[i], 4);
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<std::string>::Save(ScriptStream* stream) const {
    WriteInt(stream, static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        stream->WriteString(values[i]);
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<ScriptObjectRef>::Save(ScriptStream* stream) const {
    WriteInt(stream, static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        WriteInt(stream, ObjectId(values[i]));
        WriteName(stream, names, i);
    }
}

template <> void ScriptNamedValues<ScriptGroup>::Save(ScriptStream* stream) const {
    WriteInt(stream, static_cast<int>(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const ScriptGroup& group = values[i];
        WriteInt(stream, static_cast<int>(group.size()));
        for (const ScriptObjectRef& member : group) WriteInt(stream, ObjectId(member));
        WriteName(stream, names, i);
    }
}

// A definition (CScript::definitions_) or an instance (instances_). The pairs' blocks are not null-checked.
void ScriptDefinition::Save(ScriptStream* stream) const {
    stream->Write(&index, 4);
    stream->WriteString(name);
    stream->WriteString(params);
    WriteInt(stream, static_cast<int>(ifThen.size()));
    for (const ScriptIfThen& pair : ifThen) {
        WriteInt(stream, pair.conditions->id);
        WriteInt(stream, pair.commands->id);
    }
    WriteVars(stream, &vars);
}

// A run (CScript::runs_). Its instance is found by the address of the killed flag (the instance's
// index) in the list of the script being saved; the WorldScript's runs (no flag) write -1.
void ScriptRun::Save(ScriptStream* stream) const {
    WriteInt(stream, block->id);
    stream->Write(&index, 4);
    stream->Write(&ticks, 4);
    stream->Write(&sleepStart, 4);
    int instance = 0;
    bool found = false;
    for (const ScriptDefinition& candidate : g_streamedScript->instances_) {
        if (killFlag == &candidate.index) {
            found = true;
            break;
        }
        ++instance;
    }
    WriteInt(stream, found ? instance : -1);
    WriteVars(stream, &vars);
}

void CScript::SaveState(ScriptStream* stream) {
    g_streamedScript = this;

    WriteList(stream, pointLights_);
    WriteList(stream, particles_);
    WriteList(stream, fxSources_);
    WriteList(stream, lightning_);
    stream->Write(&nextParticleId_, 4);
    stream->Write(&nextFxId_, 4);
    WriteList(stream, blocks_);
    WriteList(stream, worldBlocks_);
    WriteList(stream, definitions_);

    WriteVars(stream, &globals_);

    // Instances (copies of definitions) and runs.
    WriteList(stream, instances_);
    WriteList(stream, runs_);

    stream->Write(&loaded_, 4);
    stream->Write(&fileCount_, 4);
    g_streamedScript = nullptr;

    // The areas: count, then per area its id, part count and parts.
    WriteInt(stream, static_cast<int>(areas_.size()));
    for (const ScriptArea& area : areas_) {
        WriteInt(stream, area.id);
        WriteInt(stream, static_cast<int>(area.parts.size()));
        for (const ScriptAreaPart& part : area.parts) stream->Write(&part, sizeof(ScriptAreaPart));
    }
}
