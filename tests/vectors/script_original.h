#pragma once

// What the script recorders read of game.exe's own CScript where its layout and ours differ: game.exe keeps the error
// as a CString followed by the code and the line, at the offset where ours has errorText_ (a std::string, larger);
// the members before it are laid out alike. game.exe's tokens, defines, definitions and string arrays (the variables'
// names and strings, the commands' string arguments) hold CStrings where ours hold std::strings (larger): the
// recorders read them through the views below, never script.h's with its sizes.

#include "../../src/game/script.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct OriginalScriptError {
    mfc::CString text;
    std::int32_t code;
    std::int32_t line;
};

inline const OriginalScriptError& OriginalError(const CScript* script) {
    return *reinterpret_cast<const OriginalScriptError*>(reinterpret_cast<const char*>(script) +
                                                         offsetof(CScript, errorText_));
}

// A token: its text and the line it starts on.
struct OriginalScriptToken {
    mfc::CString text;
    std::int32_t line;
};
static_assert(sizeof(OriginalScriptToken) == 8);
using OriginalScriptTokenList = mfc::CList<OriginalScriptToken>;
using OriginalScriptPos = OriginalScriptTokenList::Node*;

// A #define: its name and value.
struct OriginalScriptDefine {
    mfc::CString name;
    mfc::CString value;
};
using OriginalScriptDefineList = mfc::CList<OriginalScriptDefine>;
static_assert(sizeof(OriginalScriptDefineList::Node) == 0x10);

// A variable set: game.exe's string arrays hold CStrings where ours hold std::strings (the array headers are
// alike, so ScriptVars' layout is too).
template <typename Value>
struct OriginalNamedValues {
    mfc::CArray<Value> values;
    mfc::CArray<mfc::CString> names;
};
struct OriginalScriptVars {
    OriginalNamedValues<float> floats;
    OriginalNamedValues<mfc::CString> strings;
    OriginalNamedValues<ScriptObjectRef> objects;
    OriginalNamedValues<ScriptGroup> groups;
};
static_assert(sizeof(OriginalScriptVars) == sizeof(ScriptVars) &&
              offsetof(OriginalScriptVars, strings) == offsetof(ScriptVars, strings) &&
              offsetof(OriginalScriptVars, groups) == offsetof(ScriptVars, groups));

inline const OriginalScriptVars& OriginalGlobals(CScript* script) {
    return *reinterpret_cast<const OriginalScriptVars*>(script->Globals());
}

// A command, its nested calls and the block lists that hold commands: the string arguments are CStrings.
struct OriginalScriptDecl;
struct OriginalSSCCmd;
struct OriginalScriptSubCall {
    std::int32_t type;
    std::int32_t argIndex;
    OriginalSSCCmd* cmd;
};
struct OriginalSSCCmd {
    std::int32_t id;
    mfc::CArray<float> floatArgs;
    mfc::CArray<mfc::CString> stringArgs;
    mfc::CArray<ScriptObjectRef*> objectArgs;
    mfc::CArray<ScriptGroup*> groupArgs;
    mfc::CArray<ScriptBinding> localBindings;
    mfc::CArray<ScriptBinding> globalBindings;
    mfc::CArray<OriginalScriptSubCall> subCalls;
    OriginalScriptDecl* calledScript;
};
static_assert(sizeof(OriginalSSCCmd) == sizeof(SSCCmd) && offsetof(OriginalSSCCmd, subCalls) == offsetof(SSCCmd, subCalls));
struct OriginalScriptDecl {
    mfc::CArray<OriginalSSCCmd> commands;
    std::int32_t id;
};
static_assert(sizeof(OriginalScriptDecl) == sizeof(ScriptDecl));
using OriginalScriptDecls = mfc::CList<OriginalScriptDecl>;

inline const OriginalScriptDecls& OriginalBlocks(const ScriptDecls& blocks) {
    return reinterpret_cast<const OriginalScriptDecls&>(blocks);
}

// A Script / DeclareScript definition.
struct OriginalScriptDefinition {
    std::int32_t index;
    mfc::CString name;
    mfc::CString params;
    ScriptIfThens ifThen;
    OriginalScriptVars vars;
};
static_assert(offsetof(OriginalScriptDefinition, ifThen) == 0x0C && sizeof(OriginalScriptDefinition) == 0xC0);

inline const mfc::CArray<OriginalScriptDefinition>& OriginalDefinitions(const CScript* script) {
    return *reinterpret_cast<const mfc::CArray<OriginalScriptDefinition>*>(&script->definitions_);
}
