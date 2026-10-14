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

Verify idioms: *pure*; *double evaluation* (ISAAC state restored between ours into a local and the original into the real
result); *capture streams* (bytes and write sizes compared); *record / replay* (the original reads the real stream into a
scratch object through a recorder, ours replays it into the real object); *deep-copy destructor check* (the original
destroys a deep copy, ours the real object; changed dwords must agree); *ours only*.

| Subsystem | Addresses and names | Verify |
|---|---|---|
| Object refs, groups, vars | 004B24B0 (and 004B5AE0) Reset, 004B24D0 Assign, 004B2500 Construct (returns the object in EAX), 004B2530 AsUnit, 004B2550 GetUnitPerception, 004B2570 ReleaseIfDeleted, 004B2590 / 004B25F0 Load / Save; 004B2620 / 004B2920 / 004B2B60 Union / Difference / Intersection, 004B2D60 Sort, 004B2DA0 IndexOf, 004B2E60 Remove, 004B2EF0 Insert, 004B3110 RemoveDeleted, 004B31D0 InsertAll, 004B3330 Contains, 004B3360 Load; 004B35E0 ResetValues, 004B36D0 HasVariable, 004B37D0 FindVariable, 004B3940 RemoveDeletedObjects | pure; group ops on a clone; record / replay, capture streams |
| Evaluator (`script_eval.cpp`) | 004B3F60 SetParameters (idempotent), 004B4030 CalcParameters, 004B41E0 CalcFloat, 004B56C0 CalcObject, 004B5B00 CalcGroup (for Rebirth's AddNewScriptCommands) | double evaluation |
| Parser | 004CF470 Error, 004CF600 ClassifyToken, 004D0B10 FunctionSignature, 004D0BA0 KeywordName (Rebirth patches these: vanilla only unless flagged), 004CFB70 / 004D0BD0 / 004D1D30 token construct / assign / copy, 004CF380 ConstructTokens, 004CF7A0 Tokenize, 004CF4D0 ExpectLexeme, 004D37F0 QualifyName, 004D3780 FindScript, 004D34F0 AddDefine, 004D33E0 ApplyDefines, 004D0A50 SSCCmd::Construct, 004CFBC0 LoadFunction (Rebirth), 004D0BF0 LoadConditions, 004D1D50 LoadAssignOperator, 004D1FE0 LoadVariableDeclarations, 004D1030 LoadScriptBody, 004D1330 LoadCommands (Rebirth), 004D23A0 Load, 004D36F0 LoadFile | pure / original on a copy with a deep tree comparison; Load, LoadFile, LoadScriptBody, LoadCommands ours only |
| Parser arrays | 004D5B50 BindingsAdd, 004D5CB0 SubCallsAdd, 004D5ED0 CmdsAdd, 004D65E0 CmdsSetAtGrow, 004D6100 DefinitionsAdd, 004C5480 SubCallDestruct, 004D5A50 / 5A90 / 5AD0 / 5B10 FindVariableName, 004D5A10 / 004D5A30 array constructors | original on a scratch array |
| Runtime | 004B6760 Execute (Rebirth), 004B6360 / 004B6330, 004BB6A0 ScriptBindCall, 004BB520, 004BC420, 004BCC60, 004BEC80 FreeTemporaries, 004BF000 ScriptTick | ours only; Execute through an observer after each command (Sleep / WaitSegment / SleepUntil stop exactly when they should; KillScript; calls add a live instance; assignments; AddObject keeps the group sorted; SetPlayer / SetEnemy / GodMode) |
| Areas, point lights | 004B1ED0 / 004B2070 AreaAddRect / AddRound, 004BCF30 DeleteArea, 004BD6A0 / 004BD820 / 004BD920 CreatePointLight / MovePointLight / DeletePointLight | output on a copy; ours only |
| Quests | 004BAA40 QStart, 004BAE10 QObj, 004BAEB0 objective destructor, 004BAAB0 QFinish (the original runs first with its CScript::Load skipped via `scriptverify::g_loadSkip`; texts equal), 004C9C90 `QuestTable::Find` (`script_quest`'s QuestComplete line) | ours only / against the original |
| Unit modes | 004BA1C0 .. 004BA920, 004BA1A0 | above |
| Save / load | savers 004C0F90, 004C1230, 004C2EF0, 004C33C0, 004C38C0, 004C4330, 004CAEC0, 004BBFE0, 004BB320, 004B1940, 004B1A80, 004B1C50, 004B1DB0; 004C0D00 ScriptLoad, 004CAC00 Runs, 004CA980 Instances, 004CA000 IfThen, 004C40D0 Groups, 004C36B0 Objects, 004C57C0 DeclCommands, 004C3200 Strings, 004C2D40 Floats, 004CA480 Particles, 004CA5D0 FxSources, 004CA720 Lightning, 004CA810 WorldBlocks, 004B5FE0 SSCCmd::Load, 004B3AD0 ScriptVars::Load, 004BB590 IfThenPairLoad, 004B3D10 StringLoad, 004B6330 SubCallLoad, 004B18B0 / 004B1B40 PointLight / FxSource load, 004BFA50 FindBlock | capture streams; record / replay (nested loaders run through their wrappers in both runs, so SSCCmd trees are read 2^depth times in the verify build); ScriptLoad ours only; FindBlock pure |
| Appends, clears, copies | 004B3900 AppendFrom, 004BB800 AddParameter, 004C3DC0 / 004C3450 / 004C2FE0 / 004C2B70 per-type Append, 004C0020 VarsClear, 004C1760 / 004C1700 / 004C16B0 / 004C1650 per-type clears; 004C6BC0 SSCCmd::operator=, 004CB570 run, 004CBAE0 vars, 004CB220 FX source, 004CB2D0 decl assign; 004CB7E0 VarsCopyConstruct, 004CCAA0 SubCallCopyAt, 004CE750 CmdCopyAt, 004CBEF0 / 004CBDB0 / 004CBA00 / 004CBB80 / 004CBCA0 copy constructors, 004CB460 DefinitionAssign, 004C2F80 StringsCopyTo, 004CDCB0 LooksSetAtGrow, 004CE5F0 NamesSetAtGrow, 004C00D0 VarsConstruct, 004D3320 DefinitionReset | ours only, the source captured first; constructors: original into a 0xCC scratch |
| Constructors, destructors, blocks | 004BE090 VarsDestruct, 004BE9D0 DefinitionDestruct, 004BF640 CmdDestruct, 004BC690 / 4BC740 / 4BC870 / 4BC950 named-value constructors, 004BC6C0 / 4BC770 / 4BC8A0 / 4BC810 / 4BC9A0 / 4BCA20 array destructors, 004BCAE0 RunDestruct, 004CE1E0 / 4CE210 / 4CE230 / 4CE270 / 4CE2B0 / 4CE2D0 / 4BF620 element destructors, 004C81B0, 004CD690, 004B17F0 / 4B1820 / 4B1850 / 4B1880 / 004BD540 effect element destructors; 004BE6E0 ScriptReset, 004BED90 PurgeDeletedObjects, 004BE490 DropInstancesAndRuns, 004BEB00 ResetZone; 004BF540 / 004BF900 / 004BCE10 / 004B59F0 / 004B5F70 NewCommandList / NewWorldScript / FindArea / NewTempObject / Clear | deep-copy destructor check; Reset / Purge / ResetZone ours only (one run per WorldScript block at its start, lights zeroed) |
| Effects | 004BD260 CreateFX, 004BCFF0 CreateLightning, 004BD9A0 CreateParticleSource, 004BDBB0 MoveParticleSource (CheckNear) | ours only |
| Text reader | 004DF030, 004DFCB0, 004DF8D0, 004DF510, 004DF770, 004DFB80, 004DF6E0, 004DEFC0, 004DF4E0 (pure), 004DF020 | original first, state restored, then ours |

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

V: S against the original (it runs first on scratch / copies, ours on the real object), O ours only.

| Area (file) | Addresses and names | V; scenario |
|---|---|---|
| Memory stream (`core_memory_stream.cpp`) | 004A5380 / 004A53F0 / 004A5490 / 004A55D0 `CMemoryStream` Read / Write / Seek / CopyFrom, 004A54D0 Resize, 004A5570 SetSize, 004A5610 StartByte, 004A5650 EndBits; 004A4D40 `CStream::CopyFrom` (slot 7), 004A51C0 / 004A5250 ReadSized / WriteSized, 004A52D0 / 004A5340 ReadHeader / WriteHeader | S on a 0x28-byte copy (`verify_memory_stream.cpp`); other stream classes O. Every save / load and network message |
| Stream diff, tagged blocks (`core_stream_diff.cpp`, `core_stream_chunks.cpp`) | 004A4DA0 `CStream::WriteDiff`, 004A50D0 `ApplyDiff` (run headers patched in place; a full run of 255 is followed by an empty run of the other kind), 004A4940 `ReadTagged`, 004A4A90 `FindTagged`, 004A48F0 `ReadText`, 004A4AF0 `ReplaceTagged` (no caller in game.exe: Rebirth's EI_Plugin.asi calls it through a thiscall thunk; Rebirth runs only) | S; world_load, ui_saveload, newgame |
| Growing byte buffer (`game_buffer.cpp`, `net_coder.h`) | 004DA7F0 ConstructGrowing, 004DA9E0 ConstructCopy, 004DAA80 AssignFrom, 004DA910 DestructGrowing, 004DA8F0 deleting destructor, 004DA5C0 Grow, 004DA630 / 004DA6B0 Reserve (slots 0 / 1), 004DA740 WriteMore, 004DA860 Seek, 004DA170 ReadPastEnd, 004DA450 / 004DA490 `BufferCursor::BeginRead` / `BeginWrite`; 004DA5A0 / 004DA5B0 / 004DA730 slots returning 0, 004DA850 (= 0047D960), 004DA580 / 004DA590 (the fixed view's empty slots); 004DA200 `GameBuffer::ReadString`, 004DA380 `WriteString` (`game_buffer_strings.cpp`) | S on a 0x1C-byte copy; save compression (world_load, newgame), multiplayer, net_lobby |
| Buffered file streams (`buffered_file.cpp`) | 004DAAF0 `Flush`, 004DB040 `BufferedStreamCommit` (slot 2, `Rebase`), 004DAC80 `ReleaseBlock`, slots 0, 1, 3, 4, 6 004DAE30 / 004DAFC0 / 004DB200 / 004DB420 / 004DB0F0 (`ReserveRead`, `ReserveWrite`, `ReadMore`, `WriteMore`, `Seek`), 004DB5E0 / 004DB630 `ReadAt` / `WriteAt` (7 / 8), 004DB680 / 004DB700 `BufferedFile::Close` / `Open` (O) | S; newgame (005809D0 reads "quest.reg") |
| Matrix helpers (`matrix_math.cpp`) | 004D9880 `MatrixMultiply`, 004D9AA0 `Quaternion::ToMatrix`, 004D9B80 `CTransformPipeline::PushMatrix` (stacks at +0x14, 8 bytes each); 004D9ED0 (= 0047B940) | S (same aliasing); every frame |
| View / light matrices (`view_matrices.cpp`) | 004F0EF0 `view::CameraViewMatrix` (stdcall: rows = the camera rotation's X axis (0040D940), Y and Z axes inline, column 3 = -position . row), 004F15C0 `LightRotation`, 004F1440 `LightShearMatrix` (the shadow's projection; map_object_shadow.cpp calls them by address; acos is the C++ one), 004E5660 (byte copy of `CubicSegment::Evaluate` 0040E440), 004E5710 `AllocateBlocks32` (as 00452DA0), 004F65B0 `AllocateBlocks8` (as 004545A0) | S; every frame, shadows, unit motion |
| Text splitting (`text_split.cpp`) | 004DBE40 `text::Split`, 004DBF50 `SplitWords`, 004DC400 `ReplaceChar`, 004DC390 `Join` (A-S10; CTexture::LoadBmp calls it directly) | S; console (lines with breaks), keyboard.ini at startup, item name keys; Join no scenario (loose .bmp only) |
| Text archive list (`text_sources.cpp`) | 004DE930 / 004DEAF0 / 004DEB60 `TextSourceList::Add` / `Remove` / `Clear`, 004DEDB0 / 004DEDD0 / 004DED40 `CTextManager::AddSource` / `RemoveSource` / `ClearSources` (004DED40 is slot 3 of vtable 0073F6F0) | O; newgame (client start, briefings add / remove zone texts), world_load |
| Zone exit (`exit_area.cpp`) | 004E59F0 `ZoneExit::Contains` (world.h's +0x24 is `angle`); A-S10: 004E5750 / 004E58A0 `ZoneExit::Read` / `Write` (+0x00..+0x20 dwords, target with terminator, +0x2C, +0x30, then byte +0x34, +0x38, angle; read from version 0x68 on, else 0), 004E5840 `CStream::ReadInto` (ReadString into a temporary, assigned) | P; Read / ReadInto S (the original reads first, the stream put back: position, ok flag), Write O; FindExitAt and 005677F0; zone_base_bz1g, newgame (client zone load, server zone message, saves) |
| Ground decals (`ground_decal.cpp`) | 004D7F50 `DecalBuilder::Construct`, 004D8000 `Begin`, 004D7F60 `Touches`, 004D8780 `Reserve`, 004D89A0 `Finish`; 004D85F0 `AddTriangle`, 004D8490 `AddTerrainTriangle`, 004D7BD0 `ClipPlane`, 004D74B0 `ClipFromV`, 004D71D0 `ClipFromUMax`, 004D6CF0 `Emit`, 004D6C50 `AddVertex`; 004D6E90 `decal::ClipTriangle`, 004D7140 `Interpolate`, 004D8470 `Vector3::LengthSquared` | S (NearlyEqual); every frame with shadows. ClipPlane / AddTriangle / AddTerrainTriangle are in tests/verify/disabled.txt (X17) |
| .mob reader (`mob_reader.cpp`), decoder | 00481F00 `ReadMobChunks` (scripts, weather, records, players), 00481660 `LoadMobFile` (relations applied, traps get their hostile mask and origin, units their alarm records 005D6D20, records deleted, scripts loaded unless keepIds), 004D8E10 `MobScriptDecoder::Decode` (resize 004D8EB0 = `CharVectorResizeOriginal`) | O / S; every zone load, zone_gz2g_traps |
| Trader stock (`class_template_stock.cpp`) | 004A6DC0 `ClassTemplateSetLevels`, 004A6DF0 `ClassTemplateGenerateItems`, 004A6F60 `ClassTemplateGenerateMagic` (list<CItem*> sentinel from the node pool, generators, AddItem(1, 1) + Release), 004A6580 / 004A66C0 `ClassTemplate::Construct` / `ItemStock::Construct` (193-bucket maps), 004A6A90 / 004A6A70 `ClassTemplate::Destruct` / `DeletingDestruct` | O; trader_stock, every new game (004A7630), world_load |
| Script runtime helpers (`script_runtime_helpers.cpp`, `party_script.cpp`) | 004BAEC0 `PartyForScript` ("owner::name"), 004B5EF0 `ScriptGroup::DestructBody`, 004BF7E0 `ScriptDeclDestruct`, 004D6340 `ScriptDefineList::Destruct`, 004BA090 `CAIMap::SetRefreshUnits`, 004BA0A0 / 004BA0C0 / 004BA0E0 / 004BA100 sector map wrappers (Move by address: Rebirth), 004BA9D0 `GameOStream::PutFloatArgument`, 004BA120 `CSceneRenderer::SetFogColour` (slot 0 00501AB0, `fogColour_` +0x14) | S / O; script_exec, script_copy, world_load |
| Global map clouds (`global_map_clouds.cpp`, `CCloudNoise`) | 00499DC0 Construct, 00499E90 DestructBody, 00499E30 base deleting destructor, 00499EE0 Start, 00499F10 Draw, 0049A010 SetRamp, 0049A090 MakeLayer (slot 0), 0049A220 Generate (slot 1) | S (SetRamp, MakeLayer, Generate steps 7-14) / O; ui_camp, frames_basic |
| Model helpers (`face_normal.cpp`, `model_names.cpp`) | 0049AEC0 `scene::ComputeFaceNormal` (edges from vertex 1 normalised unless below 1e-10 squared, crossed, normalised; zero or NaN length takes 007A9C98), 004DBCA0 `ReadPrefixedName` | S; ui_hero_screen, newgame, models |
| Particle attachment (`particle_attach.cpp`) | 004849F0 `CParticleSource::Attach`, 00484C00 `AttachmentPoint`, 00484B40 `PartPoint`, 00485280 `AttachmentOffset`, 00485380 `StopAndClear` (+ `ReleaseAllParticles`) | S (CheckNear; a unit's effect point, attachPoint 0, not compared: 00532860 can read uninitialised stack) / O; particles, spells_cast |
| Link figures (`link_figure.cpp`, `link_loader.cpp`) | 004F30F0 `CLinkMesh::AddPart` (bool in AL), 004F7F00 `LoadFigPart` (FIG / FIG8), 004F87D0 `LoadOldPart`, 004F2F90 `SetMorph`, slots 1-6 004F45E0 `UpdateScreenRect`, 004F3040 `HasPart`, 004F2FB0 `CollectPartNames`, 004F30D0 `LoadPart` (-> 004F7480), 004F3530 `RemovePart`, 004F3780 `GetPartData`; 004DB9C0 `ReadLinkString`, 004F7370 `CopyName`; loader slots 3 004F7150 `LoadFigure`, 4 004F7080 `LoadLink`, 5 004F7020 `ClearCache`, 6 004F75E0 `Remorph`, 004F77C0 `LoadLinkFile`, 004F7B10 `MakeFigure`, 004F7480 `LoadPartInto`, 004F7D20 `LoadArchivePart`, 004FAB00 `FindFigureSet`; 004F2C00 / 004F2E40 / 004F2D40 / 004F2E00 / 004F2E20 mesh Construct / Destruct / DestructBase / DeletingDestructBase / DeleteThis, 004F6BD0 / 004F6A30 loader Destruct / DeleteThis, 004F9E50 `RemorphPart`, 004F9FD0 `RemorphFigPart`, 004FA670 `RemorphOldPart`, 004F3800 `SetPartVertices`, 004F4B50 `MemoryBytes`, 004FAAA0 `CacheBytes` (no scenario) | S / O; newgame (00568800), models, ui_hero_screen |
| Sky (`sky_object.cpp`) | 004D9360 `CSkyObject::Light`, 004D9DE0 `CSky::Light`, 004D9110 / 004D92B0 / 004D9290 / 004D9270 / 004D9280 `CSkyObject` Construct / Destruct / DeletingDestruct / slots 11 / 12, 004D9680 `CSky::Construct`, 004D96E0 `CSky::Draw` (renderer slots 11 `SetTexture` / 14 `DrawFigure`; the object at the eye lowered by 16), 004D9E80 / 004D9E90 / 004D9EA0 `lib::AlignedArray<T>::Construct` | S when the haze needs no recomputing / O; every zone with a sky, render_scene |
| Server-specific manager (`server_specific_manager.cpp`) | 004D6900 / 004D6960 / 004D6940 / 004D6920 / 004D6930 / 004D6970 `CServerSpecificManager` Construct, Destruct, DeletingDestruct, slots 11 / 12, slot 14 `NewObject` (0x46-0x49 through their constructors, 0x4B `CTorchObjectSpec` inline + Create through the vtable); 004D6BA0 / 004D6B80 / 004D6B60 / 004D6B70 / 004D6B50 `CTorchObjectSpec` Destruct, DeletingDestruct, slots 11 / 12 / 6 | names / ids S, rest O; newgame |
| Pipeline projections (`model.cpp`) | 004F2650 `CTransformPipeline::ProjectBlocks`, 004F29F0 `ProjectTerrain` (`ITransformPipeline` slots 9 / 14 / 18 / 19) | O; shadows, every frame |
| Party manager archive (`party_manager.cpp`, A-S9) | 004A8560 / 004A8310 `PartyManager::Save` (S, ISAAC state put back) / `Load` (the manager 007AA5A0: parties hash_map<int, CParty> +0x00, entries +0x14, field +0x20, records +0x24, templates +0x38); 004ACEC0 / 004AD0C0 / 004ACDC0 / 004AD2C0 `ChunkArchive::SerializeParties` / `SerializePartyEntries` / `SerializeClassTemplates` / `IntField`, 004A64B0 `NamedPartyRecords::Serialize` (covered by Save); 004A6050 / 004A6360 `BeginRedeploy` / `EndRedeploy` (Begin spreads the variables (004A8AD0 operator=), zone and +0x6C to every record; End clears the party when no record matches, then text10 = text0C); 004A5AF0 `PartyRecord::Serialize`, 004A6C40 `ClassTemplate::Serialize` (SaveLists 0069BAC0 by address) | every save (autosave), world_load, ui_saveload, zone_* (RedeployParty) |
| Rain / snow (`effects_weather.cpp`, `CRainSnow`, CWorldClient + 0x6F4 = world.h's `ClientRibbons`) | A-S10: 004E3B80 `Clear` (slot 3 via the thunk 005617B0: rain loop stopped, texture +0x1C released, wind / mode / fade / world +0x20 cleared, density 64), 004E3FC0 `Start(mode, duration)` (texture "RainSnow" kind 6 through 00500A00; rain plays the loop "nature\rain\circle.wav" (Play2D, priority 1000); fade start = the world's tick). Not ported: 004E3980 (noise tables, only from the static initialiser 004E3B20), 004E3C20 (buffers; renderer data), the draws 004E4240 / 004E4C80 | Clear S (copy without sound and texture), Start O; world teardown, the server's weather messages |
| Visibility fog object (`effects_weather.cpp`, `CVisibilityFog`, vtable 0073D6B4) | A-S10: slots 2 / 3 004E23D0 / 004E2440 (cells freed to the pool, bounds invalid; slot 2 returns 1), 11 / 12 004E24A0 / 004E24B0 ("CVisibilityFog", 0x5F), destructor 004E24E0 and slot 0 004E24C0 | O (table empty) / S (11, 12); the fog of the show-visibility effect (no scenario known) |
| Misc | 00480B80 `DirectXErrorText` (`dx_errors.cpp`, a sorted table of 149 codes), 004800E0 `app::FatalErrorCode` ("%s (0x%.8X) %s"; always exits); the thrown `GameError` (A-S10, 0xC: vtable 0073CAB4 / base 0073CAB0, operator new message, result): constructors 00480250 (wvsprintf) / 00480300 (+ result text), destructor 004815D0, deleting destructors 004815B0 / 00481580 | S (constructors: the original formats into a scratch object with the first eight argument dwords passed on) / O; only on a DirectX failure |

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
  pointers are released through `CRefCounted` (`delete` = slot 0 with 1); the party map's party is `PartyInPlace` storage.
  The archive on the stack: `ChunkArchive::ConstructLocal` / `DestructLocal` (chunk_archive.cpp).

Classification of 0x004A0000-0x00500000: library.csv holds the STLport / MFC members (mesh maps, the link loader's cache
004F4CD0-004F6850 / 004FAE40-004FC6A0, the party manager's containers 004A90F0-004AFAC0 including the weapon / armour lookups
004AB720 / 004AB820, EH-only helpers); dead_code.csv the dumper's writer 004D41C0 and the 3DS loader's helpers
004A1E60-004A47D0. Not game logic here: the device layer 004E5E50-004F0B00 and state blocks 004FCA60-004FDB50 (renderer
work), `CSysInfo` 004DDCB0-004DE8B0 (static initialiser table 00783310; ported by A-E2).

### Next candidates in this range

- Renderer tickets: the D3D state-block manager 004FCA60-004FDFE0 (its hash_map of state vectors; the STLport members
  004FE0C0-004FF4C0 are now library rows), rain / snow draws 004E4240 / 004E4C80 and buffers 004E3C20, the fog draw
  004E1EB0-004E2290, the CLinkMesh draw slots 7-14 004F3870-004F4850, the device functions 004E5E50-004EFEA0; the TL
  library loader and matrices 004F0B90-004F15C0 (004F1330 Rebirth); the debug line lists 0049B140 / 0049B780.
- A-S10 did the particle manager (particles.md), the ISAAC seed 0049F660 / 0049F4B0 (core.md), 004DC390, the unit modes'
  slots, GameError, CRainSnow Clear / Start, the zone exit's stream form, CVisibilityFog's slots. Classified: 0049F820 /
  0049F840 / 0049F870 / 004E3B20 / 004E3AD0 are static initialisers (before the hooks; 004E3980 is only theirs: ported by A-E2); 0049F890
  (keyframe track) is reached only from 00448720, which only calls itself (dead); STLport / MFC rows for the party
  manager's containers 004B0A90-004B1590, the script CArrays 004B5ED0 / 004BD120 / 004BDB80 / 004BE250-004BE430, 004A39C0.
  Only reachable from rewritten callers (left unported): 004824B0 / 00483D40 (LoadMobFile's record map), 004B3200 (from
  0066E2E0), the accessors 004B9F20-004BA080 / 004BA9C0 (from 004B6760). Left: 004BD310 / 004BDE40 (skipped, above).
- Standalone build: `CSysInfo`, the party manager's static constructor 004A5870 and exit destructor 004A5700, CTorchObjectSpec
  Create / Destroy 0052C610 / 0052C680 (not ported; slots 13-15 004D6B20/30/40 are a bare RET).

### A-E1: the script leftovers below 0x004C0000

- 004BD310 `ScriptCreateFxSource` (`script_effects.cpp`; CreateFXSource 0x16 and CreateRandomizedFXSource 0x18). The id
  is FISTP-rounded, a negative one takes `nextFxId_` (then counted down). An existing source's sound is dropped; a new
  source is a `NewNode` at the tail. Position, volume, radius and name come from the command. 0x16 makes the sound at once
  (class 0x48 from the effect manager, the source's position, volume, radius and name, added to the timed objects, one
  more reference) and clears `idle`; 0x18 keeps no sound, takes min delay, max delay (FISTP) and spread, countdown 0,
  idle 1. Decision: for a new id 0x16 leaves spread, the delays and the countdown as whatever the stack held (the
  temporary element at EBP-0x50 is copied whole into the node, 004CB220); the tick (004BF000) counts such a source down
  and replaces its sound when the countdown reaches zero. Ours writes 0, 0, 0 and INT_MAX: the sound stays, as with any
  positive residue (zero would replace it on every tick). Until now the original ran from our `Execute`, so the residue
  was already ours. Ours only (`verify_script_effects.cpp`): the source and its sound against the command. script_fx.
- 004BDE40 `CScript::Construct` (`script_construct.cpp`): the member vtables (`scriptvtable::kPointLights` ..
  `kTempObjectList` in script.h), lists in blocks of 10, the four global variable sets, 256 zeroed light slots, one empty
  temporary group (004C8CE0) and one null temporary reference (004C9210), an empty error text, then `ScriptReset`. Only
  the world's constructor 00589ED0 calls it (twice), from the static initialiser 00589BE0, before the hooks: the hook
  and its side-by-side check (a shadow script constructed by the original; every dword but the scripts' own
  allocations equal) never fire in the hooked game. The rewrite is for the standalone build.
- 00456360 `CScript::GetError` (text copy-constructed at `out`, code, line; `ScriptErrorInfo` of world_glue.h): side by
  side, reached only when the command line's script fails to load.
- 004B3200 `ScriptGroup::CopyToList` (script_group.cpp): the group's objects replace a pointer list of counted
  references (each old element released, the list emptied, one counted element per object). Side by side on a
  shadow list holding the same elements (counted once more). The only caller is the server's party sync of a
  player without a party copy (player_sync.cpp, through `ScriptGroupToListOriginal`).
- app_lifetime.cpp: the creators the application's start registers with the class registry 0079B270 (00457850
  a class template as class 1, 00457740 a party record as class 2; both clear the count's dword before the null
  check, as the original), the party record's destructor (004577E0, slot 0 004577C0) and the party manager's
  exit-time destructor (004A5700: templates and records released, the strings, the entries, every party through
  the map's own node deleter 004ABA70, the bucket array). All ours only: a party's Create is not repeatable
  side by side and the rest destroy. The creators are reached when a saved party manager is loaded.
- small_accessors.cpp: the accessors the interpreter calls out of line, as `__fastcall` shims: 004B2520 (the
  referenced map object's companion, null without an object), 004B9F50 / 004B9F60 (bits of the object's flags
  set / cleared), 004B9F90 (the map object's position), 004B9FD0 (a counted object exists and is not
  detached; asked of the game server), 004BA010 (FISTP of a number), 004BA030 (`CItem::Repair`), 004BA060
  (the unit's controller), 004BA070 / 004BA080 (the carried items' count and element), 004BA9C0 (the
  controller's statistics), 004BAA20 (`ObjectListIterator::First`), 004BC1F0 (a CString written to a stream
  with its terminator; the definition's Save). Their only callers are Execute (004B6760) and
  ScriptDefinitionSave (004BBFE0), both rewritten with the same work inline, so no scenario reaches the
  hooks. Side by side where the prologue allows (the field put back between the runs); 004B2520, 004B9FD0,
  004BAA20 and 004BC1F0 ours only.

### A-S11: the plain game code left in 0x004C0000-0x00500000

The classification of all 173 functions that were left in the range is in renderer.md (section 12). The plain
game code among them (39): 18 rewritten, 21 left.

- Text manager (`text_sources.cpp`, `item_db_text.h`). 004DEBC0 `TextSourcesFind`: the first source that has
  the key, its `GetFile` result (the size through the out pointer); null without one. 004DEC40
  `TextSourcesListKeys`: every source's keys matching the pattern appended to the caller's array through a
  temporary `CStringArray` (`mfc::CStringArray::Append`, 006FEB01, added to mfc.h as a forwarder);
  Rebirth-patched (ResOverride), so it hooks in the vanilla launch only and `CTextManager::ListKeys` (004DEFA0)
  calls the original through a forwarder. 004DED20, forwarded elsewhere as an unnamed constructor, is
  `CTextManager`'s Create slot: `setlocale(LC_CTYPE, <the pointer at 0079BE94>)`, `_setmbcp(_MB_CP_LOCALE)`,
  returns 1. Side by side: 004DEC40 and 004DEFA0 (the original into a scratch array, names compared by content),
  004DED20 (running it twice changes nothing); 004DEBC0 ours only (GetFile counts the file's users)
  (`verify_text_keys.cpp`). Scenarios: Create on every start; the interface's "pers *" listing (00607720,
  00635250) has no known scenario.
- 004D0AF0 `ScriptSubCallClear` (`script_blocks.cpp`): type and argument index zero, the command cleared. Only
  the original `LoadFunction` calls it (a Rebirth launch's script loads). Ours only.
- `CSysInfo` (`sys_info.cpp`; the global at 007AB8F8, vtable 0073D644, class id 0xD, five CStrings at +0x1C).
  Rewritten: what can run after the hooks install: Create's shell 004DDF05 (it calls the collector 004DDF28
  and returns 1), the empty Destroy 004DDF1D, the class name / id slots 004DE890 / 004DE8A0, the destructor
  004DDE75, its deleting form 004DE8B0 and the exit handler 004DDD60. Side by side: the two pure slots; the
  rest ours only (`verify_sys_info.cpp`); the exit handler runs on every exit. A static initialiser runs the
  rest before the hooks install (ported by A-E2 and compared at WinMain's entry with what the original built,
  `verify_static_initialisers.cpp`): the initialisers 004DDCB0, 004DDCBA (it stores 2 * pi at
  007AB8F0), 004DDCCD, 004DDCD7, 004DDCEE, 004DDCF8, 004DDD0F, 004DDD19, 004DDD30 (constructs the global and
  registers 004DDD60), 004DDD3F, 004DDD4E, 004DE855, 004DE87C, the constructor 004DDD6F, the collector 004DDF28
  (1887 bytes: registry and version queries into the five texts) and its helpers 004DE687, 004DE6C2, 004DE6F3,
  004DE789.
- 004E3980 (the weather's noise tables filled from `rand`; `weather_noise.cpp`): only a static initialiser calls
  it; the tables are rebuilt from `rand` seeded with 1 at WinMain's entry and match bit for bit (A-E2).
- 004D5880 (from 004D36F0: a text file read through `CStdioFile` inside an MFC TRY / CATCH(CFileException);
  `script_text_file.cpp`, A-E2): the text is emptied, the file opened by the throwing constructor (text mode,
  share deny write), lines of up to 0x7FF characters appended, the file closed; a caught exception gives 0.
  Side by side at start-up (app.md "MFC TRY / CATCH"); no scenario loads a script text file.
- The visibility fog's manager (`vis_fog_manager.cpp`; `ClientArrayManager` in world.h is `CVisFogManager`,
  vtable 0073F554: the "VisionFog" texture at +0xB0 and a CList of counted fogs at +0xB4). 004E2290 (slot 14)
  makes a `CVisibilityFog`: engine object fields, +0x1C..+0x53 zero, a cell map of 193 buckets (resize(100));
  the map's three hash functor bytes at +0x54 are left as allocated, as in the original. 004E2990 (Create):
  the texture (kind 6), then the tile table at 007ABA58 the fog's draw reads, 16 records {u0, v1, u1, v0} with
  u0 = column / 4, v0 = 1 - (row + 1) / 4 and a quarter added for the other two, then the base manager's
  Create. 004E2850 (Destroy): the texture dropped (the pointer cleared before the release), every fog
  released, the list emptied. 004E2760 (the tick): each fog's tick; one returning 0 leaves the list and is
  released; returns 0. 004E28D0: a fog from `CreateObject(0, -1)`, counted once more, appended. 004E2700 (the
  world draw 00569E40): nothing listed returns 1; otherwise the texture is loaded if needed and bound to stage
  0 (0 when that fails) and every fog draws through 004E1EB0 (not rewritten). All ours only
  (`verify_vis_fog_manager.cpp`: each loads, allocates, releases or draws); Create / Destroy run on every
  start and exit, the tick and the draw on every frame of a map, the factory and the new fog only with the
  "show visibility" effect (0067C660), for which no scenario is known. The world files keep their forwarders
  to 004E2850, 004E28D0 and 004E2700 (another ticket's files); the hooks catch them.
