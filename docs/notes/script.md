# Mission script engine (0x004B0000-0x004D8000), text reader (0x004DEFC0-0x004DFFFF) and the rest of 0x00480000-0x00500000

Files (`src/game/`): `script.h` (every layout with offset asserts, including the whole `CScript`, 0x1F8 bytes),
`script_*.cpp` (group, vars, eval, parse, parse_load, parse_arrays, exec, exec_run, exec_helpers, save, load, vars_append,
copy, destroy, arrays, blocks, reset, cleanup, effects, quest, unit_modes, addon* (Rebirth, `rebirth.md`)), `text_reader.cpp`.
Verify: `tests/verify/verify_script*.cpp` (+ `script_compare.h`), `verify_text_reader.cpp`. Scenarios: `script_parse`,
`script_exec`, `script_copy`, `script_fx`, `script_quest`, `world_load` (save / load round trip), `console`, `effects`,
`particles`, `zone_gz2g_traps`. Nothing rewritten here lies in Rebirth's patched range 0x004CF4A2-0x004D460C except where
marked "Rebirth".

## Data structures

- Value types (numbering used everywhere): 1 float, 2 string (CString), 3 object, 4 group.
- `ScriptObjectRef` (4 bytes): counted reference to a world object (`refCount_` at +4; Release is slot 4); a world object
  deleted while referenced has bit 0 of `flags_`. `ScriptGroup`: `CArray<ScriptObjectRef>` (vtable 0073D110, its destructor
  sets 0073D11C), kept sorted by object address (unsigned) without duplicates; `SetSize` is 004C2050.
- `ScriptVars` (0xA0): per type a value CArray and a CStringArray of names: floats +0x00 / names +0x14, strings +0x28 /
  +0x3C, objects +0x50 / +0x64, groups (0x14 each) +0x78 / +0x8C.
- `SSCCmd` (0x94): id; argument arrays floats +0x04, strings +0x18, objects +0x2C (`ScriptObjectRef*` into variables or
  temporaries), groups +0x40 (`ScriptGroup*`); local variable bindings +0x54 and global bindings +0x68 (`{type, argIndex,
  varIndex}`); nested calls +0x7C (`{type, argIndex, SSCCmd*}`); +0x90 a block pointer (called script, or a For / ForIf
  body; its +0x14 is the block id).
- `ScriptDefinition` (0xC0; CArray at CScript+0x8C): +0 index, +4 qualified name, +8 parameter type letters ("fso"), +0x0C
  CArray of `ScriptIfThen` {conditions block, commands block}, +0x20 `ScriptVars`. `ScriptRun` (0xB4): block, index, ticks,
  sleep start, kill flag (`int*` to the instance's flag; null for the WorldScript), `ScriptVars` copy at +0x14. Instances
  (CList nodes 0xC8) are copies of a definition whose +0 is the "killed" flag; runs are CList nodes 0xBC.
- `CScript`: a command block's id is the list's count right after the append (the WorldScript's with bit 31 set); the
  temporaries lists (+0x1B0 groups, +0x1CC object references) always keep one entry (the constructor adds it); +0x1E8 counts
  the For nesting (temporaries are not freed while it is positive); +0x1A8 loaded flag, +0x1AC file counter; +0x1EC / +0x1F0
  / +0x1F4 error text / code / line; +0x68 / +0x6C are -2 after a reset. Areas (+0x194) hold 0x14-byte parts `{u8 isRect; x,
  y, x2 | radius^2, y2}`. FX sources (0x34 bytes) are sound sources: +0x20 / +0x24 volume / radius copied to the effect's
  +0x24C / +0x250, +0x28 `idle` (the tick skips it), +0x2C placement radius, +0x30 / +0x34 min / max delay, +0x38 countdown.
  Point lights: 256 slots of 0x20.
- Keyword table at 0078BCA0 (`ScriptKeyword`, 16 bytes, null-terminated at 0078CAB0): name, id, function signature ("Fff":
  result + params) or command signature ("of"). Ids: ( 6D, ) 6E, if 6F, then 70, `,` 71, = 72, : 73, WorldScript 74, Script
  75, DeclareScript 76, GlobalVars 77; numbers 7A and strings 7B are not in the table; command 8 is a declared-script call.
  `CalcFloat` handles the float functions, `CalcObject` the object ones (0x4E, 0x85, 0xAB, 0xB4, 0xE4), `CalcGroup` (004B5B00)
  the group ones. Particle type table 0078B120 (64 entries: name, type, lifetime).
- Globals: world 007AFF60 (FindObject 00593AA0, GetPlayer 00594180, TerrainHeight 00594220), game time int 007AFFC4, alarm
  slots 007AFFEC, IsNight float 007AFFE4, ISAAC 007A9D40, diplomacy 007C2BC4, game server 0079B5DC, empty string 0079B298,
  the mission script 007C29CC (the console's; world +0x12A6C), the world's first script 007C27D4 (world +0x12874; block ids
  resolve in ITS lists whatever script is loading), 007AA620 the script being saved / loaded, 007AA608 / 007AA61C the quest
  under construction.

## Loader and parser (004CF380-004D36F0)

`CScript::Load(text, runWorldScript)` 004D23A0 is the entry for mission files (`LoadFile` 004D36F0 reads a file first),
quest scripts and the console. The console (005DC620, cheat word required) turns `@x` into `WorldScript(x)`, `#x` into
`WorldScript( ConsoleFloat( x ))` and `$x` into `WorldScript( ConsoleString( x ))` and loads it into the mission script
007C29CC (005785D0, which then runs one script tick, 004BF000); on failure it prints `Script error: <error text>`.
1. Reset the error state ("No errors found"). 2. Preprocess line by line: `#define name value` lines are collected (and
become empty lines); in every other line each define's name is replaced by its value (in list order, restarting after each
replacement; `AddDefine` inserts before the first shorter name else before the last define; a value containing its name
never finishes expanding). 3. `Tokenize` into a `CList<ScriptToken>` (vtable 0073D424; token = text CString + line; "no
token" is "None", -1). 4. Sections by keyword id: `WorldScript(` commands `)` (at most one per file; run at the end when
`runWorldScript`), `Script name(` params `)(` body `)` (a definition not declared before; after `DeclareScript` it is
`Script name(` body `)` with no parameter list, once), `DeclareScript name(` params `)` ("Duplicate script" if it exists),
`GlobalVars(` declarations `)`, anything else "Unexpected '%s'". 5. On success the loaded flag is set and the file counter
incremented; a failed load does not count the file but definitions and globals added before the error stay.

Names declared in a file are qualified with the file counter: `QualifyName` 004D37F0 gives `!<count>!name` once a file has
been loaded; lookups try the plain name then the qualified one; `FindScript` 004D3780 also retries without each leading
`!n!`, so a console line can refer to a name from the previous console line only if that line failed (a scenario relies on
it). Script body `if(` float functions `)then(` commands `)` pairs (`LoadScriptBody` 004D1030; conditions `LoadConditions`
004D0BF0, commands `LoadCommands` 004D1330) go into command blocks from `NewCommandList` 004BF540. A command is a keyword
with a command signature (For / ForIf are followed by `(` body `)` parsed into another block at SSCCmd+0x90, without the
script's locals), a declared script (command 8: float 0 = script index, string 0 = name, the parameter types as signature),
or `global = value` (`LoadAssignOperator` 004D1D50: command 0x78, float 0 = variable index, float 1 = type - 1).
`LoadFunction` 004CFBC0 parses one call against a signature string (f float, s string, o object, g group, O loop counter = a
global object variable); each parameter is a literal, a local variable (binding +0x54), a global variable (plain or
qualified, binding +0x68) or a nested call of a function with the right result type (`FunctionSignature` 004D0B10).

**Lexer** (`ClassifyToken` 004CF600): lines start at 1 and count `\n` in white space and block comments; a token runs to
white space (a space inside quotes does not end it) or to one of `, ) ( : =` after its first character, at most 255
characters looked at; only the recognised prefix is kept and the rest scanned again (a number, a quoted string, or the
longest keyword the text starts with, case-insensitively), so `(abc` is `(` `abc`, `1.5.5` is `1.5` `.5`, and `Run1` is
`Run` `1`; `/*` starts a comment only at the start of a token and the search for `*/` starts after that token (`/**/2` does
not close it; quoted `"*/"` inside is skipped); `//` runs to the end of the line. The keyword table address is read from
ClassifyToken's code (immediate at 004CF69B, which Rebirth repoints at its extended table).

**PF1, keyword compare without a CRT call per byte.** The original compares the token with every keyword through the game
CRT's `_strnicmp` (006FA1A0); under the game's locale (`__lc_handle[LC_CTYPE]` 007CABF0 nonzero) each byte goes through
`_tolower_lk` (006EBAC2), which sends upper-case bytes through `__crtLCMapStringA` (006F1FDE) and LCMapStringW. Ours
tables the CRT's own `tolower` (006EBA53) for all 256 bytes (`gamecrt::CaseFoldTable` in src/lib/crt since PF1a, rebuilt when the LC_CTYPE
handle or `__lc_codepage` 007CAC00 changes) and compares with `SameIgnoringCase`, which keeps `_strnicmp`'s rule that a
zero byte in either string ends the compare (equal only when both are zero; the locale branch tests the raw bytes for
zero, not the folded ones). Timing (EI_PLAIN=1, EI_PROFILE=1, world_load vanilla, one run each): the new game's zone
load (the `waitfor ... HP(GetLeader())` step) 615 ms -> 225 ms; samples with ClassifyToken on the stack 203 -> 6 over
the run (about 400 ms -> 12 ms). Verified: the ClassifyToken wrapper (ours against the original, id and rest) with
004CF600 taken out of the build's verified.txt for the run: 0 mismatches in 26k-40k calls per run over script_copy,
script_exec, script_fx, script_parse, script_quest and world_load on both installs (the Rebirth runs compare against
Rebirth's patched code with the AddNewScriptCommands table and its RejectsMatch); plus a one-time check
"ClassifyToken case fold" that compares SameIgnoringCase with the game's `_strnicmp` for all 65536 byte pairs (0
differ). 004CF600 stays in verified.txt, so a normal suite run does not reach the wrapper.

## Runtime (004B6760, 004BB520-004BF000)

The world calls `ScriptTick` 004BF000 ("CommandQueue::Run") every world update for its two scripts. A tick, when loaded:
1. every waiting run (+0x15C) continues with `ScriptRun::Execute`; one returning 0 is removed; 2. every instance (+0x140)
drops deleted objects from its variables and runs its if-then pairs in order (`ScriptIfThenRun` 004BB520: conditions with
CalcFloat, temporaries freed after each, the first zero or NaN ends it; when all hold `ScriptRunCommands` 004BC420 runs the
commands block once in a new run (index 0, a copy of the instance's variables, kill flag = the instance) and, if Execute
returns 1, a copy goes to the head of the run list) until one kills it; 3. global object variables and groups drop deleted
objects; 4. timed effects: FX sources respawn their effect when the countdown runs out (`Next() % (max - min + 1) + min`,
class 0x48 object at a random offset within the radius), point lights move for their remaining steps then stop (removed
when asked), particle sources move, count down their lifetime and are removed; 5. `ScriptFreeTemporaries`. The WorldScript
and every console `@` line are added by `ScriptRunWorldScript` 004BCC60 and first run on the next tick.

`Execute`: counts the call (+8); returns 0 if the block had already ended; otherwise drops deleted objects, then per command
binds variables (`SetParameters`), evaluates nested calls (`CalcParameters`), runs the command, frees temporaries, advances.
A waiting command leaves the index on itself and Execute returns 1: Sleep(n) records the tick count (+0xC) and waits while
`ticks - start < n` (FISTP; so n further Execute calls), then resets it to -1; WaitSegment(n) waits while the run has been
executed fewer than n times in total; SleepUntil(c) while c is 0 or NaN; SleepUntilIdle(unit) while the unit's brain has an
action (+0x240 object +0x984). For(counter, group)(body) / ForIf(counter, group, condition)(body) walk the group from last to
first (size read once), store the member in the counter (a global object variable) and run the body block in the same run
(block, index, ticks and sleep start saved, set to body / 0 / 0 / -1, Execute recursion, restored: a Sleep in the body ends
the body for that member); ForIf evaluates its condition for each member. A script call (command 8) adds an instance of
definition `float 0` at the HEAD of the instance list (004C7C90 is an AddHead), `ScriptBindCall` 004BB6A0 copies the
arguments into its variables (FatalError "Wrong number of script %s float parameters" etc.); an instance started by a
waiting run runs its pairs in the same tick, one started by another instance's commands from the next tick. Assignment
(0x78): float 1 selects floats (copied as bits), strings, objects (reference) or groups (CArray Copy 004C1290).
Unit fields the commands touch: +0x240 server state (+0x980 AI brain, +0x1E0 skills / stats, +0x1C owner object), +0x244
hostile mask, +0x250 side, +0x4A0 / +0x4A4 quest items; `flags_` 0x800000 blocked, 0x10000 hidden, 0x80000 trap active.
Players: +0x20 quest variable / music / zone object, +0xC0 money, +0x108 god mode byte.

## Savegame writer and reader

Reached by every save (the new game's autosave) and load (`world_load`); the world loads only its first script (007C27D4).
`ScriptSave` 004C0F90 order: point lights (004B1940 each, count first), particle / FX / lightning lists (004B1A80 / 004B1C50
/ 004B1DB0), +0x68, +0x6C, command blocks (+0x70) and WorldScript blocks (+0x178) via `ScriptDeclSave` 004C1230 (block id,
count, `SSCCmd::Save` each; 004B6360), definitions (+0x8C, 004BBFE0), globals (+0x0A0, four savers 004C2EF0 floats / 004C33C0
strings / 004C38C0 objects / 004C4330 groups), instances (+0x140, 004BBFE0), runs (+0x15C, 004BB320: finds the run's instance
by comparing its kill-flag pointer with each node's data in 007AA620's instance list, -1 when absent), +0x1A8, +0x1AC, then
007AA620 = 0 and the areas (004CAEC0, only read for stream versions >= 0x57). Lists are written as count then each node
(next read before the call); a variable's name is written with its terminator, or as one zero byte when the name array is
shorter than the value array. Effect elements: object id (-1 none), then fields; loading looks the id up in the effect
manager (007AFFA8, `CBaseManager::FindObject` 00403F40) and raises the found object's count inline.
`ScriptLoad` 004C0D00 mirrors it: list loaders read the count, append that many default nodes, then read every node from the
head (a non-empty list would have its old nodes read again; never the case: the world is new). Block ids resolve in the lists
of 007C27D4 (`FindBlock` 004BFA50: negative ids in the WorldScript blocks, others in the command blocks; unknown gives null).
Runs: block id, index, ticks, sleep start, the instance index (n-th node of the loading script's list, 0 out of range),
`ScriptVars::Load` 004B3AD0 (one stack variable holds the float count and index and then receives every object id and group
size; object ids go through `FindObject` and `Assign` 004B24D0, group members through `ScriptObjectRef::Load` then `Sort`).

## Quests, unit modes, text manager

- Quest construction: `QStart` 004BAA40 (name in ECX; empties the objectives at 007AA608 `{type, text, value}`, 12 bytes),
  `QObj*` 004BAE10 (objective appended), `QFinish` 004BAAB0 builds a script text and loads it: `DeclareScript Su0()` ..
  `SuN()`, per objective `Script Sui(if(` condition `)then(KillScript() Sui+1() GSSetVar(0,"q.q.q.i+1",2)` (and
  `GSSetVar(0,"q.q.q.i+2",1)` except for the last), the closing `SuN` (QuestComplete, GSSetVar "q.q.q" 2), `WorldScript( Su0()
  )`; then the quest name is emptied (the objectives stay; QStart frees them). The quest kill table (`QuestTable`, 007C87A8,
  hash_map by name at +0x0C) is looked up by QuestComplete through `QuestTable::Find` 004C9C90 (operator[]: a missing name
  gets a null entry).
- Unit mode constructors (`script_unit_modes.cpp`, vtables in `aimode::` / `unit_ai.h`): 004BA1C0 `GuardMode` (0073D128) then
  `Set(point, radius, wander)`, 004BA360 `SentryMode` (0073D190), 004BA450 `FollowMode` (base 0073D15C, then the leader
  referenced, 0073D1C4), 004BA610 `SuspectionMode` (0073D1F8), 004BA710 / 004BA830 `AggressionMode` (0073D260; the second
  with `fixed` = UMRevenge), 004BA920 `CorpseWatcherMode` (0073D294), 004BA1A0 `AILook` (UMPatrolLook). None sets the count
  (+4): the commands write 0 there after operator new.
- Their own slots (A-S10, same file; `verify_script_unit_mode_slots.cpp`): Clone (slot 3) 004BA250 / 004BA3B0 / 004BA4A0 /
  004BA630 / 004BA730 / 004BA930, Reset (slot 2, OnShutdown: references dropped, constructor values back, EAX untouched)
  004BA2D0 / 004BA3F0 / 004BA510 / 004BA6B0 / 004BA7C0 / 004BA950, deleting destructors 004BA300 / 004BA340 / 004BA420 /
  004BA590 / 004BA680 / 004BA6E0 / 004BA8A0 / 004BA990 and bodies (004BA5B0 / 004BA8C0 release the leader / enemy; the rest
  only store the root vtable 0073BB68), GetType 004BA410 / 004BA580 / 004BA6D0 / 004BA860 / 004BA980, Aggression
  OnObjectDeleted 004BA870. Kept: a clone stores its count before testing operator new's result; Aggression's clone copies
  `chosen` without a reference. Not hookable (3 bytes): 004BA220 / 004BA230 (RET 4), 004BA240 (MOV AL,1), 004BA2B0 / 004BA2C0
  / 004BA970 (RET 4), 004BA2F0 (Guard type 0), 004BA620 (MOV AL,0). The accessors 004B9F20-004BA080 and 004BA9C0 are called
  only from the rewritten runtime 004B6760 (not ported).
- Text manager line reader (`text_reader.cpp` on `CTextManager`; 54 callers of Open in the interface, the map loader's
  FindLine / Split, the item texts): the cursor (+0x20) walks the current text (+0x24, +0x28 bytes); every end test is the
  signed `(p - text) < length`; characters are MBCS. Open 004DEFC0 asks each archive (slot 5, GetFile(key, &length)) in list
  order (its wrapper runs GetFile twice: the archive's count rises twice); PeekWord 004DF770 (the cursor does not move);
  SkipLine 004DF6E0; NextLine 004DF510 (past the line and every line whose first word starts "##"); ReadLine 004DF8D0;
  ReadToHash 004DFB80; Split 004DFCB0 (a word still open when the text ends is dropped); FindLine 004DF030 (key null = the
  next line whose first word starts with "#" but not "##"; a one-word key = the next line whose first word equals it;
  several words (004DBE40 splits the key) = a line whose first words, lower-cased with MakeLower, equal the key's); CharCode
  004DF4E0; Close 004DF020. The original inlines the "move past the next LF" loop in five variants; `Advance` takes the
  variant. Not rewritten: 004DEC40 (Rebirth), the archive list helpers 004DEB60 / 004DED40 / 004DEDE0 / 004DEBC0, 004DED20.

## Rewritten (script engine)

Every rewritten function carries its address in its source comment (grep the address); the per-function tables that used to
be here are gone. Where things live: `script_eval.cpp` (SetParameters / CalcParameters / CalcFloat / CalcObject / CalcGroup;
CalcGroup is there for Rebirth's AddNewScriptCommands), `script_parse*.cpp` and `script_blocks.cpp` (tokens, lexer,
loaders, defines, qualified names), `script_arrays.cpp` (parser arrays), `script_exec*.cpp` (Execute 004B6760, bind calls,
ScriptTick 004BF000, FreeTemporaries), `script_load.cpp` / `script_save.cpp` / `script_vars_append.cpp` (savers, loaders,
appends, copies), `script_destroy.cpp` / `script_cleanup.cpp` / `script_reset.cpp` (constructors, destructors,
ScriptReset, PurgeDeletedObjects, ResetZone), `script_effects.cpp` (CreateFX, CreateLightning, particle sources, point
lights), `script_quest.cpp` (QStart, QObj, QFinish, `QuestTable::Find`), `script_unit_modes.cpp` (the unit mode commands),
the object references, groups and variables at 004B24B0-004B3940, areas 004B1ED0-004BCF30, the text reader below.

- Rebirth patches these, so they hook in the vanilla launch only unless the patch is behind a flag: 004B6760 Execute,
  004CFBC0 LoadFunction, 004D1330 LoadCommands, 004D0B10 FunctionSignature, 004D0BA0 KeywordName, 004DEC40.
- QFinish: its CScript::Load is skipped in the original through `scriptverify::g_loadSkip` when the two are compared.
- `Execute` is observed after each command by the harness checks (Sleep / WaitSegment / SleepUntil stop where they
  should, KillScript, calls add a live instance, AddObject keeps the group sorted, SetPlayer / SetEnemy / GodMode); the
  observer reads the instance list's head (+0x144), not the tail (see the gz2g note below).
- Not rewritten: 004DEC40 (Rebirth), the archive list helpers 004DEB60 / 004DED40 / 004DEDE0 / 004DEBC0, 004DED20.

## Original bugs and oddities kept (script engine)

- NaN: `IsEqual` and `Not` treat NaN as equal, `IsLess` as less; `IsInSquare`, `UnitInSquare`, `IsInArea` the same mixed
  way; `SetDiplomacy` maps NaN to neutral; flag commands treat NaN as zero.
- `CalcParameters` for a string-returning nested call evaluates its arguments, then FatalError "Function does not return
  string at SSCCmd::CalcFunction" (no such functions exist; Rebirth's patched CalcParameters calls `CalcString` instead).
  `IsInArea` with an unknown id creates an empty area; `GetObject` logs the id after converting the float again; `Every` with
  no nested condition copies the constant argument as the result.
- Parser: keyword prefixes split identifiers; `/**/` as one token does not close the comment; the parameter token after `(` /
  `,`, the name after `Script` / `DeclareScript`, the token after `if` / `then` and a declared variable's name are read
  without an end check (a script ending in `Foo(` dereferences a null position); a For / ForIf whose header fails still
  parses its body (usually "Wrong lexeme ,"); For / ForIf bodies are parsed without the script's locals (Rebirth's
  AllowScriptArgsInLoops fixes it); `LoadVariableDeclarations` returns 0 without an error when the tokens run out;
  `LoadScriptBody` leaves a failed pair's blocks in the list; "In script %s should be at least one condtion" (sic) prints the
  qualified name.
- Runtime: `SetSpellAggression` (0x42) does nothing; `KillScript` in the WorldScript reports FatalError and then writes
  through a null pointer; a run keeps a pointer to its instance's killed flag after the instance is removed; `SleepUntilIdle`
  has no null check; `ResetTarget` (0xE8) on a unit without a target can crash the original (005D6630); `InflictDamage` calls
  TakeHit (vtable +0x50) with three arguments (hit, 0, 0): a two-argument call unbalanced the stack and crashed the first run;
  the mode commands write the count (+4) before testing operator new; a For counter is a global, so nested loops on the same
  counter overwrite it; `CreatePointLight` stops at the first free slot (two lights can share an id; with 256 slots used and no
  match it writes one slot past the array); `DeleteArea` leaves the moved-out last entry in memory; the tick saves each node's
  successor before running it; `CreateLightning` appends without looking the id up (two bolts can share an id); its id test is
  `FCOMP 0; TEST AH,1` (negative or NaN: automatic) then `_ftol`; `CreateFX` keeps no reference (timed object only);
  `CreateParticleSource` id is FISTP-rounded; `QStart` sets size and capacity to 0 even without an array; `AddParameter` with a
  type outside 1..4 returns the name argument's address and appends an empty letter.
- Savegame: block ids resolve in the first world script's lists whatever script loads; the first block id of an if-then pair
  reads into a variable preset to -1, the second into the same variable without a preset; list loaders re-read nodes that were
  in the list; `ScriptVars::Load` reuses one stack variable for the float count and every object id; `AppendFrom` copies over
  the other set's names while the values grow by its values; the definition and run savers do not null-check blocks; a missing
  name is one zero byte.
- The exception frames of the rewritten functions are not reproduced (a failed operator new in `SubCallCopyAt` leaves the
  copy's command null, as the original does).

Skipped: 004BD310 `CreateFXSource` / `CreateRandomizedFXSource` (for a new id the original leaves spread, min / max delay and
countdown (+0x24..+0x30) as stack garbage the tick then uses; zero would replace the sound every tick); 004BDE40 `CScript`
constructor (runs in the world's static initialiser 00589BE0 before the hooks; standalone build: lights array 0x73D394 resized
to 256 zeroed slots, list vtables 0x73D388 / 0x73D37C / 0x73D370 / 0x73D364 (blocks and WorldScript blocks) / 0x73D34C /
0x73D340, block size 10, definitions 0x73D358, areas 0x73D334, globals 004BC690 / 004BC740 / 004BC870 / 004BC950, one temporary
group and one null reference added (004C8CE0 / 004C9210), error text empty, then the reset); 004BFC60 (find a definition by
name; only the dead dumper calls it); the small accessors 004B9F20-004BAA30 (inlined in our commands); 004B2520 (16 bytes with
a branch in the first 5: no trampoline). The multiplayer branches of GSSetVar / GSSetVarMax / GSDelVar / QuestComplete are
rewritten but only reached in multiplayer. Dead (config/dead_code.csv): the quest script builders 004C01A0 / 004C0520 /
004C06F0 / 004C09F0, the script dumper 004D53C0 -> 004D39D0 / 004D50C0 -> 004D41C0 (Rebirth patches three operands in it) /
004D4E80 / 004D5310, 004D38B0 / 004D38F0 / 004D5EC0 / 004D65B0; 004B3A70 has no callers. Library: 237 functions, the `CArray`
/ `CList` instantiations of the script types between 004C1290 and 004CEE80 (vtables 0073D110-0073D4C0) and the quest table's
hash_map members (004CE110, 004CE460, 004CEF00, 004CF0F0, 004CF160). Called only by rewritten originals (dead in the
standalone build): 004DEBC0, 004C17F0, 004D0AF0, 004D3650 / 004D3670 / 004D6310, 004C17E0. 004D5880 (LoadFile's CStdioFile TRY / CATCH) is ported (A-E2: our
code does catch the game's C++ exceptions, app.md "MFC TRY / CATCH").

gz2g's scripts (zone2.mob chunk 0xACCEECCB, decoded as 004D8E10 does: the first dword seeds the MSVC rand LCG, `x = x *
0x343FD + 0x269EC3`, each byte XORed with `x >> 16`) start every magic trap armed and disarm them from triggers
(`ActivateTrap(MTF1..4, 0)`); `zone_gz2g_traps` reaches command 8 (`KillScript() VTriger#0#n( this )`) from a killed instance,
which is why the Execute observer reads the instance list's head (+0x144), not the tail.

## Leftovers: the rest of 0x00480000-0x00500000 outside the script engine (rewritten)

Every function carries its address in its source comment. The files, with the scenarios that reach them (all of it also runs
in `newgame` unless noted): `core_memory_stream.cpp` (CMemoryStream, CStream::CopyFrom / ReadSized / ReadHeader; every save
and network message), `core_stream_diff.cpp` / `core_stream_chunks.cpp` (WriteDiff, ApplyDiff, ReadTagged, FindTagged,
ReadText, ReplaceTagged; world_load, ui_saveload), `game_buffer.cpp` / `game_buffer_strings.cpp` / `net_coder.h` (growing
byte buffer; save compression, multiplayer, net_lobby), `buffered_file.cpp` (005809D0 reads "quest.reg"),
`matrix_math.cpp` (MatrixMultiply, Quaternion::ToMatrix, PushMatrix; every frame), `view_matrices.cpp` (CameraViewMatrix
004F0EF0, LightRotation 004F15C0, LightShearMatrix 004F1440 called by address from map_object_shadow.cpp, 004E5660 a byte
copy of CubicSegment::Evaluate 0040E440, AllocateBlocks32 004E5710 / 8 004F65B0), `text_split.cpp` (Split, SplitWords,
ReplaceChar, Join; Join has no scenario: loose .bmp only), `text_sources.cpp` (archive list and CTextManager sources;
briefings add and remove zone texts), `exit_area.cpp`, `ground_decal.cpp` (every frame with shadows), `mob_reader.cpp`,
`class_template_stock.cpp` (trader_stock, every new game), `script_runtime_helpers.cpp` / `party_script.cpp`,
`global_map_clouds.cpp` (CCloudNoise; ui_camp, frames_basic), `face_normal.cpp` / `model_names.cpp`, `particle_attach.cpp`
(particles, spells_cast), `link_figure.cpp` / `link_loader.cpp` (newgame via 00568800, models, ui_hero_screen),
`sky_object.cpp` (render_scene), `server_specific_manager.cpp`, `model.cpp` (ProjectBlocks 004F2650, ProjectTerrain
004F29F0: `ITransformPipeline` slots 9 / 14 / 18 / 19), `party_manager.cpp`, `effects_weather.cpp`, `dx_errors.cpp`.

- Stream diff: `ApplyDiff` patches run headers in place and follows a full run of 255 by an empty run of the other kind.
  `ReplaceTagged` 004A4AF0 has no caller in game.exe (Rebirth's EI_Plugin.asi calls it through a thiscall thunk).
- Zone exit `Read` / `Write` 004E5750 / 004E58A0: +0x00..+0x20 dwords, target with terminator, +0x2C, +0x30, then bytes
  +0x34, +0x38 and the angle; read from version 0x68 on, else 0 (world.h's +0x24 is `angle`). `ReadInto` 004E5840 reads a
  string into a temporary and assigns. Covered by zone_base_bz1g, newgame, saves.
- Ground decals: ClipPlane / AddTriangle / AddTerrainTriangle are in tests/verify/disabled.txt (X17).
- .mob: `ReadMobChunks` 00481F00 (scripts, weather, records, players); `LoadMobFile` 00481660 (relations applied, traps get
  their hostile mask and origin, units their alarm records 005D6D20, records deleted, scripts loaded unless keepIds);
  `MobScriptDecoder::Decode` 004D8E10 (resize 004D8EB0 = `CharVectorResizeOriginal`); zone_gz2g_traps.
- Trader stock: `ClassTemplateSetLevels` 004A6DC0, `GenerateItems` 004A6DF0, `GenerateMagic` 004A6F60 (a list<CItem*>
  sentinel from the node pool, generators, AddItem(1, 1) + Release), `ClassTemplate::Construct` 004A6580 and
  `ItemStock::Construct` 004A66C0 (193-bucket maps).
- Script runtime helpers: `PartyForScript` 004BAEC0 ("owner::name"), `CAIMap::SetRefreshUnits` 004BA090, the sector map
  wrappers 004BA0A0..004BA100 (Move by address: Rebirth), `GameOStream::PutFloatArgument` 004BA9D0,
  `CSceneRenderer::SetFogColour` 004BA120 (slot 0 00501AB0, `fogColour_` +0x14).
- Party manager archive (A-S9): `PartyManager::Save` / `Load` 004A8560 / 004A8310 (the manager 007AA5A0: parties
  hash_map<int, CParty> +0x00, entries +0x14, field +0x20, records +0x24, templates +0x38); `ChunkArchive::SerializeParties`
  / `SerializePartyEntries` / `SerializeClassTemplates` / `IntField` 004ACEC0 / 004AD0C0 / 004ACDC0 / 004AD2C0,
  `NamedPartyRecords::Serialize` 004A64B0, `PartyRecord::Serialize` 004A5AF0, `ClassTemplate::Serialize` 004A6C40
  (SaveLists 0069BAC0 by address). `BeginRedeploy` / `EndRedeploy` 004A6050 / 004A6360: Begin spreads the variables
  (004A8AD0 operator=), zone and +0x6C to every record; End clears the party when no record matches, then text10 = text0C.
  Scenarios: every save (autosave), world_load, ui_saveload, zone_* (RedeployParty).
- Rain / snow (`CRainSnow`, CWorldClient +0x6F4): `Clear` 004E3B80 (slot 3 via the thunk 005617B0: rain loop stopped,
  texture +0x1C released, wind / mode / fade / world +0x20 cleared, density 64), `Start(mode, duration)` 004E3FC0 (texture
  "RainSnow" kind 6 through 00500A00; rain plays the loop "nature\rain\circle.wav" (Play2D, priority 1000); fade start = the
  world's tick). Not ported: the draws 004E4240 / 004E4C80 and buffers 004E3C20 (renderer data); 004E3980 (noise tables,
  only from the static initialiser 004E3B20; ported by A-E2).
- `CVisibilityFog` (vtable 0073D6B4): slots 2 / 3 004E23D0 / 004E2440 (cells freed to the pool, bounds invalid; slot 2
  returns 1), 11 / 12 004E24A0 / 004E24B0 ("CVisibilityFog", 0x5F), destructor 004E24E0, slot 0 004E24C0. The fog of the
  show-visibility effect; no scenario known.
- Misc: `DirectXErrorText` 00480B80 (a sorted table of 149 codes), `app::FatalErrorCode` 004800E0 ("%s (0x%.8X) %s"; always
  exits), the thrown `GameError` (0xC: vtable 0073CAB4 / base 0073CAB0, operator new message, result): constructors
  00480250 (wvsprintf) / 00480300 (+ result text), destructor 004815D0, deleting destructors 004815B0 / 00481580.
  Only on a DirectX failure.
- Models: `ComputeFaceNormal` 0049AEC0 (edges from vertex 1 normalised unless below 1e-10 squared, crossed, normalised; zero
  or NaN length takes 007A9C98).
- Link figures: `CLinkMesh::AddPart` returns a bool in AL; `FindFigureSet` 004FAB00; the loader slots 3-6 004F7150 /
  004F7080 / 004F7020 / 004F75E0; `CacheBytes` 004FAAA0 has no scenario.
- Sky: `CSkyObject::Light` 004D9360, `CSky::Light` 004D9DE0, `CSky::Draw` 004D96E0 (renderer slots 11 `SetTexture` /
  14 `DrawFigure`; the object at the eye lowered by 16).
- Server-specific manager 004D6900.. : slot 14 `NewObject` builds 0x46-0x49 through their constructors and 0x4B
  `CTorchObjectSpec` inline + Create through the vtable.

Facts:
- Memory stream: Write grows to (capacity + size) * 2 copying the old capacity's bytes; Resize grows to size * 1.5 + 0x10
  copying the old size's bytes and flushes the pending bits byte only when it grows; SetSize resets only bitMask_
  (bitsIndex_ stays); a short Read zero-fills the whole request before copying what is left. Growing buffer: every growth
  makes a block of 2 * (needed end, counted from `base`) + 0x40 bytes and copies the old capacity's bytes; Reserve and
  WriteMore grow without testing the room (callers test); Seek returns the new capacity in EAX when it grew. ReadString
  inlines the string assign (overwrite and cut, or overwrite and append through 0040C2A0).
- Buffered file streams: a `GameBuffer` whose block is a window onto a file (`base` = file position of `begin`); abstract
  vtable 0073F8C8 (constructor / destructor 00580DD0 / 00580DF0, slots 7 / 8 pure), file vtable 0073F8A4 (+0x1C `FILE*`,
  destructor 00580E60); the game's stdio is in `lib/crt.h` (fopen 006E9107, fclose 006E8EB8, fseek 006E95D2, ftell 006E97C4,
  fread 006E8F35, fwrite 006E968B). Kept: `ReserveRead` growing reloads at `base` with min(capacity, high - cur) bytes; the
  reload count can be negative after a large read (passed to fread as is); Seek's out-of-window path returns no meaningful
  EAX (ours: the old cursor). Not ported: 004DABC0 and 004DAB40 (no callers).
- Matrices: each product element sums its four terms in the original's order (two orders: `kProductOrder` / `kPushOrder`),
  pinned with StoreFloat against /fp:fast regrouping; elements are written in memory order, so an output that is also an
  input behaves as in the original.
- MFC naming traps: Mid / Right in core.md ("MFC CString: Mid and Right"); 006FE216 is TrimLeft() and 006FE11F TrimRight()
  (ini_file.cpp's TextOps names them the other way round; the calls are right).
- Text archive list: Add keeps a failed archive in the list (unlike `CLinkFigureLoader::AddArchive`) and returns
  OpenResource's result; nodes go back to the 0x18-byte list through `stlp::deallocate`. Not ported: 004DEBC0 (`Find`),
  004DEC40 (Rebirth) and its forwarder 004DEFA0, 004DED20 (setlocale / _setmbcp, slot 2).
- Zone exit `Contains`: a zero or NaN angle tests the rectangle as is (x1 > x, x > x2, y1 > y, y > y2 reject; NaN passes);
  otherwise the point is turned by -angle about the centre (centre x and the x offset kept unrounded) and a NaN turned x is
  outside.
- Ground decals: 0056C750 projects a shadow quad (DrawShadow 0051CE60, 006CEDE0), 006CCF20 adds the terrain triangles under
  it. The original's clip stages are five hand-unrolled functions with a function-static triangle buffer per stage (007AA710
  plane, 007AA750 u >= 0, 007AA790 v >= 0, 007AA7D0 u <= 1, 007AA810 v <= 1); `ClipStage` is one recursive stage function with
  a buffer per stage (same in-place clipping and emission order). A corner is inside when its distance's sign bit is clear (-0
  outside, NaN with a clear sign inside); 1 - u is FLD 1.0; FSUB (u = 1 gives +0, inside); 004D6E90's inside test is NOT; SHR
  31. Vertex arrays are 4-vertex SoA packets (x[4], y[4], z[4], w untouched); `Reserve` grows the packets by triangles * 3 / 4 +
  1 + the vertex count (over-reserves, kept); `Finish` pads the last packet with copies of vertex 0; the statics' guard bits and
  empty atexit handlers are not reproduced. /fp:fast sums the projections as (z + y) + (x + offset) where the original does
  ((z + y) + x) + offset: the wrapper compares within 1e-3 + 1e-5 relative (`NearClipped`). In the verify build the original
  004D8490 calls the hooked 004D7BD0, so a corner within rounding of a clip line changes the triangles that follow (why X17
  disabled the three).
- Link figures: `CLinkMesh` (vtable 0073D790, 0xB0, slots 0-6 declared): part headers stlp::map<string, LinkPartHeader 0x50>
  +0x14, morph coefficients +0x30, six aligned arrays +0x3C (vertex packets, normal packets, texture coordinates, 6-byte
  corners (`lib::AlignedArray<IndexTriangle>`), dwords, words), ranges map<string, 0x30> +0x9C, loader +0xA8, pipeline +0xAC
  (`ITransformPipeline`, 3DfpFPU.dll's; slot 7 FitBounds). The loader (model.h, 7 slots): `figureSets_` +0x10, `renderer_`
  +0x24 (the device object, whose +0xAC is the pipeline: `CTextureDevice::pipeline_`), the cache `stlp::map<string,
  CLinkMesh*>` +0x28. Morphs are plain floats: FIG8 lerp(lerp(lerp(c0,c1,s), lerp(c2,c3,s), t), lerp(lerp(c4,c5,s),
  lerp(c6,c7,s), t), u), old records lerp(lerp(lerp(c0,c1,s), c2, t), lerp(lerp(c3,c4,s), c5, t), u) (`MorphFig8Packet`,
  `MorphOldPacket`). Forwarders: 004F9E50, 004FBD80 (the cache's insert_equal; 004F7B10 inlines the descent and calls
  _M_insert 004FC4F0). The rewritten loaders are called through their addresses so their checks stay reached. `AddPart`
  returns bool in AL (EAX's upper bytes are 004F4CE0's value pointer). Kept:
  `RemovePart` lowers the later parts' starts by the removed part's starts, not its counts (only right for the first part),
  and picks the later parts by their index start alone; RemorphFigPart reads every record as FIG8 (a plain FIG part is
  skipped 0x168 bytes and morphed from 8 versions); an empty part name in a .lnk keeps the previous one; a missing link file
  leaks the new mesh (ours returns null instead of FatalError's EAX). Not kept: the header map's value destructor 004F4E70
  zeroes the freed node's 0x50 bytes. Not ported: slots 7-14 (004F3870 ... 004F4850, D3D part draws: renderer), 004F6A20
  (4 bytes).
- Sky: haze is recomputed only when the vertex count changes (0 for vertex z < 1, else sqrt(-normal z)); diffuse = ambient *
  255 (alpha 0xFF; 0 for hazeless vertices, which get no specular); specular = haze * 255 alpha and light * max(0, n .
  dir)^2 * 150, all truncated. The fourth float of each colour and the third colour are not read. 004D9E60 / 004D9E70 /
  004D9EB0 are only called from EH unwind funclets.
- Attachment: attachPoint 1-7 = "hd", "hd" + 1 in z, "lh2", "rh2", "ll2", "rl2", "bd"; 8-12 = "body00".."body04"; every part
  falls back to "bd", then to the position raised by the bounds' upper z extent. Not a unit or attachPoint > 12: position
  raised by radius * 1.6 (bounds bit 0) or the box height. Attach recurses through Add/RemoveEffectSource; the nested
  Attach(object) runs before the outer call sets the origin, so the wrapper checks the origin only for calls that change the
  attachment.
- .mob: the wind strength is clamped by the float at 0079BE58, which nothing writes (0), so ChangeWind gets at most 0; the
  weather chunk's last two floats are read and unused. 00481660's branch under 0079BCF4 (colour quantisation 0049BEA0 and its
  STLport helpers) never runs: nothing writes 0079BCF4 (bss); the 78 functions only that branch reaches are in
  config/dead_code.csv. Kept: 00481660 deletes every record while the hash_map still holds the pointers. ReadLinkString's static
  buffer 007AA8D0 is only its own, so ours copies straight from the record.
- Clouds: SetRamp's alpha is trunc(255 - pow(width / 255, max(0, i - start)) * 255) >> 4 with the CRT pow (a last-bit pow
  difference matters only on an exact multiple of 16); Draw's blend is trunc((step + phase) / 16 * 254 + 1); the original's
  third SetRamp argument is the fade speed.
- `CTransformPipeline` (model.h) has `transform_` +0x0C (3DfpFPU's object); 0059F030 (CFigure's destructor body) is
  `FigureDestructBodyOriginal` (originals_core.h); the figure model forwarders 005A9A50 / 005A9C90 / 005A9EB0 are shared in
  originals_world.h; 004A91B0 is `PartyMapAtOriginal` (originals_gameplay.h); 006617F0 `CParty::ArchiveParty`. Counted
  pointers are released through `CRefCounted` (`delete` = slot 0 with 1); the party map's party is `PartyInPlace` (a `PartyStorage`, party_storage.h).
  The archive on the stack: `ChunkArchive::ConstructLocal` / `DestructLocal` (chunk_archive.cpp).

Classification of 0x004A0000-0x00500000: library.csv holds the STLport / MFC members (mesh maps, the link loader's cache
004F4CD0-004F6850 / 004FAE40-004FC6A0, the party manager's containers 004A90F0-004AFAC0 including the weapon / armour lookups
004AB720 / 004AB820, EH-only helpers); dead_code.csv the dumper's writer 004D41C0 and the 3DS loader's helpers
004A1E60-004A47D0. Not game logic here: the device layer 004E5E50-004F0B00 and state blocks 004FCA60-004FDB50 (renderer
work), `CSysInfo` 004DDCB0-004DE8B0 (static initialiser table 00783310; ported by A-E2).

### Next candidates in this range

- Renderer tickets: the D3D state-block manager 004FCA60-004FDFE0, rain / snow draws 004E4240 / 004E4C80 and buffers
  004E3C20, the fog draw 004E1EB0-004E2290, the CLinkMesh draw slots 7-14 004F3870-004F4850, the device functions
  004E5E50-004EFEA0, the TL library loader and matrices 004F0B90-004F15C0 (004F1330 Rebirth), the debug line lists 0049B140 /
  0049B780.
- Classified: 0049F820 / 0049F840 / 0049F870 / 004E3B20 / 004E3AD0 are static initialisers (before the hooks); 0049F890
  (keyframe track) is reached only from 00448720, which only calls itself (dead). Only reachable from rewritten callers
  (left unported): 004824B0 / 00483D40 (LoadMobFile's record map), the accessors 004B9F20-004BA080 / 004BA9C0 (from
  004B6760). Left: 004BD310 / 004BDE40 (skipped above, ported in A-E1 for the standalone build).
- Standalone build: `CSysInfo`, the party manager's static constructor 004A5870 and exit destructor 004A5700, CTorchObjectSpec
  Create / Destroy 0052C610 / 0052C680 (not ported; slots 13-15 004D6B20/30/40 are a bare RET).

### A-E1: the script leftovers below 0x004C0000

- 004BD310 `ScriptCreateFxSource` (`script_effects.cpp`; CreateFXSource 0x16 and CreateRandomizedFXSource 0x18). The id is
  FISTP-rounded, a negative one takes `nextFxId_` (then counted down). An existing source's sound is dropped; a new source
  is a `NewNode` at the tail. 0x16 makes the sound at once (class 0x48 from the effect manager, added to the timed objects,
  one more reference) and clears `idle`; 0x18 keeps no sound, takes min delay, max delay (FISTP) and spread, countdown 0,
  idle 1. Decision: for a new id 0x16 leaves spread, the delays and the countdown as whatever the stack held (the temporary
  element at EBP-0x50 is copied whole into the node, 004CB220); the tick (004BF000) counts such a source down and replaces
  its sound when the countdown reaches zero. Ours writes 0, 0, 0 and INT_MAX: the sound stays, as with any positive
  residue (zero would replace it on every tick). Scenario script_fx.
- 004BDE40 `CScript::Construct` (`script_construct.cpp`): the member vtables (`scriptvtable::kPointLights` ..
  `kTempObjectList` in script.h), lists in blocks of 10, the four global variable sets, 256 zeroed light slots, one empty
  temporary group (004C8CE0) and one null temporary reference (004C9210), an empty error text, then `ScriptReset`. Only the
  world's constructor 00589ED0 calls it (twice), from the static initialiser 00589BE0, before the hooks: the hook never
  fires in the hooked game. The rewrite is for the standalone build.
- 00456360 `CScript::GetError` (text copy-constructed at `out`, code, line; `ScriptErrorInfo` of world_glue.h): reached
  only when the command line's script fails to load.
- 004B3200 `ScriptGroup::CopyToList` (script_group.cpp): the group's objects replace a pointer list of counted references
  (each old element released, the list emptied, one counted element per object). The only caller is the server's party
  sync of a player without a party copy (player_sync.cpp, through `ScriptGroupToListOriginal`).
- app_lifetime.cpp: the creators the application's start registers with the class registry 0079B270 (00457850 a class
  template as class 1, 00457740 a party record as class 2; both clear the count's dword before the null check, as the
  original), the party record's destructor (004577E0, slot 0 004577C0) and the party manager's exit-time destructor (004A5700:
  templates and records released, the strings, the entries, every party through the map's own node deleter 004ABA70, the
  bucket array). The creators are reached when a saved party manager is loaded.
- small_accessors.cpp: the accessors the interpreter calls out of line, as `__fastcall` shims: 004B2520 (the referenced map
  object's companion, null without an object), 004B9F50 / 004B9F60 (bits of the object's flags set / cleared), 004B9F90
  (the map object's position), 004B9FD0 (a counted object exists and is not detached; asked of the game server), 004BA010
  (FISTP of a number), 004BA030 (`CItem::Repair`), 004BA060 (the unit's controller), 004BA070 / 004BA080 (the carried
  items' count and element), 004BA9C0 (the controller's statistics), 004BAA20 (`ObjectListIterator::First`), 004BC1F0 (a
  CString written to a stream with its terminator; the definition's Save). Their only callers are Execute (004B6760) and
  ScriptDefinitionSave (004BBFE0), both rewritten with the same work inline, so no scenario reaches the hooks.

### A-S11: the plain game code left in 0x004C0000-0x00500000

The classification of the 173 functions that were left in the range is in renderer.md (section 12); the plain game code among
them is below.

- Text manager (`text_sources.cpp`, `item_db_text.h`). 004DEBC0 `TextSourcesFind`: the first source that has the key, its
  `GetFile` result (the size through the out pointer); null without one. 004DEC40 `TextSourcesListKeys`: every source's keys
  matching the pattern appended to the caller's array through a temporary `CStringArray` (`mfc::CStringArray::Append`,
  006FEB01, a forwarder in mfc.h); Rebirth-patched (ResOverride), so it hooks in the vanilla launch only and
  `CTextManager::ListKeys` (004DEFA0) calls the original through a forwarder. 004DED20 is `CTextManager`'s Create slot:
  `setlocale(LC_CTYPE, <the pointer at 0079BE94>)`, `_setmbcp(_MB_CP_LOCALE)`, returns 1 (runs on every start). The
  interface's "pers *" listing (00607720, 00635250) has no known scenario.
- 004D0AF0 `ScriptSubCallClear` (`script_blocks.cpp`): type and argument index zero, the command cleared. Only the original
  `LoadFunction` calls it (a Rebirth launch's script loads).
- `CSysInfo` (`sys_info.cpp`; the global at 007AB8F8, vtable 0073D644, class id 0xD, five CStrings at +0x1C). Rewritten: what
  can run after the hooks install: Create's shell 004DDF05 (it calls the collector 004DDF28 and returns 1), the empty
  Destroy 004DDF1D, the class name / id slots 004DE890 / 004DE8A0, the destructor 004DDE75, its deleting form 004DE8B0 and the
  exit handler 004DDD60 (runs on every exit). A static initialiser runs the rest before the hooks install (ported by A-E2
  and compared at WinMain's entry with what the original built, `verify_static_initialisers.cpp`): 004DDCB0, 004DDCBA (it
  stores 2 * pi at 007AB8F0), 004DDCCD, 004DDCD7, 004DDCEE, 004DDCF8, 004DDD0F, 004DDD19, 004DDD30 (constructs the global and
  registers 004DDD60), 004DDD3F, 004DDD4E, 004DE855, 004DE87C, the constructor 004DDD6F, the collector 004DDF28 (1887 bytes:
  registry and version queries into the five texts) and its helpers 004DE687, 004DE6C2, 004DE6F3, 004DE789.
- 004E3980 (the weather's noise tables filled from `rand`; `weather_noise.cpp`): only a static initialiser calls it; the
  tables are rebuilt from `rand` seeded with 1 at WinMain's entry and match bit for bit (A-E2).
- 004D5880 (from 004D36F0: a text file read through `CStdioFile` inside an MFC TRY / CATCH(CFileException);
  `script_text_file.cpp`, A-E2): the text is emptied, the file opened by the throwing constructor (text mode, share deny
  write), lines of up to 0x7FF characters appended, the file closed; a caught exception gives 0 (app.md "MFC TRY / CATCH").
  No scenario loads a script text file.
- The visibility fog's manager (`vis_fog_manager.cpp`; `ClientArrayManager` in world.h is `CVisFogManager`, vtable 0073F554:
  the "VisionFog" texture at +0xB0 and a CList of counted fogs at +0xB4). 004E2290 (slot 14) makes a `CVisibilityFog`: engine
  object fields, +0x1C..+0x53 zero, a cell map of 193 buckets (resize(100)); the map's three hash functor bytes at +0x54 are
  left as allocated. 004E2990 (Create): the texture (kind 6), then the tile table at 007ABA58 the fog's draw reads, 16 records
  {u0, v1, u1, v0} with u0 = column / 4, v0 = 1 - (row + 1) / 4 and a quarter added for the other two, then the base
  manager's Create. 004E2850 (Destroy): the texture dropped (the pointer cleared before the release), every fog released,
  the list emptied. 004E2760 (the tick): each fog's tick; one returning 0 leaves the list and is released; returns 0. 004E28D0:
  a fog from `CreateObject(0, -1)`, counted once more, appended. 004E2700 (the world draw 00569E40): nothing listed returns 1;
  otherwise the texture is loaded if needed and bound to stage 0 (0 when that fails) and every fog draws through 004E1EB0 (not
  rewritten). Create / Destroy run on every start and exit, the tick and the draw on every frame of a map, the factory and
  the new fog only with the "show visibility" effect (0067C660), for which no scenario is known. The world files keep their
  forwarders to 004E2850, 004E28D0 and 004E2700.