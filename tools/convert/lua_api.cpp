// ei-convert --lua-api, --lua-api-docs: see lua_api.h.
#include "lua_api.h"

#include "game/script/script_lua_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace lua_api {

namespace {

namespace fs = std::filesystem;
using luaapi::Kind;
using luaapi::kRows;
using luaapi::Row;

constexpr const char* kSource = "src/game/script/script_lua_api.inc";
constexpr const char* kShippedCopy = "examples/lua_api/ei_api.lua";
constexpr const char* kDoc = "docs/modding.md";

std::string Trim(std::string_view text) {
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(' ') - first + 1));
}

bool IsWordStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool IsWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }

// The classes the Field rows make, in their first row's order.
std::vector<std::string> Owners() {
    std::vector<std::string> owners;
    for (const Row& row : kRows)
        if (row.kind == Kind::Field && std::find(owners.begin(), owners.end(), row.owner) == owners.end())
            owners.emplace_back(row.owner);
    return owners;
}

// The declarations' short type names as LuaLS's.
constexpr std::pair<std::string_view, std::string_view> kShortTypes[] = {
    {"object", "ei.Object"}, {"group", "ei.Group"}, {"point", "ei.Point"}, {"position", "ei.Position"},
    {"cast", "ei.Cast"},     {"stats", "ei.Stats"}, {"effect", "ei.Effect"}, {"stat", "ei.StatName"},
};

// A declared type as LuaLS writes it: the short names and the Field rows' classes under ei. (an argument's name in a
// fun() or a table type stays); `object` alone may be nil.
std::string LuaLsType(std::string_view type) {
    static const std::vector<std::string> owners = Owners();
    if (type == "object") return "ei.Object?";
    std::string out;
    for (std::size_t i = 0; i < type.size();) {
        const char c = type[i];
        if (c == '"') {
            const std::size_t end = type.find('"', i + 1);
            out.append(type.substr(i, end + 1 - i));
            i = end + 1;
            continue;
        }
        if (!IsWordStart(c)) {
            out += c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < type.size() && IsWordChar(type[j])) ++j;
        const std::string_view word = type.substr(i, j - i);
        std::size_t k = j;
        while (k < type.size() && type[k] == ' ') ++k;
        if (k < type.size() && type[k] == '?') ++k;
        const bool name = k < type.size() && type[k] == ':';
        std::string mapped(word);
        if (!name) {
            for (const auto& [shortName, full] : kShortTypes)
                if (word == shortName) mapped = full;
            if (std::find(owners.begin(), owners.end(), word) != owners.end()) mapped = "ei." + std::string(word);
        }
        out += mapped;
        i = j;
    }
    return out;
}

// "name: type, ..." for a fun() type.
std::string FunParams(const std::vector<Param>& params) {
    std::string out;
    for (const Param& param : params) {
        if (!out.empty()) out += ", ";
        out += param.vararg ? "..." : param.name + (param.optional ? "?" : "");
        out += ": " + LuaLsType(param.type);
    }
    return out;
}

// ---- The definition file --------------------------------------------------------------------------------------

void Comment(std::string& out, std::string_view text) {
    if (!text.empty()) out += "---" + std::string(text) + "\n";
}

// A function's annotations and its empty body: `owner` "Cast:" for a method, "" for a global.
void Function(std::string& out, const Row& row, std::string_view owner, std::string_view text) {
    const std::vector<std::vector<Param>> forms = Forms(row.params);
    Comment(out, text);
    std::string names;
    if (!forms.empty())
        for (const Param& param : forms[0]) {
            out += "---@param " + (param.vararg ? std::string("...") : param.name + (param.optional ? "?" : "")) + " " +
                   LuaLsType(param.type) + "\n";
            if (!names.empty()) names += ", ";
            names += param.vararg ? "..." : param.name;
        }
    for (std::size_t f = 1; f < forms.size(); ++f) out += "---@overload fun(" + FunParams(forms[f]) + ")\n";
    if (*row.returns) out += "---@return " + LuaLsType(row.returns) + "\n";
    out += "function " + std::string(owner) + row.name + "(" + names + ") end\n\n";
}

void Fields(std::string& out, std::string_view owner) {
    const bool options = owner.ends_with("Options") || owner.ends_with("Filter");
    for (const Row& row : kRows)
        if (row.kind == Kind::Field && owner == row.owner) {
            std::string type = LuaLsType(row.params);
            if (options && type.ends_with('?')) type.pop_back();   // the field's own ? says it
            out += std::string("---@field ") + row.name + (options ? "? " : " ") + type +
                   (*row.text ? " # " + std::string(row.text) : "") + "\n";
        }
}

// The text of a binding with the game's keyword where its Lua name differs.
std::string BindingText(const Row& row) {
    std::string text = row.text;
    if (row.kind == Kind::Binding && std::string_view(row.owner) != row.name)
        text += std::string(" (the game's `") + row.owner + "`)";
    return text;
}

}   // namespace

std::vector<std::string> SplitTop(std::string_view text, char separator) {
    std::vector<std::string> parts;
    int depth = 0;
    bool quoted = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : separator;
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == '(' || c == '{' || c == '[' || c == '<') ++depth;
        if (c == ')' || c == '}' || c == ']' || c == '>') --depth;
        if (c == separator && depth == 0) {
            const std::string part = Trim(text.substr(start, i - start));
            if (!part.empty()) parts.push_back(part);
            start = i + 1;
        }
    }
    return parts;
}

std::vector<std::vector<Param>> Forms(std::string_view params) {
    std::vector<std::vector<Param>> forms;
    for (const std::string& form : SplitTop(params, ';')) {
        std::vector<Param>& out = forms.emplace_back();
        for (const std::string& text : SplitTop(form, ',')) {
            Param param;
            const std::size_t colon = text.find(':');
            param.name = Trim(std::string_view(text).substr(0, colon));
            param.type = colon == std::string::npos ? "" : Trim(std::string_view(text).substr(colon + 1));
            if (param.name.ends_with('?')) {
                param.optional = true;
                param.name.pop_back();
            }
            param.vararg = param.name == "...";
            out.push_back(std::move(param));
        }
    }
    if (forms.empty()) forms.emplace_back();
    return forms;
}

std::string DefinitionFile() {
    std::string out =
        "---@meta\n"
        "-- The Evil Islands rebuild's Lua API: the mission scripts' functions, the spells' cast value, methods and\n"
        "-- hooks, the active effects' stats. For a Lua language server (VS Code's Lua extension and others):\n"
        "-- docs/modding.md, \"Lua in an editor\".\n"
        "-- Generated by ei-convert --lua-api from " + std::string(kSource) + ". Do not edit.\n\n"
        "---A map object: a unit, an item, a lever, ... A function that takes one takes nil for none.\n"
        "---@class ei.Object\n\n"
        "---A group of objects (a value shared as a table is: CopyGroup copies it): #group, group[i] from 1.\n"
        "---@class ei.Group\n"
        "---@field [integer] ei.Object?\n"
        "---@operator len: integer\n\n"
        "---A point or an object (where it stands).\n"
        "---@alias ei.Position ei.Point|ei.Object\n\n"
        "---A stats total's name (OnStats).\n"
        "---@alias ei.StatName\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::Stat) out += std::string("---| \"") + row.name + "\" # " + row.text + "\n";
    out += "\n";
    for (const std::string& owner : Owners()) {
        if (owner == "Cast" || owner == "Effect") continue;
        out += "---@class ei." + owner + "\n";
        Fields(out, owner);
        out += "\n";
    }

    out += "---A cast of a scripted spell: the same table at every hook of the cast, its fields brought up to date "
           "before each.\n---@class ei.Cast\n";
    Fields(out, "Cast");
    out += "local Cast = {}\n\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::CastMethod) Function(out, row, "Cast:", row.text);

    out += "---OnStats' totals.\n---@class ei.Stats\nlocal Stats = {}\n\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::StatsMethod) Function(out, row, "Stats:", row.text);
    out += "---OnStats' effect: one kind of active effect on a unit.\n---@class ei.Effect\n";
    Fields(out, "Effect");
    out += "\n";

    out += "---A spell file's table (Res\\spells\\<file>.lua returns it; annotate it `---@type ei.Spell`): the "
           "hooks, each optional, and the timers cast:After names.\n---@class ei.Spell\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::Hook)
            out += std::string("---@field ") + row.name + "? fun(" + FunParams(Forms(row.params)[0]) + ") # " +
                   row.text + "\n";
    out += "---@field [string] fun(cast: ei.Cast, payload: any)\n\n";

    out += "-- The mission scripts' functions (also in the spells' hooks).\n\n";
    for (const Row& row : kRows) {
        if (row.kind == Kind::Section) out += std::string("-- ---- ") + row.name + "\n\n";
        if (row.kind == Kind::Binding || row.kind == Kind::Global || row.kind == Kind::Helper)
            Function(out, row, "", BindingText(row));
    }
    while (out.ends_with("\n\n")) out.pop_back();
    return out;
}

// ---- docs/modding.md's tables ------------------------------------------------------------------------------------

namespace {

// A table cell: a | escaped (also inside code).
std::string Cell(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '|') out += '\\';
        out += c;
    }
    return out;
}

std::string Code(std::string_view text) { return text.empty() ? "" : "`" + Cell(text) + "`"; }

// `prefix` + "name(params)" for each form, a line apart.
std::string Call(const Row& row, std::string_view prefix) {
    std::string out;
    for (const std::string& form : SplitTop(row.params, ';')) {
        if (!out.empty()) out += "<br>";
        out += Code(std::string(prefix) + row.name + "(" + form + ")");
    }
    if (out.empty()) out = Code(std::string(prefix) + row.name + "()");
    return out;
}

std::string FunctionsBlock() {
    std::string out;
    for (const Row& row : kRows) {
        if (row.kind == Kind::Section) {
            if (!out.empty()) out += "\n";
            out += std::string("#### ") + row.name + "\n\n| Function | Gives | What it does |\n|---|---|---|\n";
        }
        if (row.kind != Kind::Binding && row.kind != Kind::Global && row.kind != Kind::Helper) continue;
        std::string call = Call(row, "");
        if (row.kind == Kind::Binding && std::string_view(row.owner) != row.name)
            call += " (`" + std::string(row.owner) + "`)";
        out += "| " + call + " | " + Code(row.returns) + " | " + Cell(row.text) + " |\n";
    }
    return out;
}

std::string HooksBlock() {
    std::string out = "| Hook | When it runs |\n|---|---|\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::Hook) {
            std::string names;
            const std::vector<std::vector<Param>> forms = Forms(row.params);
            for (const Param& param : forms[0]) names += (names.empty() ? "" : ", ") + param.name;
            out += "| " + Code(std::string(row.name) + "(" + names + ")") + " | " + Cell(row.text) + " |\n";
        }
    return out;
}

std::string FieldsBlock(std::string_view owner, std::string_view prefix, std::string_view heading) {
    std::string out = "| " + std::string(heading) + " | Type | What it holds |\n|---|---|---|\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::Field && owner == row.owner)
            out += "| " + Code(std::string(prefix) + row.name) + " | " + Code(row.params) + " | " + Cell(row.text) +
                   " |\n";
    return out;
}

std::string MethodsBlock(Kind kind, std::string_view prefix) {
    std::string out = "| Method | Gives | What it does |\n|---|---|---|\n";
    for (const Row& row : kRows)
        if (row.kind == kind)
            out += "| " + Call(row, prefix) + " | " + Code(row.returns) + " | " + Cell(row.text) + " |\n";
    return out;
}

std::string OptionsBlock() {
    std::string out = "| Table | Key | Type | What it does |\n|---|---|---|---|\n";
    for (const std::string& owner : Owners()) {
        if (owner == "Cast" || owner == "Effect" || owner == "Point") continue;
        bool first = true;
        for (const Row& row : kRows)
            if (row.kind == Kind::Field && owner == row.owner) {
                out += "| " + (first ? Code(owner) : std::string()) + " | " + Code(row.name) + " | " +
                       Code(row.params) + " | " + Cell(row.text) + " |\n";
                first = false;
            }
    }
    return out;
}

std::string StatsBlock() {
    std::string out = "| Stat | What the game does with it |\n|---|---|\n";
    for (const Row& row : kRows)
        if (row.kind == Kind::Stat) out += "| " + Code(row.name) + " | " + Cell(row.text) + " |\n";
    return out;
}

// The blocks by their marker's name.
std::vector<std::pair<std::string, std::string>> Blocks() {
    return {
        {"functions", FunctionsBlock()},
        {"hooks", HooksBlock()},
        {"cast-fields", FieldsBlock("Cast", "cast.", "Field")},
        {"cast-methods", MethodsBlock(Kind::CastMethod, "cast:")},
        {"options", OptionsBlock()},
        {"effect-fields", FieldsBlock("Effect", "effect.", "Field")},
        {"stats-methods", MethodsBlock(Kind::StatsMethod, "stats:")},
        {"stats", StatsBlock()},
    };
}

// The document with every block's lines replaced; false with the problem in *error.
bool Document(const std::string& text, std::string* out, std::string* error) {
    out->clear();
    std::size_t at = 0;
    std::vector<std::string> seen;
    const std::vector<std::pair<std::string, std::string>> blocks = Blocks();
    constexpr std::string_view kOpen = "<!-- lua-api: ";
    constexpr std::string_view kClose = "<!-- /lua-api -->";
    while (true) {
        const std::size_t open = text.find(kOpen, at);
        if (open == std::string::npos) break;
        const std::size_t nameEnd = text.find(" -->\n", open);
        const std::size_t close = text.find(kClose, open);
        if (nameEnd == std::string::npos || close == std::string::npos) {
            *error = "a lua-api block without its end";
            return false;
        }
        const std::string name = text.substr(open + kOpen.size(), nameEnd - open - kOpen.size());
        const auto block = std::find_if(blocks.begin(), blocks.end(), [&](const auto& b) { return b.first == name; });
        if (block == blocks.end() || std::find(seen.begin(), seen.end(), name) != seen.end()) {
            *error = "lua-api block " + name + ": unknown or twice";
            return false;
        }
        seen.push_back(name);
        out->append(text, at, nameEnd + 5 - at);
        *out += "\n" + block->second + "\n";
        at = close;
    }
    out->append(text, at);
    for (const auto& [name, block] : blocks)
        if (std::find(seen.begin(), seen.end(), name) == seen.end()) {
            *error = "no lua-api block " + name;
            return false;
        }
    return true;
}

bool ReadFile(const fs::path& path, std::string* text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    text->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFile(const fs::path& path, const std::string& text) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

}   // namespace

int WriteDefinitions(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::printf("usage: ei-convert --lua-api <file>\n");
        return 2;
    }
    if (!WriteFile(fs::absolute(args[0]), DefinitionFile())) {
        std::printf("lua_api: cannot write %s\n", args[0].c_str());
        return 1;
    }
    return 0;
}

int WriteDocs(const std::vector<std::string>& args) {
    const bool check = args.size() == 2 && args[1] == "--check";
    if (args.size() != 1 && !check) {
        std::printf("usage: ei-convert --lua-api-docs <repository> [--check]\n");
        return 2;
    }
    const fs::path root = fs::absolute(args[0]);
    std::string doc, written, error, shipped;
    if (!ReadFile(root / kDoc, &doc) || !Document(doc, &written, &error)) {
        std::printf("lua_api: %s: %s\n", kDoc, error.empty() ? "cannot read it" : error.c_str());
        return 1;
    }
    const std::string definitions = DefinitionFile();
    if (check) {
        int problems = 0;
        if (written != doc) {
            std::printf("lua_api: %s's Lua API tables are out of date: ei-convert --lua-api-docs <repository>\n", kDoc);
            ++problems;
        }
        if (!ReadFile(root / kShippedCopy, &shipped) || shipped != definitions) {
            std::printf("lua_api: %s is out of date: ei-convert --lua-api-docs <repository>\n", kShippedCopy);
            ++problems;
        }
        return problems ? 1 : 0;
    }
    if (!WriteFile(root / kDoc, written) || !WriteFile(root / kShippedCopy, definitions)) {
        std::printf("lua_api: cannot write into %s\n", root.string().c_str());
        return 1;
    }
    return 0;
}

}   // namespace lua_api
