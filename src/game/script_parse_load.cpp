// Script loader, part 2: the parser. Script::Load preprocesses the text (#define), tokenizes it
// and parses the sections (WorldScript, Script, DeclareScript, GlobalVars); the Load* functions
// below build the ScriptCall trees. See script.h and docs/notes/script.md ("Loader and parser").
#include "script.h"
#include "game_strings.h"
#include "script_parse.h"
#include "script_addon.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>

using namespace scriptparse;

int Script::Error(Script* script, int line, ScriptParseError code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = ScriptErrorV(script, line, code, format, args);
    va_end(args);
    return result;
}

namespace {

// ---- Small helpers ------------------------------------------------------------------------------

// *dst = (float)atof(text): the CRT's result stored as a float.
void StoreAtof(float* dst, const std::string& text) { *dst = str::ToFloat(text); }

// An int stored as a float.
void StoreIntAsFloat(float* dst, int value) { *dst = static_cast<float>(value); }

void StoreBits(float* dst, std::uint32_t bits) { *dst = std::bit_cast<float>(bits); }

int CountChar(const char* text, char c) {
    int n = 0;
    for (; *text; ++text)
        if (*text == c) ++n;
    return n;
}

// The ScriptType named by a declaration's type word ("float", ...); 0 for none.
int TypeOfName(const std::string& name) {
    for (int type = kScriptFloat; type <= kScriptGroup; ++type)
        if (name == ScriptTypeName(type)) return type;
    return 0;
}

} // namespace


bool Script::LoadFunction(ScriptCall* cmd, TokenReader* tokens, int id, ScriptVars* locals, const char* signature, bool assignment,
                          bool scriptCall) {
    int paramIndex = 0;
    // The next argument slot of each type (by ScriptType). Extra leading slots: an assignment keeps the variable
    // index and type in floats 0 and 1, a script call the script index in float 0 and its name in string 0.
    int nextArg[kScriptGroup + 1] = {};
    ScriptToken token;
    std::string qualified;
    ScriptBinding local{0, 0, 0}, global{0, 0, 0};
    ScriptSubCall sub;
    if (assignment) nextArg[kScriptFloat] = 2;
    if (scriptCall) {
        nextArg[kScriptFloat] = 1;
        nextArg[kScriptString] = 1;
    }

    cmd->id = id;
    cmd->floatArgs.resize(static_cast<std::size_t>(CountChar(signature, 'f') + nextArg[kScriptFloat]));
    cmd->stringArgs.resize(static_cast<std::size_t>(CountChar(signature, 's') + nextArg[kScriptString]));
    cmd->objectArgs.resize(static_cast<std::size_t>(CountChar(signature, 'o') + CountChar(signature, 'O')));
    cmd->groupArgs.resize(static_cast<std::size_t>(CountChar(signature, 'g')));
    cmd->localBindings.clear();
    cmd->globalBindings.clear();
    cmd->subCalls.clear();

    if (tokens->AtEnd()) return Error(this, tokens->LastLine(), kErrExpected, "Expected '%s'", KeywordName(kLexOpen));

    ScriptVars* vars = &globals;
    do {
        int tokenId;
        token = tokens->Next(&tokenId);
        if (!assignment) {
            scriptaddon::SignatureStep(cmd, &signature, tokenId);
            if (tokenId == kLexClose) {
                if (*signature) return Error(this, token.line, kErrNotEnoughParameters, "Not enough parameters");
                return true;
            }
            if (paramIndex == 0) {
                if (tokenId != kLexOpen) return Error(this, token.line, kErrExpected, "Expected '%s'", KeywordName(kLexOpen));
            } else if (tokenId != kLexComma) {
                return Error(this, token.line, kErrExpected, "Expected '%s'", KeywordName(kLexComma));
            }
            // The parameter (no end-of-tokens check, as in the game).
            token = tokens->Next(&tokenId);
            if (paramIndex == 0 && tokenId == kLexClose) {
                if (*signature == 0) return true;
                return Error(this, token.line, kErrNotEnoughParameters, "Not enough parameters");
            }
        }

        qualified = QualifyName(token.text);

        // Literal parameters are stored directly; others bind a local or global variable of
        // the same name, or are parsed as a nested function call.
        if (*signature == 0) return Error(this, token.line, kErrTooManyParameters, "Too much parameters");
        const int type = ScriptTypeOfLetter(*signature);
        if (type == 0) game::FatalError("wrong type char in CScript::LoadFunction");
        const int argIndex = nextArg[type]++;
        local.type = type;
        global.type = type;
        local.argIndex = argIndex;
        global.argIndex = argIndex;
        if (*signature == 'O') {
            // Loop counter of For/ForIf/Every/Any/Sum: must be a global object variable.
            local.varIndex = -1;
            global.varIndex = vars->FindOfType(type, token.text);
            if (global.varIndex < 0) {
                global.varIndex = vars->FindOfType(type, qualified);
                if (global.varIndex < 0)
                    return Error(this, token.line, kErrLoopCounter, "Only global variables are allowed to be loop counters");
            }
        } else {
            local.varIndex = locals->FindOfType(type, token.text);
            global.varIndex = vars->FindOfType(type, token.text);
            if (global.varIndex < 0) global.varIndex = vars->FindOfType(type, qualified);
        }
        bool bind = true;
        if (type == kScriptFloat && tokenId == kLexNumber) {
            StoreAtof(&cmd->floatArgs[argIndex], token.text);
            bind = false;
        } else if (type == kScriptString && tokenId == kLexString) {
            cmd->stringArgs[argIndex] = str::Mid(token.text, 1, static_cast<int>(token.text.size()) - 2);
            bind = false;
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
                if (!nested) return Error(this, token.line, kErrParameterType, "Parameter of type %s expected", ScriptTypeName(type));
                if (!LoadFunction(sub.cmd.get(), tokens, tokenId, locals, nested, false, false)) return false;
                cmd->subCalls.push_back(sub);
            }
        }

        if (assignment) return true;
        ++signature;
        ++paramIndex;
    } while (!tokens->AtEnd());

    return tokens->EndOfFileError();
}

bool Script::LoadConditions(std::vector<ScriptCall>* conditions, TokenReader* tokens, ScriptVars* locals) {
    ScriptCall cmd;
    conditions->clear();
    for (;;) {
        if (tokens->AtEnd()) return tokens->EndOfFileError();
        cmd.Clear();
        int id;
        const ScriptToken token = tokens->Next(&id);
        if (id == kLexClose) return true;
        const char* signature = FunctionSignature(id, kScriptFloat);
        if (!signature) return Error(this, token.line, kErrFloatFunction, "Function returning float expected");
        if (!LoadFunction(&cmd, tokens, id, locals, signature, false, false)) return false;
        conditions->push_back(cmd);
    }
}

bool Script::LoadScriptBody(ScriptDefinition* script, TokenReader* tokens) {
    // A body that fails leaves nothing: the blocks and pairs it added are dropped.
    const std::size_t blockCount = blocks.size();
    const std::size_t pairCount = script->ifThen.size();
    auto loadPairs = [&]() -> bool {
        ScriptIfThen pair;
        for (;;) {
            if (tokens->AtEnd()) return tokens->EndOfFileError();
            int id;
            ScriptToken token = tokens->Next(&id);
            if (id == kLexClose) {
                if (script->ifThen.empty())
                    return Error(this, token.line, kErrNoCondition, "In script %s should be at least one condition", script->name);
                return true;
            }
            if (id != kLexIf) {
                Error(this, token.line, kErrExpectedIf, "expected if section");
                return false;
            }
            token = tokens->Next(&id);
            if (id != kLexOpen) {
                Error(this, token.line, kErrExpected, "If section, ( expected");
                return false;
            }
            pair.conditions = NewCommandList();
            if (!LoadConditions(&pair.conditions->commands, tokens, &script->vars)) return false;
            token = tokens->Next(&id);
            if (id != kLexThen) {
                Error(this, token.line, kErrExpected, "then expected after if");
                return false;
            }
            token = tokens->Next(&id);
            if (id != kLexOpen) {
                Error(this, token.line, kErrExpected, "Expected ( after then");
                return false;
            }
            pair.commands = NewCommandList();
            if (!LoadCommands(&pair.commands->commands, tokens, &script->vars)) return false;
            script->ifThen.push_back(pair);
        }
    };
    if (loadPairs()) return true;
    blocks.resize(blockCount);
    script->ifThen.resize(pairCount);
    return false;
}

bool Script::LoadAssignOperator(ScriptCall* cmd, TokenReader* tokens, ScriptVars* locals, const std::string* name) {
    cmd->Clear();
    if (!tokens->Expect(kLexAssign)) return false;

    auto [index, type] = globals.FindVariable(*name);
    // The value is parsed as the single parameter of command 0x78; float 1 holds the type - 1.
    static const std::uint32_t kTypeCode[] = {0, 0x00000000, 0x3F800000, 0x40000000, 0x40400000};
    if (type < 1 || type > 4) {
        game::FatalError("Uknown variable type at CScript::LoadAssignOperator");
        return false;
    }
    const char signature[2] = {ScriptTypeLetter(type), 0};
    bool result = LoadFunction(cmd, tokens, kCmdAssign, locals, signature, true, false);
    StoreBits(&cmd->floatArgs[1], kTypeCode[type]);
    StoreIntAsFloat(&cmd->floatArgs[0], index);
    return result;
}

bool Script::LoadVariableDeclarations(ScriptDefinition* script, TokenReader* tokens, bool qualify) {
    bool first = true;
    while (!tokens->AtEnd()) {
        ScriptToken token = tokens->Next();
        if (token.text == ")") return true;
        if (!first) {
            if (token.text != ",") return Error(this, token.line, kErrExpected, "Expected , or ) after variable");
            token = tokens->Next();
        }
        std::string name = token.text;
        if (!tokens->Expect(kLexColon)) return false;
        if (qualify) name = QualifyName(name);
        token = tokens->Next();
        const int type = TypeOfName(token.text);
        if (type == 0) return Error(this, token.line, kErrUnknownType, "Unknown parameter type");
        if (!script->AddParameter(&name, type)) return Error(this, token.line, kErrDuplicateVariable, "Duplicate variable name %s", name);
        first = false;
    }
    return tokens->EndOfFileError();
}

bool Script::LoadCommands(std::vector<ScriptCall>* commands, TokenReader* tokens, ScriptVars* locals) {
    ScriptCall cmd;
    ScriptVars empty;
    if (!locals) locals = &empty;
    commands->clear();
    for (;;) {
        if (tokens->AtEnd()) return tokens->EndOfFileError();
        cmd.Clear();
        int id;
        const ScriptToken token = tokens->Next(&id);
        if (id == kLexClose) return true;

        const ScriptKeyword* command = nullptr;
        for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
            if (static_cast<int>(k->id) == id && k->command) {
                command = k;
                break;
            }

        bool ok;
        if (command) {
            ok = LoadFunction(&cmd, tokens, id, locals, command->command, false, false);
            if (id == kCmdFor || id == kCmdForIf) {
                // The loop body: `(` commands `)`, which sees the script's parameters and locals. Parsed even
                // when the loop header failed.
                if (!tokens->Expect(kLexOpen)) return false;
                ScriptBlock* body = NewCommandList();
                cmd.body = body;
                if (!LoadCommands(&body->commands, tokens, locals)) return false;
            }
        } else {
            const std::string& text = token.text;
            const std::string qualified = QualifyName(text);
            std::size_t index = 0;
            for (; index < definitions.size(); ++index)
                if (definitions[index].name == text || definitions[index].name == qualified) break;
            if (index < definitions.size()) {
                // A call to a declared script: command 8, float 0 = script index, string 0 = name.
                if (!LoadFunction(&cmd, tokens, kCmdCallScript, locals, definitions[index].params.c_str(), false, true))
                    return false;
                cmd.stringArgs[0] = definitions[index].name == text ? text : qualified;
                StoreIntAsFloat(&cmd.floatArgs[0], static_cast<int>(index));
                commands->push_back(cmd);
                continue;
            }
            if (globals.HasVariable(text)) {
                ok = LoadAssignOperator(&cmd, tokens, locals, &text);
            } else if (globals.HasVariable(qualified)) {
                ok = LoadAssignOperator(&cmd, tokens, locals, &qualified);
            } else {
                return Error(this, token.line, kErrUnexpectedToken, "Unexpected token '%s'", text);
            }
        }
        if (!ok) return false;
        commands->push_back(cmd);
    }
}

bool Script::Load(std::string* text, bool runWorldScript) {
    std::vector<ScriptToken> tokens;
    ScriptDefinition definition;
    ScriptDefinition* decl = &definition;
    ScriptBlock* worldScript = nullptr;

    errorLine = 0;
    errorCode = 0;
    errorText = "No errors found";

    // Preprocess line by line: "#define name value" lines are collected (and become empty), the
    // other lines get the defines substituted. The text is replaced by the result.
    {
        std::vector<ScriptDefine> defines;
        std::string result, line;
        ScriptDefine define;
        const char* keyword = "#define";
        char* p = text->data();   // cut into lines in place
        while (*p) {
            char* start = p;
            while (*p && *p != '\n') ++p;
            if (*p) *p++ = 0;
            line = start;
            if (std::strncmp(start, keyword, std::strlen(keyword)) == 0) {
                define.name = game::kEmptyText;
                define.value = game::kEmptyText;
                char* q = start + std::strlen(keyword);
                while (*q && text::IsSpace(*q)) ++q;
                char* name = q;
                while (*q && !text::IsSpace(*q)) ++q;
                if (*q) *q++ = 0;
                define.name = name;
                while (*q && text::IsSpace(*q)) ++q;
                define.value = q;
                AddDefine(&defines, &define);
                line = game::kEmptyText;
            } else {
                ApplyDefines(&line, &defines);
            }
            result = (result + line) + "\n";
        }
        *text = result;
    }

    if (!Tokenize(*text, &tokens)) return false;

    TokenReader reader(this, &tokens);
    while (!reader.AtEnd()) {
        int id;
        ScriptToken token = reader.Next(&id);
        switch (id) {
        case kLexWorldScript: {
            if (!reader.Expect(kLexOpen)) return false;
            if (worldScript) return Error(this, token.line, kErrSecondWorldScript, "Only one WorldScript() per file allowed");
            worldScript = NewWorldScript();
            if (!LoadCommands(&worldScript->commands, &reader, nullptr)) return false;
            break;
        }
        case kLexScript: {
            decl->Reset();
            token = reader.Next();
            decl->name = QualifyName(token.text);
            if (!reader.Expect(kLexOpen)) return false;
            const int index = FindScript(decl->name);
            if (index < 0) {
                // Not declared before: parameters, then the body.
                if (!LoadVariableDeclarations(decl, &reader, false)) return false;
                if (!reader.Expect(kLexOpen)) return false;
                if (!LoadScriptBody(decl, &reader)) return false;
                definitions.push_back(*decl);
            } else {
                ScriptDefinition* declared = &definitions[static_cast<std::size_t>(index)];
                if (!declared->ifThen.empty())
                    return Error(this, token.line, kErrMultipleDeclaration, "Multiple declaration of script %s", declared->name);
                if (!LoadScriptBody(declared, &reader)) return false;
            }
            break;
        }
        case kLexDeclareScript: {
            decl->Reset();
            token = reader.Next();
            decl->name = QualifyName(token.text);
            if (FindScript(decl->name) != -1) return Error(this, token.line, kErrDuplicateScript, "Duplicate script %s", token.text);
            if (!reader.Expect(kLexOpen)) return false;
            if (!LoadVariableDeclarations(decl, &reader, false)) return false;
            definitions.push_back(*decl);
            break;
        }
        case kLexGlobalVars: {
            decl->Reset();
            if (!reader.Expect(kLexOpen)) return false;
            // Declared with qualified names, then added to the global variables.
            if (!LoadVariableDeclarations(decl, &reader, true)) return false;
            decl->vars.ResetValues();
            globals.AppendFrom(decl->vars);
            break;
        }
        default:
            return Error(this, token.line, kErrUnexpectedToken, "Unexpected '%s'", token.text);
        }
    }

    if (worldScript && runWorldScript) RunWorldScript(worldScript);
    loaded = 1;
    ++fileCount;
    return true;
}

bool Script::LoadFile(const std::string& path) {
    std::string text;
    if (!ReadTextFile(path, &text)) return Error(this, 0, kErrCannotRead, "Unable to read file \"%s\"", path);
    return Load(&text, true);
}
