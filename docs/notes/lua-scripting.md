# Lua scripting

Every world script runs on Lua (L9, 2026-11-10): the game's parser reads each text (a .mob's script, AddMob'd mobs,
QFinish's quests, console lines, execs files), its parse tree is written as readable Lua (cached per text) and runs on
the script's scheduler in the game's tick order; a mod's or a converted install's `.lua` stands for a .mob's script;
saves hold the Lua state (Eris), and old-format saves resume through a shim. So the game's mission scripts are
readable Lua files that mods edit (ei-convert writes them from the originals, which keep loading), and mods write
their own scripts in Lua. Decided 2026-11-10 with the user: readable Lua over a hidden compile step, Lua 5.3 + Eris, saves ours only.
Tickets L1-L9: `python tools/tickets.py list --group lua`. Engine details are in `script.md`; Rebirth's extensions
are in `rebirth.md` ("AddNewScriptCommands").

## The game's script engine

What the original's executor did; the Lua path keeps its rules.

- **Language.** It has no operators: every value comes from a built-in function. The game's table has 214 commands and
  functions (and 11 punctuation and section words); Rebirth adds 69, 7 of them under a game name, so 276 in all.
- **Control flow.**
  - Each script instance re-checks its `if(...)then(...)` rules on every tick until KillScript.
  - For / ForIf walk a group from last to first.
  - There are four waits: Sleep, WaitSegment, SleepUntil and SleepUntilIdle.
  - A wait inside a For body ends that body for the current member, so a run only ever waits at a top-level command of its
    block.
- **Saves hold the whole VM, compiled program included** (ScriptSave):
  - command blocks, definitions, globals and instances;
  - every waiting run as block id, command index, ticks, sleep start, and a copy of its locals.

  Loading never re-reads the mission file, so a save keeps running the code it was saved with.
- **Text gets compiled at runtime too.** QFinish builds quest scripts as text, and console lines are loaded into the mission
  script.

## Survey

Every script text the game runs, parsed by the game's parser (L1, 2026-11-10): `build\verify\ei-convert.exe --scripts
build\unpacked\vanilla build\unpacked\rebirth --scenarios tests\scenarios [--dump DIR]` (about 10 s; `--dump` writes each
text and its tree with lines). A .mob that another's AddMob names, or a network quest's (its archive's map.txt names
the base mob after `#res`), is loaded after that one into the same script, as the game does; QFinish's texts are built
from each mission's QStart / QObj* / QFinish commands by the game's own builder and loaded after the mission.

| texts | count (distinct) | lines | calls | parse errors |
|---|---|---|---|---|
| vanilla .mob scripts (97 encoded) | 99 (99) | 54,245 | 22,023 | 0 |
| Rebirth's .mob scripts (20 differ from vanilla's) | 99 (99) | 54,600 | 22,150 | 0 |
| AllHeroes | 11 (11) | 15,299 | 5,927 | 0 |
| Lost in Astral | 130 (78) | 57,380 | 40,524 | 56 |
| QFinish quest texts | 52 (26) | 1,952 | 1,298 | 0 |
| engine texts (trap, thunder, bolt, heroes, briefing) | 10 (5) | 10 | 33 | 0 |
| harness console lines (284 scenarios) | 1,614 (892) | 1,614 | 4,833 | 40 (meant) |

IronMod has no scripts (databases only); `2utoexec.cfg` holds only commented lines. Of the 339 .mob scripts, 159 are
loaded after another (vanilla 49: 21 AddMob'd, 28 network quests). The counts below are of the .mob scripts and the
quest and engine texts (401 texts, 183,486 lines), without the harness lines.

- **Keywords.** 276 in the table. The .mob scripts use 159 (vanilla 143); with the quest texts 160 (IsUnitVisible only
  there). The harness's console lines reach 88 more. 27 are used nowhere: PlayFX, SetPlayerAggression,
  RemoveUnitFromControl, SetDirectionToObject, SetSpellAggression, SetSunLight, StartAnimation, ShowCredits, ShowBitmap,
  WaitEndAnimation, Sum, IsInSquare, GetBSZValue, GroupSub, IsUnitInWater, IsPlayerInSafety, GetAIClass,
  GetLootItemsCount, AlarmPosX, AlarmPosY, IsNight and Rebirth's DistancePointPoint, NearestUnit, UnitsNearPoint,
  GSGetPlayerVar, RandObj, PlayerExists. Of Rebirth's 62 new ones the shipped scripts use two (GameOver 35 and
  GameOverMSG 15, both Lost in Astral). ForIf and WaitSegment appear only in harness lines. The most used (.mob scripts): KillScript
  10,435, GetObject 10,280, declared-script calls 9,695, GSSetVarMax 4,615, Sleep 4,441, AddObject 4,087, IsEqual
  3,246, assignments 2,533, GetObjectByID 2,503, GSGetVar 2,470. The polled conditions are mostly IsEqual (3,265),
  GSGetVar (2,406), IsLess, GetObject, GetLeverState, IsEqualString.
- **Waits.** Sleep 4,444, SleepUntilIdle 944, SleepUntil 197, WaitSegment 0. Every one stands at the top level of its
  block (287 in WorldScripts, 5,298 in then-sections); no loop body in any shipped, generated or harness text holds a
  wait.
- **Loops.** 421 For (vanilla 188), never nested (depth 1 everywhere), no loop counter reused inside a loop, ForIf
  only in harness lines; Every 118 and Any 326 calls, in conditions and arguments.
- **Declared scripts.** 9,915 calls start an instance (545 in loop bodies, 3,027 in WorldScripts). A call starts an
  instance at the head of the list, so "recursion" is no stack: 6 definitions start themselves, 1,821 start themselves
  through others (VCheck / VTriger re-arming chains). 720 definitions are started by no call of their own text (called
  from a later text, or not at all).
- **Script shapes.** 9,448 definitions, 10,722 rules (at most 98 in one definition); 5,017 rules have an empty
  if-section. 4,603 definitions are started tasks (one unconditional rule whose first command is KillScript); 9,421
  start every rule with KillScript (fire once); 16 never call KillScript (polled until the zone ends).
- **KillScript** (10,651) never stands in a WorldScript, after a wait or in a loop body. It only sets the instance's
  flag: 9,144 runs go on after it, 5,416 start a declared script after it (gz2g's `KillScript() VTriger#0#n( this )` is
  the common form, not an exception) and 2,329 wait after it, so the run outlives its instance. The kill flag a waiting
  run loses (KillScript after a wait only warns) is never reached.
- **Globals** per text: at most 69 (vanilla), 4,117 in all: 3,555 objects, 532 groups, 30 floats; strings only in
  harness lines.
- **#define:** used by no shipped or generated text (only the samples). **`!n!`:** never written; 1,585 names resolve
  only in their qualified form (globals, calls and assignments of a text loaded after another: AddMob'd and network
  quest mobs, the quest texts' `Su1` as `!2!Su1`).
- **Names.** 9,950 script, global and parameter names are no Lua identifier: `#` in 9,419 (`VTriger#0#312`,
  `VSS#i#val`, `#OnBriefingComplete`), `-` in 531 (`DogRedF12-422369`). 82 calls are written in another case than the
  table's (`any` in every quest text).
- **Comments.** None in vanilla's scripts (the editor stripped them); 24 lines in Rebirth's changed ones, 176 in
  AllHeroes, 337 in Lost in Astral. The lexer drops them, so the tree has none.
- **Quest texts.** All 52 come from network quest mobs (z*q*): QFinish runs only in multiplayer quests. A QObjSeeObject /
  QObjSeeUnit / QObjKillUnit / QObjKillGroup / QObjUse argument is pasted into the text as an expression
  (`GetObject(1000020)`), and the conditions use the mission's globals `i` and `Heroes`.
- **Parse errors in shipped files.** None in vanilla, Rebirth or AllHeroes. Lost in Astral: bz23k (line 267, the never
  declared `BuyHaburuMain#2#1#0`, mods.md) and 55 of its 61 AddMob'd mobs, which fail at their first DeclareScript with
  "Duplicate script VCheck#1#1": the zone they load into declares the same name, and FindScript's `!n!` retry finds it.
  AddMob's load (MobFile::Load) drops the file's scripts as the original did (it ignored the error; no Rebirth patch
  touches FindScript or the load) and now says "Script error: <file>.mob line <n>: ..." on the console (L1a). All
  those files hold the same leftover template script (watch object 1000027, set q.gz5g.q22g.1 to 2), so only the
  first copy running loses nothing.
- **Block ids.** Only old-format saves use them (if-then pairs, For bodies, runs). They counted the blocks in load
  order (the mission, then AddMob'd mobs and QFinish texts as the game ran them), so the same file got other ids after
  another load history. A saved run is always at a top-level command (no waits in loop bodies): on a wait, at index 0 of a
  WorldScript not yet run, or at the end of its block.

### What it settled for L2-L9

- **L2 (bindings).** All 276 are generated from the table; 160 are reached by the game's content, 88 more only by
  harness lines, 27 by nothing (bindings no scenario reaches). Rebirth's string and maths
  functions are reached only by harness lines.
- **L3 (writer)**: no wait in a loop body, no nested loop, no ForIf, no #define and no written `!n!` in any shipped
  text, so converted missions need no special form for the end-the-body rule (the runtime keeps it for mods), and
  KillScript is a call that sets a flag, the function going on ("Converted scripts"). Comments: vanilla's scripts
  have none; Rebirth's 24 lines, AllHeroes 176, Lost in Astral 337 (cp1251), kept by L3a.
- **L4 (scheduler).** KillScript-then-go-on is the main path (86% of kills), and a killed instance starting the next
  script (5,416) is common; the lost kill flag of a waiting run is not reached by content.
- **L6 (saves).** Resume points are only top-level waits, starts and ends, so the old-save shim enters a converted block
  at a top-level label (Lua 5.3's `goto` at function level is enough). Since block ids follow the load history, the
  shim should build its tree from the save's own program (the saved blocks), not map ids onto a fresh load of the
  .mob.
- **L7 (quests, console).** 26 distinct quest texts, multiplayer only; their objective arguments are expressions and
  need the mission's `i` and `Heroes`. The engine's texts are five fixed forms; the console's are three wrappers.
- **L8 (mods).** The mods use the vanilla built-ins plus GameOver / GameOverMSG; Lost in Astral's AddMob name clash
  shows that mod scripts loaded into one script need their own name space.

## Design

- **Front end.** The existing parser fills a parse tree beside its declarations (`src/game/script/script_ir.h`,
  `scriptir::File`; `Script::Parse`); it builds no program of its own (L11). That keeps the lexer quirks (`Run1` lexes
  as `Run 1`; Rebirth's stricter match makes `Orc1` an identifier), #define expansion, `!n!` name qualification and the
  parse errors. The
  tree holds the sections (globals, WorldScripts, definitions with their parameters and rules), each call with its
  keyword id, the token as written, its line, its arguments in the written order (each with its type, slot and kind:
  number, string, parameter, global, nested call); Assign and declared-script calls name their target. The Lua writer
  never reads script text itself.
  - `scriptir::Dump` prints the tree (by type and slot), `ei-convert --scripts --dump` writes it for every text it
    loads (Survey below). The build runs `--scripts` on the samples in `tools/convert/convert_scripts.cpp` and the
    scenarios' console lines (`--quiet`, no game data): each text's Lua loaded and checked. Until L11 it also compared
    the tree with the ScriptCall program the parser built beside it (0 differ), which saves no longer hold.
- **Built-ins and data stay in C++.** Lua drives control flow and calls bindings generated from the keyword table, which go
  to the same CalcFloat / CalcObject / CalcGroup / command code by id, under readable Lua names ("Bindings" below).
  - Object references and groups are userdata over the existing types: reference counts, deleted-object dropping, and
    groups kept sorted (by object id, script.md).
  - Floats stay single precision. A float→double→float round trip is exact; check that NaN payloads survive, since
    assignment copies bits.
- **Readable Lua.** The old engine only waits at a top-level command of a block (a wait inside a For body ends
  that member's body), so a script converts to plain code: a rule set as condition and action functions, waits as
  calls that yield (`Sleep`, `WaitSegment`, `SleepUntil`, `SleepUntilIdle`), For / ForIf as
  `ForEach(group, function(member) ... end)` keeping the original's end-the-body rule, the WorldScript as the start
  function ("Converted scripts" below). ei-convert writes `<zone>.lua` beside the .mob (and MF9's TOML); the game
  loads it through the loose-file lookup (mods.md, "Scripts in Lua") and converts an original .mob's script at its load
  the same way ("Texts at runtime").
  No pc state machines: a modder reads and edits the file.
- **Scheduler.** It keeps Script::Tick's order exactly:
  - waiting runs first, then instances, then dropping deleted objects, then timed effects, then freeing temporaries;
  - a new instance goes at the head of the instance list;
  - an instance started by a waiting run runs in the same tick, and one started by an instance's commands runs from the
    next tick;
  - Execute counts calls, which WaitSegment uses;
  - Sleep counts ticks as the original does.
- **Saves** ("Saves" below).
  - New saves persist the whole Lua state with Eris: game-script coroutines and mod coroutines alike. The bindings form the
    "permanents" table; object references and groups persist as object ids through `__persist`, as the current save writes
    them. Waits yield from Lua, never from inside a C++ call.
  - Saves are ours only: game.exe is no longer a target (own goldens), so no old-format writer.
  - Old saves (vanilla, or ours from before Lua) load: each saved run (block, index, ticks, sleep start, locals) resumes
    through a shim that runs the rest of its block from that command.
- **Mods (L8, "Mods" below).** A .mob's `.lua` in the loose-file lookup runs in place of its script on the scheduler
  (the real path); the same built-ins under Lua names, waits as yields, a sandbox, errors as the console's "Script
  error: ...", the Lua state in the save beside the old format.

## Bindings

What exists (L2): `src/game/script/script_lua.h` (`ScriptLua`), Lua 5.3.5 with Eris in `third_party/eris`
(README.txt there: Eris's master-lua5.3 tree, unmodified, built as C++). The scheduler (below) owns one, every world
script's. Checked by `ei-convert --lua-check` (build checks; ctest `port_lua`) and the unit check `lua_number.cpp`.

- **The state.** Libraries base (without `dofile` / `loadfile`), coroutine, table, string, math (without `random` /
  `randomseed`: a draw outside the game's generator; scripts draw through `Random`), utf8; no io, os, package or debug.
  `print` writes a console line, `load` takes text only (bytecode could break the state), `pairs` walks the keys
  sorted (numbers, strings by their bytes, false, true, then the rest; a `__pairs` first), since Lua's own walk
  follows the string hashes, whose seed changes every run (L8). The sandbox is `engine/core/lua_sandbox.h`'s, shared
  with the effects' hooks' state (effects.md, "Lua hooks"); its `pairs` and `load` go in after the prelude, which keeps
  Lua's own. The runtime of a state is in its
  extra space (`ScriptLua::Of`).
- **Generated from the keyword table.** The constructor walks `KeywordTable()` (the game's table with Rebirth's):
  every entry with a function or command signature gets a binding, 276; the 11 punctuation and section words none.
  A binding is one C closure (`CallBinding`) over that keyword's signature and a `ScriptCall` it reuses: the Lua
  arguments go into the call's slots as `LoadFunction` would put them (Rebirth's `?`: the rest optional, `+`: the
  letter before it repeated), then the same `CalcFloat` / `CalcObject` / `CalcGroup` / `scriptaddon::CalcString` runs
  by id, or for a command `ScriptRun::RunCommand` (Execute's step, split out for this) in the current run; the
  temporaries are freed after each call. Too few or too many arguments, or a value of the wrong type (no string for a
  number or a number for a string) raise a Lua error.
- **Special keywords** (no nested call or body in Lua): `For` / `ForIf` are `ForEach([counter,] group, body)` /
  `ForEachIf([counter,] group, condition, body)`, members from last to first, the size read once; each body runs on a
  thread of its own from a fresh position in the same run, and a wait in it ends the body for that member (RunBody's
  rule). `Every` / `Any` / `Sum([counter,] group, condition)` call the condition for each member from the first (the
  size read again each time); a number for the condition is the game's call without a nested one (`Every` copies it,
  `Sum` multiplies it by the size). A condition gives a number or a boolean. The optional first argument names the
  loop's counter global ("Loop counters" under "Converted scripts").
- **Waits** yield from Lua: `Sleep`, `WaitSegment`, `SleepUntil` and `SleepUntilIdle` are Lua functions (the prelude in
  script_lua.cpp) that poll their command's binding once per resume (`RunCommand`, so the run's ticks and sleep start
  count as Execute's) and `coroutine.yield()` while it waits. An argument may be a function, called on every poll: the
  game evaluated a nested call again on each Execute (`Sleep(Random(n))` draws every tick; `SleepUntil(c)` needs it).
  `ScriptLua::Continue(thread, run, ...)` is Execute's step for a Lua run: it counts the tick and resumes with `run` as
  the current run (KillScript sets its kill flag; outside a resume a run of its own without a flag, as the
  WorldScript's). `--lua-check` checks the tick counts against the game's.
- **Values.** A float is a Lua number (double) exactly; a number passed in rounds to the nearest float
  (`script_lua_number.h`); NaNs keep sign and payload bit for bit both ways, signalling ones too (by bit operations,
  not the FPU). Strings are Lua strings (UTF-8 bytes). Objects and groups are full userdata over `ScriptObjectRef` /
  `ScriptGroup` (the counts held as the variables hold them; groups in id order through ScriptGroup's own methods;
  `#g`, `g[i]` from 1; two values of one object are `==`, though each read is a new value, so not one table key). A
  function result of no object is `nil`, and `nil` passes a null object. Every live value is linked into its runtime's
  list: `ScriptLua::RemoveDeletedObjects` drops deleted objects from all of them, as the game's tick and purge did to
  the variables (the scheduler's tick and `Script::PurgeDeletedObjects` call it on the script's runtime). `NewGroup()`
  and `CopyGroup(g)` are the two
  helpers: a group value is shared by reference, the game's assignment copied.
- **Persistence.** `ScriptLua::Persist` / `Unpersist` (Eris) with permanents built at start: the globals table itself
  and every function in it and in the libraries' tables by name (`_G.GetObject`, `coroutine.yield`), and the hidden
  helpers. So the globals are not in the data (the scheduler's Save adds the scripts' own: "Saves"), and a coroutine
  waiting in `Sleep` resumes in the new state's own `Sleep`. An object persists as its id, a group as its members' ids
  (`__persist`, looked up in the server world as `ScriptObjectRef::Load` does; a missing object comes back as a value
  of none). The run's position (ticks, sleep start) is C++'s, not in the data: L6 keeps it beside the coroutine.

**The naming rule.** A keyword's Lua name is its own (the table's spelling) except where it abbreviates or misspells
(72; each keyword's `EI_LUA_BINDING` row in script_lua_api.inc names it): `UM…` → `UnitMode…` (`UMAg` →
`UnitModeAggressionLevel`, `UMSuspection` → `UnitModeSuspicion`); `GS…Var` → `…GameVar` (`GSSetVar` → `SetGameVar`, `GSDelVar` → `DeleteGameVar`,
Rebirth's `GSSetPlayerVar` → `SetPlayerGameVar`, `GSListPlayerVars` → `ListPlayerGameVars`); `Q…` / `QObj…` →
`Quest…` / `QuestObjective…`; `FX` → `Sound` (`CreateFX` → `CreateSound`, `PlayFX` → `PlaySound`); `HP` → `HitPoints`;
`SetCP` → `SetPosition`; `AlarmPos…` → `AlarmPosition…`; `GetZValue` / `GetBSZValue` → `GetGridHeight` /
`GetSurfaceHeight`; `GetMercsNumber` → `GetMercenaryCount`, `RecalcMercBriefings` → `RecalculateMercenaryBriefings`;
`GroupAdd` / `GroupSub` / `GroupCross` → `GroupUnion` / `GroupDifference` / `GroupIntersection`; `UnitSee` /
`GroupSee` / `PlayerSee` → `SeenByUnit` / `SeenByGroup` / `SeenByPlayer`; `SetBackGroundColor` →
`SetBackgroundColor`; `For` / `ForIf` → `ForEach` / `ForEachIf`; Rebirth's `Str…` → `String…` (`StrCat` →
`StringConcat`, `StrHas` → `StringContains`, `FloatToStr` → `FloatToString`), `…Exp…` → `…Experience…`, `RandStr` /
`RandObj` → `RandomString` / `RandomObject`, `GameOverMSG` → `GameOverMessage`, `IsFileExists` → `FileExists`. `ID`,
`Ex` and the rest stay. `scriptlua::LuaName` / `BindingNames` give the names (the writer's table).

The converted scripts' helpers (the prelude, for L3): `GlobalVars { name = "type", ... }` sets each global to its
type's zero value (`0.0`, `""`, nil, a new empty group) and keeps the type (`ScriptLua::GlobalType`;
`ScriptLua::ResetGlobals` zeroes them all again); `Script { rules }` checks each rule (an optional condition function, an
optional action function) and gives the table a metatable whose `__call` starts an instance through
`ScriptLua::startScript` (the scheduler's; without one a call is a Lua error). Lua arithmetic is in doubles: one
`+ - * /` of two floats rounded to a float equals the float operation, a chain does not, and `Add(a, b)` is `b + a` in
floats; converted scripts keep the bindings (`Add`, `IsEqual`, whose NaN rules differ from Lua's `==`).

### The API's declarations

`src/game/script/script_lua_api.inc` declares every name the game's Lua gives or reads, once (MF17i): each keyword's
binding (its Lua name, arguments with types, result, one line of what it does; the rows' order and sections are the
docs'), the runtime's own globals (`NewGroup`, `CopyGroup`) and the prelude's helpers (`GlobalVars`, `Script`), the
cast value's methods and fields, the stats' methods and names, the effect's fields, the options tables, the spell
hooks. The same rows register: `LuaName` is the keyword's row's name (a keyword without a row gets no binding), the
constructor registers the globals and helpers from theirs, ScriptSpellHost the cast and stats methods (X-macros over
the .inc; `script_lua_api.h` holds them as data, `luaapi::kRows`). Lua's own (`print`, `pairs`, `load`, the
libraries) are not declared.

- **Exported.** `ei-convert --lua-api <file>` writes a LuaLS definition file (`---@meta`; classes `ei.Object`,
  `ei.Group`, `ei.Cast`, `ei.Stats`, `ei.Effect`, `ei.Spell` with the hooks, the options; every global function with
  `---@param` / `---@return` / `---@overload`) (tools/convert/lua_api.cpp). The short types of the rows (`object`,
  `group`, `point`, `position`, ...) become `ei.*`; an `object` argument or result is `ei.Object?`.
- **Generated docs.** `ei-convert --lua-api-docs <repository>` rewrites docs/modding.md's blocks between
  `<!-- lua-api: NAME -->` and `<!-- /lua-api -->` (functions, hooks, cast-fields, cast-methods, options,
  effect-fields, stats-methods, stats) and the shipped copy `examples/lua_api/ei_api.lua` (the release takes the
  examples folder beside ei-convert); with `--check` it compares them instead (a build check; ctest `port_lua_api`).
  Edit the .inc, then run it.
- **Checked** by `--lua-check` (CheckDeclarations, CheckCastDeclarations): every keyword with a signature has one row
  and its declared argument types (the first of a union) and result are the signature's letters (`?`, `+` included;
  the loops and Every / Any / Sum have shapes of their own); the state's global functions beyond a plain Lua's are the
  declared ones and the reverse; a live cast value's methods and fields, the stats value's methods and an effect's
  fields are the declared ones; the stat rows are `kEffectStatNames` in order.

## Converted scripts

What exists (L3): `src/game/script/script_lua_writer.h` (`scriptlua::WriteLua`) writes the Lua of one script text from
the tree of its load (`scriptir::File`); `scriptlua::LuaNames` carries the names across the texts loaded into one
Script (AddMob'd mobs, network quests, quest texts, console lines). The game loads a mod's (or a converted install's)
`.lua` in place of its .mob's script ("Mods").

- **Where.** `ei-convert <install> <target>` writes each .mob's script as `<target>\<its path>.lua` (`Maps\zone1.lua`),
  converted after the missions it loads after (an AddMob's parent, a network quest's base) as `--scripts` finds them;
  `--check <install>` converts and checks without writing ("mission script: 99 files, 0 failed"). `ei-convert --scripts
  ... --lua <folder>` writes every text of the survey as `<group>\<name>.lua`, the quest, engine and console texts too.
- **The file.**
  - Comments of the original (L3a). The lexer hands each comment (`//` to the end of the line, `/* */`) to the tree
    (`scriptir::File::comments`: its line, whether a token of the text came before it on that line, its bytes), and the
    writer puts it beside the tree's item by line: an item is a global, a definition (its `Script` line), a rule, a call
    (a For body's too) or the WorldScript (`Block::line`). A comment on a line of its own is written, as `-- text` lines,
    before the first item at or after its line (before a condition: inside the function, lined up with its `and`);
    one after a token of its line goes at the end of the first Lua line of the last item at or before that line; what
    follows the last item goes at the end of the file, and a text the parser stopped in drops what lies past its error.
    Text is UTF-8 (the asset code page's bytes converted unless they already are UTF-8), each line trimmed, a block
    comment one `--` line per line. Comments inside a call's arguments go before the next item; `Dump` leaves comments
    out. A header comment (`-- <name>: converted by ei-convert ...`) is the writer's own.
  - A header comment; for a text the game's parser stops in, the error (what came before stays loaded, as in the game;
    its WorldScript, if the error came after it, the game runs only at the zone reset).
  - `GlobalVars { ... }`: the text's globals (a renamed one with its original name in a comment). A global the text
    declares again under an earlier text's name and never names is a comment only: every use finds the earlier one
    (the plain name is looked up first). 159 AddMob'd and quest mobs re-declare NULL, i and VSS#i#val so.
  - Each definition `Name = Script { rules }` after a comment with its original name and typed parameters
    (`-- VCheck#0#1(this: object)`); a rule is `{ condition = function(params) ... end, action = function(params) ...
    end }`, the condition left out for an empty if-section, the action for an empty then-section (the game's run of an
    empty block ends at its first Execute and never enters the list; a Lua run that returns stays a tick, see
    "Scheduler"); a definition without a body is `Name = Script {}`. A
    condition is `return c1 ~= 0 and c2 ~= 0 ...`: the first zero ends the rule and the later ones are not evaluated
    (their draws not made), a NaN holds, as ScriptIfThen::Run's `== 0.0f`. A declared-script call is a call of the
    definition (`VTriger_0_15(this)`).
  - `function WorldScript() ... end`, the start function: the loader takes it after the chunk and clears the global
    (each text has its own; the zone reset runs every text's again). An empty WorldScript is a comment only, for the
    same reason (its run is gone after the first tick, so nothing sees the difference).
  - Commands are the bindings under their Lua names with the arguments in the written order; `KillScript()` sets the
    run's flag and the function goes on. Assign is `global = value`, a group from a variable `CopyGroup(x)` (the game
    copied; none in the corpus). For / ForIf are `ForEach("i", group, function(i) ... end)` / `ForEachIf("i", group,
    condition, function(i) ... end)`, Every / Any / Sum `Any("i", group, function(i) return ... end)`: the game's
    argument order, the counter global by its Lua name, and the member parameter under the same name, so the body reads
    it where the game's body read the global.
  - Numbers: the shortest text that reads back (as a double the binding rounds) as the parser's float (`54.5`, `72.17`,
    `0.1`), else `%.9g`; `-0.0` and `math.huge` where they occur. Strings quoted, a long bracket for paths
    (`[[nature\thunder\2.wav]]`), bytes outside printable ASCII as `\ddd` (none: the scripts' cp1251 is in comments, which are written as UTF-8).
- **Arguments as functions.** What the game evaluated again on each poll or member:
  - a wait's nested call (`SleepUntil(function() return IsDead(u) end)`, `Sleep(function() return Random(4) end)`);
  - a wait's global, unless it is settled: assigned at most once in the texts so far, at the top level of a
    WorldScript, and no loop counter. A settled global changes only from its zero value, on which Sleep, WaitSegment
    and SleepUntilIdle do not wait, so `SleepUntilIdle(Human5)` passes the value; SleepUntil's global stays a
    function. Every global wait argument of the corpus is settled (945 SleepUntilIdle);
  - Every / Any / Sum / ForIf's condition when it is a nested call (a function of the member); a constant stays a number.
- **Loop counters** (L8c). The game's counter is a global object the loop sets before each step (script_exec.cpp
  `SetLoopCounter`, script_eval.cpp's Every / Any / Sum): For and ForIf walk from the last member to the first and set
  it for every member (ForIf before its condition, a constant one too), so after them it holds the first; Every / Any /
  Sum walk from the first and set it before each evaluation of a nested condition, so it holds the last one tested
  (where Every or Any stopped); a constant condition or an empty group leaves it as it was. The bindings do the same
  with the global the loop names first (`PushMember`, script_lua.cpp: one object value pushed, set into the global and
  passed to the function). So a text reading the counter outside its loop (Rebirth's `listgvar`, a later text, a
  script the body starts) reads the game's value, and the digest and the saves compare the counters.
  - Why a name argument and not the parameter's name (lua_getlocal on the function): what the loop writes is in the
    call as the original has it (`For(i, Heroes)` reads `ForEach("i", Heroes, ...)`), renaming a parameter changes no
    global, a mod's own loop without the name writes nothing (a plain Lua loop over a local), and nothing depends on
    the debug information of a function.
  - The name must be an object global of GlobalVars (else a Lua error); the writer passes the Lua name of the counter
    the parser resolved (a renamed global's: `"VSS_i_val"`).
- **Names.** The `!n!` qualifier dropped, every character but letters, digits and `_` as `_` (`VTriger#0#312` is
  `VTriger_0_312`, `#OnBriefingComplete` `_OnBriefingComplete`, `DogRedF12-422369` `DogRedF12_422369`), `_` before a
  leading digit. A name already given (Lua's keywords and globals, the bindings, the helpers, an earlier global or script
  of the same Script) gets `_2`, `_3`...; a parameter avoids those too (a parameter `this` beside a global `this` is
  `this_2`). Globals and scripts share Lua's one name space. The game's lookups (the plain name, then the qualified
  one; a parameter before a global) are already resolved in the tree, so each reference gets the Lua name of the
  variable the parser found. The corpus needs no suffix and renames no parameter.
- **Checks.**
  - `ei-convert --scripts` (the build check with `--scenarios --quiet`: the samples, the engine texts, the console
    lines; by hand with the installs): each text's Lua is compiled and run in a ScriptLua state after the earlier texts
    of its script; every global its bytecode reads (`tools/convert/lua_globals.cpp`: GETTABUP on `_ENV`) must be
    defined there or a declared global, every global it writes a declared global, a script or WorldScript. It fails on
    those, on a name the writer did not know and on a text assigning a global an earlier text's wait read once.
  - `ei-convert --lua-check` (the build checks, ctest `port_lua`; tools/convert/lua_check.cpp): seven short texts on
    its three stones for 24 ticks: rules whose conditions draw, KillScript going on, starts from an instance (next
    tick) and from a waiting run (same tick), waits on nested calls and on settled and changing globals, group copies,
    loops and Every / Any / Sum, a NaN condition, renamed names, a second text loaded later, number literals, the zone
    reset with an empty WorldScript and an empty then-section, two quests built by QFinish. Each runs straight and
    through a save at every tick (Script::SaveState, LoadState into a new script), every tick's state (the globals,
    instances and runs as the digest reads them, the random draws, the texts loaded) the same as the straight run's
    and no Lua error; with the bindings, the waits, the sandbox, persistence, the cache and an effect of an unknown
    particle type (`CheckUnknownParticleType`: no source, no fatal exit, logged once), 257 checks.
- **Corpus** (`--scripts build\unpacked\vanilla build\unpacked\rebirth --scenarios tests\scenarios`, 13 s): every text
  loads and checks.

  | texts | count | lines of Lua (the original's) |
  |---|---|---|
  | vanilla .mob scripts | 99 | 47,322 (54,245) |
  | Rebirth's .mob scripts | 99 | 47,610 (54,600) |
  | AllHeroes | 11 | 13,314 (15,299) |
  | Lost in Astral | 130 | 50,578 (57,380) |
  | quest texts | 52 | 3,240 (1,952) |
  | engine texts, samples | 13 | 146 |
  | harness console lines | 1,615 | 8,317 |

Examples (vanilla zone1.mob and basecam.mob, a network quest's QFinish text):

```lua
-- zone1.mob: converted by ei-convert from the game's script (docs/notes/lua-scripting.md, "Converted scripts").

GlobalVars {
    NULL = "object",
    VSS_i_val = "object",   -- VSS#i#val
    i = "object",
    Ogre = "object",
    Human_NPC_Male_699 = "object",   -- Human_NPC_Male-699
    ...
    Peasans = "group",
}

-- VCheck#0#1(this: object)
VCheck_0_1 = Script {
    {
        condition = function(this)
            return IsLess(DistanceUnitUnit(this, DgunDragon), 10) ~= 0
                and Not(Any("i", Peasans, function(i) return IsEnemy(i, 0) end)) ~= 0
        end,
        action = function(this)
            KillScript()
            VTriger_0_15(this)
        end,
    },
}

-- VTriger#0#13(this: object)
VTriger_0_13 = Script {
    {
        action = function(this)
            KillScript()
            ForEach("VSS_i_val", Heroes, function(VSS_i_val)
                VCheck_0_1(VSS_i_val)
                VCheck_0_10(VSS_i_val)
                VCheck_0_299(VSS_i_val)
            end)
        end,
    },
}
...
function WorldScript()
    Sleep(2)
    Ogre = GetObjectByID("663")
    ...
    AddObject(Peasans, GetObject(702))
    VCheck_0_137(NULL)
    ...
end
```

```lua
            PlayAnimation(Human5, "uspecial11")
            Sleep(40)
            MoveToPoint(Human5, 72.17, 61.73)
            SleepUntilIdle(Human5)
            PlayAnimation(Human5, "ubriefing09")
```

```lua
-- Su1()
Su1 = Script {
    {
        condition = function()
            return Any("i", Heroes, function(i) return IsLess(DistanceUnitUnit(i, GetObject(1000020)), 7) end) ~= 0
        end,
        action = function()
            KillScript()
            Su2()
            SetGameVar(0, "q.z12q1.z12q1.2", 2)
            SetGameVar(0, "q.z12q1.z12q1.3", 1)
        end,
    },
}
```

## Scheduler

What exists (L4): `src/game/script/script_lua_scheduler.h` (`ScriptLuaScheduler`), owning a `ScriptLua` on a
`Script` (whose effects, temporaries and quest builder the bindings use) and filling its `startScript`; every text the
script loads comes to it ("Texts at runtime"). Every world script runs on one (`Script::runner`, made by
`Script::Runner` at the first load after a Reset). A Reset carries the spell casts going over to the next runner
(`ScriptSpellHost::Hand` persists their entries with Eris, `Take` restores them in the new state with their keys and
effects): a base's or a network zone's objects cast its campfires (items_spells.md) before `LoadZoneScripts` resets
the main script for the zone's own scripts, and the world keeps the casts. The teardowns (ResetZone, TeardownZone,
the same zone loaded again, Destroy) clear the world's casts before the scripts, so nothing carries there; a save's
load (`LoadState`) drops the runner without carrying. Checked by the goldens and `--lua-check` ("Verification").

- **Load(text, name, runWorldScript).** The chunk runs; its `WorldScript` is taken out of the globals and kept (load
  order) for the zone reset, and with `runWorldScript` started as a run at the head of the list, first resumed by the
  next tick (Script::Load). A text the game's parser stopped in (the tree's `ok`) is loaded with
  `runWorldScript` false: the game runs its WorldScript only at the zone reset.
- **Tick()**, Script::Tick's order:
  1. every waiting run continues (`ScriptLua::Continue`: the tick counted, the run current while it runs); a run whose
     function returned on its last resume is dropped now. A run that ends stays in the list until its next Execute
     finds the block ended (Execute returns true after running the last command): a rule's action that does not wait
     still goes to the head of the list and leaves on the next tick, and so does the WorldScript's run;
  2. every instance checks its rules in order until one kills it: the condition with the instance's own values (a
     number holds unless it is zero, a NaN holds; a boolean as it is; a failed condition does not hold), the action at
     once on a new thread with copies (a group as a new one) and the instance's kill flag, then at the head of the run
     list without it; a rule without an action starts no run. A killed instance is dropped;
  3. `ScriptLua::RemoveDeletedObjects` (the game drops the instances' variables before their rules and the globals
     after; every world object removal already purged them through `Script::PurgeDeletedObjects`, which calls the
     runtime's, so the one drop here is the same);
  4. `Script::TickEffects` (the FX sources, point lights and particle sources, then the temporaries freed).
- **Starts.** A call of a definition puts an instance at the head of the instance list, its arguments copied: one a
  waiting run starts checks its rules in the same tick (the instance walk comes after), one an action starts from the
  next tick (the walk has passed the head).
- **ResetZone()**: instances and runs dropped, `Script::ResetEffects` (the temporaries and every effect),
  `ScriptLua::ResetGlobals` (every declared global zeroed), every text's WorldScript started again in load order, each
  at the head (the last loaded runs first).
- **Errors.** A Lua error ends its run (a failed condition does not hold); its message goes to `report`, by default
  the console's `Script error: <chunk>:<line>: <message>`.
- **Spells.** Every scheduler has a `ScriptSpellHost` (`spells`): the scripted spells' casts run in the main script's
  state, beside the mission's runs, their hooks called from the world tick's projectile walk (items_spells.md,
  "Scripted spells"). Their modules have globals of their own; their casts are the persisted table's `casts`.

## Saves

What exists (L6, L9): the scheduler's whole state saves and restores with Eris, and an old-format save's script
state (vanilla's, or ours before L9) resumes in it through the shim. Checked by `--lua-check` (the short texts through
a save at every tick), the scenarios through a save and load (script_quest_save, mod_lua_changed, mod_lua_addmob,
world_load ...) and those that load the user's old-format saves ("Verification").

- **Save / Restore** (`ScriptLuaScheduler`, Eris through `ScriptLua::Persist`): one table persisted:
  - `globals`: every global the runtime did not make or that holds another value now (`ScriptLua::PushGlobals`
    compares with a copy of the globals taken at the end of the constructor, `ei.builtins` in the registry): the declared
    globals, the definitions (`Script {...}` tables), the scripts' functions. `types`: the GlobalVars types. Restore
    sets them into the new state's globals and into its existing types table (the prelude's GlobalVars and
    ResetGlobals hold that table);
  - `instances` in list order: the values table (definition, copies), the count of values (a nil object makes the
    length unreliable), the kill flag;
  - `runs` in list order: the coroutine (false for a run that ended: no thread, dropped by the next tick), the tick
    count and sleep start (the C++ side of the position), the arguments not yet passed (a WorldScript's run not
    resumed yet), the index of the instance its kill flag names (-1);
  - `worlds`: every text's WorldScript, for the zone reset;
  - `names`: the Lua names of the texts converted so far (`scriptlua::SaveNames`, a string), so that a text loaded
    after a restore (a quest, a console line) finds the earlier ones; `texts`, their count (the chunk names).

  An instance shares its definition table with the global in the data, so a restored state keeps one table per
  definition. Restore needs a scheduler that has loaded nothing. Closures persist with their prototypes (Eris), so the
  texts need not be loaded again; the bindings and helpers are permanents.
- **The old-save shim** (`ScriptLuaScheduler::Resume`, script_lua_resume.cpp), into a scheduler that has loaded nothing,
  from a Script an old-format save loaded (vanilla's or ours: `Script::LoadState`):
  - the program comes from the save itself: `scriptir::Loaded` builds a tree of the saved Script (every global by
    type, every definition with its rules, every WorldScript block, each call's arguments in the signature's order,
    which is the written one: the loader filled each type's slots in it; Rebirth's `?` ends the call at the first type
    without a slot, `+` repeats the letter before it), and `WriteLua` converts it as one text. Block ids follow the load
    history and a save keeps running the code it was saved with, so nothing is mapped onto a fresh load of the .mob;
  - it loads without starting a WorldScript; the later WorldScript blocks (an AddMob'd mob's, a console line's) become
    functions of their own (`WriteFunction`, in the names of that conversion) for the zone reset; an empty one starts
    nothing, as the writer's rule;
  - the globals get the saved values by their declared names (a declaration every use finds an earlier one of is left
    out, as in the writer), the instances are made in list order with their saved values and kill flags;
  - each waiting run (always at a top-level command: a wait, index 0 of a WorldScript not yet run, or the end of its
    block) becomes a run of `WriteFunction(block, index, params)`: a chunk giving `return function(params) <the
    block's calls from that command on> end`, the parameters its definition's, the arguments the run's saved values,
    the tick count and sleep start the saved ones, the kill flag the saved instance's (a resumed run with a flag keeps
    it as the game's does, forgotten when that instance dies: Script::Tick's rule, now also the scheduler's). A run at
    the end of its block is a run that ended. The first resume counts the tick and enters the wait at that command
    again, as Execute does.

  Why this form: the converted files stay as written (no resume labels, gotos or pc states in them), and the shim
  needs no knowledge of the files at all. Resuming by replaying an action up to the command would run its bindings
  again (draws, world changes) or need a mode where they do nothing, which breaks the assignments on the way. Since a
  run waits only at a top-level command, the rest of its block is exactly the statements after it, so a function of
  those statements resumes it; once running it is an ordinary coroutine (Eris persists its closure like any other),
  and it ends with its block, so a later save holds no shim but those still waiting.
- **In the save files** (`Script::SaveState`, script_save.cpp): the old format with the scheduler's state in its
  fields as far as they go: no blocks (both counts 0: the Eris state holds the program, L11), the definitions with
  their names, parameters and variables' names and their rule count of if-then pairs {0, the rule's number in the save}
  (a later text's names and its "Multiple declaration" check), then `WriteLuaState`: the declared globals with their
  Lua values, each instance as its definition with its kill flag and values, each run with its rule's number as the
  block, command -1 and its values, a WorldScript's run with block -1; 2 added to `loaded` (`kScriptLoadedOnLua`), and
  after the areas this state (a count and its bytes; one that does not persist is written empty and said on the
  console). The save version stays 0x74: the layout is the old one, so a save of ours from before L11 (with its
  blocks) loads as before, its blocks read and dropped.
  `Script::LoadState` reads it into a new scheduler (`Restore`), or without the 2 resumes the old format by the shim;
  either way the old format's globals, instances and runs are dropped after, and a state that does not go on leaves
  the script empty, said on the console. tools/savefile.py reads the old format's fields (mods.md, "Scripts in
  Lua").

## Mods

What exists (L8): a .mob's `.lua` in the loose-file lookup (`Maps\<zone>.lua`, or the file the .mob.toml's `script`
key names; each layer down to the one that has the .mob) runs in place of the .mob's script: `Script::LoadLua`
(script_lua_mission.cpp) parses and converts the .mob's text for its names only and loads the `.lua` into the
script's scheduler with its WorldScript started; every later text is converted at its load ("Texts at runtime"). The files, the lookup, the saves and the checks are in mods.md, "Scripts in Lua";
the modder's guide in docs/modding.md, "Scripts".

- **The plain conversion plays as the .mob.** mod_lua_plain (zone1.lua as ei-convert writes it) and its twin
  mod_lua_none (the .mob converted at its load) have the same digests, dump, saves and console: the digest's script
  record and the save decoder describe a script in the game's executor's terms (L8a; mods.md "Scripts in Lua", "The
  record"), without block ids, command indices and evaluation slots; the loops' counters hold the game's values
  ("Loop counters").
- **A mod's own code.** Its definitions, coroutines and locals are Lua values like the converted ones: they run in the
  scheduler's order, and an Eris save keeps them (mod_lua_changed: a WorldScript counting in a local goes on after a
  load).
- **Not yet.** A mod cannot add to a .mob's script, only stand for all of it.

## Texts at runtime

What exists (L7, L9): every text a script loads while the game runs goes through `Script::Load`, and its parse tree
goes on to the script's scheduler at once. The texts: QFinish's quest script (script_exec.cpp: built from the QStart / QObj* calls, loaded into the script that runs
it), the console's `@` / `#` / `$` lines (`WorldScript(x)`, `WorldScript( ConsoleFloat( x ))`, `WorldScript(
ConsoleString( x ))` into the world's second script, console.cpp), Rebirth's `execs` files (`LoadFile` into the
mission script, console_addon.cpp), AddMob'd mobs, the zone's .mob and the engine's texts.

- **Script::Load** parses (`Script::Parse`: the parser stays the front end, its declarations resolve the next text's
  names) and hands the tree to `ScriptLuaScheduler::LoadText`, which converts it and loads the chunk, its WorldScript
  started unless the parser stopped in the text.
- **Convert(file, text)**: `WriteLua` in the scheduler's `names` (the earlier texts' Lua names), the chunk `text<N>`
  by the texts converted so far. **Cached per text**: a conversion that leaves the names as they were (sizes of every
  set and map the writer adds to, and the sum of the WorldScript assignment counts) is kept by its key, the
  #define-expanded text and the names its tree resolved (globals, parameters, script calls and assignment targets,
  declarations, the parse error); the same key again takes the Lua from the cache with a new header line (block ids,
  which differ on every load, are not in the key: the Lua does not hold them). A conversion that changes the names
  empties the cache. So a console line that declares and assigns nothing is written once; a quest text, which
  declares its Su scripts, never comes from the cache.
- **Two quests in one script.** QFinish's text declares plain names (`Su0` .. `SuN`); the parser qualifies them with the
  file counter (`!2!Su0`, `!3!Su0` for a second quest) and the second's calls (`Su1()`) resolve to its own (the plain
  name first, then the qualified one); the Lua names are `Su0`, `Su0_2` (the suffix rule), and its calls `Su1_2()`.
  The objectives' conditions read the mission's `i` and `Heroes`, which a console line's script lacks (script_quest's
  quests fail to parse there, quietly, as the game's).
- **Errors**: a Lua load failure is Script::Load's error 18 (`ScriptParseError::Lua`) with the Lua's message, so the
  console line's `Script error: <message>`, execs' `Script error: line 0: <message>` and the zone's `Script error:
  <zone>.mob line 0: ...` report it as they report parse errors; a run's or condition's Lua error goes to the
  scheduler's `report` (the console's `Script error: ...`).
- **Saves after a quest**: an Eris save keeps the names (above); an old-format save resumes by the shim from the saved
  program, the quest texts' blocks included, and a later text converts in the names the shim gave.
- **Checks**: `--lua-check`'s case `quests` (two quests built by QFinish into one script, the areas made later, console
  lines declaring nothing loaded again) with a save at every tick, and its runtime checks (the cache, a run's error
  reported); in the game `script_quest_save` (two quests from an execs file into the mission script, a save and load
  with both waiting, then an execs file that finishes them, console lines repeated) and net_pair_quest (a network
  quest to its QFinish).

## Decisions (user 2026-11-10)

- Readable Lua files are the scripts' modding format (MF9 waits only on L3).
- Lua 5.3 + Eris (Eris has no 5.4; Luau + Ares less proven). Vendored unmodified, pinned.
- Saves: Eris only, old saves still load.

## Verification

- **The goldens.** The normal suite (`--golden --rebirth`) runs every script converted at its load, the converted
  install's (`--converted`) from ei-convert's `.lua` files; both compare with the same goldens (the digest's script
  record and the saves in the game's executor's terms: mods.md, "Scripts in Lua").
- **Old saves.** The scenarios that load the user's saves (tests/saves: render_load_town, render_shadows_mound,
  render_shadows_shimmer, render_shore_seams, mod_lost_in_astral_elevator, _first_pose,
  _unarmed_search) resume old-format saves through the shim.
- **`--lua-check`** (the build checks): the bindings, the waits, the sandbox, persistence, the short texts through a
  save at every tick, the runtime conversion's cache ("Converted scripts", "Checks").
- **`--scripts`** (the build checks on the samples and console lines; by hand with the installs): every text's Lua
  loaded and its globals checked ("Survey", "Converted scripts").
- **Determinism.** The `deterministic` step and the harness's virtual clock make the runs repeatable.
- Before L9 the game's executor ran beside the Lua and was compared call by call over every scenario and the whole
  corpus (0 differ); git history has those checks (L4-L8c).

## Risks

- **Timing.** Same-tick versus next-tick behaviour, and head insertion: the goldens catch it.
- **Floats.** Exactness through Lua numbers, and NaN handling.
- **Rebirth's quirks.** They have to be kept (`DistancePointPoint` reads four floats, `Random` with one argument, and the
  others in `rebirth.md`); the bindings call the same handlers, so they carry over.
- **Speed.** Rules are polled every tick, so each binding call has to stay cheap. Measure on the biggest mission.
