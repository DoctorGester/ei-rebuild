// The script parse tree's text form, and the tree of an old-format save's program (script_ir.h).
#include "game/script/script_ir.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace scriptir {

namespace {

constexpr ScriptType kTypes[] = {ScriptType::Float, ScriptType::String, ScriptType::Object, ScriptType::Group};

// The shortest text that reads back as the same float.
std::string Number(float value) { return std::format("{}", value); }

std::string SlotTag(ScriptType type, int slot) { return std::format("{}{}=", ScriptTypeLetter(type), slot); }

// A call's name: the keyword's, or the target of an Assign or a declared-script call.
std::string Head(ScriptFunction id, const std::string& targetName, int target, ScriptType targetType) {
    if (id == ScriptFunction::Assign) return std::format("set {}:{}", targetName, ScriptTypeName(targetType));
    if (id == ScriptFunction::CallScript) return std::format("run {}#{}", targetName, target);
    return KeywordName(static_cast<ScriptLexeme>(id));
}

class Writer {
public:
    explicit Writer(bool lines) : lines(lines) {}

    void Line(int depth, std::string_view text, int line = 0) {
        out.append(static_cast<std::size_t>(depth) * 2, ' ');
        out += text;
        if (lines && line) out += std::format("  @{}", line);
        out += '\n';
    }

    std::string out;

private:
    bool lines = false;
};

// ---- The tree ---------------------------------------------------------------------------------------------------

std::string CallText(const Call& call);

std::string ArgText(const Arg& arg) {
    switch (arg.kind) {
    case ArgKind::Number: return Number(arg.number);
    case ArgKind::Text: return std::format("\"{}\"", arg.text);
    case ArgKind::Local: return "local " + arg.name;
    case ArgKind::Global: return "global " + arg.name;
    case ArgKind::Call: return CallText(*arg.call);
    }
    return {};
}

std::string CallText(const Call& call) {
    std::vector<const Arg*> args;
    for (const Arg& arg : call.args) args.push_back(&arg);
    std::stable_sort(args.begin(), args.end(), [](const Arg* a, const Arg* b) {
        return a->type != b->type ? a->type < b->type : a->slot < b->slot;
    });
    std::string text = Head(call.id, call.targetName, call.target, call.targetType) + "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) text += ", ";
        text += SlotTag(args[i]->type, args[i]->slot) + ArgText(*args[i]);
    }
    return text + ")";
}

void WriteBlock(Writer& w, int depth, const Block& block) {
    for (const Call& call : block.calls) {
        if (call.body) {
            w.Line(depth, CallText(call) + " body", call.line);
            WriteBlock(w, depth + 1, *call.body);
        } else {
            w.Line(depth, CallText(call), call.line);
        }
    }
}

std::string ParamsText(const std::vector<Variable>& params) {
    std::string text;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i) text += ", ";
        text += std::format("{} {}", ScriptTypeName(params[i].type), params[i].name);
    }
    return text;
}

const std::string kNoName;

template <typename T>
const std::string& NameIn(const ScriptNamedValues<T>& values, int index) {
    return index >= 0 && index < std::ssize(values.names) ? values.names[static_cast<std::size_t>(index)] : kNoName;
}

} // namespace

const std::string& VariableName(const ScriptVars& vars, ScriptType type, int index) {
    switch (type) {
    case ScriptType::Float: return NameIn(vars.floats, index);
    case ScriptType::String: return NameIn(vars.strings, index);
    case ScriptType::Object: return NameIn(vars.objects, index);
    default: return NameIn(vars.groups, index);
    }
}

std::vector<Variable> Params(const ScriptDefinition& definition) {
    std::vector<Variable> params;
    int next[4] = {};
    for (const char letter : definition.params) {
        const ScriptType type = ScriptTypeOfLetter(letter);
        params.push_back({VariableName(definition.vars, type, next[lib::Index(type) - 1]++), type});
    }
    return params;
}

std::string Dump(const File& file, bool lines) {
    Writer w(lines);
    std::vector<const Variable*> globals;
    for (const Variable& global : file.globals) globals.push_back(&global);
    std::stable_sort(globals.begin(), globals.end(), [](const Variable* a, const Variable* b) { return a->type < b->type; });
    for (const Variable* global : globals) w.Line(0, std::format("global {} {}", ScriptTypeName(global->type), global->name), global->line);
    for (const Block& world : file.worldScripts) {
        w.Line(0, "world");
        WriteBlock(w, 1, world);
    }
    std::vector<const Definition*> definitions;
    for (const Definition& definition : file.definitions) definitions.push_back(&definition);
    std::stable_sort(definitions.begin(), definitions.end(), [](const Definition* a, const Definition* b) { return a->index < b->index; });
    for (const Definition* definition : definitions) {
        w.Line(0, std::format("script {} {}({})", definition->index, definition->name, ParamsText(definition->params)), definition->line);
        for (const Rule& rule : definition->rules) {
            w.Line(1, "rule", rule.line);
            w.Line(2, "if");
            WriteBlock(w, 3, rule.conditions);
            w.Line(2, "then");
            WriteBlock(w, 3, rule.commands);
        }
    }
    return w.out;
}

namespace {

// What Loaded builds a tree from: the saved Script's calls, each argument slot as its binding, nested call or constant.
class LoadedTree {
public:
    LoadedTree(const Script& script, std::string* error) : script(script), error(error) {}

    bool Commands(const ScriptBlock& in, const ScriptVars& locals, Block& out) {
        out.id = in.id;
        for (const ScriptCall& command : in.commands)
            if (!Call(command, ScriptType::None, locals, out.calls.emplace_back())) return false;
        return true;
    }

    bool Conditions(const ScriptBlock& in, const ScriptVars& locals, Block& out) {
        out.id = in.id;
        for (const ScriptCall& condition : in.commands)
            if (!Call(condition, ScriptType::Float, locals, out.calls.emplace_back())) return false;
        return true;
    }

private:
    bool Fail(const std::string& what) {
        *error = what;
        return false;
    }

    // The parameter letters of a call: a declared script's, an assignment's (its variable's type), a function's of
    // the `result` type, else a command's.
    std::string_view Signature(const ScriptCall& call, ScriptType result) {
        if (call.id == ScriptFunction::CallScript) return script.definitions[static_cast<std::size_t>(call.target)].params;
        if (call.id == ScriptFunction::Assign) {
            assignLetter = ScriptTypeLetter(call.targetType);
            return std::string_view(&assignLetter, 1);
        }
        if (result != ScriptType::None) return FunctionSignature(static_cast<int>(call.id), result).value_or("");
        for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
            if (k->id == call.id && k->command) return k->command;
        return {};
    }

    bool Call(const ScriptCall& in, ScriptType result, const ScriptVars& locals, scriptir::Call& out) {
        out.id = in.id;
        out.target = in.target;
        out.targetType = in.targetType;
        if (in.id == ScriptFunction::CallScript) {
            if (in.target < 0 || in.target >= std::ssize(script.definitions))
                return Fail(std::format("a call of script {}, which the program lacks", in.target));
            out.targetName = script.definitions[static_cast<std::size_t>(in.target)].name;
        } else if (in.id == ScriptFunction::Assign) {
            out.targetName = VariableName(script.globals, in.targetType, in.target);
        }
        out.token = in.id == ScriptFunction::CallScript || in.id == ScriptFunction::Assign
                        ? out.targetName
                        : std::string(KeywordName(static_cast<ScriptLexeme>(in.id)));
        const std::string_view signature = Signature(in, result);
        const std::size_t counts[] = {in.floatArgs.size(), in.stringArgs.size(), in.objectArgs.size(), in.groupArgs.size()};
        std::size_t next[4] = {};
        // The slots in the signature's order: '?' makes the rest optional (the call ends at the first type without a
        // slot left), '+' repeats the letter before it while slots are left.
        const auto take = [&](char letter) {
            const ScriptType type = ScriptTypeOfLetter(letter);
            if (type == ScriptType::None) return false;
            const std::size_t t = static_cast<std::size_t>(lib::Index(type) - 1);
            if (next[t] >= counts[t]) return false;
            Arg& arg = out.args.emplace_back();
            arg.type = type;
            arg.slot = static_cast<int>(next[t]++);
            return true;
        };
        char previous = 0;
        for (const char letter : signature) {
            if (letter == '?') continue;
            if (letter == '+') {
                while (take(previous)) {}
                continue;
            }
            previous = letter;
            if (!take(letter)) break;
        }
        for (std::size_t t = 0; t < 4; ++t)
            if (next[t] != counts[t])
                return Fail(std::format("{}: {} {} arguments, its signature \"{}\" places {}", out.token, counts[t],
                                        ScriptTypeName(kTypes[t]), signature, next[t]));
        for (Arg& arg : out.args)
            if (!Source(in, locals, arg)) return false;
        if (in.body) {
            out.body = std::make_unique<Block>();
            if (!Commands(*in.body, locals, *out.body)) return false;
        }
        return true;
    }

    // What fills the argument's slot: a local, a global, a nested call, or the constant the slot holds.
    bool Source(const ScriptCall& in, const ScriptVars& locals, Arg& arg) {
        const auto matches = [&](const auto& entry) { return entry.type == arg.type && entry.argIndex == arg.slot; };
        if (auto b = std::ranges::find_if(in.localBindings, matches); b != in.localBindings.end()) {
            arg.kind = ArgKind::Local;
            arg.index = b->varIndex;
            arg.name = arg.token = VariableName(locals, arg.type, b->varIndex);
        } else if (auto g = std::ranges::find_if(in.globalBindings, matches); g != in.globalBindings.end()) {
            arg.kind = ArgKind::Global;
            arg.index = g->varIndex;
            arg.name = arg.token = VariableName(script.globals, arg.type, g->varIndex);
        } else if (auto s = std::ranges::find_if(in.subCalls, matches); s != in.subCalls.end()) {
            arg.kind = ArgKind::Call;
            arg.call = std::make_unique<scriptir::Call>();
            return Call(*s->cmd, s->type, locals, *arg.call);
        } else if (arg.type == ScriptType::Float) {
            arg.kind = ArgKind::Number;
            arg.number = in.floatArgs[static_cast<std::size_t>(arg.slot)];
            arg.token = Number(arg.number);
        } else if (arg.type == ScriptType::String) {
            arg.kind = ArgKind::Text;
            arg.text = in.stringArgs[static_cast<std::size_t>(arg.slot)];
            arg.token = "\"" + arg.text + "\"";
        } else {
            return Fail(std::format("{}: {} argument {} has no value", KeywordName(static_cast<ScriptLexeme>(in.id)),
                                    ScriptTypeName(arg.type), arg.slot));
        }
        return true;
    }

    const Script& script;
    std::string* error = nullptr;
    char assignLetter = 0;
};

} // namespace

bool Loaded(const Script& script, File* file, std::string* error) {
    *file = File{};
    file->fileCount = script.fileCount;
    file->ok = true;
    const ScriptVars& globals = script.globals;
    const std::size_t counts[] = {globals.floats.values.size(), globals.strings.values.size(), globals.objects.values.size(),
                                  globals.groups.values.size()};
    for (int t = 0; t < 4; ++t)
        for (std::size_t i = 0; i < counts[t]; ++i)
            file->globals.push_back({VariableName(globals, kTypes[t], static_cast<int>(i)), kTypes[t]});
    LoadedTree tree(script, error);
    for (std::size_t i = 0; i < script.definitions.size(); ++i) {
        const ScriptDefinition& source = script.definitions[i];
        Definition& definition = file->definitions.emplace_back();
        definition.index = static_cast<int>(i);
        definition.name = source.name;
        definition.params = Params(source);
        for (const ScriptIfThen& pair : source.ifThen) {
            Rule& rule = definition.rules.emplace_back();
            if (!tree.Conditions(*pair.conditions, source.vars, rule.conditions) ||
                !tree.Commands(*pair.commands, source.vars, rule.commands))
                return false;
        }
    }
    const ScriptVars noLocals;
    for (const ScriptBlock& world : script.worldBlocks)
        if (!tree.Commands(world, noLocals, file->worldScripts.emplace_back())) return false;
    return true;
}

} // namespace scriptir
