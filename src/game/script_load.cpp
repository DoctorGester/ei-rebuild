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

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// The id registry.
CBaseManager* Registry() { return TheServerWorld()->manager48_; }

int ReadInt(CStream* stream) {
    int value = 0;
    stream->Read(&value, 4);
    return value;
}

std::size_t ReadCount(CStream* stream) { return static_cast<std::size_t>(ReadInt(stream)); }

// Areas are saved from 0x57 on.
int StreamVersion(const CStream* stream) { return static_cast<int>(stream->version_); }

// -1 is none, else the registry's object.
template <class T>
T* FindById(int id) {
    if (id == -1) return nullptr;
    return Registry()->FindObjectAs<T>(id);
}

// The world's first script (the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const CScript* SavedScript() { return TheServerWorld()->MainScript(); }

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

} // namespace

template <> void ScriptNamedValues<float>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        stream->Read(&values[i], 4);
        ScriptStringLoad(&names[i], stream);
    }
}

template <> void ScriptNamedValues<std::string>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        ScriptStringLoad(&values[i], stream);
        ScriptStringLoad(&names[i], stream);
    }
}

// An object variable: the world's object with the saved id (FindObject), -1 = none.
template <> void ScriptNamedValues<ScriptObjectRef>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i].Load(stream);
        ScriptStringLoad(&names[i], stream);
    }
}

// A group variable: its size, the member ids (missing objects stay null), then sorted.
template <> void ScriptNamedValues<ScriptGroup>::Load(CStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i].Load(stream);
        ScriptStringLoad(&names[i], stream);
    }
}

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save. Object and group arguments are saved as counts only: the
// slots are made and left null. The called block is found in the lists of the world's first script (-1, an
// unset block, finds a WorldScript block with id -1 if any).
void SSCCmd::Load(CStream* stream) {
    stream->Read(&id, 4);

    floatArgs.resize(ReadCount(stream));
    for (float& value : floatArgs) stream->Read(&value, 4);

    stringArgs.resize(ReadCount(stream));
    for (std::string& text : stringArgs) ScriptStringLoad(&text, stream);

    objectArgs.assign(ReadCount(stream), nullptr);
    groupArgs.assign(ReadCount(stream), nullptr);

    localBindings.resize(ReadCount(stream));
    for (ScriptBinding& binding : localBindings) stream->Read(&binding, 12);
    globalBindings.resize(ReadCount(stream));
    for (ScriptBinding& binding : globalBindings) stream->Read(&binding, 12);

    subCalls.resize(ReadCount(stream));
    for (ScriptSubCall& call : subCalls) call.Load(stream);

    calledScript = SavedScript()->FindBlock(ReadInt(stream));
}

void ScriptSubCall::Load(CStream* stream) {
    stream->Read(&type, 4);
    stream->Read(&argIndex, 4);
    cmd->Load(stream);
}

void ScriptStringLoad(std::string* out, CStream* stream) {
    std::string temporary;
    *out = stream->ReadString(&temporary)->c_str();
}

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The ids of the condition and command blocks, resolved in the saved script's block lists (FindBlock). The
// second read keeps the first id when it reads nothing; the first defaults to -1.
void ScriptIfThen::Load(CStream* stream) {
    int id = -1;
    stream->Read(&id, 4);
    conditions = FindBlock(id);
    stream->Read(&id, 4);
    commands = FindBlock(id);
}

// Negative ids are WorldScript blocks.
ScriptDecl* CScript::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks_ : blocks_;
    for (const ScriptDecl& block : list)
        if (block.id == id) return const_cast<ScriptDecl*>(&block);
    return nullptr;
}

// ---- ScriptVars::Load ------------------------------------------------------------------------------

// The variable loaders' format for all four kinds; group members read with ScriptObjectRef::Load.
void ScriptVars::Load(CStream* stream) {
    LoadVariables(this, stream);
}

namespace {

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
        stream->Read(&source.id, 4);
        stream->Read(&source.deleteAfterMove, 4);
        stream->Read(&source.moving, 4);
        stream->Read(&source.steps, 4);
        stream->Read(&source.step, 0xC);
        stream->Read(&source.lifetime, 4);
    });
}

// Lightning: object, id.
void LoadLightning(std::list<ScriptLightning>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptLightning& bolt) {
        bolt.object = ScriptEffect<CEffectLightning>(FindById<CEffectLightning>(ReadInt(stream)));
        stream->Read(&bolt.id, 4);
    });
}

// Script instances: killed flag, name, parameter types, if-then pairs, variables.
void LoadInstances(std::list<ScriptDefinition>* list, CStream* stream) {
    LoadList(list, stream, [stream](ScriptDefinition& instance) {
        stream->Read(&instance.index, 4);
        ScriptStringLoad(&instance.name, stream);
        ScriptStringLoad(&instance.params, stream);
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
        stream->Read(&run.index, 4);
        stream->Read(&run.ticks, 4);
        stream->Read(&run.sleepStart, 4);
        const int instanceIndex = ReadInt(stream);
        run.killFlag = nullptr;
        int k = 0;
        for (ScriptDefinition& instance : g_streamedScript->instances_) {
            if (k++ == instanceIndex) {
                run.killFlag = &instance.index;   // an instance's index is its killed flag
                break;
            }
        }
        run.vars.Load(stream);
    });
}

} // namespace

// ---- CScript::LoadState ---------------------------------------------------------------------------

// Reads what SaveState wrote, in the same order. Sets the script pointer the loaders use to the
// script while the runs are read. Areas are read only from stream version 0x57 on.
void CScript::LoadState(CStream* stream) {
    g_streamedScript = this;

    pointLights_.resize(ReadCount(stream));
    for (ScriptPointLight& light : pointLights_) light.Load(stream);

    LoadParticles(&particles_, stream);
    LoadList(&fxSources_, stream, [stream](ScriptFxSource& source) { source.Load(stream); });
    LoadLightning(&lightning_, stream);
    stream->Read(&nextParticleId_, 4);
    stream->Read(&nextFxId_, 4);

    // Command blocks: created empty, then each block's id and commands.
    const int blocks = ReadInt(stream);
    for (int i = 0; i < blocks; ++i) NewCommandList();
    for (ScriptDecl& block : blocks_) {
        stream->Read(&block.id, 4);
        LoadBlockCommands(&block, stream);
    }
    // WorldScript blocks: id, count, each SSCCmd.
    LoadList(&worldBlocks_, stream, [stream](ScriptDecl& block) {
        stream->Read(&block.id, 4);
        LoadBlockCommands(&block, stream);
    });

    definitions_.resize(ReadCount(stream));
    for (ScriptDefinition& definition : definitions_) {
        stream->Read(&definition.index, 4);
        ScriptStringLoad(&definition.name, stream);
        ScriptStringLoad(&definition.params, stream);
        definition.ifThen.resize(ReadCount(stream));
        for (ScriptIfThen& pair : definition.ifThen) pair.Load(stream);
        definition.vars.Load(stream);
    }

    LoadVariables(Globals(), stream);
    LoadInstances(&instances_, stream);
    LoadRuns(&runs_, stream);
    stream->Read(&loaded_, 4);
    stream->Read(&fileCount_, 4);
    g_streamedScript = nullptr;

    if (StreamVersion(stream) < 0x57) return;
    areas_.resize(ReadCount(stream));
    for (ScriptArea& area : areas_) {
        stream->Read(&area.id, 4);
        area.parts.resize(ReadCount(stream));
        for (ScriptAreaPart& part : area.parts) stream->Read(&part, sizeof(ScriptAreaPart));
    }
}
