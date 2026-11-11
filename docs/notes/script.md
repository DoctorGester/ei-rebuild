# Mission script engine, text reader and small engine pieces

Files: `src/game/script/` (`script.h`, the records and `Script`; `script_*.cpp`: group, vars, eval, parse,
parse_load, ir, exec, exec_run, exec_helpers, blocks, effects, save, load, addon* (Rebirth, `rebirth.md`)),
`src/game/world/quest_database.cpp`, `src/engine/data/text_manager.cpp`. Scenarios: the sections `script_parse`,
`script_exec`, `script_quest`; `script_copy`, `script_fx`, `script_file`, `script_quest_save`, `console`,
`zone_gz2g_traps`. The Lua runtime, writer, scheduler and old-save shim: `lua-scripting.md`.

## Data structures

- Value types (numbering used everywhere): 1 float, 2 string, 3 object, 4 group.
- `ScriptObjectRef`: a counted reference to a world object (`HeldRef<MapObject>`: every object a script names is a
  map object; its special members are out of line because script.h sees MapObject only declared); a world object
  deleted while referenced has bit 0 of `flags`. `ScriptGroup`: references kept sorted by object address (unsigned)
  without duplicates.
- `ScriptVars`: per type a value array and a name array.
- `ScriptCall` (a Lua binding's call, or a command of an old-format save's program): id (`ScriptFunction`); for Assign
  and CallScript the target (`target`, `targetType`, `targetName`); argument arrays per type (objects and groups point
  into variables or temporaries); local and global variable bindings `{type, argIndex, varIndex}`; nested calls
  `{type, argIndex, ScriptCall*}`; the For / ForIf body. An old-format save keeps the target in leading argument slots,
  as the game did (Assign: float 0 the variable index, float 1 its type - 1; CallScript: float 0 the definition index,
  string 0 its name; the saved bindings and nested calls count those slots); `ScriptCall::Load` moves it out.
- Commands and functions read their arguments through `ScriptArgs` (`Float()`, `Text()`, `Object()`, `Group()`,
  `FindPlayer()`), in signature order, each type at its own position. One read per statement: a function call's
  arguments are evaluated in no fixed order. The loops (For, ForIf, Every / Any / Sum) re-read their counter and
  condition slots directly.
- `ScriptDefinition`: index, qualified name, parameter type letters ("fso"), its rule count (a second body is
  "Multiple declaration"), an old-format save's if-then pairs {conditions block, commands block}, its variables.
  `ScriptRun`, a Lua run's state: ticks, sleep start, the kill flag (a pointer to the instance's flag; null for the
  WorldScript). An old-format save's instances are copies of a definition whose first field is the "killed" flag, its
  waiting runs `ScriptSavedRun`s (block, index, ticks, sleep start, the instance's index, a copy of the variables),
  both only on the way to the old-save shim.
- `Script`: an old-format save's command block's id is the list's count right after the append (the WorldScript's
  with bit 31 set); the temporaries lists always keep one entry; the loaded flag and file counter; error text / code /
  line. Areas hold parts `{isRect; x, y, x2 | radius^2, y2}` (AddRectToArea / AddRoundToArea make a missing area;
  IsInArea of an unknown id is 0 and makes none). A script body (`LoadScriptBody`) that fails to parse drops the
  rules it added. FX sources are sound sources (volume, radius, idle, placement radius, min / max delay, countdown).
  Point lights: 256 slots.
- The keyword table (`ScriptKeyword`): name, id, function signature ("Fff": result + params) or command signature
  ("of"). Ids: ( 6D, ) 6E, if 6F, then 70, `,` 71, = 72, : 73, WorldScript 74, Script 75, DeclareScript 76, GlobalVars
  77; numbers 7A and strings 7B are not in the table; command 8 is a declared-script call. `CalcFloat`, `CalcObject` and
  `CalcGroup` evaluate the functions by result type. Rebirth's AddNewScriptCommands extends the table.
- The world holds two scripts: its first (block ids resolve in ITS lists whatever script is loading) and the mission
  script (the console's).

## Loader and parser

`Script::Load(text, runWorldScript)` is the entry for mission files (`LoadFile` reads a file first), quest scripts and
the console. The console (cheat word required) turns `@x` into `WorldScript(x)`, `#x` into `WorldScript( ConsoleFloat(
x ))` and `$x` into `WorldScript( ConsoleString( x ))`, loads it into the world's second script (`CommandScript()`, not
the mission script) and runs one script tick; on failure it prints `Script error: <error text>`. The second script is
not in a save: effects, instances and definitions made by `@` lines are gone after a save and load. Rebirth's `execs
<file>` loads into the mission script (the saved one), with the path as the game opens it (`.\saves\gaps.txt` is the
run's `harness_saves`; tools/make_gap_script.py, script_file.txt). A multiplayer client (a hosted game too) cannot run
`@` lines at all.

1. Reset the error state ("No errors found"). 2. Preprocess line by line: `#define name value` lines are collected
   (and become empty lines); in every other line each define's name is replaced by its value (in list order,
   restarting after each replacement; `AddDefine` inserts before the first shorter name else before the last define;
   a value containing its name never finishes expanding). 3. `Tokenize` into tokens (text + line; "no token" is
   "None", -1). 4. Sections by keyword id: `WorldScript(` commands `)` (at most one per file; run at the end when
   `runWorldScript`), `Script name(` params `)(` body `)` (a definition not declared before; after `DeclareScript`
   it is `Script name(` body `)`, once), `DeclareScript name(` params `)` ("Duplicate script" if it exists),
   `GlobalVars(` declarations `)`, anything else "Unexpected '%s'". 5. On success the loaded flag is set and the
   file counter incremented; a failed load does not count the file but definitions and globals added before the
   error stay. A zone's mission script that fails (`WorldServer::LoadZoneScripts`) says so on the console, `Script
   error: <zone>.mob line <n>: <error>` (ours; the original said nothing; Lost in Astral's bz23k, mods.md); so does
   an AddMob'd file's (`MobFile::Load`), whose scripts stay dropped when a name clashes (Lost in Astral's
   zone10hunter.mob, scenario `mod_lost_in_astral_addmob_error`).

Names declared in a file are qualified with the file counter: `QualifyName` gives `!<count>!name` once a file has been
loaded; lookups try the plain name then the qualified one; `FindScript` also retries without each leading `!n!`, so a
console line can refer to a name from the previous console line only if that line failed (a scenario relies on it).
Script bodies are `if(` float functions `)then(` commands `)` pairs. A command is a keyword with a command signature
(For / ForIf are followed by `(` body `)` parsed into another block, which sees the enclosing locals), a declared script
(CallScript: the definition's index and name in the target), or `global = value` (Assign: the variable's index and
type in the target; the value is the one argument). `LoadFunction` parses one call against a signature string (f
float, s string, o object, g group, O loop counter = a global object variable); each parameter is a literal, a local
variable, a global variable (plain or qualified) or a nested call of a function with the right result type.

The parser reads the tokens through a `TokenReader` (`Next(&id)` takes a token and classifies it, `Expect(lexeme)`
checks the next one, `EndOfFileError()` reports on the last line); error codes are `ScriptParseError`. The Load*
functions build the declarations (definitions with their parameters and rule counts, globals) and the text's parse
tree (`script_ir.h`; lua-scripting.md, "Design" and "Survey"), no program of their own. `Script::Parse` stops there
(ei-convert's survey, `Script::LoadLua`); `Script::Load` hands the tree to the script's scheduler, which converts and
loads it and starts its WorldScript; its failure is error 18, ours (lua-scripting.md, "Texts at runtime").

**Lexer** (`ClassifyToken`): lines start at 1 and count `\n` in white space and block comments; a token runs to white
space (a space inside quotes does not end it) or to one of `, ) ( : =` after its first character, at most 255
characters looked at; only the recognised prefix is kept and the rest scanned again (a number, a quoted string, or the
longest keyword the text starts with, case-insensitively), so `(abc` is `(` `abc`, `1.5.5` is `1.5` `.5`, and `Run1` is
`Run` `1`; `/*` starts a comment only at the start of a token and the search for `*/` starts after that token (`/**/2`
does not close it; quoted `"*/"` inside is skipped); `//` runs to the end of the line. `Script::Tokenize` can hand each
comment to the caller (`ScriptComment`, for the Lua writer); the parse is the same. Keywords compare with
`text::CompareIgnoringCase` (src/lib/text_case.h), keeping `_strnicmp`'s rule that a zero byte in either string ends
the compare; the keywords are ASCII, so the UTF-8 fold finds the same keyword as the original code page fold (the
original's locale-aware `_strnicmp` per byte cost ~400 ms of a zone load).

## Runtime

The world calls `Script::Tick` every world update for its two scripts; the tick is the Lua scheduler's
(lua-scripting.md, "Scheduler"), whose bindings call the built-ins (`ScriptCall::CalcFloat` / `CalcObject` /
`CalcGroup`, `scriptaddon::CalcString`, `ScriptRun::RunCommand`). The original executor's rules, which the scheduler
and the bindings keep:
- A rule's conditions run in order, the first zero ending it (a NaN holds); when all hold the commands run once in a
  new run (a copy of the instance's variables, kill flag = the instance). A run that ran to its end leaves at its next
  tick, so it stays in the run list one tick.
- Timed effects at the tick's end: FX sources respawn their effect when the countdown runs out (`Next() % (max - min +
  1) + min`, a class 0x48 object at a random offset within the radius), point lights move for their remaining steps
  then stop, particle sources move, count down their lifetime and are removed; then the temporaries are freed. The
  WorldScript and every console `@` line first run on the next tick.
- A wait's argument is evaluated again on every poll. Sleep(n) records the tick count and waits while `ticks - start <
  n` (n FISTP-rounded; so n further polls), then resets it to -1; WaitSegment(n) waits while the run has been executed
  fewer than n times in total; SleepUntil(c) while c is 0 (a NaN goes on); SleepUntilIdle(unit) while the unit's brain
  has an action.
- For(counter, group)(body) / ForIf(counter, group, condition)(body) walk the group from last to first (size read
  once), store the member in the counter (a global object variable) and run the body in the same run: a wait in the
  body ends the body for that member.
- A script call (command 8) adds an instance at the HEAD of the instance list and copies the arguments into its
  variables (the parser checked their counts; a mismatch ends the game: "Wrong number of script %s float parameters"
  etc.); an instance started by a waiting run runs its rules in the same tick, one started by another instance's
  commands from the next tick. Assign (0x78) copies floats as bits, strings, objects (reference) or groups.

The FX sources: CreateFXSource (0x16) makes the sound at once and clears `idle`; CreateRandomizedFXSource (0x18) keeps
no sound and takes min delay, max delay (FISTP) and spread, countdown 0, idle 1. A negative id takes `nextFxId`
(counted down). For a new id 0x16 left spread, delays and countdown as stack garbage in the original; ours writes 0, 0,
0 and INT_MAX (the sound stays, as with any positive garbage).

## Savegame writer and reader

`ScriptSave` order: point lights, the particle / FX / lightning lists, two state ints, command blocks and WorldScript
blocks (block id, count, each command; ours writes both counts 0: the Lua state holds the program), definitions,
globals (floats, strings, objects, groups), instances, runs (each with the index of its instance, found by comparing
its kill-flag pointer with each instance, -1 when absent), the loaded flag, the file counter, then the areas (read only
for stream versions >= 0x57). Lists are written as count then each node; a variable's name is written with its
terminator, or as one zero byte when the name array is shorter than the value array. Effect elements: object id (-1
none), then fields; loading looks the id up in the effect manager. `ScriptLoad` mirrors it: list loaders replace the
list with that many default nodes and read each.

Block ids resolve in the lists of the world's first script (negative ids in the WorldScript blocks, others in the
command blocks; unknown gives null). A definition's or instance's if-then pair and a run whose block did not resolve (a
save made with other scripts) are dropped at load, and a For / ForIf whose body did not resolve is cleared into a
command that does nothing (id 0; the block keeps its length, so runs' saved indices stay right), so the old-save shim
always has its blocks; the program is dropped after the shim.

Our saves write the scheduler's globals, instances and runs in those fields as far as they go and its Lua state after
the areas, 2 added to the loaded flag (lua-scripting.md, "Saves"); a definition's (and an instance's) if-then pairs are
its rule count of pairs {0, the rule's number in the save, from 1 in definition order}, and a run's block that number
(-1 for a WorldScript's), so the decoder names a run's rule; the loader drops the pairs (no block resolves) and keeps
the count. An old-format save (vanilla's) goes on through the old-save shim; one of ours that still holds blocks loads
the same way, its blocks read and dropped. Runs: block id, index, ticks, sleep start, the instance index (0 out of
range), the variables (object ids looked up, group members sorted).

## Quests, unit modes, text manager

- Quest construction (`Script::quest`, each script its own): `QStart` (empties the objectives `{type, text, value}`),
  `QObj*` (an objective appended), `QFinish` builds a script text and loads it: `DeclareScript Su0()` .. `SuN()`, per
  objective `Script Sui(if(` condition `)then(KillScript() Sui+1() GSSetVar(0,"q.q.q.i+1",2)` (and
  `GSSetVar(0,"q.q.q.i+2",1)` except for the last), the closing `SuN` (QuestComplete, GSSetVar "q.q.q" 2), `WorldScript(
  Su0() )`; then the quest name is emptied (the objectives stay). QuestComplete looks the quest kill table up by name (a
  missing name gets a null entry).
- Unit mode commands make the modes of `unit_ai.h` (Guard, Sentry, Follow, Suspection, Aggression (UMRevenge: `fixed`),
  CorpseWatcher, AILook); the brain owns them (unit_ai.md).
- Text manager line reader (`TextManager`): the cursor walks the current text; every end test is the signed `(p - text)
  < length`. Open asks each archive in list order; PeekWord (the cursor does not move); SkipLine; NextLine (past the
  line and every line whose first word starts "##"); ReadLine; ReadToHash; Split (a word still open when the text ends
  is dropped); FindLine (key null = the next line whose first word starts with "#" but not "##"; a one-word key = the
  next line whose first word equals it; several words = a line whose first words, lower-cased, equal the key's). The
  archive list keeps a failed archive (unlike `LinkFigureLoader::AddArchive`).

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9). Crashes, undefined behaviour and
invisible bookkeeping are fixed, not listed.

- NaN: `IsEqual` and `Not` treat NaN as equal, `IsLess` as less; `IsInSquare`, `UnitInSquare`, `IsInArea` the same
  mixed way; `SetDiplomacy` maps NaN to neutral; flag commands treat NaN as zero. `Every` with no nested condition
  copies the constant argument as the result.
- Parser: keyword prefixes split identifiers; `/**/` as one token does not close the comment; a For / ForIf whose
  header fails still parses its body (usually "Wrong lexeme ,").
- Runtime: `SetSpellAggression` (0x42) does nothing; a For counter is a global, so nested loops on the same counter
  overwrite it; `CreatePointLight` stops at the first free slot (two lights can share an id); `CreateLightning` appends
  without looking the id up (two bolts can share an id); `CreateParticleSource`'s id is FISTP-rounded.
- Savegame: block ids resolve in the first world script's lists whatever script loads.

gz2g's scripts (zone2.mob's script chunk, decoded by seeding the MSVC rand LCG with the first dword, `x = x * 0x343FD +
0x269EC3`, each byte XORed with `x >> 16`) start every magic trap armed and disarm them from triggers; a killed instance
can reach command 8 (`KillScript() VTriger#0#n( this )`).

## Leftovers: streams, matrices, text, zone exits, .mob, party manager, weather, sky, clouds

- Memory stream: Write grows to (capacity + size) * 2 copying the old capacity's bytes; Resize grows to size * 1.5 +
  0x10 copying the old size's bytes and flushes the pending bits byte only when it grows; SetSize resets only bitMask; a
  short Read zero-fills the whole request before copying what is left. Growing buffer (`GameBuffer`): every growth makes
  a block of 2 * (needed end) + 0x40 bytes; Reserve and WriteMore grow without testing the room (callers test). A
  buffered file stream is a `GameBuffer` whose block is a window onto a file.
- Stream diff (`WriteDiff`, `ApplyDiff`): `ApplyDiff` patches run headers in place and follows a full run of 255 by an
  empty run of the other kind.
- Matrices (projection.cpp, pipeline.h): each product element sums its four terms in the original's order (two orders:
  `kProductOrder` / `kPushOrder`), pinned with StoreFloat against /fp:fast regrouping; elements are written in memory
  order, so an output that is also an input behaves as in the original.
- Text helpers (text_split.h): Split, SplitWords, ReplaceChar, Join.
- Zone exits: Read / Write (dwords, the target with terminator, then bytes and the angle; the angle from version 0x68
  on). `Contains`: a zero or NaN angle tests the rectangle as is (NaN passes); otherwise the point is turned by -angle
  about the centre and a NaN turned x is outside.
- .mob: `MobFile::ReadChunks` (scripts, weather, records, players); `MobFile::Load` (relations applied, traps get their
  hostile mask and origin, units their alarm records, records deleted, scripts loaded unless keepIds). The wind
  strength is clamped by a float nothing writes (0), so ChangeWind gets at most 0; the weather chunk's last two floats
  are read and unused.
- Party manager (party_manager.h): parties by id, entries, records, class templates; Save / Load through ChunkArchive.
  `BeginRedeploy` spreads the variables, zone and flags to every record; `EndRedeploy` clears the party when no record
  matches. Trader stock: class templates generate items and magic per level.
- Rain / snow (`RainSnow`): Clear (rain loop stopped, density 64), Start(mode, duration) (rain plays the loop
  "nature\rain\circle.wav" at priority 1000; the fade starts at the world's tick).
- Sky: haze is recomputed only when the vertex count changes (0 for vertex z < 1, else sqrt(-normal z)); diffuse =
  ambient * 255 (alpha 0xFF; 0 for hazeless vertices, which get no specular); specular = haze * 255 alpha and light *
  max(0, n . dir)^2 * 150, all truncated.
- Clouds (global map): SetRamp's alpha is trunc(255 - pow(width / 255, max(0, i - start)) * 255) >> 4; Draw's blend is
  trunc((step + phase) / 16 * 254 + 1).
- Attachment points: 1-7 = "hd", "hd" + 1 in z, "lh2", "rh2", "ll2", "rl2", "bd"; 8-12 = "body00".."body04"; every part
  falls back to "bd", then to the position raised by the bounds' upper z extent. Not a unit or attachPoint > 12: the
  position raised by radius * 1.6 or the box height.
- `ComputeFaceNormal`: edges from vertex 1 normalised unless below 1e-10 squared, crossed, normalised; a zero or NaN
  length takes the default normal.
