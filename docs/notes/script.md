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
- CScript: 004B59F0 NewTempObject, 004BCE10 FindArea. SSCCmd::CalcGroup 004B5B00 (not rewritten).
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

- 004B5B00 SSCCmd::CalcGroup: the PlayerSee case walks a hash container through iterator helpers
  (00455830/00455890/004558B0) with an unclear frame; left for later.
- 004B6760 SSCCmd::Execute (14 KB command switch). The CScript loader 004CF4D0-004D53C0 was
  rewritten later: see "Loader and parser" below.
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
(LoadFunction, LoadCommands, the keyword lookups, Error) keep Rebirth's version, and in verify
builds every callee runs through its own wrapper. LoadFunction calls itself directly (through its
wrapper the tree would be parsed 2^depth times). The keyword table is read from ClassifyToken's code
(the immediate at 004CF69B, which Rebirth points at its extended table); the vanilla value is
0078BCA0. Only the Rebirth-patched rewrites use it.

In the side-by-side wrappers both runs call `CScript::Error` with the same arguments on an error;
the second call writes the same text, code and line again.

## Forwarders added (standalone build must replace)

- CScript: 004CF470 Error (Rebirth patches it), 004BF540 NewCommandList, 004BF900 NewWorldScript,
  004BCC60 RunWorldScript; 004D5880 reads a text file (CStdioFile).
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

- 004CF470 CScript::Error: Rebirth patches it; forwarder.
- 004D3320 (definition reset) and the CArray/CList template members listed above.
- 004D38F0 / 004D38B0 (strip the `!n!` prefix) and the script dumper 004D39D0, 004D41C0
  (Rebirth patches it), 004D4E80, 004D50C0, 004D5310, 004D53C0 (writes definitions back as text).
- 004D5880 (reads a file through CStdioFile, MFC exceptions).
- 004BFCC0, 004C01A0, 004C06F0, 004C09F0: build quest scripts from generated text with the same
  parser functions ("Symbols behind conditions list" etc.); not rewritten.
- 004C0000-004CF000: mostly CArray/CList template instantiations of the script types.
