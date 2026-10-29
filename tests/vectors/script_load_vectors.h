#pragma once

// Payloads of the script loader's records in the "script" family (vectors.h, script_vectors.h):
// Script::ExpectLexeme, LoadFunction, LoadCommands and Load. Recorded by record_script_load.cpp,
// replayed by replay_script_load.cpp. The serialisers below run on game.exe's objects when recording
// (script_original.h's views) and on the replay's when checking, so both write the same bytes for the same
// contents.
//
// Every payload starts with the script's state (PutState) and the call's inputs, then holds what the
// call left: the result, the script's error (code, line, text) and hashes (HashBytes of a serialiser's
// bytes) of what it built. Arrays are written with their size (i32); object and group values only by
// count (the loader never reads them, and the replay has no world objects).
//
// Tokens (PutRun): the tokens the call consumed (count, then text and line each), whether a token
// followed them (u8, then its text and line: the position the call ended at), the tail's line (i32;
// "Unexpected end of file" reports it).
// The loaders read no token past the ones they consume, so the replay's list is just these.
//
// ExpectLexeme: the error before, the id (i32), the run; then the result and the error.
// LoadFunction: the state (no definitions), the id, the signature (string), assignment and script call
//   (i32 each), the locals (u8 flag, PutVars), the command's id and its called block (u8 flag, block id)
//   (the command is empty: every caller clears it), the run; then the result, the error and the hash of the
//   command (PutCmd).
// LoadCommands: the state, the locals (u8 flag, PutVars) (the array is empty on entry), the run; then the
//   result, the error, the hash of the array (PutCmds) and of the new blocks.
// Load: the state, the text (string), runWorldScript (i32); then the result,
//   the error, the loaded flag and file count, the hash of the text afterwards (the #define pass
//   rewrites it), of the definitions (pairs from before skipped), of the globals, of the new command
//   blocks and the new WorldScript blocks, and which block RunWorldScript was given (u8 flag, id).

#include "script_original.h"
#include "vectors.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ei::vectors::script {

// LoadFunction / LoadCommands records larger than this are not kept (a mission's sections: Load's
// records run those through ours in the replay).
constexpr std::uint32_t kMaxLoadPayload = 0x4000;

inline void PutText(Writer& w, const exe::CString& s) { w.PutString(static_cast<const char*>(s)); }
inline void PutText(Writer& w, const std::string& s) { w.PutString(s.c_str()); }

// An array's size and, for plain elements, its bytes.
template <typename A>
void PutSize(Writer& w, const A& a) {
    w.Put<std::int32_t>(static_cast<std::int32_t>(a.size()));
}
template <typename A>
void PutPlain(Writer& w, const A& a) {
    PutSize(w, a);
    if (a.size()) w.PutBytes(&a[0], a.size() * sizeof(a[0]));
}

template <typename A>
void PutTexts(Writer& w, const A& a) {
    PutSize(w, a);
    for (std::size_t i = 0; i < a.size(); ++i) PutText(w, a[i]);
}

// A variable set: names and values per type (floats bit for bit, strings as text, objects and groups
// by count). game.exe's (OriginalScriptVars) when recording, ours when checking.
template <typename Vars>
void PutVars(Writer& w, const Vars& v) {
    PutPlain(w, v.floats.values);
    PutTexts(w, v.floats.names);
    PutTexts(w, v.strings.values);
    PutTexts(w, v.strings.names);
    PutSize(w, v.objects.values);
    PutTexts(w, v.objects.names);
    PutSize(w, v.groups.values);
    PutTexts(w, v.groups.names);
}

template <typename Block>
void PutBlockRef(Writer& w, const Block* block) {
    w.Put<std::uint8_t>(block ? 1 : 0);
    if (block) w.Put<std::int32_t>(block->id);
}

// A call tree: constants, which object / group slots are set, bindings, nested calls, the block.
template <typename Cmd>
void PutCmd(Writer& w, const Cmd& c) {
    w.Put<std::int32_t>(c.id);
    PutPlain(w, c.floatArgs);
    PutTexts(w, c.stringArgs);
    PutSize(w, c.objectArgs);
    for (std::size_t i = 0; i < c.objectArgs.size(); ++i) w.Put<std::uint8_t>(c.objectArgs[i] ? 1 : 0);
    PutSize(w, c.groupArgs);
    for (std::size_t i = 0; i < c.groupArgs.size(); ++i) w.Put<std::uint8_t>(c.groupArgs[i] ? 1 : 0);
    PutPlain(w, c.localBindings);
    PutPlain(w, c.globalBindings);
    PutSize(w, c.subCalls);
    for (std::size_t i = 0; i < c.subCalls.size(); ++i) {
        const auto& s = c.subCalls[i];
        w.Put<std::int32_t>(s.type);
        w.Put<std::int32_t>(s.argIndex);
        w.Put<std::uint8_t>(s.cmd ? 1 : 0);
        if (s.cmd) PutCmd(w, *s.cmd);
    }
    PutBlockRef(w, c.calledScript);
}

template <typename A>
void PutCmds(Writer& w, const A& a) {
    PutSize(w, a);
    for (std::size_t i = 0; i < a.size(); ++i) PutCmd(w, a[i]);
}

// The blocks of `list` after its first `skip`: count, then id and commands each.
template <typename List>
void PutBlocksAfter(Writer& w, const List& list, std::size_t skip) {
    const std::size_t count = list.size();
    w.Put(static_cast<std::uint32_t>(count > skip ? count - skip : 0));
    std::size_t i = 0;
    for (const auto& block : list) {
        if (i++ < skip) continue;
        w.Put<std::int32_t>(block.id);
        PutCmds(w, block.commands);
    }
}

// The definitions after a load: the pairs a definition had before (`skip`, by index) are left out. game.exe's
// (OriginalScriptDefinition) when recording, ours when checking.
template <typename A>
void PutDefinitionsAfter(Writer& w, const A& a, const std::vector<int>& skip) {
    PutSize(w, a);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& d = a[i];
        w.Put<std::int32_t>(d.index);
        PutText(w, d.name);
        PutText(w, d.params);
        PutSize(w, d.ifThen);
        for (std::size_t k = i < skip.size() ? static_cast<std::size_t>(skip[i]) : 0; k < d.ifThen.size(); ++k) {
            PutBlockRef(w, d.ifThen[k].conditions);
            PutBlockRef(w, d.ifThen[k].commands);
        }
        PutVars(w, d.vars);
    }
}

// The script's state as the loader reads it (game.exe's script). With `definitions` false (LoadFunction) the
// definitions are left out.
inline void PutState(Writer& w, const OriginalScript* s, bool definitions) {
    w.Put<std::int32_t>(s->fileCount);
    w.Put<std::int32_t>(s->loaded);
    w.Put<std::int32_t>(static_cast<std::int32_t>(s->blocks.size()));
    w.Put<std::int32_t>(static_cast<std::int32_t>(s->worldBlocks.size()));
    w.Put<std::uint8_t>(definitions ? 1 : 0);
    if (definitions) {
        PutSize(w, s->definitions);
        for (const OriginalScriptDefinition& d : s->definitions) {
            w.Put<std::int32_t>(d.index);
            PutText(w, d.name);
            PutText(w, d.params);
            PutSize(w, d.ifThen);
            PutVars(w, d.vars);
        }
    }
    PutVars(w, s->globals);
}

// (game.exe's script)
inline void PutError(Writer& w, const OriginalScript* s) {
    w.Put<std::int32_t>(s->errorCode);
    w.Put<std::int32_t>(s->errorLine);
    PutText(w, s->errorText);
}

template <typename Fn>
std::uint64_t HashOf(Fn&& put) {
    Writer w;
    put(w);
    return HashBytes(w.data(), w.size());
}

// The tokens from `from` up to `to` (see the header comment; game.exe's tokens).
inline void PutRun(Writer& w, const OriginalScriptTokenList* tokens, OriginalScriptPos from, OriginalScriptPos to) {
    std::uint32_t n = 0;
    for (OriginalScriptPos p = from; p && p != to; p = p->next) ++n;
    w.Put(n);
    for (OriginalScriptPos p = from; p && p != to; p = p->next) {
        PutText(w, p->data.text);
        w.Put<std::int32_t>(p->data.line);
    }
    w.Put<std::uint8_t>(to ? 1 : 0);
    if (to) {
        PutText(w, to->data.text);
        w.Put<std::int32_t>(to->data.line);
    }
    w.Put<std::int32_t>(tokens->tail ? tokens->tail->data.line : 0);
}

} // namespace ei::vectors::script
