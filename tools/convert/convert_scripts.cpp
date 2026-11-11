// ei-convert --scripts: the script survey and the Lua check (convert_scripts.h).
// usage: ei-convert --scripts [<install>...] [--scenarios <folder>] [--dump <folder>] [--lua <folder>] [--quiet]
//   <install>     an unpacked install (tools/unpack_install.py): every .mob under it, a mod's (starter\Mods\<mod>\...)
//                 counted as the mod's; a mob that another's AddMob names, or a network quest's (its archive's
//                 map.txt names its base), is loaded after that one into the same script
//   --scenarios   the harness scenarios (tests/scenarios): their `console @...`, `#...` and `$...` lines, each
//                 scenario's lines loaded one after another as the console does
//   --dump        each text and its tree's dump (with lines) written there, as <group>\<name>.txt and .ir
//   --lua         each text's Lua (script_lua_writer.h) written there, as <group>\<name>.lua
//   --quiet       only the Lua check's lines (the build check: the samples and the scenarios, no game data)
// The texts: each zone's script chunk (encoded or plain, read as WorldServer::LoadZoneScripts does), the quest texts
// QFinish would build from each mission's QStart / QObj* commands (loaded after the mission, into its script), the
// console lines, the texts the engine writes (a trap's mushroom, a lightning bolt, the heroes' group, a briefing's
// completion) with sample numbers, and samples of the parser's sections (kSamples). Every text's Lua is written and
// loaded into a Lua state after the earlier texts of its script, each global it reads or writes checked (CheckLua).
#include "convert_scripts.h"

#include "lua_globals.h"

#include "game/console/console.h"
#include "engine/core/core_streams.h"
#include "game/map/mob_script_decoder.h"
#include "game/script/script.h"
#include "game/script/script_ir.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_writer.h"
#include "lib/text_case.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace convert_scripts {

namespace {

constexpr std::uint32_t kScriptsChunk = 0xA000;
constexpr std::uint32_t kScriptEncoded = 0xACCEECCB;
constexpr std::uint32_t kScriptPlain = 0xACCEECCA;

// The engine's own texts, as their writers format them (traps.cpp, world_weather.cpp, world_units.cpp,
// world_quests.cpp), with sample numbers.
constexpr const char* kTrapText = "Worldscript( CreateParticleSource( 100, 54.5, 138.25, 12.75, 1, \"Mushroom\"))";
constexpr const char* kThunderText = "WorldScript(CreateFX( 10.000000,20.000000,30.000000,60,150,\"nature\\thunder\\2.wav\" ) )";
constexpr const char* kBoltText =
    "WorldScript(CreatePointLight(1,15.000000,25.000000,35.000000,80,255,255,255 ) "
    "CreateFX( 10.000000,20.000000,30.000000,60,150,\"nature\\lightning\\1.wav\" ) "
    "CreateLightning(1,10.000000,20.000000,30.000000,20.000000,30.000000,40.000000,-7)Sleep(1) DeleteLightning(1) Sleep(1)"
    "CreateLightning(2,10.000000,20.000000,30.000000,20.000000,30.000000,40.000000,-7)Sleep(1) DeleteLightning(2) "
    "DeletePointLight(1) )";
constexpr const char* kHeroesText =
    "WorldScript(AddObject( Heroes, GetObjectByID( \"2001\" ) )AddObject( Heroes, GetObjectByID( \"2002\" ) )"
    "PlayAnimation(GetObjectByName( \"hero\" ),\"uspecial25\"))";
constexpr const char* kBriefingText = "WorldScript( GSSetVar( 0,\"q.b.z1b1\",2 ) #OnBriefingComplete( 0, \"q.b.z1b1\" ) )";

// Texts loaded one after another into one script: defines, globals, a declaration with its body further on, rules
// with and without conditions, a loop, assignments, nested calls, Rebirth's optional and repeated parameters, a body
// that fails (its blocks dropped), names of the first text found qualified from the next.
constexpr const char* kSamples[] = {
    "#define SPOT 54.5\n#define TWICE Mul(SPOT, 2)\n"
    "GlobalVars( Heroes : group, i : object, n : float, word : string, u : object )\n"
    "DeclareScript Later( who : object, much : float )\n"
    "Script Now( k : float, t : string ) (\n"
    "  if ( IsLess( k, TWICE ) Not( IsEqualString( t, \"x\" ) ) ) then ( KillScript() Later( u, k ) n = Add( k, 1, 2 ) )\n"
    "  if () then ( For( i, Heroes ) ( Later( i, SPOT ) Sleep( 0 ) ) Sleep( 10 ) word = t )\n"
    ")\n"
    "Script Later ( if ( Any( i, Heroes, IsAlive( i ) ) IsLess( Random( 5 ), Random( 1, 6 ) ) ) then ( KillScript() u = who ) )\n"
    "WorldScript( Now( 1, \"a\" ) u = GetObjectByName( \"hero\" ) ForIf( i, Heroes, IsDead( i ) ) ( SleepUntil( 1 ) ) )\n",
    "GlobalVars( m : float )\nScript Again( x : float ) ( if () then ( m = x n = m Now( m, word ) ) )\n"
    "WorldScript( Again( n ) Later( u, m ) )\n",
    "Script Broken() ( if ( IsLess( 1, 2 ) ) then ( Later( u ) ) )\n",
};

// A text with comments of both kinds (the bytes of "Привет" in cp1251 in two of them) before and after a global, a
// definition, a rule, a condition, a command, the WorldScript and the end of the text; kCommentLua is its Lua.
constexpr const char* kCommentSample =
    "// head \xcf\xf0\xe8\xe2\xe5\xf2\n"
    "GlobalVars( n : float ) // after globals\n"
    "/* before the script */\n"
    "Script Greet( k : float ) (\n"
    "  // before the rule\n"
    "  if ( IsLess( k, 5 )\n"
    "       // before the second condition\n"
    "       IsLess( 0, k ) ) then (\n"
    "    // first command \xcf\xf0\xe8\xe2\xe5\xf2\n"
    "    n = Add( k, 1 ) // after the assignment\n"
    "    Sleep( 2 ) /* after the wait */\n"
    "  )\n"
    ")\n"
    "/* block\n"
    "   comment */\n"
    "WorldScript( Greet( 1 ) ) // after the WorldScript\n"
    "// last\n";

constexpr const char* kCommentLua =
    "-- comments: converted by ei-convert from the game's script (docs/notes/lua-scripting.md, \"Converted scripts\").\n"
    "\n"
    "GlobalVars {\n"
    "    -- head \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\n"
    "    n = \"float\", -- after globals\n"
    "}\n"
    "\n"
    "-- before the script\n"
    "-- Greet(k: float)\n"
    "Greet = Script {\n"
    "    -- before the rule\n"
    "    {\n"
    "        condition = function(k)\n"
    "            return IsLess(k, 5) ~= 0\n"
    "                -- before the second condition\n"
    "                and IsLess(0, k) ~= 0\n"
    "        end,\n"
    "        action = function(k)\n"
    "            -- first command \xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\n"
    "            n = Add(k, 1) -- after the assignment\n"
    "            Sleep(2) -- after the wait\n"
    "        end,\n"
    "    },\n"
    "}\n"
    "\n"
    "-- block\n"
    "-- comment\n"
    "function WorldScript()\n"
    "    Greet(1) -- after the WorldScript\n"
    "end\n"
    "\n"
    "-- last\n";

constexpr ScriptFunction kWaits[] = {ScriptFunction::Sleep, ScriptFunction::WaitSegment, ScriptFunction::SleepUntil,
                                     ScriptFunction::SleepUntilIdle};

bool IsWait(ScriptFunction id) { return std::ranges::find(kWaits, id) != std::end(kWaits); }
bool IsLoop(ScriptFunction id) { return id == ScriptFunction::For || id == ScriptFunction::ForIf; }
constexpr std::string_view kLuaKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
                                             "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true",
                                             "until", "while"};

bool IsCountingFunction(ScriptFunction id) {
    return id == ScriptFunction::Every || id == ScriptFunction::Any || id == ScriptFunction::Sum;
}

std::string Name(ScriptFunction id) { return KeywordName(static_cast<ScriptLexeme>(id)); }

std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::optional<std::string> ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream bytes;
    bytes << file.rdbuf();
    return bytes.str();
}

// A .mob's script text, found as WorldServer::LoadZoneScripts finds it; none when the file has no script chunk.
std::optional<std::string> MobScriptText(const std::string& bytes) {
    MemoryStream file;
    file.Fill(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    MemoryStream scripts;
    MemoryStream text;
    if (!file.FindTagged(kScriptsChunk, &scripts)) return std::nullopt;
    if (scripts.FindTagged(kScriptEncoded, &text)) {
        MobScriptDecoder decoder;
        decoder.input.assign(text.data.begin(), text.data.begin() + text.size);
        return decoder.Decode();
    }
    if (scripts.FindTagged(kScriptPlain, &text)) {
        std::string plain(text.data.begin(), text.data.begin() + text.size);
        return plain.substr(0, plain.find('\0'));
    }
    return std::nullopt;
}

// ---- What one text gives ------------------------------------------------------------------------------------------

struct Source {
    std::string group;      // vanilla, rebirth, a mod's name, quests, console, engine
    std::string name;       // file or scenario name
    std::string text;       // as loaded (before the preprocessing)
    bool encoded = false;
};

// Where a call sits.
struct Where {
    bool world = false;         // in a WorldScript block (else a then-section)
    bool conditions = false;    // in an if-section
    int loops = 0;              // For / ForIf bodies around it
    bool waited = false;        // a wait ran before it at the top level of its block: the run lost its kill flag
    bool killed = false;        // a KillScript ran before it in the same run, before any wait
    bool followed = false;      // a command ran after that KillScript
    int definition = -1;
};

struct Listing {
    std::vector<std::string> lines;
    void Add(std::string line, std::size_t cap = 40) {
        if (lines.size() < cap) lines.push_back(std::move(line));
        ++count;
    }
    int count = 0;
};

struct Stats {
    int files = 0;
    int lines = 0;
    int tokens = 0;
    int calls = 0;
    int failed = 0;
    int encoded = 0;
    int commentLines = 0;
    std::set<std::string> distinct;
    std::map<std::string, int> keywords;        // by table name
    std::map<std::string, int> conditionFunctions;
    std::map<std::string, int> argKinds;
    // Waits by place.
    std::map<std::string, int> waitsByKind;
    int waitsWorldTop = 0;
    int waitsRuleTop = 0;
    int waitsInLoop = 0;
    int loops = 0;
    int loopsWithWait = 0;
    std::map<int, int> loopDepths;               // depth of each For / ForIf (1 = not nested)
    int counterReuse = 0;                        // a nested loop or Every / Any / Sum on an enclosing loop's counter
    int callScripts = 0;
    int callScriptsInLoop = 0;
    int callScriptsInWorld = 0;
    int selfStarting = 0;                        // definitions that start themselves
    int cycles = 0;                              // other start cycles (two or more definitions)
    int definitions = 0;
    int rules = 0;
    int maxRules = 0;
    int globals = 0;
    int maxGlobals = 0;
    std::map<std::string, int> globalsByType;
    int defineFiles = 0;
    int defines = 0;
    int defineUses = 0;
    int unusedDefines = 0;
    int explicitQualified = 0;                   // `!n!` written in the text
    int qualifiedLookups = 0;                    // names that resolved only in their `!n!` form
    int assigns = 0;
    int kills = 0;
    int killsInWorld = 0;
    int killsAfterWait = 0;
    int killsInLoop = 0;
    int killsFollowed = 0;                       // effective KillScript with commands after it in the run
    int killedCallScripts = 0;                   // a declared script started after its starter's KillScript
    int killedWaits = 0;                         // a wait after KillScript (the run outlives its instance)
    int questTexts = 0;
    int questObjectives = 0;
    int loadedAfter = 0;                         // missions loaded after another (AddMob, a network quest's base)
    int unconditional = 0;                       // rules with an empty if-section
    int oneShot = 0;                             // one rule, no condition, KillScript first: a started task
    int killedFirst = 0;                         // definitions whose every rule starts with KillScript
    int neverKilled = 0;                         // definitions without a KillScript: polled until the zone ends
    int neverStarted = 0;                        // definitions no call of the same text starts
    int oddNames = 0;                            // names (scripts, globals, parameters) that are no Lua identifier
    int keywordCase = 0;                         // calls written in another case than the table's name
    Listing oddNameList;
    Listing waitsInLoops;
    Listing killedStarts;
    Listing errors;
    Listing recursion;
    Listing reuse;
    Listing qualified;
    Listing questArgs;
    Listing defineList;
};

std::map<std::string, Stats> g_stats;   // by group
Stats g_all;
fs::path g_dumpFolder;

std::string ArgText(const scriptir::Arg& arg) {
    switch (arg.kind) {
    case scriptir::ArgKind::Number: return std::format("{}", arg.number);
    case scriptir::ArgKind::Text: return "\"" + arg.text + "\"";
    case scriptir::ArgKind::Local:
    case scriptir::ArgKind::Global: return arg.name;
    case scriptir::ArgKind::Call: return Name(arg.call->id) + "(...)";
    }
    return {};
}

const char* ArgKindName(scriptir::ArgKind kind) {
    switch (kind) {
    case scriptir::ArgKind::Number: return "number";
    case scriptir::ArgKind::Text: return "string";
    case scriptir::ArgKind::Local: return "parameter";
    case scriptir::ArgKind::Global: return "global";
    case scriptir::ArgKind::Call: return "nested call";
    }
    return "";
}

// Counts one text's tree into `s` (and the group-less totals through the caller).
class Survey {
public:
    Survey(Stats& s, const Source& source, const scriptir::File& file) : s(s), source(source), file(file) {}

    void Run() {
        for (const scriptir::Block& world : file.worldScripts) {
            Where where;
            where.world = true;
            Commands(world, where);
        }
        for (const scriptir::Definition& definition : file.definitions) {
            ++s.definitions;
            s.rules += static_cast<int>(definition.rules.size());
            s.maxRules = std::max(s.maxRules, static_cast<int>(definition.rules.size()));
            Shape(definition);
            for (const scriptir::Rule& rule : definition.rules) {
                Where where;
                where.definition = definition.index;
                where.conditions = true;
                for (const scriptir::Call& call : rule.conditions.calls) Function(call, where);
                where.conditions = false;
                Commands(rule.commands, where);
            }
        }
        Recursion();
        for (const scriptir::Definition& definition : file.definitions)
            if (!started.contains(definition.index)) ++s.neverStarted;
        for (const scriptir::Variable& global : file.globals) Identifier(global.name, global.line);
        s.globals += static_cast<int>(file.globals.size());
        s.maxGlobals = std::max(s.maxGlobals, static_cast<int>(file.globals.size()));
        for (const scriptir::Variable& global : file.globals) ++s.globalsByType[ScriptTypeName(global.type)];
        if (!file.defines.empty()) ++s.defineFiles;
        for (const scriptir::Define& define : file.defines) {
            ++s.defines;
            s.defineUses += define.uses;
            if (define.uses == 0) ++s.unusedDefines;
            s.defineList.Add(std::format("{} line {}: #define {} {} ({} uses)", source.name, define.line, define.name, define.value,
                                         define.uses));
        }
    }

    // The quest texts QFinish would build from this tree's QStart / QObj* / QFinish commands, in block order.
    std::vector<std::string> questTexts;

private:
    // What kind of script a definition is: a started task, a once-firing trigger, a rule set polled for ever.
    void Shape(const scriptir::Definition& definition) {
        Identifier(definition.name, definition.line);
        for (const scriptir::Variable& param : definition.params) Identifier(param.name, param.line);
        bool allKilledFirst = !definition.rules.empty();
        bool anyKill = false;
        for (const scriptir::Rule& rule : definition.rules) {
            if (rule.conditions.calls.empty()) ++s.unconditional;
            const auto& calls = rule.commands.calls;
            if (calls.empty() || calls.front().id != ScriptFunction::KillScript) allKilledFirst = false;
            if (Contains(rule.commands, ScriptFunction::KillScript)) anyKill = true;
        }
        if (allKilledFirst) ++s.killedFirst;
        if (!anyKill) ++s.neverKilled;
        if (allKilledFirst && definition.rules.size() == 1 && definition.rules.front().conditions.calls.empty()) ++s.oneShot;
    }

    static bool Contains(const scriptir::Block& block, ScriptFunction id) {
        for (const scriptir::Call& call : block.calls)
            if (call.id == id || (call.body && Contains(*call.body, id))) return true;
        return false;
    }

    // A name a Lua writer cannot use as it is: not [A-Za-z_][A-Za-z0-9_]*, or a Lua keyword.
    void Identifier(const std::string& qualifiedName, int line) {
        std::string name = qualifiedName;
        if (name.starts_with('!')) name = name.substr(std::min(name.size(), name.find('!', 1) + 1));
        const auto word = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9'); };
        bool odd = name.empty() || (name[0] >= '0' && name[0] <= '9') || !std::ranges::all_of(name, word) ||
                   std::ranges::find(kLuaKeywords, name) != std::end(kLuaKeywords);
        if (!odd) return;
        ++s.oddNames;
        s.oddNameList.Add(std::format("{}: {}", At(line), name), 12);
    }

    std::string At(int line) const { return std::format("{} line {}", source.name, line); }

    // A function call and its nested calls.
    void Function(const scriptir::Call& call, const Where& where) {
        ++s.calls;
        ++s.keywords[Head(call)];
        Case(call);
        if (where.conditions) ++s.conditionFunctions[Head(call)];
        if (IsCountingFunction(call.id) && !call.args.empty()) CounterUse(call);
        Args(call, where);
    }

    void Args(const scriptir::Call& call, const Where& where) {
        for (const scriptir::Arg& arg : call.args) {
            ++s.argKinds[ArgKindName(arg.kind)];
            if (arg.kind == scriptir::ArgKind::Global && arg.name != arg.token) Qualified(arg.token, arg.name, arg.line);
            if (arg.kind == scriptir::ArgKind::Call) Function(*arg.call, where);
        }
    }

    void Qualified(const std::string& written, const std::string& found, int line) {
        ++s.qualifiedLookups;
        s.qualified.Add(std::format("{}: {} as {}", At(line), written, found));
    }

    void Case(const scriptir::Call& call) {
        if (call.id != ScriptFunction::CallScript && call.id != ScriptFunction::Assign && call.token != Name(call.id)) ++s.keywordCase;
    }

    std::string Head(const scriptir::Call& call) const {
        if (call.id == ScriptFunction::CallScript) return "(declared-script call)";
        if (call.id == ScriptFunction::Assign) return "(assignment)";
        return Name(call.id);
    }

    // The loop counter of a For / ForIf / Every / Any / Sum (its first argument).
    void CounterUse(const scriptir::Call& call) {
        const std::string& counter = call.args.front().name;
        if (std::ranges::find(counters, counter) != counters.end()) {
            ++s.counterReuse;
            s.reuse.Add(std::format("{}: {} on {}, the counter of an enclosing loop", At(call.line), Name(call.id), counter));
        }
    }

    void Commands(const scriptir::Block& block, Where& where) {
        for (const scriptir::Call& call : block.calls) Command(call, where);
    }

    void Command(const scriptir::Call& call, Where& where) {
        ++s.calls;
        ++s.keywords[Head(call)];
        Case(call);
        Args(call, where);
        if (IsWait(call.id)) Wait(call, where);
        switch (call.id) {
        case ScriptFunction::CallScript:
            ++s.callScripts;
            if (where.loops) ++s.callScriptsInLoop;
            if (where.world) ++s.callScriptsInWorld;
            if (where.definition >= 0) starts[where.definition].insert(call.target);
            started.insert(call.target);
            if (call.targetName != call.token) Qualified(call.token, call.targetName, call.line);
            if (where.killed) {
                ++s.killedCallScripts;
                s.killedStarts.Add(std::format("{}: {} after KillScript", At(call.line), call.targetName));
            }
            break;
        case ScriptFunction::Assign:
            ++s.assigns;
            if (call.targetName != call.token) Qualified(call.token, call.targetName, call.line);
            break;
        case ScriptFunction::KillScript:
            ++s.kills;
            if (where.world) ++s.killsInWorld;
            else if (where.waited) ++s.killsAfterWait;
            else where.killed = true;
            if (where.loops) ++s.killsInLoop;
            break;
        case ScriptFunction::QStart:
        case ScriptFunction::QObjArea:
        case ScriptFunction::QObjSeeObject:
        case ScriptFunction::QObjSeeUnit:
        case ScriptFunction::QObjKillUnit:
        case ScriptFunction::QObjKillGroup:
        case ScriptFunction::QObjUse:
        case ScriptFunction::QObjGetItem:
        case ScriptFunction::QFinish: Quest(call); break;
        default: break;
        }
        if (where.killed && call.id != ScriptFunction::KillScript && !where.followed) {
            ++s.killsFollowed;
            where.followed = true;
        }
        if (IsLoop(call.id)) Loop(call, where);
    }

    void Wait(const scriptir::Call& call, Where& where) {
        ++s.waitsByKind[Name(call.id)];
        if (where.killed) ++s.killedWaits;
        if (where.loops) {
            ++s.waitsInLoop;
            loopWaited = true;
            s.waitsInLoops.Add(std::format("{}: {}({}) in a loop body (depth {})", At(call.line), Name(call.id),
                                           call.args.empty() ? "" : ArgText(call.args.front()), where.loops));
            return;
        }
        if (where.world) ++s.waitsWorldTop;
        else ++s.waitsRuleTop;
        where.waited = true;
        where.killed = false;   // a waiting run has no kill flag
    }

    // A body's KillScript kills the instance for the rest of the run too; a body's wait only ends the body.
    void Loop(const scriptir::Call& call, Where& where) {
        ++s.loops;
        ++s.loopDepths[where.loops + 1];
        if (!call.args.empty()) CounterUse(call);
        const bool outerWaited = loopWaited;
        loopWaited = false;
        counters.push_back(call.args.empty() ? std::string() : call.args.front().name);
        Where inside = where;
        ++inside.loops;
        if (call.body) Commands(*call.body, inside);
        where.killed = inside.killed;
        where.followed = inside.followed;
        counters.pop_back();
        if (loopWaited) ++s.loopsWithWait;
        loopWaited = outerWaited || loopWaited;
    }

    // QStart / QObj* / QFinish with their literal arguments (a variable's value is not known here: listed).
    void Quest(const scriptir::Call& call) {
        const auto first = [&](ScriptType type) -> const scriptir::Arg* {
            for (const scriptir::Arg& arg : call.args)
                if (arg.type == type) return &arg;
            return nullptr;
        };
        const scriptir::Arg* text = first(ScriptType::String);
        const scriptir::Arg* value = first(ScriptType::Float);
        for (const scriptir::Arg* arg : {text, value})
            if (arg && arg->kind != scriptir::ArgKind::Text && arg->kind != scriptir::ArgKind::Number)
                s.questArgs.Add(std::format("{}: {} argument {} is not a literal", At(call.line), Name(call.id), arg->token));
        if (call.id == ScriptFunction::QStart) {
            quest.Start(text ? text->text : std::string());
        } else if (call.id == ScriptFunction::QFinish) {
            questTexts.push_back(quest.Finish());
        } else {
            ++s.questObjectives;
            quest.AddObjective(call.id, text ? text->text : std::string(), value ? value->number : 0.0f);
        }
    }

    // Definitions that start themselves, directly or through others (an instance's run starts a new instance; there
    // is no call stack).
    void Recursion() {
        for (const auto& [from, targets] : starts) {
            if (targets.contains(from)) {
                ++s.selfStarting;
                s.recursion.Add(std::format("{}: {} starts itself", source.name, DefinitionName(from)));
            }
        }
        // Cycles through two or more: a definition reaching itself through another.
        for (const auto& [from, targets] : starts) {
            std::set<int> seen;
            std::vector<int> stack;
            for (int t : targets)
                if (t != from) stack.push_back(t);
            bool cycle = false;
            while (!stack.empty() && !cycle) {
                const int at = stack.back();
                stack.pop_back();
                if (!seen.insert(at).second) continue;
                const auto next = starts.find(at);
                if (next == starts.end()) continue;
                for (int t : next->second) {
                    if (t == from) cycle = true;
                    else stack.push_back(t);
                }
            }
            if (cycle) {
                ++s.cycles;
                s.recursion.Add(std::format("{}: {} starts itself through others", source.name, DefinitionName(from)));
            }
        }
    }

    std::string DefinitionName(int index) const {
        for (const scriptir::Definition& definition : file.definitions)
            if (definition.index == index) return definition.name;
        return std::format("#{}", index);
    }

    Stats& s;
    const Source& source;
    const scriptir::File& file;
    std::map<int, std::set<int>> starts;   // definition -> the definitions its then-sections start
    std::set<int> started;                 // every definition a call of this text starts
    std::vector<std::string> counters;     // the enclosing loops' counters
    bool loopWaited = false;
    ScriptQuestBuilder quest;
};

int CountCommentLines(const std::string& text) {
    int count = 0;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        if (line.find("//") != std::string::npos || line.find("/*") != std::string::npos) ++count;
    return count;
}

int CountExplicitQualified(const std::string& text) {
    int count = 0;
    for (std::size_t at = text.find('!'); at != std::string::npos; at = text.find('!', at + 1)) {
        std::size_t p = at + 1;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
        if (p > at + 1 && p < text.size() && text[p] == '!') ++count;
    }
    return count;
}

void WriteDump(const Source& source, const scriptir::File& file) {
    if (g_dumpFolder.empty()) return;
    const fs::path folder = g_dumpFolder / source.group;
    fs::create_directories(folder);
    std::ofstream(folder / (source.name + ".txt"), std::ios::binary) << source.text;
    std::ofstream(folder / (source.name + ".ir"), std::ios::binary) << scriptir::Dump(file, true);
}

// ---- The Lua of each text -----------------------------------------------------------------------------------------

// The Lua side of one Script: the names its texts' Lua gave out, and a state that ran each text's chunk.
struct LuaSide {
    Script script;   // the bindings' (no binding runs: a chunk only declares)
    ScriptLua lua{&script};
    scriptlua::LuaNames names;
};

struct LuaStats {
    int texts = 0;
    int lines = 0;
    int failed = 0;
    scriptlua::LuaText writer;   // the writer's counts summed (no text)
    Listing failures;
};
std::map<std::string, LuaStats> g_lua;   // by group
LuaStats g_luaAll;
fs::path g_luaFolder;

void Sum(scriptlua::LuaText& to, const scriptlua::LuaText& from) {
    to.loops += from.loops;
    to.argumentFunctions += from.argumentFunctions;
    to.groupCopies += from.groupCopies;
    to.suffixedNames += from.suffixedNames;
    to.renamedParams += from.renamedParams;
    to.lateSettledWrites += from.lateSettledWrites;
    to.unresolvedNames += from.unresolvedNames;
}

struct LuaResult {
    scriptlua::LuaText lua;
    std::vector<std::string> failures;
};

// `source`'s Lua, from the tree of its load into the side's script: written, compiled and run in the side's state
// (after the earlier texts of the script), every global it reads defined there (a binding, a library, a helper, a
// script, a declared global) and every global it writes a declared global or a script; the WorldScript taken out.
LuaResult LoadLua(LuaSide& side, const Source& source, const scriptir::File& file) {
    const bool mob =
        source.group != "quests" && source.group != "engine" && source.group != "samples" && source.group != "console";
    LuaResult result{
        scriptlua::WriteLua(file, side.names, mob ? source.name + ".mob" : source.group + "\\" + source.name), {}};
    const scriptlua::LuaText& lua = result.lua;
    std::vector<std::string>& failures = result.failures;
    lua_State* L = side.lua.State();
    const std::string chunkName = "=" + source.name + ".lua";
    std::set<std::string> reads, writes;
    if (luaL_loadbufferx(L, lua.text.data(), lua.text.size(), chunkName.c_str(), "t") != LUA_OK) {
        failures.push_back(lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        lua_globals::Find(L, -1, &reads, &writes);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            failures.push_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    const auto global = [&](const std::string& name) {
        lua_getglobal(L, name.c_str());
        const int type = lua_type(L, -1);
        lua_pop(L, 1);
        return type;
    };
    if (failures.empty()) {
        for (const std::string& name : reads)
            if (side.lua.GlobalType(name).empty() && global(name) == LUA_TNIL)
                failures.push_back("reads " + name + ", which nothing defines");
        for (const std::string& name : writes)
            if (side.lua.GlobalType(name).empty() && name != "WorldScript" && global(name) != LUA_TTABLE)
                failures.push_back("writes " + name + ", which is no declared global or script");
        if (!file.worldScripts.empty() && !file.worldScripts[0].calls.empty() && global("WorldScript") != LUA_TFUNCTION)
            failures.push_back("no WorldScript function");
    }
    lua_pushnil(L);
    lua_setglobal(L, "WorldScript");
    if (lua.lateSettledWrites) failures.push_back("assigns a global an earlier text's wait read once");
    if (lua.unresolvedNames) failures.push_back("names the writer did not know");
    return result;
}

// LoadLua, counted where `count` (and written to --lua's folder).
void CheckLua(LuaSide& side, const Source& source, const scriptir::File& file, bool count) {
    const LuaResult result = LoadLua(side, source, file);
    if (!count) return;
    const scriptlua::LuaText& lua = result.lua;

    if (!g_luaFolder.empty()) {
        const fs::path folder = g_luaFolder / source.group;
        fs::create_directories(folder);
        std::ofstream(folder / (source.name + ".lua"), std::ios::binary) << lua.text;
    }
    for (LuaStats* s : {&g_lua[source.group], &g_luaAll}) {
        ++s->texts;
        s->lines += static_cast<int>(std::ranges::count(lua.text, '\n'));
        Sum(s->writer, lua);
        if (result.failures.empty()) continue;
        ++s->failed;
        for (const std::string& failure : result.failures)
            s->failures.Add(std::format("{}\\{}: {}", source.group, source.name, failure), 20);
    }
}

// Loads `source` into `script` with its tree and checks its Lua (CheckLua) without counting either: a text the
// counted one loads after.
void LoadEarlier(Script& script, LuaSide& side, const Source& source) {
    std::string text = source.text;
    scriptir::File file;
    script.Parse(&text, file);
    CheckLua(side, source, file, false);
}

// Loads `source` into `script` with its tree, checks its Lua, counts it. Returns the survey's quest texts.
std::vector<std::string> LoadAndSurvey(Script& script, LuaSide& side, const Source& source) {
    std::string text = source.text;
    scriptir::File file;
    script.Parse(&text, file);
    WriteDump(source, file);
    CheckLua(side, source, file, true);

    std::vector<std::string> quests;
    for (Stats* s : {&g_stats[source.group], &g_all}) {
        ++s->files;
        s->lines += file.lines;
        s->tokens += file.tokens;
        s->distinct.insert(source.text);
        if (source.encoded) ++s->encoded;
        s->commentLines += CountCommentLines(source.text);
        s->explicitQualified += CountExplicitQualified(source.text);
        if (!file.ok) {
            ++s->failed;
            s->errors.Add(std::format("{}\\{} line {}: {}", source.group, source.name, file.error.line, file.error.text));
        }
        Survey survey(*s, source, file);
        survey.Run();
        if (s == &g_all) quests = survey.questTexts;
    }
    return quests;
}

// The comments of kCommentSample, where the Lua writer puts them (docs/notes/lua-scripting.md, "Converted scripts"):
// a Lua check failure when they are not.
void CheckComments() {
    Script script;
    std::string text = kCommentSample;
    scriptir::File file;
    script.Parse(&text, file);
    scriptlua::LuaNames names;
    const std::string lua = scriptlua::WriteLua(file, names, "comments").text;
    if (lua == kCommentLua) return;
    ++g_luaAll.failed;
    g_luaAll.failures.Add("samples\\comments: the Lua differs from the expected:\n" + lua);
}

// ---- The corpus ---------------------------------------------------------------------------------------------------

// The group of a .mob: the mod's name under starter\Mods\<mod>, else the install folder's name.
std::string GroupOf(const fs::path& install, const fs::path& path) {
    const fs::path relative = fs::relative(path, install);
    std::vector<std::string> parts;
    for (const fs::path& part : relative) parts.push_back(Utf8(part));
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        if (text::CompareIgnoringCase(parts[i], "mods") == 0) return parts[i + 1];
    return Utf8(install.filename());
}

// A .mob's script and the mission it loads after: the one whose AddMob names it, or a network quest's base (its
// archive's map.txt, `#res`); -1 for none.
struct Mission {
    Source source;
    fs::path path;
    int parent = -1;
};

std::string LowerAscii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// The file stems the text's AddMob / AddMobEx commands name, lower case, in order.
std::vector<std::string> AddedMobs(const std::string& text) {
    std::vector<std::string> stems;
    const std::string lower = LowerAscii(text);
    for (std::size_t at = lower.find("addmob"); at != std::string::npos; at = lower.find("addmob", at + 1)) {
        const std::size_t open = lower.find('"', at);
        const std::size_t close = open == std::string::npos ? open : lower.find('"', open + 1);
        if (close == std::string::npos || lower.find_first_not_of(" \tex(", at + 6) < open) continue;
        stems.push_back(LowerAscii(Utf8(fs::path(lower.substr(open + 1, close - open - 1)).stem())));
    }
    return stems;
}

// A network quest's base mission: the second word after `#res` in <folder>\<stem>\<stem>\map.txt, lower case.
std::optional<std::string> QuestBase(const fs::path& mob) {
    const std::string stem = Utf8(mob.stem());
    const std::optional<std::string> map = ReadBytes(mob.parent_path() / stem / stem / "map.txt");
    if (!map) return std::nullopt;
    std::istringstream lines(*map);
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("#res")) continue;
        std::string mapName, mobName;
        if (std::getline(lines, line) && std::istringstream(line) >> mapName >> mobName) return LowerAscii(mobName);
    }
    return std::nullopt;
}

// Each mission's parent among `missions`: one of the same group first.
void FindParents(std::vector<Mission>& missions) {
    const auto find = [&](const std::string& stem, const std::string& group) {
        int any = -1;
        for (std::size_t i = 0; i < missions.size(); ++i) {
            if (LowerAscii(missions[i].source.name) != stem) continue;
            if (missions[i].source.group == group) return static_cast<int>(i);
            if (any < 0) any = static_cast<int>(i);
        }
        return any;
    };
    for (std::size_t i = 0; i < missions.size(); ++i) {
        for (const std::string& child : AddedMobs(missions[i].source.text)) {
            const int c = find(child, missions[i].source.group);
            if (c >= 0 && c != static_cast<int>(i) && missions[static_cast<std::size_t>(c)].parent < 0)
                missions[static_cast<std::size_t>(c)].parent = static_cast<int>(i);
        }
    }
    for (Mission& mission : missions) {
        if (mission.parent >= 0) continue;
        if (const std::optional<std::string> base = QuestBase(mission.path)) mission.parent = find(*base, mission.source.group);
    }
}

std::vector<Mission> FindMissions(const fs::path& install) {
    std::vector<Mission> missions;
    std::vector<fs::path> paths;
    for (const auto& entry : fs::recursive_directory_iterator(install))
        if (entry.is_regular_file() && text::CompareIgnoringCase(Utf8(entry.path().extension()), ".mob") == 0) paths.push_back(entry.path());
    std::ranges::sort(paths);
    for (const fs::path& path : paths) {
        const std::optional<std::string> bytes = ReadBytes(path);
        if (!bytes) continue;
        const std::optional<std::string> text = MobScriptText(*bytes);
        if (!text) continue;
        Mission mission;
        mission.path = path;
        mission.source.group = GroupOf(install, path);
        mission.source.name = Utf8(path.stem());
        mission.source.text = *text;
        MemoryStream file;
        file.Fill(bytes->data(), static_cast<std::uint32_t>(bytes->size()));
        MemoryStream scripts, coded;
        mission.source.encoded = file.FindTagged(kScriptsChunk, &scripts) && scripts.FindTagged(kScriptEncoded, &coded);
        missions.push_back(std::move(mission));
    }
    FindParents(missions);
    return missions;
}

// The missions `mission` loads after, the first one first.
std::vector<const Mission*> Chain(const std::vector<Mission>& missions, const Mission& mission) {
    std::vector<const Mission*> chain;
    for (int p = mission.parent; p >= 0 && chain.size() < 8; p = missions[static_cast<std::size_t>(p)].parent)
        chain.insert(chain.begin(), &missions[static_cast<std::size_t>(p)]);
    return chain;
}

// The scenarios' console script lines as the console writes them (RunScriptLine): one source per scenario, its lines.
std::vector<std::pair<std::string, std::vector<std::string>>> ConsoleLines(const fs::path& folder) {
    std::vector<std::pair<std::string, std::vector<std::string>>> scenarios;
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(folder))
        if (entry.is_regular_file() && entry.path().extension() == ".txt") paths.push_back(entry.path());
    std::ranges::sort(paths);
    for (const fs::path& path : paths) {
        std::ifstream file(path, std::ios::binary);
        std::vector<std::string> lines;
        for (std::string line; std::getline(file, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.starts_with("console ")) continue;
            std::vector<std::string> args = console::Tokenize(line.substr(8));
            if (args.empty()) continue;
            const std::string command = args.front();
            const char first = command[0];
            if (first != '@' && first != '#' && first != '$') continue;
            std::string text = first == '@' ? "WorldScript(" : first == '#' ? "WorldScript( ConsoleFloat( " : "WorldScript( ConsoleString( ";
            text += command.substr(1);
            for (std::size_t i = 1; i < args.size(); ++i) text += " " + args[i];
            text += first == '@' ? ")" : " ))";
            lines.push_back(text);
        }
        if (!lines.empty()) scenarios.emplace_back(Utf8(path.stem()), std::move(lines));
    }
    return scenarios;
}

bool Declares(const std::string& text, std::string_view name) { return text.find(name) != std::string::npos; }

// ---- The report ---------------------------------------------------------------------------------------------------

void PrintListing(const char* title, const Listing& listing) {
    if (listing.count == 0) return;
    std::printf("  %s: %d\n", title, listing.count);
    for (const std::string& line : listing.lines) std::printf("    %s\n", line.c_str());
    if (listing.count > static_cast<int>(listing.lines.size())) std::printf("    ...\n");
}

void PrintStats(const std::string& group, const Stats& s, bool details) {
    std::printf("== %s\n", group.c_str());
    std::printf("  texts %d (distinct %zu, encoded %d, loaded after another %d), lines %d, tokens %d, calls %d, comment lines %d,"
                " failed %d\n",
                s.files, s.distinct.size(), s.encoded, s.loadedAfter, s.lines, s.tokens, s.calls, s.commentLines, s.failed);
    std::printf("  definitions %d, rules %d (at most %d in one), globals %d (at most %d in one text:", s.definitions, s.rules,
                s.maxRules, s.globals, s.maxGlobals);
    for (const auto& [type, count] : s.globalsByType) std::printf(" %s %d", type.c_str(), count);
    std::printf(")\n");
    std::printf("  waits:");
    for (const auto& [kind, count] : s.waitsByKind) std::printf(" %s %d", kind.c_str(), count);
    std::printf("; top level of a WorldScript %d, of a then-section %d, in a loop body %d\n", s.waitsWorldTop, s.waitsRuleTop,
                s.waitsInLoop);
    std::printf("  loops %d, with a wait in the body %d; depth:", s.loops, s.loopsWithWait);
    for (const auto& [depth, count] : s.loopDepths) std::printf(" %d:%d", depth, count);
    std::printf("; counter reused inside %d\n", s.counterReuse);
    std::printf("  declared-script calls %d (in loops %d, in WorldScripts %d); self-starting definitions %d, start cycles %d\n",
                s.callScripts, s.callScriptsInLoop, s.callScriptsInWorld, s.selfStarting, s.cycles);
    std::printf("  KillScript %d: in WorldScripts %d (a warning), after a wait %d (a warning), in loops %d; runs going on after"
                " it %d, starting a script after it %d, waiting after it %d\n",
                s.kills, s.killsInWorld, s.killsAfterWait, s.killsInLoop, s.killsFollowed, s.killedCallScripts, s.killedWaits);
    std::printf("  assignments %d; #define: texts %d, defines %d, uses %d, unused %d; `!n!` written %d, names found qualified %d\n",
                s.assigns, s.defineFiles, s.defines, s.defineUses, s.unusedDefines, s.explicitQualified, s.qualifiedLookups);
    std::printf("  arguments:");
    for (const auto& [kind, count] : s.argKinds) std::printf(" %s %d", kind.c_str(), count);
    std::printf("\n");
    std::printf("  rules without a condition %d; definitions: started tasks (one unconditional rule, KillScript first) %d,"
                " every rule KillScript first %d, never killed %d, never started by their text %d\n",
                s.unconditional, s.oneShot, s.killedFirst, s.neverKilled, s.neverStarted);
    std::printf("  names that are no Lua identifier %d; calls written in another case %d\n", s.oddNames, s.keywordCase);
    if (s.questTexts) std::printf("  quest texts %d, objectives %d\n", s.questTexts, s.questObjectives);
    if (!details) return;
    PrintListing("parse errors", s.errors);
    PrintListing("waits in loop bodies", s.waitsInLoops);
    PrintListing("scripts started after KillScript", s.killedStarts);
    PrintListing("self-starting", s.recursion);
    PrintListing("loop counters reused", s.reuse);
    PrintListing("names found qualified", s.qualified);
    PrintListing("quest arguments", s.questArgs);
    PrintListing("defines", s.defineList);
    PrintListing("names that are no Lua identifier", s.oddNameList);
}

void PrintKeywords(const std::vector<std::string>& groups) {
    std::vector<std::string> names;
    int game = 0, rebirth = 0;
    std::set<std::string> seen;
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        if (!k->function && !k->command) continue;
        if (!seen.insert(k->name).second) continue;
        names.push_back(k->name);
        if (static_cast<int>(k->id) >= 300) ++rebirth;
        else ++game;
    }
    std::printf("== keywords: %d in the table (%d the game's, %d Rebirth's new)\n", game + rebirth, game, rebirth);
    std::printf("  %-28s", "name");
    for (const std::string& group : groups) std::printf(" %12.12s", group.c_str());
    std::printf("\n");
    std::vector<std::string> unused;
    for (const std::string& name : names) {
        int total = g_all.keywords.contains(name) ? g_all.keywords.at(name) : 0;
        if (total == 0) {
            unused.push_back(name);
            continue;
        }
        std::printf("  %-28s", name.c_str());
        for (const std::string& group : groups) {
            const auto& k = g_stats[group].keywords;
            std::printf(" %12d", k.contains(name) ? k.at(name) : 0);
        }
        std::printf("\n");
    }
    for (const char* special : {"(declared-script call)", "(assignment)"}) {
        std::printf("  %-28s", special);
        for (const std::string& group : groups) {
            const auto& k = g_stats[group].keywords;
            std::printf(" %12d", k.contains(special) ? k.at(special) : 0);
        }
        std::printf("\n");
    }
    std::printf("  used %zu of %zu; never used (%zu):", names.size() - unused.size(), names.size(), unused.size());
    for (const std::string& name : unused) std::printf(" %s", name.c_str());
    std::printf("\n");
    std::vector<std::string> consoleOnly;
    for (const std::string& name : names) {
        int shipped = 0;
        for (const std::string& group : groups)
            if (group != "console" && g_stats[group].keywords.contains(name)) shipped += g_stats[group].keywords.at(name);
        if (shipped == 0 && g_all.keywords.contains(name)) consoleOnly.push_back(name);
    }
    std::printf("  used only by the harness's console lines (%zu):", consoleOnly.size());
    for (const std::string& name : consoleOnly) std::printf(" %s", name.c_str());
    std::printf("\n");
    std::printf("== condition functions (all texts):");
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [name, count] : g_all.conditionFunctions) top.emplace_back(-count, name);
    std::ranges::sort(top);
    for (const auto& [count, name] : top) std::printf(" %s %d", name.c_str(), -count);
    std::printf("\n");
}

void PrintLuaStats(const std::string& title, const LuaStats& s) {
    const scriptlua::LuaText& w = s.writer;
    std::printf("== %s: %d texts written as %d lines of Lua, %d fail to load or check\n", title.c_str(), s.texts,
                s.lines, s.failed);
    std::printf(
        "  loops on a counter %d, argument functions %d, group copies %d, names suffixed %d, parameters renamed %d,"
        " late settled writes %d, unresolved names %d\n",
        w.loops, w.argumentFunctions, w.groupCopies, w.suffixedNames, w.renamedParams, w.lateSettledWrites,
        w.unresolvedNames);
    PrintListing("failures", s.failures);
}

int Usage() {
    std::printf("usage: ei-convert --scripts [<install>...] [--scenarios <folder>] [--dump <folder>] [--lua <folder>] "
                "[--quiet]\n");
    return 2;
}

} // namespace

MissionCounts WriteMissions(const fs::path& install, const fs::path& target) {
    MissionCounts counts;
    const std::vector<Mission> missions = FindMissions(install);
    for (const Mission& mission : missions) {
        Script script;
        auto side = std::make_unique<LuaSide>();
        for (const Mission* parent : Chain(missions, mission)) LoadEarlier(script, *side, parent->source);
        std::string text = mission.source.text;
        scriptir::File file;
        script.Parse(&text, file);
        const LuaResult result = LoadLua(*side, mission.source, file);
        ++counts.files;
        if (!result.failures.empty()) {
            ++counts.failed;
            for (const std::string& failure : result.failures)
                std::printf("ei-convert: %s: %s\n", Utf8(fs::relative(mission.path, install)).c_str(), failure.c_str());
        }
        if (target.empty()) continue;
        const fs::path output = target / fs::relative(mission.path, install).replace_extension(".lua");
        fs::create_directories(output.parent_path());
        std::ofstream(output, std::ios::binary) << result.lua.text;
    }
    return counts;
}

int Main(const std::vector<std::string>& args) {
    std::vector<fs::path> installs;
    fs::path scenarios;
    bool quiet = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--scenarios" && i + 1 < args.size()) scenarios = fs::absolute(args[++i]);
        else if (args[i] == "--dump" && i + 1 < args.size()) g_dumpFolder = fs::absolute(args[++i]);
        else if (args[i] == "--lua" && i + 1 < args.size()) g_luaFolder = fs::absolute(args[++i]);
        else if (args[i] == "--quiet") quiet = true;
        else if (args[i].starts_with("--")) return Usage();
        else installs.push_back(fs::absolute(args[i]));
    }

    std::vector<std::string> groups;
    const auto noteGroup = [&](const std::string& group) {
        if (std::ranges::find(groups, group) == groups.end()) groups.push_back(group);
    };
    for (const fs::path& install : installs) {
        const std::vector<Mission> missions = FindMissions(install);
        std::set<std::string> briefingGroups;
        for (const Mission& mission : missions) {
            noteGroup(mission.source.group);
            // The missions it loads after (not counted again), the mission, then its quests, into one script, as the
            // zone loader, AddMob and QFinish do.
            Script script;
            auto side = std::make_unique<LuaSide>();
            for (const Mission* parent : Chain(missions, mission)) LoadEarlier(script, *side, parent->source);
            if (mission.parent >= 0) {
                ++g_stats[mission.source.group].loadedAfter;
                ++g_all.loadedAfter;
            }
            const std::vector<std::string> quests = LoadAndSurvey(script, *side, mission.source);
            for (std::size_t q = 0; q < quests.size(); ++q) {
                noteGroup("quests");
                const Source quest{"quests", std::format("{}.{}.{}", mission.source.group, mission.source.name, q), quests[q]};
                LoadAndSurvey(script, *side, quest);
                ++g_stats["quests"].questTexts;
                ++g_all.questTexts;
            }
            // The engine's texts, after the missions that have what they name.
            const auto engine = [&](const char* name, const char* text) {
                noteGroup("engine");
                Script withMission;
                auto withMissionLua = std::make_unique<LuaSide>();
                LoadEarlier(withMission, *withMissionLua, mission.source);
                const Source source{"engine", std::format("{}.{}.{}", name, mission.source.group, mission.source.name),
                                    text};
                LoadAndSurvey(withMission, *withMissionLua, source);
            };
            if (mission.source.name == "zone1" && mission.source.group == Utf8(install.filename())) {
                engine("trap", kTrapText);
                engine("heroes", kHeroesText);
            }
            if (Declares(mission.source.text, "Script #OnBriefingComplete") && briefingGroups.insert(mission.source.group).second)
                engine("briefing", kBriefingText);
        }
    }
    {
        // The bolts go to the world's second script (ScriptQueue), the console's.
        noteGroup("engine");
        Script second;
        auto secondLua = std::make_unique<LuaSide>();
        const std::vector<Source> sources{{"engine", "thunder", kThunderText}, {"engine", "bolt", kBoltText}};
        for (const Source& source : sources) LoadAndSurvey(second, *secondLua, source);
    }
    {
        noteGroup("samples");
        Script samples;
        auto samplesLua = std::make_unique<LuaSide>();
        for (std::size_t i = 0; i < std::size(kSamples); ++i)
            LoadAndSurvey(samples, *samplesLua, {"samples", std::format("sample{}", i), kSamples[i]});
        CheckComments();
    }
    if (!scenarios.empty()) {
        for (const auto& [scenario, lines] : ConsoleLines(scenarios)) {
            noteGroup("console");
            Script second;
            auto secondLua = std::make_unique<LuaSide>();
            for (std::size_t i = 0; i < lines.size(); ++i)
                LoadAndSurvey(second, *secondLua, {"console", std::format("{}.{}", scenario, i), lines[i]});
        }
    }

    if (!quiet) {
        for (const std::string& group : groups) PrintStats(group, g_stats[group], true);
        PrintStats("all", g_all, false);
        PrintKeywords(groups);
    }
    if (!quiet)
        for (const std::string& group : groups) PrintLuaStats(group, g_lua[group]);
    PrintLuaStats("lua check", g_luaAll);
    return g_luaAll.failed == 0 ? 0 : 1;
}

}   // namespace convert_scripts
