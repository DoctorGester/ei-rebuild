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
- 004B6760 SSCCmd::Execute (14 KB command switch) and the CScript loader 004CF4D0-004D53C0
  (tokenizer, LoadFunction, LoadAssignOperator...): mostly inside Rebirth's patched range.
- 004B5FE0 SSCCmd::Load, 004B3AD0 ScriptVars::Load, 004B3360's siblings (CArray SetSize/Serialize
  template instantiations 004C1xxx-004CExxx): MFC template code.
- 004B3A70 (copy all values from another ScriptVars): no callers found.
- 004B2520 (unit stats pointer): 16 bytes with a branch in the first 5, no clean trampoline; one caller.
- 004B0000-004B2070 and 004BA000-004CE000: other subsystems (areas, quests, unit modes) not mapped.
