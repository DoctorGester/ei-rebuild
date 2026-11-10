// The readable Lua of a script text. See script_lua_writer.h and docs/notes/lua-scripting.md, "Converted scripts".
#include "game/script/script_lua_writer.h"

#include "game/script/script_lua.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <vector>

namespace scriptlua {

namespace {

constexpr const char* kLuaKeywords[] = {"and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
                                        "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
                                        "repeat",   "return", "then", "true", "until",  "while"};

// The globals of the runtime's state that are not bindings: the libraries Lua opens, the helpers, the start function.
constexpr const char* kLuaGlobals[] = {
    "_G",     "_VERSION", "_ENV",         "assert",   "collectgarbage", "error",      "getmetatable", "ipairs",
    "load",   "next",     "pairs",        "pcall",    "print",          "rawequal",   "rawget",       "rawlen",
    "rawset", "select",   "setmetatable", "tonumber", "tostring",       "type",       "xpcall",       "coroutine",
    "table",  "string",   "math",         "utf8",     "dofile",         "loadfile",   "require",      "package",
    "io",     "os",       "debug",        "NewGroup", "CopyGroup",      "GlobalVars", "Script",       "WorldScript",
};

bool IsLoop(ScriptFunction id) { return id == ScriptFunction::For || id == ScriptFunction::ForIf; }
bool IsAggregate(ScriptFunction id) {
    return id == ScriptFunction::Every || id == ScriptFunction::Any || id == ScriptFunction::Sum;
}
bool IsWait(ScriptFunction id) {
    return id == ScriptFunction::Sleep || id == ScriptFunction::WaitSegment || id == ScriptFunction::SleepUntil ||
           id == ScriptFunction::SleepUntilIdle;
}
// A call whose first argument is a counter global it sets to each member.
bool HasCounter(const scriptir::Call& call) {
    return (IsLoop(call.id) || IsAggregate(call.id)) && !call.args.empty() &&
           call.args.front().kind == scriptir::ArgKind::Global;
}
// The argument of a counting call evaluated for each member: the condition of Every / Any / Sum / ForIf.
bool IsMemberCondition(const scriptir::Call& call, const scriptir::Arg& arg) {
    return (IsAggregate(call.id) || call.id == ScriptFunction::ForIf) && arg.type == ScriptType::Float;
}

std::string StripQualifier(std::string_view name) {
    while (name.size() > 2 && name[0] == '!') {
        const std::size_t end = name.find('!', 1);
        if (end == std::string_view::npos || end == 1) break;
        if (!std::all_of(name.begin() + 1, name.begin() + static_cast<std::ptrdiff_t>(end),
                         [](char c) { return c >= '0' && c <= '9'; }))
            break;
        name.remove_prefix(end + 1);
    }
    return std::string(name);
}

// The shortest text Lua reads back as a number that rounds to the same float (the bindings round each number they
// take); %.9g, which always does, where the shortest does not.
std::string LuaNumber(float value) {
    if (std::isinf(value)) return value > 0 ? "math.huge" : "-math.huge";
    if (value == 0.0f) return std::signbit(value) ? "-0.0" : "0";
    const auto readsBack = [&](const std::string& text) {
        double read = 0.0;
        std::from_chars(text.data(), text.data() + text.size(), read);
        return std::bit_cast<std::uint32_t>(static_cast<float>(read)) == std::bit_cast<std::uint32_t>(value);
    };
    std::string text = std::format("{}", value);
    if (readsBack(text)) return text;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

// A Lua string of the same bytes: a long bracket for text with backslashes (paths), else quoted; control bytes and
// bytes from 0x7F as decimal escapes.
std::string LuaString(const std::string& text) {
    const bool plain = std::ranges::all_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte >= 0x20 && byte < 0x7F;
    });
    if (plain && text.find('\\') != std::string::npos && text.find(']') == std::string::npos) return "[[" + text + "]]";
    std::string out = "\"";
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20 || byte >= 0x7F) {
            out += std::format("\\{:03}", static_cast<int>(byte));
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// Every global read outside a loop on it (the counter argument itself is not a read; a loop's group is read before
// the loop), every global named at all, the assignments of each and the loop counters.
class ReadFinder {
public:
    std::set<std::string> reads;
    std::set<std::string> used;
    std::map<std::string, int> assignments;
    std::set<std::string> counters;

    void Block(const scriptir::Block& block) {
        for (const scriptir::Call& call : block.calls) Call(call);
    }

    void Call(const scriptir::Call& call) {
        const bool counting = HasCounter(call);
        if (call.id == ScriptFunction::Assign) {
            used.insert(call.targetName);
            ++assignments[call.targetName];
        }
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const scriptir::Arg& arg = call.args[i];
            if (counting && i == 0) {
                used.insert(arg.name);
                counters.insert(arg.name);
                continue;
            }
            const bool member = counting && IsMemberCondition(call, arg);
            if (member) open.push_back(call.args.front().name);
            Arg(arg);
            if (member) open.pop_back();
        }
        if (call.body) {
            if (counting) open.push_back(call.args.front().name);
            Block(*call.body);
            if (counting) open.pop_back();
        }
    }

private:
    void Arg(const scriptir::Arg& arg) {
        if (arg.kind == scriptir::ArgKind::Global) used.insert(arg.name);
        if (arg.kind == scriptir::ArgKind::Global && std::ranges::find(open, arg.name) == open.end())
            reads.insert(arg.name);
        if (arg.kind == scriptir::ArgKind::Call) Call(*arg.call);
    }

    std::vector<std::string> open;   // the counters of the loops around
};

class Writer {
public:
    Writer(const scriptir::File& file, LuaNames& names, LuaText& out) : file(file), names(names), out(out) {}

    void Write(std::string_view source) {
        FindReads();
        NameDeclarations();
        Line(std::format(
            "-- {}: converted by ei-convert from the game's script (docs/notes/lua-scripting.md, \"Converted "
            "scripts\").",
            source));
        if (!file.ok) {
            Line(std::format(
                "-- The game's parser stops at line {} of the original: {}. What came before stays loaded, as "
                "in the game.",
                file.error.line, file.error.text));
        }
        Globals();
        for (const scriptir::Definition& definition : file.definitions) Definition(definition);
        for (std::size_t i = 0; i < file.worldScripts.size(); ++i) World(file.worldScripts[i], i);
    }

private:
    struct Member {
        std::string counter;   // the tree's name of the counter global
        std::string name;      // the Lua parameter
    };

    // ---- Names --------------------------------------------------------------------------------------------------

    void FindReads() {
        ReadFinder& finder = found;
        for (const scriptir::Block& world : file.worldScripts) finder.Block(world);
        for (const scriptir::Definition& definition : file.definitions) {
            for (const scriptir::Rule& rule : definition.rules) {
                finder.Block(rule.conditions);
                finder.Block(rule.commands);
            }
        }
        for (const std::string& read : finder.reads) {
            if (names.unwrittenCounters.contains(read)) ++out.lateCounterReads;
            names.counterReads.insert(read);
        }
        // Settled globals: at most one assignment in all texts, at the top level of a WorldScript, and no loop on it.
        std::map<std::string, int> top;
        for (const scriptir::Block& world : file.worldScripts)
            for (const scriptir::Call& call : world.calls)
                if (call.id == ScriptFunction::Assign) ++top[call.targetName];
        const auto unsettle = [&](const std::string& global) {
            if (names.settledReads.contains(global)) ++out.lateSettledWrites;
            names.unsettled.insert(global);
        };
        for (const auto& [global, count] : finder.assignments) {
            const int atTop = top.contains(global) ? top.at(global) : 0;
            if (count > atTop || (names.worldAssignments[global] += atTop) > 1) unsettle(global);
        }
        for (const std::string& counter : finder.counters) unsettle(counter);
    }

    // `base`, or with the first suffix `_2`, `_3`... that `taken` does not hold.
    template <class Taken>
    std::string Unique(const std::string& base, Taken taken) {
        if (!taken(base)) return base;
        for (int n = 2;; ++n) {
            std::string name = std::format("{}_{}", base, n);
            if (!taken(name)) return name;
        }
    }

    std::string Allocate(const std::string& irName) {
        const std::string base = LuaIdentifier(irName);
        const std::string name =
            Unique(base, [&](const std::string& n) { return names.taken.contains(n) || ReservedNames().contains(n); });
        if (name != base) ++out.suffixedNames;
        names.taken.insert(name);
        return name;
    }

    // A global this text declares again under an earlier text's name and never names: every use of the name finds
    // the earlier one (the plain name is looked up first), so the Lua leaves it out.
    bool Hidden(const scriptir::Variable& global) const {
        if (found.used.contains(global.name)) return false;
        const std::string plain = StripQualifier(global.name);
        return std::ranges::any_of(names.globals,
                                   [&](const auto& entry) { return StripQualifier(entry.first) == plain; });
    }

    void NameDeclarations() {
        for (const scriptir::Variable& global : file.globals) {
            if (names.globals.contains(global.name)) continue;
            if (Hidden(global)) hidden.insert(global.name);
            else names.globals[global.name] = Allocate(global.name);
        }
        for (const scriptir::Definition& definition : file.definitions)
            if (!names.scripts.contains(definition.name)) names.scripts[definition.name] = Allocate(definition.name);
    }

    std::string Known(const std::map<std::string, std::string>& map, const std::string& irName) {
        if (const auto entry = map.find(irName); entry != map.end()) return entry->second;
        ++out.unresolvedNames;
        return LuaIdentifier(irName);
    }
    std::string GlobalName(const std::string& irName) { return Known(names.globals, irName); }
    std::string ScriptName(const std::string& irName) { return Known(names.scripts, irName); }

    // A local name (a parameter or a member) that hides no global or script name, nor a local in scope.
    std::string LocalName(const std::string& base) {
        return Unique(base, [&](const std::string& n) {
            return names.taken.contains(n) || ReservedNames().contains(n) ||
                   std::ranges::find(locals, n) != locals.end();
        });
    }

    // ---- Output -------------------------------------------------------------------------------------------------

    void Line(const std::string& text) {
        if (!text.empty()) out.text.append(static_cast<std::size_t>(depth) * 4, ' ');
        out.text += text;
        out.text += '\n';
    }

    void Globals() {
        if (file.globals.empty()) return;
        Line("");
        Line("GlobalVars {");
        ++depth;
        for (const scriptir::Variable& global : file.globals) {
            const std::string plain = StripQualifier(global.name);
            if (hidden.contains(global.name)) {
                Line(std::format("-- {}: {}, declared again (every use finds the earlier script's)", plain,
                                 ScriptTypeName(global.type)));
                continue;
            }
            const std::string& name = names.globals.at(global.name);
            Line(std::format("{} = \"{}\",{}", name, ScriptTypeName(global.type),
                             name == plain ? "" : "   -- " + plain));
        }
        --depth;
        Line("}");
    }

    static std::string ParamsComment(const std::vector<scriptir::Variable>& list) {
        std::string text;
        for (const scriptir::Variable& param : list) {
            if (!text.empty()) text += ", ";
            text += std::format("{}: {}", param.name, ScriptTypeName(param.type));
        }
        return text;
    }

    void Definition(const scriptir::Definition& definition) {
        params.clear();
        locals.clear();
        std::string list;
        for (const scriptir::Variable& param : definition.params) {
            const std::string base = LuaIdentifier(param.name);
            const std::string name = LocalName(base);
            if (name != base) ++out.renamedParams;
            params[param.name] = name;
            locals.push_back(name);
            if (!list.empty()) list += ", ";
            list += name;
        }
        Line("");
        Line(std::format("-- {}({})", StripQualifier(definition.name), ParamsComment(definition.params)));
        const std::string name = ScriptName(definition.name);
        if (definition.rules.empty()) {
            Line(name + " = Script {}");
            return;
        }
        Line(name + " = Script {");
        ++depth;
        for (const scriptir::Rule& rule : definition.rules) {
            Line("{");
            ++depth;
            if (!rule.conditions.calls.empty()) {
                Line(std::format("condition = function({})", list));
                ++depth;
                for (std::size_t i = 0; i < rule.conditions.calls.size(); ++i)
                    Line(std::format("{}{} ~= 0", i == 0 ? "return " : "    and ",
                                     Expression(rule.conditions.calls[i])));
                --depth;
                Line("end,");
            }
            Line(std::format("action = function({})", list));
            Commands(rule.commands);
            Line("end,");
            --depth;
            Line("},");
        }
        --depth;
        Line("}");
    }

    void World(const scriptir::Block& block, std::size_t index) {
        params.clear();
        locals.clear();
        Line("");
        if (index > 0) {
            Line("-- A second WorldScript, which the game's parser refuses.");
            return;
        }
        if (!file.ok)
            Line("-- The game does not run it at load (the text failed), only when the zone is entered again.");
        Line("function WorldScript()");
        Commands(block);
        Line("end");
    }

    void Commands(const scriptir::Block& block) {
        ++depth;
        for (const scriptir::Call& call : block.calls) Command(call);
        --depth;
    }

    // ---- Calls --------------------------------------------------------------------------------------------------

    static std::string FunctionName(ScriptFunction id) {
        const char* keyword = KeywordName(static_cast<ScriptLexeme>(id));
        const char* lua = keyword ? LuaName(keyword) : nullptr;
        return lua ? lua : std::format("UnknownKeyword{}", static_cast<int>(id));
    }

    std::string Value(const scriptir::Arg& arg) {
        switch (arg.kind) {
        case scriptir::ArgKind::Number:
            return LuaNumber(arg.number);
        case scriptir::ArgKind::Text:
            return LuaString(arg.text);
        case scriptir::ArgKind::Local:
            return Known(params, arg.name);
        case scriptir::ArgKind::Global:
            return GlobalName(arg.name);
        case scriptir::ArgKind::Call:
            return Expression(*arg.call);
        }
        return "nil";
    }

    // The counter's Lua parameter: the counter's own name where no text reads the global outside its loops, else
    // `member`, the function writing the global first. ForIf's condition and body share one counter: `count` once.
    Member OpenMember(const scriptir::Arg& counter, bool count) {
        Member member{counter.name, GlobalName(counter.name)};
        if (count) ++out.loops;
        if (names.counterReads.contains(counter.name)) {
            if (count) ++out.countersWritten;
            member.name = LocalName("member");
        } else {
            names.unwrittenCounters.insert(counter.name);
        }
        locals.push_back(member.name);
        return member;
    }
    // The statement writing the counter global, or nothing where the parameter is the counter's name.
    std::string CounterWrite(const Member& member) {
        const std::string global = GlobalName(member.counter);
        return member.name == global ? "" : std::format("{} = {}", global, member.name);
    }
    void CloseMember() { locals.pop_back(); }

    // A member condition: a function of the member where the game evaluated a nested call for each, else the value.
    std::string MemberCondition(const scriptir::Arg& counter, const scriptir::Arg& condition, bool count) {
        if (condition.kind != scriptir::ArgKind::Call) return Value(condition);
        ++out.argumentFunctions;
        const Member member = OpenMember(counter, count);
        std::string write = CounterWrite(member);
        if (!write.empty()) write += ' ';
        std::string text = std::format("function({}) {}return {} end", member.name, write, Value(condition));
        CloseMember();
        return text;
    }

    std::string Expression(const scriptir::Call& call) {
        std::string args;
        const auto add = [&](const std::string& value) {
            if (!args.empty()) args += ", ";
            args += value;
        };
        if (IsAggregate(call.id) && HasCounter(call)) {
            for (std::size_t i = 1; i < call.args.size(); ++i)
                add(IsMemberCondition(call, call.args[i]) ? MemberCondition(call.args.front(), call.args[i], true)
                                                          : Value(call.args[i]));
        } else {
            for (const scriptir::Arg& arg : call.args) add(Value(arg));
        }
        return std::format("{}({})", FunctionName(call.id), args);
    }

    std::string ArgumentList(const scriptir::Call& call) {
        std::string args;
        for (const scriptir::Arg& arg : call.args) {
            if (!args.empty()) args += ", ";
            args += Value(arg);
        }
        return args;
    }

    void Command(const scriptir::Call& call) {
        switch (call.id) {
        case ScriptFunction::CallScript:
            Line(std::format("{}({})", ScriptName(call.targetName), ArgumentList(call)));
            return;
        case ScriptFunction::Assign: {
            std::string value = call.args.empty() ? "nil" : Value(call.args.front());
            if (call.targetType == ScriptType::Group && !call.args.empty() &&
                call.args.front().kind != scriptir::ArgKind::Call) {
                ++out.groupCopies;
                value = std::format("CopyGroup({})", value);
            }
            Line(std::format("{} = {}", GlobalName(call.targetName), value));
            return;
        }
        case ScriptFunction::For:
        case ScriptFunction::ForIf:
            if (HasCounter(call) && call.body) {
                WriteLoop(call);
                return;
            }
            break;
        default:
            break;
        }
        if (IsWait(call.id)) {
            // The game read a variable or a nested call again on every poll. A settled global's value is passed: from
            // its zero value (no wait of Sleep, WaitSegment or SleepUntilIdle waits on it) it changes once, by its one
            // assignment, so no such wait sees it change.
            std::string args;
            for (const scriptir::Arg& arg : call.args) {
                if (!args.empty()) args += ", ";
                const bool settled = arg.kind == scriptir::ArgKind::Global && call.id != ScriptFunction::SleepUntil &&
                                     !names.unsettled.contains(arg.name);
                if (settled) names.settledReads.insert(arg.name);
                if (arg.kind == scriptir::ArgKind::Call || (arg.kind == scriptir::ArgKind::Global && !settled)) {
                    ++out.argumentFunctions;
                    args += std::format("function() return {} end", Value(arg));
                } else {
                    args += Value(arg);
                }
            }
            Line(std::format("{}({})", FunctionName(call.id), args));
            return;
        }
        Line(Expression(call));
    }

    void WriteLoop(const scriptir::Call& call) {
        const scriptir::Arg& counter = call.args.front();
        std::string head = Value(call.args[1]);
        if (call.id == ScriptFunction::ForIf && call.args.size() > 2)
            head += ", " + MemberCondition(counter, call.args[2], false);
        const Member member = OpenMember(counter, true);
        Line(std::format("{}({}, function({})", FunctionName(call.id), head, member.name));
        if (const std::string write = CounterWrite(member); !write.empty()) {
            ++depth;
            Line(write);
            --depth;
        }
        Commands(*call.body);
        CloseMember();
        Line("end)");
    }

    const scriptir::File& file;
    LuaNames& names;
    LuaText& out;
    ReadFinder found;
    std::set<std::string> hidden;   // the declared globals the Lua leaves out (Hidden)
    int depth = 0;
    std::map<std::string, std::string> params;   // the definition's, by the tree's name
    std::vector<std::string> locals;             // the Lua names in scope
};

}   // namespace

std::string LuaIdentifier(std::string_view name) {
    std::string text = StripQualifier(name);
    for (char& c : text) {
        const bool word = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!word) c = '_';
    }
    if (text.empty() || (text[0] >= '0' && text[0] <= '9')) text.insert(text.begin(), '_');
    return text;
}

const std::set<std::string>& ReservedNames() {
    static const std::set<std::string> reserved = [] {
        std::set<std::string> names(std::begin(kLuaKeywords), std::end(kLuaKeywords));
        names.insert(std::begin(kLuaGlobals), std::end(kLuaGlobals));
        for (const BindingName& binding : BindingNames()) names.insert(binding.lua);
        return names;
    }();
    return reserved;
}

LuaText WriteLua(const scriptir::File& file, LuaNames& names, std::string_view source) {
    LuaText out;
    Writer(file, names, out).Write(source);
    return out;
}

}   // namespace scriptlua
