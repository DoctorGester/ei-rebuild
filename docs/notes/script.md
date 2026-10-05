# Mission script engine (0x004B0000-0x004D8000)

Code: `src/game/script.h`, `script_group.cpp` (object references, groups), `script_vars.cpp`
(variables, SSCCmd::SetParameters/Save), `script_eval.cpp` (CalcParameters/CalcFloat/CalcObject).
Verify wrappers: `tests/verify/verify_script.cpp`.

## Data structures recovered

- **Value types** (numbering used everywhere): 1 float, 2 string (CString), 3 object, 4 group.
- **ScriptObjectRef** (4 bytes): counted reference to a world object (`CEngineObject::refCount_`
  at +4; Release is vtable slot 4). A world object deleted while referenced has bit 0 of `flags_`.
- **ScriptGroup**: `CArray<ScriptObjectRef, ScriptObjectRef&>` (vtable 0073D110, destructor sets
  0073D11C), kept sorted by object address (unsigned), no duplicates. `SetSize` is 004C2050.
- **ScriptVars** (0xA0): for each type a value CArray and a CStringArray of names:
  floats +0x00 / names +0x14, strings +0x28 / +0x3C, objects +0x50 / +0x64, groups (CArray of
  ScriptGroup, 0x14 each) +0x78 / +0x8C.
- **SSCCmd** (0x94, size from the copy constructor at 004CCAA0): `id` +0, argument arrays floats
  +0x04, strings +0x18, objects +0x2C (ScriptObjectRef* pointing into variables or temporaries),
  groups +0x40 (ScriptGroup*), local variable bindings +0x54 and global bindings +0x68
  (`{type, argIndex, varIndex}`, 12 bytes), nested calls +0x7C (`{type, argIndex, SSCCmd*}`), and
  +0x90 a called-script declaration (its +0x14 is written by Save).
- **CScript** (the interpreter state passed to the Calc functions as `script`): global variables
  (ScriptVars) at +0xA0, areas array at +0x194 (FindArea 004BCE10), temporary groups list at +0x1B0
  (CList, AddTail 004C8D80), temporary object references list at +0x1D0 (NewTempObject 004B59F0).
- **Areas** (IsInArea): array of 0x14-byte parts `{u8 isRect; float x, y, x2|radius², y2}`.
- **Keyword table** at 0078BCA0 (`ScriptKeyword`, 16 bytes, null-terminated at 0078CAB0): name, id,
  function signature ("Fff": result + params) or command signature ("of"). Ids 0x6D-0x77 are
  punctuation/sections, commands and functions follow. CalcFloat handles the float functions,
  CalcObject the object ones (0x4E, 0x85, 0xAB, 0xB4, 0xE4), CalcGroup (004B5B00) the group ones.
- **Save stream**: object with vtable slot 1 = Read(buf, n), slot 2 = Write(buf, n).
- Globals used: world/object registry 007AFF60 (FindObject 00593AA0, GetPlayer 00594180,
  TerrainHeight 00594220), game time int 007AFFC4, alarm slots 007AFFEC (16 bytes:
  active, time, x, y), IsNight float 007AFFE4, ISAAC generator 007A9D40, diplomacy 007C2BC4,
  game server pointer 0079B5DC (multiplayer), empty string constant 0079B298.

## Rewritten functions

| Address | Name | Verification |
|---|---|---|
| 004B24B0 | ScriptObjectRef::Reset (also 004B5AE0, identical code) | stateful: slot is null after |
| 004B24D0 | ScriptObjectRef::Assign | stateful: slot holds object, refcount +1 (unless same object) |
| 004B2500 | ScriptObjectRef::Construct (returns the object in EAX) | stateful: result, slot, refcount |
| 004B2530 | ScriptObjectRef::AsUnit | pure, side by side |
| 004B2550 | ScriptObjectRef::GetUnitPerception (+0x240) | pure, side by side |
| 004B2570 | ScriptObjectRef::ReleaseIfDeleted | stateful: cleared iff deleted flag |
| 004B2590 | ScriptObjectRef::Load | original reads the real stream into a scratch ref (bytes recorded), ours replays the bytes; compare objects |
| 004B25F0 | ScriptObjectRef::Save | both write to capture streams; bytes and write sizes compared; original's writes forwarded |
| 004B2620 | ScriptGroup::Union | output: original into a clone (own references) of `this`, ours into the real group; elements, size, capacity |
| 004B2920 | ScriptGroup::Difference | as Union |
| 004B2B60 | ScriptGroup::Intersection | as Union |
| 004B2D60 | ScriptGroup::Sort | clone, compare |
| 004B2DA0 | ScriptGroup::IndexOf | pure |
| 004B2E60 | ScriptGroup::Remove | clone, compare |
| 004B2EF0 | ScriptGroup::Insert | clone, compare, plus sortedness |
| 004B3110 | ScriptGroup::RemoveDeleted | clone, compare |
| 004B31D0 | ScriptGroup::InsertAll | clone, compare |
| 004B3330 | ScriptGroup::Contains | pure |
| 004B3360 | ScriptGroup::Load | record/replay stream as ScriptObjectRef::Load, clone, compare |
| 004B35E0 | ScriptVars::ResetValues | stateful: all floats 0, strings empty, objects null, groups freed |
| 004B36D0 | ScriptVars::HasVariable | pure |
| 004B37D0 | ScriptVars::FindVariable | output: original into scratch index/type, compare result and outputs |
| 004B3940 | ScriptVars::RemoveDeletedObjects | stateful: no deleted objects left, groups have no null entries |
| 004B3F60 | SSCCmd::SetParameters | idempotent: ours, record bound argument values, original, compare |
| 004B6360 | SSCCmd::Save | capture streams as above (nested calls recurse through the wrapper) |
| 004B4030 | SSCCmd::CalcParameters | double evaluation (below); compares the filled argument (float bits, object, group contents) |
| 004B41E0 | SSCCmd::CalcFloat | double evaluation; result bits and generator state |
| 004B56C0 | SSCCmd::CalcObject | double evaluation; objects of the two temporaries |

Double evaluation: save the ISAAC state (007A9D40), run ours into a local result, record the
generator state, restore it, run the original into the real result, compare. Re-evaluation is
otherwise harmless: Every/Any/Sum leave the object variable on the same last member, FindArea
creates a missing area only once, and the extra temporaries go to the CScript's temporary lists
(freed with them). Ours calls its own callees directly, the original goes through the wrappers, so
the cost grows roughly with tree size times depth.

None of the rewritten functions lies in Rebirth's patched range 0x004CF4A2-0x004D460C.

## Forwarders added (standalone build must replace)

- World: 00593AA0 FindObject, 00594180 GetPlayer, 00594220 TerrainHeight, 00591740 MercsNumber,
  0055FB00 FindUnitObject; vtable +0x70 (GetBSZValue).
- Unit/objects: 00530280 unit ground height; vtable +0xD0 future position; stats (+0x170) vtable
  +0x44 death state.
- 00455630 diplomacy, 00665640 player HasItem, 00460AF0 player quest variable, 005BBC70 water test,
  0057F440 multiplayer flag, 00419940 / 0041A020 game server GetVar / leader id, 005788B0 camera
  playing, 00461360 object id from name.
- CScript: 004B59F0 NewTempObject, 004BCE10 FindArea; the group temporaries list (+0x1B0) AddTail 004C8D80,
  group destructor 004C2290, assignment 004C1290; world object iterator 00404920 / 00404960.
- CArray<ScriptObjectRef>::SetSize 004C2050; CStringArray copy-to helper 004C2F80.
- CRT kept local to the script files: qsort 006E88FC, memmove 006E79A0, _mbscmp 006E848B,
  atoi 006E80D8, _ftol 006E853C, _CIfmod 006E95AA (could move to `lib/crt.h`).

## Original bugs and oddities kept

- IsEqual and Not treat NaN as equal (FCOM C3), IsLess treats NaN as less; IsInSquare,
  UnitInSquare and IsInArea use the same mixed NaN behaviour.
- CalcParameters for a string-returning nested call still evaluates its arguments, then calls
  FatalError "Function does not return string at SSCCmd::CalcFunction" (no such functions exist).
  CalcGroup's default case uses the same "string" message.
- IsInArea with an unknown area id creates an empty area (FindArea).
- GetObject logs the id via DebugLog after converting the float again.
- Every with no nested condition copies the constant argument as the result.

## Skipped

- (004B5B00 SSCCmd::CalcGroup was rewritten later, for Rebirth's AddNewScriptCommands: script_eval.cpp;
  PlayerSee walks the party hash map like combat_actions.cpp.)
- 004B6760 SSCCmd::Execute (14 KB command switch), rewritten later as ScriptRun::Execute: see
  "Command execution and runtime" below. The CScript loader 004CF4D0-004D53C0 was rewritten
  later: see "Loader and parser" below.
- 004B5FE0 SSCCmd::Load, 004B3AD0 ScriptVars::Load, 004B3360's siblings (CArray SetSize/Serialize
  template instantiations 004C1xxx-004CExxx): MFC template code.
- 004B3A70 (copy all values from another ScriptVars): no callers found.
- 004B2520 (unit stats pointer): 16 bytes with a branch in the first 5, no clean trampoline; one caller.
- 004B0000-004B2070 and 004BA000-004CE000: other subsystems (areas, quests, unit modes) not mapped.

# Loader and parser (004CF380-004D36F0)

Code: `src/game/script_parse.cpp` (tokens, lexer, keyword lookup, #define, names),
`script_parse_load.cpp` (parser), `script_parse.h` (helpers shared with the verify file); types in
`script.h`. Verify wrappers: `tests/verify/verify_script_parse.cpp`. In-game scenario:
`tests/scenarios/script_parse.txt`.

## How a script is loaded

`CScript::Load(CString* text, BOOL runWorldScript)` (004D23A0) is the entry for mission files
(`LoadFile` 004D36F0 reads a file first), quest scripts and the console. The console (005DC620,
cheat word required) turns `@x` into `WorldScript(x)`, `#x` into `WorldScript( ConsoleFloat( x ))`
and `$x` into `WorldScript( ConsoleString( x ))` and loads that into the mission script at
007C29CC (005785D0, which then runs one script tick, 004BF000); on failure it prints
`Script error: <error text>`.

1. Error state reset: line +0x1F4 = 0, code +0x1F0 = 0, text +0x1EC = "No errors found".
2. Preprocessing, line by line: `#define name value` lines are collected into a list (and become
   empty lines); in every other line each define's name is replaced by its value (ApplyDefines,
   in list order, restarting after each replacement). The caller's text is replaced by the result.
3. `Tokenize` (004CF7A0) into a `CList<ScriptToken>` (vtable 0073D424, block size 10).
4. Top-level sections, by keyword id:
   - `WorldScript(` commands `)`: at most one per file; parsed into `NewWorldScript()` (004BF900)
     with an empty local variable set, run at the end when `runWorldScript`.
   - `Script name(` params `)(` body `)`: a definition not declared before. When `DeclareScript`
     declared it: `Script name(` body `)` (no parameter list), and only once.
   - `DeclareScript name(` params `)`: a definition without body ("Duplicate script" if it exists).
   - `GlobalVars(` declarations `)`: the variables (qualified names) are added to the globals.
   - anything else: "Unexpected '%s'".
5. On success the loaded flag +0x1A8 is set and the file counter +0x1AC incremented. A failed load
   does not count the file, but definitions and globals added before the error stay.

Names declared in a file are qualified with the file counter: `QualifyName` (004D37F0) gives
`!<count>!name` once a file has been loaded. Lookups try the plain name, then the qualified one;
`FindScript` (004D3780) also retries without each leading `!n!`. So a console line can only refer
to a name declared by the previous console line if that line failed (the scenario relies on it).

Script body: `if(` float functions `)then(` commands `)` pairs up to `)` (LoadScriptBody 004D1030).
Conditions (LoadConditions 004D0BF0) and commands (LoadCommands 004D1330) go into command blocks
from `CScript::NewCommandList` (004BF540; CList at CScript+0x70, block id from CScript+0x7C), and
the pair is appended to the definition's `ifThen` array. A command is one of: a keyword with a
command signature (For/ForIf are followed by `(` body `)`, parsed into another block stored in
SSCCmd+0x90, without the script's local variables); a declared script (command 8: float 0 = script
index, string 0 = name, the script's parameter types as signature; executing it starts an instance
of the script); or `global = value` (LoadAssignOperator 004D1D50: command 0x78, float 0 = variable
index, float 1 = type - 1, the value as the single parameter after them).

`LoadFunction` (004CFBC0) parses one call `(` param `,` param ... `)` against a signature string
(f float, s string, o object, g group, O loop counter: a global object variable). Each parameter is
a literal (a number for f, a quoted string for s), a local variable (binding into SSCCmd+0x54), a
global variable (plain or qualified name, binding into +0x68), or a nested call of a function with
the right result type (FunctionSignature 004D0B10), deep-copied into +0x7C. The argument arrays are
sized from the signature up front (the game's inlined CArray::SetSize).

## Data structures recovered

- **ScriptToken** (8): text CString, line. The "no token" value is "None", -1 (004CFB70).
- **ScriptTokenList**: CList<ScriptToken> (0x1C; nodes next, prev, token). A position is a node
  pointer; the parser passes `ScriptPos*` and consumes tokens through it.
- **ScriptDefineList**: CList of {name, value} CStrings (vtable 0073D4C0, block size 10).
- **ScriptDefinition** (0xC0; CArray at CScript+0x8C, data +0x90, size +0x94): +0 index (0),
  +4 qualified name, +8 parameter type letters ("fso"), +0x0C CArray of `ScriptIfThen`
  {conditions block, commands block}, +0x20 ScriptVars (parameters and locals).
- **ScriptDecl** (name from the evaluator work, kept): a command block, CArray<SSCCmd> and its id at
  +0x14 (what SSCCmd::Save writes for +0x90).
- **CScript**: +0x70 owned command blocks, +0x8C definitions, +0xA0 globals, +0x17C WorldScript
  blocks, +0x1A8 loaded flag, +0x1AC file counter, +0x1EC/+0x1F0/+0x1F4 error text/code/line.
- Keyword ids: ( 6D, ) 6E, if 6F, then 70, `,` 71, = 72, : 73, WorldScript 74, Script 75,
  DeclareScript 76, GlobalVars 77; numbers (7A) and strings (7B) are not in the table.

## Lexer (Tokenize, ClassifyToken)

- Lines start at 1 and count `\n` in white space and in block comments.
- A token runs to white space (a space inside quotes does not end it; a tab or newline does) or to
  one of `, ) ( : =` after its first character (not inside quotes); at most 255 characters are
  looked at.
- ClassifyToken (004CF600) keeps only the recognised prefix and the rest is scanned again: a number
  (digits, at most one '.', may start with '-' or '.'), a quoted string up to its closing quote, or
  the longest keyword the text starts with, case-insensitively. So `(abc` becomes `(` and `abc`,
  `1.5.5` becomes `1.5` and `.5`, and an identifier that starts with a keyword is split: `Run1` is
  `Run` and `1`, `ifx` is `if` and `x`.
- `/*` starts a comment only at the start of a token, and the search for `*/` starts after that
  token: `/**/2` (one token) does not close it. Quoted text inside the comment is skipped (`"*/"`
  does not close it). `//` comments run to the end of the line.

## Rewritten functions

| Address | Name | Verification |
|---|---|---|
| 004CF470 | CScript::Error (Rebirth patches it: vanilla only) | ours only (variadic): result 0, code and line stored |
| 004CF600 | ClassifyToken (Rebirth patches it: vanilla only) | pure, side by side (id, rest) |
| 004D0B10 | FunctionSignature (Rebirth patches it) | pure |
| 004D0BA0 | KeywordName (Rebirth patches it) | pure |
| 004CFB70 | ScriptToken::Construct | output: original into a scratch token |
| 004D0BD0 | ScriptToken::Assign | ours only: the copy equals the source |
| 004D1D30 | ScriptToken::CopyConstruct | ours only: the copy equals the source |
| 004CF380 | ConstructTokens (ConstructElements) | ours only: every token "None"/-1 |
| 004CF7A0 | CScript::Tokenize | output: original into a scratch list; tokens, lines, result, error state |
| 004CF4D0 | CScript::ExpectLexeme | original on a copy of the position; result, position, error state |
| 004D37F0 | CScript::QualifyName | output: scratch string |
| 004D3780 | CScript::FindScript | pure |
| 004D34F0 | AddDefine | ours only: count +1, the define is in the list, the walk matches the count |
| 004D33E0 | ApplyDefines | original on a copy of the line |
| 004D0A50 | SSCCmd::Construct | output: all 0x94 bytes of a scratch command |
| 004CFBC0 | CScript::LoadFunction (Rebirth patches it) | original into a fresh scratch SSCCmd (callers always clear the command first) on a copy of the position; deep tree comparison including capacities, position, result, error state |
| 004D0BF0 | CScript::LoadConditions | original into a scratch command array; deep comparison |
| 004D1D50 | CScript::LoadAssignOperator | as LoadFunction |
| 004D1FE0 | CScript::LoadVariableDeclarations | original into a fresh scratch definition (same name) when the definition is empty (always so in Load); parameter string and variable names compared |
| 004D1030 | CScript::LoadScriptBody | ours only (allocates command blocks): success has a pair, all filled, last token `)` |
| 004D1330 | CScript::LoadCommands (Rebirth patches it) | ours only: success ends on `)`, every command has an id |
| 004D23A0 | CScript::Load | ours only: success counts the file (and leaves no error unless a nested load ran), failure does not |
| 004D36F0 | CScript::LoadFile | ours only, as Load |

Calls between loader functions go through the original addresses: under Rebirth the patched ones
(Error, LoadFunction, LoadCommands, the keyword lookups) keep Rebirth's version, and in verify
builds every callee runs through its own wrapper. LoadFunction calls itself directly (through its
wrapper the tree would be parsed 2^depth times). The keyword table is read from ClassifyToken's code
(the immediate at 004CF69B, which Rebirth points at its extended table); the vanilla value is
0078BCA0. Only the Rebirth-patched rewrites use it.

In the side-by-side wrappers both runs call `CScript::Error` with the same arguments on an error;
the second call writes the same text, code and line again.

## Forwarders added (standalone build must replace)

- CScript: 004CF470 Error (called through its address: Rebirth patches it), 004BF540 NewCommandList, 004BF900 NewWorldScript,
  004BCC60 RunWorldScript (rewritten since, ScriptRunWorldScript); 004D5880 reads a text file (CStdioFile).
- SSCCmd: 004B5F70 Clear, 004BF640 destructor. ScriptDefinition: 004BB800 AddParameter,
  004D3320 Reset, 004BE9D0 destructor. ScriptVars: 004C00D0 constructor, 004BE090 destructor,
  004C0020 remove all, 004B3900 append another set; ResetValues/HasVariable/FindVariable are called
  through their addresses (004B35E0/004B36D0/004B37D0).
- CArray/CList template members: 004D5B50 (bindings Add), 004D5CB0 / 004C4FD0 (nested calls Add /
  SetSize), 004CD2B0 / 004D65E0 / 004D5ED0 (command arrays SetSize / SetAtGrow / Add), 004C59C0
  (if/then SetSize), 004D6100 (definitions Add), 004CA260 (token list destructor), 004D6340
  (define list destructor), 006FE268 / 006FE296 (CString Construct/DestructElements).
- CString members not in lib/mfc.h: 006FD4DE CString(LPCTSTR, int), 006FD9D7 Mid, 006FDAE9 Left,
  006FDA6D Right, 006FDBD9 Find, 00700E6A LockBuffer, 00700E7C UnlockBuffer (`script_parse.h`).
- CRT kept local: isdigit 006E9379, isspace 006E93CE, strncmp 006E9180, atof 006E911A, _mbscmp.

## Original bugs and oddities kept

- Keyword prefixes split identifiers (`Run1` is `Run` `1`); `1.5.5` is two numbers.
- `/**/` written as one token does not close the comment. A token longer than 255 characters loses
  the characters after the 255th that ClassifyToken did not keep.
- The parameter token after `(` / `,` is read without an end check (a script ending in `Foo(`
  dereferences a null position), as are the name after `Script`/`DeclareScript`, the token after
  `if` and `then`, and a declared variable's name after `,` and its type.
- A For/ForIf whose header fails still parses its `(` body: the reported error is then usually
  "Wrong lexeme ," instead of the header's error.
- For/ForIf bodies are parsed without the script's local variables.
- LoadVariableDeclarations returns 0 without an error when the tokens run out (Load then fails
  with "No errors found").
- LoadScriptBody leaves the command blocks of a failed pair in the CScript's list.
- AddDefine inserts before the first shorter name, else before the last define (not at the end).
  A define whose value contains its name never finishes expanding.
- "In script %s should be at least one condtion" (sic) prints the qualified name.

## Skipped

- 004D3320 (definition reset) and the CArray/CList template members listed above.
- 004D38F0 / 004D38B0 (strip the `!n!` prefix) and the script dumper 004D39D0, 004D41C0
  (Rebirth patches it), 004D4E80, 004D50C0, 004D5310, 004D53C0 (writes definitions back as text).
- 004D5880 (reads a file through CStdioFile, MFC exceptions).
- 004BFCC0, 004C01A0, 004C06F0, 004C09F0: build quest scripts from generated text with the same
  parser functions ("Symbols behind conditions list" etc.); not rewritten.
- 004C0000-004CF000: mostly CArray/CList template instantiations of the script types.

# Command execution and runtime (004B6760, 004BB520-004BF000)

Code: `src/game/script_exec.cpp` (ScriptRun::Execute, the command switch),
`src/game/script_exec_run.cpp` (instances, if-then pairs, runs, temporaries, the tick); types in
`script.h` (ScriptRun, ScriptCommandObserver, the runtime entry points). Verify wrappers:
`tests/verify/verify_script_exec.cpp`. In-game scenario: `tests/scenarios/script_exec.txt`.

## How scripts run

The world calls `ScriptTick` (004BF000, "CommandQueue::Run") every world update for its two
CScripts (world +0x12874, and +0x12A6C = 007C29CC, the mission script the console uses). A tick,
when a script is loaded (+0x1A8):

1. Every waiting **run** (CList<ScriptRun> at +0x15C, nodes 0xBC) is continued with
   `ScriptRun::Execute`; a run that returns 0 (it had already reached its end) is removed.
2. Every **script instance** (CList at +0x140, nodes 0xC8: a copy of the ScriptDefinition whose
   +0 is the "killed" flag) drops deleted objects from its variables, then runs its if-then pairs
   in order (`ScriptIfThenRun`) until one kills it; killed instances are removed.
3. Global object variables and groups drop deleted objects.
4. Timed effects: FX sources (+0x30) respawn their effect when their countdown runs out (new
   countdown `Next() % (max - min + 1) + min`, new effect object of class 0x48 at a random offset
   within the source's radius); point lights (+0x04, 256 slots of 0x20) move for their remaining
   steps, then stop (and are removed when MovePointLight asked for it); particle sources (+0x14)
   move, count down their lifetime and are removed at 0 (or after the move when flagged).
5. `ScriptFreeTemporaries`.

`ScriptIfThenRun` (004BB520) evaluates the conditions with CalcFloat in the instance's variables,
freeing the temporaries after each; the first zero (or NaN) condition ends it. When all hold,
`ScriptRunCommands` (004BC420) runs the commands block once in a new ScriptRun (index 0, a copy of
the instance's variable values, kill flag = the instance); if that Execute returns 1 (it waits, or
ran to the end) a copy goes to the head of the run list. The WorldScript (and every console `@`
line) is added to the run list by `ScriptRunWorldScript` (004BCC60) and first runs on the
next tick (the console runs one tick right after loading).

`ScriptRun::Execute` (004B6760): counts the call (+8); returns 0 if the block had already ended;
otherwise drops deleted objects from the run's variables and runs commands: bind local and global
variables (SetParameters), evaluate the nested calls (CalcParameters), run the command, free the
temporaries, advance. A waiting command leaves the index on itself and Execute returns 1:

- Sleep(n): the first time it records the tick count (+0xC); waits while `ticks - start < n`
  (FISTP), then resets +0xC to -1. So it waits n further Execute calls, i.e. world updates.
- WaitSegment(n): waits while the run has been executed fewer than n times in total.
- SleepUntil(c): while c is 0 or NaN. SleepUntilIdle(unit): while the unit's brain has an action
  (+0x240 object +0x984).

For(counter, group)(body) / ForIf(counter, group, condition)(body) walk the group from the last
member to the first (size read once), store the member in the loop counter (a global object
variable), and run the body block (SSCCmd +0x90) in the same ScriptRun: block, index, ticks and
sleep start are saved, set to body/0/0/-1, Execute is called recursively, and restored (a Sleep in
the body therefore ends the body for that member). ForIf evaluates its first float nested call
(the condition) for each member and skips the member when argument float 0 is zero or NaN.
CScript +0x1E8 counts the For nesting; FreeTemporaries does nothing while it is positive.

A script call (command 8) adds an instance of definition `float 0` at the head of the list
(004C7C90: an AddHead, see "X10" below),
`ScriptBindCall` (004BB6A0) copies the arguments after the first float and string into its
variables (FatalError "Wrong number of script %s float parameters" etc. when a count differs), and
the instance's killed flag is cleared. The instance loop walks from the head, so an instance
started by a waiting run (tick step 1) runs its pairs in the same tick, one started by another
instance's commands (step 2) from the next tick on.

Assignment (command 0x78): float 1 = type - 1 selects floats (copied as bits), strings (CString
assignment), objects (reference) or groups (CArray Copy 004C1290); float 0 = variable index.

## Data structures recovered

- **ScriptRun** (0xB4): block (ScriptDecl*), index, ticks, sleep start, kill flag (int*, the
  instance; null for the WorldScript), ScriptVars copy at +0x14.
- **CScript** runtime fields: +0x04 point lights (CArray, 256 slots of 0x20 set up by the
  constructor 004BDE40: object, id, remove-when-done, active, steps left, step dx dy dz),
  +0x14 particle sources (CList; node +0x08 source, +0x0C id, +0x10 delete after move, +0x14
  moving, +0x18 steps, +0x1C step, +0x28 lifetime), +0x30 FX sources (CList; node +0x08 effect,
  +0x0C id, +0x10 name, +0x14 x y z, +0x20 / +0x24 copied to the effect's +0x24C / +0x250, +0x28
  disabled, +0x2C radius, +0x30 / +0x34 min / max delay, +0x38 countdown), +0x4C lightning (CList;
  node +0x0C id), +0x140 instances, +0x15C runs, +0x178 WorldScript blocks, +0x194 areas,
  +0x1B0 / +0x1CC temporary groups / object references (CLists that always keep one entry; the
  constructor adds it), +0x1E8 For depth.
- Units: +0x240 server state (its +0x980 is the AI brain, +0x1E0 the skills/stats, +0x1C the
  owner object), +0x244 hostile mask, +0x250 side, +0x4A0 / +0x4A4 quest items. flags_ bits:
  0x800000 blocked (IsUnitBlocked), 0x10000 hidden, 0x80000 trap active (units: "changed").
- Players: +0x20 quest variable / music / zone object, +0x70 and +0x9C party data, +0xC0 money,
  +0x108 god mode byte.

## Rewritten functions

| Address | Name | Verification |
|---|---|---|
| 004B6760 | ScriptRun::Execute (Rebirth patches it: vanilla only) | ours only. Run: ticks +1; block, kill flag and For depth unchanged; result 0 only when the run was already at its end; otherwise it stops only at the end or on a waiting command. Per command (observer, after each command, before the temporaries are freed): the run's position and tick count unchanged, sleep start unchanged except by Sleep, only waiting commands stop, For depth restored; plus the effect: Sleep/WaitSegment/SleepUntil/SleepUntilIdle stop exactly when they should and Sleep's start is kept/reset; KillScript sets the flag; a script call adds a live instance with the call's floats; assignments hold the value (float bits, string, object, group members); AddObject leaves the group sorted and containing the object, RemoveObject without it; BlockUnit/HideObject/ActivateTrap flag bits; SetPlayer side (+0x250); SetEnemy bit in the hostile mask; GodMode byte |
| 004BB6A0 | ScriptBindCall | ours only: when the counts match, every parameter equals its argument |
| 004BB520 | ScriptIfThenRun | ours only: temporaries freed, run list well formed |
| 004BC420 | ScriptRunCommands | ours only: run list grew by at most one; a new run has the block, the kill flag, ticks 1, a waiting command at its index (or the block's end), and the instance's variable counts |
| 004BCC60 | ScriptRunWorldScript (CScript::RunWorldScript) | ours only: one more run, at the head, at the block's start (ticks 0, sleep -1, no kill flag, empty variables) |
| 004BEC80 | ScriptFreeTemporaries | ours only: inside For nothing changes, else one empty group and one empty reference left; lists well formed |
| 004BF000 | ScriptTick | ours only: run, instance and particle lists well formed, no killed instance left, no deleted object in the globals, temporaries freed |
| 004B1ED0 | ScriptAreaAddRect (`script_exec_helpers.cpp`) | output: original appends to a copy of the parts array; sizes, capacity and parts compared |
| 004B2070 | ScriptAreaAddRound | as AddRect (the circle's unused fourth value is left out: the original leaves stack garbage there, ours 0) |
| 004BCF30 | ScriptDeleteArea | ours only: the areas without the id are left, in order, with their parts |
| 004BD6A0 | ScriptCreatePointLight | ours only: the slot with the id holds the new object at the point with the radius, not moving |
| 004BD820 | ScriptMovePointLight | output: original on a copy of the light array (a stand-in script with only +0x04/+0x08); the arrays compared bytewise (the step is computed with the original's mixed precision: the x difference stays in the FPU) |
| 004BD920 | ScriptDeletePointLight | ours only: the first light with the id has no object |

Every callee is called through its original address (`EI_ORIGINAL` forwarders, named in the .cpp
files): CalcParameters, CalcFloat, FatalError, 005935C0 and Execute itself are patched by Rebirth,
and the rewritten runtime functions call each other through their original addresses too, so each
runs through its verify wrapper in the verify build. For bodies recurse through 004B6760.

## Forwarders added (standalone build must replace)

All in `script_exec.cpp` / `script_exec_run.cpp`:
- Script: 004C7C90 instance AddHead, 004CDAB0 / 004CD9B0 run / instance FreeNode, 004CECB0 /
  004CB570 ScriptRun ConstructElements / operator=, 004C00D0 / 004BE090 ScriptVars, 004C1A80 /
  004C14A0 / 004C1290 / 004C1BE0 value array Copy, 004C2200 group RemoveAt, 004C2050 SetSize,
  004CC7D0 reference DestructElements, 004C8AF0 / 004C9100 temporaries RemoveTail, 004C6040 /
  004C6370 / 004C6680 particle / FX / lightning RemoveAt.
- Command helpers not rewritten: 004BD9A0 CreateParticleSource, 004BDBB0 MoveParticleSource,
  004BCFF0 CreateLightning, 004BD260 CreateFX, 004BD310 CreateFXSource, 004BAA40 QStart, 004BAAB0 QFinish (builds a script text and
  loads it), 004BAE10 quest objective, 004BAEC0 party records of a "party name", unit mode
  constructors 004BA1C0 / 004BA360 / 004BA450 / 004BA610 / 004BA710 / 004BA830 / 004BA920 /
  005D0F60 / 005D2850 / 005D3760, 004BA1A0 patrol look, 004BA9D0 console float, 004BA120
  background colour, 004BA0E0 / 004BA100 / 004BA0A0 / 004BA0C0 / 004BA090 AI map sector helpers.
- World (007AFF60): 00594180 GetPlayer, 00594220 GridHeight, 00593FC0 InvokeAlarm, 005910A0 add
  unit, 005935C0 remove object, 00591800, 0058EE20 wind, 0058EEE0 / 0058EEA0 time speed / time,
  00593FA0 water level, 00595AB0 AddMob, 00594AB0 / 00594BC0 item / spell by name, 00590A40 /
  00590700 redeploy, 005911C0 award kill, 0058E850 cast spell, 0058E5D0 / 0058E520 timed objects;
  the object factory at 007AFFA8 (vtable +0x5C).
- AI brain 005D7AE0 Walk, 005D7B30 TurnTowards, 005D7E40 Follow, 005D7B50 AttackTarget,
  005D7DA0 CastAt, 005D7C30, 005D4DF0 clear modes, 005D7790 StopAction, 005D6230 Guard, 005D64E0
  Sentry, 005D5F60 add mode, 005D5E30 patrol mode, 005D6140 remove mode; 005D12B0 / 005D1690
  patrol points; 004C9970 / 004CDCB0 their arrays. Unit server 00555700 / 00555270 / 00555200 /
  00554450. Units 0052E970 / 00530120 / 0052E940 / 005300D0 / 00531220 / 005313B0 / 00531820 /
  0050E9F0; levers 0052B7A0 / 0052BAD0 / 0052BB20; skills 00528AB0 / 00528DA0 / 00529090 /
  00529220 / 00529400; 00522B40 damage info.
- Players 00665470 / 00665780, player state 0066EE10 / 0066EE80 / 0066EF30 / 0066EB90 /
  0066F310 / 0066EF80; party records 006621B0 / 006628D0 / 006620F0 / 00660500; parties
  (007AA5A0 +0x24) 004A5E10 / 004A5E90 / 004A5FA0, party 00661DB0 / 00661F30, party manager
  004A8890 / 004A8880 / 004A88E0; diplomacy 004554E0 / 00455660; network 0057F440 / 0057FB90;
  game server 00419730 / 004197D0 / 004198D0 with STLport strings 004217E0 / 00428F10 / 00425040
  / 0040C5A0 / 0040C720 / 0041F770 / 0040C760; events (007AFF98) 0066C340 / 0066C3C0; spells
  00689030; particles 00679280 / 006792B0; console ostream 006FC31D / 00434FE0 (endl 004BA9F0);
  camera 00578820, movies 00578DC0 / 005DB3F0, renderer 00500960; quest table 004C9C90;
  lists 004C96A0 / 004C96F0, 004C98C0 / 004C98F0 / 00427830, 004BB000 / 004BB020; object
  iterator 00404920 / 00404960; 00675CA0 effect moved; 004667F0 random; 00461360 name key.
- The small accessors 004B9F20-004BAA30 that the original calls out of line are inlined (field
  reads and flag writes).

## Original bugs and oddities kept

- SetSpellAggression (0x42) only reads the unit's +0x170 and does nothing.
- KillScript in the WorldScript (no instance) reports a FatalError and then writes through a null
  pointer.
- A run keeps a pointer to its instance's killed flag after the instance is removed (a KillScript
  after a Sleep writes into freed memory). The scenario's script kills itself before it sleeps.
- SleepUntilIdle dereferences the unit's +0x240 object without a null check.
- ResetTarget (0xE8) on a unit without a current target can crash the original (005D6630 reads
  through a null pointer, seen on a freshly spawned wolf after Follow/Idle); the scenario does not
  use it.
- InflictDamage calls the unit state's TakeHit (vtable +0x50) with three arguments (hit, 0, 0); the
  original pushes the first 0 before building the hit, which is easy to miss (a two-argument call
  unbalanced the stack and crashed the first in-game run).
- The mode commands write the reference count (+4) of the new mode before testing operator new's
  result.
- Sleep counts Execute calls, so a Sleep inside a For body ends the body for that member and the
  loop goes on; WaitSegment counts from the run's start (in a For body: from that member's start).
- SetDiplomacy maps a NaN value to neutral (FCOM C3), as the flag commands treat NaN as zero.
- The ForIf condition is evaluated before every member; a For loop counter is a global, so nested
  loops on the same counter overwrite it.
- CreatePointLight stops at the first free slot, so a light with the same id after a free slot is
  not replaced (two lights share the id); with all 256 slots used and no match it writes one slot
  past the array. The replaced light's object is released but its slot is reused at once.
- DeleteArea leaves the moved-out last entry in memory (the size shrinks; MFC style).
- The tick saves each node's successor before running it, so instances added by script calls
  during the instance loop run in the same tick only when added after the current node.

## Skipped

- The command helpers listed under forwarders (particles, lightning, FX, quests, party records):
  self-contained, still original code. Execute calls the area and point light helpers through
  their original addresses as well (they are rewritten in `script_exec_helpers.cpp`; 004BCF10, the
  area destructor, and 006E79A0 memmove are forwarders there).
- 004BB800 AddParameter, an instance-run loop in an undefined region
  around 004BBC60 (calls 004BC420), 004BED90 / 004BEB00 / 004BE6E0 (CScript reset and
  destruction), 004BDE40 CScript constructor.
- The multiplayer branches of GSSetVar, GSSetVarMax, GSDelVar and QuestComplete are rewritten
  but only reachable in multiplayer (not exercised by the scenario).

# Savegame writer (004C0F90 and its parts)

Code: `src/game/script_save.cpp`; types (`ScriptNamedValues`, `ScriptArea`) in `script.h`. Verify:
`tests/verify/verify_script_save.cpp` (both versions write into capture streams; bytes and the
size of every Write must match; the original's pieces then go to the real stream). Reached by the
autosave of every new game (world_load also loads it back).

| Address | Size | Name |
|---|---|---|
| 004C0F90 | 657 | ScriptSave (CScript::Save) |
| 004C1230 | 94 | ScriptDeclSave (block id, count, SSCCmd::Save each) |
| 004C2EF0 | ~130 | ScriptFloatsSave |
| 004C33C0 | ~140 | ScriptStringsSave |
| 004C38C0 | ~160 | ScriptObjectsSave |
| 004C4330 | ~220 | ScriptGroupsSave |
| 004CAEC0 | ~170 | ScriptAreasSave (__fastcall) |

CScript::Save order: point lights (the CArray at +0x00, data +0x04, size +0x08; 004B1940 each), particle / FX / lightning lists (+0x14,
+0x30, +0x4C; 004B1A80 / 004B1C50 / 004B1DB0), +0x68, +0x6C, command blocks (+0x70) and WorldScript
blocks (+0x178), definitions (+0x8C, 004BBFE0), globals (+0xA0, four savers), instances (+0x140,
004BBFE0), runs (+0x15C, 004BB320, which finds the run's instance through 007AA620), +0x1A8,
+0x1AC, then 007AA620 = 0 and the areas. Lists are written as count, then each node (the next
pointer read before the call). A variable's name is written with its terminator, or as a single
zero byte when the name array is shorter than the value array.

Forwarders: 004B1940, 004B1A80, 004B1C50, 004B1DB0, 004BBFE0, 004BB320, SSCCmd::Save 004B6360;
the savers of this file call each other through their addresses (verify wrappers run).

Skipped at first (rewritten since, see "Savegame reader"): 004C0D00 (CScript::Load, 654) and its loaders (004C5CB0, 004CA480 / 004CA5D0 / 004CA720,
004C57C0, 004CA810, 004CD770, 004CA000, 004C2D40 / 004C3200 / 004C36B0 / 004C40D0, 004CA980,
004CAC00, 004CDB30, 004CC640): mapped, not rewritten (a save/load round trip does not reproduce the
bytes when objects are missing, so they need record/replay verification). Areas are read only for
stream versions >= 0x57. 004C01A0, 004C0520, 004C06F0, 004C09F0 (quest script builders): no
references in the exe (dead). The rest of 004C0000-004CF000 is CArray / CList template code.

# Savegame reader (004C0D00 and its parts)

Code: `src/game/script_load.cpp` (the loaders), `script_array.h` (the inlined `CArray::SetSize`
shared with the variable appends), declarations in `script.h`. Verify:
`tests/verify/verify_script_load.cpp`. Reached by every save load (`world_load.txt`): the world
(0059749D) loads only its first script, 007C27D4 (world +0x12874).

## How a script is read

`ScriptLoad` (CScript::Load) mirrors ScriptSave: 007AA620 = the script; point lights (count, the
array's out-of-line SetSize 004C5CB0, then 004B18B0 per 0x20-byte slot), particle / FX / lightning
lists, +0x68, +0x6C, the command blocks (the count, that many `NewCommandList` 004BF540 calls,
then for every block of the +0x70 list its id and `ScriptDeclCommandsLoad`), the WorldScript
blocks, the definitions (count, SetSize 004CD770, then per definition +0 (4 bytes), name and
parameter string through 004B3D10, `ScriptIfThenLoad`, `ScriptVars::Load` 004B3AD0), the globals
(the four variable loaders), instances, runs, +0x1A8, +0x1AC; 007AA620 = 0; then, from stream
version (+8) 0x57 on, the areas (count, SetSize 004CDB30, per area its id, part count, SetSize
004CC640 and 0x14 bytes per part).

The list loaders read the count, append that many default nodes (the list's NewNode, element
copied from a stack temporary, linked at the tail), then read every node of the list from the
head, so a list that was not empty would have its old nodes read again (never the case: the world
is new). Particle sources: object id (looked up in the registry at *007AFFA8 (00403F40), reference
count raised; -1 = none), then +0x0C, +0x10, +0x14, +0x18, 12 bytes, +0x28. Lightning: object id,
+0x0C. FX sources: 004B1B40 per node. WorldScript blocks: id, count, the command array's SetSize
004CD2B0, SSCCmd::Load 004B5FE0 each. Instances: +0 (killed flag), name and parameters (004B3D10),
if-then pair count, SetSize 004C59C0, 004BB590 per pair, then the four variable loaders. Runs: block
id, index, ticks, sleep start, the instance index (the n-th node of the loading script's (007AA620)
instance list, 0 when out of range), ScriptVars::Load.

Block ids resolve in the lists of 007C27D4, not of the script being loaded (`FindBlock`): negative
ids in the WorldScript blocks (+0x178), others in the command blocks (+0x70); the block's id is its
+0x14. Unknown ids give null.

Variables: the count, the values resized (inlined SetSize with the instantiation's construct /
destruct helpers), the names resized to the count (004C2510), then per value index its value and
its name (004B3D10): float bits; a string (004B3D10); an object id looked up with the world's
FindObject (00593AA0) after which the slot's old reference is released and the object referenced;
a group's size (ScriptGroup::SetSize 004C2050), each member id assigned in place (Assign 004B24D0,
missing objects stay null), then Sort (004B2D60).

## Rewritten functions

| Address | Size | Name | Verification |
|---|---|---|---|
| 004C0D00 | 654 | ScriptLoad (CScript::Load) | ours only: 007AA620 cleared, the seven lists well formed (count matches the walk, prev links); every part runs through its own wrapper |
| 004CAC00 | 693 | ScriptRunsLoad | record / replay on an empty scratch list; per node block, index, ticks, sleep start, kill flag compared and both runs saved with 004BB320 (bytes compared) |
| 004CA980 | 638 | ScriptInstancesLoad | record / replay; nodes compared through the definition saver 004BBFE0 and the pair array's size and capacity |
| 004CA000 | 595 | ScriptIfThenLoad | record / replay on an empty scratch array; size, capacity and pair pointers |
| 004C40D0 | 603 | ScriptGroupsLoad | record / replay on empty scratch arrays; sizes, capacities, every group's members, size, capacity, growBy; names |
| 004C36B0 | 523 | ScriptObjectsLoad | as groups (object pointers) |
| 004C57C0 | 476 | ScriptDeclCommandsLoad | record / replay on an empty scratch block; size, capacity and both saved with 004C1230 |
| 004C3200 | 443 | ScriptStringsLoad | as groups (string contents) |
| 004C2D40 | 424 | ScriptFloatsLoad | as groups (float bits) |
| 004CA480 | 321 | ScriptParticlesLoad | record / replay on an empty scratch list; the 0x24-byte elements |
| 004CA5D0 | 330 | ScriptFxSourcesLoad | record / replay; object, id, name, the other 0x28 bytes |
| 004CA720 | 228 | ScriptLightningLoad | record / replay; object, id |
| 004CA810 | 368 | ScriptWorldBlocksLoad | record / replay; blocks compared through 004C1230 |

Record / replay: the original reads the real stream into a fresh empty scratch object through a
recording stream (each Read's size, result and bytes; the version at +8 copied), ours replays the
recording into the real object; the reads must match in order and size and all be consumed. The
scratch is then destroyed with the game's own helpers (SetSize(0), DestructElements, the element
destructors 004BE9D0 / 004BE090, references released, 006FE90F for list blocks). When the real
object is not empty on entry the wrapper runs ours alone and checks only the shape. Nested loaders
run through their wrappers in both runs (the stream they get is the recording or replaying one).

## Forwarders added (standalone build must replace)

In `script_load.cpp`: 004B18B0 / 004B1B40 point light / FX source loaders, 004B3AD0
ScriptVars::Load, 004B5FE0 SSCCmd::Load, 004BB590 instance pair loader, 004B3D10 string loader,
004BF540 NewCommandList, 00403F40 registry lookup, 00593AA0 FindObject; the rewritten 004B24B0 /
004B24D0 / 004B2D60 / 004C2050 through their addresses; CArray members 004C2510, 004C5CB0,
004CD770, 004CDB30, 004CC640, 004CD2B0, 004C59C0, copies 004CC0C0 / 004CC4A0 / 004CBAE0, ScriptVars
constructor / destructor 004C00D0 / 004BE090, element helpers 004CC870, 006FE268, 006FE296,
004CC800, 004CC7D0, 004CC9B0, 004CC980, 004CE920, 004CD4A0, 004CE1E0, 004CE210, 004BF620; list
NewNode 004CD540 / 004CD5B0 / 004CD620 / 004CD6D0 / 004CD930 / 004CDA30. The loaders of this file
call each other through their addresses.

## Original behaviour kept

- Block ids are resolved in the first world script's lists (007C27D4) whatever script is loaded.
- The first block id of an if-then pair reads into a variable preset to -1, the second into the same
  variable without a preset.
- List loaders re-read nodes that were in the list before the load.
- Instance names are read with 00461190 into a temporary and assigned (004B3D10 does exactly that).

# Variable appends and clears (004C2B70-004C3DC0, 004C0020, 004C1650-004C1760)

Code: `src/game/script_vars_append.cpp`. Verify: `tests/verify/verify_script_vars_append.cpp`.

`ScriptVars::AppendFrom` (004B3900, the loader's GlobalVars) calls one append per type: the values
grow by the other set's value count (inlined SetSize, `script_array.h`), the names to the old size
plus the other set's name count (004C2510); then for each of the other set's names, its name
(GetAt 004C2680 into a temporary, assigned) and the value of the same index go into the new slots:
floats copied through the FPU (FLD / FSTP: a signalling NaN becomes quiet), strings by GetAt and
assignment, objects by GetAt (a referenced copy; the new slot's null object released, the object
referenced again, the copy released), groups by GetAt (a copy), the new group sized (004C2050) and
each member copied in place (Reset 004B24B0 then Construct 004B2500), the copy destroyed.

The clears (CScript reset 004BE6E0, the loader's `VarsAt::Clear`) free one type's values (strings
destructed, references reset, groups destroyed with 004B5EF0) and empty the names; 004C0020 does the
whole set, through the out-of-line SetSize(0) for every array but the groups.

| Address | Size | Name | Verification |
|---|---|---|---|
| 004C3DC0 | 782 | ScriptGroupsAppend | ours only: the other set captured first (it may be the same set); sizes, names, every new group's members |
| 004C3450 | 607 | ScriptObjectsAppend | ours only: as groups, object pointers |
| 004C2FE0 | 532 | ScriptStringsAppend | ours only: as groups, string contents |
| 004C2B70 | 451 | ScriptFloatsAppend | ours only: as groups, float bits after FLD / FSTP |
| 004C0020 | 174 | ScriptVarsClear | ours only: all eight arrays empty without storage |
| 004C1760 / 004C1700 | 87 / 87 | ScriptGroupsClear / ScriptObjectsClear | ours only: both arrays empty |
| 004C16B0 / 004C1650 | 67 / 54 | ScriptStringsClear / ScriptFloatsClear | ours only: both arrays empty |

Original quirk kept: the copy loop runs over the other set's names, the values grow by its values
(they are always equal in practice).

# Assignment operators (004C6BC0, 004CB220, 004CB2D0, 004CB570, 004CBAE0)

Code: `src/game/script_copy.cpp`. Verify: `tests/verify/verify_script_copy.cpp`. The compiler wrote
them member by member through the CArray instantiations; all return `this`.

| Address | Size | Name | Verification |
|---|---|---|---|
| 004C6BC0 | 324 | ScriptCmdAssign (SSCCmd::operator=) | ours only: the source captured first (its SSCCmd::Save bytes, float bits after FLD / FSTP, strings, pointer arguments, bindings, called script), the destination must match |
| 004CB570 | 181 | ScriptRunAssign | ours only: the five words and the variables (savers' bytes plus object and member pointers) |
| 004CBAE0 | 149 | ScriptVarsAssign | ours only: as the run's variables |
| 004CB220 | 101 | ScriptFxSourceAssign | ours only: the words and the name |
| 004CB2D0 | 27 | ScriptDeclAssign | ours only: 004C1230's bytes before (source) and after (destination) |

SSCCmd::operator=: id; float arguments resized (004C4410) and copied through the FPU; strings
Copy (004C14A0); object and group argument pointers and both binding arrays resized (004C46F0,
004C49F0, 004C4CE0) and copied as words; nested calls Copy (004C6D10, deep); the called script
pointer. ScriptVars::operator=: floats resized (004C2880) and copied through the FPU, names and
strings Copy (004C14A0), objects Copy (004C1290), groups Copy (004C1BE0). The run's operator= copies
its five words first and inlines the variables'. Forwarders: the SetSize / Copy members above,
004CC0C0, 00700AA9.

# Text manager line reader (004DEFC0-004DFFFF)

Code: `src/game/text_reader.cpp/.h` (on `CTextManager`, `item_db_text.h`). Verify:
`tests/verify/verify_text_reader.cpp`. Used by the interface (54 callers of Open), the map database
loader (FindLine / Split) and the item texts (ReadLine / ReadToHash); reached by every new game.

The manager's cursor (+0x20) walks the current text (+0x24, +0x28 bytes); every end test is
`(p - text) < length`, signed. Characters are MBCS: the code is (signed lead << 8) + signed trail
when `_ismbblead` (on the unsigned byte) says so, else the signed byte; a character is appended to a
string as one or two bytes by `isleadbyte` on the signed byte (through a temporary CString).

- Open (004DEFC0): clears the current text, asks each archive (vtable slot 5, GetFile(key, &length))
  in list order; cursor = text = the first hit; true when found and length != 0.
- PeekWord (004DF770): out = the first word at the cursor (spaces and LFs before it skipped; it
  ends at a space or LF after it started, or at CR or TAB at any time); the cursor does not move.
- SkipLine (004DF6E0): past the next LF; 0 and the cursor at the end when there is none.
- NextLine (004DF510): past the current line, then past every line whose first word starts "##".
- ReadLine (004DF8D0): out = up to CR / LF; with `advance` as NextLine (the first move's end test
  through IsPastEnd 004E0000). ReadToHash (004DFB80): out = up to '#'; `advance` puts the cursor
  there. Split (004DFCB0): the line's words (space / tab separated) through SetAtGrow; a word still
  open when the text ends is dropped; `advance` as ReadLine.
- FindLine (004DF030, the map loader's "NextLine"): key null = the next line whose first word starts
  with "#" but not "##"; a one-word key = the next line whose first word equals it (comment lines
  skipped while moving); several words (004DBE40 splits the key at spaces) = the next line (Split)
  whose first words, each lower-cased (CString::MakeLower 00700EFE), equal the key's words (the key
  is not lower-cased). Returns 1 when found, else 0 with the cursor at the end.

The original inlines the "move past the next LF" loop in five variants (end tests inline or through
IsPastEnd, character codes inline or through 004DF4E0); `Advance` takes the variant, so the same
calls happen.

| Address | Size | Name | Verification |
|---|---|---|---|
| 004DF030 | 1186 | TextFindLine | original first, state recorded and restored, then ours; result and cursor compared |
| 004DFCB0 | 848 | TextSplit | as FindLine, the original into a scratch array; words compared |
| 004DF8D0 | 674 | TextReadLine | as FindLine, the original into a scratch copy of the output; string compared |
| 004DF510 | 450 | TextNextLine | as FindLine |
| 004DF770 | 349 | TextPeekWord | as ReadLine |
| 004DFB80 | 302 | TextReadToHash | as ReadLine |
| 004DF6E0 | 139 | TextSkipLine | as FindLine |
| 004DEFC0 | 95 | TextOpen | as FindLine (GetFile runs twice; the archive raises the file's reference count twice, as in `verify_item_db.cpp`) |
| 004DF4E0 | 38 | TextCharCode | pure |
| 004DF020 | 14 | TextClose | ours only: text and cursor cleared, result 1 |

Forwarders: CRT 006E99F2 `_ismbblead`, 006E9A6C `isleadbyte`, 006E9947 `_mbsinc`, 006E848B
`_mbscmp`; CString 006FD559 `operator=(TCHAR)`, 00700D73 `operator+=(TCHAR)`, 006FDAE9 `Left`;
004DBE40 text split; the reader functions themselves and 004E0000 / 004DF4E0 where the original
calls them. Not rewritten here: 004DEC40 (Rebirth patches it), the archive list helpers 004DEB60 /
004DED40 / 004DEDE0 (release every archive and free the nodes), 004DEBC0 (list find, also inlined
by Open), 004DECE0 (no callers), 004DED20 (locale setup), the thunks 004DEDB0 / 004DEDD0 / 004DEFA0.

# 004C0000-004E0000: what is left

- Named in `config/library.csv` (237 functions): the CArray / CList instantiations of the script
  types between 004C1290 and 004CEE80 (vtables 0073D110-0073D4C0: constructors, destructors,
  scalar deleting destructors, Serialize, SetSize, Copy, GetAt helpers, RemoveAt, RemoveHead /
  RemoveTail, AddHead / AddTail, GetNext, NewNode / FreeNode, Construct / DestructElements) and the
  quest table's STLport hash_map members (004CE110, 004CE460, 004CEF00, 004CF0F0, 004CF160).
  Each "subclass" vtable is the derived class the game declares over a CArray (its destructor
  falls back to the base vtable).
- Dead code: 004C01A0, 004C0520, 004C06F0, 004C09F0 (quest script builders) and the script dumper
  004D53C0 -> 004D39D0 / 004D50C0 -> 004D41C0 (Rebirth patches it) / 004D4E80 / 004D5310, plus
  004D38B0: no code or data reference in the exe.
- Game code not rewritten: compiler-generated copy members of the script structs (ScriptDefinition
  copy 004CB460 and the float / string named-value copy constructors 004CBB80 / 004CBCA0; the
  others are rewritten since, see "Copy constructors" below; all only reached from CList
  RemoveHead / GetAt instantiations or dead code), element destructors
  (004CE1E0, 004CE210, 004CE230, 004CE270, 004CE2B0, 004CE2D0, 004C5480, 004C17E0, 004C81B0,
  004C9AF0), the ScriptVars constructor 004C00D0, the quest table lookup 004C9C90 and 004CC600
  (called from 006B1C90).
- 004D8000-004DEFFF belongs to other subsystems (renderer and AI map helpers, the text manager's
  archive list, STLport instantiations); apart from the line reader above, not touched.

# Copy constructors (ticket S1: 004CB7E0, 004CBA00, 004CBDB0, 004CBEF0, 004CE750, 004CCAA0)

What was left of the script runtime in 0x004C0000-0x004D8000 that is live, not library, not dead
and not Rebirth-patched is small: the largest such functions were these compiler-generated copy
constructors (the 004D6E90-004D7BD0 functions above 400 bytes are geometry/clipping code, not
script; 004D41C0 is the dead, Rebirth-patched dumper). Code: `src/game/script_copy.cpp`
(declarations in `script.h`). Verify: `tests/verify/verify_script_copy.cpp`. Scenario:
`tests/scenarios/script_copy.txt` (a live instance with all four parameter types and a sleeping
run, then `load savea`; script_parse / script_exec / console reach the two GetAt bodies while
parsing, world_load reaches the reset ones when the zone's mission script has instances or runs).

Each array is built as the original does: the base constructor (fields 0, base vtable; written
inline, as only the derived vtable stays), the derived vtable, the instantiation's out-of-line
SetSize to the source's size, then an element loop: floats through the FPU (the existing FLD /
FSTP helper; a signalling NaN becomes quiet), strings by GetAt (004C2680) into a temporary and
CString assignment, references by GetAt (004C21E0) and Reset / Construct (004B24B0 / 004B2500) or
the same written inline where the original inlines it, groups by GetAt (004C3B00).

| Address | Size | Name | Verification |
|---|---|---|---|
| 004CB7E0 | 544 | ScriptVarsCopyConstruct (reached from the instance list's RemoveHead 004C7A10 via the definition copy) | ours only: source captured as in ScriptVarsAssign (savers' bytes, float bits after FLD / FSTP, object and member pointers); the copy must match; the eight arrays' vtables |
| 004CCAA0 | 528 | ScriptSubCallCopyAt (body of CArray<ScriptSubCall>::GetAt 004C7510: type, arg index, the command deep-copied into operator new(0x94); null when new fails) | ours only: type and index equal, a new command whose SSCCmd image (Save bytes, floats, strings, pointer args, bindings, called script) equals the source's; array vtables |
| 004CE750 | 464 | ScriptCmdCopyAt (body of CArray<SSCCmd>::GetAt 004CD450) | ours only: as the nested call's command |
| 004CBEF0 | 464 | ScriptGroupsCopyConstruct (group values and names; run list RemoveHead 004C82B0) | ours only: every group's members, names, vtables |
| 004CBDB0 | 320 | ScriptObjectsCopyConstruct (object values and names; run list RemoveHead) | ours only: objects, names, vtables |
| 004CBA00 | 192 | ScriptNamesCopyConstruct (CStringArray; storage for exactly the source's size, ConstructElements 006FE268) | ours only: names, size == capacity, growBy 0, no storage when empty |

SSCCmd's copy constructor is inlined into both GetAt bodies (ScriptSubCallCopyAt's version calls
the float array's base constructor 004CD080 instead of inlining it; same end state): id, float
arguments (SetSize 004C4410), strings (004C2510), object / group argument pointers (004C46F0 /
004C49F0, copied as words), both binding arrays and the nested calls through the library copy
constructors 004CCCB0 / 004CCD90 (the latter calls 004C7510, so nested trees recurse through
ScriptSubCallCopyAt), and the called script pointer.

Forwarders added in `script_copy.cpp`: 004C2510, 004C3980, 004C2050 SetSize; 004C2680, 004C21E0,
004C3B00 GetAt; 004B24B0 / 004B2500 (rewritten, through their addresses), 004CE1E0 reference
destructor, 004C2290 group destructor, 006FE268 CString ConstructElements, 004CCCB0 / 004CCD90
array copy constructors, 004CBA00 (ours, through its address from ScriptVarsCopyConstruct).

Differences and quirks: the C++ exception frames (unwinding on a throwing operator new) are not
reproduced; kept: a failed operator new in ScriptSubCallCopyAt leaves the copy's command null, as the
original does.

# Runtime leftovers (ticket S2: 004B5FE0, 004BAAB0, 004BE6E0, 004BB800)

None of the four is library code, dead, or touched by Rebirth (no site of config/rebirth_patches.csv
falls inside them; not in docs/rebirth-patched-functions.txt), so they install in both launches.
(Earlier sections listed 004B5FE0 as "MFC template code": it is the game's own SSCCmd::Load.)

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004B5FE0 | 837 | ScriptCmdLoad (SSCCmd::Load, `script_load.cpp`) | record / replay against the original (verify_script_load.cpp): the original reads into a fresh command built by 004D0A50, ours replays the reads; SSCCmd::Save bytes, every array's size and capacity, null object / group slots and the called block pointer compared; ours only (shape) when the command is not empty on entry | world_load, script_copy (`load savea`) |
| 004BAAB0 | 825 | ScriptQuestFinish (QFinish, `script_exec_helpers.cpp`) | against the original (verify_script_runtime.cpp): the original runs first with its CScript::Load skipped (the 004D23A0 wrapper in verify_script_parse.cpp only copies the text when `scriptverify::g_loadSkip` is set), the quest name it empties is restored, then ours runs and loads; the two texts must be equal and the name empty | script_quest (new) |
| 004BE6E0 | 737 | ScriptReset (CScript reset, `script_reset.cpp`) | ours only: flags, error text "Unknown error" (0078BBDC) and line, the seven lists empty with no blocks, globals empty, For depth 0, one empty temporary group and one null temporary reference, point light slots zeroed, areas and definitions empty, +0x68 / +0x6C = -2 | every new game and zone load (newgame, world_load, ...) |
| 004BB800 | 731 | ScriptAddParameter (ScriptDefinition::AddParameter, `script_vars.cpp`) | ours only: before/after counts; a new name gets one more value and name of its type, the name last, the letter appended to `params`; an existing name changes nothing and returns 0 | script_parse, script_exec, script_copy (scripts with parameters) |

SSCCmd::Load mirrors SSCCmd::Save: id, floats (004C4410 SetSize, a 4-byte read each), strings
(004C2510, 004B3D10 each), the object argument count (SetSize inlined, 004CCA10 zeroing; slots
nulled), the group argument count (004C49F0; slots nulled), both binding arrays (004C4CE0, 12 bytes
each), the nested calls (004C4FD0, then 004B6330 per element: type, argument index, SSCCmd::Load into
the element's command) and the called block id, resolved with the FindBlock copy of
`script_load.cpp` (the original calls 004BFA50, the same lookup in 007C27D4's lists). Nested
commands reload through the wrapper in both runs, so a tree of depth d is read 2^d times in the
verify build (script trees are shallow).

QFinish text (quest name q from QStart, N objectives of the CArray at 007AA608 {id, CString, float}):
`DeclareScript Su0()` .. `SuN()`, then per objective `Script Sui(if(` condition `)then(KillScript()
Sui+1() GSSetVar(0,"q.q.q.i+1",2)` and, except for the last, `GSSetVar(0,"q.q.q.i+2",1)`, the
closing script `SuN` (QuestComplete, GSSetVar "q.q.q" 2) and `WorldScript( Su0() )`; loaded with the
WorldScript started; then the quest name is emptied (the objectives stay; QStart frees them). The
pieces are joined with operator+ (00700B9F) and += as the original does.

CScript reset order: +0x1A8 / +0x1AC = 0, error text, +0x1F4 = 0 (the error code +0x1F0 is left),
instances (RemoveHead 004C7A10, destructor 004BE9D0), runs (004C82B0, ScriptVars destructor at +0x14),
WorldScript blocks (RemoveAll inlined, 004CE2D0 per element), the four global clears (rewritten,
through their addresses), For depth, the temporaries (RemoveTail 004C8AF0 / 004C9100 down to one
entry; the last group SetSize(0), the last reference Assign(null)), point lights (each object's timed
entry removed (0058E5D0) and released; the slots zeroed, the array kept), FX / particle / lightning
lists (RemoveAll inlined; 004CE2B0 / 004CE270 / 004CD690), areas SetSize(0) 004CDB30, +0x68 /
+0x6C = -2, definitions SetSize(0) 004CD770, command blocks (004C69B0, destructor 004C54A0).

AddParameter quirk kept: a type outside 1..4 returns the name argument's address (nonzero, as the
original leaves it in ESI) and appends an empty letter (params rebuilt through operator+). The
loader only passes 1..4.

Forwarders added: `script_load.cpp` 004CCA10, 004C4410, 004C49F0, 004C4CE0, 004C4FD0, 004B6330;
`script_vars.cpp` 004C2880, 004C2510, 004C2050, 004C3980, 004CE5F0 (names SetAtGrow), 00700B9F;
`script_exec_helpers.cpp` 004D23A0 (CScript::Load, through its address), 00700B9F; `script_reset.cpp`
the list RemoveHead / RemoveTail and element destructors named above, 004CDB30, 004CD770,
004C2050, 0058E5D0, and the rewritten 004B24B0 / 004B24D0 / 004C1650-004C1760 through their
addresses. The exception frames of QFinish, AddParameter and the reset are not reproduced.

Skipped: 004BDE40 (CScript constructor). Its only caller is the world's constructor 00589ED0, run
by the CRT static initialiser at 00589BE0 for the global world 007AFF60, i.e. before WinMain and the
hook installation: a replacement could never run or be verified in the hooked game. It is needed
for the standalone build (layout: lights CArray 0x73D394 resized to 256 zeroed slots, the lists'
vtables 0x73D388 / 0x73D37C / 0x73D370 / 0x73D364 (blocks and WorldScript blocks) / 0x73D34C /
0x73D340, block size 10, definitions 0x73D358, areas 0x73D334, globals 004BC690 / 004BC740 /
004BC870 / 004BC950, one temporary group and one null reference added (004C8CE0 / 004C9210), the
error text empty, then the reset).

# Savegame parts (ticket S3: 004BBFE0, 004BB320, 004B3AD0, 004BB590, 004BFA50)

Live script runtime functions of 0x004B0000-0x004C0000 that the scenarios reach, none in
config/library.csv, config/dead_code.csv or docs/rebirth-patched-functions.txt (no
config/rebirth_patches.csv site inside them), so they install in both launches. All are pieces of
the savegame writer and reader above that were still forwarders.

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004BBFE0 | 527 | ScriptDefinitionSave (`script_save.cpp`) | against the original (verify_script_save.cpp): both into capture streams, bytes and write sizes compared | every save (the new game's autosave): newgame, world_load, script_copy |
| 004BB320 | 505 | ScriptRunSave (`script_save.cpp`) | as above | saves with waiting runs (world_load, script_copy) |
| 004B3AD0 | 574 | ScriptVarsLoad (ScriptVars::Load, `script_load.cpp`) | record / replay against the original (verify_script_load.cpp): the original into a scratch set built by 004C00D0, ours replays into the real (empty) set; all eight arrays' sizes and capacities, float bits, strings, object pointers, group members / capacity / growBy, names; scratch destroyed with 004BE090. Ours only (counts) when the set is not empty on entry | every save load: definitions and runs (world_load, script_copy) |
| 004BB590 | 204 | ScriptIfThenPairLoad (`script_load.cpp`) | record / replay against the original into a scratch pair; both block pointers compared | instances of a loaded save (world_load, script_copy) |
| 004BFA50 | 71 | ScriptFindBlock (CScript::FindBlock, `script_load.cpp`) | pure, against the original | SSCCmd::Load (every command of a loaded save) |

The definition and run savers inline the float, string and object savers (the same pieces; ours
calls ScriptFloatsSave / ScriptStringsSave directly, objects through ScriptObjectRef::Save 004B25F0)
and call the group saver 004C4330. A missing name is written as one zero byte (a stack variable
the original zeroes first). The run saver finds the run's instance by comparing its kill flag
pointer with each node's data in the +0x140 list of 007AA620 (-1 when absent; the WorldScript's
runs). Neither null-checks the blocks (pairs, run block).

ScriptVars::Load is the four variable loaders' format written out with the arrays' out-of-line
SetSize (004C2880, 004C2510 for strings and names, 004C2050 for the object array, 004C3980),
objects assigned with 004B24D0 after FindObject (00593AA0) and group members read with
ScriptObjectRef::Load (004B2590), then Sort. One stack variable holds the float count and index
and then receives every object id and group size (never reset in between); kept as such.

FindBlock: SSCCmd::Load now calls 004BFA50 through its address (on 007C27D4) as the original does;
the loaders that inline the lookup (if-then pairs, runs) call ScriptFindBlock directly.

Forwarders added: `script_save.cpp` 004B25F0; `script_load.cpp` 004BFA50, 004B2590, 004C3980 (the
rewritten ones through their addresses so their wrappers run); verify 004C00D0. The savers
004BBFE0 / 004BB320 and loaders 004B3AD0 / 004BB590 keep being called through their addresses.

Not taken: 004BE9D0 (definition destructor) and 004BE090 (ScriptVars destructor): destructors are
hard to verify against the original (both would free); the next candidates with the same pattern
are the point light / particle / FX / lightning savers and loaders 004B1940, 004B1A80, 004B1C50,
004B1DB0, 004B18B0, 004B1B40 (reached only when a save holds such sources) and 004B3D10 (string
loader, a few instructions around 00461190).

# Blocks, areas and temporaries (ticket S4: 004BF540, 004BF900, 004BCE10, 004B59F0, 004B5F70)

Five small allocation helpers that the parser and evaluator call on every script load or object
function; none in config/library.csv, config/dead_code.csv or docs/rebirth-patched-functions.txt,
so they install in both launches. Code: `src/game/script_blocks.cpp` (free functions declared at
the end of the loader block of `script.h`); every caller, rewritten or not, keeps calling them
through their addresses (the member forwarders CScript::NewCommandList / NewWorldScript /
FindArea / NewTempObject and SSCCmd::Clear stay), so the wrappers see every call. Verify:
`tests/verify/verify_script_blocks.cpp`.

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004BF540 | 213 | ScriptNewCommandList (CScript::NewCommandList) | against the original on a scratch CScript image holding an empty +0x70 list with the real count and block size (its node from a CPlex block of its own, freed after): the new ScriptDecl's 0x18 bytes and the count equal; ours only: node linked at the real tail | script_parse (every if-then pair and For body), script_exec, script_copy |
| 004BF900 | 228 | ScriptNewWorldScript (CScript::NewWorldScript) | as above on the +0x178 list | script_parse (WorldScript), every mission script load |
| 004BCE10 | 250 | ScriptFindArea (CScript::FindArea) | against the original on a scratch copy of the +0x194 array (own buffer, elements shared shallowly; the original may grow / free that buffer): returned index, vtable, size, capacity, growBy and all elements' bytes equal | script_exec (AddRectToArea / AddRoundToArea / IsInArea / DeleteArea) |
| 004B59F0 | 229 | ScriptNewTempObject (CScript::NewTempObject) | against the original on a scratch +0x1CC list (as the blocks): the reference (null) and count equal; ours only: linking | every object function (CalcObject): script_exec, script_parse |
| 004B5F70 | 108 | ScriptCmdClear (SSCCmd::Clear) | against the original on a deep copy of the command (CArray<SSCCmd>::Copy 004CC0C0 from a one-element view; the arrays' vtables and growBy copied over, which the assignment leaves out): the cleared 0x94 bytes equal; the copy destroyed with 004BF620 | script_parse (LoadFunction / LoadConditions / LoadCommands) |

Findings: a command block's id is not a counter of its own: it is the list's count right after
the append (the dword after the tail, +0x7C / +0x184), the WorldScript's with bit 31 set. The
originals build an empty temporary element on the stack and copy it in (CArray::Copy 004CC0C0 /
004CE2F0; kept, through the addresses) including its uninitialised +0x14 word, which the id then
overwrites (not reproduced). The temporaries own nothing, so their destructors are left out.
NewTempObject inlines CList::AddTail of a null reference: the free list refilled from a new CPlex
block (006FE8EF) last node first, ConstructElements 004CC800, then the null assignment (Release
through vtable slot 4 when set; never, the element was just zeroed). FindArea re-reads the size
after SetSize to index the new slot, as kept.

Forwarders added (`script_blocks.cpp`): 004CD6D0 (CList<ScriptDecl>::NewNode), 004CDB30 areas
SetSize, 004CE2F0 parts Copy, 004CC800, the SSCCmd array SetSizes 004C2510 / 004C46F0 / 004C49F0 /
004C4CE0 / 004C4FD0; 004CC0C0 from originals_library.h. Verify: 004CC0C0, 004BF620.
The exception frames are not reproduced.

# Constructor, reset and small loaders (ticket S5: 004C00D0, 004D3320, 004B3D10, 004B6330)

None is in config/library.csv, config/dead_code.csv or docs/rebirth-patched-functions.txt, so they
install in both launches. Every caller (rewritten or not) calls them through their addresses, so
the wrappers see every call.

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004C00D0 | 203 | ScriptVarsConstruct (ScriptVars constructor, `script_copy.cpp`) | against the original (verify_script_copy.cpp): the original into a 0xCC-filled scratch, ours into the real set; all 0xA0 bytes (every one is written: vtables and zeroes, no allocation) and both returned pointers | CScript::Load (004D23A0's local definition: every script load), the instance loader (world_load) |
| 004D3320 | 182 | ScriptDefinitionReset (`script_reset.cpp`) | against the original (verify_script_runtime.cpp): a scratch definition built like Load's local one and assigned from the real one with 004CB460 (deep copy); the original resets the scratch, ours the real one; index, name, parameters, every array's vtable / data / size / capacity, and the real growBy words unchanged; scratch destroyed with 004BE9D0 | CScript::Load before every `Script` section: script_parse, script_exec, script_copy, console |
| 004B3D10 | 86 | ScriptStringLoad (`script_load.cpp`) | record / replay against the original (verify_script_load.cpp): the original into a scratch empty CString, ours replays into the real one; texts compared | every name and string of a loaded save (world_load, script_copy) |
| 004B6330 | 45 | ScriptSubCallLoad (`script_load.cpp`) | record / replay against the original: the original into a scratch element with a fresh command (004D0A50), ours into the real one; type, argument index and the commands as ScriptCmdLoad compares them (`SameLoadedCmd`); scratch command destroyed with 004BF640 | SSCCmd::Load of a command with nested calls (world_load, script_copy) |

The constructor writes each array's base-constructor state (004C24F0 / 004C2030 / 004C3960: zeroes
and the base vtable) and then the derived vtable; ours writes the end state directly (InitArray).
The reset frees the if-then pairs inline (no element destructor; the pairs only point at blocks the
CScript owns), runs the eight variable arrays' SetSize(0, -1) in member order (004C2880, 004C2510,
004C2050, 004C3980), then index 0, parameters and name assigned 0079B298 (read from the exe).
004B3D10 is __fastcall (ECX the string, EDX the stream): 00461190 (rewritten, called through its
address) constructs the temporary, CString::operator= assigns it.

Forwarders added: `script_load.cpp` 00461190; `script_reset.cpp` 004C3980 and the library header's
004C2880 / 004C2510; verify: 004CB460 (ScriptDefinition operator=), 004BE9D0, 004C00D0.

Not taken: 004BFC60 (find a definition by name): only the dead dumper 004D41C0 calls it.
004B3900 (ScriptVars::AppendFrom, four calls to the rewritten appends) is the next small one;
004BE9D0 / 004BE090 / 004BF640 destructors still need a verification idea (both runs would free).

# Destructors and AppendFrom (ticket S6: 004BE090, 004BE9D0, 004BF640, 004B3900)

None is in config/library.csv, config/dead_code.csv or docs/rebirth-patched-functions.txt (nor any
callee), so they install in both launches. Every caller (rewritten or not, the C++ unwind funclets
included) calls them through their addresses. Code: `src/game/script_destroy.cpp` (destructors),
`script_vars_append.cpp` (AppendFrom). Verify: `tests/verify/verify_script_destroy.cpp`.

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004BE090 | 438 | ScriptVarsDestruct (ScriptVars destructor) | against the original on a deep copy (copy constructor 004CB7E0), see below | runs leaving the run list (004C82B0), the reset, CScript::Load's locals: script_exec, script_parse, script_copy, world_load |
| 004BE9D0 | 295 | ScriptDefinitionDestruct | against the original on a deep copy (built like Load's local definition, assigned with 004CB460) | instances leaving the list (004C7A10), definitions SetSize (004CD770), Load's local definition: every script load |
| 004BF640 | 285 | ScriptCmdDestruct (SSCCmd destructor) | against the original on a deep copy (CArray<SSCCmd>::Copy 004CC0C0 from a one-element view) | command blocks freed by the reset, Clear / Load / parser temporaries: script_parse, script_exec, world_load |
| 004B3900 | 55 | ScriptVarsAppendFrom | ours only: all eight sizes as the appends set them (the four appends check the contents) | GlobalVars sections (script_exec, script_parse, zone scripts with GlobalVars) |

Destructor verification (the idea asked for in S3 / S5): the original destroys a deep copy made
with the game's own copy functions, ours the real object. Compared: (1) the words written: every
dword the original changed in the copy must hold the same value in the real object, every other
dword of the real object must be unchanged (only base vtables are written; data pointers, sizes and
capacities stay); (2) the reference counts dropped: for each CString buffer (nRefs; the shared
empty string skipped) and world object (refCount_) the real object holds, ours must drop it by the
number of times it is held (an SSCCmd's nested commands included), and by as much as the original
dropped it on the copy when the copy took exactly that many references. Counters ours drops to
zero (buffer or object possibly freed) are not read after. The frees themselves are not checked.
While the original runs on a copy, nested destructor calls (the copy's nested commands through
004CE230) go straight to the original; nested calls made by ours are verified in turn, so a command
tree of depth d is copied d times.

Layouts: each inlined CArray destructor writes only the base vtable (the derived destructor's store
is dropped), destroys the elements and frees the storage, members last to first. ScriptVars: name
arrays 0073D2F8 (006FE296), groups 0073D310 (004CE210 per element), objects 0073D11C (004CC7D0),
floats 0073D2EC. The definition's destructor calls the out-of-line array destructors instead
(004C26A0 names, 004C2290 objects, 004C29C0 floats; groups inline with 004CC980, strings inline),
then the if-then pairs (0073D3AC, freed, nothing per element), parameters and name (00700970); the
index is left. SSCCmd: nested calls 0073D418 (004CE230 per element: the command destroyed through
004BF640 and deleted), bindings 0073D40C, group / object argument pointers 0073D400 / 0073D3F4
(storage only), strings, floats 0073D3E8; id and the called script are left.

Forwarders added: `script_destroy.cpp` 006FE296, 004CC7D0, 004CC980, 004CE210, 004CE230, 004C26A0,
004C2290, 004C29C0; `script_vars_append.cpp` the four appends through their addresses; verify:
004CB7E0, 004C00D0, 004CB460, 004CC0C0. The exception frames are not reproduced.

Not taken: the element destructors 004CE1E0, 004CE210, 004CE230, 004CE270, 004CE2B0, 004CE2D0 and
004BF620 (small wrappers around these; the same deep-copy check would fit them).

## X10: command 8 mismatches in gz2g (verify bug)

zone_gz2g_traps reported `ScriptRun::Execute command` mismatches on "command 8 (Unknown command)"
(the declared-script call; KeywordName has no name for it since it is not a keyword). Ours did the
same as the original (004B68F6..004B692D: `_ftol` float 0, definition = CScript+0x90 + i * 0xC0,
004C7C90 on CScript+0x140, ScriptBindCall 004BB6A0, killed flag 0). The check was wrong: 004C7C90
was taken for an AddTail, but it links the new node before the head (node->next = old head, old
head->prev = node, head = node; the tail changes only when the list was empty). The observer read
the tail instance (+0x148), i.e. the oldest one. That passes while the list was empty or the
oldest instance looks like the new one (alive, same float count), and fails in gz2g's pattern
`KillScript() VTriger#0#n( this )` when the calling instance, already flagged killed, is the oldest
one. The check now
reads the head (+0x144); the forwarder is renamed InstanceAddHead. No change to the game's
behaviour.

gz2g's scripts (zone2.mob chunk 0xACCEECCB, decoded as 004D8E10 does: the first dword seeds the
MSVC rand LCG, x = x * 0x343FD + 0x269EC3, each byte XORed with x >> 16) start every magic trap
armed and disarm them from triggers (`ActivateTrap(MTF1..4, 0)` etc.).
# Element destructors (ticket S7: 004CE1E0, 004CE210, 004CE230, 004CE270, 004CE2B0, 004CE2D0, 004BF620)

None is in config/library.csv, config/dead_code.csv or docs/rebirth-patched-functions.txt (nor are the
callees 004B5EF0, 004BD540, 004BF7E0, 004BF640 and 0058E5D0), so they install in both launches. All are
`__thiscall (flags)`, RET 4, return `this`; bit 0 of the flags also deletes the element (007005D8).
Code: `src/game/script_destroy.cpp`; verify: `tests/verify/verify_script_destroy.cpp`.

| Address | Element | Does | Verification | Reached by |
|---|---|---|---|---|
| 004CE1E0 | ScriptObjectRef | release the object, clear the slot | against the original on a copy of the slot with the object's count raised (words, count drops, return) | CArray<ScriptObjectRef> SetSize / copy (script_copy, script_vars_append, script_load) |
| 004CE210 | ScriptGroup | body 004B5EF0 (base vtable 0073D11C, references released, array freed) | ours only: vtable the only word changed, counts dropped, return | ScriptVars destructor / group arrays (S6's list) |
| 004CE230 | ScriptSubCall | its command destroyed with 004BF640 and deleted | against the original on a deep copy (CArray<SSCCmd>::Copy 004CC0C0) | SSCCmd destructor, nested commands: script_parse, script_exec, world_load |
| 004CE270 | particle source's object | timed removal (0058E5D0 with the id at +0xC), release, slot cleared | ours only: slot cleared, count down by 1 or 2, return | the reset's particle list (+0x14), zone changes |
| 004CE2B0 | FX source | body 004BD540 (the same removal and release, then its CString) | ours only: object slot cleared, return | the reset's FX list (+0x30) |
| 004CE2D0 | world block | body 004BF7E0 (vtable 0073D3D0, commands destroyed inline, array freed) | ours only: vtable the only word changed, counts dropped, return | the reset's block list (+0x178) |
| 004BF620 | SSCCmd | 004BF640 | ours only: counts dropped, return (004BF640 has its own comparison) | command arrays SetSize / Copy (script_load) |

"Ours only" for four of them because the first three bytes are followed by a relative CALL, so the
original cannot be run through a trampoline (no 5 stolen bytes without it). 004CE270 / 004CE2B0 also
change the world (timed removal), so running the original first on a copy would change what ours sees.
Flags with bit 0 set are passed through unchecked (the element is freed). Forwarders added:
004B5EF0, 004BD540, 004BF7E0, 004BF640 (script_destroy.cpp). The bodies keep their exception frames
(they stay original); the wrappers have none.

# Area ticket A-S (2026-10-05): script effects, ...

Layouts added to `script.h` for this ticket: `MfcList<T>` (MFC CList, nodes next / prev / element),
the whole `CScript` (0x1F8 bytes, every field named, offsets asserted; the old accessors now return
members) and the effect elements `ScriptPointLight` (0x20), `ScriptParticleSource` (0x24),
`ScriptFxSource` (0x34: a sound source; +0x18 / +0x1C become the sound's volume / radius, +0x20
`idle`: the tick skips a source with it set, +0x24 the random placement radius, +0x28 / +0x2C /
+0x30 delays and countdown), `ScriptLightning` (8) and the particle type table 0078B120
(`ScriptParticleType`: name, CEffectParticle type, lifetime; 64 entries).

## Effect savers and loaders (004B1940, 004B1A80, 004B1C50, 004B1DB0, 004B18B0, 004B1B40)

Code: `src/game/script_effects.cpp`. None is in config/library.csv, config/dead_code.csv or
docs/rebirth-patched-functions.txt; neither are the callees (CBaseManager::FindObject 00403F40 and
CStream::ReadString 00461190, both rewritten and called directly). The savers are called from
CScript::Save (004C0F90), the point light loader from CScript::Load (004C0D00, every slot), the FX
source loader from the FX list loader 004CA5D0; all callers go through the original addresses.

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004B1940 | 161 | ScriptPointLightSave | against the original: both into capture streams, bytes and write sizes (verify_script_save.cpp) | every save (256 slots): newgame's autosave, script_fx |
| 004B1A80 | 182 | ScriptParticleSourceSave | as above | saves with particle sources: script_fx |
| 004B1C50 | 272 | ScriptFxSourceSave | as above | saves with FX sources: script_fx (CreateRandomizedFXSource) |
| 004B1DB0 | 85 | ScriptLightningSave | as above | saves with lightning: script_fx |
| 004B18B0 | 130 | ScriptPointLightLoad | record / replay (verify_script_load.cpp): the original into a zeroed scratch slot, ours replays into the real one; the 0x20 bytes compared; the scratch's reference released | every load: world_load, script_copy, script_fx |
| 004B1B40 | 264 | ScriptFxSourceLoad | record / replay: scratch element (empty name), all fields and the name's text compared | loads with FX sources: script_fx |

Format: the effect object's id (-1 when none), then the element's fields in order, each as one
Write / Read (the step vector and an FX source's position as one 12-byte piece; an FX source's name
with its terminator, its length from strlen). Loading looks the id up in the world's effect manager
(`CWorld::manager48_`, 007AFFA8) and raises the found object's reference count inline.

New scenario `tests/scenarios/script_fx.txt`: particle sources (by id, automatic id, unknown type,
same id again, moved), lightning, a one-off sound, a point light and a randomized FX source, then a
save into a new slot through the escape menu (as ui_saveload) and `load save1`.

## Effect element destructors, CreateFX, CreateLightning (004B17F0, 004B1820, 004B1850, 004B1880, 004BD260, 004BCFF0)

Code: `src/game/script_effects.cpp`; verify: `tests/verify/verify_script_effects.cpp`. Not library,
dead or Rebirth-patched; the callees (CBaseManager::CreateObject slot 23 on the effect manager,
CWorldServer::AddTimedObject / RemoveTimedObject 0058E520 / 0058E5D0, CEffectLightning::SetEnds
0067A6B0, all rewritten) are called directly; the list's NewNode 004CD620 is library (forwarder,
now shared in originals_library.h with 004CD540 / 004CD5B0, which script_load.cpp used).

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004B17F0 | 38 | ScriptPointLightDestruct (point light slot) | ours only: slot cleared, count down by 1 or 2 (read when > 2 before) | the slot array's deleting destructor 0059DD70 / Serialize 004C5E60 (world teardown) |
| 004B1820 | 38 | ScriptLightningDestruct | as above | CreateLightning's temporary (script_fx), the list's RemoveAll 0059C1F0, 004BEB00 |
| 004B1850 | 38 | ScriptParticleSourceDestruct | as above | the particle list's RemoveAll 0059C1A0 (world teardown) |
| 004B1880 | 38 | ScriptFxSourceDropSound (FX element, the object only) | as above | CreateFXSource's temporary (script_fx) |
| 004BD260 | 167 | ScriptCreateFx (CreateFX) | ours only: the effect manager registered a new object (lastId_), class 0x48, position / volume / radius bits and name as the arguments, not looping | sound, script_fx |
| 004BCFF0 | 304 | ScriptCreateLightning (CreateLightning) | ours only: one more node at the tail; id (automatic for a negative or NaN argument, nextParticleId_ counted down; else truncated); the bolt is the newest effect, class 0x4D, count >= 2, ends and width bits as the arguments | script_fx |

The four destructors are the same 38 bytes (one per element type). CreateFX keeps no reference:
the sound lives as a timed object only (not looping: it ends after its first tick). CreateLightning
appends without looking the id up (two bolts can share an id; DeleteLightning removes the first),
and its id test is `FCOMP 0; TEST AH,1` (negative or NaN: automatic) followed by `_ftol` (truncation).

## Particle sources and the FX source destructor (004BD9A0, 004BDBB0, 004BD540)

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004BD9A0 | 471 | ScriptCreateParticleSource (CreateParticleSource) | ours only: unknown type name: nothing changed (count, nextParticleId_, the manager's lastId_); else the source with the id (automatic for a negative FISTP id) holds the newest effect (class 0x4A, the table's type, position and size bits, count >= 2), not moving, the type's lifetime; the list grew by one exactly when the id was new (then at the tail) | effects, particles, script_fx |
| 004BDBB0 | 214 | ScriptMoveParticleSource (MoveParticleSource) | against the original: the original moves the real source, its result is kept, the source restored, ours runs; flags and steps equal, the step with CheckNear | script_fx |
| 004BD540 | 102 | ScriptFxSourceDestruct (FX source element body) | ours only: object slot cleared, count down by 1 or 2 | the element destructor 004CE2B0 (the reset), the list's RemoveAt 004C6370 (DeleteFXSource), 004BEB00 |

CreateParticleSource: the type name is copied into a CString first and compared with the game's
stricmp (table name first); the table is read from the exe's memory (0078B120, 64 entries of name,
type, lifetime). The id is FISTP (rounded), unlike CreateLightning's truncation. For a new id the
original copies a stack temporary (only its first word set) into the new node; every field is
written afterwards, so ours just links the node. MoveParticleSource's step is
(point - position) * (1 / steps), the reciprocal rounded to float first (plain C++ now; the
original keeps the x difference on the FPU).

Not taken: 004BD310 (CreateFXSource / CreateRandomizedFXSource). For CreateFXSource (command 0x16)
of a new id the original leaves spread, minimum / maximum delay and countdown (+0x24..+0x30) as the
stack garbage of a temporary it copies into the node, and the tick then uses them (respawn delay,
DIV by max - min + 1, placement radius). Rewriting it means choosing values the original never
had (zero would replace the sound every tick); left original. 004BD120 / 004BDB80 (the same 38-byte
body as the element destructors above) are only called from C++ unwind funclets.

## Variable array constructors and destructors (004BC690-004BCAE0)

Code: `src/game/script_arrays.cpp`; verify: `tests/verify/verify_script_destroy.cpp`. The compiler-
generated constructors and destructors of `ScriptNamedValues<T>` (one type's values and names), of
single CString / reference / group arrays, and the ScriptRun destructor. None is library, dead or
Rebirth-patched (their library siblings 004C26A0, 004C2290, 004CC980, 004CC7D0 are; those stay
forwarders, now shared in originals_library.h where script_destroy.cpp / script_load.cpp had their
own). The vtables they write are named in `script.h` (`scriptvtable::`).

| Address | Size | Name | Verification | Reached by |
|---|---|---|---|---|
| 004BC690 / 004BC740 / 004BC870 / 004BC950 | 42 each | ScriptFloats / Strings / Objects / GroupsConstruct | against the original: original into a 0xCC scratch, ours into the real set; all 0x28 bytes and the returned pointers | the original LoadCommands (004D1330, Rebirth-patched: the Rebirth launch's script_parse, script_exec), the CScript constructor (before the hooks), the list Serialize instantiations |
| 004BC6C0 / 004BC770 / 004BC8A0 | 126 / 147 / 175 | ScriptFloats / Strings / ObjectsDestruct | against the original on a deep copy (the game's copy constructors 004CBB80 / 004CBCA0 / 004CBDB0), S6's rules: the words each writes and the reference counts each drops | the instance / run lists' RemoveHead (004C7A10 / 004C82B0: the reset, every new game and zone load), the definition list's destructor, the original LoadCommands (Rebirth) |
| 004BC810 | 88 | ScriptNamesDestruct (CString array) | as above, the copy by 004CBA00 | the CString array's deleting destructor 004C1860 |
| 004BC9A0 | 118 | ScriptRefArrayDestruct | ours only: only the vtable word changes (to 0073D11C), every held reference drops by its multiplicity | the reference array's deleting destructor 004C1840 |
| 004BCA20 | 182 | ScriptGroupArrayDestruct | ours only: as above (0073D310), the members' references | the group array's deleting destructor 004C18A0 |
| 004BCAE0 | 380 | ScriptRunDestruct | against the original on a copy (the first five words, the variables by 004CB7E0) | every finished run (the run list's FreeNode 004CDAB0), DestructElements<ScriptRun> 004CEC80: script_exec, script_parse, console |

The groups' references in 004BCA20 are reset with 004B5AE0 (the copy of ScriptObjectRef::Reset).
004BCAE0 destroys the run's variables as ScriptVarsDestruct (004BE090) does, except that the group
elements go through DestructElements<ScriptGroup> (004CC980) and the float names through the
out-of-line CString array destructor (004C26A0), as the original calls them.
