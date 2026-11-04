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

// The world's first script (the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const Script* SavedScript() { return g_serverWorld.MainScript(); }

// ScriptNamedValues::Load reads a value by the overload for its type.
void ReadOne(ByteStream* stream, float& value) { stream->ReadValue(value); }
void ReadOne(ByteStream* stream, std::string& value) { value = stream->ReadString(); }
void ReadOne(ByteStream* stream, ScriptObjectRef& value) { value.Load(stream); }   // the world's object of the id
void ReadOne(ByteStream* stream, ScriptGroup& value) { value.Load(stream); }       // missing objects stay null

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void LoadBlockCommands(ScriptDecl* block, ByteStream* stream) {
    block->commands.resize(stream->ReadCount());
    for (SSCCmd& cmd : block->commands) cmd.Load(stream);
}

// Each list loader reads the count, makes the list that many default elements, then reads each.
template <typename Container, typename Read>
void LoadList(Container* list, ByteStream* stream, Read read) {
    list->clear();
    list->resize(stream->ReadCount());
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

} // namespace

// Killed flag, name, parameter types, if-then pairs, variables.
void ScriptDefinition::Load(ByteStream* stream) {
    stream->ReadValue(index);
    name = stream->ReadString();
    params = stream->ReadString();
    ifThen.resize(stream->ReadCount());
    for (ScriptIfThen& pair : ifThen) pair.Load(stream);
    DropUnresolvedPairs(&ifThen);
    vars.Load(stream);
}

template <typename T>
void ScriptNamedValues<T>::Load(ByteStream* stream) {
    const int count = stream->ReadCount();
    values.resize(count);
    names.resize(count);
    for (int i = 0; i < count; ++i) {
        ReadOne(stream, values[i]);
        names[i] = stream->ReadString();
    }
}
template void ScriptNamedValues<float>::Load(ByteStream*);
template void ScriptNamedValues<std::string>::Load(ByteStream*);
template void ScriptNamedValues<ScriptObjectRef>::Load(ByteStream*);
template void ScriptNamedValues<ScriptGroup>::Load(ByteStream*);

void ScriptVars::Load(ByteStream* stream) {
    floats.Load(stream);
    strings.Load(stream);
    objects.Load(stream);
    groups.Load(stream);
}

void ScriptRun::Load(ByteStream* stream) {
    block = SavedScript()->FindBlock(stream->ReadAs<std::int32_t>());
    stream->ReadValue(index);
    stream->ReadValue(ticks);
    stream->ReadValue(sleepStart);
    const int instanceIndex = stream->ReadAs<std::int32_t>();
    killFlag = nullptr;
    int k = 0;
    for (ScriptDefinition& instance : g_streamedScript->instances) {
        if (k++ == instanceIndex) {
            killFlag = &instance.index;   // an instance's index is its killed flag
            break;
        }
    }
    vars.Load(stream);
}

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save. Object and group arguments are saved as counts only: the
// slots are made and left null. The called block is found in the lists of the world's first script (-1, an
// unset block, finds a WorldScript block with id -1 if any). A For / ForIf whose body did not resolve (a save made
// with other scripts) is cleared into a command that does nothing: its block keeps its length, so the saved runs'
// indices still point at their commands, and Execute always has a body to run.
void SSCCmd::Load(ByteStream* stream) {
    stream->ReadValue(id);

    floatArgs.resize(stream->ReadCount());
    for (float& value : floatArgs) stream->ReadValue(value);

    stringArgs.resize(stream->ReadCount());
    for (std::string& text : stringArgs) text = stream->ReadString();

    objectArgs.assign(stream->ReadCount(), nullptr);
    groupArgs.assign(stream->ReadCount(), nullptr);

    localBindings.resize(stream->ReadCount());
    for (ScriptBinding& binding : localBindings) stream->ReadValue(binding);
    globalBindings.resize(stream->ReadCount());
    for (ScriptBinding& binding : globalBindings) stream->ReadValue(binding);

    subCalls.resize(stream->ReadCount());
    for (ScriptSubCall& call : subCalls) call.Load(stream);

    calledScript = SavedScript()->FindBlock(stream->ReadAs<std::int32_t>());
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
    conditions = SavedScript()->FindBlock(stream->ReadAs<std::int32_t>());
    commands = SavedScript()->FindBlock(stream->ReadAs<std::int32_t>());
}

// Negative ids are WorldScript blocks.
ScriptDecl* Script::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks : blocks;
    for (const ScriptDecl& block : list)
        if (block.id == id) return const_cast<ScriptDecl*>(&block);
    return nullptr;
}

// ---- Script::LoadState ---------------------------------------------------------------------------

// Reads what SaveState wrote, in the same order. Sets the script pointer the loaders use to the
// script while the runs are read. Areas are read only from stream version 0x57 on.
void Script::LoadState(ByteStream* stream) {
    g_streamedScript = this;

    pointLights.resize(stream->ReadCount());
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
    for (int count = stream->ReadCount(); count > 0; --count) NewCommandList();
    for (ScriptDecl& block : blocks) loadBlock(block);
    LoadList(&worldBlocks, stream, loadBlock);

    definitions.resize(stream->ReadCount());
    for (ScriptDefinition& definition : definitions) definition.Load(stream);

    globals.Load(stream);
    LoadEach(&instances, stream);
    LoadEach(&runs, stream);
    std::erase_if(runs, [](const ScriptRun& run) { return !run.block; });
    stream->ReadValue(loaded);
    stream->ReadValue(fileCount);
    g_streamedScript = nullptr;

    if (stream->version < kVersionScriptAreas) return;
    areas.resize(stream->ReadCount());
    for (ScriptArea& area : areas) {
        stream->ReadValue(area.id);
        area.parts.resize(stream->ReadCount());
        for (ScriptAreaPart& part : area.parts) stream->ReadValue(part);
    }
}
