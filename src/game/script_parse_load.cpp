// Script loader, part 2: the parser. CScript::Load preprocesses the text (#define), tokenizes it
// and parses the sections (WorldScript, Script, DeclareScript, GlobalVars); the Load* functions
// below build the SSCCmd trees. See script.h and docs/notes/script.md ("Loader and parser").
// Calls between loader functions go through the game's code (Original below): Rebirth
// patches LoadFunction, LoadCommands and the keyword lookups (they install only in the vanilla
// game), and in verify builds each callee then runs through its own wrapper. LoadFunction calls
// itself directly (its verify wrapper would otherwise run the subtree twice at every level).
#include "script.h"
#include "game_strings.h"
#include "script_parse.h"
#include "script_addon.h"

#include "ei/rebirth.h"
#include "game_data.h"
#include "lib/str.h"

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
int CScript::Error(CScript* script, int line, int code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = ScriptErrorV(script, line, code, format, args);
    va_end(args);
    return result;
}
#endif
namespace {

// ---- Small helpers ------------------------------------------------------------------------------

// Index of `name` in a variable name array (four identical copies).
int FindName(const std::vector<std::string>& names, std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (Mbscmp(names[i], name) == 0) return static_cast<int>(i);
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

// Takes the token at *pos and advances (no end check, like the game's GetNext).
const ScriptToken& NextToken(std::list<ScriptToken>::const_iterator* pos) { return *(*pos)++; }

// The last token's line ("Unexpected end of file" reports it).
int LastLine(const std::list<ScriptToken>* tokens) { return tokens->back().line; }

const char* kTypeLetter[] = {nullptr, "f", "s", "o", "g"};

} // namespace


BOOL CScript::LoadFunction(SSCCmd* cmd, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens, int id, ScriptVars* locals,
                           const char* signature, BOOL assignment, BOOL scriptCall) {
    int paramIndex = 0;
    int floatCount = 0, stringCount = 0, objectCount = 0, groupCount = 0;
    ScriptToken token;
    std::string text, typeName, qualified;
    ScriptBinding local{0, 0, 0}, global{0, 0, 0};
    ScriptSubCall sub;
    // Extra leading slots: an assignment keeps the variable index and type in floats 0 and 1, a
    // script call the script index in float 0 and its name in string 0.
    if (assignment) floatCount = 2;
    if (scriptCall) {
        floatCount = 1;
        stringCount = 1;
    }

    cmd->id = id;
    cmd->floatArgs.resize(static_cast<std::size_t>(CountChar(signature, 'f') + floatCount));
    cmd->stringArgs.resize(static_cast<std::size_t>(CountChar(signature, 's') + stringCount));
    cmd->objectArgs.resize(static_cast<std::size_t>(CountChar(signature, 'o') + CountChar(signature, 'O')));
    cmd->groupArgs.resize(static_cast<std::size_t>(CountChar(signature, 'g')));
    cmd->localBindings.clear();
    cmd->globalBindings.clear();
    cmd->subCalls.clear();

    if (*pos == tokens->end()) return Error(this, LastLine(tokens), 4, "Expected '%s'", KeywordName(kLexOpen));

    ScriptVars* globals = &globals_;
    do {
        token = NextToken(pos);
        text = token.text;
        int tokenId;
        ClassifyToken(text.c_str(), &tokenId, nullptr);
        if (!assignment) {
            // Rebirth's AddNewScriptCommands: the signature's '?' and '+'.
            if (scriptaddon::Enabled()) scriptaddon::SignatureStep(cmd, &signature, tokenId);
            if (tokenId == kLexClose) {
                if (*signature) return Error(this, token.line, 5, "Not enough parameters");
                return 1;
            }
            if (paramIndex == 0) {
                if (tokenId != kLexOpen) return Error(this, token.line, 4, "Expected '%s'", KeywordName(kLexOpen));
            } else if (tokenId != kLexComma) {
                return Error(this, token.line, 4, "Expected '%s'", KeywordName(kLexComma));
            }
            // The parameter (no end-of-tokens check, as in the game).
            token = NextToken(pos);
            text = token.text;
            ClassifyToken(text.c_str(), &tokenId, nullptr);
            if (paramIndex == 0 && tokenId == kLexClose) {
                if (*signature == 0) return 1;
                return Error(this, token.line, 5, "Not enough parameters");
            }
        }

        {
            std::string name;
            QualifyName(&name, text);
            qualified = name;
        }

        // Literal parameters are stored directly; others bind a local or global variable of
        // the same name, or are parsed as a nested function call.
        bool bind = true;
        switch (*signature) {
        case 0:
            return Error(this, token.line, 6, "Too much parameters");
        case 'f':
            local.type = kScriptFloat;
            global.type = kScriptFloat;
            local.varIndex = FindName(locals->floats.names, text);
            global.varIndex = FindName(globals->floats.names, text);
            if (global.varIndex < 0) global.varIndex = FindName(globals->floats.names, qualified);
            local.argIndex = floatCount;
            global.argIndex = floatCount;
            if (tokenId == kLexNumber) {
                StoreAtof(&cmd->floatArgs[floatCount], text.c_str());
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
                cmd->stringArgs[stringCount] = str::Mid(text, 1, static_cast<int>(text.size()) - 2);
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
                if (global.varIndex < 0)
                    return Error(this, token.line, 8, "Only global variables are allowed to be loop counters");
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
                cmd->localBindings.push_back(local);
            } else if (global.varIndex >= 0) {
                cmd->globalBindings.push_back(global);
            } else {
                sub.cmd->Clear();
                sub.argIndex = local.argIndex;
                sub.type = local.type;
                const char* nested = FunctionSignature(tokenId, local.type);
                if (!nested) return Error(this, token.line, 7, "Parameter of type %s expected", typeName.c_str());
                if (!LoadFunction(sub.cmd.get(), pos, tokens, tokenId, locals, nested, 0, 0)) return 0;
                cmd->subCalls.push_back(sub);
            }
        }

        if (assignment) return 1;
        ++signature;
        ++paramIndex;
    } while (*pos != tokens->end());

    return Error(this, LastLine(tokens), 1, "Unexpected end of file");
}

BOOL CScript::LoadConditions(std::vector<SSCCmd>* conditions, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens,
                             ScriptVars* locals) {
    ScriptToken token;
    std::string text;
    SSCCmd cmd;
    conditions->clear();
    for (;;) {
        if (*pos == tokens->end()) return Error(this, token.line, 1, "Unexpected end of file");
        cmd.Clear();
        token = NextToken(pos);
        text = token.text;
        int id;
        ClassifyToken(text.c_str(), &id, nullptr);
        if (id == kLexClose) return 1;
        const char* signature = FunctionSignature(id, kScriptFloat);
        if (!signature) return Error(this, token.line, 9, "Function returning float expected");
        if (!LoadFunction(&cmd, pos, tokens, id, locals, signature, 0, 0)) return 0;
        conditions->push_back(cmd);
    }
}

BOOL CScript::LoadScriptBody(ScriptDefinition* script, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens) {
    ScriptToken token;
    std::string text;
    ScriptIfThen pair;
    for (;;) {
        if (*pos == tokens->end()) return Error(this, LastLine(tokens), 1, "Unexpected end of file");
        int id;
        token = NextToken(pos);
        text = token.text;
        ClassifyToken(text.c_str(), &id, nullptr);
        if (id == kLexClose) {
            if (script->ifThen.empty())
                return Error(this, token.line, 11, "In script %s should be at least one condtion", script->name.c_str());
            return 1;
        }
        if (id != kLexIf) {
            Error(this, token.line, 10, "expected if section");
            return 0;
        }
        token = NextToken(pos);
        text = token.text;
        ClassifyToken(text.c_str(), &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, "If section, ( expected");
            return 0;
        }
        pair.conditions = NewCommandList();
        if (!LoadConditions(&pair.conditions->commands, pos, tokens, &script->vars)) return 0;
        token = NextToken(pos);
        text = token.text;
        ClassifyToken(text.c_str(), &id, nullptr);
        if (id != kLexThen) {
            Error(this, token.line, 4, "then expected after if");
            return 0;
        }
        token = NextToken(pos);
        text = token.text;
        ClassifyToken(text.c_str(), &id, nullptr);
        if (id != kLexOpen) {
            Error(this, token.line, 4, "Expected ( after then");
            return 0;
        }
        pair.commands = NewCommandList();
        if (!LoadCommands(&pair.commands->commands, pos, tokens, &script->vars)) return 0;
        script->ifThen.push_back(pair);
    }
}

BOOL CScript::LoadAssignOperator(SSCCmd* cmd, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens, ScriptVars* locals,
                                 const std::string* name) {
    cmd->Clear();
    BOOL ok;
    if (*pos == tokens->end()) {
        ok = Error(this, LastLine(tokens), 1, "Unexpected end of file");
    } else {
        const ScriptToken token = NextToken(pos);
        std::string text(token.text);
        int id;
        ClassifyToken(text.c_str(), &id, nullptr);
        ok = id == kLexAssign ? 1 : Error(this, token.line, 2, "Wrong lexeme %s", text.c_str());
    }
    if (!ok) return 0;

    int index, type;
    globals_.FindVariable((*name).c_str(), &index, &type);
    // The value is parsed as the single parameter of command 0x78; float 1 holds the type - 1.
    static const std::uint32_t kTypeCode[] = {0, 0x00000000, 0x3F800000, 0x40000000, 0x40400000};
    if (type < 1 || type > 4) {
        game::FatalError("Uknown variable type at CScript::LoadAssignOperator");
        return 0;
    }
    BOOL result = LoadFunction(cmd, pos, tokens, kCmdAssign, locals, kTypeLetter[type], 1, 0);
    StoreBits(&cmd->floatArgs[1], kTypeCode[type]);
    StoreIntAsFloat(&cmd->floatArgs[0], index);
    return result;
}

BOOL CScript::LoadVariableDeclarations(ScriptDefinition* script, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens,
                                       BOOL qualify) {
    ScriptToken token;
    std::string name, typeName;
    BOOL result = 0;
    bool first = true;
    while (*pos != tokens->end()) {
        token = NextToken(pos);
        name = token.text;
        if (Mbscmp(name, ")") == 0) {
            result = 1;
            break;
        }
        if (!first) {
            if (Mbscmp(name, ",") != 0)
                return Error(this, token.line, 4, "Expected , or ) after variable");
            token = NextToken(pos);
            name = token.text;
        }
        BOOL ok;
        if (*pos == tokens->end()) {
            ok = Error(this, LastLine(tokens), 1, "Unexpected end of file");
        } else {
            const ScriptToken colon = NextToken(pos);
            std::string text(colon.text);
            int id;
            ClassifyToken(text.c_str(), &id, nullptr);
            ok = id == kLexColon ? 1 : Error(this, colon.line, 2, "Wrong lexeme %s", text.c_str());
        }
        if (!ok) return 0;
        if (qualify) {
            std::string qualified;
            QualifyName(&qualified, name);
            name = qualified;
        }
        token = NextToken(pos);
        typeName = token.text;
        int type;
        if (Mbscmp(typeName, "float") == 0) type = kScriptFloat;
        else if (Mbscmp(typeName, "string") == 0) type = kScriptString;
        else if (Mbscmp(typeName, "object") == 0) type = kScriptObject;
        else if (Mbscmp(typeName, "group") == 0) type = kScriptGroup;
        else return Error(this, token.line, 12, "Unknown parameter type");
        if (!script->AddParameter(&name, type))   // "Duplicate variable name %s"
            return Error(this, token.line, 13, "Duplicate variable name %s", name.c_str());
        first = false;
    }
    return result;
}

BOOL CScript::LoadCommands(std::vector<SSCCmd>* commands, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens,
                           ScriptVars* locals) {
    ScriptToken token;
    std::string text, qualified;
    SSCCmd local;
    SSCCmd* cmd = &local;
    ScriptVars empty;
    if (!locals) locals = &empty;
    commands->clear();
    for (;;) {
        if (*pos == tokens->end()) return Error(this, LastLine(tokens), 1, "Unexpected end of file");
        cmd->Clear();
        token = NextToken(pos);
        text = token.text;
        int id;
        ClassifyToken(text.c_str(), &id, nullptr);
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
                if (*pos == tokens->end()) {
                    open = Error(this, LastLine(tokens), 1, "Unexpected end of file");
                } else {
                    const ScriptToken next = NextToken(pos);
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
                std::string name;
                QualifyName(&name, text);
                qualified = name;
            }
            std::vector<ScriptDefinition>& scripts = definitions_;
            std::size_t index = 0;
            for (; index < scripts.size(); ++index)
                if (Mbscmp(scripts[index].name, text) == 0 || Mbscmp(scripts[index].name, qualified) == 0) break;
            if (index < scripts.size()) {
                // A call to a declared script: command 8, float 0 = script index, string 0 = name.
                if (!LoadFunction(cmd, pos, tokens, kCmdCallScript, locals, scripts[index].params.c_str(), 0, 1))
                    return 0;
                cmd->stringArgs[0] = Mbscmp(scripts[index].name, text) == 0 ? text : qualified;
                StoreIntAsFloat(&cmd->floatArgs[0], static_cast<int>(index));
                commands->push_back(*cmd);
                continue;
            }
            if (globals_.HasVariable(text)) {
                ok = LoadAssignOperator(cmd, pos, tokens, locals, &text);
            } else if (globals_.HasVariable(qualified)) {
                ok = LoadAssignOperator(cmd, pos, tokens, locals, &qualified);
            } else {
                return Error(this, token.line, 3, "Unexpected token '%s'", text.c_str());
            }
        }
        if (!ok) return 0;
        commands->push_back(*cmd);
    }
}
// Rebirth's AddNewScriptCommands also patches this function: the keyword table and a hook
// that would parse a body after commands whose record has flag 1 (no record has), so
// For and ForIf stay the only ones.


BOOL CScript::Load(std::string* text, BOOL runWorldScript) {
    std::list<ScriptToken> tokens;
    ScriptDefinition definition;
    ScriptDefinition* decl = &definition;
    ScriptDecl* worldScript = nullptr;

    errorLine_ = 0;
    errorCode_ = 0;
    errorText_ = "No errors found";

    // Preprocess line by line: "#define name value" lines are collected (and become empty), the
    // other lines get the defines substituted. The text is replaced by the result.
    {
        std::list<ScriptDefine> defines;
        std::string result, line;
        ScriptDefine define;
        const char* keyword = "#define";
        char* p = text->data();   // cut into lines in place (LockBuffer)
        while (*p) {
            char* start = p;
            while (*p && *p != '\n') ++p;
            if (*p) *p++ = 0;
            line = start;
            if (std::strncmp(start, keyword, std::strlen(keyword)) == 0) {
                define.name = game::kEmptyText;
                define.value = game::kEmptyText;
                char* q = start + std::strlen(keyword);
                while (*q && IsSpace(*q)) ++q;
                char* name = q;
                while (*q && !IsSpace(*q)) ++q;
                if (*q) *q++ = 0;
                define.name = name;
                while (*q && IsSpace(*q)) ++q;
                define.value = q;
                AddDefine(&defines, &define);
                line = game::kEmptyText;
            } else {
                ApplyDefines(&line, &defines);
            }
            result = (result + line) + "\n";   // "\n"
        }
        *text = result;
    }

    if (!Tokenize((*text).c_str(), &tokens)) return 0;

    for (std::list<ScriptToken>::const_iterator pos = tokens.begin(); pos != tokens.end();) {
        ScriptToken token = NextToken(&pos);
        std::string word = token.text;
        int id;
        ClassifyToken(word.c_str(), &id, nullptr);
        switch (id) {
        case kLexWorldScript: {
            BOOL ok;
            if (pos == tokens.end()) {
                ok = Error(this, LastLine(&tokens), 1, "Unexpected end of file");
            } else {
                const ScriptToken next = NextToken(&pos);
                std::string nextText = next.text;
                int nextId;
                ClassifyToken(nextText.c_str(), &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, "Wrong lexeme %s", nextText.c_str());
            }
            if (!ok) return 0;
            if (worldScript) return Error(this, token.line, 14, "Only one WorldScript() per file allowed");
            worldScript = NewWorldScript();
            if (!LoadCommands(&worldScript->commands, &pos, &tokens, nullptr)) return 0;
            break;
        }
        case kLexScript: {
            decl->Reset();
            token = NextToken(&pos);
            word = token.text;
            {
                std::string name;
                QualifyName(&name, word);
                decl->name = name;
            }
            if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return 0;
            const int index = FindScript(decl->name);
            if (index < 0) {
                // Not declared before: parameters, then the body.
                if (!LoadVariableDeclarations(decl, &pos, &tokens, 0)) return 0;
                if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return 0;
                if (!LoadScriptBody(decl, &pos, &tokens)) return 0;
                definitions_.push_back(*decl);
            } else {
                ScriptDefinition* declared = &definitions_[static_cast<std::size_t>(index)];
                if (!declared->ifThen.empty())
                    return Error(this, token.line, 16, "Multiple declaration of script %s", declared->name.c_str());
                if (!LoadScriptBody(declared, &pos, &tokens)) return 0;
            }
            break;
        }
        case kLexDeclareScript: {
            decl->Reset();
            token = NextToken(&pos);
            word = token.text;
            {
                std::string name;
                QualifyName(&name, word);
                decl->name = name;
            }
            if (FindScript(decl->name) != -1) return Error(this, token.line, 15, "Duplicate script %s", word.c_str());
            if (!ExpectLexeme(&tokens, &pos, kLexOpen)) return 0;
            if (!LoadVariableDeclarations(decl, &pos, &tokens, 0)) return 0;
            definitions_.push_back(*decl);
            break;
        }
        case kLexGlobalVars: {
            decl->Reset();
            BOOL ok;
            if (pos == tokens.end()) {
                ok = Error(this, LastLine(&tokens), 1, "Unexpected end of file");
            } else {
                const ScriptToken next = NextToken(&pos);
                std::string nextText = next.text;
                int nextId;
                ClassifyToken(nextText.c_str(), &nextId, nullptr);
                ok = nextId == kLexOpen ? 1 : Error(this, next.line, 2, "Wrong lexeme %s", nextText.c_str());
            }
            if (!ok) return 0;
            // Declared with qualified names, then added to the global variables.
            if (!LoadVariableDeclarations(decl, &pos, &tokens, 1)) return 0;
            decl->vars.ResetValues();
            globals_.AppendFrom(decl->vars);
            break;
        }
        default:
            return Error(this, token.line, 3, "Unexpected '%s'", word.c_str());
        }
    }

    if (worldScript && runWorldScript) RunWorldScript(worldScript);
    loaded_ = 1;
    ++fileCount_;
    return 1;
}

BOOL CScript::LoadFile(const char* path) {
    std::string text;
    if (!ReadTextFile(path, &text)) return Error(this, 0, 17, "Unable to read file \"%s\"", path);   // "Unable to read file \"%s\""
    return Load(&text, 1);
}
