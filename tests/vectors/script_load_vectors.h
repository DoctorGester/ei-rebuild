#pragma once

// Payloads of the script loader's records in the "script" family (vectors.h, script_vectors.h):
// CScript::ExpectLexeme, LoadFunction, LoadCommands and Load. Recorded by record_script_load.cpp,
// replayed by replay_script_load.cpp. The serialisers below run on the game's objects when recording
// and on the replay's when checking, so both write the same bytes for the same contents.
//
// Every payload starts with the script's state (PutState) and the call's inputs, then holds what the
// call left: the result, the script's error (code, line, text) and hashes (HashBytes of a serialiser's
// bytes) of what it built. Arrays are written with their shape (size, allocated size, grow-by); object
// and group values only by count (the loader never reads them, and the replay has no world objects).
//
// Tokens (PutRun): the tokens the call consumed (count, then text and line each), whether a token
// followed them (u8, then its text and line: the position the call ended at), the tail's line (i32;
// "Unexpected end of file" reports it), and the game CRT's answers for the texts' negative chars.
// The loaders read no token past the ones they consume, so the replay's list is just these.
//
// ExpectLexeme: the error before, the id (i32), the run; then the result and the error.
// LoadFunction: the state (no definitions), the id, the signature (string), assignment and script call
//   (i32 each), the locals (u8 flag, PutVars), the command's id, its called block (u8 flag, block id)
//   and its arrays' grow-by (the command is empty: every caller clears it), the run; then the result,
//   the error and the hash of the command (PutCmd).
// LoadCommands: the state, the locals (u8 flag, PutVars), the array's grow-by (empty on entry), the
//   run; then the result, the error, the hash of the array (PutCmds) and of the new blocks.
// Load: the state, the text (string) and its negative chars, runWorldScript (i32); then the result,
//   the error, the loaded flag and file count, the hash of the text afterwards (the #define pass
//   rewrites it), of the definitions (pairs from before skipped), of the globals, of the new command
//   blocks and the new WorldScript blocks, and which block RunWorldScript was given (u8 flag, id).

#include "crt_vectors.h"
#include "vectors.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace ei::vectors::script {

// LoadFunction / LoadCommands records larger than this are not kept (a mission's sections: Load's
// records run those through ours in the replay).
constexpr std::uint32_t kMaxLoadPayload = 0x4000;

inline void PutText(Writer& w, const mfc::CString& s) { w.PutString(static_cast<const char*>(s)); }

template <typename T>
void PutShape(Writer& w, const mfc::CArray<T>& a) {
    w.Put<std::int32_t>(a.size);
    w.Put<std::int32_t>(a.maxSize);
    w.Put<std::int32_t>(a.growBy);
}

inline void PutTexts(Writer& w, const mfc::CArray<mfc::CString>& a) {
    PutShape(w, a);
    for (int i = 0; i < a.size; ++i) PutText(w, a.data[i]);
}

// A variable set: names and values per type (floats bit for bit, strings as text, objects and groups
// by count).
inline void PutVars(Writer& w, const ScriptVars& v) {
    PutShape(w, v.floats);
    if (v.floats.size) w.PutBytes(v.floats.data, static_cast<std::size_t>(v.floats.size) * 4);
    PutTexts(w, v.floatNames);
    PutTexts(w, v.strings);
    PutTexts(w, v.stringNames);
    PutShape(w, v.objects);
    PutTexts(w, v.objectNames);
    PutShape(w, v.groups);
    PutTexts(w, v.groupNames);
}

inline void PutBlockRef(Writer& w, const ScriptDecl* block) {
    w.Put<std::uint8_t>(block ? 1 : 0);
    if (block) w.Put<std::int32_t>(block->id);
}

// A call tree: constants, which object / group slots are set, bindings, nested calls, the block.
inline void PutCmd(Writer& w, const SSCCmd& c) {
    w.Put<std::int32_t>(c.id);
    PutShape(w, c.floatArgs);
    if (c.floatArgs.size) w.PutBytes(c.floatArgs.data, static_cast<std::size_t>(c.floatArgs.size) * 4);
    PutTexts(w, c.stringArgs);
    PutShape(w, c.objectArgs);
    for (int i = 0; i < c.objectArgs.size; ++i) w.Put<std::uint8_t>(c.objectArgs.data[i] ? 1 : 0);
    PutShape(w, c.groupArgs);
    for (int i = 0; i < c.groupArgs.size; ++i) w.Put<std::uint8_t>(c.groupArgs.data[i] ? 1 : 0);
    for (const mfc::CArray<ScriptBinding>* b : {&c.localBindings, &c.globalBindings}) {
        PutShape(w, *b);
        if (b->size) w.PutBytes(b->data, static_cast<std::size_t>(b->size) * sizeof(ScriptBinding));
    }
    PutShape(w, c.subCalls);
    for (int i = 0; i < c.subCalls.size; ++i) {
        const ScriptSubCall& s = c.subCalls.data[i];
        w.Put<std::int32_t>(s.type);
        w.Put<std::int32_t>(s.argIndex);
        w.Put<std::uint8_t>(s.cmd ? 1 : 0);
        if (s.cmd) PutCmd(w, *s.cmd);
    }
    PutBlockRef(w, c.calledScript);
}

inline void PutCmds(Writer& w, const mfc::CArray<SSCCmd>& a) {
    PutShape(w, a);
    for (int i = 0; i < a.size; ++i) PutCmd(w, a.data[i]);
}

// The blocks of `list` after `last` (all of them when null): count, then id and commands each.
inline void PutBlocksAfter(Writer& w, const mfc::CList<ScriptDecl>& list, const mfc::CList<ScriptDecl>::Node* last) {
    std::uint32_t n = 0;
    for (auto* node = last ? last->next : list.head; node; node = node->next) ++n;
    w.Put(n);
    for (auto* node = last ? last->next : list.head; node; node = node->next) {
        w.Put<std::int32_t>(node->data.id);
        PutCmds(w, node->data.commands);
    }
}

// The definitions after a load: the pairs a definition had before (`skip`, by index) are left out.
inline void PutDefinitionsAfter(Writer& w, const mfc::CArray<ScriptDefinition>& a, const std::vector<int>& skip) {
    PutShape(w, a);
    for (int i = 0; i < a.size; ++i) {
        const ScriptDefinition& d = a.data[i];
        w.Put<std::int32_t>(d.index);
        PutText(w, d.name);
        PutText(w, d.params);
        PutShape(w, d.ifThen);
        for (int k = i < static_cast<int>(skip.size()) ? skip[i] : 0; k < d.ifThen.size; ++k) {
            PutBlockRef(w, d.ifThen.data[k].conditions);
            PutBlockRef(w, d.ifThen.data[k].commands);
        }
        PutVars(w, d.vars);
    }
}

// The script's state as the loader reads it. With `definitions` false (LoadFunction) the definitions
// are left out.
inline void PutState(Writer& w, CScript* s, bool definitions) {
    w.Put<std::int32_t>(s->FileCount());
    w.Put<std::int32_t>(s->LoadedFlag());
    w.Put<std::int32_t>(s->blocks_.count);
    w.Put<std::int32_t>(s->worldBlocks_.count);
    w.Put<std::uint8_t>(definitions ? 1 : 0);
    if (definitions) {
        const mfc::CArray<ScriptDefinition>& a = s->Scripts();
        PutShape(w, a);
        for (int i = 0; i < a.size; ++i) {
            const ScriptDefinition& d = a.data[i];
            w.Put<std::int32_t>(d.index);
            PutText(w, d.name);
            PutText(w, d.params);
            PutShape(w, d.ifThen);
            PutVars(w, d.vars);
        }
    }
    PutVars(w, *s->Globals());
}

inline void PutError(Writer& w, CScript* s) {
    w.Put<std::int32_t>(s->ErrorCode());
    w.Put<std::int32_t>(s->ErrorLine());
    PutText(w, s->ErrorText());
}

template <typename Fn>
std::uint64_t HashOf(Fn&& put) {
    Writer w;
    put(w);
    return HashBytes(w.data(), w.size());
}

// The tokens from `from` up to `to` (see the header comment).
inline void PutRun(Writer& w, const ScriptTokenList* tokens, ScriptPos from, ScriptPos to) {
    std::uint32_t n = 0;
    std::vector<char> texts;
    for (ScriptPos p = from; p && p != to; p = p->next) ++n;
    w.Put(n);
    for (ScriptPos p = from; p && p != to; p = p->next) {
        PutText(w, p->data.text);
        w.Put<std::int32_t>(p->data.line);
        const char* t = p->data.text;
        texts.insert(texts.end(), t, t + std::strlen(t));
    }
    w.Put<std::uint8_t>(to ? 1 : 0);
    if (to) {
        PutText(w, to->data.text);
        w.Put<std::int32_t>(to->data.line);
    }
    w.Put<std::int32_t>(tokens->tail ? tokens->tail->data.line : 0);
    crt::PutNegativeClasses(w, texts.data(), texts.size());
}

} // namespace ei::vectors::script
