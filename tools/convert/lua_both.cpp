// Script texts run both ways: see lua_both.h.
#include "lua_both.h"

#include "game/app/singletons.h"
#include "game/core/base_manager.h"
#include "game/core/core_streams.h"
#include "game/core/random.h"
#include "game/objects/map_object.h"
#include "game/objects/map_object_manager.h"
#include "game/players/player_manager.h"
#include "game/script/script.h"
#include "game/spells/spell.h"
#include "game/script/script_ir.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_scheduler.h"
#include "game/script/script_lua_writer.h"
#include "game/world/world.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lua_both {

namespace {

// The texts of one short case, each loaded before the tick of its number; the zone reset before `resetTick`.
struct CaseText {
    int tick = 0;
    const char* text = nullptr;
};
struct Case {
    const char* name = nullptr;
    std::vector<CaseText> texts;
    int resetTick = -1;
    bool quests = false;   // the game's QFinish loads quest texts in it
};

constexpr int kTicks = 24;

// Rules with conditions that draw (stopping at the first false one keeps the draws), KillScript and the run going on,
// instances started by an instance (they run from the next tick) and by a waiting run (the same tick), the waits with
// nested and variable arguments, a group assignment copied, the loops and Every / Any / Sum, a counter read after its
// loop.
const Case kRules{"rules", {{0, R"(
GlobalVars( a : float, b : float, hits : float, g : group, h : group, u : object, i : object, last : object, n : float,
            r : float, word : string )
DeclareScript Tally( k : float )
DeclareScript Echo( who : object, much : float, said : string )
Script Tally (
  if ( IsLess( a, k ) IsLess( Random( 10 ), 7 ) ) then ( a = Add( a, 1 ) )
  if ( IsEqual( a, k ) ) then ( KillScript() b = Add( b, 10 ) Echo( u, k, "done" ) )
)
Script Echo (
  if () then ( KillScript() hits = Add( hits, much ) last = who word = said Sleep( 2 ) hits = Add( hits, 100 ) )
)
WorldScript(
  u = GetObject( 1001 )
  AddObject( g, GetObject( 1003 ) ) AddObject( g, GetObject( 1001 ) ) AddObject( g, GetObject( 1002 ) )
  Tally( 5 )
  Sleep( 3 )
  Echo( GetObject( 1002 ), 2.5, "x" )
  h = g
  RemoveObject( h, GetObject( 1001 ) )
  n = GroupSize( g )
  Sleep( n )
  For( i, g ) ( r = Add( r, GetObjectID( i ) ) )
  last = i
  SleepUntil( IsLess( 3, a ) )
  r = Sum( i, g, GetObjectID( i ) )
  b = Any( i, g, IsEqual( GetObjectID( i ), 1002 ) )
  Sleep( Random( 4 ) )
  WaitSegment( 12 )
  a = Every( i, h, IsLess( 1001, GetObjectID( i ) ) )
)
)"}}};

// A rule that kills ends the instance's other rules for the tick; the run waits on without its kill flag; a group
// parameter is a copy per instance and per run; a rule set without a kill stays polled; a NaN condition holds; ForIf;
// a Sleep on a global that the instances change while it waits.
const Case kKills{"kills", {{0, R"(
GlobalVars( t : float, g : group, s : float, i : object, x : float )
DeclareScript Watch( q : group, step : float )
Script Watch (
  if ( IsLess( t, 6 ) ) then ( t = Add( t, step ) AddObject( q, GetObject( 1003 ) ) s = Add( s, GroupSize( q ) ) )
  if ( IsEqual( t, 2 ) ) then ( KillScript() Sleep( 3 ) x = Add( x, GroupSize( q ) ) )
  if () then ( x = Add( x, 1000 ) )
  if ( Div( 0, 0 ) ) then ( x = Add( x, 0.25 ) )
)
WorldScript(
  AddObject( g, GetObject( 1001 ) ) AddObject( g, GetObject( 1002 ) )
  Watch( g, 1 ) Watch( g, 0.5 )
  ForIf( i, g, IsEqual( GetObjectID( i ), 1001 ) ) ( s = Add( s, 7 ) )
  Sleep( s )
  Sleep( 1 )
  s = Add( s, GroupSize( g ) )
)
)"}}};

// Names that are no Lua identifier, a parameter with a global's name.
const Case kNames{"names", {{0, R"(
GlobalVars( this : object, Heroes : group, VSS#i#val : object, seen : float, Dog-12 : object )
DeclareScript VTriger#0#1( this : object )
Script VTriger#0#1 ( if () then ( KillScript() seen = Add( seen, GetObjectID( this ) ) ) )
WorldScript(
  AddObject( Heroes, GetObject( 1002 ) ) AddObject( Heroes, GetObject( 1003 ) )
  For( VSS#i#val, Heroes ) ( VTriger#0#1( VSS#i#val ) )
  Dog-12 = GetObject( 1001 )
  this = Dog-12
  VTriger#0#1( this )
)
)"}}};

// A second text loaded later into the same script: it starts the first one's script and uses its global, and its own
// global of the same name is never found.
const Case kLater{"later",
                  {{0, R"(
GlobalVars( m : float, k : object )
DeclareScript Plus( x : float )
Script Plus ( if () then ( KillScript() m = Add( m, x ) ) )
WorldScript( Plus( 2 ) )
)"},
                   {4, R"(
GlobalVars( m : float, z : float )
WorldScript( Plus( 5 ) Sleep( 1 ) z = m m = 9 )
)"}}};

// Number literals: each written so that it reads back as the parser's float.
const Case kNumbers{"numbers", {{0, R"(
GlobalVars( a : float, b : float, c : float, d : float, e : float, f : float )
WorldScript(
  a = 0.1 b = 16777217 c = -0 d = 340282346638528859811704183484516925440
  e = 1000000000000000000000000000000000000000000 f = 0.000001
  Sleep( 1 )
  a = 123456789.123 b = -2.5 c = 0.0000000000000000000000000000000000000117549435
  d = 0.0000000000000000000000000000000000000000000007 e = 33333.333 f = .5
)
)"}}};

// The zone reset: instances and runs dropped, every global zeroed and every text's WorldScript started again (the last
// loaded first), an empty WorldScript and an empty then-section (no run), a run that ends staying a tick.
const Case kReset{"reset",
                  {{0, R"(
GlobalVars( n : float, g : group, s : string, u : object )
DeclareScript Count()
Script Count ( if ( IsLess( n, 100 ) ) then ( n = Add( n, 1 ) ) if ( IsEqual( n, 3 ) ) then () )
WorldScript( u = GetObject( 1001 ) AddObject( g, u ) s = "x" Count() Sleep( 2 ) n = Add( n, 10 ) )
)"},
                   {2, "WorldScript()"},
                   {3, R"(
GlobalVars( m : float )
WorldScript( m = Add( m, Random( 5 ) ) Sleep( 20 ) m = Add( m, 1 ) )
)"}},
                  8};

// Two quests built by QFinish into the same script (L7): the Lua side's own QFinish converts each quest text at once
// (the runtime conversion), the second quest's plain names (Su0, Su1 ...) qualified by the parser (!2!Su0) and given a
// suffix in Lua (Su0_2), its calls finding its own scripts; the objectives' conditions use the mission's i and
// Heroes. Then console-like lines that declare nothing, the same text again (from the cache).
const Case kQuests{"quests",
                   {{0, R"q(
GlobalVars( i : object, Heroes : group, n : float )
WorldScript(
  AddObject( Heroes, GetObject( 1002 ) )
  QStart( "qa" ) QObjKillUnit( "GetObject( 1001 )" ) QObjArea( 7 ) QFinish()
  Sleep( 3 )
  QStart( "qb" ) QObjArea( 8 ) QObjKillGroup( "Heroes" ) QFinish()
  Sleep( 4 )
  AddRoundToArea( 7, GetX( GetObject( 1002 ) ), GetY( GetObject( 1002 ) ), 5 )
  Sleep( 5 )
  AddRoundToArea( 8, GetX( GetObject( 1002 ) ), GetY( GetObject( 1002 ) ), 5 )
)
)q"},
                    {14, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"},
                    {15, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"},
                    {16, "WorldScript( RemoveObject( Heroes, GetObject( 1003 ) ) )"},
                    {17, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"}},
                   -1,
                   true};

const Case* const kCases[] = {&kRules, &kKills, &kNames, &kLater, &kNumbers, &kReset, &kQuests};

constexpr ScriptType kTypes[] = {ScriptType::Float, ScriptType::String, ScriptType::Object, ScriptType::Group};

std::string ObjectText(const MapObject* object) { return object ? std::to_string(object->id) : "none"; }

std::string GroupText(const ScriptGroup& group) {
    std::string text = "{";
    for (const ScriptObjectRef& member : group) text += " " + ObjectText(member.object);
    return text + " }";
}

// One tick's state: each global's value by its tree name, the instance and waiting run counts, the random draws.
struct Snapshot {
    std::map<std::string, std::string> globals;
    std::map<std::string, ScriptType> types;
    std::size_t instances = 0;
    std::vector<std::string> definitions;   // each instance's, by its tree name, in list order
    std::size_t runs = 0;
    int draws = 0;
};

int g_draws = 0;   // the random generator's draws so far (its observer counts them)

void CountDraw(IsaacRandom&, std::uint32_t, const void*) { ++g_draws; }

Snapshot GameSnapshot(Script& script) {
    Snapshot snapshot;
    ScriptVars& vars = script.globals;
    for (ScriptType type : kTypes) {
        for (int index = 0;; ++index) {
            std::string value;
            if (type == ScriptType::Float && index < std::ssize(vars.floats.values))
                value = std::format("{:08X}",
                                    std::bit_cast<std::uint32_t>(vars.floats.values[static_cast<std::size_t>(index)]));
            else if (type == ScriptType::String && index < std::ssize(vars.strings.values))
                value = "\"" + vars.strings.values[static_cast<std::size_t>(index)] + "\"";
            else if (type == ScriptType::Object && index < std::ssize(vars.objects.values))
                value = ObjectText(vars.objects.values[static_cast<std::size_t>(index)].object);
            else if (type == ScriptType::Group && index < std::ssize(vars.groups.values))
                value = GroupText(vars.groups.values[static_cast<std::size_t>(index)]);
            else break;
            const std::string& name = scriptir::VariableName(vars, type, index);
            snapshot.globals[name] = value;
            snapshot.types[name] = type;
        }
    }
    snapshot.instances = script.instances.size();
    for (const ScriptDefinition& instance : script.instances) snapshot.definitions.push_back(instance.name);
    snapshot.runs = script.runs.size();
    snapshot.draws = g_draws;
    return snapshot;
}

// The Lua side's values of the game snapshot's globals (the left-out ones skipped: a declaration never found, and a
// counter no text reads outside its loops, which the Lua does not write).
Snapshot LuaSnapshot(ScriptLuaScheduler& scheduler, const Snapshot& game) {
    const scriptlua::LuaNames& names = scheduler.names;
    Snapshot snapshot;
    lua_State* L = scheduler.lua.State();
    for (const auto& [irName, type] : game.types) {
        const auto found = names.globals.find(irName);
        if (found == names.globals.end()) continue;
        if (names.unwrittenCounters.contains(irName) && !names.counterReads.contains(irName)) continue;
        lua_getglobal(L, found->second.c_str());
        std::string value;
        switch (type) {
        case ScriptType::Float:
            value =
                lua_type(L, -1) == LUA_TNUMBER
                    ? std::format("{:08X}", std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(lua_tonumber(L, -1))))
                    : std::string("not a number");
            break;
        case ScriptType::String:
            value = lua_type(L, -1) == LUA_TSTRING ? std::format("\"{}\"", lua_tostring(L, -1)) : "not a string";
            break;
        case ScriptType::Object:
            value = lua_isnil(L, -1) || luaL_testudata(L, -1, "ei.object")
                        ? ObjectText(scriptlua::CheckObject(L, -1)->object)
                        : std::string("not an object");
            break;
        case ScriptType::Group:
            value = luaL_testudata(L, -1, "ei.group") ? GroupText(*scriptlua::CheckGroup(L, -1)) : "not a group";
            break;
        default:
            break;
        }
        lua_pop(L, 1);
        snapshot.globals[irName] = value;
    }
    snapshot.instances = scheduler.InstanceCount();
    std::map<const void*, std::string> definitionNames;   // a definition's table -> its tree name
    for (const auto& [name, lua] : names.scripts) {
        if (lua_getglobal(L, lua.c_str()) == LUA_TTABLE) definitionNames[lua_topointer(L, -1)] = name;
        lua_pop(L, 1);
    }
    for (const void* definition : scheduler.InstanceDefinitions()) {
        const auto found = definitionNames.find(definition);
        snapshot.definitions.push_back(found == definitionNames.end() ? "an unknown table" : found->second);
    }
    snapshot.runs = scheduler.RunCount();
    snapshot.draws = g_draws;
    return snapshot;
}

// The first difference, or empty.
std::string Difference(const Snapshot& game, const Snapshot& lua) {
    if (game.instances != lua.instances)
        return std::format("instances: {} in the game, {} in Lua", game.instances, lua.instances);
    for (std::size_t i = 0; i < game.definitions.size(); ++i)
        if (game.definitions[i] != lua.definitions[i])
            return std::format("instance {}: {} in the game, {} in Lua", i, game.definitions[i], lua.definitions[i]);
    if (game.runs != lua.runs) return std::format("waiting runs: {} in the game, {} in Lua", game.runs, lua.runs);
    if (game.draws != lua.draws)
        return std::format("random draws so far: {} in the game, {} in Lua", game.draws, lua.draws);
    for (const auto& [name, value] : lua.globals)
        if (game.globals.at(name) != value)
            return std::format("{}: {} in the game, {} in Lua", name, game.globals.at(name), value);
    return {};
}

// The random generator as it was, its draws counted while it lives.
struct DrawCounter {
    DrawCounter() : saved(g_random) { IsaacRandom::SetDrawObserver(CountDraw); }
    ~DrawCounter() {
        IsaacRandom::SetDrawObserver(nullptr);
        g_random = saved;
    }
    // The generator and the count back to the start (the other side's run).
    void Restart() {
        g_random = saved;
        g_draws = 0;
    }
    DrawCounter(const DrawCounter&) = delete;
    DrawCounter& operator=(const DrawCounter&) = delete;

    IsaacRandom saved;
};

// The server world's managers the commands reach (World::Create's, without the data it loads): the effects (sounds,
// lights, bolts, particle sources), the players and map objects (none), the spells (no database: none made), the
// camera.
void PrepareWorld() {
    if (!g_serverWorld.timedObjectManager) {
        g_serverWorld.timedObjectManager = std::make_unique<EffectManager>();
        g_serverWorld.timedObjectManager->Init(&g_serverWorld);
    }
    if (!g_serverWorld.players) {
        g_serverWorld.players = std::make_unique<PlayerManager>();
        g_serverWorld.players->Init(&g_serverWorld);
    }
    if (!g_serverWorld.objectManager) {
        g_serverWorld.objectManager = std::make_unique<MapObjectManager>();
        // Not created: its Create opens the data archives; the walks of its objects find none.
    }
    if (!g_serverWorld.spellManager) {
        g_serverWorld.spellConstructor = std::make_unique<SpellConstructor>();
        g_serverWorld.spellConstructor->Create();
        g_serverWorld.spellManager = std::make_unique<SpellManager>();
        g_serverWorld.spellManager->Init(&g_serverWorld);
    }
    g_clientWorld.camera = &g_camera;
}

// The commands and functions the test world cannot run: they need the game's data or its window.
// AddMobEx (Rebirth's) loads its file whether or not it was loaded; GSListPlayerVars needs its player.
constexpr std::string_view kNeedsGame[] = {"PlayMovie", "PlayCamera", "AddMobEx", "GSListPlayerVars"};

// What a text's calls need from the test world: the first keyword it cannot run, and the files AddMob loads.
struct Needs {
    const char* missing = nullptr;
    std::vector<std::string> mobs;
};

void FindNeeds(const scriptir::Block& block, Needs& needs);

void FindNeeds(const scriptir::Call& call, Needs& needs) {
    const char* keyword = KeywordName(static_cast<ScriptLexeme>(call.id));
    if (keyword && std::ranges::find(kNeedsGame, keyword) != std::end(kNeedsGame) && !needs.missing)
        needs.missing = keyword;
    if (call.id == ScriptFunction::AddMob) {
        if (!call.args.empty() && call.args[0].kind == scriptir::ArgKind::Text) needs.mobs.push_back(call.args[0].text);
        else if (!needs.missing) needs.missing = "AddMob of a computed name";
    }
    for (const scriptir::Arg& arg : call.args)
        if (arg.call) FindNeeds(*arg.call, needs);
    if (call.body) FindNeeds(*call.body, needs);
}

void FindNeeds(const scriptir::Block& block, Needs& needs) {
    for (const scriptir::Call& call : block.calls) FindNeeds(call, needs);
}

Needs FindNeeds(const std::vector<Text>& texts) {
    Needs needs;
    Script script;
    for (const Text& text : texts) {
        std::string copy = text.text;
        scriptir::File file;
        script.Load(&copy, false, &file);
        for (const scriptir::Block& block : file.worldScripts) FindNeeds(block, needs);
        for (const scriptir::Definition& definition : file.definitions) {
            for (const scriptir::Rule& rule : definition.rules) {
                FindNeeds(rule.conditions, needs);
                FindNeeds(rule.commands, needs);
            }
        }
    }
    return needs;
}

// The world's list of loaded .mob files with the ones the texts AddMob (the lockstep loads them as texts of the
// script: LoadMobFile then finds each loaded), as it was again at the end.
struct MobFiles {
    explicit MobFiles(const std::vector<std::string>& mobs) : saved(g_serverWorld.mobFiles) {
        for (const std::string& mob : mobs) g_serverWorld.mobFiles.push_back(mob);
    }
    ~MobFiles() { g_serverWorld.mobFiles = saved; }
    MobFiles(const MobFiles&) = delete;
    MobFiles& operator=(const MobFiles&) = delete;

    std::vector<std::string> saved;
};

}   // namespace

bool g_logMuted = false;

namespace {

// The runtime conversion's cache and errors: a text that declares nothing converted once (the same text again from
// the cache, which a text that changes the names empties), a Lua error of a run reported, a Lua load failure as
// Script::Load's error 18.
int CheckRuntime(int* checks) {
    int failures = 0;
    const auto expect = [&](bool ok, const char* what) {
        ++*checks;
        if (ok) return;
        ++failures;
        std::printf("lua_check: runtime: %s\n", what);
    };
    Script script;
    ScriptLuaScheduler scheduler(&script);
    std::vector<std::string> reported;
    scheduler.report = [&reported](const std::string& message) { reported.push_back(message); };
    const auto load = [&script](const char* text) {
        std::string copy = text;
        return script.Load(&copy, true);
    };
    const char* line = "WorldScript( Sleep( 1 ) )";
    load(line);
    load(line);
    load(line);
    expect(scheduler.texts == 3 && scheduler.cachedTexts == 2, "a line loaded three times is converted once");
    load("GlobalVars( k : float ) WorldScript( k = 2 )");
    load(line);
    expect(scheduler.cachedTexts == 2, "a text that changes the names empties the cache");
    load(line);
    expect(scheduler.cachedTexts == 3, "the cache fills again");
    expect(scheduler.RunCount() == 6, "every text's WorldScript starts in Lua");
    expect(script.runs.empty(), "no WorldScript starts in the game's executor");

    std::string error;
    scheduler.Load("function WorldScript() error('boom') end", "=broken", true, &error);
    scheduler.Tick();
    expect(reported.size() == 1 && reported[0] == "broken:1: boom", "a run's Lua error is reported");

    scheduler.lua.loadText = [](const scriptir::File&, std::string_view, bool, std::string* message) {
        *message = "text9:1: a Lua error";
        return false;
    };
    expect(!load(line) && script.errorCode == ScriptParseError::Lua && script.errorText == "text9:1: a Lua error",
           "a Lua load failure is Script::Load's error 18");
    return failures;
}

}   // namespace

Outcome Compare(const std::vector<Text>& texts, int ticks, int resetTick, Saves saves) {
    PrepareWorld();
    struct Mute {
        Mute() { g_logMuted = true; }
        ~Mute() { g_logMuted = false; }
    } mute;
    Outcome outcome;
    const Needs needs = FindNeeds(texts);
    if (needs.missing) {
        outcome.leftOut = needs.missing;
        return outcome;
    }
    const MobFiles mobFiles(needs.mobs);
    DrawCounter counter;
    counter.Restart();
    std::vector<Snapshot> gameTicks;
    std::vector<std::uint8_t> saved;   // the game's script state at the old-format save
    {
        Script script;
        int loads = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            for (std::size_t t = 0; t < texts.size(); ++t) {
                if (texts[t].tick != tick) continue;
                std::string text = texts[t].text;
                if (script.Load(&text, true)) ++loads;
            }
            if (tick == resetTick) script.ResetZone();
            script.Tick();
            if (script.fileCount > loads) outcome.questLoaded = true;
            gameTicks.push_back(GameSnapshot(script));
            if (tick == saves.oldFormat) {
                MemoryStream stream;
                script.SaveState(&stream);
                saved.assign(stream.data.begin(), stream.data.begin() + stream.size);
            }
        }
    }

    // The Lua side: its script parses every text as the game's does, and hands it to the scheduler (the runtime
    // conversion, ScriptLua::loadText), the quest scripts its own QFinish builds too.
    counter.Restart();
    Script script;
    std::vector<std::string> luaErrors;
    const auto makeScheduler = [&script, &luaErrors] {
        auto made = std::make_unique<ScriptLuaScheduler>(&script);
        made->report = [&luaErrors](const std::string& message) { luaErrors.push_back(message); };
        return made;
    };
    auto lua = makeScheduler();
    for (int tick = 0; tick < std::ssize(gameTicks) && outcome.difference.empty(); ++tick) {
        for (std::size_t t = 0; t < texts.size(); ++t) {
            if (texts[t].tick != tick) continue;
            std::string text = texts[t].text;
            if (!script.Load(&text, true) && script.errorCode == ScriptParseError::Lua)
                luaErrors.push_back(script.errorText);
        }
        if (tick == resetTick) lua->ResetZone();
        lua->Tick();
        if (script.errorCode == ScriptParseError::Lua) luaErrors.push_back(script.errorText);
        const Snapshot& game = gameTicks[static_cast<std::size_t>(tick)];
        std::string difference = Difference(game, LuaSnapshot(*lua, game));
        if (difference.empty() && !luaErrors.empty()) difference = "Lua error: " + luaErrors.front();
        std::string error;
        if (difference.empty() && tick == saves.oldFormat) {
            MemoryStream stream;
            stream.Fill(saved.data(), static_cast<std::uint32_t>(saved.size()));
            Script loaded;
            loaded.LoadState(&stream);
            auto resumed = makeScheduler();
            std::string text;
            if (resumed->Resume(loaded, &text, &error)) {
                lua = std::move(resumed);
                difference = Difference(game, LuaSnapshot(*lua, game));
                if (!difference.empty()) difference = "resumed from the old format: " + difference;
            } else {
                difference = "the old format did not resume: " + error;
            }
        }
        if (difference.empty() && tick == saves.eris) {
            std::string data;
            auto restored = makeScheduler();
            if (!lua->Save(&data, &error) || !restored->Restore(data, &error)) {
                difference = "Eris: " + error;
            } else {
                lua = std::move(restored);
                difference = Difference(game, LuaSnapshot(*lua, game));
                if (!difference.empty()) difference = "restored by Eris: " + difference;
            }
        }
        if (!difference.empty()) outcome.difference = std::format("tick {}: {}", tick, difference);
        outcome.ticks = tick + 1;
        outcome.mostInstances = std::max(outcome.mostInstances, game.instances);
        outcome.mostRuns = std::max(outcome.mostRuns, game.runs);
    }
    return outcome;
}

int Check(int* checks) {
    int failures = 0;
    for (const Case* test : kCases) {
        ++*checks;
        std::vector<Text> texts;
        for (const CaseText& text : test->texts) texts.push_back({text.tick, text.text});
        const auto run = [&](Saves saves, const std::string& how) {
            const Outcome outcome = Compare(texts, kTicks, test->resetTick, saves);
            if (outcome.difference.empty() && outcome.ticks == kTicks && outcome.questLoaded == test->quests) return;
            ++failures;
            std::printf("lua_check: %s%s: %s\n", test->name, how.c_str(),
                        !outcome.difference.empty()          ? outcome.difference.c_str()
                        : outcome.questLoaded != test->quests ? "the game's QFinish did not load its quest texts"
                                                              : "compared fewer ticks");
        };
        run({}, "");
        for (int tick = 0; tick + 1 < kTicks; ++tick) {
            *checks += 2;
            run({tick, tick + 1}, std::format(" (saved at tick {} in the old format, at {} by Eris)", tick, tick + 1));
            run({-1, tick}, std::format(" (saved at tick {} by Eris)", tick));
        }
    }
    failures += CheckRuntime(checks);
    return failures;
}

}   // namespace lua_both
