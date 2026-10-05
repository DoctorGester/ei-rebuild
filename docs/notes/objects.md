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
  (`CBaseManager::context_`) is what `ManagerContext` returns; 00455950 calls that object's slot 13.
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

## Second round: lightning bolts (00466190-00468A00)

Files: `src/game/lightning.h/.cpp`; verify: `tests/verify/verify_lightning.cpp`; scenario:
`tests/scenarios/objects_more.txt` (three `CreateLightning` bolts with power 5, 0 and -7 drawn for
3 s, then the lightning spell on a wolf).

### Classes

- `CLightning` (class id 0x1E, vtable 0073C550, 0x514 bytes, created by `CLightningManager` slot 14
  at 00468680): +0x1C / +0x20 two texture references, +0x24 bounds for the renderer (kind 3, centre
  +0x28, radius +0x34, box +0x38 / +0x44 relative to the centre, +0x50 a cleared vector), +0x5C the
  main segment, +0x104 six branches (0xA8 each), +0x4F4 where each branch leaves the main segment
  (0..1), +0x50C power (sign selects the look; 0 = six parallel bolts), +0x510 sway amplitude.
  Vtable slot 2 (00466EA0) resets and registers the bolt, slot 3 (00468790) resets it; 00468810 is
  the destructor body, 004680D0 the initialiser (power, branches, textures "SmtgLight"/"Lightning" or
  "DemonLight"/"DemonLightning" through 00500A00), 00467AD0 draws, 00467590 draws a glow sprite.
- `LightningSegment` (0xA8): start, two displacement axes (`side` +0x0C, `up` +0x18, scaled to the
  bolt's thickness), step, two sets of three noise keys (+0x30, +0x60, newest first), the key time
  +0x90, an aligned CArray of 16-byte points +0x94 (`SetSize` 00468C50 is a library instantiation),
  width +0xA4.
- `NoiseKey` (16 bytes): two rows of the noise table and their weights. The table (0079CE78, 32 rows
  of 400 floats) is built at start-up by 004660B0 / 00465EE0; rows 16..31 belong to the key drawn
  fresh for every sample. `SampleNoise` blends three stored keys and the fresh one with uniform
  B-spline weights of the fraction since the newest key; `LightningSegment::Animate` subtracts the
  straight line between the curve's first and last values so the noise is zero at both ends. The
  curves go through the globals 0079C1F8 / 0079C838 (400 floats each).
- `CLightningManager` (vtable 0073F5B8): its object list (CBaseManager +0x34) holds the bolts; a
  second `CTypedPtrList` at +0xB0 holds a reference to each bolt registered since the last frame.
  00468940 (`DrawAll`, called by the renderer at 0056A252) draws every bolt and drops those
  references. Slot 2 (004689D0) runs the Rebirth-patched 00468A00 once.
- Callers: `CreateLightning` (script, 004BCFF0) and the weather create a client object (class 0x4D,
  setter 0067A6B0); its update 0067A8F0 calls `SetEndpoints` every frame while the bolt follows
  units (lightning spell).

### Rewritten

| Address | Name | Verify |
|---|---|---|
| 00466190 | `NoiseKey::Randomize` | random state saved; original on a copy, state restored, ours; key and generator state compared |
| 004662C0 | `LightningSegment::SampleNoise` | as above, original into scratch curves; segment, both curves, generator compared |
| 00466CE0 | `LightningSegment::Animate` | as above on a segment copy with its own point buffer; points, segment, generator and the global curves compared |
| 00466820 | `LightningSegment::Init` | original on a segment copy (own buffer, capacity 400, so SetSize never reallocates); segment fields and size compared (not the points: a grown array is uninitialised) |
| 00466FD0 | `CLightning::ExpandBounds` | original on a copy of the first 0x50 bytes; box compared |
| 00467150 | `CLightning::UpdateBounds` | original on a byte copy (points shared, read only); whole object compared |
| 004672E0 | `CLightning::Animate` | random state saved; original on a deep copy (seven own point buffers); bolt, all points, generator and global curves compared |
| 00467E10 | `CLightning::SetEndpoints` | as Animate; the points and bounds are compared only when they cannot hold uninitialised elements (the bolt animated, or no real array grew past its capacity), otherwise the fields outside the bounds and every segment's fields |
| 00466EA0 | `CLightning::Register` (slot 2) | ours only: bolt is the new tail of the manager's list, list count and reference count +1, textures cleared, power 1 |
| 00468790 | `CLightning::Reset` (slot 3) | ours only: textures cleared, power 1, amplitude 0.1 |
| 00468940 | `CLightningManager::DrawAll` | ours only: returns 1 and the new-bolt list is empty |

Slots 2 and 3 are non-virtual members with their own names (`Register`, `Reset`) so they do not
override `CEngineObject::Create` / `Destroy`. The x87 code keeps the original's float stores
(`Store`) and holds the values the original keeps on the FPU stack in `double` (spilled as 53-bit,
exact unless the FPU runs at 64-bit precision). All names are set in Ghidra.

### Forwarders and globals

- 00468C50 `LightningPointArray::SetSize` (library, in `config/library.csv` with the other
  CArray instantiations of this block: 00468D60 / 00468D80 / 00468F50 / 00468DA0 an 8-byte CArray
  with vtable 0073C584, 00468F70 aligned allocation, 00468F90 / 00468FE0 ConstructElements).
- 00467AD0 `CLightning::Draw`.
- Globals: random generator 007A9D40, noise table 0079CE78, noise curves 0079C1F8 / 0079C838, branch
  sway frequencies 00787C8C (six pairs), world clock 007AF61C (ticks) / 007AFCA8 (fraction).

### Original behaviour kept

- `LightningSegment::Animate` reads the curve value before the first one (0079C1F4) when the segment
  has no points.
- `SampleNoise` caps the keys added per call at 100 but leaves the key time behind by the rest.

### Skipped (next candidates)

- 004680D0 `CLightning::Init` (random branch layout, texture loading through 00500A00), the factory
  00468680 and destructor body 00468810 (eh vector constructor / destructor iterators), the drawing
  00467AD0 / 00467590 (renderer virtuals), 00466E60 (a copy of `Reset` without callers).
- Animation loading (00446C10-0044C470, `CFakeUnit` scene loading via slot 62 at 00446BA0; scene
  files, .bon/.anm splitting), the map database loader 0045EA20, the map-editor specs (00469630+)
  and networking: not touched this round.

## Third round: scene loading, options, map database, unit record

Files: `src/game/scene_loader.h/.cpp` (00471B10, 00446C10), `src/game/options.h/.cpp` (0045B210),
`src/game/map_database.h/.cpp` (0045EA20), `src/game/unit_record.h/.cpp` (00477B00). Verify:
`tests/verify/verify_scene_loader.cpp`, `verify_options.cpp`, `verify_map_database.cpp`,
`verify_unit_record.cpp`. No new scenario: every scenario starts the game and loads zone1, which
reaches all of these.

Shared files changed (additive): `src/lib/mfc.h/.cpp` (`CString::GetBufferSetLength`, 00700E17),
`src/game/object_tables.h` (`CMapDatabase::Load` declaration).

### What the functions are

| Address | Size | What |
|---|---|---|
| 00446C10 | 4.9 KB | `LoadFakeUnitScene(file, regFile, unit)` (__stdcall; the caller, CFakeUnit slot 62 at 00446BA0, also passes the unit in ECX, unused): opens the .amf scene, loads it (00471B10), requires exactly one top-level node ("Scene ... have more then one top-level object", fatal), reads the figure's .reg (`Ini's\<name>.reg` for scenes under `scenes\`, 0044B980), links the nodes (0044C4A0), builds one figure (0x220 bytes, unit +0x530) per mesh, the skeleton (00448A40; "Model File Name" fatal error), bone links (+0x558), matrices and index arrays, the textures named in the .reg (".bmp" stripped) and their links, then registers the figures and the skin |
| 00471B10 | 12.6 KB | `SceneInfo::Load(file, meshes, helpers, nodes)`: the binary scene reader. Header (0x14: version, record count, skipped count, helper count, a flags word for `this`); per record (0x2C header) a node (0x120: names, offset, rotation / position keys, normals in version 0x1468FCCA, morph keys with a full point set each) and a mesh (0x18C: name, faces, material groups, merged vertices: one vertex per (position, texture) index pair per group, built with two CMap<DWORD,DWORD>; the morph keys are then reordered to the merged vertices); then the skipped records (read and dropped) and the helpers (0x3E header, three names) |
| 0046F1F0 | 10.4 KB | an older copy of 00471B10 (no version check); no references in the exe: not rewritten |
| 0045B210 | 4.9 KB | `GameOptions::LoadFromRegistry(path)` (options object 0079BC70, called once by WinMain): ~120 values under HKCU\<path>\<subkey> through the game's registry key helper (006972A0-006988A0), each with the field's current value as the default |
| 00459100 | 4.7 KB | the same options from a .reg file (reader 00695B80); no references: not rewritten |
| 0045EA20 | 4.1 KB | `CMapDatabase::Load(file)`: map.txt / map-LMP.txt (0045EA00 loads both; 005810A0 loads one) read through the text manager (007AF788); the directives are listed in map_database.h |
| 00477B00 | 0.8 KB | `UnitRecord::ApplyTo(unit)`: the world server's unit record (005927C0): stats, controller fields, protection from the prototype model, quick / carried items, spells, armour, weapons. A record without stats (+0x10 / +0x14 floats 0 and +4 0, or +0x110 0) only gives the items |

### Rewritten

| Address | Name | Verify |
|---|---|---|
| 00471B10 | `scene::SceneInfo::Load` | recorded file: ours reads through a proxy that records every Read (slot 13); the original then runs on empty copies of the three arrays against a replaying proxy (same sizes in the same order); meshes, nodes and helpers compared in depth (all bytes but heap pointers; names, vertex / face / group / key arrays by content; the never-written tail +0x24..+0x33 of mesh faces skipped). Arrays not empty on entry: ours only |
| 00446C10 | `scene::LoadFakeUnitScene` | ours only: one figure and one bone-link array per mesh, figure names equal to the mesh names, unit +0x1C = (0, 0, 500) |
| 0045B210 | `options::GameOptions::LoadFromRegistry` | original on a copy of the 0x26C loaded bytes (CString fields copy-constructed, destroyed afterwards), ours on the real object; bytes and string texts compared |
| 0045EA20 | `CMapDatabase::Load` | a shadow database (original constructor 0045E820) loaded by the original alongside the real one on every call, compared in depth (allods with their map lists, maps with their exits, strings by text). A database first seen non-empty: ours only |
| 00477B00 | `unitrecord::UnitRecord::ApplyTo` | ours only: the byte blocks the record sets (controller +0x270, +0x8A8); its other fields can be recomputed by the equipment calls that follow |

All names are provisional and set in Ghidra (also 00448A40 BuildSkeleton, 0044B980
LoadFigureRegistry, 0044C4A0 LinkNodes, 0044E3B0 PlaceMeshes, the RegistryKey getters).

### How the code is organised

- The scene loader's inlined `CArray::SetSize` is one helper (`SetSizeInline`, given the
  instantiation's ConstructElements / DestructElements forwarders) plus `SetSizeAligned` for the
  32-byte aligned vertex arrays (allocators 00477250 / 00452C20 / 00476EF0 / 00406B50). The template
  code the original calls out of line (SetAtGrow, Add, Copy, CMap GetAssocAt 00477340 and
  operator[] 00476050, the local arrays' constructors and destructors) is reached through the
  original addresses. The inlined `CMap::operator[]` is the same code as 00476050 (InitHashTable on
  first use, NewAssoc), which is called instead.
- Floats the original copies through the FPU (FLD / FSTP) go through `FpuCopyBits`; products the
  original keeps on the FPU are stored by small naked helpers (`StoreUnsignedProduct`,
  `StoreRadians`, `StoreScaledTwice`) so they are rounded once, as in the original.
- `LoadFakeUnitScene` calls the scene loader through 00471B10, so the verify wrapper sees it.
- Registry value names, map.txt directives and message formats are passed by their original
  addresses.

### Forwarders added (the standalone build must replace)

- Scene: element helpers 004545C0 / 00476F50 / 00454F80 / 00477100 / 00477130 / 00477160 /
  00477190 / 00454DB0 / 00476F20 / 004771C0 / 004771F0 / 00477220 / 00477530 / 00454ED0 / 00454F00 /
  00452DC0 / 00454F30 / 00453700 / 00477380; arrays 00452AF0, 0044F410, 0044F1D0, 00474C70,
  0044F1B0, 00476DD0, 0044FBB0, 00474C80, 00453100, 0044FB90, 004773F0, 00476640, 00476780,
  00474DB0, 00474C50, 00453550, 006FF29A; maps 00477340, 00476050, 00476140; local destructors
  00474F30, 00475110, 004752F0, 00454CE0, 00475680, 004759E0, 00475D40, 00476AB0, 004763C0; skipped
  records 00475500, 00475860, 00475BC0, 00476930, 00475520, 00475880, 00476950, 00475BE0, 00475F10,
  00475F40, 00475F20.
- LoadFakeUnitScene: 00453240 / 00453210, 004534F0 / 004534C0, 00406B50, 00450370, 0044FCD0,
  00450F20, 0044F960, 004543E0, 0044C490, 00694520 / 00694540, 00455940, 0070AC94 (AfxMessageBox),
  0044B980, 0044C4A0, 00448A40 (__fastcall: meshes, nodes, frame count, skin), 0044E3B0, 0044DCF0,
  0044CFF0, 0044E8A0, 0051B2F0, 0051B470, 0068EAF0, 005A6D40, 005164B0, 00508BF0, 00508990;
  CString Find / Left / Right (006FDBD9 / 006FDAE9 / 006FDA6D; 006FDA6D is `Right`, not `Mid`).
- Options: 006972A0 / 00697340 / 006973A0 / 006972B0 (registry key), 00698610 / 00698570 (int; two
  identical copies, each called where the original calls it), 00698750 (float in ST0), 006988A0
  (string; takes the default CString by value and destroys it), 00700E91 (Find(TCHAR)), 006E80D8
  (atoi), 006E853C (_ftol, inside a helper).
- Map database: text manager 004DEFC0 / 004DF030 / 004DFCB0 / 004DF020, 00460720 / 004606F0,
  004607E0 / 004607B0, 004606A0 / 00460670, 00460500, 006E911A (atof), 006E80D8.
- Unit record: 005315B0, 00530970, 00531180, 00531220, 00531820, 00530730, 00531070, 00525A00,
  00525AF0, 006AC970 (patched by Rebirth), 006AD1E0, 006E79A0 (memmove).

### Original behaviour kept

- `LoadFakeUnitScene` passes the scene flags to 00448A40 as a dword minus one whose upper word the
  original never initialises (ours: 0). Every consumer reads only the low word (MOVSX), which is the
  same.
- The scene loader allocates one CMap per material group and never uses them; the skipped records
  read only their names, keys, normals and morph keys (non-zero point / texture / face / group
  counts there would desynchronise the file).
- `LoadFakeUnitScene` builds three CStrings, a CStringArray and a CFileRegistry it never uses.
- Options: "Joystick" always defaults to 0; "LightingFreq" is read under two subkeys into two
  fields; the light colours are converted 0..1 <-> 0..255 with a truncating `_ftol`; the camera
  flips are stored as -1 / 1 floats; "Difficulty Level" goes to both +0x250 and +0x254.
- Map database: the map index continues from the last map of the earlier file (lines before the
  first "#zone" go to it), the exit index restarts with every "#zone"; "#allod" reads word 0 of its
  five following lines even when a line has no words; "#view" / "#deployangle" append a "0" word so
  an empty line gives 0.

### Skipped

- 0046F1F0 and 00459100: no references in game.exe (searched for absolute addresses and relative
  calls / jumps), so they never run.
- The template instantiations listed above (forwarders), and 00446BA0 (CFakeUnit slot 62, a short
  wrapper).

## Fourth round: options writer, application helpers

Files: `src/game/options.cpp` (0045C560 added), `src/game/app.h/.cpp`. Verify: `verify_options.cpp`
(writer added), `verify_app.cpp`. No new scenario: every run starts the game (window procedure,
input dispatch, message pump, StartFirstMap) and quits (the writer runs on exit).

| Address | Size | Name | Verify |
|---|---|---|---|
| 0045C560 | 3650 | `GameOptions::SaveToRegistry` | both run: the RegCreateKeyExA / RegSetValueExA import slots point at recorders; the original's writes are only recorded, ours are passed on to whatever the slots held (the harness's skip stub); the event lists (subkey; name, type, data bytes) must match |
| 00457D40 | 705 | `app::WindowProc` | ours only: WM_ACTIVATE flags (0079BC5C active, 0079BC5D seen), results of the handled messages |
| 00457970 | 917 | `app::DispatchInput` | ours only: input messages return 1 |
| 00458220 | 80 | `app::PumpMessages` | ours only, counted |
| 00456140 | 537 | `app::StartFirstMap` | ours only: result 1 when no map is given on the command line |

The writer uses the loader's value tables: every value goes through the setter matching its getter
(GetInt -> SetInt 00697D40, GetDword -> SetDword 00697CA0, floats 00697E80 as REG_BINARY bits,
strings 00697FC0 taking a CString by value).

### Forwarders added (the standalone build must replace)

- Registry setters 00697CA0 / 00697D40 / 00697E80 / 00697FC0.
- Interface input (007C7D50): 005F5AB0, 005F76A0, 005F76E0, 005F7740, 005F7780, 005F77C0, 005F7800,
  005F7840, 005F7880, 005F78C0, 005F7B40, 005F7BE0, 005F8C50. Console ostream 006FC14E (int),
  006FC91C (long), 006FC0B0, 006FC31D, 00434FE0.
- Activation: 005E5270, 004EEC60, 00504780, 0065EC70, 006B7320 / 006B7380, 005DAE70.
- Start: 00581660, 0057C700 (Rebirth), 00595480 (Rebirth), 0058DC90, 00566CE0, 005699F0, 004D36F0
  (rewritten; through its address), 00456360, FatalError 00480080 (Rebirth).

### Original behaviour kept

- Writer: "Joystick" is always written as 0; "Difficulty Level" from +0x254 (the loader fills both
  +0x250 and +0x254); colours _ftol(value * 255) truncated.
- WindowProc's WM_ACTIVATE passes 005DAE70 a second stack argument built from AL only: its upper
  bytes are those of the device pointer (0x007AF080), which ours rebuilds. States above 2 are ignored.
- DispatchInput logs every WM_SYSKEYUP to the console ("<key>:<lParam>"); Alt+Tab is turned into a
  WM_ACTIVATE with the window's iconic state; keyboard positions use an arithmetic shift, mouse ones
  a logical shift (as the original).
- PumpMessages hands messages straight to WindowProc (no TranslateMessage / DispatchMessage).

### Skipped (with reasons)

- 0045A380 (3722, the options written to a .reg file) and 00459100: no references (dead).
- 00458870 (GameOptions constructor): runs from a static initializer, before the hooks are
  installed at WinMain, so a replacement would never run. 00458DA0 (defaults reset) is only reached
  from 00455B50/00455BA0 (a 1% random branch in CPlayerClient::OnNewObject); not done.
- 00458010 (frame present + screenshot), 00458270 (startup movies; the harness empties the list),
  0045E310 (DirectX version probe), 00458570 (STLport hash_map registration): mapped (see the
  disassembly notes in app.h's neighbours), not rewritten this round.
- The skeleton / figure building callees of LoadFakeUnitScene (00448A40, 00449240, 004492F0,
  0044EB60, 0044CFF0, 0044DCF0, 0044E3B0, 0044E8A0, 0044C4A0, 0044B980, 0044A2A0, 0044A7B0, 0044B060):
  heavy x87 with inlined CArray SetSize; still forwarders. Largest remaining work in the range.

## Fifth round: the fake unit scene's helpers

Files: `src/game/scene_registry.cpp` (0044B980), `src/game/scene_meshes.cpp` (0044CFF0),
`src/game/scene_bones.cpp` (004492F0); verify: `tests/verify/verify_scene_registry.cpp` (all three).
No new scenario: every scenario loads zone1, which loads the fake unit scenes. Shared files: none.

### Rewritten

| Address | Name | Verify |
|---|---|---|
| 0044B980 | `scene::LoadFigureRegistry` (__fastcall: unit, .reg path; RET 8) | original first; its results recorded (every mesh's texture coordinates and +0x184 / +0x188, the records it added to both arrays), the coordinates restored and the arrays cut back, then ours; all compared |
| 0044CFF0 | `scene::IndexMeshCorners` (__fastcall: the mesh array) | original first, then ours over its results (the tables depend on the faces only); every mesh's +0x108 / +0x118 / +0x128 compared |
| 004492F0 | `scene::BuildBone` (__fastcall: skeleton, bone; node, frames; RET 8; recursive) | ours only (it appends to the skeleton and creates the child bones): name, frame count, every array's size against the node's counts, the children's parent links |

### LoadFigureRegistry (0044B980)

The scene's .reg file (CFileRegistry through its game addresses; "Can't open .reg file" is
fatal, no sections returns 0). For every mesh (unit +0x544, 0x18C each) a local CMap<DWORD,DWORD>
(vtable 0073C16C, 17 buckets) remembers the texture coordinates already moved. Each face group
(mesh +0x148) named exactly like a helper (unit +0x56C, name +0x1C, `_mbscmp`): the texture
section is the file section equal (ignoring case, 00447F50) to the helper's +0x8C name, its "Main
Texture Name" (default "none") names the mapping section; the texture size is "Dimentions"
Width / Height (default 256). An empty mapping name gives the rectangle (0, 0, 0, 0); a mapping
with any of X, Y, Width, Height missing (-1) logs "CorrectMappings ..." (message box with the debug
flag 0x10000 of 007AF5C0) and gives (0, 0, 0, 0) too. Each corner's texture coordinate (face +0xC
indices into +0xE8) not yet in the map becomes u = (w / W) u + x / W, v = 1 - ((h / H)(1 - v) +
y / H) and is marked. After the groups, the material type of the last group name seen (0044A2A0;
the name carries over from the previous mesh when a mesh has no groups) goes to the mesh (+0x188
type, +0x184 value) and, once each, to the caller's (type, value) array and (type, texture name)
array; "Undefined texture/material types" is logged otherwise.

- The coordinate formula is a naked transcription (the quotients and products stay on the FPU;
  its x87 sequence was compared with the original's); the integers become floats once (FILD /
  FSTP).
- The map's operator[] is inlined twice by the original (once through GetAssocAt 00453C60, once
  with its own bucket search); both are kept, with InitHashTable 00453B90 / NewAssoc 00453C00. Its
  destructor is inlined: the table deleted (even when null), the blocks freed (006FE90F).
- The (type, name) array's SetSize is the inlined MFC CArray one (elements constructed by
  004553F0, destroyed by 00454360); the (type, value) array's is called (00453A30).
- Strings passed by value (00447F50, the registry's GetString default) are copied into a raw slot
  the callee destroys, as in options.cpp.

### BuildBone (004492F0)

Called for the scene's root node by 00449240 (which creates the root bone) and for every child.
The bone (0x128 bytes, constructed by 00506710) is appended to the skeleton's list (+4, SetAtGrow
00452C40) and takes the node's name (+4), the frame count (low word of the argument, +0x40), the
rotation keys (node +0xA8 -> bone +0x58, 32 bytes: frame, frame + 1, the three floats, and at
+0x14 the step to the next key, b - a; the first key starts with the vector at 0079B9B0), the
position keys (node +0xBC -> +0x44, 40 bytes: frame, frame + 1, the floats at +0x18, the node
record's +8 dword at +0x24), a zeroed 12-byte array per normal (+0x6C), node +0x98..+0xA4 to
+0xE4 and +0x8C..+0x94 to +0xF4. With morph keys (node +0xD0): two zeroed point arrays the size of
the first key's points (+0x100, +0xD4) and one morph frame (+0x84, 24 bytes: frame, frame + 1,
points) per position key: frames before a key interpolate from the previous key ((f - prev) /
(key - prev), FILD / FIDIV; the frames before the first key copy it), the frame at a key goes
towards the next key through the vector helpers 0044A230 / 00448A10 / 0040CDC0 (the last key is
copied, 0044F410). Without morph keys the frames are emptied (0044F7E0). Then the children (+0x20,
node +0x110): a new bone each, its parent (+0x1C) set, built recursively (through 004492F0, so the
verify wrapper sees every bone). A pivot (node +0x54) not (0, 0, 0) is fatal ("node %s have
NONULL pivot", zero or NaN counts as 0).

- The five inlined CArray::SetSize copies are three helpers (aligned arrays with 00452DA0 /
  00452D80 / 00452C20; the 12-byte CArray zeroed by 00452DC0; the morph frames constructed by
  00452E20 and destroyed by 00452DF0). The morph frame array is sized twice (key count + 1, then
  the position key count), as in the original.
- Each float operation is one x87 operation rounded to a float (`Store`); the frame ratio is a
  naked FILD / FIDIV.

### IndexMeshCorners (0044CFF0)

Per mesh (0x18C each): every face corner's key ((merged vertex << 16 | position) << 16 | texture
vertex, 64 bits) goes into a CMap (vtable 0073C190, 64-bit values); its entries, in the map's
iteration order, become 16-byte records (the three low words as ints and the running index; a
second map 0073C184 maps the keys to those indices, never read), qsort-ed with 0044CF00 (by the
three ints) into the mesh's corner table (+0x108, three words per entry, SetSize inlined with
00452E90). Each face corner's position in that table (-1 when absent; linear search) is appended
to +0x128 (cleared first). The distinct (merged vertex << 16 | position) pairs (third map,
0073C178) become dwords with the two words swapped, qsort-ed with 0044CF60 (unsigned words),
padded to a multiple of four with the last vertex and positions counting up from the last one,
and copied to +0x118. The record list (CArray, vtable 0073C19C) is shared by all meshes (SetSize 0
per mesh) and deleted at the end; the first map's destructor is called (00453F80), the other two
are inlined, after the pair list is freed.

- The maps are walked with GetNextAssoc inlined (the next association is found before the current
  one is used); their GetAssocAt / InitHashTable / NewAssoc are the original instantiations, so the
  hashing and the iteration order are the original's. qsort is the game's (006E88FC) with the
  original comparators by address.

Forwarders: 00450750, 00447F50, 0044A2A0, 00453A30, 004553F0, 00454360, 00453C60, 00453B90,
00453C00 (registry); 004540A0 / 00453FD0 / 00454040 / 00453F80, 004541C0 / 004540F0 / 00454160,
004542E0 / 00454210 / 00454280, 00453CA0, 00453E00, 00452E90, 0044FA90, 00452FC0, 00452EB0,
006E88FC (corners); 00452C40, 00452DA0, 00452D80, 00452C20, 00452DC0, 00452E20, 00452DF0, 0044F7E0,
0044F1D0, 0044F410, 0044F540, 0044A230, 00448A10, 0040CDC0, 00506710 (bones); the registry 00694520 / 00694540 / 006945A0 / 00694880 / 00694BE0 / 00694D20 / 006946B0
(rewritten in registry.cpp, called through their addresses).

## Sixth round: 00440000-00480000 (scene, fake unit, lightning, zone records)

Part of the 0x00440000-0x00480000 worker's round; the application, network and table functions of
the range are in unmapped_410_470.md ("Ninth round"). Plain float maths (rule 7) throughout.

### Rewritten

| Address | File | What |
| --- | --- | --- |
| 0044C4A0 | scene_weld.cpp | the scene's vertex welding (LinkNodes) |
| 0044DCF0, 0044E3B0, 0044E8A0 | scene_normals.cpp | normals, packed positions (PlaceMeshes), mesh bounds |
| 0044EB60 | scene_bone_frames.cpp | skeleton frames made relative to the parent bones |
| 00448A40, 00449240 | scene_skeleton.cpp | the skeleton from the nodes and the root bone |
| 00449030, 004491C0 | scene_skeleton.cpp | the node hierarchy (children at node +0x10C: nodes whose parent name, +8, equals the node's name, +4; _mbscmp), the root (the last node with an empty parent name) and the rest pose copy (pivot +0x70 -> +0x8C, rotation +0x7C -> +0x98) from the root's children down |
| 0044A2A0 | scene_material.cpp | the material name parser |
| 004466D0, 00446910, 004468F0, 00446B20, 00446BA0, 004468E0 | fake_unit.cpp | CFakeUnit (0073BFE8): constructor, destructor, deleting destructor, Destroy (slot 3), the scene load (slot 62: 00446C10, then each figure gets a class 0x45 object from the world's slot-21 manager, 0059F390), class id |
| 00453450-0044C480 (11) | figure_slots.cpp | CFigure / CFakeFigure class names and ids, destructors, clearing constructors |
| 0044F1B0-00454A30, 00474C80, 00475F40, 00476DD0 | aligned_arrays.cpp | the engine's plain-element arrays in 32-byte aligned blocks (SetSize, Add, SetAtGrow, copy, allocation, destructors), one template per operation |
| 004680D0, 00467AD0, 00467590, 00468680, 00468810 | lightning_start.cpp, lightning_draw.cpp | the bolt's start, its drawing and glow, the manager's bolt factory, the bolt's destructor body (lightning.h: `Start` declared) |
| 00461CB0 | hoover_loot.cpp | a dead unit's loot |
| 00469CA0-0046E490 | map_spec.cpp, map_spec_unit.cpp, map_spec_effect.cpp | the zone file's records (map_spec.h): base, object, flame, magic trap, lever, unit (with behaviours, patrols, waypoints), particle source, light, sound; their chunk readers / writers |
| 0046A4B0, 0046E5E0 | map_spec.cpp | the editor's quaternion rotation; the zone's diplomacy table constructor |
| 00458DA0, 00458730 | options_defaults.cpp | the options' defaults and destructor |

### Verify wrappers

verify_scene_weld, verify_scene_normals, verify_scene_bone_frames, verify_scene_skeleton,
verify_scene_material, verify_lightning_start, verify_lightning_draw, verify_hoover_loot,
verify_map_spec, verify_options_defaults (differential: the defaults' bytes and strings),
verify_aligned_arrays (one instantiation per template), verify_fake_unit (ours only: the vtable and
four empty arrays after construction; the figures emptied by Destroy).

### Original behaviour kept

- 00449030: without a root node (-1) the original copies the rest pose of the element before the
  node array and walks its children; kept.
- CFakeUnit's destructor walks the figure array after SetSize(0, -1) (so never) dropping each
  figure's count at +4; kept as written.

### Classified as library

The CArray / CMap members around the animation and figure loaders, ConstructElements with inlined
element constructors (00453240, 004545C0, 00454F80), CArray::ElementAt 00450750, the scene
loader's CArray Serialize / Add / SetAtGrow and CMap InitHashTable / operator[] (00476430,
00476640, 004773F0, 00477270, 00476050).
