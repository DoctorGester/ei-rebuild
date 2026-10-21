// Replay of the script loader's records (script_load_vectors.h) against src/game/script_parse_load.cpp
// and the script code it calls (the arrays', variables' and blocks' ports, reached through their
// addresses: hook_stub.cpp). Each record gets a CScript built from the recorded state: the error, the
// file count and loaded flag, the definitions (their pairs empty: the loader only counts them), the
// global variables, and the block lists holding just their counts (the loader numbers new blocks by
// the count; the replay's lists then hold only the new blocks, which are what the record hashed).
#include "script_load_vectors.h"
#include "script_vectors.h"

#include "../../src/game/script.h"

#include "ei/hook.h"
#include "game/game_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::script;

// ---- Building the state -------------------------------------------------------------------------

// Storage for `maxSize` elements as the game's arrays have it (operator new; none when 0).
template <typename T>
void ReadShape(Reader& r, mfc::CArray<T>* a, void* vtable) {
    a->vtable = vtable;
    a->size = r.Get<std::int32_t>();
    a->maxSize = r.Get<std::int32_t>();
    a->growBy = r.Get<std::int32_t>();
    a->data = nullptr;
    if (a->maxSize > 0) {
        const std::size_t bytes = static_cast<std::size_t>(a->maxSize) * sizeof(T);
        a->data = static_cast<T*>(gamecrt::operator_new(bytes));
        std::memset(static_cast<void*>(a->data), 0, bytes);
    }
}

void ReadTexts(Reader& r, mfc::CArray<mfc::CString>* a) {
    ReadShape(r, a, scriptvtable::kStrings);
    for (int i = 0; i < a->size; ++i) {
        const std::string text = r.String();
        new (&a->data[i]) mfc::CString(text.c_str(), static_cast<int>(text.size()));
    }
}

// PutVars' set: objects null, groups empty (the loader reads neither).
void ReadVars(Reader& r, ScriptVars* v) {
    v->ScriptVarsConstruct();
    ReadShape(r, &v->floats, scriptvtable::kFloats);
    if (v->floats.size > 0) {
        if (const unsigned char* p = r.Bytes(static_cast<std::size_t>(v->floats.size) * 4))
            std::memcpy(v->floats.data, p, static_cast<std::size_t>(v->floats.size) * 4);
    }
    ReadTexts(r, &v->floatNames);
    ReadTexts(r, &v->strings);
    ReadTexts(r, &v->stringNames);
    ReadShape(r, &v->objects, scriptvtable::kRefs);
    ReadTexts(r, &v->objectNames);
    ReadShape(r, &v->groups, scriptvtable::kGroups);
    for (int i = 0; i < v->groups.size; ++i) v->groups.data[i].vtable = scriptvtable::kRefs;
    ReadTexts(r, &v->groupNames);
}

void InitBlocks(mfc::CList<ScriptDecl>* list, int count) {
    list->vtable = scriptvtable::kBlockList;
    list->head = list->tail = list->free = nullptr;
    list->blocks = nullptr;
    list->count = count;
    list->blockSize = 10;
}

CScript* EmptyScript() {
    auto* s = static_cast<CScript*>(std::calloc(1, sizeof(CScript)));
    new (&s->errorText_) mfc::CString;
    return s;
}

// PutState's script, and (for Load) the pair count each definition had.
CScript* ReadState(Reader& r, std::vector<int>* pairs = nullptr) {
    CScript* s = EmptyScript();
    s->fileCount_ = r.Get<std::int32_t>();
    s->loaded_ = r.Get<std::int32_t>();
    InitBlocks(&s->blocks_, r.Get<std::int32_t>());
    InitBlocks(&s->worldBlocks_, r.Get<std::int32_t>());
    s->definitions_.vtable = scriptvtable::kDefinitions;
    if (r.Get<std::uint8_t>()) {
        ReadShape(r, &s->definitions_, scriptvtable::kDefinitions);
        for (int i = 0; i < s->definitions_.size && r.ok(); ++i) {
            ScriptDefinition& d = s->definitions_.data[i];
            d.index = r.Get<std::int32_t>();
            const std::string name = r.String();
            new (&d.name) mfc::CString(name.c_str(), static_cast<int>(name.size()));
            const std::string params = r.String();
            new (&d.params) mfc::CString(params.c_str(), static_cast<int>(params.size()));
            ReadShape(r, &d.ifThen, scriptvtable::kIfThen);
            if (pairs) pairs->push_back(d.ifThen.size);
            ReadVars(r, &d.vars);
        }
    }
    ReadVars(r, s->Globals());
    return s;
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

void SetError(CScript* s, const Error& e) {
    s->ErrorCode() = e.code;
    s->ErrorLine() = e.line;
    s->ErrorText() = e.text.c_str();
}

Error TakeError(CScript* s) { return {s->ErrorCode(), s->ErrorLine(), static_cast<const char*>(s->ErrorText())}; }

bool SameError(const Error& a, const Error& b) { return a.code == b.code && a.line == b.line && a.text == b.text; }

// Blocks a command refers to by id (a called block the command had before).
std::deque<ScriptDecl> g_blocks;
ScriptDecl* ReadBlockRef(Reader& r) {
    if (!r.Get<std::uint8_t>()) return nullptr;
    g_blocks.emplace_back();
    ScriptDecl& block = g_blocks.back();
    std::memset(static_cast<void*>(&block), 0, sizeof(block));
    block.commands.vtable = scriptvtable::kCmds;
    block.id = r.Get<std::int32_t>();
    return &block;
}

// PutRun's tokens: the list, and where the call must end (the token after the consumed ones, or null).
struct Run {
    std::vector<ScriptTokenList::Node> nodes;
    ScriptTokenList::Node tail{};
    ScriptTokenList list{};
    ScriptPos start = nullptr;
    ScriptPos end = nullptr;
};

void ReadToken(Reader& r, ScriptTokenList::Node* node) {
    const std::string text = r.String();
    new (&node->data.text) mfc::CString(text.c_str(), static_cast<int>(text.size()));
    node->data.line = r.Get<std::int32_t>();
}

void ReadRun(Reader& r, Run* run) {
    const std::uint32_t n = r.Get<std::uint32_t>();
    if (n > r.left()) return;   // a broken payload: RunBytes reports it
    run->nodes.resize(n + 1);
    for (std::uint32_t i = 0; i < n; ++i) ReadToken(r, &run->nodes[i]);
    const bool next = r.Get<std::uint8_t>() != 0;
    if (next) ReadToken(r, &run->nodes[n]);
    const std::uint32_t linked = n + (next ? 1 : 0);
    for (std::uint32_t i = 0; i < linked; ++i) {
        run->nodes[i].prev = i ? &run->nodes[i - 1] : nullptr;
        run->nodes[i].next = i + 1 < linked ? &run->nodes[i + 1] : nullptr;
    }
    new (&run->tail.data.text) mfc::CString;
    run->tail.data.line = r.Get<std::int32_t>();
    run->list.vtable = ei::GamePointer<void>(0x0073D424);
    run->list.head = linked ? &run->nodes[0] : nullptr;
    run->list.tail = &run->tail;   // only its line is read
    run->list.count = static_cast<int>(linked);
    run->list.blockSize = 10;
    run->start = run->list.head;
    run->end = next ? &run->nodes[n] : nullptr;
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
    CScript* s = EmptyScript();   // ExpectLexeme reads only the error
    SetError(s, ReadError(r));
    const int id = r.Get<std::int32_t>();
    Run run;
    ReadRun(r, &run);
    const crt::NegativeClassesScope negative(r);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    if (!r.ok()) return true;
    ScriptPos pos = run.start;
    const int result = s->ExpectLexeme(&run.list, &pos, id);
    const Error error = TakeError(s);
    const bool ok = result == result0 && SameError(error, error0) && pos == run.end;
    if (!ok) Describe(d, n, "ExpectLexeme", result0, result, error0, error, pos == run.end ? "" : "; ends elsewhere");
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::ExpectLexeme", CheckExpect);

ScriptVars* ReadLocals(Reader& r) {
    if (!r.Get<std::uint8_t>()) return nullptr;
    auto* v = static_cast<ScriptVars*>(std::calloc(1, sizeof(ScriptVars)));
    ReadVars(r, v);
    return v;
}

bool CheckLoadFunction(Reader& r, char* d, std::size_t n) {
    CScript* s = ReadState(r);
    SetError(s, ReadError(r));
    const int id = r.Get<std::int32_t>();
    const std::string signature = r.String();
    const BOOL assignment = r.Get<std::int32_t>();
    const BOOL scriptCall = r.Get<std::int32_t>();
    ScriptVars* locals = ReadLocals(r);
    alignas(8) unsigned char storage[sizeof(SSCCmd)];
    SSCCmd* cmd = reinterpret_cast<SSCCmd*>(storage)->Construct();
    cmd->id = r.Get<std::int32_t>();
    cmd->calledScript = ReadBlockRef(r);
    for (int* growBy : {&cmd->floatArgs.growBy, &cmd->stringArgs.growBy, &cmd->objectArgs.growBy, &cmd->groupArgs.growBy,
                        &cmd->localBindings.growBy, &cmd->globalBindings.growBy, &cmd->subCalls.growBy})
        *growBy = r.Get<std::int32_t>();
    Run run;
    ReadRun(r, &run);
    const crt::NegativeClassesScope negative(r);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const std::uint64_t cmd0 = r.Get<std::uint64_t>();
    if (!r.ok()) return true;
    ScriptPos pos = run.start;
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
    CScript* s = ReadState(r);
    SetError(s, ReadError(r));
    ScriptVars* locals = ReadLocals(r);
    ScriptCommands commands{{scriptvtable::kCmds, nullptr, 0, 0, r.Get<std::int32_t>()}};
    Run run;
    ReadRun(r, &run);
    const crt::NegativeClassesScope negative(r);
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const std::uint64_t commands0 = r.Get<std::uint64_t>();
    const std::uint64_t blocks0 = r.Get<std::uint64_t>();
    if (!r.ok()) return true;
    ScriptPos pos = run.start;
    const int result = s->LoadCommands(&commands, &pos, &run.list, locals);
    const Error error = TakeError(s);
    const bool sameCommands = HashOf([&](Writer& h) { PutCmds(h, commands); }) == commands0;
    const bool sameBlocks = HashOf([&](Writer& h) { PutBlocksAfter(h, s->blocks_, nullptr); }) == blocks0;
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
    CScript* s = ReadState(r, &pairs);
    SetError(s, ReadError(r));
    const std::string source = r.String();
    const crt::NegativeClassesScope negative(r);
    const BOOL runWorldScript = r.Get<std::int32_t>();
    const int result0 = r.Get<std::int32_t>();
    const Error error0 = ReadError(r);
    const int loaded0 = r.Get<std::int32_t>();
    const int files0 = r.Get<std::int32_t>();
    std::uint64_t hashes0[5];
    for (std::uint64_t& h : hashes0) h = r.Get<std::uint64_t>();
    const bool ran0 = r.Get<std::uint8_t>() != 0;
    const int ranId0 = ran0 ? r.Get<std::int32_t>() : 0;
    if (!r.ok()) return true;

    mfc::CString text(source.c_str(), static_cast<int>(source.size()));
    g_ranWorld = false;
    const int result = s->Load(&text, runWorldScript);
    const Error error = TakeError(s);
    const std::uint64_t hashes[5] = {
        HashOf([&](Writer& h) { PutText(h, text); }),
        HashOf([&](Writer& h) { PutDefinitionsAfter(h, s->Scripts(), pairs); }),
        HashOf([&](Writer& h) { PutVars(h, *s->Globals()); }),
        HashOf([&](Writer& h) { PutBlocksAfter(h, s->blocks_, nullptr); }),
        HashOf([&](Writer& h) { PutBlocksAfter(h, s->worldBlocks_, nullptr); }),
    };
    static const char* const kParts[5] = {"the text", "the definitions", "the globals", "the new blocks",
                                          "the new WorldScript blocks"};
    const char* differs = nullptr;
    for (int i = 0; i < 5 && !differs; ++i)
        if (hashes[i] != hashes0[i]) differs = kParts[i];
    const bool sameRun = g_ranWorld == ran0 && (!ran0 || g_ranWorldId == ranId0);
    const bool ok = result == result0 && SameError(error, error0) && s->LoadedFlag() == loaded0 &&
                    s->FileCount() == files0 && !differs && sameRun;
    if (!ok) {
        char extra[160];
        std::snprintf(extra, sizeof(extra), "; loaded %d / %d, files %d / %d%s%s%s", s->LoadedFlag(), loaded0,
                      s->FileCount(), files0, differs ? "; differs: " : "", differs ? differs : "",
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
void __fastcall ScriptRunWorldScript(CScript*, void*, ScriptDecl* block) {
    g_ranWorld = true;
    g_ranWorldId = block->id;
}
