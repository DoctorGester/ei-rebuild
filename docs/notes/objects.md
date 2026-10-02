# World objects, shared helpers (00440000-00470000)

Files: `src/game/object.h/.cpp` (CEngineObject helpers, CTransformObject/CHoover constructor bodies,
unit and figure getters, CMemoryStream), `src/game/object_diplomacy.h/.cpp`,
`src/game/object_maths.h/.cpp`, `src/game/object_tables.h/.cpp`.
Verify: `tests/verify/verify_object.cpp`, `tests/verify/verify_object_diplomacy.cpp`.

Shared headers touched (declarations only, no layout change): `engine_object.h` (three non-virtual
CEngineObject helpers), `core_transform.h` (`CTransformObject::ConstructBase`), `maths.h`
(`Vector3::DivideInPlace`), `lib/crt.h/.cpp` (`gamecrt::mbscmp`, 006E848B `_mbscmp`).

## What lives in this range

The range is not only the object base classes; roughly:

| Range | Contents |
|---|---|
| 00440000-00444000 | network sessions (UDP/TCP sockets, server link, "CONNECTION LOST"), STLport list/vector instantiations |
| 00444000-00446000 | debug text overlay (Courier New font), STLport map<string> helpers (00445780, 00445810 are string compares) |
| 004466D0-00446C10 | CFakeUnit constructor (004466D0) and its own slots; it embeds four CArrays (vtables 0073C0E4..0073C108) |
| 00447F50-0044C470 | .anm/.bon animation loading and splitting ("Error during split animation"), CArray instantiations |
| 0044C470 | `DebugLog` stub (81 callers) |
| 0044F000-00452B00 | CArray / CMap template instantiations (destructors, SetSize, element constructors) |
| 00453240-00454A70 | CFigure / CFakeFigure helpers and their CArray/CMap instantiations |
| 004554B0-00455800 | CDiplomacy (rewritten) |
| 00455830-004558B0 | STLport hashtable begin / iterator increment (skipped) |
| 00455930-00455980 | CEngineObject base: destructor body, ManagerContext, SetOwner, Release (00455980, base_manager.cpp) |
| 00455A90-00456400 | misc: disk free space, PostQuitMessage flag, message box helper |
| 00456440 | WinMain; 00457D40 window procedure; 00458220 message pump |
| 0045D5B0-00460BA0 | map database (map.txt / map_LMP.txt: "#allod" and map records), name -> float hash tables |
| 00461360-004613A0 | text keys |
| 00461C10-00461CA0 | CHoover |
| 004621A0-00462A00 | CInventory (vtable 0073C474; its slots 16-21 are shared with the CItem vtables) |
| 00462F70 | key -> command table lookup |
| 00463000-00466000 | STLport hash_map / list instantiations |
| 004667F0 | `IsaacRandom::Next` (random.cpp) |
| 00466E60-00469000 | CLightning (vtable 0073C550) and CLightningManager (0073F5B8) |
| 00469630-0046F160 | map-editor object specs (base vtable 0073C5C4, then 0073C5F0; magic trap 0073C650, fire 0073C620, particle source 0073C758, light 0073C788, sound 0073C7B8; "efcu*" effect names) saved through the chunked stream at 0042BF50 |

### Vtables using functions of this range

Found by scanning .rdata for runs that have CEngineObject's slot 1 (00402D40) and slot 4 (00455980);
156 engine-object vtables in total. Class names from slot 11, class ids from slot 12.

- `CFakeUnit` (0073BFE8, class id 0x50, 64 slots) and `CUnit` (0073EA8C, 0x50, 77 slots) share:
  slot 7 GetName (+0x238, a CString), 29 / 43 return true, 31 / 56 / 57 / 58 forward to the unit's
  controller object at +0x240 (its slots 20, 42, 53, 31), 47 forwards to +0x170 slot 19, 54 returns
  +0x278 (AI notes: "large unit"), 55 returns +0x274 (AI notes: skip unit layers), 59 calls
  0052F920 and returns its first output, 60 returns float +0x328, 61 assigns the name (CString =).
  Slots 43 / 47 also appear in CTree, CBuilding, CStone, CStuffObject, CTrapObject, CMagicTrapObject,
  CLeverObject (47 only) and CTorchObject.
- `CFigure` (0073C1C4, 0073C224, 0073FBF0; 0x44): slot 7 GetName = +0x120 (also in CSkyObject and
  CI3DFigure). `CFakeFigure` (0073C274, 0x47): slot 7 = +0x94, slots 2/3/16/17 empty.
- `CHoover` (0073C434, 0x1F), `CInventory` (0073C474, 0x17), `CLightning` (0073C550, 0x1E),
  `CLightningManager` (0073F5B8, slots 2 and 14 here).
- CEngineObject itself: vtable 0073B8E0 (13 slots), restored by the base destructor body 00455930.

### Classes and fields

- `CEngineObject`: the inline constructor pattern is refCount_ = 0, id_ = -1, unknown_18 = type_ =
  flags_ = owner_ = 0 (00461C10, 00453400, 00468680). `owner_` is the manager; the manager's +0xAC
  (`CBaseManager::owner_AC`) is what `ManagerContext` returns; 00455950 calls that object's slot 13.
- `ObjectListIterator` {manager, position}: walks a manager's CPtrList at +0x34 (00404920 First,
  00404960 Next). It holds a reference on the manager (`refCount_`), which the owner drops with
  `--manager->refCount_`. The world object manager is the pointer at 007AFFA0.
- CUnit fields (offsets in `unitfield::`, the class layout belongs to ai_map.h): +0x238 name,
  +0x240 controller, +0x244 hostile mask (bit p: hostile to player p; set by 0052E970, or-ed by
  0052E940, both also set flags_ 0x80000), +0x24C party (its members in a hash_map at +0x40),
  +0x250 player, +0x274 / +0x278 flags, +0x328 float.
- `CDiplomacy` (global 007C2BC4, 0x100 bytes): `allied_[32]` (+0) and `hostile_[32]` (+0x80), bit q
  of row p. Relation codes 0 allied, 1 neutral, 2 hostile (script GetDiplomacy maps them to 1/0/-1).
  The script area has a local forwarder for 00455630 in script_eval.cpp; it could use this class.
- `CMemoryStream` (vtable 0073BD90, 0x28 bytes): +5 ok flag, +0x10 grow step, +0x18 data,
  +0x20 read position, +0x24 end; slot 1 Read (004A5380), slot 2 Write. The diplomacy table is read
  and written through slots 1 / 2 of such a stream.
- `CMapDatabase` (global 0079BF00, constructor 0045E820, loader 0045EA20): two CArrays, allods (0x2C
  records) at +0 and maps (0x5C records) at +0x14, names first.

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 00455940 | `CEngineObject::ManagerContext` | pure |
| 00455970 | `CEngineObject::SetOwner` (returns the old owner) | original on a scratch copy of the 0x1C bytes |
| 00455930 | `CEngineObject::DestructBase` | scratch copy |
| 00453400 | `CTransformObject::ConstructBase` (returns this) | scratch copy of 0x54 bytes |
| 00461C10 | `CHoover::Construct` (returns this) | scratch copy |
| 00461C80 | `CHoover::Destruct` | scratch copy (the original's tail jump reaches the 00455930 wrapper) |
| 00446820 | `UnitGetName` (CUnit/CFakeUnit slot 7) | pure |
| 00446880 / 00446890 | `UnitGetFlag274` / `UnitGetFlag278` (slots 55 / 54) | pure |
| 004468A0 | `UnitGetValue328` (slot 60, float) | pure, bits |
| 00453440 | `FigureGetName` (CFigure slot 7) | pure |
| 004547C0 | `FakeFigureGetName` (CFakeFigure slot 7) | pure |
| 00468C20 | `CMemoryStream::Reset` | scratch copy of 0x28 bytes |
| 004554B0 | `CDiplomacy::Reset` | scratch copy of the table |
| 004554E0 | `CDiplomacy::SetRelation` | table: original on a scratch copy without the unit pass; units: consistency (see below) |
| 00455630 | `CDiplomacy::GetRelation` | pure |
| 00455660 | `CDiplomacy::GetHostileMask` | pure |
| 004557C0 / 004557F0 | `CDiplomacy::ReadFrom` / `WriteTo` | recorded stream calls |
| 00455680 | `CDiplomacy::OnAttack` | ours only, consistency |
| 004489D0 | `CrossProduct` (`__fastcall`, returns out) | pure, ours into scratch first |
| 0044A230 | `SubtractVectors` | pure |
| 0044CC10 | `SubtractVectors2` (2D) | pure |
| 00448A10 | byte-identical copy of `ScaleVector` (00410770); registered with the core implementation | pure |
| 004679B0 | `Vector3::DivideInPlace` | in place, scratch copy |
| 004679D0 | `Vector4::FromPoint` (returns this) | scratch copy |
| 00461360 | `TextKeyFromName` (`__fastcall`) | pure |
| 004613A0 | `IsTextKey` (`__fastcall`) | pure |
| 0045E850 / 0045E910 | `CMapDatabase::FindMap` / `FindAllod` | pure |
| 00460AF0 | `NamedFloatTable::Get` (hash_map<CString, float>) | pure, bits |
| 00462F70 | `FindCommandByKey` (`__fastcall`) | pure, against a copy of the original body (below) |

All names are provisional and set in Ghidra (plus the forwarders and the GetClassName slots of
CFakeUnit, CHoover, CInventory, CLightning, CFigure, CFakeFigure, and the map database constructor
and loader).

The getters and constructors registered as `__fastcall` free functions are thiscall methods with no
stack arguments in the original; their classes' virtual interfaces are not declared, and the
CEngineObject virtual names (GetName) must not be reused by non-virtual members.

All wrappers except OnAttack, SetRelation's unit pass and NamedFloatTable::Get were also run
offline (scratch harness, not committed: game.exe mapped at 0x400000 in a suspended child, the
trampolines installed as in the DLL, the original addresses called with random data, including NaN /
denormal bit patterns and aliased outputs, under x87 precision control 24, 53 and 64): 60000+ calls
each, no mismatches. NamedFloatTable::Get needs the game's CRT heap (CString), so it is only checked
in game.

## Forwarders added

- 00404920 / 00404960 `ObjectListIterator::First/Next` (core range, not rewritten).
- 0052E970 `UnitSetHostileMask`, 0052E940 `UnitAddHostileMask` (unit range).
- `gamecrt::mbscmp` (006E848B).

## Verify wrappers

- Pure functions: both run, compared bit for bit (floats as bits); the original's result is returned.
- Constructors / destructor bodies / setters / `CMemoryStream::Reset` / `CDiplomacy::Reset`: the
  original runs on a scratch copy of the bytes the function can touch, ours on the real object;
  bytes and return values compared.
- `CDiplomacy::SetRelation`: the original runs on a scratch copy of the table with `applyToUnits` 0,
  ours for real; the tables must match. With `applyToUnits`, every unit of the player in the world
  object list must then have the other player's bit in its hostile mask exactly when the relation
  is 2.
- `CDiplomacy::ReadFrom/WriteTo`: the original against a stream that only records (Read fills
  nothing), ours against the real stream through a recording proxy; the (slot, offset, size)
  sequences must match.
- `CDiplomacy::OnAttack`: ours only. If the victim's player is allied with the attacker's, the
  victim's mask and flags must be unchanged; otherwise the victim and its party members (up to the
  first empty entry) must have the attacker's bit and the victim flag 0x80000.
- `FindCommandByKey` (00462F70) loops back to 00462F73, inside the 5 bytes the hook's jump
  overwrites, so it cannot run through a trampoline: the wrapper copies the whole 61-byte body
  (no calls, only short internal jumps) to executable memory when the DLL loads, before the hooks are
  installed, and runs that copy as the original (`stolenBytes` 0).

## Original bugs kept on purpose

- `CDiplomacy::SetRelation` with a relation outside 0..2 leaves the table alone but still clears the
  bit in the units' hostile masks (the unit pass only checks for 2). Player indices are not checked.
- `CDiplomacy::GetRelation` does not check its indices (GetHostileMask does).
- `CDiplomacy::OnAttack` stops at the first party entry with a null unit, and sets the victim's
  0x80000 flag a second time after 0052E940 already did when the mask changed.

(`CMemoryStream::Reset` drops the buffer pointer without freeing it and `ConstructBase` leaves the
positions uninitialised; both are only used from constructors, so these are not bugs.)

## Skipped

- STLport / MFC template instantiations: hashtable begin / ++ (00455830, 004558B0, 004614C0,
  00462680), string compares (00445780, 00445810), `__stl_hash_string` (00460BA0), node pool frees,
  CArray / CMap SetSize / destructors / element constructors (most of 0044F000-00455400, 0045E130-
  004607E0, 00463000-00465000), pair constructors (00455890, 00461520).
- Trivial bodies too short for the 5-byte jump (`MOV AL,1; RET`, `RET`, 4-byte getters such as
  CInventory slot 16 at 004622F0).
- Pure forwarders to another object's virtual (CUnit slots 31, 47, 56-58, 00455950): rewriting them
  adds nothing, and a side-by-side check would call the target twice.
- Functions without callers (00466270 curve sample, 0045E8B0, 0045E960): verification would never run.
- Networking (00440000-00444000), WinMain and the window procedure, animation file loading, the
  map-editor specs and CLightning: larger, stateful, and mostly built on CString/CArray/stream code
  that is not rewritten yet. Good next candidates: the map database loader (0045EA20) once CStringArray
  parsing exists; CFakeUnit construction (004466D0) with the CArray classes.
