// Loading the mission script from a savegame (Script::LoadState) and the loaders of its parts:
// the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs (an old-format save's program and state, for the
// old-save shim; ours writes the blocks empty: script_save.cpp). See docs/notes/script.md. Every piece is read
// through the stream's Read in the save's order and in its pieces.
#include "engine/core/base_manager.h"
#include "engine/core/core_streams.h"
#include "game/script/script.h"
#include "game/script/script_lua_scheduler.h"
#include "game/console/console.h"
#include "game/effects/world_effect.h"

#include "game/saves/save_versions.h"

#include "lib/rounding.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
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
    rules = stream->ReadCount();
    ifThen.resize(static_cast<std::size_t>(rules));
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

void ScriptSavedRun::Load(ByteStream* stream, const Script* script) {
    block = script->FindBlock(stream->ReadAs<std::int32_t>());
    stream->ReadValue(index);
    stream->ReadValue(ticks);
    stream->ReadValue(sleepStart);
    instance = stream->ReadAs<std::int32_t>();
    if (instance < 0 || instance >= std::ssize(script->savedInstances)) instance = -1;
    vars.Load(stream);
}

// ---- ScriptCall::Load -----------------------------------------------------------------------------------

// An old-format save's call: id, arguments, bindings, nested calls, body. Object and group arguments are saved as
// counts only: the slots are made and left null. The called block is found in the lists of the world's first script
// (-1, an unset block, finds a WorldScript block with id -1 if any). A For / ForIf whose body did not resolve (a save
// made with other scripts) is cleared into a command that does nothing: its block keeps its length, so the saved
// runs' indices still point at their commands, and the old-save shim always has a body to convert.
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
// from stream version 0x57 on. The globals', instances' and runs' fields hold the saved state either way: an
// old-format save's for the shim, ours the scheduler's as far as those fields go (script_save.cpp, WriteLuaState),
// which its Lua state after the areas holds whole. The program's blocks are an old-format save's (ours writes none).
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
    LoadList(&savedInstances, stream, [this, stream](ScriptDefinition& instance) { instance.Load(stream, this); });
    LoadList(&savedRuns, stream, [this, stream](ScriptSavedRun& run) { run.Load(stream, this); });
    std::erase_if(savedRuns, [](const ScriptSavedRun& run) { return !run.block; });
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
    // The script goes on with its saved state, whatever the zone's files say now (a save keeps running the code it
    // was saved with): our save's Lua state, or an old-format save's program and state through the old-save shim.
    // One that does not go on leaves the script empty, said on the console.
    std::string error;
    if (onLua) {
        std::string data(static_cast<std::size_t>(stream->ReadCount()), '\0');
        stream->Read(data.data(), static_cast<std::uint32_t>(data.size()));
        if (!data.empty() && !Runner().Restore(data, &error)) {
            console::EndLine(console::g_consoleOut << "Script error: the save's Lua state did not load: " << error);
            DropRunner();
        }
    } else {
        std::string text;
        if (!Runner().Resume(*this, &text, &error)) {
            console::EndLine(console::g_consoleOut << "Script error: the save's script did not go on in Lua: " << error);
            DropRunner();
        }
    }
    // Only the shim reads the old format's program and state; the declarations stay: the later texts' names.
    savedInstances.clear();
    savedRuns.clear();
    blocks.clear();
    worldBlocks.clear();
    for (ScriptDefinition& definition : definitions) definition.ifThen.clear();
    globals.ResetValues();
}
