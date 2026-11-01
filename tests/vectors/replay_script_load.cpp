// Replay of the script loader's records (script_load_vectors.h) against src/game/script_parse_load.cpp
// and the script code it calls. Each record gets a Script built from the recorded state: the error, the
// file count and loaded flag, the definitions (their pairs empty: the loader only counts them), the
// global variables, and the block lists holding as many empty blocks as they had (the loader numbers new
// blocks by the count; the record hashed the new blocks after them).
#include "script_load_vectors.h"
#include "script_vectors.h"

#include "../../src/game/script.h"


#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::script;

// ---- Building the state -------------------------------------------------------------------------

void ReadTexts(Reader& r, std::vector<std::string>* a) {
    a->resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
    for (std::string& text : *a) text = r.String();
}

// PutVars' set: objects null, groups empty (the loader reads neither).
void ReadVars(Reader& r, ScriptVars* v) {
    v->floats.values.resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
    if (!v->floats.values.empty()) {
        if (const unsigned char* p = r.Bytes(v->floats.values.size() * 4))
            std::memcpy(v->floats.values.data(), p, v->floats.values.size() * 4);
    }
    ReadTexts(r, &v->floats.names);
    ReadTexts(r, &v->strings.values);
    ReadTexts(r, &v->strings.names);
    v->objects.values.resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
    ReadTexts(r, &v->objects.names);
    v->groups.values.resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
    ReadTexts(r, &v->groups.names);
}

// PutState's script, the block counts it had (the new blocks follow them) and, for Load, the pair count
// each definition had. The loader numbers new blocks by the list's size: empty blocks stand for the
// recorded ones.
constexpr std::size_t kMaxBlocks = 0x100000;

struct State {
    Script* script = nullptr;
    std::size_t blocks = 0;
    std::size_t worldBlocks = 0;
};

State ReadState(Reader& r, std::vector<int>* pairs = nullptr) {
    State state;
    Script* s = state.script = new Script;
    s->fileCount = r.Get<std::int32_t>();
    s->loaded = r.Get<std::int32_t>();
    state.blocks = r.Get<std::uint32_t>();
    state.worldBlocks = r.Get<std::uint32_t>();
    if (state.blocks > kMaxBlocks || state.worldBlocks > kMaxBlocks) return state;   // a broken payload
    s->blocks.resize(state.blocks);
    s->worldBlocks.resize(state.worldBlocks);
    if (r.Get<std::uint8_t>()) {
        s->definitions.resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
        for (ScriptDefinition& d : s->definitions) {
            if (!r.ok()) break;
            d.index = r.Get<std::int32_t>();
            d.name = r.String();
            d.params = r.String();
            d.ifThen.resize(static_cast<std::size_t>(r.Get<std::int32_t>()));
            if (pairs) pairs->push_back(static_cast<int>(d.ifThen.size()));
            ReadVars(r, &d.vars);
        }
    }
    ReadVars(r, &s->globals);
    return state;
}

struct Error {
    int code = 0, line = 0;
    std::string text;
};

Error ReadError(Reader& r) {
    Error e;
    e.code = r.Get<std::int32_t>();
    e.line = r.Get<std::int32_t>();
    e.text = r.String();
    return e;
}

void SetError(Script* s, const Error& e) {
    s->errorCode = e.code;
    s->errorLine = e.line;
    s->errorText = e.text.c_str();
}

Error TakeError(Script* s) { return {s->errorCode, s->errorLine, s->errorText.c_str()}; }

bool SameError(const Error& a, const Error& b) { return a.code == b.code && a.line == b.line && a.text == b.text; }

// Blocks a command refers to by id (a called block the command had before).
std::deque<ScriptDecl> g_blocks;
ScriptDecl* ReadBlockRef(Reader& r) {
    if (!r.Get<std::uint8_t>()) return nullptr;
    ScriptDecl& block = g_blocks.emplace_back();
    block.id = r.Get<std::int32_t>();
    return &block;
}

// PutRun's tokens: the list, and where the call starts and must end (the token after the consumed ones, or
// the list's end). The list's first token stands for the ones before the run and carries the tail's line:
// when a run reaches the end, the list's last token is the original's tail (the last consumed token, or
// this one when none was).
struct Run {
    std::vector<ScriptToken> list;
    std::vector<ScriptToken>::const_iterator start;
    std::vector<ScriptToken>::const_iterator end;
};

ScriptToken ReadToken(Reader& r) {
    ScriptToken token;
    token.text = r.String();
    token.line = r.Get<std::int32_t>();
    return token;
}

void ReadRun(Reader& r, Run* run) {
    const std::uint32_t n = r.Get<std::uint32_t>();
    if (n > r.left()) return;   // a broken payload: RunBytes reports it
    std::vector<ScriptToken> consumed;
    consumed.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) consumed.push_back(ReadToken(r));
    const bool next = r.Get<std::uint8_t>() != 0;
    ScriptToken after;
    if (next) after = ReadToken(r);
    ScriptToken before;
    before.line = r.Get<std::int32_t>();
    run->list.push_back(std::move(before));
    for (ScriptToken& token : consumed) run->list.push_back(std::move(token));
    if (next) run->list.push_back(std::move(after));
    run->start = std::next(run->list.cbegin());
    run->end = next ? std::prev(run->list.cend()) : run->list.cend();
}

// The block RunWorldScript was given in the replay (Load).
bool g_ranWorld = false;
int g_ranWorldId = 0;

// "result 1 vs 0, error ..." for the detail line.
void Describe(char* d, std::size_t n, const char* what, int result0, int result, const Error& e0, const Error& e,
              const char* extra) {
    std::snprintf(d, n, "%s: result ours %d, original %d; error ours %d line %d \"%.60s\", original %d line %d \"%.60s\"%s",
                  what, result, result0, e.code, e.line, e.text.c_str(), e0.code, e0.line, e0.text.c_str(), extra);
}

// ---- Checks -------------------------------------------------------------------------------------

bool CheckExpect(Reader& r, char* d, std::size_t n) {
    Script* s = new Script;   // ExpectLexeme reads only the error
    SetError(s, ReadError(r));
    const int id = r.Get<std::int32_t>();
    Run run;
    ReadRun(r, &run);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    if (!r.ok()) return true;
    std::vector<ScriptToken>::const_iterator pos = run.start;
    const int result = s->ExpectLexeme(&run.list, &pos, id);
    const Error error = TakeError(s);
    const bool ok = result == result0 && SameError(error, error0) && pos == run.end;
    if (!ok) Describe(d, n, "ExpectLexeme", result0, result, error0, error, pos == run.end ? "" : "; ends elsewhere");
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::ExpectLexeme", CheckExpect);

ScriptVars* ReadLocals(Reader& r) {
    if (!r.Get<std::uint8_t>()) return nullptr;
    auto* v = new ScriptVars;
    ReadVars(r, v);
    return v;
}

bool CheckLoadFunction(Reader& r, char* d, std::size_t n) {
    Script* s = ReadState(r).script;
    SetError(s, ReadError(r));
    const int id = r.Get<std::int32_t>();
    const std::string signature = r.String();
    const bool assignment = r.Get<std::int32_t>();
    const bool scriptCall = r.Get<std::int32_t>();
    ScriptVars* locals = ReadLocals(r);
    auto* cmd = new SSCCmd;
    cmd->id = r.Get<std::int32_t>();
    cmd->calledScript = ReadBlockRef(r);
    Run run;
    ReadRun(r, &run);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const std::uint64_t cmd0 = r.Get<std::uint64_t>();
    if (!r.ok()) return true;
    std::vector<ScriptToken>::const_iterator pos = run.start;
    const int result = s->LoadFunction(cmd, &pos, &run.list, id, locals, signature.c_str(), assignment, scriptCall);
    const Error error = TakeError(s);
    const bool sameCmd = HashOf([&](Writer& h) { PutCmd(h, *cmd); }) == cmd0;
    const bool ok = result == result0 && SameError(error, error0) && pos == run.end && sameCmd;
    if (!ok) {
        char what[160];
        std::snprintf(what, sizeof(what), "LoadFunction id %X \"%s\"%s%s", id, signature.c_str(),
                      assignment ? " (assignment)" : "", scriptCall ? " (script call)" : "");
        Describe(d, n, what, result0, result, error0, error,
                 !sameCmd ? "; the command differs" : pos != run.end ? "; ends elsewhere" : "");
    }
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::LoadFunction", CheckLoadFunction);

bool CheckLoadCommands(Reader& r, char* d, std::size_t n) {
    const State state = ReadState(r);
    Script* s = state.script;
    SetError(s, ReadError(r));
    ScriptVars* locals = ReadLocals(r);
    std::vector<SSCCmd> commands;
    Run run;
    ReadRun(r, &run);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const std::uint64_t commands0 = r.Get<std::uint64_t>();
    const std::uint64_t blocks0 = r.Get<std::uint64_t>();
    if (!r.ok()) return true;
    std::vector<ScriptToken>::const_iterator pos = run.start;
    const int result = s->LoadCommands(&commands, &pos, &run.list, locals);
    const Error error = TakeError(s);
    const bool sameCommands = HashOf([&](Writer& h) { PutCmds(h, commands); }) == commands0;
    const bool sameBlocks = HashOf([&](Writer& h) { PutBlocksAfter(h, s->blocks, state.blocks); }) == blocks0;
    const bool ok = result == result0 && SameError(error, error0) && pos == run.end && sameCommands && sameBlocks;
    if (!ok)
        Describe(d, n, "LoadCommands", result0, result, error0, error,
                 !sameCommands ? "; the commands differ" : !sameBlocks ? "; the new blocks differ"
                 : pos != run.end ? "; ends elsewhere" : "");
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::LoadCommands", CheckLoadCommands);

bool CheckLoad(Reader& r, char* d, std::size_t n) {
    std::vector<int> pairs;
    const State state = ReadState(r, &pairs);
    Script* s = state.script;
    SetError(s, ReadError(r));
    const std::string source = r.String();
    const bool runWorldScript = r.Get<std::int32_t>();
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const int loaded0 = r.Get<std::int32_t>();
    const int files0 = r.Get<std::int32_t>();
    std::uint64_t hashes0[5];
    for (std::uint64_t& h : hashes0) h = r.Get<std::uint64_t>();
    const bool ran0 = r.Get<std::uint8_t>() != 0;
    const int ranId0 = ran0 ? r.Get<std::int32_t>() : 0;
    if (!r.ok()) return true;

    std::string text = source;
    g_ranWorld = false;
    const int result = s->Load(&text, runWorldScript);
    const Error error = TakeError(s);
    const std::uint64_t hashes[5] = {
        HashOf([&](Writer& h) { PutText(h, text); }),
        HashOf([&](Writer& h) { PutDefinitionsAfter(h, s->definitions, pairs); }),
        HashOf([&](Writer& h) { PutVars(h, s->globals); }),
        HashOf([&](Writer& h) { PutBlocksAfter(h, s->blocks, state.blocks); }),
        HashOf([&](Writer& h) { PutBlocksAfter(h, s->worldBlocks, state.worldBlocks); }),
    };
    static const char* const kParts[5] = {"the text", "the definitions", "the globals", "the new blocks",
                                          "the new WorldScript blocks"};
    const char* differs = nullptr;
    for (int i = 0; i < 5 && !differs; ++i)
        if (hashes[i] != hashes0[i]) differs = kParts[i];
    const bool sameRun = g_ranWorld == ran0 && (!ran0 || g_ranWorldId == ranId0);
    const bool ok = result == result0 && SameError(error, error0) && s->loaded == loaded0 &&
                    s->fileCount == files0 && !differs && sameRun;
    if (!ok) {
        char extra[160];
        std::snprintf(extra, sizeof(extra), "; loaded %d / %d, files %d / %d%s%s%s", s->loaded, loaded0,
                      s->fileCount, files0, differs ? "; differs: " : "", differs ? differs : "",
                      sameRun ? "" : "; RunWorldScript differs");
        char what[96];
        std::snprintf(what, sizeof(what), "Load of %u bytes", static_cast<unsigned>(source.size()));
        Describe(d, n, what, result0, result, error0, error, extra);
    }
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::Load", CheckLoad);

} // namespace

// 004BCC60 ScriptRunWorldScript (it adds a run of the block; the runtime, script_exec_run.cpp, is not
// replayed): which block Load handed it.
void Script::RunWorldScript(ScriptDecl* block) {
    g_ranWorld = true;
    g_ranWorldId = block->id;
}
