# Lua scripting: plan

Goal: the game's mission scripts as readable Lua files that mods edit (ei-convert writes them from the originals,
which keep loading), and mods' own scripts in Lua, with running scripts (coroutines included) kept in saves. Revised
2026-11-10 with the user: readable Lua over a hidden compile step, Lua 5.3 + Eris, saves ours only. Tickets L1-L9: `python tools/tickets.py list --group lua`. Engine details are in
`script.md`; Rebirth's extensions are in `rebirth.md` ("AddNewScriptCommands").

## What the current engine is

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
  AddMob's load (MobFile::Load) ignores the error, so those zones play without the added scripts, silently (L1a).
- **Block ids.** Only saves use them (if-then pairs, For bodies, runs). They count the blocks in load order (the
  mission, then AddMob'd mobs and QFinish texts as the game runs them), so the same file gets other ids after another
  load history. A saved run is always at a top-level command (no waits in loop bodies): on a wait, at index 0 of a
  WorldScript not yet run, or at the end of its block.

### What it changes in L2-L9

- **L2 (bindings).** All 276 are generated from the table; 160 are reached by the game's content, 88 more only by
  harness lines, 27 by nothing (bindings without coverage: lockstep cannot check them). Rebirth's string and maths
  functions are reached only by harness lines.
- **L3 (writer)** is smaller than planned: no wait in a loop body, no nested loop, no ForIf, no #define and no written
  `!n!` in any shipped text, so the end-the-body rule needs no special form in converted missions (the runtime keeps it
  for mods). Two idioms cover nearly everything: the started task (one unconditional rule, KillScript first) and the
  once-firing trigger; KillScript converts to a flag that lets the function go on. Names need a mapping (`#`, `-`).
  Comments do not reach the tree; carrying mods' comments needs the lexer to keep them (open).
- **L4 (scheduler).** KillScript-then-go-on is the main path (86% of kills), and a killed instance starting the next
  script (5,416) is common; the lost kill flag of a waiting run is not reached by content.
- **L5 (lockstep).** The corpus is the 339 .mob scripts with their AddMob / quest parents; the hot condition bindings
  are IsEqual, GSGetVar, IsLess, GetObject, GetLeverState.
- **L6 (saves).** Resume points are only top-level waits, starts and ends, so the old-save shim enters a converted block
  at a top-level label (Lua 5.3's `goto` at function level is enough). Since block ids follow the load history, the
  shim should build its tree from the save's own program (the saved blocks, as `DumpLoaded` walks them), not map ids
  onto a fresh load of the .mob.
- **L7 (quests, console).** 26 distinct quest texts, multiplayer only; their objective arguments are expressions and
  need the mission's `i` and `Heroes`. The engine's texts are five fixed forms; the console's are three wrappers.
- **L8 (mods).** The mods use the vanilla built-ins plus GameOver / GameOverMSG; Lost in Astral's AddMob name clash
  shows that mod scripts loaded into one script need their own name space.

## Design

- **Front end.** The existing parser fills a parse tree beside its own structures (`src/game/script/script_ir.h`,
  `scriptir::File`; `Script::Load`'s optional `ir`). That keeps the lexer quirks (`Run1` lexes as `Run 1`; Rebirth's
  stricter match makes `Orc1` an identifier), #define expansion, `!n!` name qualification and the parse errors. The
  tree holds the sections (globals, WorldScripts, definitions with their parameters and rules), each call with its
  keyword id, the token as written, its line, its arguments in the written order (each with its type, slot and kind:
  number, string, parameter, global, nested call) and the block id the loader gave each block; Assign and declared-script
  calls name their target. The Lua writer never reads script text itself.
  - `scriptir::Dump` prints the tree, `scriptir::DumpLoaded` prints what a load added to a Script in the same form (by
    type and slot: the loader keeps no other order); `ei-convert --scripts` compares the two for every text it loads
    (Survey below). The build runs it on the samples in `tools/convert/convert_scripts.cpp` and the scenarios' console
    lines (`--quiet`, no game data).
- **Built-ins and data stay in C++.** Lua drives control flow and calls bindings generated from the keyword table, which go
  to the same CalcFloat / CalcObject / CalcGroup / command code by id, under readable Lua names ("Bindings" below).
  - Object references and groups are userdata over the existing types: reference counts, deleted-object dropping, and
    groups kept sorted (by object id, script.md).
  - Floats stay single precision. A float→double→float round trip is exact; check that NaN payloads survive, since
    assignment copies bits.
- **Readable Lua (L3).** The old engine only waits at a top-level command of a block (a wait inside a For body ends
  that member's body), so a script converts to plain code: a rule set as condition and action functions, waits as
  calls that yield (`Sleep`, `WaitSegment`, `SleepUntil`, `SleepUntilIdle`), For / ForIf as
  `ForEach(group, function(member) ... end)` keeping the original's end-the-body rule, the WorldScript as the start
  function. ei-convert writes `<zone>.lua` beside MF9's .mob TOML and the game loads it through the loose-file lookup
  (mods.md); the original .mob's script converts at load the same way. No pc state machines: a modder reads and edits
  the file.
- **Scheduler.** It keeps Script::Tick's order exactly:
  - waiting runs first, then instances, then dropping deleted objects, then timed effects, then freeing temporaries;
  - a new instance goes at the head of the instance list;
  - an instance started by a waiting run runs in the same tick, and one started by an instance's commands runs from the
    next tick;
  - Execute counts calls, which WaitSegment uses;
  - Sleep counts ticks as the original does.
- **Saves (L6).**
  - New saves persist the whole Lua state with Eris: game-script coroutines and mod coroutines alike. The bindings form the
    "permanents" table; object references and groups persist as object ids through `__persist`, as the current save writes
    them. Waits yield from Lua, never from inside a C++ call.
  - Saves are ours only: game.exe is no longer a target (own goldens), so no old-format writer.
  - Old saves (vanilla, or ours from before Lua) load: each saved run (block, index, ticks, sleep start, locals) resumes
    through a shim that enters the converted block at that command.
- **Mods (L8).**
  - `.lua` files loaded beside or instead of the mission's (the loose-file lookup; replace as MF3's records).
  - The same built-ins under Lua names, with waits as yields.
  - A sandbox without io / os / debug.
  - Errors reported like "Script error: ...".

## Bindings

What exists (L2): `src/game/script/script_lua.h` (`ScriptLua`), Lua 5.3.5 with Eris in `third_party/eris`
(README.txt there: Eris's master-lua5.3 tree, unmodified, built as C++). Nothing in the game creates a `ScriptLua` yet.
Checked by `ei-convert --lua-check` (build checks; ctest `port_lua`) and the unit check `lua_number.cpp`.

- **The state.** Libraries base (without `dofile` / `loadfile`), coroutine, table, string, math, utf8; no io, os,
  package or debug (L8 decides the rest: `math.random` and `print` are still there). The runtime of a state is in its
  extra space (`ScriptLua::Of`).
- **Generated from the keyword table.** The constructor walks `KeywordTable()` (the game's table with Rebirth's):
  every entry with a function or command signature gets a binding, 276; the 11 punctuation and section words none.
  A binding is one C closure (`CallBinding`) over that keyword's signature and a `ScriptCall` it reuses: the Lua
  arguments go into the call's slots as `LoadFunction` would put them (Rebirth's `?`: the rest optional, `+`: the
  letter before it repeated), then the same `CalcFloat` / `CalcObject` / `CalcGroup` / `scriptaddon::CalcString` runs
  by id, or for a command `ScriptRun::RunCommand` (Execute's step, split out for this) in the current run; the
  temporaries are freed after each call. Too few or too many arguments, or a value of the wrong type (no string for a
  number or a number for a string) raise a Lua error.
- **Special keywords** (no nested call or body in Lua): `For` / `ForIf` are `ForEach(group, body)` /
  `ForEachIf(group, condition, body)`, members from last to first, the size read once; each body runs on a thread of its
  own from a fresh position in the same run, and a wait in it ends the body for that member (RunBody's rule).
  `Every` / `Any` / `Sum(group, condition)` call the condition for each member from the first (the size read again
  each time); a number for the condition is the game's call without a nested one (`Every` copies it, `Sum` multiplies
  it by the size). A condition gives a number or a boolean.
- **Waits** yield from Lua: `Sleep`, `WaitSegment`, `SleepUntil` and `SleepUntilIdle` are Lua functions (the prelude in
  script_lua.cpp) that poll their command's binding once per resume (`RunCommand`, so the run's ticks and sleep start
  count as Execute's) and `coroutine.yield()` while it waits. An argument may be a function, called on every poll: the
  game evaluated a nested call again on each Execute (`Sleep(Random(n))` draws every tick; `SleepUntil(c)` needs it).
  `ScriptLua::Continue(thread, run, ...)` is Execute's step for a Lua run: it counts the tick and resumes with `run` as
  the current run (KillScript sets its kill flag; outside a resume a run of its own without a flag, as the
  WorldScript's). `--lua-check` compares the tick counts with the game's executor.
- **Values.** A float is a Lua number (double) exactly; a number passed in rounds to the nearest float
  (`script_lua_number.h`); NaNs keep sign and payload bit for bit both ways, signalling ones too (by bit operations,
  not the FPU). Strings are Lua strings (UTF-8 bytes). Objects and groups are full userdata over `ScriptObjectRef` /
  `ScriptGroup` (the counts held as the variables hold them; groups in id order through ScriptGroup's own methods;
  `#g`, `g[i]` from 1; two values of one object are `==`, though each read is a new value, so not one table key). A
  function result of no object is `nil`, and `nil` passes a null object. Every live value is linked into its runtime's
  list: `ScriptLua::RemoveDeletedObjects` drops deleted objects from all of them, as the tick and
  `Script::PurgeDeletedObjects` do to the variables (L4 calls it there). `NewGroup()` and `CopyGroup(g)` are the two
  helpers: a group value is shared by reference, the game's assignment copied.
- **Persistence.** `ScriptLua::Persist` / `Unpersist` (Eris) with permanents built at start: the globals table itself
  and every function in it and in the libraries' tables by name (`_G.GetObject`, `coroutine.yield`), and the hidden
  helpers. So the globals are not in the data (L6 has to save the scripts' own globals another way), and a coroutine
  waiting in `Sleep` resumes in the new state's own `Sleep`. An object persists as its id, a group as its members' ids
  (`__persist`, looked up in the server world as `ScriptObjectRef::Load` does; a missing object comes back as a value
  of none). The run's position (ticks, sleep start) is C++'s, not in the data: L6 keeps it beside the coroutine.

**The naming rule.** A keyword's Lua name is its own (the table's spelling) except where it abbreviates or misspells;
`kRenames` in script_lua.cpp is the list (72): `UM…` → `UnitMode…` (`UMAg` → `UnitModeAggressionLevel`,
`UMSuspection` → `UnitModeSuspicion`); `GS…Var` → `…GameVar` (`GSSetVar` → `SetGameVar`, `GSDelVar` → `DeleteGameVar`,
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

For L3 (the writer):
- Write each float with 9 significant digits (`%.9g`): Lua reads it as a double and the binding rounds it to the same
  float the parser made.
- Lua arithmetic is in doubles: one `+ - * /` of two floats rounded to a float equals the float operation, a chain does
  not, and `Add(a, b)` is `b + a` in floats; keep the bindings (`Add`, `IsEqual`, whose NaN rules differ from Lua's
  `==`) unless an expression is known exact.
- A nested call in a wait's argument becomes a function (`SleepUntil(function() return IsDead(u) end)`); a nested call
  in Every / Any / Sum / ForIf's condition becomes a function of the member; a constant stays a number.
- The game's loop counter is a global object variable: after a For it holds the first member, after Every the last
  evaluated. A converted loop's member is a parameter; write the global too where the script reads it later.
- Group assignment copies (`h = CopyGroup(g)`); object assignment shares the value (objects are immutable from Lua).

## Decisions (user 2026-11-10)

- Readable Lua files are the scripts' modding format (MF9 waits only on L3).
- Lua 5.3 + Eris (Eris has no 5.4; Luau + Ares less proven). Vendored unmodified, pinned.
- Saves: Eris only, old saves still load.

## Verification

- **Lockstep (`luaboth`).** The old executor and Lua run side by side on the same world. Each tick compares the variables,
  run positions, instance lists, and a log of every built-in call with its arguments.
- **Coverage.** Every scenario, plus a corpus that loads each mission's scripts and runs N ticks.
- **Saves.** Round trips: old save loaded into Lua, Lua save loaded into Lua, and old save loaded into the old engine and
  compared.
- **Determinism.** The `deterministic` step and the harness's virtual clock make the runs repeatable.

## Risks

- **Timing.** Same-tick versus next-tick behaviour, and head insertion: lockstep catches it.
- **Floats.** Exactness through Lua numbers, and NaN handling.
- **Rebirth's quirks.** They have to be kept (`DistancePointPoint` reads four floats, `Random` with one argument, and the
  others in `rebirth.md`); the bindings call the same handlers, so they carry over.
- **Speed.** Rules are polled every tick, so each binding call has to stay cheap. Measure on the biggest mission.
