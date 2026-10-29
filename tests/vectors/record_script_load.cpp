// Record mode for the script loader's records of the "script" family (script_load_vectors.h). The
// original loader calls these through their addresses (Load calls ExpectLexeme and LoadCommands,
// LoadCommands calls LoadFunction, LoadFunction itself), so every level is recorded, and so are the
// original zone and mission loaders' calls (recording runs the original game). script_parse reaches every
// parser message through console lines; the zones load their mission scripts.
#include "script_load_vectors.h"
#include "script_vectors.h"

#include "../../src/game/script.h"

#include "ei/hook.h"

#include <vector>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::script;

Sampler g_expect("script", "CScript::ExpectLexeme", kVariableSize, 64, 2);
Sampler g_loadFunction("script", "CScript::LoadFunction", kVariableSize, 32, 6);
Sampler g_loadCommands("script", "CScript::LoadCommands", kVariableSize, 16, 4);
Sampler g_load("script", "CScript::Load", kVariableSize, 6, 6);

// Load's records may hold a whole mission script.
constexpr std::uint32_t kMaxLoad = 0x30000;

void PutLocals(Writer& w, const OriginalScriptVars* locals) {
    w.Put<std::uint8_t>(locals ? 1 : 0);
    if (locals) PutVars(w, *locals);
}

// No elements and no storage.
template <typename T>
bool Empty(const exe::Vector<T>& a) {
    return a.empty() && a.maxSize == 0 && !a.data();
}

// 004CF4D0: MOV EAX,FS:[0] (6). Bucket: the result.
int(__fastcall* s_expect)(OriginalScript*, void*, const OriginalScriptTokenList*, OriginalScriptPos*, int);
int __fastcall ExpectRecord(OriginalScript* self, void*, const OriginalScriptTokenList* tokens, OriginalScriptPos* pos,
                             int id) {
    OfferFixtures();
    Writer w;
    PutError(w, self);
    w.Put<std::int32_t>(id);
    const OriginalScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const int result = s_expect(self, nullptr, tokens, pos, id);
    PutRun(w, tokens, from, *pos);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    g_expect.OfferBytes(w, cw, result ? 0 : 1);
    return result;
}

// 004CFBC0: PUSH -1; PUSH imm32 (7). Bucket: a plain call, an assignment or a script call, each
// succeeded or failed.
int(__fastcall* s_loadFunction)(OriginalScript*, void*, OriginalSSCCmd*, OriginalScriptPos*,
                                 const OriginalScriptTokenList*, int, OriginalScriptVars*, const char*, int, int);
int __fastcall LoadFunctionRecord(OriginalScript* self, void*, OriginalSSCCmd* cmd, OriginalScriptPos* pos,
                                   const OriginalScriptTokenList* tokens, int id, OriginalScriptVars* locals,
                                   const char* signature, int assignment, int scriptCall) {
    OfferFixtures();
    const bool empty = Empty(cmd->floatArgs) && Empty(cmd->stringArgs) && Empty(cmd->objectArgs) &&
                       Empty(cmd->groupArgs) && Empty(cmd->localBindings) && Empty(cmd->globalBindings) &&
                       Empty(cmd->subCalls);
    Writer w;
    if (empty) {
        PutState(w, self, false);
        PutError(w, self);
        w.Put<std::int32_t>(id);
        w.PutString(signature);
        w.Put<std::int32_t>(assignment);
        w.Put<std::int32_t>(scriptCall);
        PutLocals(w, locals);
        w.Put<std::int32_t>(cmd->id);
        PutBlockRef(w, cmd->calledScript);
    }
    const OriginalScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const int result = s_loadFunction(self, nullptr, cmd, pos, tokens, id, locals, signature, assignment, scriptCall);
    if (!empty) return result;   // never seen: every caller clears the command
    PutRun(w, tokens, from, *pos);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    w.Put(HashOf([&](Writer& h) { PutCmd(h, *cmd); }));
    if (w.size() <= kMaxLoadPayload)
        g_loadFunction.OfferBytes(w, cw, (result ? 0 : 3) + (assignment ? 1 : scriptCall ? 2 : 0));
    return result;
}

// 004D1330: PUSH -1; PUSH imm32 (7). Bucket: succeeded or failed, payload up to 4 KB or larger.
int(__fastcall* s_loadCommands)(OriginalScript*, void*, exe::Vector<OriginalSSCCmd>*, OriginalScriptPos*,
                                  const OriginalScriptTokenList*, OriginalScriptVars*);
int __fastcall LoadCommandsRecord(OriginalScript* self, void*, exe::Vector<OriginalSSCCmd>* commands,
                                   OriginalScriptPos* pos, const OriginalScriptTokenList* tokens,
                                   OriginalScriptVars* locals) {
    OfferFixtures();
    const bool empty = Empty(*commands);
    Writer w;
    if (empty) {
        PutState(w, self, true);
        PutError(w, self);
        PutLocals(w, locals);
    }
    const std::size_t blocksBefore = self->blocks.size();
    const OriginalScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const int result = s_loadCommands(self, nullptr, commands, pos, tokens, locals);
    if (!empty) return result;   // never seen: the arrays are new
    PutRun(w, tokens, from, *pos);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    w.Put(HashOf([&](Writer& h) { PutCmds(h, *commands); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->blocks, blocksBefore); }));
    if (w.size() <= kMaxLoadPayload) g_loadCommands.OfferBytes(w, cw, (result ? 0 : 2) + (w.size() > 0x1000 ? 1 : 0));
    return result;
}

// 004D23A0: PUSH -1; PUSH imm32 (7). Bucket: succeeded or failed, by the text's size (SourceBucket).
int(__fastcall* s_load)(OriginalScript*, void*, exe::CString*, int);
int __fastcall LoadRecord(OriginalScript* self, void*, exe::CString* text, int runWorldScript) {
    OfferFixtures();
    Writer w;
    PutState(w, self, true);
    PutError(w, self);
    PutText(w, *text);
    w.Put<std::int32_t>(runWorldScript);
    const std::uint32_t length = Length(*text);
    std::vector<int> skip;
    for (const OriginalScriptDefinition& d : self->definitions) skip.push_back(static_cast<int>(d.ifThen.size()));
    const std::size_t blocksBefore = self->blocks.size();
    const std::size_t worldBlocksBefore = self->worldBlocks.size();
    const std::size_t runs = self->runs.size();
    const std::uint16_t cw = ControlWord();
    const int result = s_load(self, nullptr, text, runWorldScript);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    w.Put<std::int32_t>(self->loaded);
    w.Put<std::int32_t>(self->fileCount);
    w.Put(HashOf([&](Writer& h) { PutText(h, *text); }));
    w.Put(HashOf([&](Writer& h) { PutDefinitionsAfter(h, self->definitions, skip); }));
    w.Put(HashOf([&](Writer& h) { PutVars(h, self->globals); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->blocks, blocksBefore); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->worldBlocks, worldBlocksBefore); }));
    // RunWorldScript (004BCC60) adds a run of the block at the head of the run list.
    PutBlockRef(w, self->runs.size() > runs ? (*self->runs.begin()).block : nullptr);
    if (w.size() <= kMaxLoad) g_load.OfferBytes(w, cw, (result ? 0 : 3) + SourceBucket(length));
    return result;
}

} // namespace

EI_RECORD(0x004CF4D0, ExpectRecord, 6, s_expect, CScript::ExpectLexeme);
EI_RECORD(0x004CFBC0, LoadFunctionRecord, 7, s_loadFunction, CScript::LoadFunction);
EI_RECORD(0x004D1330, LoadCommandsRecord, 7, s_loadCommands, CScript::LoadCommands);
EI_RECORD(0x004D23A0, LoadRecord, 7, s_load, CScript::Load);
