// Script loader, part 2: the parser. CScript::Load preprocesses the text (#define), tokenizes it
// and parses the sections (WorldScript, Script, DeclareScript, GlobalVars); the Load* functions
// below build the SSCCmd trees. See script.h and docs/notes/script.md ("Loader and parser").
// Calls between loader functions go through the game's code (Original below): Rebirth
// patches LoadFunction, LoadCommands and the keyword lookups (they install only in the vanilla
// game), and in verify builds each callee then runs through its own wrapper. LoadFunction calls
// itself directly (its verify wrapper would otherwise run the subtree twice at every level).
#include "game_arrays.h"
#include "record_arrays.h"
#include "script.h"
#include "game_strings.h"
#include "script_parse.h"
#include "script_addon.h"

#include "ei/rebirth.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/mfc_string.h"
#include "lib/mfc_array.h"
#include "game_lists.h"
#include "game_data.h"
#include "game/game_slots.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>

using namespace scriptparse;

#ifndef EI_VECTORS_REPLAY   // ei_vectors has its own (tests/vectors/replay_script.cpp)
int __cdecl CScript::Error(CScript* script, int line, int code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = ScriptErrorV(script, line, code, format, args);
    va_end(args);
    return result;
}
#endif
void SSCCmd::Clear() { ScriptCmdClear(this); }
void SSCCmd::Destruct() { ScriptCmdDestruct(this); }

namespace {

// CArray template members of the loader's arrays (MFC code, not rewritten; thiscall on the array):
// the bindings' Add; the sub-calls' Add (deep-copies the nested SSCCmd) and
// their SetSize (deletes the nested SSCCmds it drops); the commands' SetSize,
// SetAtGrow and Add (both copy the SSCCmd); the if-then pairs' SetSize.
void IfThensSetSize(mfc::CArray<ScriptIfThen>* pairs, int size, int growBy) { pairs->SetSize(size, growBy); }

// The token list's destructor (gamelists::Tokens').
void TokensDestruct(ScriptTokenList* tokens) {
    gamelists::Tokens::Destruct<gameslots::TokenListClass>(mfc::impl::ListAs<gamelists::Tokens>(tokens));
}

// ---- Small helpers ------------------------------------------------------------------------------

// Index of `name` in a variable name array (four identical copies).
int FindName(const mfc::CArray<std::string>& names, const char* name) {
    for (int i = 0; i < names.size; ++i)
        if (Mbscmp(names.data[i].c_str(), name) == 0) return i;
    return -1;
}

// *dst = (float)atof(text): the CRT's result in ST0 stored with FSTP.
void StoreAtof(float* dst, const char* text) { *dst = static_cast<float>(std::atof(text)); }

// FILD / FSTP of an int.
void StoreIntAsFloat(float* dst, int value) { *dst = static_cast<float>(value); }

void StoreBits(float* dst, std::uint32_t bits) { std::memcpy(dst, &bits, 4); }

int CountChar(const char* text, char c) {
    int n = 0;
    for (; *text; ++text)
        if (*text == c) ++n;
    return n;
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
        text = "None";
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
    lib::InPlace<SSCCmd> storage;
    LocalCmd() { Get()->Construct(); }
    ~LocalCmd() { Get()->Destruct(); }
    SSCCmd* Get() { return storage.Get(); }
};

// A ScriptVars on the stack.
struct LocalVars {
    lib::InPlace<ScriptVars> storage;
    LocalVars() { Get()->ScriptVarsConstruct(); }
    ~LocalVars() { ScriptVarsDestruct(Get()); }
    ScriptVars* Get() { return storage.Get(); }
};

// The ScriptDefinition Load keeps on its stack.
struct LocalDefinition {
    lib::InPlace<ScriptDefinition> storage;
    LocalDefinition() {
        ScriptDefinition* d = Get();
        std::construct_at(&d->name);
        std::construct_at(&d->params);
        d->ifThen.vtable = mfc::impl::ScriptIfThenPairsClass::Vtable();
        d->ifThen.data = nullptr;
        d->ifThen.size = 0;
        d->ifThen.maxSize = 0;
        d->ifThen.growBy = 0;
        d->vars.ScriptVarsConstruct();
        d->index = 0;
    }
    ~LocalDefinition() { ScriptDefinitionDestruct(Get()); }
    ScriptDefinition* Get() { return storage.Get(); }
};

// What DeclareScript and GlobalVars do to Load's definition before parsing (inline in the game).
void ClearDefinition(ScriptDefinition* decl) {
    IfThensSetSize(&decl->ifThen, 0, -1);
    ScriptVarsClear(&decl->vars);
    decl->index = 0;
    decl->params = game::EmptyText();
    decl->name = game::EmptyText();
}

const char* kTypeLetter[] = {nullptr, "f", "s", "o", "g"};

} // namespace


SSCCmd* SSCCmd::Construct() {
    auto init = [](auto& a, void* vtable) {
        a.data = nullptr;
        a.growBy = 0;
        a.maxSize = 0;
        a.size = 0;
        a.vtable = vtable;
    };
    init(floatArgs, mfc::impl::ArgFloatArrayClass::Vtable());
    init(stringArgs, gamearrays::ScriptStringValuesClass::Vtable());
    init(objectArgs, mfc::impl::ArgObjectsClass::Vtable());
    init(groupArgs, mfc::impl::ArgGroupsClass::Vtable());
    init(localBindings, recordarrays::BindingArrayClass::Vtable());
    init(globalBindings, recordarrays::BindingArrayClass::Vtable());
    init(subCalls, recordarrays::SubCallArrayClass::Vtable());
    id = 0;
    calledScript = nullptr;
    return this;
}

BOOL CScript::LoadFunction(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id, ScriptVars* locals,
                           const char* signature, BOOL assignment, BOOL scriptCall) {
    int paramIndex = 0;
    int floatCount = 0, stringCount = 0, objectCount = 0, groupCount = 0;
    LocalToken token;
    mfc::CString text, typeName, qualified;
    ScriptBinding local{0, 0, 0}, global{0, 0, 0};
    ScriptSubCall sub{0, 0, nullptr};

    if (auto* memory = gamecrt::operator_new_as<SSCCmd>()) sub.cmd = memory->Construct();
    // Extra leading slots: an assignment keeps the variable index and type in floats 0 and 1, a
    // script call the script index in float 0 and its name in string 0.
    if (assignment) floatCount = 2;
    if (scriptCall) {
        floatCount = 1;
        stringCount = 1;
    }

    cmd->id = id;
    cmd->floatArgs.SetSize(CountChar(signature, 'f') + floatCount);
    cmd->stringArgs.SetSize(CountChar(signature, 's') + stringCount);
    cmd->objectArgs.SetSize(CountChar(signature, 'o') + CountChar(signature, 'O'));
    cmd->groupArgs.SetSize(CountChar(signature, 'g'));
    FreeArray(cmd->localBindings);
    FreeArray(cmd->globalBindings);
    mfc::impl::ArrayAs<recordarrays::SubCalls>(cmd->subCalls).SetSize(0, -1);

    BOOL result;
    if (!*pos) {
        result = Error(this, tokens->tail->data.line, 4, "Expected '%s'", KeywordName(kLexOpen));
        DeleteCmd(sub.cmd);
        return result;
    }

    ScriptVars* globals = Globals();
    do {
        token.Set(NextToken(pos));
        text = token.text;
        int tokenId;
        ClassifyToken(text, &tokenId, nullptr);
        if (!assignment) {
            // Rebirth's AddNewScriptCommands: the signature's '?' and '+'.
            if (scriptaddon::Enabled()) scriptaddon::SignatureStep(cmd, &signature, tokenId);
            if (tokenId == kLexClose) {
                if (*signature) {
                    result = Error(this, token.line, 5, "Not enough parameters");
                    DeleteCmd(sub.cmd);
                    return result;
                }
                DeleteCmd(sub.cmd);
                return 1;
            }
            if (paramIndex == 0) {
                if (tokenId != kLexOpen) {
                    result = Error(this, token.line, 4, "Expected '%s'", KeywordName(kLexOpen));
                    DeleteCmd(sub.cmd);
                    return result;
                }
            } else if (tokenId != kLexComma) {
                result = Error(this, token.line, 4, "Expected '%s'", KeywordName(kLexComma));
                DeleteCmd(sub.cmd);
                return result;
            }
            // The parameter (no end-of-tokens check, as in the game).
            token.Assign(NextToken(pos));
            text = token.text;
            ClassifyToken(text, &tokenId, nullptr);
            if (paramIndex == 0 && tokenId == kLexClose) {
                if (*signature == 0) {
                    DeleteCmd(sub.cmd);
                    return 1;
                }
                result = Error(this, token.line, 5, "Not enough parameters");
                DeleteCmd(sub.cmd);
                return result;
            }
        }

        {
            mfc::CString name(mfc::CString::kUnset);
            QualifyName(&name, text.GetString());
            qualified = name;
        }

        // Literal parameters are stored directly; others bind a local or global variable of
        // the same name, or are parsed as a nested function call.
        bool bind = true;
        switch (*signature) {
        case 0:
            result = Error(this, token.line, 6, "Too much parameters");
            DeleteCmd(sub.cmd);
            return result;
        case 'f':
            local.type = kScriptFloat;
            global.type = kScriptFloat;
            local.varIndex = FindName(locals->floats.names, text);
            global.varIndex = FindName(globals->floats.names, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->floats.names, qualified);
            local.argIndex = floatCount;
            global.argIndex = floatCount;
            if (tokenId == kLexNumber) {
                StoreAtof(&cmd->floatArgs.data[floatCount], text);
                bind = false;
            }
            ++floatCount;
            typeName = "float";
            break;
        case 's':
            local.type = kScriptString;
            global.type = kScriptString;
            local.varIndex = FindName(locals->strings.names, text);
            global.varIndex = FindName(globals->strings.names, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->strings.names, qualified);
            local.argIndex = stringCount;
            global.argIndex = stringCount;
            if (tokenId == kLexString) {
                cmd->stringArgs.data[stringCount] = text.Mid(1, text.GetLength() - 2);
                bind = false;
            }
            ++stringCount;
            typeName = "string";
            break;
        case 'o':
            local.type = kScriptObject;
            global.type = kScriptObject;
            local.varIndex = FindName(locals->objects.names, text);
            global.varIndex = FindName(globals->objects.names, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->objects.names, qualified);
            local.argIndex = objectCount;
            global.argIndex = objectCount;
            ++objectCount;
            typeName = "object";
            break;
        case 'O':
            // Loop counter of For/ForIf/Every/Any/Sum: must be a global object variable.
            local.varIndex = -1;
            global.type = kScriptObject;
            global.varIndex = FindName(globals->objects.names, text);
            if (global.varIndex < 0) {
                global.varIndex = FindName(globals->objects.names, qualified);
                if (global.varIndex < 0) {
                    result = Error(this, token.line, 8, "Only global variables are allowed to be loop counters");   // "Only global variables ..."
                    DeleteCmd(sub.cmd);
                    return result;
                }
            }
            global.argIndex = objectCount;
            ++objectCount;
            typeName = "object";
            break;
        case 'g':
            local.type = kScriptGroup;
            global.type = kScriptGroup;
            local.varIndex = FindName(locals->groups.names, text);
            global.varIndex = FindName(globals->groups.names, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->groups.names, qualified);
            local.argIndex = groupCount;
            global.argIndex = groupCount;
            ++groupCount;
            typeName = "group";
            break;
        default:
            game::FatalError("wrong type char in CScript::LoadFunction");
            break;
        }

        if (bind) {
            if (local.varIndex >= 0) {
                cmd->localBindings.Add(local);
            } else if (global.varIndex >= 0) {
                cmd->globalBindings.Add(global);
            } else {
                sub.type = 0;
                sub.argIndex = 0;
                sub.cmd->Clear();
                sub.argIndex = local.argIndex;
                sub.type = local.type;
                const char* nested = FunctionSignature(tokenId, local.type);
                if (!nested) {
                    result = Error(this, token.line, 7, "Parameter of type %s expected", typeName.GetString());
                    DeleteCmd(sub.cmd);
                    return result;
                }
                if (!LoadFunction(sub.cmd, pos, tokens, tokenId, locals, nested, 0, 0)) {
                    DeleteCmd(sub.cmd);
                    return 0;
                }
                cmd->subCalls.Add(sub);
            }
        }

        if (assignment) {
            DeleteCmd(sub.cmd);
            return 1;
        }
        ++signature;
        ++paramIndex;
    } while (*pos);

    result = Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
    DeleteCmd(sub.cmd);
    return result;
}

BOOL CScript::LoadConditions(ScriptCommands* conditions, ScriptPos* pos, const ScriptTokenList* tokens,
                             ScriptVars* locals) {
    LocalToken token;
    mfc::CString text;
    LocalCmd cmd;
    mfc::impl::ArrayAs<recordarrays::Cmds>(*conditions).SetSize(0, -1);
    for (;;) {
        if (!*pos) return Error(this, token.line, 1, "Unexpected end of file");
        cmd.Get()->Clear();
        token.Set(NextToken(pos));
        text = token.text;
        int id;
        ClassifyToken(text, &id, nullptr);
        if (id == kLexClose) return 1;
        const char* signature = FunctionSignature(id, kScriptFloat);
        if (!signature) return Error(this, token.line, 9, "Function returning float expected");
        if (!LoadFunction(cmd.Get(), pos, tokens, id, locals, signature, 0, 0)) return 0;
        conditions->SetAtGrow(conditions->size, *cmd.Get());
    }
}

BOOL CScript::LoadScriptBody(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens) {
    LocalToken token;
    mfc::CString text;
    ScriptIfThen pair{nullptr, nullptr};
    for (;;) {
        if (!*pos) return Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
        int id;
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyToken(text, &id, nullptr);
        if (id == kLexClose) {
            if (script->ifThen.size == 0)   // "In script %s should be at least one condtion"
                return Error(this, token.line, 11, "In script %s should be at least one condtion", script->name.c_str());
            return 1;
        }
        if (id != kLexIf) {
            Error(this, token.line, 10, "expected if section");
            return 0;
        }
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyToken(text, &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, "If section, ( expected");
            return 0;
        }
        pair.conditions = NewCommandList();
        if (!LoadConditions(&pair.conditions->commands, pos, tokens, &script->vars)) return 0;
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyToken(text, &id, nullptr);
        if (id != kLexThen) {
            Error(this, token.line, 4, "then expected after if");
            return 0;
        }
        token.Set(NextToken(pos));
        text = token.text;
        ClassifyToken(text, &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, "Expected ( after then");
            return 0;
        }
        pair.commands = NewCommandList();
        if (!LoadCommands(&pair.commands->commands, pos, tokens, &script->vars)) return 0;
        const int n = script->ifThen.size;
        IfThensSetSize(&script->ifThen, n + 1, -1);
        script->ifThen.data[n] = pair;
    }
}

BOOL CScript::LoadAssignOperator(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals,
                                 const mfc::CString* name) {
    LocalToken unused;   // constructed and destroyed by the game, never read
    std::string unusedText;
    cmd->Clear();
    BOOL ok;
    if (!*pos) {
        ok = Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
    } else {
        ScriptToken token;
        token.CopyConstruct(NextToken(pos));
        mfc::CString text(token.text);
        int id;
        ClassifyToken(text, &id, nullptr);
        ok = id == kLexAssign ? 1 : Error(this, token.line, 2, "Wrong lexeme %s", text.GetString());
    }
    if (!ok) return 0;

    int index, type;
    Globals()->FindVariable(*name, &index, &type);
    // The value is parsed as the single parameter of command 0x78; float 1 holds the type - 1.
    static const std::uint32_t kTypeCode[] = {0, 0x00000000, 0x3F800000, 0x40000000, 0x40400000};
    if (type < 1 || type > 4) {
        game::FatalError("Uknown variable type at CScript::LoadAssignOperator");
        return 0;
    }
    BOOL result = LoadFunction(cmd, pos, tokens, kCmdAssign, locals, kTypeLetter[type], 1, 0);
    StoreBits(&cmd->floatArgs.data[1], kTypeCode[type]);
    StoreIntAsFloat(&cmd->floatArgs.data[0], index);
    return result;
}

BOOL CScript::LoadVariableDeclarations(ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens,
                                       BOOL qualify) {
    LocalToken token;
    mfc::CString name, typeName;
    BOOL result = 0;
    bool first = true;
    while (*pos) {
        token.Set(NextToken(pos));
        name = token.text;
        if (Mbscmp(name, ")") == 0) {
            result = 1;
            break;
        }
        if (!first) {
            if (Mbscmp(name, ",") != 0)
                return Error(this, token.line, 4, "Expected , or ) after variable");
            token.Set(NextToken(pos));
            name = token.text;
        }
        BOOL ok;
        if (!*pos) {
            ok = Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
        } else {
            ScriptToken colon;
            colon.CopyConstruct(NextToken(pos));
            mfc::CString text(colon.text);
            int id;
            ClassifyToken(text, &id, nullptr);
            ok = id == kLexColon ? 1 : Error(this, colon.line, 2, "Wrong lexeme %s", text.GetString());
        }
        if (!ok) return 0;
        if (qualify) {
            mfc::CString qualified(mfc::CString::kUnset);
            QualifyName(&qualified, name.GetString());
            name = qualified;
        }
        token.Set(NextToken(pos));
        typeName = token.text;
        int type;
        if (Mbscmp(typeName, "float") == 0) type = kScriptFloat;
        else if (Mbscmp(typeName, "string") == 0) type = kScriptString;
        else if (Mbscmp(typeName, "object") == 0) type = kScriptObject;
        else if (Mbscmp(typeName, "group") == 0) type = kScriptGroup;
        else return Error(this, token.line, 12, "Unknown parameter type");
        if (!script->AddParameter(&name, type))   // "Duplicate variable name %s"
            return Error(this, token.line, 13, "Duplicate variable name %s", name.GetString());
        first = false;
    }
    return result;
}

BOOL CScript::LoadCommands(ScriptCommands* commands, ScriptPos* pos, const ScriptTokenList* tokens, ScriptVars* locals) {
    LocalToken token;
    mfc::CString text, qualified;
    LocalCmd local;
    SSCCmd* cmd = local.Get();
    LocalVars empty;
    if (!locals) locals = empty.Get();
    mfc::impl::ArrayAs<recordarrays::Cmds>(*commands).SetSize(0, -1);
    for (;;) {
        if (!*pos) return Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
        cmd->Clear();
        token.Set(NextToken(pos));
        text = token.text;
        int id;
        ClassifyToken(text, &id, nullptr);
        if (id == kLexClose) return 1;

        const ScriptKeyword* command = nullptr;
        for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
            if (k->id == id && k->command) {
                command = k;
                break;
            }

        BOOL ok;
        if (command) {
            ok = LoadFunction(cmd, pos, tokens, id, locals, command->command, 0, 0);
            if (id == kCmdFor || id == kCmdForIf) {
                // The loop body: `(` commands `)`, parsed without the script's local variables
                // (with them under Rebirth's AllowScriptArgsInLoops, which passes this call's
                // `locals`, the empty set when there were none). Parsed even when the loop header
                // failed.
                BOOL open;
                if (!*pos) {
                    open = Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
                } else {
                    ScriptToken next;
                    next.CopyConstruct(NextToken(pos));
                    std::string nextText = next.text;
                    int nextId;
                    ClassifyToken(nextText.c_str(), &nextId, nullptr);
                    open = nextId == kLexOpen ? 1 : Error(this, next.line, 2, "Wrong lexeme %s", nextText.c_str());
                }
                if (!open) return 0;
                ScriptDecl* body = NewCommandList();
                cmd->calledScript = body;
                ScriptVars* bodyLocals =
                    ei::rebirth::Enabled(ei::rebirth::Patch::AllowScriptArgsInLoops) ? locals : nullptr;
                if (!LoadCommands(&body->commands, pos, tokens, bodyLocals)) return 0;
            }
        } else {
            {
                mfc::CString name(mfc::CString::kUnset);
                QualifyName(&name, text.GetString());
                qualified = name;
            }
            mfc::CArray<ScriptDefinition>& scripts = Scripts();
            int index = 0;
            for (; index < scripts.size; ++index)
                if (Mbscmp(scripts.data[index].name, text) == 0 || Mbscmp(scripts.data[index].name, qualified) == 0) break;
            if (index < scripts.size) {
                // A call to a declared script: command 8, float 0 = script index, string 0 = name.
                if (!LoadFunction(cmd, pos, tokens, kCmdCallScript, locals, scripts.data[index].params.c_str(), 0, 1))
                    return 0;
                cmd->stringArgs.data[0] = Mbscmp(scripts.data[index].name, text) == 0 ? text : qualified;
                StoreIntAsFloat(&cmd->floatArgs.data[0], index);
                commands->Add(*cmd);
                continue;
            }
            if (Globals()->HasVariable(text)) {
                ok = LoadAssignOperator(cmd, pos, tokens, locals, &text);
            } else if (Globals()->HasVariable(qualified)) {
                ok = LoadAssignOperator(cmd, pos, tokens, locals, &qualified);
            } else {
                return Error(this, token.line, 3, "Unexpected token '%s'", text.GetString());
            }
        }
        if (!ok) return 0;
        commands->Add(*cmd);
    }
}
// Rebirth's AddNewScriptCommands also patches this function: the keyword table and a hook
// that would parse a body after commands whose record has flag 1 (no record has), so
// For and ForIf stay the only ones.


BOOL CScript::Load(mfc::CString* text, BOOL runWorldScript) {
    ScriptTokenList tokens{gameslots::TokenListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    LocalDefinition definition;
    ScriptDefinition* decl = definition.Get();
    ScriptDecl* worldScript = nullptr;

    ErrorLine() = 0;
    ErrorCode() = 0;
    ErrorText() = "No errors found";

    // Preprocess line by line: "#define name value" lines are collected (and become empty), the
    // other lines get the defines substituted. The text is replaced by the result.
    {
        ScriptDefineList defines{gameslots::DefineListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
        mfc::CString result, line;
        ScriptDefine define;
        const char* keyword = "#define";
        char* p = mfc::ImplOf(*text).LockBuffer();
        while (*p) {
            char* start = p;
            while (*p && *p != '\n') ++p;
            if (*p) *p++ = 0;
            line = start;
            if (gamecrt::strncmp(start, keyword, std::strlen(keyword)) == 0) {
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
                AddDefine(&defines, &define);
                line = game::EmptyText();
            } else {
                ApplyDefines(&line, &defines);
            }
            result = (result + line) + "\n";   // "\n"
        }
        mfc::ImplOf(*text).UnlockBuffer();
        *text = result;
        ScriptDefinesDestruct(&defines);
    }

    auto fail = [&tokens](BOOL value) {
        TokensDestruct(&tokens);
        return value;
    };
    if (!Tokenize((*text).GetString(), &tokens)) return fail(0);

    for (ScriptPos pos = tokens.head; pos;) {
        ScriptToken token;
        token.CopyConstruct(NextToken(&pos));
        std::string word = token.text;
        int id;
        ClassifyToken(word.c_str(), &id, nullptr);
        switch (id) {
        case kLexWorldScript: {
            BOOL ok;
            if (!pos) {
                ok = Error(this, tokens.tail->data.line, 1, "Unexpected end of file");
            } else {
                ScriptToken next;
                next.CopyConstruct(NextToken(&pos));
                std::string nextText = next.text;
                int nextId;
                ClassifyToken(nextText.c_str(), &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, "Wrong lexeme %s", nextText.c_str());
            }
            if (!ok) return fail(0);
            if (worldScript) return fail(Error(this, token.line, 14, "Only one WorldScript() per file allowed"));
            worldScript = NewWorldScript();
            if (!LoadCommands(&worldScript->commands, &pos, &tokens, nullptr)) return fail(0);
            break;
        }
        case kLexScript: {
            ScriptDefinitionReset(decl);
            token.Assign(NextToken(&pos));
            word = token.text;
            {
                mfc::CString name(mfc::CString::kUnset);
                QualifyName(&name, word);
                decl->name = name;
            }
            if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return fail(0);
            const int index = FindScript(decl->name.c_str());
            if (index < 0) {
                // Not declared before: parameters, then the body.
                if (!LoadVariableDeclarations(decl, &pos, &tokens, 0)) return fail(0);
                if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return fail(0);
                if (!LoadScriptBody(decl, &pos, &tokens)) return fail(0);
                Scripts().Add(*decl);
            } else {
                ScriptDefinition* declared = &Scripts().data[index];
                if (declared->ifThen.size != 0)   // "Multiple declaration of script %s"
                    return fail(Error(this, token.line, 16, "Multiple declaration of script %s", declared->name.c_str()));
                if (!LoadScriptBody(declared, &pos, &tokens)) return fail(0);
            }
            break;
        }
        case kLexDeclareScript: {
            ClearDefinition(decl);
            token.Assign(NextToken(&pos));
            word = token.text;
            {
                mfc::CString name(mfc::CString::kUnset);
                QualifyName(&name, word);
                decl->name = name;
            }
            if (FindScript(decl->name.c_str()) != -1)   // "Duplicate script %s"
                return fail(Error(this, token.line, 15, "Duplicate script %s", word.c_str()));
            if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return fail(0);
            if (!LoadVariableDeclarations(decl, &pos, &tokens, 0)) return fail(0);
            Scripts().Add(*decl);
            break;
        }
        case kLexGlobalVars: {
            ClearDefinition(decl);
            BOOL ok;
            if (!pos) {
                ok = Error(this, tokens.tail->data.line, 1, "Unexpected end of file");
            } else {
                ScriptToken next;
                next.CopyConstruct(NextToken(&pos));
                std::string nextText = next.text;
                int nextId;
                ClassifyToken(nextText.c_str(), &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, "Wrong lexeme %s", nextText.c_str());
            }
            if (!ok) return fail(0);
            // Declared with qualified names, then added to the global variables.
            if (!LoadVariableDeclarations(decl, &pos, &tokens, 1)) return fail(0);
            decl->vars.ResetValues();
            Globals()->AppendFrom(&decl->vars);
            break;
        }
        default:   // "Unexpected '%s'"
            return fail(Error(this, token.line, 3, "Unexpected '%s'", word.c_str()));
        }
    }

    if (worldScript && runWorldScript) RunWorldScript(worldScript);
    LoadedFlag() = 1;
    ++FileCount();
    TokensDestruct(&tokens);
    return 1;
}

BOOL CScript::LoadFile(const char* path) {
    mfc::CString text;
    if (!ReadTextFile(path, &text)) return Error(this, 0, 17, "Unable to read file \"%s\"", path);   // "Unable to read file \"%s\""
    return Load(&text, 1);
}
