#pragma once

// What the script recorders read of game.exe's own CScript where its layout and ours differ: game.exe keeps the error
// as a CString followed by the code and the line, at the offset where ours has errorText_ (a std::string, larger);
// the members before it are laid out alike. game.exe's tokens, defines and definitions hold CStrings where ours hold
// std::strings (larger): the recorders read them through the views below, never script.h's with its sizes.

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

// A Script / DeclareScript definition.
struct OriginalScriptDefinition {
    std::int32_t index;
    mfc::CString name;
    mfc::CString params;
    ScriptIfThens ifThen;
    ScriptVars vars;
};
static_assert(offsetof(OriginalScriptDefinition, ifThen) == 0x0C && sizeof(OriginalScriptDefinition) == 0xC0);

inline const mfc::CArray<OriginalScriptDefinition>& OriginalDefinitions(const CScript* script) {
    return *reinterpret_cast<const mfc::CArray<OriginalScriptDefinition>*>(&script->definitions_);
}
