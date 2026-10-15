// Record mode for the script loader's records of the "script" family (script_load_vectors.h). The
// original loader calls these through their addresses (Load calls ExpectLexeme and LoadCommands,
// LoadCommands calls LoadFunction, LoadFunction itself), so every level is recorded; our rewritten
// callers (the zone and mission loaders) call ours, hence EI_RECORD_OURS. script_parse reaches every
// parser message through console lines; the zones load their mission scripts.
#include "script_load_vectors.h"
#include "script_vectors.h"

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

void PutLocals(Writer& w, const ScriptVars* locals) {
    w.Put<std::uint8_t>(locals ? 1 : 0);
    if (locals) PutVars(w, *locals);
}

template <typename T>
bool Empty(const mfc::CArray<T>& a) {
    return a.size == 0 && a.maxSize == 0 && !a.data;
}

// 004CF4D0: MOV EAX,FS:[0] (6). Bucket: the result.
BOOL(__fastcall* s_expect)(CScript*, void*, const ScriptTokenList*, ScriptPos*, int);
BOOL __fastcall ExpectRecord(CScript* self, void*, const ScriptTokenList* tokens, ScriptPos* pos, int id) {
    OfferFixtures();
    Writer w;
    PutError(w, self);
    w.Put<std::int32_t>(id);
    const ScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const BOOL result = s_expect(self, nullptr, tokens, pos, id);
    PutRun(w, tokens, from, *pos);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    g_expect.OfferBytes(w, cw, result ? 0 : 1);
    return result;
}

// 004CFBC0: PUSH -1; PUSH imm32 (7). Bucket: a plain call, an assignment or a script call, each
// succeeded or failed.
BOOL(__fastcall* s_loadFunction)(CScript*, void*, SSCCmd*, ScriptPos*, const ScriptTokenList*, int, ScriptVars*,
                                 const char*, BOOL, BOOL);
BOOL __fastcall LoadFunctionRecord(CScript* self, void*, SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                                   int id, ScriptVars* locals, const char* signature, BOOL assignment, BOOL scriptCall) {
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
        for (int growBy : {cmd->floatArgs.growBy, cmd->stringArgs.growBy, cmd->objectArgs.growBy, cmd->groupArgs.growBy,
                           cmd->localBindings.growBy, cmd->globalBindings.growBy, cmd->subCalls.growBy})
            w.Put<std::int32_t>(growBy);
    }
    const ScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const BOOL result = s_loadFunction(self, nullptr, cmd, pos, tokens, id, locals, signature, assignment, scriptCall);
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
BOOL(__fastcall* s_loadCommands)(CScript*, void*, mfc::CArray<SSCCmd>*, ScriptPos*, const ScriptTokenList*, ScriptVars*);
BOOL __fastcall LoadCommandsRecord(CScript* self, void*, mfc::CArray<SSCCmd>* commands, ScriptPos* pos,
                                   const ScriptTokenList* tokens, ScriptVars* locals) {
    OfferFixtures();
    const bool empty = Empty(*commands);
    Writer w;
    if (empty) {
        PutState(w, self, true);
        PutError(w, self);
        PutLocals(w, locals);
        w.Put<std::int32_t>(commands->growBy);
    }
    const auto* lastBlock = self->blocks_.tail;
    const ScriptPos from = *pos;
    const std::uint16_t cw = ControlWord();
    const BOOL result = s_loadCommands(self, nullptr, commands, pos, tokens, locals);
    if (!empty) return result;   // never seen: the arrays are new
    PutRun(w, tokens, from, *pos);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    w.Put(HashOf([&](Writer& h) { PutCmds(h, *commands); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->blocks_, lastBlock); }));
    if (w.size() <= kMaxLoadPayload) g_loadCommands.OfferBytes(w, cw, (result ? 0 : 2) + (w.size() > 0x1000 ? 1 : 0));
    return result;
}

// 004D23A0: PUSH -1; PUSH imm32 (7). Bucket: succeeded or failed, by the text's size (SourceBucket).
BOOL(__fastcall* s_load)(CScript*, void*, mfc::CString*, BOOL);
BOOL __fastcall LoadRecord(CScript* self, void*, mfc::CString* text, BOOL runWorldScript) {
    OfferFixtures();
    Writer w;
    PutState(w, self, true);
    PutError(w, self);
    PutText(w, *text);
    crt::PutNegativeClasses(w, static_cast<const char*>(*text), static_cast<std::size_t>(text->GetLength()));
    w.Put<std::int32_t>(runWorldScript);
    const std::uint32_t length = static_cast<std::uint32_t>(text->GetLength());
    std::vector<int> skip;
    for (int i = 0; i < self->Scripts().size; ++i) skip.push_back(self->Scripts().data[i].ifThen.size);
    const auto* lastBlock = self->blocks_.tail;
    const auto* lastWorld = self->worldBlocks_.tail;
    const int runs = self->runs_.count;
    const std::uint16_t cw = ControlWord();
    const BOOL result = s_load(self, nullptr, text, runWorldScript);
    w.Put<std::int32_t>(result);
    PutError(w, self);
    w.Put<std::int32_t>(self->LoadedFlag());
    w.Put<std::int32_t>(self->FileCount());
    w.Put(HashOf([&](Writer& h) { PutText(h, *text); }));
    w.Put(HashOf([&](Writer& h) { PutDefinitionsAfter(h, self->Scripts(), skip); }));
    w.Put(HashOf([&](Writer& h) { PutVars(h, *self->Globals()); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->blocks_, lastBlock); }));
    w.Put(HashOf([&](Writer& h) { PutBlocksAfter(h, self->worldBlocks_, lastWorld); }));
    // RunWorldScript (004BCC60) adds a run of the block at the head of the run list.
    PutBlockRef(w, self->runs_.count > runs ? self->runs_.head->data.block : nullptr);
    if (w.size() <= kMaxLoad) g_load.OfferBytes(w, cw, (result ? 0 : 3) + SourceBucket(length));
    return result;
}

} // namespace

EI_RECORD_OURS(0x004CF4D0, ExpectRecord, 6, s_expect);
EI_RECORD_OURS(0x004CFBC0, LoadFunctionRecord, 7, s_loadFunction);
EI_RECORD_OURS(0x004D1330, LoadCommandsRecord, 7, s_loadCommands);
EI_RECORD_OURS(0x004D23A0, LoadRecord, 7, s_load);
