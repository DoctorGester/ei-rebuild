#pragma once

// game.exe's CScript and the parts of it the script recorders read, at game.exe's own offsets (the script's MFC 4.2
// arrays and lists, CStrings for text) through tests/verify/exe_objects.h's container views: the record side's
// serialisers (script_load_vectors.h) read these with the same std calls (size(), [], range-for) as the replay's
// read our classes (src/game/script.h), whose layout is our own.

#include "../verify/exe_objects.h"

#include <cstddef>
#include <cstdint>

namespace ei::vectors::script {

namespace exe = ei::verify::exe;

// A token: its text and the line it starts on.
struct OriginalScriptToken {
    exe::CString text;
    std::int32_t line;
};
static_assert(sizeof(OriginalScriptToken) == 8);
using OriginalScriptTokenList = exe::List<OriginalScriptToken>;
using OriginalScriptPos = OriginalScriptTokenList::Node*;

// A CString's length (its header's data length, before the text).
inline std::uint32_t Length(const exe::CString& s) {
    return static_cast<std::uint32_t>(reinterpret_cast<const std::int32_t*>(s.text)[-2]);
}

// A #define: its name and value.
struct OriginalScriptDefine {
    exe::CString name;
    exe::CString value;
};
using OriginalScriptDefineList = exe::List<OriginalScriptDefine>;
static_assert(sizeof(OriginalScriptDefineList::Node) == 0x10);

// A variable set: values and names of the four kinds (objects and groups only counted by the recorders).
template <typename Value>
struct OriginalNamedValues {
    exe::Vector<Value> values;
    exe::Vector<exe::CString> names;
};
struct OriginalScriptVars {
    OriginalNamedValues<float> floats;
    OriginalNamedValues<exe::CString> strings;
    OriginalNamedValues<exe::ScriptObjectRef> objects;
    OriginalNamedValues<exe::Vector<exe::ScriptObjectRef>> groups;
};
static_assert(sizeof(OriginalScriptVars) == 0xA0 && offsetof(OriginalScriptVars, groups) == 0x78);

// A variable binding: argument `argIndex` of a command is variable `varIndex` of the type.
struct OriginalScriptBinding {
    std::int32_t type;
    std::int32_t argIndex;
    std::int32_t varIndex;
};
static_assert(sizeof(OriginalScriptBinding) == 12);

// A command, its nested calls and the block lists that hold commands.
struct OriginalScriptDecl;
struct OriginalSSCCmd;
struct OriginalScriptSubCall {
    std::int32_t type;
    std::int32_t argIndex;
    const OriginalSSCCmd* cmd;
};
struct OriginalSSCCmd {
    std::int32_t id;
    exe::Vector<float> floatArgs;
    exe::Vector<exe::CString> stringArgs;
    exe::Vector<const void*> objectArgs;
    exe::Vector<const void*> groupArgs;
    exe::Vector<OriginalScriptBinding> localBindings;
    exe::Vector<OriginalScriptBinding> globalBindings;
    exe::Vector<OriginalScriptSubCall> subCalls;
    const OriginalScriptDecl* calledScript;
};
static_assert(sizeof(OriginalSSCCmd) == 0x94 && offsetof(OriginalSSCCmd, subCalls) == 0x7C);
struct OriginalScriptDecl {
    exe::Vector<OriginalSSCCmd> commands;
    std::int32_t id;
};
static_assert(sizeof(OriginalScriptDecl) == 0x18);
using OriginalScriptDecls = exe::List<OriginalScriptDecl>;

// A Script / DeclareScript definition and its if-then pairs (two command blocks).
struct OriginalScriptIfThen {
    const OriginalScriptDecl* conditions;
    const OriginalScriptDecl* commands;
};
struct OriginalScriptDefinition {
    std::int32_t index;
    exe::CString name;
    exe::CString params;
    exe::Vector<OriginalScriptIfThen> ifThen;
    OriginalScriptVars vars;
};
static_assert(offsetof(OriginalScriptDefinition, ifThen) == 0x0C && sizeof(OriginalScriptDefinition) == 0xC0);

// A waiting run (the recorders read only its block).
struct OriginalScriptRun {
    const OriginalScriptDecl* block;
    std::uint8_t rest[0xB0];
};
static_assert(sizeof(OriginalScriptRun) == 0xB4);

// game.exe's CScript (0x1F8 bytes): the members the loader's recorders read; the effects and temporaries are padding.
struct OriginalScript {
    std::uint8_t effects[0x68];                     // +0x00 point lights, particle / FX / lightning sources
    std::int32_t nextParticleId;                    // +0x68
    std::int32_t nextFxId;                          // +0x6C
    OriginalScriptDecls blocks;                     // +0x70
    exe::Vector<OriginalScriptDefinition> definitions;   // +0x8C
    OriginalScriptVars globals;                     // +0xA0
    std::uint8_t instances[0x1C];                   // +0x140
    exe::List<OriginalScriptRun> runs;              // +0x15C
    OriginalScriptDecls worldBlocks;                // +0x178
    std::uint8_t areas[0x14];                       // +0x194
    std::int32_t loaded;                            // +0x1A8
    std::int32_t fileCount;                         // +0x1AC
    std::uint8_t temporaries[0x3C];                 // +0x1B0 temporary groups and references, For depth
    exe::CString errorText;                         // +0x1EC
    std::int32_t errorCode;                         // +0x1F0
    std::int32_t errorLine;                         // +0x1F4
};
static_assert(offsetof(OriginalScript, blocks) == 0x70 && offsetof(OriginalScript, definitions) == 0x8C &&
              offsetof(OriginalScript, globals) == 0xA0 && offsetof(OriginalScript, runs) == 0x15C &&
              offsetof(OriginalScript, worldBlocks) == 0x178 && offsetof(OriginalScript, loaded) == 0x1A8 &&
              offsetof(OriginalScript, errorText) == 0x1EC && sizeof(OriginalScript) == 0x1F8);

} // namespace ei::vectors::script
