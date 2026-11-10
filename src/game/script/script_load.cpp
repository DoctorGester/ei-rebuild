// Loading the mission script from a savegame (Script::LoadState) and the loaders of its parts:
// the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs. The mirror image of script_save.cpp; see
// docs/notes/script.md. Every piece is read through the stream's Read in the save's order and in its pieces.
#include "game/core/base_manager.h"
#include "game/core/core_streams.h"
#include "game/script/script.h"
#include "game/script/script_lua_scheduler.h"
#include "game/script/script_lua_shadow.h"
#include "game/console/console.h"
#include "game/effects/world_effect.h"

#include "game/saves/save_versions.h"

#include "lib/rounding.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// ScriptNamedValues::Load reads a value by the overload for its type.
void ReadOne(ByteStream* stream, float& value) { stream->ReadValue(value); }
void ReadOne(ByteStream* stream, std::string& value) { value = stream->ReadString(); }
void ReadOne(ByteStream* stream, ScriptObjectRef& value) { value.Load(stream); }   // the world's object of the id
void ReadOne(ByteStream* stream, ScriptGroup& value) { value.Load(stream); }       // missing objects stay null

// The commands of a block of `script` (its id is read by the caller): count, then each ScriptCall.
void LoadBlockCommands(ScriptBlock* block, ByteStream* stream, const Script* script) {
    block->commands.resize(stream->ReadCount());
    for (ScriptCall& cmd : block->commands) cmd.Load(stream, script);
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
void ScriptDefinition::Load(ByteStream* stream, const Script* script) {
    killed = stream->ReadAs<std::int32_t>() != 0;
    name = stream->ReadString();
    params = stream->ReadString();
    ifThen.resize(stream->ReadCount());
    for (ScriptIfThen& pair : ifThen) pair.Load(stream, script);
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

void ScriptRun::Load(ByteStream* stream, Script* script) {
    position.block = script->FindBlock(stream->ReadAs<std::int32_t>());
    stream->ReadValue(position.index);
    stream->ReadValue(position.ticks);
    stream->ReadValue(position.sleepStart);
    const int instanceIndex = stream->ReadAs<std::int32_t>();
    killFlag = nullptr;
    int k = 0;
    for (ScriptDefinition& instance : script->instances) {
        if (k++ == instanceIndex) {
            killFlag = &instance.killed;
            break;
        }
    }
    vars.Load(stream);
}

// ---- ScriptCall::Load -----------------------------------------------------------------------------------

// The mirror of ScriptCall::Save. Object and group arguments are saved as counts only: the
// slots are made and left null. The called block is found in the lists of the world's first script (-1, an
// unset block, finds a WorldScript block with id -1 if any). A For / ForIf whose body did not resolve (a save made
// with other scripts) is cleared into a command that does nothing: its block keeps its length, so the saved runs'
// indices still point at their commands, and Execute always has a body to run.
void ScriptCall::Load(ByteStream* stream, const Script* script) {
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
    for (ScriptSubCall& call : subCalls) call.Load(stream, script);

    body = script->FindBlock(stream->ReadAs<std::int32_t>());
    if (!body && (id == ScriptFunction::For || id == ScriptFunction::ForIf)) Clear();

    // Assign / CallScript: the target out of its leading slots; the bindings and nested calls count past them.
    const bool assign = id == ScriptFunction::Assign;
    const bool callScript = id == ScriptFunction::CallScript;
    if (!assign && !callScript) return;
    const int leadFloats = assign ? 2 : 1;
    const int leadTexts = callScript ? 1 : 0;
    target = lib::Truncate(floatArgs[0]);
    if (assign) targetType = static_cast<ScriptType>(lib::Truncate(floatArgs[1]) + 1);
    else targetName = stringArgs[0];
    floatArgs.erase(floatArgs.begin(), floatArgs.begin() + leadFloats);
    stringArgs.erase(stringArgs.begin(), stringArgs.begin() + leadTexts);
    auto argIndexPastLead = [&](ScriptType type, int* argIndex) {
        if (type == ScriptType::Float) *argIndex -= leadFloats;
        else if (type == ScriptType::String) *argIndex -= leadTexts;
    };
    for (ScriptBinding& binding : localBindings) argIndexPastLead(binding.type, &binding.argIndex);
    for (ScriptBinding& binding : globalBindings) argIndexPastLead(binding.type, &binding.argIndex);
    for (ScriptSubCall& call : subCalls) argIndexPastLead(call.type, &call.argIndex);
}

void ScriptSubCall::Load(ByteStream* stream, const Script* script) {
    stream->ReadValue(type);
    stream->ReadValue(argIndex);
    cmd->Load(stream, script);
}

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The ids of the condition and command blocks, resolved in `script`'s block lists (FindBlock).
void ScriptIfThen::Load(ByteStream* stream, const Script* script) {
    conditions = script->FindBlock(stream->ReadAs<std::int32_t>());
    commands = script->FindBlock(stream->ReadAs<std::int32_t>());
}

// Negative ids are WorldScript blocks.
ScriptBlock* Script::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks : blocks;
    for (const ScriptBlock& block : list)
        if (block.id == id) return const_cast<ScriptBlock*>(&block);
    return nullptr;
}

// ---- Script::LoadState ---------------------------------------------------------------------------

// Reads what SaveState wrote, in the same order; every block id resolves in this script. Areas are read only
// from stream version 0x57 on.
void Script::LoadState(ByteStream* stream) {
    DropRunner();
    pointLights.resize(stream->ReadCount());
    for (ScriptPointLight& light : pointLights) light.Load(stream);

    LoadEach(&particles, stream);
    LoadEach(&fxSources, stream);
    LoadEach(&lightning, stream);
    stream->ReadValue(nextParticleId);
    stream->ReadValue(nextFxId);

    // Command blocks, then WorldScript blocks: each block's id and commands. The command blocks are made with
    // their ids first (NewCommandList), so that a command's call of a later block resolves as it loads.
    auto loadBlock = [this, stream](ScriptBlock& block) {
        stream->ReadValue(block.id);
        LoadBlockCommands(&block, stream, this);
    };
    blocks.clear();
    for (int count = stream->ReadCount(); count > 0; --count) NewCommandList();
    for (ScriptBlock& block : blocks) loadBlock(block);
    LoadList(&worldBlocks, stream, loadBlock);

    definitions.resize(stream->ReadCount());
    for (ScriptDefinition& definition : definitions) definition.Load(stream, this);

    globals.Load(stream);
    LoadList(&instances, stream, [this, stream](ScriptDefinition& instance) { instance.Load(stream, this); });
    LoadList(&runs, stream, [this, stream](ScriptRun& run) { run.Load(stream, this); });
    std::erase_if(runs, [](const ScriptRun& run) { return !run.position.block; });
    stream->ReadValue(loaded);
    const bool onLua = loaded >= kScriptLoadedOnLua;
    if (onLua) loaded -= kScriptLoadedOnLua;
    stream->ReadValue(fileCount);

    if (stream->version >= kVersionScriptAreas) {
        areas.resize(stream->ReadCount());
        for (ScriptArea& area : areas) {
            stream->ReadValue(area.id);
            area.parts.resize(stream->ReadCount());
            for (ScriptAreaPart& part : area.parts) stream->ReadValue(part);
        }
    }
    // A script saved on Lua goes on there with its saved state, whatever the zone's files say now (a save keeps
    // running the code it was saved with). The old format's globals, instances and runs hold the Lua's as far as
    // those fields go (script_save.cpp, WriteLuaState): dropped, as the old executor's are when a script goes on Lua.
    if (onLua) {
        DropInstancesAndRuns();
        globals.ResetValues();
        std::string data(static_cast<std::size_t>(stream->ReadCount()), '\0');
        stream->Read(data.data(), static_cast<std::uint32_t>(data.size()));
        runner = std::make_unique<ScriptLuaScheduler>(this);
        std::string error;
        if (!data.empty() && !runner->Restore(data, &error)) {
            console::EndLine(console::g_consoleOut << "Script error: the save's Lua state did not load: " << error);
            runner = std::make_unique<ScriptLuaScheduler>(this);
        }
        if (shadow) shadow->RunsOnLua(this);
        return;
    }
    if (shadow) shadow->StateLoaded(this);
}
