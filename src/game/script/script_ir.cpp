// The script parse tree's text form, and the same form of what a load built into a Script (script_ir.h).
#include "game/script/script_ir.h"

#include <algorithm>
#include <format>
#include <iterator>
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
            w.Line(depth, std::format("{} body {}", CallText(call), call.body->id), call.line);
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

// ---- What the loader built --------------------------------------------------------------------------------------

std::string LoadedCallText(const ScriptCall& call, const ScriptVars& locals, const ScriptVars& globals) {
    std::string target = call.targetName;
    if (call.id == ScriptFunction::Assign) target = VariableName(globals, call.targetType, call.target);
    std::string text = Head(call.id, target, call.target, call.targetType) + "(";
    const std::size_t counts[] = {call.floatArgs.size(), call.stringArgs.size(), call.objectArgs.size(), call.groupArgs.size()};
    bool first = true;
    for (int t = 0; t < 4; ++t) {
        const ScriptType type = kTypes[t];
        for (std::size_t slot = 0; slot < counts[t]; ++slot) {
            const int argIndex = static_cast<int>(slot);
            const auto matches = [&](const auto& entry) { return entry.type == type && entry.argIndex == argIndex; };
            std::string value;
            if (auto b = std::ranges::find_if(call.localBindings, matches); b != call.localBindings.end()) {
                value = "local " + VariableName(locals, type, b->varIndex);
            } else if (auto g = std::ranges::find_if(call.globalBindings, matches); g != call.globalBindings.end()) {
                value = "global " + VariableName(globals, type, g->varIndex);
            } else if (auto s = std::ranges::find_if(call.subCalls, matches); s != call.subCalls.end()) {
                value = LoadedCallText(*s->cmd, locals, globals);
            } else if (type == ScriptType::Float) {
                value = Number(call.floatArgs[slot]);
            } else if (type == ScriptType::String) {
                value = std::format("\"{}\"", call.stringArgs[slot]);
            } else {
                value = "-";
            }
            if (!first) text += ", ";
            first = false;
            text += SlotTag(type, argIndex) + value;
        }
    }
    return text + ")";
}

void WriteLoadedBlock(Writer& w, int depth, const ScriptBlock& block, const ScriptVars& locals, const ScriptVars& globals) {
    for (const ScriptCall& call : block.commands) {
        if (call.body) {
            w.Line(depth, std::format("{} body {}", LoadedCallText(call, locals, globals), call.body->id));
            WriteLoadedBlock(w, depth + 1, *call.body, locals, globals);
        } else {
            w.Line(depth, LoadedCallText(call, locals, globals));
        }
    }
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

LoadMark Mark(const Script& script) {
    LoadMark mark;
    mark.definitions = script.definitions.size();
    for (const ScriptDefinition& definition : script.definitions) mark.rules.push_back(definition.ifThen.size());
    mark.globals[0] = script.globals.floats.values.size();
    mark.globals[1] = script.globals.strings.values.size();
    mark.globals[2] = script.globals.objects.values.size();
    mark.globals[3] = script.globals.groups.values.size();
    mark.worldBlocks = script.worldBlocks.size();
    return mark;
}

std::string Dump(const File& file, bool lines) {
    Writer w(lines);
    std::vector<const Variable*> globals;
    for (const Variable& global : file.globals) globals.push_back(&global);
    std::stable_sort(globals.begin(), globals.end(), [](const Variable* a, const Variable* b) { return a->type < b->type; });
    for (const Variable* global : globals) w.Line(0, std::format("global {} {}", ScriptTypeName(global->type), global->name), global->line);
    for (const Block& world : file.worldScripts) {
        w.Line(0, std::format("world {}", world.id));
        WriteBlock(w, 1, world);
    }
    std::vector<const Definition*> definitions;
    for (const Definition& definition : file.definitions) definitions.push_back(&definition);
    std::stable_sort(definitions.begin(), definitions.end(), [](const Definition* a, const Definition* b) { return a->index < b->index; });
    for (const Definition* definition : definitions) {
        w.Line(0, std::format("script {} {}({})", definition->index, definition->name, ParamsText(definition->params)), definition->line);
        for (const Rule& rule : definition->rules) {
            w.Line(1, std::format("rule {} {}", rule.conditions.id, rule.commands.id), rule.line);
            w.Line(2, "if");
            WriteBlock(w, 3, rule.conditions);
            w.Line(2, "then");
            WriteBlock(w, 3, rule.commands);
        }
    }
    return w.out;
}

std::string DumpLoaded(const Script& script, const LoadMark& mark) {
    Writer w(false);
    const ScriptVars& globals = script.globals;
    const std::size_t counts[] = {globals.floats.values.size(), globals.strings.values.size(), globals.objects.values.size(),
                                  globals.groups.values.size()};
    for (int t = 0; t < 4; ++t)
        for (std::size_t i = mark.globals[t]; i < counts[t]; ++i)
            w.Line(0, std::format("global {} {}", ScriptTypeName(kTypes[t]), VariableName(globals, kTypes[t], static_cast<int>(i))));
    const ScriptVars noLocals;
    for (auto world = std::next(script.worldBlocks.begin(), static_cast<std::ptrdiff_t>(mark.worldBlocks)); world != script.worldBlocks.end();
         ++world) {
        w.Line(0, std::format("world {}", world->id));
        WriteLoadedBlock(w, 1, *world, noLocals, globals);
    }
    for (std::size_t i = 0; i < script.definitions.size(); ++i) {
        const ScriptDefinition& definition = script.definitions[i];
        const std::size_t firstRule = i < mark.definitions ? mark.rules[i] : 0;
        if (i < mark.definitions && definition.ifThen.size() <= firstRule) continue;
        w.Line(0, std::format("script {} {}({})", i, definition.name, ParamsText(Params(definition))));
        for (std::size_t r = firstRule; r < definition.ifThen.size(); ++r) {
            const ScriptIfThen& pair = definition.ifThen[r];
            w.Line(1, std::format("rule {} {}", pair.conditions->id, pair.commands->id));
            w.Line(2, "if");
            WriteLoadedBlock(w, 3, *pair.conditions, definition.vars, globals);
            w.Line(2, "then");
            WriteLoadedBlock(w, 3, *pair.commands, definition.vars, globals);
        }
    }
    return w.out;
}

} // namespace scriptir
