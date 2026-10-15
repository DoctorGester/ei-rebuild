// Script loader, part 2: the parser. CScript::Load preprocesses the text (#define), tokenizes it
// and parses the sections (WorldScript, Script, DeclareScript, GlobalVars); the Load* functions
// below build the SSCCmd trees. See script.h and docs/notes/script.md ("Loader and parser").
//
// Calls between loader functions go through the original addresses (Original below): Rebirth
// patches LoadFunction, LoadCommands and the keyword lookups (they install only in the vanilla
// game), and in verify builds each callee then runs through its own wrapper. LoadFunction calls
// itself directly (its verify wrapper would otherwise run the subtree twice at every level).
#include "script.h"
#include "game_strings.h"
#include "script_parse.h"
#include "script_addon.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "originals_script.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>

#pragma warning(disable : 4740) // inline assembly

using namespace scriptparse;

// ---- Forwarders ---------------------------------------------------------------------------------

#ifndef EI_VECTORS_REPLAY   // ei_vectors has its own (tests/vectors/replay_script.cpp)
__declspec(naked) int __cdecl CScript::Error(CScript*, int, int, const char*, ...) EI_ORIGINAL(0x004CF470)
#endif
__declspec(naked) mfc::CArray<SSCCmd>* CScript::NewWorldScript() EI_ORIGINAL(0x004BF900)
__declspec(naked) void CScript::RunWorldScript(mfc::CArray<SSCCmd>*) EI_ORIGINAL(0x004BCC60)
__declspec(naked) void SSCCmd::Clear() EI_ORIGINAL(0x004B5F70)
__declspec(naked) void SSCCmd::Destruct() EI_ORIGINAL(0x004BF640)
__declspec(naked) BOOL ScriptDefinition::AddParameter(const mfc::CString&, int) EI_ORIGINAL(0x004BB800)

namespace {

// The loader's own functions, called through their original addresses.
struct Original : CScript {
    BOOL TokenizeAt(const mfc::CString& text, ScriptTokenList* tokens);
    BOOL ExpectLexemeAt(const ScriptTokenList* tokens, ScriptPos* pos, int id);
    mfc::CString* QualifyNameAt(mfc::CString* out, const mfc::CString& name);
    int FindScriptAt(const char* name);
    BOOL LoadFunctionAt(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id, ScriptVars* locals,
                        const char* signature, BOOL assignment, BOOL scriptCall);
    BOOL LoadConditionsAt(mfc::CArray<SSCCmd>* conditions, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals);
    BOOL LoadScriptBodyAt(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens);
    BOOL LoadAssignOperatorAt(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals,
                              const mfc::CString* name);
    BOOL LoadVariableDeclarationsAt(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens, BOOL qualify);
    BOOL LoadCommandsAt(mfc::CArray<SSCCmd>* commands, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals);
    BOOL LoadAt(mfc::CString* text, BOOL runWorldScript);
};
__declspec(naked) BOOL Original::TokenizeAt(const mfc::CString&, ScriptTokenList*) EI_ORIGINAL(0x004CF7A0)
__declspec(naked) BOOL Original::ExpectLexemeAt(const ScriptTokenList*, ScriptPos*, int) EI_ORIGINAL(0x004CF4D0)
__declspec(naked) mfc::CString* Original::QualifyNameAt(mfc::CString*, const mfc::CString&) EI_ORIGINAL(0x004D37F0)
__declspec(naked) int Original::FindScriptAt(const char*) EI_ORIGINAL(0x004D3780)
__declspec(naked) BOOL Original::LoadFunctionAt(SSCCmd*, ScriptPos*, const ScriptTokenList*, int, ScriptVars*, const char*,
                                                BOOL, BOOL) EI_ORIGINAL(0x004CFBC0)
__declspec(naked) BOOL Original::LoadConditionsAt(mfc::CArray<SSCCmd>*, ScriptPos*, const ScriptTokenList*, ScriptVars*)
    EI_ORIGINAL(0x004D0BF0)
__declspec(naked) BOOL Original::LoadScriptBodyAt(ScriptDefinition*, ScriptPos*, const ScriptTokenList*) EI_ORIGINAL(0x004D1030)
__declspec(naked) BOOL Original::LoadAssignOperatorAt(SSCCmd*, ScriptPos*, const ScriptTokenList*, ScriptVars*,
                                                      const mfc::CString*) EI_ORIGINAL(0x004D1D50)
__declspec(naked) BOOL Original::LoadVariableDeclarationsAt(ScriptDefinition*, ScriptPos*, const ScriptTokenList*, BOOL)
    EI_ORIGINAL(0x004D1FE0)
__declspec(naked) BOOL Original::LoadCommandsAt(mfc::CArray<SSCCmd>*, ScriptPos*, const ScriptTokenList*, ScriptVars*)
    EI_ORIGINAL(0x004D1330)
BOOL Original::LoadAt(mfc::CString* a0, BOOL a1) { return (BOOL)ScriptLoadLine((void*)this, nullptr, (const mfc::CString*)a0, (int)a1); }
Original* Orig(CScript* script) { return static_cast<Original*>(script); }

// ScriptVars members through their original addresses (__thiscall): FindVariable (RET 0xC),
// HasVariable (RET 4), AppendFrom (RET 4: adds the other set's variables), ResetValues and
// Clear (removes every variable).
__declspec(naked) BOOL __fastcall VarsFindVariable(ScriptVars*, void*, const char*, int*, int*) EI_ORIGINAL(0x004B37D0)
__declspec(naked) BOOL __fastcall VarsHasVariable(ScriptVars*, void*, const char*) EI_ORIGINAL(0x004B36D0)
__declspec(naked) void __fastcall VarsAppendFrom(ScriptVars*, void*, const ScriptVars*) EI_ORIGINAL(0x004B3900)
__declspec(naked) void __fastcall VarsResetValues(ScriptVars*) EI_ORIGINAL(0x004B35E0)
__declspec(naked) void __fastcall VarsClear(ScriptVars*) EI_ORIGINAL(0x004C0020)

// CArray template members of the loader's arrays (MFC code, not rewritten; thiscall on the array).
// 004D5B50 bindings' Add; 004D5CB0 sub-calls' Add (deep-copies the nested SSCCmd) and 004C4FD0
// their SetSize (deletes the nested SSCCmds it drops); 004CD2B0 commands' SetSize, 004D65E0
// SetAtGrow and 004D5ED0 Add (both copy the SSCCmd); 004C59C0 if-then pairs' SetSize.
__declspec(naked) int __fastcall BindingsAdd(mfc::CArray<ScriptBinding>*, void*, const ScriptBinding*) EI_ORIGINAL(0x004D5B50)
__declspec(naked) int __fastcall SubCallsAdd(mfc::CArray<ScriptSubCall>*, void*, const ScriptSubCall*) EI_ORIGINAL(0x004D5CB0)
void SubCallsSetSize(mfc::CArray<ScriptSubCall>* calls, int size, int growBy) {
    SubCallArraySetSizeOriginal(calls, nullptr, size, growBy);
}
void CmdsSetSize(mfc::CArray<SSCCmd>* cmds, int size, int growBy) { CmdArraySetSizeOriginal(cmds, nullptr, size, growBy); }
__declspec(naked) void __fastcall CmdsSetAtGrow(mfc::CArray<SSCCmd>*, void*, int, const SSCCmd*) EI_ORIGINAL(0x004D65E0)
__declspec(naked) int __fastcall CmdsAdd(mfc::CArray<SSCCmd>*, void*, const SSCCmd*) EI_ORIGINAL(0x004D5ED0)
void IfThensSetSize(mfc::CArray<ScriptIfThen>* pairs, int size, int growBy) {
    IfThenArraySetSizeOriginal(pairs, nullptr, size, growBy);
}

// 004D3320 (__thiscall): empties a definition (name and parameters too).
__declspec(naked) void __fastcall DefinitionReset(ScriptDefinition*) EI_ORIGINAL(0x004D3320)

// 004D6100: the definitions' Add (copies the definition).
__declspec(naked) int __fastcall DefinitionsAdd(mfc::CArray<ScriptDefinition>*, void*, const ScriptDefinition*)
    EI_ORIGINAL(0x004D6100)

// 004D6340: the define list's destructor, through its address.
__declspec(naked) void __fastcall DefinesDestructAt(ScriptDefineList*) EI_ORIGINAL(0x004D6340)

// 004CA260 (__thiscall): the token list's destructor.
__declspec(naked) void __fastcall TokensDestruct(ScriptTokenList*) EI_ORIGINAL(0x004CA260)

// 006FE268 / 006FE296: ConstructElements / DestructElements for CString.
void __stdcall ConstructStrings(mfc::CString* a0, int a1) { CStringElementsConstruct((void*)a0, a1); }
void __stdcall DestructStrings(mfc::CString* a0, int a1) { CStringElementsDestruct((void*)a0, a1); }

// The #define helpers through their original addresses (script_parse.cpp).
__declspec(naked) void __fastcall AddDefineAt(ScriptDefineList*, const ScriptDefine*) EI_ORIGINAL(0x004D34F0)
__declspec(naked) void __fastcall ApplyDefinesAt(mfc::CString*, const ScriptDefineList*) EI_ORIGINAL(0x004D33E0)

// ---- Small helpers ------------------------------------------------------------------------------

// Index of `name` in a variable name array (004D5A50/5A90/5AD0/5B10, identical copies).
int FindName(const mfc::CArray<mfc::CString>& names, const char* name) {
    for (int i = 0; i < names.size; ++i)
        if (Mbscmp(names.data[i], name) == 0) return i;
    return -1;
}

// *dst = (float)atof(text): the CRT's result in ST0 stored with FSTP.
void StoreAtof(float* dst, const char* text) {
#ifdef EI_VECTORS_REPLAY   // no game CRT: its port, through the address
    *dst = static_cast<float>(GameAtof(text));
#else
    __asm {
        push text
        mov eax, 0x006E911A
        call eax
        add esp, 4
        mov eax, dst
        fstp dword ptr [eax]
    }
#endif
}

// FILD / FSTP of an int.
void StoreIntAsFloat(float* dst, int value) {
    __asm {
        fild value
        mov eax, dst
        fstp dword ptr [eax]
    }
}

void StoreBits(float* dst, std::uint32_t bits) { std::memcpy(dst, &bits, 4); }

int CountChar(const char* text, char c) {
    int n = 0;
    for (; *text; ++text)
        if (*text == c) ++n;
    return n;
}

// CArray::SetSize(n, -1) as the game inlines it into LoadFunction. Elements are constructed by
// zeroing (floats, object and group pointers) or with the CString helpers; only strings are
// destructed when the array shrinks.
template <typename T>
void InlineSetSize(mfc::CArray<T>& a, int n, bool strings) {
    auto construct = [strings](T* p, int count) {
        if (strings) ConstructStrings(reinterpret_cast<mfc::CString*>(p), count);
        else std::memset(p, 0, static_cast<unsigned>(count) * 4);
    };
    static_assert(sizeof(T) == 4);
    if (n == 0) {
        if (a.data) {
            if (strings) DestructStrings(reinterpret_cast<mfc::CString*>(a.data), a.size);
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
        }
        a.maxSize = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = static_cast<T*>(gamecrt::operator_new(static_cast<unsigned>(n) * 4));
        construct(a.data, n);
        a.maxSize = n;
        a.size = n;
    } else if (n <= a.maxSize) {
        if (n > a.size) construct(a.data + a.size, n - a.size);
        else if (strings && n < a.size) DestructStrings(reinterpret_cast<mfc::CString*>(a.data + n), a.size - n);
        a.size = n;
    } else {
        int grow = a.growBy;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int newMax = a.maxSize + grow;
        if (n >= newMax) newMax = n;
        T* data = static_cast<T*>(gamecrt::operator_new(static_cast<unsigned>(newMax) * 4));
        std::memcpy(data, a.data, static_cast<unsigned>(a.size) * 4);
        construct(data + a.size, n - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = n;
        a.maxSize = newMax;
    }
}

// Frees an array's buffer and empties it (the bindings arrays in LoadFunction).
template <typename T>
void FreeArray(mfc::CArray<T>& a) {
    if (a.data) {
        gamecrt::operator_delete(a.data);
        a.data = nullptr;
    }
    a.maxSize = 0;
    a.size = 0;
}

// Takes the token at *pos and advances (no end check, like the game's GetNext).
const ScriptToken& NextToken(ScriptPos* pos) {
    ScriptPos node = *pos;
    *pos = node->next;
    return node->data;
}

// A token with the game's initial value ("None", line -1), destroyed at scope exit.
struct LocalToken : ScriptToken {
    LocalToken() {
        text = game::Text(0x0078BC6C);
        line = -1;
    }
    void Set(const ScriptToken& from) {
        text = from.text;
        line = from.line;
    }
};

// Deletes the nested SSCCmd LoadFunction parses parameters into.
void DeleteCmd(SSCCmd* cmd) {
    if (cmd) {
        cmd->Destruct();
        gamecrt::operator_delete(cmd);
    }
}

// An SSCCmd on the stack (the loaders keep one and copy it into their result arrays).
struct LocalCmd {
    alignas(4) unsigned char storage[sizeof(SSCCmd)];
    LocalCmd() { Get()->Construct(); }
    ~LocalCmd() { Get()->Destruct(); }
    SSCCmd* Get() { return reinterpret_cast<SSCCmd*>(storage); }
};

// A ScriptVars on the stack.
struct LocalVars {
    alignas(4) unsigned char storage[sizeof(ScriptVars)];
    LocalVars() { VarsConstruct(Get()); }
    ~LocalVars() { VarsDestruct(Get()); }
    ScriptVars* Get() { return reinterpret_cast<ScriptVars*>(storage); }
};

// The ScriptDefinition Load keeps on its stack.
struct LocalDefinition {
    alignas(4) unsigned char storage[sizeof(ScriptDefinition)];
    LocalDefinition() {
        ScriptDefinition* d = Get();
        new (&d->name) mfc::CString;
        new (&d->params) mfc::CString;
        d->ifThen.vtable = reinterpret_cast<void*>(0x0073D430);   // 004D5A30
        d->ifThen.data = nullptr;
        d->ifThen.size = 0;
        d->ifThen.maxSize = 0;
        d->ifThen.growBy = 0;
        VarsConstruct(&d->vars);
        d->index = 0;
    }
    ~LocalDefinition() { DefinitionDestruct(Get()); }
    ScriptDefinition* Get() { return reinterpret_cast<ScriptDefinition*>(storage); }
};

// What DeclareScript and GlobalVars do to Load's definition before parsing (inline in the game).
void ClearDefinition(ScriptDefinition* decl) {
    IfThensSetSize(&decl->ifThen, 0, -1);
    VarsClear(&decl->vars);
    decl->index = 0;
    decl->params = game::EmptyText();
    decl->name = game::EmptyText();
}

const char* kTypeLetter[] = {nullptr, game::Text(0x0078BBD8), game::Text(0x0078BBD4), game::Text(0x0078BBD0), game::Text(0x0078BBCC)};

} // namespace

// ---- SSCCmd -------------------------------------------------------------------------------------

SSCCmd* SSCCmd::Construct() {
    auto init = [](auto& a, std::uintptr_t vtable) {
        a.data = nullptr;
        a.growBy = 0;
        a.maxSize = 0;
        a.size = 0;
        a.vtable = reinterpret_cast<void*>(vtable);
    };
    init(floatArgs, 0x0073D490);
    init(stringArgs, 0x0073D2D4);
    init(objectArgs, 0x0073D484);
    init(groupArgs, 0x0073D478);
    init(localBindings, 0x0073D49C);
    init(globalBindings, 0x0073D49C);
    init(subCalls, 0x0073D4A8);
    id = 0;
    calledScript = nullptr;
    return this;
}
EI_REPLACE_METHOD(0x004D0A50, SSCCmd::Construct);

// ---- LoadFunction -------------------------------------------------------------------------------

BOOL CScript::LoadFunction(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id, ScriptVars* locals,
                           const char* signature, BOOL assignment, BOOL scriptCall) {
    int paramIndex = 0;
    int floatCount = 0, stringCount = 0, objectCount = 0, groupCount = 0;
    LocalToken token;
    mfc::CString text, typeName, qualified;
    ScriptBinding local{0, 0, 0}, global{0, 0, 0};
    ScriptSubCall sub{0, 0, nullptr};

    if (void* memory = gamecrt::operator_new(sizeof(SSCCmd))) sub.cmd = static_cast<SSCCmd*>(memory)->Construct();
    // Extra leading slots: an assignment keeps the variable index and type in floats 0 and 1, a
    // script call the script index in float 0 and its name in string 0.
    if (assignment) floatCount = 2;
    if (scriptCall) {
        floatCount = 1;
        stringCount = 1;
    }

    cmd->id = id;
    InlineSetSize(cmd->floatArgs, CountChar(signature, 'f') + floatCount, false);
    InlineSetSize(cmd->stringArgs, CountChar(signature, 's') + stringCount, true);
    InlineSetSize(cmd->objectArgs, CountChar(signature, 'o') + CountChar(signature, 'O'), false);
    InlineSetSize(cmd->groupArgs, CountChar(signature, 'g'), false);
    FreeArray(cmd->localBindings);
    FreeArray(cmd->globalBindings);
    SubCallsSetSize(&cmd->subCalls, 0, -1);

    BOOL result;
    if (!*pos) {
        result = Error(this, tokens->tail->data.line, 4, game::Text(0x0078D838), KeywordName(kLexOpen));   // "Expected '%s'"
        DeleteCmd(sub.cmd);
        return result;
    }

    ScriptVars* globals = Globals();
    do {
        token.Set(NextToken(pos));
        text = token.text;
        int tokenId;
        ClassifyAt(text, &tokenId, nullptr);
        if (!assignment) {
            // AddNewScriptCommands (hook at 004D02CC): the signature's '?' and '+'.
            if (scriptaddon::Enabled()) scriptaddon::SignatureStep(cmd, &signature, tokenId);
            if (tokenId == kLexClose) {
                if (*signature) {
                    result = Error(this, token.line, 5, game::Text(0x0078D7DC));   // "Not enough parameters"
                    DeleteCmd(sub.cmd);
                    return result;
                }
                DeleteCmd(sub.cmd);
                return 1;
            }
            if (paramIndex == 0) {
                if (tokenId != kLexOpen) {
                    result = Error(this, token.line, 4, game::Text(0x0078D838), KeywordName(kLexOpen));
                    DeleteCmd(sub.cmd);
                    return result;
                }
            } else if (tokenId != kLexComma) {
                result = Error(this, token.line, 4, game::Text(0x0078D838), KeywordNameAt(kLexComma));
                DeleteCmd(sub.cmd);
                return result;
            }
            // The parameter (no end-of-tokens check, as in the game).
            token.Assign(NextToken(pos));
            text = token.text;
            ClassifyAt(text, &tokenId, nullptr);
            if (paramIndex == 0 && tokenId == kLexClose) {
                if (*signature == 0) {
                    DeleteCmd(sub.cmd);
                    return 1;
                }
                result = Error(this, token.line, 5, game::Text(0x0078D7DC));
                DeleteCmd(sub.cmd);
                return result;
            }
        }

        {
            mfc::CString name;
            Orig(this)->QualifyNameAt(reinterpret_cast<mfc::CString*>(&name), text);
            qualified = name;
        }

        // Literal parameters are stored directly; others bind a local or global variable of
        // the same name, or are parsed as a nested function call.
        bool bind = true;
        switch (*signature) {
        case 0:
            result = Error(this, token.line, 6, game::Text(0x0078D790));   // "Too much parameters"
            DeleteCmd(sub.cmd);
            return result;
        case 'f':
            local.type = kScriptFloat;
            global.type = kScriptFloat;
            local.varIndex = FindName(locals->floatNames, text);
            global.varIndex = FindName(globals->floatNames, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->floatNames, qualified);
            local.argIndex = floatCount;
            global.argIndex = floatCount;
            if (tokenId == kLexNumber) {
                StoreAtof(&cmd->floatArgs.data[floatCount], text);
                bind = false;
            }
            ++floatCount;
            typeName = game::Text(0x0078D830);   // "float"
            break;
        case 's':
            local.type = kScriptString;
            global.type = kScriptString;
            local.varIndex = FindName(locals->stringNames, text);
            global.varIndex = FindName(globals->stringNames, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->stringNames, qualified);
            local.argIndex = stringCount;
            global.argIndex = stringCount;
            if (tokenId == kLexString) {
                cmd->stringArgs.data[stringCount] = Ops(text).Mid(1, Length(text) - 2);
                bind = false;
            }
            ++stringCount;
            typeName = game::Text(0x0078D828);   // "string"
            break;
        case 'o':
            local.type = kScriptObject;
            global.type = kScriptObject;
            local.varIndex = FindName(locals->objectNames, text);
            global.varIndex = FindName(globals->objectNames, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->objectNames, qualified);
            local.argIndex = objectCount;
            global.argIndex = objectCount;
            ++objectCount;
            typeName = game::Text(0x00787D5C);   // "object"
            break;
        case 'O':
            // Loop counter of For/ForIf/Every/Any/Sum: must be a global object variable.
            local.varIndex = -1;
            global.type = kScriptObject;
            global.varIndex = FindName(globals->objectNames, text);
            if (global.varIndex < 0) {
                global.varIndex = FindName(globals->objectNames, qualified);
                if (global.varIndex < 0) {
                    result = Error(this, token.line, 8, game::Text(0x0078D7A4));   // "Only global variables ..."
                    DeleteCmd(sub.cmd);
                    return result;
                }
            }
            global.argIndex = objectCount;
            ++objectCount;
            typeName = game::Text(0x00787D5C);
            break;
        case 'g':
            local.type = kScriptGroup;
            global.type = kScriptGroup;
            local.varIndex = FindName(locals->groupNames, text);
            global.varIndex = FindName(globals->groupNames, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->groupNames, qualified);
            local.argIndex = groupCount;
            global.argIndex = groupCount;
            ++groupCount;
            typeName = game::Text(0x0078D820);   // "group"
            break;
        default:
            game::FatalError(game::Text(0x0078D7F4));   // "wrong type char in CScript::LoadFunction"
            break;
        }

        if (bind) {
            if (local.varIndex >= 0) {
                BindingsAdd(&cmd->localBindings, nullptr, &local);
            } else if (global.varIndex >= 0) {
                BindingsAdd(&cmd->globalBindings, nullptr, &global);
            } else {
                sub.type = 0;
                sub.argIndex = 0;
                sub.cmd->Clear();
                sub.argIndex = local.argIndex;
                sub.type = local.type;
                const char* nested = SignatureAt(tokenId, local.type);
                if (!nested) {
                    result = Error(this, token.line, 7, game::Text(0x0078D770), static_cast<const char*>(typeName));   // "Parameter of type %s expected"
                    DeleteCmd(sub.cmd);
                    return result;
                }
                if (!LoadFunction(sub.cmd, pos, tokens, tokenId, locals, nested, 0, 0)) {
                    DeleteCmd(sub.cmd);
                    return 0;
                }
                SubCallsAdd(&cmd->subCalls, nullptr, &sub);
            }
        }

        if (assignment) {
            DeleteCmd(sub.cmd);
            return 1;
        }
        ++signature;
        ++paramIndex;
    } while (*pos);

    result = Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));   // "Unexpected end of file"
    DeleteCmd(sub.cmd);
    return result;
}
EI_REPLACE_METHOD(0x004CFBC0, CScript::LoadFunction);
EI_REBIRTH_IMPLEMENTS(0x004CFBC0, ei::rebirth::Patch::AddNewScriptCommands);

// ---- Sections -----------------------------------------------------------------------------------

BOOL CScript::LoadConditions(mfc::CArray<SSCCmd>* conditions, ScriptPos* pos, const ScriptTokenList* tokens,
                             ScriptVars* locals) {
    LocalToken token;
    mfc::CString text;
    LocalCmd cmd;
    CmdsSetSize(conditions, 0, -1);
    for (;;) {
        if (!*pos) return Error(this, token.line, 1, game::Text(0x0078D740));
        cmd.Get()->Clear();
        token.Set(NextToken(pos));
        text = token.text;
        int id;
        ClassifyAt(text, &id, nullptr);
        if (id == kLexClose) return 1;
        const char* signature = SignatureAt(id, kScriptFloat);
        if (!signature) return Error(this, token.line, 9, game::Text(0x0078D858));   // "Function returning float expected"
        if (!Orig(this)->LoadFunctionAt(cmd.Get(), pos, tokens, id, locals, signature, 0, 0)) return 0;
        CmdsSetAtGrow(conditions, nullptr, conditions->size, cmd.Get());
    }
}
EI_REPLACE_METHOD(0x004D0BF0, CScript::LoadConditions);

BOOL CScript::LoadScriptBody(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens) {
    LocalToken token;
    mfc::CString text;
    ScriptIfThen pair{nullptr, nullptr};
    for (;;) {
        if (!*pos) return Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));
        int id;
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyAt(text, &id, nullptr);
        if (id == kLexClose) {
            if (script->ifThen.size == 0)   // "In script %s should be at least one condtion"
                return Error(this, token.line, 11, game::Text(0x0078D87C), static_cast<const char*>(script->name));
            return 1;
        }
        if (id != kLexIf) {
            Error(this, token.line, 10, game::Text(0x0078D8AC));   // "expected if section"
            return 0;
        }
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyAt(text, &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, game::Text(0x0078D8F0));   // "If section, ( expected"
            return 0;
        }
        pair.conditions = NewCommandList();
        if (!Orig(this)->LoadConditionsAt(&pair.conditions->commands, pos, tokens, &script->vars)) return 0;
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyAt(text, &id, nullptr);
        if (id != kLexThen) {
            Error(this, token.line, 4, game::Text(0x0078D8D8));   // "then expected after if"
            return 0;
        }
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyAt(text, &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, game::Text(0x0078D8C0));   // "Expected ( after then"
            return 0;
        }
        pair.commands = NewCommandList();
        if (!Orig(this)->LoadCommandsAt(&pair.commands->commands, pos, tokens, &script->vars)) return 0;
        const int n = script->ifThen.size;
        IfThensSetSize(&script->ifThen, n + 1, -1);
        script->ifThen.data[n] = pair;
    }
}
EI_REPLACE_METHOD(0x004D1030, CScript::LoadScriptBody);

BOOL CScript::LoadAssignOperator(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals,
                                 const mfc::CString* name) {
    LocalToken unused;   // constructed and destroyed by the game, never read
    mfc::CString unusedText;
    cmd->Clear();
    BOOL ok;
    if (!*pos) {
        ok = Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));
    } else {
        ScriptToken token;
        token.CopyConstruct(NextToken(pos));
        mfc::CString text(token.text);
        int id;
        ClassifyAt(text, &id, nullptr);
        ok = id == kLexAssign ? 1 : Error(this, token.line, 2, game::Text(0x0078D730), static_cast<const char*>(text));
    }
    if (!ok) return 0;

    int index, type;
    VarsFindVariable(Globals(), nullptr, *name, &index, &type);
    // The value is parsed as the single parameter of command 0x78; float 1 holds the type - 1.
    static const std::uint32_t kTypeCode[] = {0, 0x00000000, 0x3F800000, 0x40000000, 0x40400000};
    if (type < 1 || type > 4) {
        game::FatalError(game::Text(0x0078D920));   // "Uknown variable type at CScript::LoadAssignOperator"
        return 0;
    }
    BOOL result = Orig(this)->LoadFunctionAt(cmd, pos, tokens, kCmdAssign, locals, kTypeLetter[type], 1, 0);
    StoreBits(&cmd->floatArgs.data[1], kTypeCode[type]);
    StoreIntAsFloat(&cmd->floatArgs.data[0], index);
    return result;
}
EI_REPLACE_METHOD(0x004D1D50, CScript::LoadAssignOperator);

BOOL CScript::LoadVariableDeclarations(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens,
                                       BOOL qualify) {
    LocalToken token;
    mfc::CString name, typeName;
    BOOL result = 0;
    bool first = true;
    while (*pos) {
        token.Set(NextToken(pos));
        name = token.text;
        if (Mbscmp(name, game::Text(0x0078D71C)) == 0) {   // ")"
            result = 1;
            break;
        }
        if (!first) {
            if (Mbscmp(name, game::Text(0x00787B5C)) != 0)   // ","
                return Error(this, token.line, 4, game::Text(0x0078D988));   // "Expected , or ) after variable"
            token.Set(NextToken(pos));
            name = token.text;
        }
        BOOL ok;
        if (!*pos) {
            ok = Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));
        } else {
            ScriptToken colon;
            colon.CopyConstruct(NextToken(pos));
            mfc::CString text(colon.text);
            int id;
            ClassifyAt(text, &id, nullptr);
            ok = id == kLexColon ? 1 : Error(this, colon.line, 2, game::Text(0x0078D730), static_cast<const char*>(text));
        }
        if (!ok) return 0;
        if (qualify) {
            mfc::CString qualified;
            Orig(this)->QualifyNameAt(reinterpret_cast<mfc::CString*>(&qualified), name);
            name = qualified;
        }
        token.Set(NextToken(pos));
        typeName = token.text;
        int type;
        if (Mbscmp(typeName, game::Text(0x0078D830)) == 0) type = kScriptFloat;
        else if (Mbscmp(typeName, game::Text(0x0078D828)) == 0) type = kScriptString;
        else if (Mbscmp(typeName, game::Text(0x00787D5C)) == 0) type = kScriptObject;
        else if (Mbscmp(typeName, game::Text(0x0078D820)) == 0) type = kScriptGroup;
        else return Error(this, token.line, 12, game::Text(0x0078D954));   // "Unknown parameter type"
        if (!script->AddParameter(name, type))   // "Duplicate variable name %s"
            return Error(this, token.line, 13, game::Text(0x0078D96C), static_cast<const char*>(name));
        first = false;
    }
    return result;
}
EI_REPLACE_METHOD(0x004D1FE0, CScript::LoadVariableDeclarations);

BOOL CScript::LoadCommands(mfc::CArray<SSCCmd>* commands, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals) {
    LocalToken token;
    mfc::CString text, qualified;
    LocalCmd local;
    SSCCmd* cmd = local.Get();
    LocalVars empty;
    if (!locals) locals = empty.Get();
    CmdsSetSize(commands, 0, -1);
    for (;;) {
        if (!*pos) return Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));
        cmd->Clear();
        token.Set(NextToken(pos));
        text = token.text;
        int id;
        ClassifyAt(text, &id, nullptr);
        if (id == kLexClose) return 1;

        const ScriptKeyword* command = nullptr;
        for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
            if (k->id == id && k->command) {
                command = k;
                break;
            }

        BOOL ok;
        if (command) {
            ok = Orig(this)->LoadFunctionAt(cmd, pos, tokens, id, locals, command->command, 0, 0);
            if (id == kCmdFor || id == kCmdForIf) {
                // The loop body: `(` commands `)`, parsed without the script's local variables
                // (with them under Rebirth's AllowScriptArgsInLoops, which passes this call's
                // `locals`, the empty set when there were none). Parsed even when the loop header
                // failed.
                BOOL open;
                if (!*pos) {
                    open = Error(this, tokens->tail->data.line, 1, game::Text(0x0078D740));
                } else {
                    ScriptToken next;
                    next.CopyConstruct(NextToken(pos));
                    mfc::CString nextText(next.text);
                    int nextId;
                    ClassifyAt(nextText, &nextId, nullptr);
                    open = nextId == kLexOpen ? 1 : Error(this, next.line, 2, game::Text(0x0078D730), static_cast<const char*>(nextText));
                }
                if (!open) return 0;
                ScriptDecl* body = NewCommandList();
                cmd->calledScript = body;
                ScriptVars* bodyLocals =
                    ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptArgsInLoops) ? locals : nullptr;
                if (!Orig(this)->LoadCommandsAt(&body->commands, pos, tokens, bodyLocals)) return 0;
            }
        } else {
            {
                mfc::CString name;
                Orig(this)->QualifyNameAt(reinterpret_cast<mfc::CString*>(&name), text);
                qualified = name;
            }
            mfc::CArray<ScriptDefinition>& scripts = Scripts();
            int index = 0;
            for (; index < scripts.size; ++index)
                if (Mbscmp(scripts.data[index].name, text) == 0 || Mbscmp(scripts.data[index].name, qualified) == 0) break;
            if (index < scripts.size) {
                // A call to a declared script: command 8, float 0 = script index, string 0 = name.
                if (!Orig(this)->LoadFunctionAt(cmd, pos, tokens, kCmdCallScript, locals, scripts.data[index].params, 0, 1))
                    return 0;
                cmd->stringArgs.data[0] = Mbscmp(scripts.data[index].name, text) == 0 ? text : qualified;
                StoreIntAsFloat(&cmd->floatArgs.data[0], index);
                CmdsAdd(commands, nullptr, cmd);
                continue;
            }
            if (VarsHasVariable(Globals(), nullptr, text)) {
                ok = Orig(this)->LoadAssignOperatorAt(cmd, pos, tokens, locals, &text);
            } else if (VarsHasVariable(Globals(), nullptr, qualified)) {
                ok = Orig(this)->LoadAssignOperatorAt(cmd, pos, tokens, locals, &qualified);
            } else {
                return Error(this, token.line, 3, game::Text(0x0078D908), static_cast<const char*>(text));   // "Unexpected token '%s'"
            }
        }
        if (!ok) return 0;
        CmdsAdd(commands, nullptr, cmd);
    }
}
EI_REPLACE_METHOD(0x004D1330, CScript::LoadCommands);
// AddNewScriptCommands also patches this function: the keyword table (sites #19, #20) and a hook
// at 004D15D4 that would parse a body after commands whose record has flag 1 (no record has), so
// For and ForIf stay the only ones.
EI_REBIRTH_IMPLEMENTS(0x004D1330, ei::rebirth::Patch::AllowScriptArgsInLoops,
                      ei::rebirth::Patch::AddNewScriptCommands);

// ---- Load ---------------------------------------------------------------------------------------

BOOL CScript::Load(mfc::CString* text, BOOL runWorldScript) {
    ScriptTokenList tokens{reinterpret_cast<void*>(0x0073D424), nullptr, nullptr, 0, nullptr, nullptr, 10};
    LocalDefinition definition;
    ScriptDefinition* decl = definition.Get();
    mfc::CArray<SSCCmd>* worldScript = nullptr;

    ErrorLine() = 0;
    ErrorCode() = 0;
    ErrorText() = game::Text(0x0078DA0C);   // "No errors found"

    // Preprocess line by line: "#define name value" lines are collected (and become empty), the
    // other lines get the defines substituted. The text is replaced by the result.
    {
        ScriptDefineList defines{reinterpret_cast<void*>(0x0073D4C0), nullptr, nullptr, 0, nullptr, nullptr, 10};
        mfc::CString result, line;
        ScriptDefine define;
        const char* keyword = game::Text(0x0078DA04);   // "#define"
        char* p = Ops(*text).LockBuffer();
        while (*p) {
            char* start = p;
            while (*p && *p != '\n') ++p;
            if (*p) *p++ = 0;
            line = start;
            if (Strncmp(start, keyword, std::strlen(keyword)) == 0) {
                define.name = game::EmptyText();
                define.value = game::EmptyText();
                char* q = start + std::strlen(keyword);
                while (*q && IsSpace(*q)) ++q;
                char* name = q;
                while (*q && !IsSpace(*q)) ++q;
                if (*q) *q++ = 0;
                define.name = name;
                while (*q && IsSpace(*q)) ++q;
                define.value = q;
                AddDefineAt(&defines, &define);
                line = game::EmptyText();
            } else {
                ApplyDefinesAt(&line, &defines);
            }
            result = (result + line) + game::Text(0x00784340);   // "\n"
        }
        Ops(*text).UnlockBuffer();
        *text = result;
        DefinesDestructAt(&defines);
    }

    auto fail = [&tokens](BOOL value) {
        TokensDestruct(&tokens);
        return value;
    };
    if (!Orig(this)->TokenizeAt(*text, &tokens)) return fail(0);

    for (ScriptPos pos = tokens.head; pos;) {
        ScriptToken token;
        token.CopyConstruct(NextToken(&pos));
        mfc::CString word(token.text);
        int id;
        ClassifyAt(word, &id, nullptr);
        switch (id) {
        case kLexWorldScript: {
            BOOL ok;
            if (!pos) {
                ok = Error(this, tokens.tail->data.line, 1, game::Text(0x0078D740));
            } else {
                ScriptToken next;
                next.CopyConstruct(NextToken(&pos));
                mfc::CString nextText(next.text);
                int nextId;
                ClassifyAt(nextText, &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, game::Text(0x0078D730), static_cast<const char*>(nextText));
            }
            if (!ok) return fail(0);
            if (worldScript) return fail(Error(this, token.line, 14, game::Text(0x0078D9DC)));   // "Only one WorldScript() per file allowed"
            worldScript = NewWorldScript();
            if (!Orig(this)->LoadCommandsAt(worldScript, &pos, &tokens, nullptr)) return fail(0);
            break;
        }
        case kLexScript: {
            DefinitionReset(decl);
            token.Assign(NextToken(&pos));
            word = token.text;
            {
                mfc::CString name;
                Orig(this)->QualifyNameAt(reinterpret_cast<mfc::CString*>(&name), word);
                decl->name = name;
            }
            if (!Orig(this)->ExpectLexemeAt(&tokens, &pos, kLexOpen)) return fail(0);
            const int index = Orig(this)->FindScriptAt(decl->name);
            if (index < 0) {
                // Not declared before: parameters, then the body.
                if (!Orig(this)->LoadVariableDeclarationsAt(decl, &pos, &tokens, 0)) return fail(0);
                if (!Orig(this)->ExpectLexemeAt(&tokens, &pos, kLexOpen)) return fail(0);
                if (!Orig(this)->LoadScriptBodyAt(decl, &pos, &tokens)) return fail(0);
                DefinitionsAdd(&Scripts(), nullptr, decl);
            } else {
                ScriptDefinition* declared = &Scripts().data[index];
                if (declared->ifThen.size != 0)   // "Multiple declaration of script %s"
                    return fail(Error(this, token.line, 16, game::Text(0x0078D9B8), static_cast<const char*>(declared->name)));
                if (!Orig(this)->LoadScriptBodyAt(declared, &pos, &tokens)) return fail(0);
            }
            break;
        }
        case kLexDeclareScript: {
            ClearDefinition(decl);
            token.Assign(NextToken(&pos));
            word = token.text;
            {
                mfc::CString name;
                Orig(this)->QualifyNameAt(reinterpret_cast<mfc::CString*>(&name), word);
                decl->name = name;
            }
            if (Orig(this)->FindScriptAt(decl->name) != -1)   // "Duplicate script %s"
                return fail(Error(this, token.line, 15, game::Text(0x0078BC18), static_cast<const char*>(word)));
            if (!Orig(this)->ExpectLexemeAt(&tokens, &pos, kLexOpen)) return fail(0);
            if (!Orig(this)->LoadVariableDeclarationsAt(decl, &pos, &tokens, 0)) return fail(0);
            DefinitionsAdd(&Scripts(), nullptr, decl);
            break;
        }
        case kLexGlobalVars: {
            ClearDefinition(decl);
            BOOL ok;
            if (!pos) {
                ok = Error(this, tokens.tail->data.line, 1, game::Text(0x0078D740));
            } else {
                ScriptToken next;
                next.CopyConstruct(NextToken(&pos));
                mfc::CString nextText(next.text);
                int nextId;
                ClassifyAt(nextText, &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, game::Text(0x0078D730), static_cast<const char*>(nextText));
            }
            if (!ok) return fail(0);
            // Declared with qualified names, then added to the global variables.
            if (!Orig(this)->LoadVariableDeclarationsAt(decl, &pos, &tokens, 1)) return fail(0);
            VarsResetValues(&decl->vars);
            VarsAppendFrom(Globals(), nullptr, &decl->vars);
            break;
        }
        default:   // "Unexpected '%s'"
            return fail(Error(this, token.line, 3, game::Text(0x0078D9A8), static_cast<const char*>(word)));
        }
    }

    if (worldScript && runWorldScript) RunWorldScript(worldScript);
    LoadedFlag() = 1;
    ++FileCount();
    TokensDestruct(&tokens);
    return 1;
}
EI_REPLACE_METHOD(0x004D23A0, CScript::Load);

BOOL CScript::LoadFile(const char* path) {
    mfc::CString text;
    if (!ReadTextFile(path, &text)) return Error(this, 0, 17, game::Text(0x0078DA1C), path);   // "Unable to read file \"%s\""
    return Orig(this)->LoadAt(&text, 1);
}
EI_REPLACE_METHOD(0x004D36F0, CScript::LoadFile);
