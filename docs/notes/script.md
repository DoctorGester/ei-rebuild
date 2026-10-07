# Mission script engine (0x004B0000-0x004D8000) and the text manager line reader (0x004DEFC0-0x004DFFFF)

Files (`src/game/`): `script.h` (every layout with offset asserts, including the whole `CScript`, 0x1F8 bytes),
`script_group.cpp`, `script_vars.cpp`, `script_eval.cpp`, `script_parse.cpp`, `script_parse_load.cpp`,
`script_parse_arrays.cpp`, `script_exec.cpp`, `script_exec_run.cpp`, `script_exec_helpers.cpp`, `script_save.cpp`,
`script_load.cpp`, `script_array.h`, `script_vars_append.cpp`, `script_copy.cpp`, `script_destroy.cpp`,
`script_arrays.cpp`, `script_blocks.cpp`, `script_reset.cpp`, `script_cleanup.cpp`, `script_effects.cpp`,
`script_quest.cpp`, `script_unit_modes.cpp`, `script_addon*.cpp` (Rebirth, see `rebirth.md`), `text_reader.cpp`.
Verify: `tests/verify/verify_script*.cpp` (+ `script_compare.h`), `verify_text_reader.cpp`. Scenarios:
`script_parse`, `script_exec`, `script_copy`, `script_fx`, `script_quest`, `world_load` (save / load round trip),
`console`, `effects`, `particles`, `zone_gz2g_traps`. Names are provisional. Nothing rewritten here lies in Rebirth's
patched range 0x004CF4A2-0x004D460C except where a row says "Rebirth".

## Data structures

- Value types (numbering used everywhere): 1 float, 2 string (CString), 3 object, 4 group.
- `ScriptObjectRef` (4 bytes): counted reference to a world object (`refCount_` at +4; Release is vtable slot 4); a
  world object deleted while referenced has bit 0 of `flags_`. `ScriptGroup`: `CArray<ScriptObjectRef>` (vtable 0073D110,
  its destructor sets 0073D11C), kept sorted by object address (unsigned) without duplicates; `SetSize` is 004C2050.
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
- `CScript` key facts: a command block's id is the list's count right after the append (the WorldScript's with bit 31
  set); the temporaries lists (+0x1B0 groups, +0x1CC object references) always keep one entry (the constructor adds it);
  +0x1E8 counts the For nesting (temporaries are not freed while it is positive); +0x1A8 loaded flag, +0x1AC file counter;
  +0x1EC / +0x1F0 / +0x1F4 error text / code / line; +0x68 / +0x6C are -2 after a reset. Areas (+0x194) hold 0x14-byte parts
  `{u8 isRect; x, y, x2 | radius^2, y2}`. FX sources (0x34 bytes) are sound sources: +0x20 / +0x24 volume / radius copied to
  the effect's +0x24C / +0x250, +0x28 `idle` (the tick skips it), +0x2C placement radius, +0x30 / +0x34 min / max delay,
  +0x38 countdown. Point lights: 256 slots of 0x20.
- Keyword table at 0078BCA0 (`ScriptKeyword`, 16 bytes, null-terminated at 0078CAB0): name, id, function signature ("Fff":
  result + params) or command signature ("of"). Ids: ( 6D, ) 6E, if 6F, then 70, `,` 71, = 72, : 73, WorldScript 74, Script
  75, DeclareScript 76, GlobalVars 77; numbers 7A and strings 7B are not in the table; command 8 is a declared-script call
  (not a keyword). `CalcFloat` handles the float functions, `CalcObject` the object ones (0x4E, 0x85, 0xAB, 0xB4, 0xE4),
  `CalcGroup` (004B5B00) the group ones. Particle type table 0078B120 (64 entries: name, type, lifetime).
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
become empty lines); in every other line each define's name is replaced by its value (in list order, restarting after
each replacement; `AddDefine` inserts before the first shorter name else before the last define; a value containing its
name never finishes expanding). 3. `Tokenize` into a `CList<ScriptToken>` (vtable 0073D424; token = text CString + line;
"no token" is "None", -1). 4. Sections by keyword id: `WorldScript(` commands `)` (at most one per file; run at the end
when `runWorldScript`), `Script name(` params `)(` body `)` (a definition not declared before; after `DeclareScript` it is
`Script name(` body `)` with no parameter list, once), `DeclareScript name(` params `)` ("Duplicate script" if it exists),
`GlobalVars(` declarations `)`, anything else "Unexpected '%s'". 5. On success the loaded flag is set and the file counter
incremented; a failed load does not count the file but definitions and globals added before the error stay.
Names declared in a file are qualified with the file counter: `QualifyName` 004D37F0 gives `!<count>!name` once a file has
been loaded; lookups try the plain name then the qualified one; `FindScript` 004D3780 also retries without each leading
`!n!`, so a console line can refer to a name from the previous console line only if that line failed (a scenario relies on it).
Script body `if(` float functions `)then(` commands `)` pairs (`LoadScriptBody` 004D1030; conditions `LoadConditions`
004D0BF0, commands `LoadCommands` 004D1330) go into command blocks from `NewCommandList` 004BF540. A command is a keyword
with a command signature (For / ForIf are followed by `(` body `)` parsed into another block at SSCCmd+0x90, without the
script's locals), a declared script (command 8: float 0 = script index, string 0 = name, the parameter types as signature),
or `global = value` (`LoadAssignOperator` 004D1D50: command 0x78, float 0 = variable index, float 1 = type - 1).
`LoadFunction` 004CFBC0 parses one call against a signature string (f float, s string, o object, g group, O loop counter =
a global object variable); each parameter is a literal, a local variable (binding +0x54), a global variable (plain or
qualified, binding +0x68) or a nested call of a function with the right result type (`FunctionSignature` 004D0B10).
Lexer (`ClassifyToken` 004CF600): lines start at 1 and count `\n` in white space and block comments; a token runs to white
space (a space inside quotes does not end it) or to one of `, ) ( : =` after its first character, at most 255 characters
looked at; only the recognised prefix is kept and the rest scanned again (a number, a quoted string, or the longest
keyword the text starts with, case-insensitively), so `(abc` is `(` `abc`, `1.5.5` is `1.5` `.5`, and `Run1` is `Run` `1`;
`/*` starts a comment only at the start of a token and the search for `*/` starts after that token (`/**/2` does not
close it; quoted `"*/"` inside is skipped); `//` runs to the end of the line. The keyword table address is read from
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
binds variables (`SetParameters`), evaluates nested calls (`CalcParameters`), runs the command, frees temporaries,
advances. A waiting command leaves the index on itself and Execute returns 1: Sleep(n) records the tick count (+0xC) and
waits while `ticks - start < n` (FISTP; so n further Execute calls), then resets it to -1; WaitSegment(n) waits while the
run has been executed fewer than n times in total; SleepUntil(c) while c is 0 or NaN; SleepUntilIdle(unit) while the
unit's brain has an action (+0x240 object +0x984). For(counter, group)(body) / ForIf(counter, group, condition)(body) walk
the group from last to first (size read once), store the member in the counter (a global object variable) and run the
body block in the same run (block, index, ticks and sleep start saved, set to body / 0 / 0 / -1, Execute recursion, restored:
a Sleep in the body ends the body for that member); ForIf evaluates its condition for each member. A script call
(command 8) adds an instance of definition `float 0` at the HEAD of the instance list (004C7C90 is an AddHead: new node
before the head, the tail changes only for an empty list), `ScriptBindCall` 004BB6A0 copies the arguments into its
variables (FatalError "Wrong number of script %s float parameters" etc.); an instance started by a waiting run runs its
pairs in the same tick, one started by another instance's commands from the next tick. Assignment (0x78): float 1 selects
floats (copied as bits), strings, objects (reference) or groups (CArray Copy 004C1290).
Unit fields the commands touch: +0x240 server state (+0x980 AI brain, +0x1E0 skills / stats, +0x1C owner object), +0x244
hostile mask, +0x250 side, +0x4A0 / +0x4A4 quest items; `flags_` 0x800000 blocked, 0x10000 hidden, 0x80000 trap active.
Players: +0x20 quest variable / music / zone object, +0xC0 money, +0x108 god mode byte.

## Savegame writer and reader

Reached by every save (the new game's autosave) and load (`world_load`); the world loads only its first script (007C27D4).
`ScriptSave` 004C0F90 order: point lights (004B1940 each, count first), particle / FX / lightning lists (004B1A80 /
004B1C50 / 004B1DB0), +0x68, +0x6C, command blocks (+0x70) and WorldScript blocks (+0x178) via `ScriptDeclSave` 004C1230
(block id, count, `SSCCmd::Save` each; 004B6360), definitions (+0x8C, 004BBFE0), globals (+0x0A0, four savers 004C2EF0 floats /
004C33C0 strings / 004C38C0 objects / 004C4330 groups), instances (+0x140, 004BBFE0), runs (+0x15C, 004BB320: finds the
run's instance by comparing its kill-flag pointer with each node's data in 007AA620's instance list, -1 when absent),
+0x1A8, +0x1AC, then 007AA620 = 0 and the areas (004CAEC0, only read for stream versions >= 0x57). Lists are written as count
then each node (next read before the call); a variable's name is written with its terminator, or as one zero byte when the
name array is shorter than the value array. Effect elements: object id (-1 none), then fields; loading looks the id up in
the effect manager (007AFFA8, `CBaseManager::FindObject` 00403F40) and raises the found object's count inline.
`ScriptLoad` 004C0D00 mirrors it: list loaders read the count, append that many default nodes, then read every node from the
head (so a non-empty list would have its old nodes read again; never the case: the world is new). Block ids resolve in the
lists of 007C27D4 (`FindBlock` 004BFA50: negative ids in the WorldScript blocks, others in the command blocks; unknown gives
null). Runs: block id, index, ticks, sleep start, the instance index (n-th node of the loading script's list, 0 out of
range), `ScriptVars::Load` 004B3AD0 (the four variable loaders; one stack variable holds the float count and index and then
receives every object id and group size; object ids go through `FindObject` and `Assign` 004B24D0, group members through
`ScriptObjectRef::Load` then `Sort`). `SSCCmd::Load` 004B5FE0 mirrors `Save`.

## Quests, unit modes, text manager

- Quest construction: `QStart` 004BAA40 (name in ECX; empties the objectives at 007AA608 `{type, text, value}`, 12 bytes),
  `QObj*` 004BAE10 (objective appended), `QFinish` 004BAAB0 builds a script text and loads it: `DeclareScript Su0()` ..
  `SuN()`, per objective `Script Sui(if(` condition `)then(KillScript() Sui+1() GSSetVar(0,"q.q.q.i+1",2)` (and
  `GSSetVar(0,"q.q.q.i+2",1)` except for the last), the closing `SuN` (QuestComplete, GSSetVar "q.q.q" 2), `WorldScript( Su0()
  )`; then the quest name is emptied (the objectives stay; QStart frees them). Not rewritten: 004BAEC0 (party records of a
  "party name", CString Find / Mid with three arguments). The quest kill table (`QuestTable`, 007C87A8, hash_map by name at
  +0x0C) is looked up by QuestComplete through `QuestTable::Find` 004C9C90 (operator[]: a missing name gets a null entry).
- Unit mode constructors (`script_unit_modes.cpp`, vtables named in `aimode::` / `unit_ai.h`): 004BA1C0 `GuardMode` (0073D128)
  then `Set(point, radius, wander)`, 004BA360 `SentryMode` (0073D190), 004BA450 `FollowMode` (base 0073D15C, then the leader
  referenced, 0073D1C4), 004BA610 `SuspectionMode` (0073D1F8), 004BA710 / 004BA830 `AggressionMode` (0073D260; the second with
  `fixed` = UMRevenge), 004BA920 `CorpseWatcherMode` (0073D294), 004BA1A0 `AILook` (UMPatrolLook). None sets the count (+4):
  the commands write 0 there after operator new. Verify: the original constructs a copy of the object's current bytes, ours
  the object; all bytes and both returned pointers equal.
- Text manager line reader (`text_reader.cpp` on `CTextManager`; 54 callers of Open in the interface, the map loader's
  FindLine / Split, the item texts): the cursor (+0x20) walks the current text (+0x24, +0x28 bytes); every end test is the
  signed `(p - text) < length`; characters are MBCS. Open 004DEFC0 asks each archive (slot 5, GetFile(key, &length)) in
  list order; PeekWord 004DF770 (the cursor does not move); SkipLine 004DF6E0; NextLine 004DF510 (past the line and every line
  whose first word starts "##"); ReadLine 004DF8D0; ReadToHash 004DFB80; Split 004DFCB0 (a word still open when the text ends
  is dropped); FindLine 004DF030 (key null = the next line whose first word starts with "#" but not "##"; a one-word key = the
  next line whose first word equals it; several words (004DBE40 splits the key) = a line whose first words, lower-cased with
  MakeLower, equal the key's); CharCode 004DF4E0; Close 004DF020. The original inlines the "move past the next LF" loop in five
  variants; `Advance` takes the variant. Not rewritten: 004DEC40 (Rebirth patches it), the archive list helpers 004DEB60 /
  004DED40 / 004DEDE0 / 004DEBC0, 004DED20.

## Rewritten

Verify idioms used below (the column says which): *pure* side by side; *double evaluation* (save the ISAAC state, run ours into a
local result, record the generator state, restore, run the original into the real result, compare; cost grows with tree size
times depth); *capture streams* (both versions write into capture streams; bytes and write sizes compared); *record / replay*
(the original reads the real stream into a fresh scratch object through a recording stream, ours replays the recording into the
real object; reads must match in order and size; scratch destroyed with the game's own helpers; ours only when the real object
is not empty on entry); *deep-copy destructor check* (the original destroys a deep copy made with the game's copy functions, ours
the real object; every dword the original changed in the copy must hold the same value in the real one, every other dword must
be unchanged, and reference counts must drop by the number of times held); *ours only* (state checks).

| Subsystem | Addresses and names | Verify |
|---|---|---|
| Object refs, groups, vars (`script_group.cpp`, `script_vars.cpp`) | 004B24B0 (and 004B5AE0) Reset, 004B24D0 Assign, 004B2500 Construct (returns the object in EAX), 004B2530 AsUnit, 004B2550 GetUnitPerception, 004B2570 ReleaseIfDeleted, 004B2590 / 004B25F0 Load / Save; 004B2620 / 004B2920 / 004B2B60 Union / Difference / Intersection, 004B2D60 Sort, 004B2DA0 IndexOf, 004B2E60 Remove, 004B2EF0 Insert, 004B3110 RemoveDeleted, 004B31D0 InsertAll, 004B3330 Contains, 004B3360 Load; 004B35E0 ResetValues, 004B36D0 HasVariable, 004B37D0 FindVariable, 004B3940 RemoveDeletedObjects | pure; ours only (slot / count); group ops: original into a clone with its own references, elements, size, capacity compared; Load / Save record / replay and capture streams |
| Evaluator (`script_eval.cpp`) | 004B3F60 SetParameters (idempotent: ours, record bound values, original, compare), 004B4030 CalcParameters, 004B41E0 CalcFloat, 004B56C0 CalcObject, 004B5B00 CalcGroup (for Rebirth's AddNewScriptCommands) | double evaluation |
| Parser (`script_parse*.cpp`) | 004CF470 Error, 004CF600 ClassifyToken, 004D0B10 FunctionSignature, 004D0BA0 KeywordName (Rebirth patches these: vanilla only unless flagged), 004CFB70 / 004D0BD0 / 004D1D30 token construct / assign / copy, 004CF380 ConstructTokens, 004CF7A0 Tokenize, 004CF4D0 ExpectLexeme, 004D37F0 QualifyName, 004D3780 FindScript, 004D34F0 AddDefine, 004D33E0 ApplyDefines, 004D0A50 SSCCmd::Construct, 004CFBC0 LoadFunction (Rebirth), 004D0BF0 LoadConditions, 004D1D50 LoadAssignOperator, 004D1FE0 LoadVariableDeclarations, 004D1030 LoadScriptBody, 004D1330 LoadCommands (Rebirth), 004D23A0 Load, 004D36F0 LoadFile | pure / output into a scratch / original on a copy of the position with a deep tree comparison; Load, LoadFile, LoadScriptBody, LoadCommands ours only (success counts the file; ends on `)`) |
| Parser arrays (`script_parse_arrays.cpp`) | 004D5B50 BindingsAdd, 004D5CB0 SubCallsAdd, 004D5ED0 CmdsAdd, 004D65E0 CmdsSetAtGrow, 004D6100 DefinitionsAdd, 004C5480 SubCallDestruct, 004D5A50 / 5A90 / 5AD0 / 5B10 FindVariableName, 004D5A10 / 004D5A30 array constructors | original on a scratch array (shallow buffer copy): result, shape, all elements; new elements deep-equal |
| Runtime (`script_exec*.cpp`) | 004B6760 Execute (Rebirth), 004B6360 / 004B6330, 004BB6A0 ScriptBindCall, 004BB520, 004BC420, 004BCC60, 004BEC80 FreeTemporaries, 004BF000 ScriptTick | ours only; Execute through an observer after each command (position, ticks, sleep start, For depth restored; Sleep / WaitSegment / SleepUntil stop exactly when they should; KillScript sets the flag; a call adds a live instance; assignments hold the value; AddObject leaves the group sorted; flag bits; SetPlayer / SetEnemy / GodMode) |
| Areas, point lights (`script_exec_helpers.cpp`) | 004B1ED0 / 004B2070 AreaAddRect / AddRound, 004BCF30 DeleteArea, 004BD6A0 / 004BD820 / 004BD920 CreatePointLight / MovePointLight / DeletePointLight | output on a copy; ours only |
| Quests | 004BAA40 QStart, 004BAE10 QObj, 004BAEB0 objective destructor, 004BAAB0 QFinish (original runs first with its CScript::Load skipped via `scriptverify::g_loadSkip`; the two texts must be equal and the name empty); 004C9C90 `QuestTable::Find` (`script_quest.cpp`; original first, then ours: same record, map size unchanged; `script_quest`'s QuestComplete line) | ours only / against the original |
| Unit modes | 004BA1C0 .. 004BA920, 004BA1A0 | as described above |
| Save / load (`script_save.cpp`, `script_load.cpp`) | 004C0F90, 004C1230, 004C2EF0, 004C33C0, 004C38C0, 004C4330, 004CAEC0, 004BBFE0, 004BB320, 004B1940, 004B1A80, 004B1C50, 004B1DB0 (savers); 004C0D00 ScriptLoad, 004CAC00 Runs, 004CA980 Instances, 004CA000 IfThen, 004C40D0 Groups, 004C36B0 Objects, 004C57C0 DeclCommands, 004C3200 Strings, 004C2D40 Floats, 004CA480 Particles, 004CA5D0 FxSources, 004CA720 Lightning, 004CA810 WorldBlocks, 004B5FE0 SSCCmd::Load, 004B3AD0 ScriptVars::Load, 004BB590 IfThenPairLoad, 004B3D10 StringLoad, 004B6330 SubCallLoad, 004B18B0 / 004B1B40 PointLight / FxSource load, 004BFA50 FindBlock | capture streams; loaders record / replay (nested loaders run through their wrappers in both runs, so SSCCmd trees are read 2^depth times in the verify build); ScriptLoad ours only (seven lists well formed); FindBlock pure |
| Appends, clears, copies (`script_vars_append.cpp`, `script_copy.cpp`) | 004B3900 AppendFrom, 004BB800 AddParameter (`script_vars.cpp`: ours only, counts and the appended letter), 004C3DC0 / 004C3450 / 004C2FE0 / 004C2B70 Groups / Objects / Strings / Floats Append, 004C0020 VarsClear, 004C1760 / 004C1700 / 004C16B0 / 004C1650 per-type clears; 004C6BC0 SSCCmd::operator=, 004CB570 run, 004CBAE0 vars, 004CB220 FX source, 004CB2D0 decl assign; 004CB7E0 VarsCopyConstruct, 004CCAA0 SubCallCopyAt, 004CE750 CmdCopyAt, 004CBEF0 / 004CBDB0 / 004CBA00 groups / objects / names copy constructors, 004CBB80 / 004CBCA0 floats / strings copy constructors, 004CB460 DefinitionAssign, 004C2F80 StringsCopyTo, 004CDCB0 LooksSetAtGrow, 004CE5F0 NamesSetAtGrow, 004C00D0 VarsConstruct, 004D3320 DefinitionReset | ours only: the source captured first (savers' bytes, float bits after FLD / FSTP, strings, pointer arguments, bindings), the destination must match; constructors: original into a 0xCC scratch and all bytes compared; StringsCopyTo / SetAtGrow against the original on a scratch |
| Constructors, destructors (`script_destroy.cpp`, `script_arrays.cpp`, `script_reset.cpp`, `script_cleanup.cpp`) | 004BE090 VarsDestruct, 004BE9D0 DefinitionDestruct, 004BF640 CmdDestruct, 004BC690 / 4BC740 / 4BC870 / 4BC950 named-value constructors, 004BC6C0 / 4BC770 / 4BC8A0 / 4BC810 / 4BC9A0 / 4BCA20 array destructors, 004BCAE0 RunDestruct, 004CE1E0 / 4CE210 / 4CE230 / 4CE270 / 4CE2B0 / 4CE2D0 / 4BF620 element destructors, 004C81B0, 004CD690, 004B17F0 / 4B1820 / 4B1850 / 4B1880 / 004BD540 effect element destructors; 004BE6E0 ScriptReset, 004BED90 PurgeDeletedObjects, 004BE490 DropInstancesAndRuns, 004BEB00 ResetZone; 004BF540 / 004BF900 / 004BCE10 / 004B59F0 / 004B5F70 NewCommandList / NewWorldScript / FindArea / NewTempObject / Clear (`script_blocks.cpp`) | deep-copy destructor check; constructors original into a 0xCC scratch; Reset / Purge / ResetZone ours only (flags, lists empty, one run per WorldScript block at its start, lights zeroed); Blocks / FindArea against the original on a scratch image of the list |
| Effects (`script_effects.cpp`) | 004BD260 CreateFX, 004BCFF0 CreateLightning, 004BD9A0 CreateParticleSource, 004BDBB0 MoveParticleSource, savers / loaders as above | ours only (class 0x48 / 0x4D / 0x4A object registered with the arguments' bits; ids); MoveParticleSource against the original (the step with CheckNear) |
| Text reader (`text_reader.cpp`) | 004DF030, 004DFCB0, 004DF8D0, 004DF510, 004DF770, 004DFB80, 004DF6E0, 004DEFC0 (GetFile runs twice; the archive raises the file's count twice), 004DF4E0, 004DF020 | original first, state recorded and restored, then ours; result, cursor and strings compared; CharCode pure |

## Leftovers outside the script engine

Functions of 0x00480000-0x004B0000 and 0x004D8000-0x00500000 that belong to other subsystems.

| Area (file) | Addresses and names | Verify |
|---|---|---|
| Memory stream (`core_memory_stream.cpp`, `core_streams.h`) | 004A5380 / 004A53F0 / 004A5490 / 004A55D0 `CMemoryStream` slots Read / Write / Seek / CopyFrom, 004A54D0 Resize, 004A5570 SetSize, 004A5610 StartByte, 004A5650 EndBits; 004A4D40 `CStream::CopyFrom` (slot 7), 004A51C0 / 004A5250 ReadSized / WriteSized, 004A52D0 / 004A5340 ReadHeader / WriteHeader | original on a 0x28-byte copy of the stream (`verify_memory_stream.cpp` says how each shares or clones the buffer); both CopyFroms and the sized helpers on other stream classes ours only. Reached by every save / load and network message (`world_load`, `newgame`) |
| Growing byte buffer (`game_buffer.cpp`, `net_coder.h`) | 004DA7F0 ConstructGrowing, 004DA9E0 ConstructCopy, 004DAA80 AssignFrom, 004DA910 DestructGrowing, 004DA8F0 deleting destructor, 004DA5C0 Grow, 004DA630 / 004DA6B0 Reserve (slots 0 / 1), 004DA740 WriteMore, 004DA860 Seek, 004DA170 ReadPastEnd, 004DA450 / 004DA490 `BufferCursor::BeginRead` / `BeginWrite`; 004DA5A0 / 004DA5B0 / 004DA730 slots returning 0, 004DA850 (= 0047D960) | original on a 0x1C-byte copy, with a cloned block where it frees one; grown buffers compared by offsets and the copied bytes. Save-game compression (`world_load`, `newgame`) reaches construct / destruct / Reserve / Begin*; Copy, Assign, Grow, WriteMore mainly multiplayer |
| Matrix helpers (`matrix_math.cpp`, `model.h`, `maths.h`) | 004D9880 `MatrixMultiply`, 004D9AA0 `Quaternion::ToMatrix`, 004D9B80 `CTransformPipeline::PushMatrix` (stacks at +0x14, 8 bytes each); 004D9ED0 (= 0047B940 `AlignedPointerArrayAlloc`) | original into scratch matrices with the same aliasing, NearlyEqual per element; push: original first, its slot and depth recorded and put back, then ours. Every drawn frame (`world_load`, `render_scene`) |
| Text splitting (`text_split.cpp`, `text_split.h`) | 004DBE40 `text::Split`, 004DBF50 `text::SplitWords`, 004DC400 `text::ReplaceChar` | original into a scratch CStringArray / a copy of the string; sizes, strings and results compared. Split: console lines with line breaks (`console`), item texts; SplitWords: keyboard.ini at startup (every scenario); ReplaceChar: item name keys |

- Memory stream facts: Write grows to (capacity + size) * 2 copying the old capacity's bytes; Resize grows to
  size * 1.5 + 0x10 copying the old size's bytes and flushes the pending bits byte only when it grows; SetSize
  resets only bitMask_ (bitsIndex_ stays); a short Read zero-fills the whole request before copying what is left.
- Growing buffer facts: every growth makes a block of 2 * (needed end, counted from the stream start `base`) + 0x40 bytes
  and copies the old capacity's bytes; Reserve and WriteMore grow without testing the room (callers test); Seek returns
  the new capacity in EAX when it grew.
- Matrix facts: each product element sums its four terms in the original's order (two orders: `kProductOrder` /
  `kPushOrder`), pinned with StoreFloat against /fp:fast regrouping; elements are written in memory order, so an
  output that is also an input behaves as in the original.
- MFC naming traps met here: `mfc::CString::Mid(int)` (006FDA6D) is MFC's Right(n); the real Mid(first) is 006FD9B4
  (`console::Str::Mid`). 006FE216 is TrimLeft() and 006FE11F TrimRight() (ini_file.cpp's TextOps names them the other
  way round; the calls are right).

## Original bugs and oddities kept

- NaN: `IsEqual` and `Not` treat NaN as equal, `IsLess` as less; `IsInSquare`, `UnitInSquare`, `IsInArea` the same mixed
  way; `SetDiplomacy` maps NaN to neutral; flag commands treat NaN as zero.
- `CalcParameters` for a string-returning nested call evaluates its arguments, then FatalError "Function does not return
  string at SSCCmd::CalcFunction" (no such functions exist; Rebirth's patched CalcParameters calls `CalcString` instead).
  `IsInArea` with an unknown id creates an empty area; `GetObject` logs the id after converting the float again; `Every` with
  no nested condition copies the constant argument as the result.
- Parser: keyword prefixes split identifiers; `/**/` as one token does not close the comment; the parameter token after `(` /
  `,`, the name after `Script` / `DeclareScript`, the token after `if` / `then` and a declared variable's name are read without
  an end check (a script ending in `Foo(` dereferences a null position); a For / ForIf whose header fails still parses its body
  (the error is then usually "Wrong lexeme ,"); For / ForIf bodies are parsed without the script's locals (Rebirth's
  AllowScriptArgsInLoops fixes it); `LoadVariableDeclarations` returns 0 without an error when the tokens run out;
  `LoadScriptBody` leaves a failed pair's blocks in the list; "In script %s should be at least one condtion" (sic) prints the
  qualified name.
- Runtime: `SetSpellAggression` (0x42) does nothing; `KillScript` in the WorldScript reports FatalError and then writes through a
  null pointer; a run keeps a pointer to its instance's killed flag after the instance is removed; `SleepUntilIdle` has no null
  check; `ResetTarget` (0xE8) on a unit without a target can crash the original (005D6630); `InflictDamage` calls TakeHit (vtable
  +0x50) with three arguments (hit, 0, 0): a two-argument call unbalanced the stack and crashed the first run; the mode commands
  write the count (+4) before testing operator new; a For counter is a global, so nested loops on the same counter overwrite it;
  `CreatePointLight` stops at the first free slot (two lights can share an id; with 256 slots used and no match it writes one slot
  past the array); `DeleteArea` leaves the moved-out last entry in memory; the tick saves each node's successor before running it;
  `CreateLightning` appends without looking the id up (two bolts can share an id); its id test is `FCOMP 0; TEST AH,1` (negative
  or NaN: automatic) then `_ftol`; `CreateFX` keeps no reference (timed object only); `CreateParticleSource` id is FISTP-rounded;
  `QStart` sets size and capacity to 0 even without an array; `AddParameter` with a type outside 1..4 returns the name argument's
  address and appends an empty letter.
- Savegame: block ids resolve in the first world script's lists whatever script loads; the first block id of an if-then pair reads
  into a variable preset to -1, the second into the same variable without a preset; list loaders re-read nodes that were in the list;
  `ScriptVars::Load` reuses one stack variable for the float count and every object id; `AppendFrom` copies over the other set's
  names while the values grow by its values; the definition and run savers do not null-check blocks; a missing name is one zero byte.
- The exception frames of the rewritten functions are not reproduced (a failed operator new in `SubCallCopyAt` leaves the copy's
  command null, as the original does).

## Skipped, dead, library, open items

- Skipped: 004BD310 `CreateFXSource` / `CreateRandomizedFXSource`: for a new id the original leaves spread, min / max delay and
  countdown (+0x24..+0x30) as stack garbage the tick then uses; rewriting needs values the original never had (zero would replace the
  sound every tick). 004BDE40 `CScript` constructor: it runs as part of the world's static initialiser 00589BE0 before the hooks, so
  it needs the standalone build (layout: lights array 0x73D394 resized to 256 zeroed slots, list vtables 0x73D388 / 0x73D37C / 0x73D370
  / 0x73D364 (blocks and WorldScript blocks) / 0x73D34C / 0x73D340, block size 10, definitions 0x73D358, areas 0x73D334, globals 004BC690
  / 004BC740 / 004BC870 / 004BC950, one temporary group and one null reference added (004C8CE0 / 004C9210), error text empty, then the
  reset). 004BFC60 (find a definition by name; only the dead dumper calls it). The small accessors 004B9F20-004BAA30 (field reads and
  flag writes) are inlined in our commands. The multiplayer branches of GSSetVar / GSSetVarMax / GSDelVar / QuestComplete are rewritten
  but only reached in multiplayer.
- Dead (config/dead_code.csv): the quest script builders 004C01A0 / 004C0520 / 004C06F0 / 004C09F0 and the script dumper 004D53C0 ->
  004D39D0 / 004D50C0 -> 004D41C0 (Rebirth patches it) / 004D4E80 / 004D5310, 004D38B0 / 004D38F0. 004B3A70 (copy all values from
  another ScriptVars) has no callers.
- Library (config/library.csv, 237 functions): the `CArray` / `CList` instantiations of the script types between 004C1290 and
  004CEE80 (vtables 0073D110-0073D4C0; each "subclass" vtable is the derived class the game declares over a CArray, its destructor
  falls back to the base vtable) and the quest table's STLport hash_map members (004CE110, 004CE460, 004CEF00, 004CF0F0, 004CF160).
- 004B2520 (unit stats pointer, 16 bytes with a branch in the first 5): no trampoline possible; one caller. 004D8000-004DEFFF belongs to
  other subsystems (renderer / AI map helpers, STLport instantiations).
- gz2g's scripts (zone2.mob chunk 0xACCEECCB, decoded as 004D8E10 does: the first dword seeds the MSVC rand LCG, `x = x * 0x343FD +
  0x269EC3`, each byte XORed with `x >> 16`) start every magic trap armed and disarm them from triggers (`ActivateTrap(MTF1..4, 0)`);
  the `zone_gz2g_traps` scenario reaches command 8 (`KillScript() VTriger#0#n( this )`) from a killed instance, which is why the
  Execute observer reads the instance list's head (+0x144), not the tail.

## Live leftovers of 0x00480000-0x00500000 (mob reader, stream helpers, particles, clouds)

| Area (file) | Addresses and names | Verify |
|---|---|---|
| .mob reader (`mob_reader.cpp`, `map_spec.h`) | 00481F00 `ReadMobChunks` (scripts, weather, records, players), 00481660 `LoadMobFile` (relations applied, traps get their hostile mask and origin, units their alarm records 005D6D20, records deleted, scripts loaded unless keepIds) | ours only (records keyed by id, count, maxId; result). Every zone load (`newgame`, `world_load`, `zone_*`) |
| Trader stock (`class_template_stock.cpp`, `class_templates.h`: `allowed` / `magic` are now `ItemStock`) | 004A6DC0 `ClassTemplateSetLevels`, 004A6DF0 `ClassTemplateGenerateItems`, 004A6F60 `ClassTemplateGenerateMagic` (list<CItem*> sentinel from the node pool, generators, AddItem(1, 1) + Release, nodes back to the pool) | ours only: levels in both stocks / the class index. `trader_stock` |
| Runtime helpers the script code forwarded to (`script_runtime_helpers.cpp`, `party_script.cpp`) | 004BAEC0 `PartyForScript` ("owner::name"), 004B5EF0 `ScriptGroup::DestructBody`, 004BF7E0 `ScriptDeclDestruct` (each command through ScriptCmdDestruct; the original inlines it with the arrays' out-of-line destructors, same order), 004D6340 `ScriptDefineList::Destruct`, 004BA090 `CAIMap::SetRefreshUnits`, 004BA0A0 / 004BA0C0 / 004BA0E0 / 004BA100 the sector map wrappers (Move through its address: Rebirth), 004BA9D0 `GameOStream::PutFloatArgument` | PartyForScript and SetRefreshUnits against the original (copy of the name / flag put back); the rest ours only (vtables, list emptied, bool results). `script_exec`, `script_copy`, `world_load` |
| Save game difference coding (`core_stream_diff.cpp`, `core_streams.h`) | 004A4DA0 `CStream::WriteDiff`, 004A50D0 `CStream::ApplyDiff` (run headers patched in place; a full run of 255 is followed by an empty run of the other kind) | both against the original: WriteDiff into two scratch memory streams; ApplyDiff original into a scratch, the difference stream sought back, ours into the real one. `world_load`, `ui_saveload` |
| Tagged blocks and text (`core_stream_chunks.cpp`) | 004A4940 `CStream::ReadTagged`, 004A4A90 `FindTagged`, 004A48F0 `ReadText` | against the original: original first into scratch outputs, the stream sought back, ours on the real ones (result, tag, end position, output bytes). `world_load`, `ui_saveload`, `newgame` |
| Buffer strings (`game_buffer_strings.cpp`, `net_coder.h`) | 004DA200 `GameBuffer::ReadString` (string assign inlined: overwrite and cut, or overwrite and append through 0040C2A0), 004DA380 `WriteString` | ReadString against the original on a byte copy of the buffer and a scratch string when the whole string lies before `end` / `high`, else ours only; WriteString ours only (header, characters and cursor where it was). `net_lobby`, `newgame` |
| Global map clouds (`global_map_clouds.cpp/.h`, `CCloudNoise`) | 00499DC0 Construct, 00499E90 DestructBody, 00499E30 base deleting destructor, 00499EE0 Start, 00499F10 Draw, 0049A010 SetRamp, 0049A090 MakeLayer (slot 0), 0049A220 Generate (slot 1) | SetRamp, MakeLayer (generator state saved and replayed) and Generate steps 7-14 against the original; the rest ours only (buffers, step / fade ranges). `ui_camp`, `frames_basic` (`screen 4`) |
| Model helpers (`face_normal.cpp`, `model_names.cpp`; `MeshFace` +0x24 is now `normal` / `distance`) | 0049AEC0 `scene::ComputeFaceNormal` (edges from vertex 1 normalised unless below 1e-10 squared, crossed, normalised; zero or NaN length takes 007A9C98), 004DBCA0 `ReadPrefixedName` | both against the original (face copy, CheckNear; scratch CString and cursor). Fake unit scene (`ui_hero_screen`), skeleton links (`newgame`, `models`) |
| Particle attachment (`particle_attach.cpp`) | 004849F0 `CParticleSource::Attach`, 00484C00 `AttachmentPoint`, 00484B40 `PartPoint`, 00485280 `AttachmentOffset`, 00485380 `StopAndClear` (+ `ReleaseAllParticles`, shared with Clear) | PartPoint / AttachmentPoint / AttachmentOffset against the original (CheckNear; a unit's effect point, attachPoint 0, not compared: 00532860 can read uninitialised stack); Attach / StopAndClear ours only (attachedTo, listed in the object's sources, origin = the original's attachment point or the position). `particles`, `spells_cast` |

- Attachment facts: attachPoint 1-7 = "hd", "hd" + 1 in z, "lh2", "rh2", "ll2", "rl2", "bd"; 8-12 = "body00".."body04";
  every part falls back to "bd", then to the position raised by the bounds' upper z extent. Not a unit or attachPoint > 12:
  position raised by radius * 1.6 (bounds bit 0) or the box height. Attach recurses through Add/RemoveEffectSource; the
  nested Attach(object) from AddEffectSource runs before the outer call sets the origin, so the wrapper checks the origin
  only for calls that change the attachment (the first version compared it there: 573 false mismatches).
- .mob facts: the .mob wind strength is clamped by the float at 0079BE58, which nothing writes (0), so ChangeWind gets
  at most 0; the weather chunk's last two floats are read and unused. 00481660's branch under 0079BCF4 (the colour
  quantisation 0049BEA0 and its STLport map / list / introsort helpers 00482640, 00483540, 004825A0, 00482AA0, 0049BB70,
  0049C5A0, 0049D*, ...) never runs: nothing in game.exe, addon.dll or EI_Plugin.asi writes 0079BCF4 (bss, a byte search
  finds its address only in the test at 00481704). Not reproduced; the 78 functions only that branch reaches (callers
  closed over Ghidra's xrefs, no Unwind or unnamed callers) are in config/dead_code.csv.
- Original bugs kept: 00481660 deletes every record while the hash_map still holds the pointers (freed with the map).
- Skipped: 004D5880 (ReadTextFile: MFC TRY / CATCH around CStdioFile, whose constructor throws the game's CFileException;
  our code cannot catch the game's C++ exceptions), 004BA120 (the display object's background colour: needs a declared
  display class with its slot 0).
- Not ported (yet): 004A4AF0 (replaces a tagged block; no callers found), 004A8310 / 004A8560 (party load / save: tagged
  stream helpers 00409AE0.. and STLport tables), 004A66C0 / 004A6580 (ItemStock / ClassTemplate constructors: STLport members).
- Clouds: SetRamp's alpha is trunc(255 - pow(width / 255, max(0, i - start)) * 255) >> 4 with the CRT pow; a last-bit pow
  difference can only matter on an exact multiple of 16 (SetRamp's wrapper would show it). Draw's blend is
  trunc((step + phase) / 16 * 254 + 1); the original's third SetRamp argument is the fade speed.

## Decals, matrices, text archive list, zone exit; what is left of 0x004C0000-0x00500000

Classification of what was left at the time of the sweep (363 functions, 90.6 KB; config/functions.csv `game` minus library, dead and EI_REPLACE):

| Kind | Functions | Decision |
|---|---|---|
| Library templates | 25 (MFC CArray / CList helpers, STLport vector / copy / node creators in 004C98B0-004F65F0) | config/library.csv |
| Dead | 5: the dumper's writer 004D41C0 (Rebirth patches three operands in it, nothing calls it) and what only it reaches (004D4E80, 004D38F0, 004D5EC0, 004D65B0) | config/dead_code.csv |
| Called only by rewritten originals | 004DEBC0 (TextSourceList::Find: GetItemTexts), 004C17F0 (SSCCmd::Execute), 004D0AF0 (LoadFunction), 004D3650 / 004D3670 / 004D6310 (Load: the #define pair and list), 004D5880 (LoadFile: CStdioFile TRY / CATCH), 004C17E0 (a counted pointer release: 0069AC20 and unwind funclets) | stay; dead in the standalone build (docs/standalone-cleanup.md) |
| Device layer (004E5E50-004F0B00, 004FCA60-004FDB50 state blocks) | 72 + about 15, 21 KB + | not ported: the renderer worker replaces them (no flat device rewrites, docs/notes/renderer.md section 9) |
| Static initialisers | `CSysInfo` 004DDCB0-004DE8B0 (26, 3.1 KB: OS version, computer / user name, memory, CPU; built by the static initialiser table at 00783310 before the hooks) | standalone build |
| Real game code | ported: ground decals 004D6C50-004D89A0 (15), view / light matrices and helpers (6), the text archive list (6), 004E59F0 (28 in all). Left: mob script decoder 004D8E10, `CServerSpecificManager` 004D6900-004D6BA0, `CSkyObject` 004D9110-004D9EB0, CStringArray join 004DC390, fog and rain / snow 004E1EB0-004E5840, the TL library loader and matrices 004F0B90-004F15C0 (004F1330 Rebirth), figure / link loading 004F2650-004FBD80, the effect manager and STLport members 004FC320-004FF4C0 | port, largest and most called first |

### Ground decals (`ground_decal.cpp`, `ground_decal.h`)

| Address | Name | Verify |
|---|---|---|
| 004D7F50, 004D8000, 004D7F60, 004D8780, 004D89A0 | `DecalBuilder::Construct`, `Begin`, `Touches`, `Reserve`, `Finish` | Construct / Touches: the original on a copy, result and bytes; Begin / Reserve / Finish: the original on a deep copy of the builder and its three arrays, shapes and used elements (Begin's fields with NearlyEqual) |
| 004D85F0, 004D8490, 004D7BD0, 004D74B0, 004D71D0, 004D6CF0, 004D6C50 | `AddTriangle`, `AddTerrainTriangle`, `ClipPlane`, `ClipFromV`, `ClipFromUMax`, `Emit`, `AddVertex` | original first; counts, new vertices (NearlyEqual) and indices recorded, counts and the input triangle put back, then ours |
| 004D6E90, 004D7140, 004D8470 | `decal::ClipTriangle`, `decal::Interpolate`, `Vector3::LengthSquared` | the original on copies; counts, triangles, NearlyEqual |

Reached by every frame with shadows (`newgame`, `world_load`): 0056C750 projects a shadow quad (DrawShadow 0051CE60,
006CEDE0), 006CCF20 adds the terrain triangles under it. The original's clip stages are five hand-unrolled functions
with a function-static triangle buffer per stage (007AA710 plane, 007AA750 u >= 0, 007AA790 v >= 0, 007AA7D0 u <= 1,
007AA810 v <= 1); `ClipStage` is one recursive stage function with a buffer per stage, which gives the same in-place
clipping and emission order. Facts: a corner is inside when its distance's sign bit is clear (-0 is outside, NaN
with a clear sign inside); the vertex arrays are 4-vertex SoA packets (x[4], y[4], z[4], w untouched); `Reserve` grows
the packets by triangles * 3 / 4 + 1 + the vertex count (not count / 4: over-reserves, kept); `Finish` pads the last
packet with copies of vertex 0. The statics' guard bits and empty atexit handlers are not reproduced. A triangle
whose corner lies on a clipping edge can come out differently if a distance rounds across zero (rule 7).
Verify lessons (rule 7): `/fp:fast` sums the projections (u, v, plane distance) as (z + y) + (x + offset) where the original does ((z + y) + x) + offset; the sums
cancel to small values and differ by ULPs of the largest product, so the wrapper compares appended vertices within 1e-3 + 1e-5 relative (`NearClipped`) and ours is unchanged. The stage
order and buffer reuse were checked against 004D7BD0 / 004D74B0 / 004D71D0 / 004D8490 (clip plane -> u -> v -> 1 - u -> 1 - v; 1 - u as FLD 1.0; FSUB, so u = 1 gives +0, inside;
004D6E90's inside test is NOT; SHR 31 on the bits). In the verify build the original 004D8490 calls the hooked 004D7BD0 (ending with ours), so AddTerrainTriangle compares only the
corners' u / v rounding: a corner within rounding of u or v = 0 / 1, or an edge along a clip line (Interpolate dividing by a tiny dq - dp), can land on the other side and change the
triangles that follow (about 12 of 37M calls). The wrapper logs the first 4 counted mismatches ("AddTerrainTriangle mismatch N"; disabled until a run shows the case).

### View and light matrices (`view_matrices.cpp`, `view_matrices.h`)

| Address | Name | Verify |
|---|---|---|
| 004F0EF0 | `view::CameraViewMatrix` (stdcall: rows = the camera rotation's X axis (0040D940), Y and Z axes inline, column 3 = -position . row) | original into a scratch matrix, NearlyEqual |
| 004F15C0, 004F1440 | `view::LightRotation`, `view::LightShearMatrix` (the shadow's projection; map_object_shadow.cpp still calls them through their addresses) | same; acos is the C++ one (the original's _CIacos), within a few ULPs |
| 004E5660 | a byte copy of `CubicSegment::Evaluate` (0040E440), registered to the same method | against the original, CheckNear |
| 004E5710 | `view::AllocateBlocks32` (as 00452DA0) | ours only: alignment, stored block pointer |

Scenarios: every frame (`newgame`, `world_load`); shadows for the light pair; unit motion for the cubic.

### Text archive list (`text_sources.cpp`; declarations in `item_db_text.h`)

| Address | Name | Verify |
|---|---|---|
| 004DE930, 004DEAF0, 004DEB60 | `TextSourceList::Add`, `Remove`, `Clear` | ours only: list shape (Add: listed name gives 1 and no change, else one node more at the end with that name and an archive; Remove: the name gone, one node fewer; Clear: empty) |
| 004DEDB0, 004DEDD0, 004DED40 | `CTextManager::AddSource`, `RemoveSource`, `ClearSources` (list at +0x1C; 004DED40 is slot 3 of vtable 0073F6F0) | same checks on the manager's list |

Add keeps a failed archive in the list (unlike `CLinkFigureLoader::AddArchive`) and returns OpenResource's result;
nodes go back to the node allocator's 0x18-byte list through `stlp::deallocate` (the original inlines the push).
Not ported: 004DEBC0 (`Find`, only called by the original GetItemTexts, which is rewritten and inlines the lookup),
004DEC40 (Rebirth) and its 21-byte forwarder 004DEFA0, 004DED20 (setlocale / _setmbcp, slot 2). Scenarios: `newgame`
(client start adds the text archives, briefings add / remove zone texts), `world_load`.

### Zone exit area, aligned 8-byte blocks

| Address | Name | Verify |
|---|---|---|
| 004E59F0 | `ZoneExit::Contains` (`exit_area.cpp`; world.h's +0x24 is now `angle`; world_client.cpp's forwarder removed) | against the original (pure). FindExitAt (world_client.cpp) and 005677F0; `zone_base_bz1g`, `newgame` |
| 004F65B0 | `view::AllocateBlocks8` (as 004545A0) | ours only: alignment, block pointer. Interface windows, figure rasteriser, shadows |

Contains: a zero or NaN angle tests the rectangle as is (x1 > x, x > x2, y1 > y, y > y2 reject; NaN passes); otherwise
the point is turned by -angle about the centre (centre x and the x offset kept unrounded, as the original keeps
them on the FPU stack) and a NaN turned x is outside.

## Buffered file streams and link figures

### Buffered file streams (`buffered_file.cpp`, `buffered_file.h`)

A `GameBuffer` whose block is a window onto a file (`base` = file position of `begin`); abstract vtable 0073F8C8
(constructor / destructor 00580DD0 / 00580DF0, slots 7 / 8 pure), file vtable 0073F8A4 (+0x1C `FILE*`, destructor
00580E60). The game's stdio is now in `lib/crt.h` (fopen 006E9107, fclose 006E8EB8, fseek 006E95D2, ftell 006E97C4,
fread 006E8F35, fwrite 006E968B).

| Address | Name | Verify |
|---|---|---|
| 004DAAF0, 004DB040, 004DAC80 | `Flush`, `BufferedStreamCommit` (slot 2, `Rebase`), `ReleaseBlock` | original on a byte copy with a cloned block; base, flags, capacity, offsets, old capacity's bytes |
| 004DAE30, 004DAFC0, 004DB200, 004DB420, 004DB0F0 | slots 0, 1, 3, 4, 6 (`ReserveRead`, `ReserveWrite`, `ReadMore`, `WriteMore`, `Seek`) | same, plus result and bytes read (the original reads into a scratch destination); Seek's EAX only on the in-window path |
| 004DB5E0, 004DB630 | `BufferedFileReadAt` / `WriteAt` (slots 7 / 8) | original then ours (both seek first): result, bytes read |
| 004DB680, 004DB700 | `BufferedFile::Close`, `Open` | ours only: no block / file after Close; Open's window or error bit |
| 004DA580, 004DA590 | `BufferViewReserve` (the fixed view's empty slots 0 / 1) | ours only |

Scenario: `newgame` (005810A0 -> 005809D0 reads "quest.reg" through the file stream when the server exists).
Not ported: 004DABC0 (no callers; an Attach-like window setup) and the unlisted 004DAB40 (a grow helper, no callers).
Original quirks kept: `ReserveRead` growing reloads at `base` with min(capacity, high - cur) bytes; the reload count can be
negative after a large read (passed to fread as is); Seek's out-of-window path returns no meaningful EAX (ours: the old cursor).

### Link figures, part loading (`link_figure.cpp`, `link_figure.h`)

`CLinkMesh` (vtable 0073D790, 0xB0 bytes): part headers map<string, 0x50> +0x14, morph coefficients +0x30, six
aligned arrays +0x3C (vertex packets, normal packets, texture coordinates, 6-byte corners, dwords, words), part ranges
map<string, 0x30> +0x9C, loader +0xA8, pipeline +0xAC. The maps stay STLport (operator[] 004F4B90 / 004F4CE0 added to
library.csv, find 004F5C90; forwarders in originals_script.h). `ITransformPipeline` is 3DfpFPU.dll's pipeline (slot 7
FitBounds).

| Address | Name | Verify |
|---|---|---|
| 004F30F0 | `CLinkMesh::AddPart` | original on a scratch mesh (original constructor 004F2C00 / destructor 004F2E40): appended elements, ranges, result |
| 004F7F00, 004F87D0 | `CLinkFigureLoader::LoadFigPart` (FIG / FIG8), `LoadOldPart` (6 morph corners) | same: cursor, header dwords, bounds (NearlyEqual, new parts), appended elements (vertices NearlyEqual) |

Scenario: `newgame` (00568800, the world client's object creation, calls the world view's loader slot 3 004F7150).
Morphs are plain floats: FIG8 lerp(lerp(lerp(c0,c1,s), lerp(c2,c3,s), t), lerp(lerp(c4,c5,s), lerp(c6,c7,s), t), u),
old records lerp(lerp(lerp(c0,c1,s), c2, t), lerp(lerp(c3,c4,s), c5, t), u).

### Link figures, the mesh's slots

`CLinkMesh` is now declared with its virtual slots 0-6 (novtable); `LinkMapNode` gives the map nodes (key +0x10, value
+0x1C) and `Next` the in-order successor (as 0043DBE0). Erase 004F4E40 / 004F5090 added to library.csv.

| Address | Name | Verify |
|---|---|---|
| 004F2F90 | `SetMorph` | original on a byte copy, 0xB0 bytes |
| 004F45E0, 004F3040, 004F3780 | slots 1, 2, 6: `UpdateScreenRect`, `HasPart`, `GetPartData` | original on a byte copy / pure / into scratch outputs |
| 004F2FB0 | slot 3 `CollectPartNames` | original into a fresh vector; the names ours appended compared |
| 004F30D0, 004F3530 | slots 4, 5: `LoadPart` (forwards to the loader's 004F7480), `RemovePart` | ours only: a loaded part is a part; a removed part is gone and the arrays shrank by its counts |

Original bug kept: `RemovePart` lowers the later parts' starts by the removed part's starts, not its counts (only
right for the first part), and picks the later parts by their index start alone.
Not ported: slots 7-14 (004F3870 ... 004F4850): part draws through the D3D device (renderer direction).

Verify lessons: `AddPart` 004F30F0 returns a bool in AL, not an int (its success path ends `MOV EDI,EAX` then `MOV AL,1`, so EAX's upper bytes are 004F4CE0's value pointer); the wrapper
and trampoline are `bool`, and the AddPart / loader checks name the first differing field. `corners_` is `lib::AlignedArray<IndexTriangle>` (6-byte elements): `AppendedDiff` must
compare in triangle units (a `* 3` offset in 6-byte units read past the block and crashed in SameBytes), with a capacity guard ("array bounds") and static_asserts on the CLinkMesh
array offsets and element sizes.

### Next candidates in this range

Loader slots and members 004F77C0 (load a .lnk), 004F7B10 / 004F7D20 / 004F7150 / 004F7080 / 004F75E0 / 004F7480 /
004F7370 (cache multimap +0x28 of meshes by name and morph; strings and refcounts with EH paths), the mesh constructor /
destructors 004F2C00 / 004F2E40 / 004F2D40 / deleting 004F2E00 / 004F2E20, the length-prefixed string reader 004DB9C0
(keeps the old string for length 0), 004F2650 / 004F29F0, 004F9E50-004FAB00 (004FAB00 finds an archive), the STLport
map members around 004F4CD0-004F5F00 (classify as library); rain / snow draws 004E4240 / 004E4C80 (build vertices then
draw through the device: coordinate with the renderer), the fog object's slots 004E2290-004E24E0 (never reached in the
current runs), CSkyObject 004D9110-004D9EB0, CServerSpecificManager 004D6900-004D6BA0, the .mob decoder 004D8E10.

## A-S7: .mob decoder, link strings, sky, server-specific manager, link loader

| Address | Name (file) | Verify |
|---|---|---|
| 004D8E10 | `MobScriptDecoder::Decode` (`mob_script_decoder.cpp`; resize through 004D8EB0, `CharVectorResizeOriginal` in originals_script.h; world_game.cpp's decoder is now this struct) | original on a scratch decoder (same input) into a scratch CString: text bytes, string. `zone_gz2g_traps`, `world_load` |
| 004DB9C0 | `ReadLinkString` (`link_loader.cpp`; the static buffer 007AA8D0 is only this function's, so ours copies straight from the record) | original on a string copy and cursor: text, cursor. `newgame` |
