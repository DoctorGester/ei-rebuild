// The readable Lua of a script text. See script_lua_writer.h and docs/notes/lua-scripting.md, "Converted scripts".
#include "game/script/script_lua_writer.h"

#include "engine/core/asset_text.h"
#include "game/script/script_lua.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <iterator>
#include <utility>
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

// Every global named at all, the assignments of each and the loop counters.
class UseFinder {
public:
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
        for (const scriptir::Arg& arg : call.args) {
            if (counting && &arg == &call.args.front()) counters.insert(arg.name);
            if (arg.kind == scriptir::ArgKind::Global) used.insert(arg.name);
            if (arg.kind == scriptir::ArgKind::Call) Call(*arg.call);
        }
        if (call.body) Block(*call.body);
    }
};

bool IsUtf8(std::string_view bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > bytes.size()) return false;
        for (std::size_t k = 1; k < length; ++k)
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80) return false;
        i += length;
    }
    return true;
}

// A comment's lines as Lua comment text: UTF-8 (a script's bytes are the asset code page's unless they are UTF-8 already),
// each line trimmed, blank lines at the ends dropped.
std::vector<std::string> CommentLines(const std::string& bytes) {
    const std::string text = IsUtf8(bytes) ? bytes : text::FromAssetText(bytes);
    std::vector<std::string> lines;
    for (std::size_t at = 0; at <= text.size();) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string_view line = std::string_view(text).substr(at, end - at);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\r')) line.remove_prefix(1);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
        lines.emplace_back(line);
        at = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    lines.erase(lines.begin(), std::find_if(lines.begin(), lines.end(), [](const std::string& line) { return !line.empty(); }));
    return lines;
}

class Writer {
public:
    Writer(const scriptir::File& file, LuaNames& names, LuaText& out) : file(file), names(names), out(out) {}

    void Write(std::string_view source) {
        FindUses();
        NameDeclarations();
        PlaceComments();
        Line(HeaderLine(source));
        if (!file.ok) {
            Line(std::format(
                "-- The game's parser stops at line {} of the original: {}. What came before stays loaded, as "
                "in the game.",
                file.error.line, file.error.text));
        }
        Globals();
        for (const scriptir::Definition& definition : file.definitions) Definition(definition);
        for (std::size_t i = 0; i < file.worldScripts.size(); ++i) World(file.worldScripts[i], i);
        Leftover();
    }

    // `return function(params)` and the block's calls from `from` on.
    void Function(const scriptir::Block& block, std::size_t from, const std::vector<scriptir::Variable>& list) {
        Line(std::format("return function({})", OpenParams(list)));
        Commands(block, from);
        Line("end");
    }

private:
    // ---- Names --------------------------------------------------------------------------------------------------

    void FindUses() {
        UseFinder& finder = found;
        for (const scriptir::Block& world : file.worldScripts) finder.Block(world);
        for (const scriptir::Definition& definition : file.definitions) {
            for (const scriptir::Rule& rule : definition.rules) {
                finder.Block(rule.conditions);
                finder.Block(rule.commands);
            }
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

    // ---- Comments -----------------------------------------------------------------------------------------------

    // The comments of the text beside the tree's items (a global, a definition, a rule, a call, a WorldScript), kept
    // by line: a comment on a line of its own goes before the next item at or after its line; one after a token goes at
    // the end of the line of the last item that starts at or before its line (the item it follows); none such: before the
    // next item. What follows the last item goes at the end of the Lua.
    struct Notes {
        std::vector<std::string> before;   // lines
        std::string after;                 // one line
        bool written = false;              // the lines before were written
        bool afterWritten = false;
    };

    struct Item {
        int line = 0;
        const void* key = nullptr;
    };

    static void CollectCalls(const scriptir::Block& block, std::vector<Item>& items) {
        for (const scriptir::Call& call : block.calls) {
            items.push_back({call.line, &call});
            if (call.body) CollectCalls(*call.body, items);
        }
    }

    void PlaceComments() {
        if (file.comments.empty()) return;
        std::vector<Item> items;
        for (const scriptir::Variable& global : file.globals) items.push_back({global.line, &global});
        for (const scriptir::Definition& definition : file.definitions) {
            items.push_back({definition.bodyLine != 0 ? definition.bodyLine : definition.line, &definition});
            for (const scriptir::Rule& rule : definition.rules) {
                items.push_back({rule.line, &rule});
                CollectCalls(rule.conditions, items);
                CollectCalls(rule.commands, items);
            }
        }
        for (std::size_t i = 0; i < file.worldScripts.size(); ++i) {
            const scriptir::Block& world = file.worldScripts[i];
            items.push_back({world.line, &world});
            if (i == 0) CollectCalls(world, items);
        }
        std::ranges::stable_sort(items, {}, &Item::line);
        for (const ScriptComment& comment : file.comments) {
            const std::vector<std::string> lines = CommentLines(comment.text);
            // The last item at or before the line, for a comment after a token; else the first at or after it.
            const auto following = std::ranges::upper_bound(items, comment.line, {}, &Item::line);
            if (comment.trailing && following != items.begin()) {
                std::string& note = notes[std::prev(following)->key].after;
                for (const std::string& line : lines) {
                    if (line.empty()) continue;
                    note += note.empty() ? line : " " + line;
                }
                continue;
            }
            const auto next = std::ranges::lower_bound(items, comment.line, {}, &Item::line);
            // After a failed parse, what lies past the error was not converted.
            if (next == items.end() && !file.ok && comment.line > file.error.line) continue;
            std::vector<std::string>& target = next == items.end() ? tail : notes[next->key].before;
            if (lines.empty()) target.push_back("");
            for (const std::string& line : lines) target.push_back(line);
        }
    }

    // The comments before an item, written where it starts.
    void Before(const void* key) {
        const auto note = notes.find(key);
        if (note == notes.end()) return;
        note->second.written = true;
        for (const std::string& line : note->second.before) Line(line.empty() ? "--" : "-- " + line);
    }
    // The comment after an item, at the end of its first line, which starts at `from` in the text (or a line of its own
    // when the item wrote none).
    void After(const void* key, std::size_t from) {
        const auto note = notes.find(key);
        if (note == notes.end() || note->second.after.empty()) return;
        note->second.afterWritten = true;
        const std::size_t end = out.text.find('\n', from);
        if (end == std::string::npos) Line("-- " + note->second.after);
        else out.text.insert(end, " -- " + note->second.after);
    }
    // Comments beside items the Lua leaves out, and those after the last item, at the end.
    void Leftover() {
        std::vector<std::string> lines = tail;
        for (const auto& [key, note] : notes) {
            if (!note.written) lines.insert(lines.end(), note.before.begin(), note.before.end());
            if (!note.afterWritten && !note.after.empty()) lines.push_back(note.after);
        }
        if (lines.empty()) return;
        Line("");
        for (const std::string& line : lines) Line(line.empty() ? "--" : "-- " + line);
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
            Before(&global);
            const std::size_t at = out.text.size();
            if (hidden.contains(global.name)) {
                Line(std::format("-- {}: {}, declared again (every use finds the earlier script's)", plain,
                                 ScriptTypeName(global.type)));
            } else {
                const std::string& name = names.globals.at(global.name);
                Line(std::format("{} = \"{}\",{}", name, ScriptTypeName(global.type),
                                 name == plain ? "" : "   -- " + plain));
            }
            After(&global, at);
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

    // A function's parameters named (a parameter's Lua name hides no global or script) and in scope; their list.
    std::string OpenParams(const std::vector<scriptir::Variable>& variables) {
        params.clear();
        locals.clear();
        std::string list;
        for (const scriptir::Variable& param : variables) {
            const std::string base = LuaIdentifier(param.name);
            const std::string name = LocalName(base);
            if (name != base) ++out.renamedParams;
            params[param.name] = name;
            locals.push_back(name);
            if (!list.empty()) list += ", ";
            list += name;
        }
        return list;
    }

    void Definition(const scriptir::Definition& definition) {
        const std::string list = OpenParams(definition.params);
        Line("");
        Before(&definition);
        Line(std::format("-- {}({})", StripQualifier(definition.name), ParamsComment(definition.params)));
        const std::string name = ScriptName(definition.name);
        const std::size_t headAt = out.text.size();
        if (definition.rules.empty()) {
            Line(name + " = Script {}");
            After(&definition, headAt);
            return;
        }
        Line(name + " = Script {");
        After(&definition, headAt);
        ++depth;
        for (const scriptir::Rule& rule : definition.rules) {
            Before(&rule);
            const std::size_t ruleAt = out.text.size();
            Line("{");
            After(&rule, ruleAt);
            ++depth;
            if (!rule.conditions.calls.empty()) {
                Line(std::format("condition = function({})", list));
                ++depth;
                for (std::size_t i = 0; i < rule.conditions.calls.size(); ++i) {
                    const scriptir::Call& condition = rule.conditions.calls[i];
                    depth += i == 0 ? 0 : 1;   // a comment before a later condition lines up with its `and`
                    Before(&condition);
                    depth -= i == 0 ? 0 : 1;
                    const std::size_t at = out.text.size();
                    Line(std::format("{}{} ~= 0", i == 0 ? "return " : "    and ", Expression(condition)));
                    After(&condition, at);
                }
                --depth;
                Line("end,");
            }
            // An empty then-section starts no run (the game's ended at its first tick): no action.
            if (!rule.commands.calls.empty()) {
                Line(std::format("action = function({})", list));
                Commands(rule.commands);
                Line("end,");
            }
            --depth;
            Line("},");
        }
        --depth;
        Line("}");
    }

    void World(const scriptir::Block& block, std::size_t index) {
        params.clear();
        locals.clear();
        // A saved program's later WorldScripts (scriptir::Loaded) each run through a function of their own
        // (WriteFunction).
        if (index > 0 && file.ok) return;
        Line("");
        Before(&block);
        if (index > 0) {
            Line("-- A second WorldScript, which the game's parser refuses.");
            return;
        }
        // Its run would end at its first tick: nothing to start.
        if (block.calls.empty()) {
            Line("-- An empty WorldScript.");
            return;
        }
        if (!file.ok)
            Line("-- The game does not run it at load (the text failed), only when the zone is entered again.");
        const std::size_t at = out.text.size();
        Line("function WorldScript()");
        After(&block, at);
        Commands(block);
        Line("end");
    }

    void Commands(const scriptir::Block& block, std::size_t from = 0) {
        ++depth;
        for (std::size_t i = from; i < block.calls.size(); ++i) {
            const scriptir::Call& call = block.calls[i];
            Before(&call);
            const std::size_t at = out.text.size();
            Command(call);
            After(&call, at);
        }
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

    // The member's Lua parameter, in scope until CloseMember: the counter global's name, so that the function reads
    // it where the game read the global (the binding sets the global too, by the name the loop passes first).
    std::string OpenMember(const scriptir::Arg& counter) {
        std::string name = GlobalName(counter.name);
        locals.push_back(name);
        return name;
    }
    void CloseMember() { locals.pop_back(); }

    // A member condition: a function of the member where the game evaluated a nested call for each, else the value.
    std::string MemberCondition(const scriptir::Arg& counter, const scriptir::Arg& condition) {
        if (condition.kind != scriptir::ArgKind::Call) return Value(condition);
        ++out.argumentFunctions;
        const std::string member = OpenMember(counter);
        std::string text = std::format("function({}) return {} end", member, Value(condition));
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
            ++out.loops;
            add(LuaString(GlobalName(call.args.front().name)));
            for (std::size_t i = 1; i < call.args.size(); ++i)
                add(IsMemberCondition(call, call.args[i]) ? MemberCondition(call.args.front(), call.args[i])
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

    // `ForEach("counter", group, function(counter)` and the body: the game's argument order, the counter global by name.
    void WriteLoop(const scriptir::Call& call) {
        const scriptir::Arg& counter = call.args.front();
        ++out.loops;
        std::string head = LuaString(GlobalName(counter.name)) + ", " + Value(call.args[1]);
        if (call.id == ScriptFunction::ForIf && call.args.size() > 2)
            head += ", " + MemberCondition(counter, call.args[2]);
        const std::string member = OpenMember(counter);
        Line(std::format("{}({}, function({})", FunctionName(call.id), head, member));
        Commands(*call.body);
        CloseMember();
        Line("end)");
    }

    const scriptir::File& file;
    LuaNames& names;
    LuaText& out;
    UseFinder found;
    std::set<std::string> hidden;   // the declared globals the Lua leaves out (Hidden)
    std::map<const void*, Notes> notes;   // the comments beside each item (PlaceComments)
    std::vector<std::string> tail;        // those after the last item
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

std::string HeaderLine(std::string_view source) {
    return std::format("-- {}: converted by ei-convert from the game's script (docs/notes/lua-scripting.md, "
                       "\"Converted scripts\").",
                       source);
}

// Each set and map as its count and its entries, every string ended by a zero byte (no name holds one).
std::string SaveNames(const LuaNames& names) {
    std::string data;
    const auto add = [&data](std::string_view text) {
        data += text;
        data += '\0';
    };
    for (const std::map<std::string, std::string>* map : {&names.globals, &names.scripts}) {
        add(std::to_string(map->size()));
        for (const auto& [key, value] : *map) {
            add(key);
            add(value);
        }
    }
    for (const std::set<std::string>* set :
         {&names.taken, &names.unsettled, &names.settledReads}) {
        add(std::to_string(set->size()));
        for (const std::string& key : *set) add(key);
    }
    add(std::to_string(names.worldAssignments.size()));
    for (const auto& [key, count] : names.worldAssignments) {
        add(key);
        add(std::to_string(count));
    }
    return data;
}

bool LoadNames(std::string_view data, LuaNames* names) {
    *names = {};
    std::size_t at = 0;
    const auto next = [&](std::string* text) {
        const std::size_t end = data.find('\0', at);
        if (end == std::string_view::npos) return false;
        *text = data.substr(at, end - at);
        at = end + 1;
        return true;
    };
    const auto count = [&](std::size_t* n) {
        std::string text;
        if (!next(&text) || text.empty() || text.find_first_not_of("0123456789") != std::string::npos) return false;
        *n = std::stoull(text);
        return true;
    };
    std::size_t n = 0;
    std::string key, value;
    for (std::map<std::string, std::string>* map : {&names->globals, &names->scripts}) {
        if (!count(&n)) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!next(&key) || !next(&value)) return false;
            (*map)[key] = value;
        }
    }
    for (std::set<std::string>* set :
         {&names->taken, &names->unsettled, &names->settledReads}) {
        if (!count(&n)) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!next(&key)) return false;
            set->insert(key);
        }
    }
    if (!count(&n)) return false;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t assigned = 0;
        if (!next(&key) || !count(&assigned)) return false;
        names->worldAssignments[key] = static_cast<int>(assigned);
    }
    return at == data.size();
}

LuaText WriteLua(const scriptir::File& file, LuaNames& names, std::string_view source) {
    LuaText out;
    Writer(file, names, out).Write(source);
    return out;
}

std::string WriteFunction(const scriptir::Block& block, std::size_t from, const std::vector<scriptir::Variable>& params,
                          LuaNames& names) {
    static const scriptir::File noFile;
    LuaText out;
    Writer(noFile, names, out).Function(block, from, params);
    return out.text;
}

}   // namespace scriptlua
