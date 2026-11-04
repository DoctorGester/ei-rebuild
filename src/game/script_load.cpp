// Loading the mission script from a savegame (Script::LoadState) and the loaders of its parts:
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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

int ReadInt(ByteStream* stream) {
    int value = 0;
    stream->ReadValue(value);
    return value;
}

// A count; a negative one reads as 0.
std::size_t ReadCount(ByteStream* stream) { return static_cast<std::size_t>(std::max(ReadInt(stream), 0)); }

// The world's first script (the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const Script* SavedScript() { return g_serverWorld.MainScript(); }

void LoadVariables(ScriptVars* vars, ByteStream* stream) {
    vars->floats.Load(stream);
    vars->strings.Load(stream);
    vars->objects.Load(stream);
    vars->groups.Load(stream);
}

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void LoadBlockCommands(ScriptDecl* block, ByteStream* stream) {
    block->commands.resize(ReadCount(stream));
    for (SSCCmd& cmd : block->commands) cmd.Load(stream);
}

// Each list loader reads the count, makes the list that many default elements, then reads each.
template <typename Container, typename Read>
void LoadList(Container* list, ByteStream* stream, Read read) {
    list->clear();
    list->resize(ReadCount(stream));
    for (auto& element : *list) read(element);
}

// The same for elements with a Load member.
template <typename Container>
void LoadEach(Container* list, ByteStream* stream) {
    LoadList(list, stream, [stream](auto& element) { element.Load(stream); });
}

// A save made with other scripts can name blocks the loaded lists lack: such pairs and runs are dropped.
void DropUnresolvedPairs(std::vector<ScriptIfThen>* pairs) {
    std::erase_if(*pairs, [](const ScriptIfThen& pair) { return !pair.conditions || !pair.commands; });
}

// Runs: block id, index, ticks, sleep start, the index of its instance in the loading script's instance list
// (-1 or out of range: none), vars.
void LoadRuns(std::list<ScriptRun>* list, ByteStream* stream) {
    LoadList(list, stream, [stream](ScriptRun& run) {
        run.block = SavedScript()->FindBlock(ReadInt(stream));
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

// Killed flag, name, parameter types, if-then pairs, variables.
void ScriptDefinition::Load(ByteStream* stream) {
    stream->ReadValue(index);
    name = stream->ReadString();
    params = stream->ReadString();
    ifThen.resize(ReadCount(stream));
    for (ScriptIfThen& pair : ifThen) pair.Load(stream);
    DropUnresolvedPairs(&ifThen);
    vars.Load(stream);
}

template <> void ScriptNamedValues<float>::Load(ByteStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        stream->ReadValue(values[i]);
        names[i] = stream->ReadString();
    }
}

template <> void ScriptNamedValues<std::string>::Load(ByteStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i] = stream->ReadString();
        names[i] = stream->ReadString();
    }
}

// An object variable: the world's object with the saved id (FindObject), -1 = none.
template <> void ScriptNamedValues<ScriptObjectRef>::Load(ByteStream* stream) {
    const std::size_t count = ReadCount(stream);
    values.resize(count);
    names.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        values[i].Load(stream);
        names[i] = stream->ReadString();
    }
}

// A group variable: its size, the member ids (missing objects stay null), then sorted.
template <> void ScriptNamedValues<ScriptGroup>::Load(ByteStream* stream) {
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
// unset block, finds a WorldScript block with id -1 if any). A For / ForIf whose body did not resolve (a save made
// with other scripts) is cleared into a command that does nothing: its block keeps its length, so the saved runs'
// indices still point at their commands, and Execute always has a body to run.
void SSCCmd::Load(ByteStream* stream) {
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
    if (!calledScript && (id == kCmdFor || id == kCmdForIf)) Clear();
}

void ScriptSubCall::Load(ByteStream* stream) {
    stream->ReadValue(type);
    stream->ReadValue(argIndex);
    cmd->Load(stream);
}

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The ids of the condition and command blocks, resolved in the saved script's block lists (FindBlock).
void ScriptIfThen::Load(ByteStream* stream) {
    conditions = SavedScript()->FindBlock(ReadInt(stream));
    commands = SavedScript()->FindBlock(ReadInt(stream));
}

// Negative ids are WorldScript blocks.
ScriptDecl* Script::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks : blocks;
    for (const ScriptDecl& block : list)
        if (block.id == id) return const_cast<ScriptDecl*>(&block);
    return nullptr;
}

// ---- ScriptVars::Load ------------------------------------------------------------------------------

// The variable loaders' format for all four kinds; group members read with ScriptObjectRef::Load.
void ScriptVars::Load(ByteStream* stream) {
    LoadVariables(this, stream);
}

// ---- Script::LoadState ---------------------------------------------------------------------------

// Reads what SaveState wrote, in the same order. Sets the script pointer the loaders use to the
// script while the runs are read. Areas are read only from stream version 0x57 on.
void Script::LoadState(ByteStream* stream) {
    g_streamedScript = this;

    pointLights.resize(ReadCount(stream));
    for (ScriptPointLight& light : pointLights) light.Load(stream);

    LoadEach(&particles, stream);
    LoadEach(&fxSources, stream);
    LoadEach(&lightning, stream);
    stream->ReadValue(nextParticleId);
    stream->ReadValue(nextFxId);

    // Command blocks, then WorldScript blocks: each block's id and commands. The command blocks are made with
    // their ids first (NewCommandList), so that a command's call of a later block resolves as it loads.
    auto loadBlock = [stream](ScriptDecl& block) {
        stream->ReadValue(block.id);
        LoadBlockCommands(&block, stream);
    };
    blocks.clear();
    for (std::size_t count = ReadCount(stream); count > 0; --count) NewCommandList();
    for (ScriptDecl& block : blocks) loadBlock(block);
    LoadList(&worldBlocks, stream, loadBlock);

    definitions.resize(ReadCount(stream));
    for (ScriptDefinition& definition : definitions) definition.Load(stream);

    LoadVariables(&globals, stream);
    LoadEach(&instances, stream);
    LoadRuns(&runs, stream);
    std::erase_if(runs, [](const ScriptRun& run) { return !run.block; });
    stream->ReadValue(loaded);
    stream->ReadValue(fileCount);
    g_streamedScript = nullptr;

    if (stream->Version() < kVersionScriptAreas) return;
    areas.resize(ReadCount(stream));
    for (ScriptArea& area : areas) {
        stream->ReadValue(area.id);
        area.parts.resize(ReadCount(stream));
        for (ScriptAreaPart& part : area.parts) stream->ReadValue(part);
    }
}
