// Differential checks for the script loader (src/game/script_parse*.cpp): tokens, the lexer, keyword
// lookup, #define handling and the parser.
//
// The parser functions only write their output (an SSCCmd tree, an array of them, a token list) and
// the error state of the CScript (+0x1EC text, +0x1F0 code, +0x1F4 line); the token position is
// passed by pointer. So the original runs on a copy of the position into a scratch output, ours on
// the real one, and the outputs, positions, results and error states are compared. Functions that
// allocate command blocks in the CScript (LoadScriptBody, LoadCommands, Load) run only ours and are
// checked for consistency.
#include "verify.h"
#include "../../src/game/game_strings.h"

#include "../../src/game/script.h"
#include "../../src/game/script_parse.h"
#include "../../src/lib/crt.h"

#include <cstdarg>
#include <cstring>
#include <new>
#include <string>

namespace {
void* s_unused;

using namespace scriptparse;

// ---- Helpers ------------------------------------------------------------------------------------

struct ErrorState {
    std::string text;
    int code;
    int line;
    static ErrorState Take(CScript* script) {
        return {static_cast<const char*>(script->ErrorText()), script->ErrorCode(), script->ErrorLine()};
    }
    bool operator==(const ErrorState& o) const { return text == o.text && code == o.code && line == o.line; }
};

bool SameText(const mfc::CString& a, const mfc::CString& b) {
    return std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
}

template <typename T>
bool SameShape(const mfc::CArray<T>& a, const mfc::CArray<T>& b, bool capacity) {
    return a.size == b.size && a.growBy == b.growBy && (!capacity || a.maxSize == b.maxSize);
}

// Deep comparison of two call trees. Capacities are compared when both were built the same way.
bool SameCmd(const SSCCmd& a, const SSCCmd& b, bool capacity) {
    if (a.id != b.id || a.calledScript != b.calledScript) return false;
    if (!SameShape(a.floatArgs, b.floatArgs, capacity) || !SameShape(a.stringArgs, b.stringArgs, capacity) ||
        !SameShape(a.objectArgs, b.objectArgs, capacity) || !SameShape(a.groupArgs, b.groupArgs, capacity) ||
        !SameShape(a.localBindings, b.localBindings, capacity) || !SameShape(a.globalBindings, b.globalBindings, capacity) ||
        !SameShape(a.subCalls, b.subCalls, capacity))
        return false;
    if (a.floatArgs.size && std::memcmp(a.floatArgs.data, b.floatArgs.data, a.floatArgs.size * 4) != 0) return false;
    for (int i = 0; i < a.stringArgs.size; ++i)
        if (!SameText(a.stringArgs.data[i], b.stringArgs.data[i])) return false;
    for (int i = 0; i < a.objectArgs.size; ++i)
        if (a.objectArgs.data[i] != b.objectArgs.data[i]) return false;
    for (int i = 0; i < a.groupArgs.size; ++i)
        if (a.groupArgs.data[i] != b.groupArgs.data[i]) return false;
    if (a.localBindings.size &&
        std::memcmp(a.localBindings.data, b.localBindings.data, a.localBindings.size * sizeof(ScriptBinding)) != 0)
        return false;
    if (a.globalBindings.size &&
        std::memcmp(a.globalBindings.data, b.globalBindings.data, a.globalBindings.size * sizeof(ScriptBinding)) != 0)
        return false;
    for (int i = 0; i < a.subCalls.size; ++i) {
        const ScriptSubCall& x = a.subCalls.data[i];
        const ScriptSubCall& y = b.subCalls.data[i];
        if (x.type != y.type || x.argIndex != y.argIndex) return false;
        if (!x.cmd || !y.cmd) {
            if (x.cmd != y.cmd) return false;
        } else if (!SameCmd(*x.cmd, *y.cmd, capacity)) {
            return false;
        }
    }
    return true;
}

bool SameCmds(const mfc::CArray<SSCCmd>& a, const mfc::CArray<SSCCmd>& b) {
    if (!SameShape(a, b, true)) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SameCmd(a.data[i], b.data[i], true)) return false;
    return true;
}

bool EmptyCmd(const SSCCmd& c) {
    return !c.floatArgs.data && !c.stringArgs.data && !c.objectArgs.data && !c.groupArgs.data && !c.localBindings.data &&
           !c.globalBindings.data && !c.subCalls.data;
}

// SSCCmd constructor of the original code (ours in release; either way a fresh command).
struct CmdAt : SSCCmd {
    CmdAt* ConstructAt();   // 004D0A50
};
__declspec(naked) CmdAt* CmdAt::ConstructAt() EI_ORIGINAL(0x004D0A50)

// A fresh scratch SSCCmd with the same id and block pointer as `from`.
struct ScratchCmd {
    alignas(4) unsigned char storage[sizeof(SSCCmd)];
    explicit ScratchCmd(const SSCCmd& from) {
        CmdAt* c = reinterpret_cast<CmdAt*>(storage);
        c->ConstructAt();
        c->id = from.id;
        c->calledScript = from.calledScript;
    }
    ~ScratchCmd() { Get()->Destruct(); }
    SSCCmd* Get() { return reinterpret_cast<SSCCmd*>(storage); }
};

struct CmdArrayAt : mfc::CArray<SSCCmd> {
    void SetSize(int size, int growBy);   // 004CD2B0
};
__declspec(naked) void CmdArrayAt::SetSize(int, int) EI_ORIGINAL(0x004CD2B0)

struct TokenListAt : ScriptTokenList {
    void Destruct();   // 004CA260
};
__declspec(naked) void TokenListAt::Destruct() EI_ORIGINAL(0x004CA260)

struct VarsAt : ScriptVars {
    void Construct();   // 004C00D0
};
__declspec(naked) void VarsAt::Construct() EI_ORIGINAL(0x004C00D0)

struct DefinitionAt : ScriptDefinition {
    void Destruct();   // 004BE9D0
};
__declspec(naked) void DefinitionAt::Destruct() EI_ORIGINAL(0x004BE9D0)

bool SameNames(const mfc::CArray<mfc::CString>& a, const mfc::CArray<mfc::CString>& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SameText(a.data[i], b.data[i])) return false;
    return true;
}

bool SameVars(const ScriptVars& a, const ScriptVars& b) {
    return a.floats.size == b.floats.size && a.strings.size == b.strings.size && a.objects.size == b.objects.size &&
           a.groups.size == b.groups.size && SameNames(a.floatNames, b.floatNames) &&
           SameNames(a.stringNames, b.stringNames) && SameNames(a.objectNames, b.objectNames) &&
           SameNames(a.groupNames, b.groupNames);
}

bool EmptyVars(const ScriptVars& v) {
    return !v.floats.size && !v.strings.size && !v.objects.size && !v.groups.size && !v.floatNames.size &&
           !v.stringNames.size && !v.objectNames.size && !v.groupNames.size;
}

// The token before `pos` (the last one consumed) is ')'.
bool EndsWithClose(const ScriptTokenList* tokens, ScriptPos pos) {
    ScriptPos last = pos ? pos->prev : tokens->tail;
    if (!last) return false;
    int id = 0;
    ClassifyAt(last->data.text, &id, nullptr);
    return id == kLexClose;
}

} // namespace

// ---- Errors -------------------------------------------------------------------------------------

// 004CF470 (Rebirth patches it: vanilla only). Variadic, so ours only: the result is 0 and the
// error fields hold the arguments.
static int __cdecl ErrorVerify(CScript* self, int line, int code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = ScriptErrorV(self, line, code, format, args);
    va_end(args);
    bool ok = result == 0 && self->ErrorLine() == line && self->ErrorCode() == code;
    ei::verify::Check("CScript::Error", true, ok, "%d %d \"%s\"", line, code, static_cast<const char*>(self->ErrorText()));
    return result;
}
EI_VERIFY(0x004CF470, ErrorVerify, 0, s_unused);

// ---- Keyword lookup (Rebirth's AddNewScriptCommands patches these) --------------------------------

// 004CF600: SUB ESP,8; PUSH EBX; PUSH EBP (5 bytes). Pure apart from its outputs.
static void(__fastcall* s_classify)(const char*, int*, int*);
// Ours compares keywords with gamecrt::CaseFoldTable / SameIgnoringCase instead of the game CRT's
// _strnicmp (the shared helper, also used by object3d_figure_setup.cpp): on the first call every
// pair of bytes is compared both ways (ours against the original).
static void CheckCaseFold() {
    const int* fold = gamecrt::CaseFoldTable();
    int mismatches = 0;
    char a[2] = {}, b[2] = {};
    for (int x = 0; x < 256; ++x) {
        for (int y = 0; y < 256; ++y) {
            a[0] = static_cast<char>(x);
            b[0] = static_cast<char>(y);
            bool expected = gamecrt::strnicmp(a, b, 2) == 0;
            if (gamecrt::SameIgnoringCase(fold, a, b, 2) != expected && mismatches++ < 8)
                ei::verify::Check("ClassifyToken case fold", true, false, "%02X %02X: strnicmp %s", x, y, expected ? "equal" : "different");
        }
    }
    ei::verify::Check("ClassifyToken case fold", true, mismatches == 0, "%d of 65536 byte pairs differ", mismatches);
}

static void __fastcall ClassifyVerify(const char* text, int* id, int* rest) {
    static bool foldChecked = false;
    if (!foldChecked) {
        foldChecked = true;
        CheckCaseFold();
    }
    int id0 = 0x7777, rest0 = 0x7777, id1 = 0x7777, rest1 = 0x7777;
    s_classify(text, id ? &id0 : nullptr, rest ? &rest0 : nullptr);
    ClassifyToken(text, id ? &id1 : nullptr, rest ? &rest1 : nullptr);
    ei::verify::Check("ClassifyToken", true, id0 == id1 && rest0 == rest1, "\"%s\": %X/%d vs %X/%d", text, id0, rest0, id1, rest1);
    if (id) *id = id0;
    if (rest) *rest = rest0;
}
EI_VERIFY(0x004CF600, ClassifyVerify, 5, s_classify);

// The same string: Rebirth's AddNewScriptCommands table (the original's, in a Rebirth launch) and
// ours keep their own copies of the added names and signatures.
static bool SameText(const char* a, const char* b) { return a == b || (a && b && std::strcmp(a, b) == 0); }

// 004D0B10: MOV EAX,[0078BCA0] (5 bytes). Pure.
static const char*(__fastcall* s_signature)(int, int);
static const char* __fastcall SignatureVerify(int id, int type) {
    const char* expected = s_signature(id, type);
    const char* actual = FunctionSignature(id, type);
    ei::verify::Check("FunctionSignature", true, SameText(expected, actual), "id %X type %d: %s vs %s", id, type,
                      expected ? expected : "(null)", actual ? actual : "(null)");
    return expected;
}
EI_VERIFY(0x004D0B10, SignatureVerify, 5, s_signature);

// 004D0BA0: MOV EDX,[0078BCA0] (6 bytes). Pure.
static const char*(__fastcall* s_keywordName)(int);
static const char* __fastcall KeywordNameVerify(int id) {
    const char* expected = s_keywordName(id);
    const char* actual = KeywordName(id);
    ei::verify::Check("KeywordName", true, SameText(expected, actual), "id %X: %s vs %s", id, expected, actual);
    return expected;
}
EI_VERIFY(0x004D0BA0, KeywordNameVerify, 6, s_keywordName);

// ---- Tokens -------------------------------------------------------------------------------------

// 004CFB70: PUSH -1; PUSH imm32 (7 bytes). The original constructs a scratch token.
static ScriptToken*(__fastcall* s_tokenConstruct)(ScriptToken*);
static ScriptToken* __fastcall TokenConstructVerify(ScriptToken* self) {
    alignas(4) unsigned char scratch[sizeof(ScriptToken)];
    ScriptToken* expected = s_tokenConstruct(reinterpret_cast<ScriptToken*>(scratch));
    ScriptToken* result = self->Construct();
    bool ok = expected == reinterpret_cast<ScriptToken*>(scratch) && result == self && SameText(expected->text, self->text) &&
              expected->line == self->line;
    ei::verify::Check("ScriptToken::Construct", true, ok, "\"%s\" %d", static_cast<const char*>(self->text), self->line);
    expected->text.~CString();
    return result;
}
EI_VERIFY(0x004CFB70, TokenConstructVerify, 7, s_tokenConstruct);

// Assign and CopyConstruct change reference counts: run ours, check the copy.
static ScriptToken* __fastcall TokenAssignVerify(ScriptToken* self, void*, const ScriptToken* other) {
    ScriptToken* result = self->Assign(*other);
    bool ok = result == self && SameText(self->text, other->text) && self->line == other->line;
    ei::verify::Check("ScriptToken::Assign", true, ok, "\"%s\"", static_cast<const char*>(other->text));
    return result;
}
EI_VERIFY(0x004D0BD0, TokenAssignVerify, 0, s_unused);

static ScriptToken* __fastcall TokenCopyVerify(ScriptToken* self, void*, const ScriptToken* other) {
    ScriptToken* result = self->CopyConstruct(*other);
    bool ok = result == self && SameText(self->text, other->text) && self->line == other->line;
    ei::verify::Check("ScriptToken::CopyConstruct", true, ok, "\"%s\"", static_cast<const char*>(other->text));
    return result;
}
EI_VERIFY(0x004D1D30, TokenCopyVerify, 0, s_unused);

static void __stdcall ConstructTokensVerify(ScriptToken* tokens, int count) {
    ConstructTokens(tokens, count);
    bool ok = true;
    for (int i = 0; i < count; ++i)
        ok &= std::strcmp(tokens[i].text, game::Text(0x0078BC6C)) == 0 && tokens[i].line == -1;
    ei::verify::Check("ConstructTokens", true, ok, "%d", count);
}
EI_VERIFY(0x004CF380, ConstructTokensVerify, 0, s_unused);

// ---- Lexer --------------------------------------------------------------------------------------

// 004CF7A0: PUSH -1; PUSH imm32 (7 bytes). The original fills a scratch list; tokens, result and
// error state are compared.
static BOOL(__fastcall* s_tokenize)(CScript*, void*, const mfc::CString*, ScriptTokenList*);
static BOOL __fastcall TokenizeVerify(CScript* self, void*, const mfc::CString* text, ScriptTokenList* tokens) {
    ScriptTokenList scratch{tokens->vtable, nullptr, nullptr, 0, nullptr, nullptr, tokens->blockSize};
    BOOL expected = s_tokenize(self, nullptr, text, &scratch);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->Tokenize(*text, tokens);
    bool ok = expected == actual && error == ErrorState::Take(self) && scratch.count == tokens->count;
    int mismatch = -1, index = 0;
    for (auto *a = scratch.head, *b = tokens->head; ok && a && b; a = a->next, b = b->next, ++index)
        if (!SameText(a->data.text, b->data.text) || a->data.line != b->data.line) {
            ok = false;
            mismatch = index;
        }
    ei::verify::Check("CScript::Tokenize", true, ok, "%d vs %d, %d vs %d tokens, first difference %d", expected, actual,
                      scratch.count, tokens->count, mismatch);
    static_cast<TokenListAt&>(scratch).Destruct();
    return actual;
}
EI_VERIFY(0x004CF7A0, TokenizeVerify, 7, s_tokenize);

// 004CF4D0: MOV EAX,FS:[0] (6 bytes).
static BOOL(__fastcall* s_expect)(CScript*, void*, const ScriptTokenList*, ScriptPos*, int);
static BOOL __fastcall ExpectVerify(CScript* self, void*, const ScriptTokenList* tokens, ScriptPos* pos, int id) {
    ScriptPos copy = *pos;
    BOOL expected = s_expect(self, nullptr, tokens, &copy, id);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->ExpectLexeme(tokens, pos, id);
    bool ok = expected == actual && copy == *pos && error == ErrorState::Take(self);
    ei::verify::Check("CScript::ExpectLexeme", true, ok, "id %X: %d vs %d", id, expected, actual);
    return actual;
}
EI_VERIFY(0x004CF4D0, ExpectVerify, 6, s_expect);

// ---- Names --------------------------------------------------------------------------------------

// 004D37F0: PUSH -1; PUSH imm32 (7 bytes). Both construct a result string.
static mfc::CString*(__fastcall* s_qualify)(CScript*, void*, mfc::CString*, const mfc::CString*);
static mfc::CString* __fastcall QualifyVerify(CScript* self, void*, mfc::CString* out, const mfc::CString* name) {
    alignas(4) unsigned char scratch[sizeof(mfc::CString)];
    mfc::CString* expected = s_qualify(self, nullptr, reinterpret_cast<mfc::CString*>(scratch), name);
    mfc::CString* actual = self->QualifyName(out, *name);
    bool ok = expected == reinterpret_cast<mfc::CString*>(scratch) && actual == out && SameText(*expected, *actual);
    ei::verify::Check("CScript::QualifyName", true, ok, "\"%s\": \"%s\" vs \"%s\"", static_cast<const char*>(*name),
                      static_cast<const char*>(*expected), static_cast<const char*>(*actual));
    expected->~CString();
    return actual;
}
EI_VERIFY(0x004D37F0, QualifyVerify, 7, s_qualify);

// 004D3780: PUSH EBX; MOV EBX,[ESP+8] (5 bytes). Pure.
static int(__fastcall* s_findScript)(CScript*, void*, const char*);
static int __fastcall FindScriptVerify(CScript* self, void*, const char* name) {
    int expected = s_findScript(self, nullptr, name);
    ei::verify::Check("CScript::FindScript", expected, self->FindScript(name), "\"%s\"", name);
    return expected;
}
EI_VERIFY(0x004D3780, FindScriptVerify, 5, s_findScript);

// ---- #define ------------------------------------------------------------------------------------

// AddDefine inserts into a list: run ours, check the define is in the list once more than before.
static void __fastcall AddDefineVerify(ScriptDefineList* defines, const ScriptDefine* define) {
    int before = defines->count;
    AddDefine(defines, define);
    int found = 0, walked = 0;
    for (auto* n = defines->head; n; n = n->next, ++walked)
        if (SameText(n->data.name, define->name) && SameText(n->data.value, define->value)) ++found;
    bool ok = defines->count == before + 1 && walked == defines->count && found >= 1;
    ei::verify::Check("AddDefine", true, ok, "\"%s\" count %d -> %d", static_cast<const char*>(define->name), before,
                      defines->count);
}
EI_VERIFY(0x004D34F0, AddDefineVerify, 0, s_unused);

// 004D33E0: MOV EAX,FS:[0] (6 bytes). The original rewrites a copy of the line.
static void(__fastcall* s_applyDefines)(mfc::CString*, const ScriptDefineList*);
static void __fastcall ApplyDefinesVerify(mfc::CString* line, const ScriptDefineList* defines) {
    mfc::CString copy(*line);
    s_applyDefines(&copy, defines);
    ApplyDefines(line, defines);
    ei::verify::Check("ApplyDefines", true, SameText(copy, *line), "\"%s\" vs \"%s\"", static_cast<const char*>(copy),
                      static_cast<const char*>(*line));
}
EI_VERIFY(0x004D33E0, ApplyDefinesVerify, 6, s_applyDefines);

// ---- SSCCmd -------------------------------------------------------------------------------------

// 004D0A50: MOV EAX,ECX; MOV EDX,imm32 (7 bytes). Output only.
static SSCCmd*(__fastcall* s_cmdConstruct)(SSCCmd*);
static SSCCmd* __fastcall CmdConstructVerify(SSCCmd* self) {
    alignas(4) unsigned char scratch[sizeof(SSCCmd)];
    std::memcpy(scratch, self, sizeof(scratch));
    SSCCmd* expected = s_cmdConstruct(reinterpret_cast<SSCCmd*>(scratch));
    SSCCmd* actual = self->Construct();
    bool ok = expected == reinterpret_cast<SSCCmd*>(scratch) && actual == self && std::memcmp(scratch, self, sizeof(scratch)) == 0;
    ei::verify::Check("SSCCmd::Construct", true, ok, "%p", self);
    return actual;
}
EI_VERIFY(0x004D0A50, CmdConstructVerify, 7, s_cmdConstruct);

// ---- Parser -------------------------------------------------------------------------------------

// 004CFBC0 (Rebirth patches it: vanilla only): PUSH -1; PUSH imm32 (7 bytes). The command is always
// empty on entry (every caller clears it), so the original parses into a fresh scratch command.
static BOOL(__fastcall* s_loadFunction)(CScript*, void*, SSCCmd*, ScriptPos*, const ScriptTokenList*, int, ScriptVars*,
                                        const char*, BOOL, BOOL);
static BOOL __fastcall LoadFunctionVerify(CScript* self, void*, SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                                          int id, ScriptVars* locals, const char* signature, BOOL assignment, BOOL scriptCall) {
    if (!EmptyCmd(*cmd)) {
        // Never seen: every caller clears the command first. Run ours only.
        BOOL actual = self->LoadFunction(cmd, pos, tokens, id, locals, signature, assignment, scriptCall);
        ei::verify::Check("CScript::LoadFunction (unchecked)", true, actual == 0 || actual == 1, "%d", actual);
        return actual;
    }
    ScratchCmd scratch(*cmd);
    ScriptPos copy = *pos;
    BOOL expected = s_loadFunction(self, nullptr, scratch.Get(), &copy, tokens, id, locals, signature, assignment, scriptCall);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->LoadFunction(cmd, pos, tokens, id, locals, signature, assignment, scriptCall);
    bool ok = expected == actual && copy == *pos && error == ErrorState::Take(self) && SameCmd(*scratch.Get(), *cmd, true);
    ei::verify::Check("CScript::LoadFunction", true, ok, "id %X \"%s\": %d vs %d", id, signature, expected, actual);
    return actual;
}
EI_VERIFY(0x004CFBC0, LoadFunctionVerify, 7, s_loadFunction);

// 004D0BF0: PUSH -1; PUSH imm32 (7 bytes). The original fills a scratch array.
static BOOL(__fastcall* s_loadConditions)(CScript*, void*, mfc::CArray<SSCCmd>*, ScriptPos*, const ScriptTokenList*, ScriptVars*);
static BOOL __fastcall LoadConditionsVerify(CScript* self, void*, mfc::CArray<SSCCmd>* conditions, ScriptPos* pos,
                                            const ScriptTokenList* tokens, ScriptVars* locals) {
    CmdArrayAt scratch{};
    scratch.vtable = conditions->vtable;
    scratch.growBy = conditions->growBy;
    ScriptPos copy = *pos;
    BOOL expected = s_loadConditions(self, nullptr, &scratch, &copy, tokens, locals);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->LoadConditions(conditions, pos, tokens, locals);
    bool ok = expected == actual && copy == *pos && error == ErrorState::Take(self) && SameCmds(scratch, *conditions);
    ei::verify::Check("CScript::LoadConditions", true, ok, "%d vs %d, %d vs %d conditions", expected, actual, scratch.size,
                      conditions->size);
    scratch.SetSize(0, -1);
    return actual;
}
EI_VERIFY(0x004D0BF0, LoadConditionsVerify, 7, s_loadConditions);

// 004D1D50: PUSH -1; PUSH imm32 (7 bytes). The command is cleared first, so the original parses
// into a fresh scratch command.
static BOOL(__fastcall* s_loadAssign)(CScript*, void*, SSCCmd*, ScriptPos*, const ScriptTokenList*, ScriptVars*,
                                      const mfc::CString*);
static BOOL __fastcall LoadAssignVerify(CScript* self, void*, SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                                        ScriptVars* locals, const mfc::CString* name) {
    alignas(4) unsigned char storage[sizeof(SSCCmd)];
    SSCCmd* scratch = reinterpret_cast<CmdAt*>(storage)->ConstructAt();
    ScriptPos copy = *pos;
    BOOL expected = s_loadAssign(self, nullptr, scratch, &copy, tokens, locals, name);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->LoadAssignOperator(cmd, pos, tokens, locals, name);
    bool ok = expected == actual && copy == *pos && error == ErrorState::Take(self) && SameCmd(*scratch, *cmd, true);
    ei::verify::Check("CScript::LoadAssignOperator", true, ok, "\"%s\": %d vs %d", static_cast<const char*>(*name), expected,
                      actual);
    scratch->Destruct();
    return actual;
}
EI_VERIFY(0x004D1D50, LoadAssignVerify, 7, s_loadAssign);

// 004D1FE0: PUSH -1; PUSH imm32 (7 bytes). The definition is always empty here (Load clears it):
// the original then fills a fresh scratch definition with the same name. Otherwise only ours runs.
static BOOL(__fastcall* s_loadDeclarations)(CScript*, void*, ScriptDefinition*, ScriptPos*, const ScriptTokenList*, BOOL);
static BOOL __fastcall LoadDeclarationsVerify(CScript* self, void*, ScriptDefinition* script, ScriptPos* pos,
                                              const ScriptTokenList* tokens, BOOL qualify) {
    if (Length(script->params) != 0 || script->ifThen.size != 0 || !EmptyVars(script->vars)) {
        BOOL actual = self->LoadVariableDeclarations(script, pos, tokens, qualify);
        ei::verify::Check("CScript::LoadVariableDeclarations (unchecked)", true, actual == 0 || actual == 1, "%d", actual);
        return actual;
    }
    alignas(4) unsigned char storage[sizeof(ScriptDefinition)];
    ScriptDefinition* scratch = reinterpret_cast<ScriptDefinition*>(storage);
    new (&scratch->name) mfc::CString(script->name);
    new (&scratch->params) mfc::CString;
    scratch->ifThen = mfc::CArray<ScriptIfThen>{script->ifThen.vtable, nullptr, 0, 0, 0};
    static_cast<VarsAt*>(&scratch->vars)->Construct();
    scratch->index = script->index;

    ScriptPos copy = *pos;
    BOOL expected = s_loadDeclarations(self, nullptr, scratch, &copy, tokens, qualify);
    ErrorState error = ErrorState::Take(self);
    BOOL actual = self->LoadVariableDeclarations(script, pos, tokens, qualify);
    bool ok = expected == actual && copy == *pos && error == ErrorState::Take(self) && SameText(scratch->params, script->params) &&
              SameVars(scratch->vars, script->vars);
    ei::verify::Check("CScript::LoadVariableDeclarations", true, ok, "%d vs %d, \"%s\" vs \"%s\"", expected, actual,
                      static_cast<const char*>(scratch->params), static_cast<const char*>(script->params));
    static_cast<DefinitionAt*>(scratch)->Destruct();
    return actual;
}
EI_VERIFY(0x004D1FE0, LoadDeclarationsVerify, 7, s_loadDeclarations);

// LoadScriptBody and LoadCommands create command blocks in the CScript: ours only. On success the
// last token consumed is ')'; a script body has at least one if/then pair, all filled in.
static BOOL __fastcall LoadScriptBodyVerify(CScript* self, void*, ScriptDefinition* script, ScriptPos* pos,
                                            const ScriptTokenList* tokens) {
    int before = script->ifThen.size;
    BOOL actual = self->LoadScriptBody(script, pos, tokens);
    bool ok = true;
    if (actual == 1) {
        ok = script->ifThen.size > 0 && script->ifThen.size >= before && EndsWithClose(tokens, *pos);
        for (int i = 0; i < script->ifThen.size; ++i) ok &= script->ifThen.data[i].conditions && script->ifThen.data[i].commands;
    }
    ei::verify::Check("CScript::LoadScriptBody", true, ok, "%d, %d pairs", actual, script->ifThen.size);
    return actual;
}
EI_VERIFY(0x004D1030, LoadScriptBodyVerify, 0, s_unused);

static BOOL __fastcall LoadCommandsVerify(CScript* self, void*, mfc::CArray<SSCCmd>* commands, ScriptPos* pos,
                                          const ScriptTokenList* tokens, ScriptVars* locals) {
    BOOL actual = self->LoadCommands(commands, pos, tokens, locals);
    bool ok = actual != 1 || EndsWithClose(tokens, *pos);
    for (int i = 0; ok && actual == 1 && i < commands->size; ++i) ok = commands->data[i].id != 0;
    ei::verify::Check("CScript::LoadCommands", true, ok, "%d, %d commands", actual, commands->size);
    return actual;
}
EI_VERIFY(0x004D1330, LoadCommandsVerify, 0, s_unused);

// Load adds definitions and variables and may start the WorldScript: ours only. A successful load
// counts the file; a failed one does not. The WorldScript may load more files into the same script
// (nested loads), so the error state is checked only when exactly one file was counted.
// QFinish's wrapper (verify_script_exec.cpp) captures the text its load gets; with the skip flag
// set (the original QFinish's run) the load itself is skipped.
namespace scriptverify {
std::string* g_loadTextCapture = nullptr;
bool g_loadSkip = false;
} // namespace scriptverify

static BOOL __fastcall LoadVerify(CScript* self, void*, mfc::CString* text, BOOL runWorldScript) {
    if (scriptverify::g_loadTextCapture) {
        scriptverify::g_loadTextCapture->assign(static_cast<const char*>(*text));
        scriptverify::g_loadTextCapture = nullptr;
        if (scriptverify::g_loadSkip) return 0;
    }
    int files = self->FileCount();
    BOOL actual = self->Load(text, runWorldScript);
    bool ok = actual == 1 ? self->FileCount() > files && self->LoadedFlag() == 1 &&
                                (self->FileCount() != files + 1 || self->ErrorCode() == 0)
                          : self->FileCount() == files;
    ei::verify::Check("CScript::Load", true, ok, "%d, error %d \"%s\"", actual, self->ErrorCode(),
                      static_cast<const char*>(self->ErrorText()));
    return actual;
}
EI_VERIFY(0x004D23A0, LoadVerify, 0, s_unused);

static BOOL __fastcall LoadFileVerify(CScript* self, void*, const char* path) {
    int files = self->FileCount();
    BOOL actual = self->LoadFile(path);
    bool ok = actual == 1 ? self->FileCount() > files && (self->FileCount() != files + 1 || self->ErrorCode() == 0)
                          : self->FileCount() == files;
    ei::verify::Check("CScript::LoadFile", true, ok, "\"%s\": %d, error %d", path, actual, self->ErrorCode());
    return actual;
}
EI_VERIFY(0x004D36F0, LoadFileVerify, 0, s_unused);
