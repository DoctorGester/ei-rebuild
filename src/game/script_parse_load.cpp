// Script loader, part 2: the parser. Script::Load preprocesses the text (#define), tokenizes it
// and parses the sections (WorldScript, Script, DeclareScript, GlobalVars); the Load* functions
// below build the ScriptCall trees. See script.h and docs/notes/script.md ("Loader and parser").
#include "script.h"
#include "script_parse.h"
#include "script_addon.h"

#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdarg>
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

int CountChar(const char* text, char c) {
    int n = 0;
    for (; *text; ++text)
        if (*text == c) ++n;
    return n;
}

// The ScriptType named by a declaration's type word ("float", ...); 0 for none.
ScriptType TypeOfName(const std::string& name) {
    for (int type = static_cast<int>(ScriptType::Float); type <= static_cast<int>(ScriptType::Group); ++type)
        if (name == kScriptTypeNames[type - 1]) return static_cast<ScriptType>(type);
    return ScriptType::None;
}

} // namespace


bool Script::LoadFunction(ScriptCall* cmd, TokenReader* tokens, ScriptFunction id, ScriptVars* locals, const char* signature,
                          ScriptArgSyntax syntax) {
    const bool assignment = syntax == ScriptArgSyntax::Value;
    int paramIndex = 0;
    int nextArg[lib::Index(ScriptType::Group) + 1] = {};   // the next argument slot of each type (by ScriptType)
    ScriptToken token;
    std::string qualified;
    ScriptBinding local{ScriptType::None, 0, 0}, global{ScriptType::None, 0, 0};
    ScriptSubCall sub;

    cmd->id = id;
    cmd->floatArgs.resize(static_cast<std::size_t>(CountChar(signature, 'f')));
    cmd->stringArgs.resize(static_cast<std::size_t>(CountChar(signature, 's')));
    cmd->objectArgs.resize(static_cast<std::size_t>(CountChar(signature, 'o') + CountChar(signature, 'O')));
    cmd->groupArgs.resize(static_cast<std::size_t>(CountChar(signature, 'g')));
    cmd->localBindings.clear();
    cmd->globalBindings.clear();
    cmd->subCalls.clear();

    if (tokens->AtEnd()) return Error(this, tokens->LastLine(), ScriptParseError::Expected, "Expected '%s'", KeywordName(ScriptLexeme::Open));

    ScriptVars* vars = &globals;
    do {
        int tokenId;
        token = tokens->Next(&tokenId);
        if (!assignment) {
            scriptaddon::SignatureStep(cmd, &signature, tokenId);
            if (tokenId == ScriptLexeme::Close) {
                if (*signature) return Error(this, token.line, ScriptParseError::NotEnoughParameters, "Not enough parameters");
                return true;
            }
            if (paramIndex == 0) {
                if (tokenId != ScriptLexeme::Open) return Error(this, token.line, ScriptParseError::Expected, "Expected '%s'", KeywordName(ScriptLexeme::Open));
            } else if (tokenId != ScriptLexeme::Comma) {
                return Error(this, token.line, ScriptParseError::Expected, "Expected '%s'", KeywordName(ScriptLexeme::Comma));
            }
            // The parameter (no end-of-tokens check, as in the game).
            token = tokens->Next(&tokenId);
            if (paramIndex == 0 && tokenId == ScriptLexeme::Close) {
                if (*signature == 0) return true;
                return Error(this, token.line, ScriptParseError::NotEnoughParameters, "Not enough parameters");
            }
        }

        qualified = QualifyName(token.text);

        // Literal parameters are stored directly; others bind a local or global variable of
        // the same name, or are parsed as a nested function call.
        if (*signature == 0) return Error(this, token.line, ScriptParseError::TooManyParameters, "Too much parameters");
        const ScriptType type = ScriptTypeOfLetter(*signature);
        if (type == ScriptType::None) game::FatalExit("wrong type char in CScript::LoadFunction");
        const int argIndex = nextArg[lib::Index(type)]++;
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
                    return Error(this, token.line, ScriptParseError::LoopCounter, "Only global variables are allowed to be loop counters");
            }
        } else {
            local.varIndex = locals->FindOfType(type, token.text);
            global.varIndex = vars->FindOfType(type, token.text);
            if (global.varIndex < 0) global.varIndex = vars->FindOfType(type, qualified);
        }
        bool bind = true;
        if (type == ScriptType::Float && tokenId == ScriptLexeme::Number) {
            StoreAtof(&cmd->floatArgs[argIndex], token.text);
            bind = false;
        } else if (type == ScriptType::String && tokenId == ScriptLexeme::String) {
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
                if (!nested) return Error(this, token.line, ScriptParseError::ParameterType, "Parameter of type %s expected", ScriptTypeName(type));
                if (!LoadFunction(sub.cmd.get(), tokens, static_cast<ScriptFunction>(tokenId), locals, nested)) return false;
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
        if (id == ScriptLexeme::Close) return true;
        const char* signature = FunctionSignature(id, ScriptType::Float);
        if (!signature) return Error(this, token.line, ScriptParseError::FloatFunction, "Function returning float expected");
        if (!LoadFunction(&cmd, tokens, static_cast<ScriptFunction>(id), locals, signature)) return false;
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
            if (id == ScriptLexeme::Close) {
                if (script->ifThen.empty())
                    return Error(this, token.line, ScriptParseError::NoCondition, "In script %s should be at least one condition", script->name);
                return true;
            }
            if (id != ScriptLexeme::If) {
                Error(this, token.line, ScriptParseError::ExpectedIf, "expected if section");
                return false;
            }
            token = tokens->Next(&id);
            if (id != ScriptLexeme::Open) {
                Error(this, token.line, ScriptParseError::Expected, "If section, ( expected");
                return false;
            }
            pair.conditions = NewCommandList();
            if (!LoadConditions(&pair.conditions->commands, tokens, &script->vars)) return false;
            token = tokens->Next(&id);
            if (id != ScriptLexeme::Then) {
                Error(this, token.line, ScriptParseError::Expected, "then expected after if");
                return false;
            }
            token = tokens->Next(&id);
            if (id != ScriptLexeme::Open) {
                Error(this, token.line, ScriptParseError::Expected, "Expected ( after then");
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

bool Script::LoadAssignOperator(ScriptCall* cmd, TokenReader* tokens, ScriptVars* locals, const std::string& name) {
    cmd->Clear();
    if (!tokens->Expect(ScriptLexeme::Assign)) return false;

    // The value is the single argument of an Assign command (the caller found the variable).
    const auto [index, type] = globals.FindVariable(name);
    const char signature[2] = {ScriptTypeLetter(type), 0};
    bool result = LoadFunction(cmd, tokens, ScriptFunction::Assign, locals, signature, ScriptArgSyntax::Value);
    cmd->target = index;
    cmd->targetType = type;
    return result;
}

bool Script::LoadVariableDeclarations(ScriptDefinition* script, TokenReader* tokens, bool qualify) {
    bool first = true;
    while (!tokens->AtEnd()) {
        ScriptToken token = tokens->Next();
        if (token.text == ")") return true;
        if (!first) {
            if (token.text != ",") return Error(this, token.line, ScriptParseError::Expected, "Expected , or ) after variable");
            token = tokens->Next();
        }
        std::string name = token.text;
        if (!tokens->Expect(ScriptLexeme::Colon)) return false;
        if (qualify) name = QualifyName(name);
        token = tokens->Next();
        const ScriptType type = TypeOfName(token.text);
        if (type == ScriptType::None) return Error(this, token.line, ScriptParseError::UnknownType, "Unknown parameter type");
        if (!script->AddParameter(name, type)) return Error(this, token.line, ScriptParseError::DuplicateVariable, "Duplicate variable name %s", name);
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
        if (id == ScriptLexeme::Close) return true;

        const ScriptKeyword* command = nullptr;
        for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
            if (static_cast<int>(k->id) == id && k->command) {
                command = k;
                break;
            }

        bool ok;
        if (command) {
            ok = LoadFunction(&cmd, tokens, command->id, locals, command->command);
            if (command->id == ScriptFunction::For || command->id == ScriptFunction::ForIf) {
                // The loop body: `(` commands `)`, which sees the script's parameters and locals. Parsed even
                // when the loop header failed.
                if (!tokens->Expect(ScriptLexeme::Open)) return false;
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
                // A call to a declared script.
                if (!LoadFunction(&cmd, tokens, ScriptFunction::CallScript, locals, definitions[index].params.c_str()))
                    return false;
                cmd.target = static_cast<int>(index);
                cmd.targetName = definitions[index].name;
                commands->push_back(cmd);
                continue;
            }
            if (globals.HasVariable(text)) {
                ok = LoadAssignOperator(&cmd, tokens, locals, text);
            } else if (globals.HasVariable(qualified)) {
                ok = LoadAssignOperator(&cmd, tokens, locals, qualified);
            } else {
                return Error(this, token.line, ScriptParseError::UnexpectedToken, "Unexpected token '%s'", text);
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
    errorCode = ScriptParseError::None;
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
                define.name.clear();
                define.value.clear();
                char* q = start + std::strlen(keyword);
                while (*q && text::IsSpace(*q)) ++q;
                char* name = q;
                while (*q && !text::IsSpace(*q)) ++q;
                if (*q) *q++ = 0;
                define.name = name;
                while (*q && text::IsSpace(*q)) ++q;
                define.value = q;
                AddDefine(&defines, &define);
                line.clear();
            } else {
                ApplyDefines(&line, defines);
            }
            result = (result + line) + "\n";
        }
        *text = result;
    }

    if (!Tokenize(*text, &tokens)) return false;

    TokenReader reader(this, tokens);
    while (!reader.AtEnd()) {
        int id;
        ScriptToken token = reader.Next(&id);
        switch (static_cast<ScriptLexeme>(id)) {
        case ScriptLexeme::WorldScript: {
            if (!reader.Expect(ScriptLexeme::Open)) return false;
            if (worldScript) return Error(this, token.line, ScriptParseError::SecondWorldScript, "Only one WorldScript() per file allowed");
            worldScript = NewWorldScript();
            if (!LoadCommands(&worldScript->commands, &reader, nullptr)) return false;
            break;
        }
        case ScriptLexeme::Script: {
            decl->Reset();
            token = reader.Next();
            decl->name = QualifyName(token.text);
            if (!reader.Expect(ScriptLexeme::Open)) return false;
            const int index = FindScript(decl->name);
            if (index < 0) {
                // Not declared before: parameters, then the body.
                if (!LoadVariableDeclarations(decl, &reader, false)) return false;
                if (!reader.Expect(ScriptLexeme::Open)) return false;
                if (!LoadScriptBody(decl, &reader)) return false;
                definitions.push_back(*decl);
            } else {
                ScriptDefinition* declared = &definitions[static_cast<std::size_t>(index)];
                if (!declared->ifThen.empty())
                    return Error(this, token.line, ScriptParseError::MultipleDeclaration, "Multiple declaration of script %s", declared->name);
                if (!LoadScriptBody(declared, &reader)) return false;
            }
            break;
        }
        case ScriptLexeme::DeclareScript: {
            decl->Reset();
            token = reader.Next();
            decl->name = QualifyName(token.text);
            if (FindScript(decl->name) != -1) return Error(this, token.line, ScriptParseError::DuplicateScript, "Duplicate script %s", token.text);
            if (!reader.Expect(ScriptLexeme::Open)) return false;
            if (!LoadVariableDeclarations(decl, &reader, false)) return false;
            definitions.push_back(*decl);
            break;
        }
        case ScriptLexeme::GlobalVars: {
            decl->Reset();
            if (!reader.Expect(ScriptLexeme::Open)) return false;
            // Declared with qualified names, then added to the global variables.
            if (!LoadVariableDeclarations(decl, &reader, true)) return false;
            decl->vars.ResetValues();
            globals.AppendFrom(decl->vars);
            break;
        }
        default:
            return Error(this, token.line, ScriptParseError::UnexpectedToken, "Unexpected '%s'", token.text);
        }
    }

    if (worldScript && runWorldScript) RunWorldScript(worldScript);
    loaded = 1;
    ++fileCount;
    return true;
}

bool Script::LoadFile(const std::string& path) {
    std::string text;
    if (!ReadTextFile(path, &text)) return Error(this, 0, ScriptParseError::CannotRead, "Unable to read file \"%s\"", path);
    return Load(&text, true);
}
