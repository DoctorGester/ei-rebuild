// Loading the mission script from a savegame (CScript::LoadState) and the loaders of its parts:
// the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs. The mirror image of script_save.cpp; see
// docs/notes/script.md. Every piece is read through the stream's Read in the save's order and in its pieces.
#include "base_manager.h"
#include "core_streams.h"
#include "script.h"
#include "world_effect.h"

#include "game_singletons.h"
#include "world.h"
#include "save_versions.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

int ReadInt(CStream* stream) {
    int value = 0;
    stream->ReadValue(value);
    return value;
}

std::size_t ReadCount(CStream* stream) { return static_cast<std::size_t>(ReadInt(stream)); }

// Areas are saved from 0x57 on.
int StreamVersion(const CStream* stream) { return static_cast<int>(stream->version); }

// -1 is none, else the registry's object.
template <class T>
T* FindById(int id) {
    if (id == -1) return nullptr;
    return g_serverWorld.timedObjectManager->FindObjectAs<T>(id);
}

// The world's first script (the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const CScript* SavedScript() { return g_serverWorld.MainScript(); }

ScriptDecl* FindBlock(int id) { return SavedScript()->FindBlock(id); }

void LoadVariables(ScriptVars* vars, CStream* stream) {
    vars->floats.Load(stream);
    vars->strings.Load(stream);
    vars->objects.Load(stream);
    vars->groups.Load(stream);
}

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void LoadBlockCommands(ScriptDecl* block, CStream* stream) {
    block->commands.resize(ReadCount(stream));
    for (SSCCmd& cmd : block->commands) cmd.Load(stream);
}

// Each list loader reads the count, appends that many default elements, then reads every element of the
// list from the front (a list that was not empty would have its old elements read again).
template <typename T, typename Read>
void LoadList(std::list<T>* list, CStream* stream, Read read) {
    const int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) list->emplace_back();
    for (T& element : *list) read(element);
}

// Particle sources: source object, id, flags, moving, steps, step (12 bytes), lifetime.
void LoadParticles(std::list<ScriptParticleSource>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptParticleSource& source) {
        source.object = ScriptEffect<CEffectParticle>(FindById<CEffectParticle>(ReadInt(stream)));
        stream->ReadValue(source.id);
        stream->ReadValue(source.deleteAfterMove);
        stream->ReadValue(source.moving);
        stream->ReadValue(source.steps);
        stream->ReadValue(source.step);
        stream->ReadValue(source.lifetime);
    });
}

// Lightning: object, id.
void LoadLightning(std::list<ScriptLightning>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptLightning& bolt) {
        bolt.object = ScriptEffect<CEffectLightning>(FindById<CEffectLightning>(ReadInt(stream)));
        stream->ReadValue(bolt.id);
    });
}

// Script instances: killed flag, name, parameter types, if-then pairs, variables.
void LoadInstances(std::list<ScriptDefinition>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptDefinition& instance) {
        stream->ReadValue(instance.index);
        instance.name = stream->ReadString();
        instance.params = stream->ReadString();
        instance.ifThen.resize(ReadCount(stream));
        for (ScriptIfThen& pair : instance.ifThen) pair.Load(stream);
        LoadVariables(&instance.vars, stream);
    });
}

// Runs: block id, index, ticks, sleep start, the index of its instance in the loading script's instance list
// (-1 or out of range: none), vars.
void LoadRuns(std::list<ScriptRun>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptRun& run) {
        run.block = FindBlock(ReadInt(stream));
        stream->ReadValue(run.index);
        stream->ReadValue(run.ticks);
        stream->ReadValue(run.sleepStart);
        const int instanceIndex = ReadInt(stream);
        run.killFlag = nullptr;
        int k = 0;
        for (ScriptDefinition& instance : g_streamedScript->instances) {
            if (k++ == instanceIndex) {
                run.killFlag = &instance.index;   // an instance's index is its killed flag
                break;
            }
        }
        run.vars.Load(stream);
    });
}

} // namespace

template <> void ScriptNamedValues<float>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        stream->ReadValue(values[i]);
        names[i] = stream->ReadString();
    }
}

template <> void ScriptNamedValues<std::string>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = stream->ReadString();
        names[i] = stream->ReadString();
    }
}

// An object variable: the world's object with the saved id (FindObject), -1 = none.
template <> void ScriptNamedValues<ScriptObjectRef>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i].Load(stream);
        names[i] = stream->ReadString();
    }
}

// A group variable: its size, the member ids (missing objects stay null), then sorted.
template <> void ScriptNamedValues<ScriptGroup>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i].Load(stream);
        names[i] = stream->ReadString();
    }
}

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save. Object and group arguments are saved as counts only: the
// slots are made and left null. The called block is found in the lists of the world's first script (-1, an
// unset block, finds a WorldScript block with id -1 if any).
void SSCCmd::Load(CStream* stream) {
    stream->ReadValue(id);

    floatArgs.resize(ReadCount(stream));
    for (float& value : floatArgs) stream->ReadValue(value);

    stringArgs.resize(ReadCount(stream));
    for (std::string& text : stringArgs) text = stream->ReadString();

    objectArgs.assign(ReadCount(stream), nullptr);
    groupArgs.assign(ReadCount(stream), nullptr);

    localBindings.resize(ReadCount(stream));
    for (ScriptBinding& binding : localBindings) stream->ReadValue(binding);
    globalBindings.resize(ReadCount(stream));
    for (ScriptBinding& binding : globalBindings) stream->ReadValue(binding);

    subCalls.resize(ReadCount(stream));
    for (ScriptSubCall& call : subCalls) call.Load(stream);

    calledScript = SavedScript()->FindBlock(ReadInt(stream));
}

void ScriptSubCall::Load(CStream* stream) {
    stream->ReadValue(type);
    stream->ReadValue(argIndex);
    cmd->Load(stream);
}

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The ids of the condition and command blocks, resolved in the saved script's block lists (FindBlock). The
// second read keeps the first id when it reads nothing; the first defaults to -1.
void ScriptIfThen::Load(CStream* stream) {
    int id = -1;
    stream->ReadValue(id);
    conditions = FindBlock(id);
    stream->ReadValue(id);
    commands = FindBlock(id);
}

// Negative ids are WorldScript blocks.
ScriptDecl* CScript::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks : blocks;
    for (const ScriptDecl& block : list)
        if (block.id == id) return const_cast<ScriptDecl*>(&block);
    return nullptr;
}

// ---- ScriptVars::Load ------------------------------------------------------------------------------

// The variable loaders' format for all four kinds; group members read with ScriptObjectRef::Load.
void ScriptVars::Load(CStream* stream) {
    LoadVariables(this, stream);
}

// ---- CScript::LoadState ---------------------------------------------------------------------------

// Reads what SaveState wrote, in the same order. Sets the script pointer the loaders use to the
// script while the runs are read. Areas are read only from stream version 0x57 on.
void CScript::LoadState(CStream* stream) {
    g_streamedScript = this;

    pointLights.resize(ReadCount(stream));
    for (ScriptPointLight& light : pointLights) light.Load(stream);

    LoadParticles(&particles, stream);
    LoadList(&fxSources, stream, [stream](ScriptFxSource& source) { source.Load(stream); });
    LoadLightning(&lightning, stream);
    stream->ReadValue(nextParticleId);
    stream->ReadValue(nextFxId);

    // Command blocks: created empty, then each block's id and commands.
    const int blockCount = ReadInt(stream);
    for (int i = 0; i < blockCount; ++i) NewCommandList();
    for (ScriptDecl& block : blocks) {
        stream->ReadValue(block.id);
        LoadBlockCommands(&block, stream);
    }
    // WorldScript blocks: id, count, each SSCCmd.
    LoadList(&worldBlocks, stream, [stream](ScriptDecl& block) {
        stream->ReadValue(block.id);
        LoadBlockCommands(&block, stream);
    });

    definitions.resize(ReadCount(stream));
    for (ScriptDefinition& definition : definitions) {
        stream->ReadValue(definition.index);
        definition.name = stream->ReadString();
        definition.params = stream->ReadString();
        definition.ifThen.resize(ReadCount(stream));
        for (ScriptIfThen& pair : definition.ifThen) pair.Load(stream);
        definition.vars.Load(stream);
    }

    LoadVariables(&globals, stream);
    LoadInstances(&instances, stream);
    LoadRuns(&runs, stream);
    stream->ReadValue(loaded);
    stream->ReadValue(fileCount);
    g_streamedScript = nullptr;

    if (StreamVersion(stream) < kVersionScriptAreas) return;
    areas.resize(ReadCount(stream));
    for (ScriptArea& area : areas) {
        stream->ReadValue(area.id);
        area.parts.resize(ReadCount(stream));
        for (ScriptAreaPart& part : area.parts) stream->ReadValue(part);
    }
}
