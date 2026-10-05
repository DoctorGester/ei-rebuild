# World objects and shared helpers (00440000-00480000)

Code: `src/game/object.*` (CEngineObject helpers, CTransformObject / CHoover constructor bodies, unit and
figure getters, CMemoryStream), `object_diplomacy.*`, `object_maths.*`, `object_tables.*`,
`lightning*.cpp`, `scene_*.cpp` (fake unit scenes), `fake_unit.cpp`, `figure_slots.cpp`,
`aligned_arrays.cpp`, `options*.cpp`, `app.*`, `map_database.*`, `map_spec*.cpp`, `unit_record.*`,
`hoover_loot.cpp`. Verify: `tests/verify/verify_<file>.cpp` for each. Scenarios: `objects_more.txt`
(lightning bolts, power 5 / 0 / -7, then the lightning spell on a wolf); every other scenario loads
zone1, which reaches the scene, options, map database and unit record code. Network, table and
application functions of the range also appear in `unmapped_410_470.md`. Names are provisional.

## Address map

| Range | Contents |
|---|---|
| 00440000-00446000 | network sessions (UDP/TCP, "CONNECTION LOST"), debug text overlay, STLport helpers |
| 004466D0-00446C10 | CFakeUnit (0073BFE8, id 0x50, 64 slots; embeds four CArrays, vtables 0073C0E4..0073C108) |
| 00447F50-0044C470 | .anm/.bon animation loading and splitting, CArray instantiations; 0044C470 `DebugLog` stub (81 callers) |
| 0044F000-00455400 | CArray / CMap template instantiations; 00453240-00454A70 CFigure / CFakeFigure helpers |
| 004554B0-00455800 | CDiplomacy; 00455930-00455980 CEngineObject base (destructor body, ManagerContext, SetOwner, Release in base_manager.cpp) |
| 00456440 | WinMain; 00457D40 window procedure; 00458220 message pump |
| 0045B210-0045C560 | game options (global 0079BC70): registry load / save |
| 0045D5B0-00460BA0 | map database (map.txt / map_LMP.txt), name -> float hash tables |
| 00461360-004613A0 text keys; 00461C10-00461CA0 CHoover; 004621A0-00462A00 CInventory (vtable 0073C474; slots 16-21 shared with CItem); 00462F70 key -> command table |
| 00466190-00468A00 | CLightning (0073C550) and CLightningManager (0073F5B8) |
| 00469630-0046F160 | map-editor object specs (base vtable 0073C5C4, then 0073C5F0; magic trap 0073C650, fire 0073C620, particle source 0073C758, light 0073C788, sound 0073C7B8) saved through the chunked stream at 0042BF50 |
| 00471B10, 00477B00 | scene reader; `UnitRecord::ApplyTo` |

## Facts

- CEngineObject inline constructor pattern: refCount_ = 0, id_ = -1, unknown_18 = type_ = flags_ =
  owner_ = 0. `owner_` is the manager; the manager's +0xAC (`CBaseManager::context_`) is what
  `ManagerContext` returns (00455950 calls its slot 13). Base vtable 0073B8E0 (13 slots), restored by
  00455930. 156 engine-object vtables exist (runs in .rdata with slot 1 00402D40 and slot 4 00455980;
  class name slot 11, id slot 12).
- CFakeUnit and CUnit (0073EA8C, id 0x50, 77 slots) share: slot 7 GetName (+0x238 CString), 29 / 43
  true, 31 / 56 / 57 / 58 forward to the controller at +0x240 (its slots 20, 42, 53, 31), 47 forwards to
  +0x170 slot 19, 54 returns +0x278 ("large unit"), 55 +0x274 (skip unit layers), 59 calls 0052F920,
  60 float +0x328, 61 assigns the name. CFigure slot 7 GetName = +0x120 (also CSkyObject, CI3DFigure),
  CFakeFigure slot 7 = +0x94.
- `ObjectListIterator` {manager, position} walks a manager's CPtrList at +0x34 (00404920 / 00404960) and
  holds a reference on the manager; the world object manager is the pointer at 007AFFA0.
- CUnit fields (`unitfield::`): +0x238 name, +0x240 controller, +0x244 hostile mask (bit p: hostile to
  player p; set by 0052E970, or-ed by 0052E940, both also set flags_ 0x80000), +0x24C party (members in
  a hash_map at +0x40), +0x250 player, +0x274 / +0x278 flags, +0x328 float.
- CDiplomacy (global 007C2BC4, 0x100 bytes): `allied_[32]` (+0), `hostile_[32]` (+0x80), bit q of row p.
  Relation codes 0 allied, 1 neutral, 2 hostile (script GetDiplomacy maps them to 1 / 0 / -1).
- CMemoryStream (vtable 0073BD90, 0x28 bytes): +5 ok, +0x10 grow step, +0x18 data, +0x20 read position,
  +0x24 end; slot 1 Read (004A5380), slot 2 Write.
- CMapDatabase (global 0079BF00, ctor 0045E820): allods (0x2C records) at +0, maps (0x5C) at +0x14.
- Getters and constructors registered as `__fastcall` free functions are thiscall methods with no stack
  arguments; their classes' virtuals are not declared and the CEngineObject virtual names must not be
  reused by non-virtual members.
- Lightning: CLightning 0x514 bytes (id 0x1E, created by manager slot 14 at 00468680; slot 2 00466EA0
  `Register`, slot 3 00468790 `Reset`, so they do not override Create / Destroy); noise table 0079CE78
  (32 rows of 400 floats, built at start-up by 004660B0 / 00465EE0), curves 0079C1F8 / 0079C838, random
  007A9D40, branch sway 00787C8C, world clock 007AF61C / 007AFCA8. The manager's second list at +0xB0
  holds the bolts registered since the last frame; 00468940 `DrawAll` (called by the renderer at
  0056A252) draws and drops them; slot 2 (004689D0) runs the Rebirth-patched 00468A00 once. Callers:
  script `CreateLightning` (004BCFF0) and the weather create a client object (class 0x4D, setter
  0067A6B0) whose update 0067A8F0 calls `SetEndpoints` every frame while the bolt follows units.
- **Scene loader**: `LoadFakeUnitScene` (00446C10, __stdcall, from CFakeUnit slot 62 at 00446BA0) opens the
  .amf scene, loads it through `SceneInfo::Load` (00471B10), requires one top-level node, reads the
  figure's .reg (`Ini's\<name>.reg` for scenes under `scenes\`), links nodes, builds one 0x220-byte
  figure per mesh (unit +0x530), the skeleton (00448A40), bone links (+0x558), textures from the .reg.
  The scene reader's inlined `CArray::SetSize` is `SetSizeInline` / `SetSizeAligned`; out-of-line
  template code is reached through original addresses (verify wrappers see the calls); floats the
  original copies through the FPU go through `FpuCopyBits`, products it keeps on the FPU through small
  naked helpers.
- **Options**: ~120 values under HKCU\<path>\<subkey> through the game's registry helper (006972A0-
  006988A0); the writer uses the setter matching each getter (00697D40 int, 00697CA0 dword, 00697E80
  floats as REG_BINARY bits, 00697FC0 strings). 00458870 (GameOptions constructor) runs from a static
  initializer before the hooks exist; 00458DA0 (defaults) is reached only from 00455B50 / 00455BA0 (a 1%
  random branch in CPlayerClient::OnNewObject).
- **Unit record**: `UnitRecord::ApplyTo` (00477B00) applies the world server's unit record (005927C0):
  stats, controller fields, protection from the prototype model, items, spells, armour, weapons; a
  record without stats gives only the items.

## Rewritten

"Pure" = original and ours on the same input (floats as bits). "Copy" = the original runs on a scratch
copy of the bytes the function touches, ours on the real object, bytes and result compared.

| Address | Name | File | Verify |
|---|---|---|---|
| 00455940, 00455970, 00455930 | CEngineObject::ManagerContext, SetOwner (returns old owner), DestructBase | object.cpp | pure / copy |
| 00453400, 00461C10, 00461C80 | CTransformObject::ConstructBase (0x54 bytes), CHoover::Construct, Destruct | object.cpp | copy (Destruct's tail jump reaches the 00455930 wrapper) |
| 00446820, 00446880, 00446890, 004468A0, 00453440, 004547C0 | UnitGetName, UnitGetFlag274 / 278, UnitGetValue328, FigureGetName, FakeFigureGetName | object.cpp | pure |
| 00468C20 | CMemoryStream::Reset | object.cpp | copy (0x28 bytes) |
| 004554B0, 00455630, 00455660, 004557C0, 004557F0 | CDiplomacy Reset, GetRelation, GetHostileMask, ReadFrom, WriteTo | object_diplomacy.cpp | copy / pure / recorded stream calls |
| 004554E0 | CDiplomacy::SetRelation | object_diplomacy.cpp | table: original on a copy without the unit pass; with applyToUnits every unit of the player has the other's bit exactly when relation is 2 |
| 00455680 | CDiplomacy::OnAttack | object_diplomacy.cpp | ours only: allied victim unchanged, else victim and party members (to the first empty entry) get the bit and flag 0x80000 |
| 004489D0, 0044A230, 0044CC10, 00448A10, 004679B0, 004679D0 | CrossProduct, SubtractVectors, SubtractVectors2, ScaleVector copy (byte-identical to 00410770), Vector3::DivideInPlace, Vector4::FromPoint | object_maths.cpp | pure / copy |
| 00461360, 004613A0, 0045E850, 0045E910, 00460AF0, 00462F70 | TextKeyFromName, IsTextKey, CMapDatabase::FindMap / FindAllod, NamedFloatTable::Get, FindCommandByKey | object_tables.cpp | pure (NamedFloatTable::Get needs the game's CRT heap: in game only) |
| 00460BC0, 00460DB0, 00460EE0, 00460FE0, 00461260 | NamedFloatTable Set, Remove, CopyPrefixed, Load, Save (details in `unmapped_410_470.md`) | object_tables.cpp | `verify_named_float_table.cpp` |
| 00466190, 004662C0, 00466CE0, 00466820, 00466FD0, 00467150 | NoiseKey::Randomize, LightningSegment SampleNoise / Animate / Init, CLightning ExpandBounds / UpdateBounds | lightning.cpp | original on a copy (random state saved and restored; deep copies with own point buffers where it allocates); fields, points, generator, global curves compared |
| 004672E0, 00467E10 | CLightning::Animate, SetEndpoints | lightning.cpp | as above; SetEndpoints compares points and bounds only when no uninitialised elements are possible |
| 00466EA0, 00468790, 00468940 | CLightning::Register, Reset, CLightningManager::DrawAll | lightning.cpp | ours only (list / reference counts, textures cleared, power 1; DrawAll returns 1, list empty) |
| 004680D0, 00468680, 00468810, 00467AD0, 00467590 | CLightning start (power, branches, textures "SmtgLight" / "Lightning" or "DemonLight" / "DemonLightning" via 00500A00), factory, destructor body, draw, glow | lightning_start.cpp, lightning_draw.cpp | see `verify_lightning_start / _draw` |
| 00471B10 | SceneInfo::Load | scene_loader.cpp | recorded file: ours reads through a recording proxy, the original then runs on empty arrays against a replaying proxy; meshes, nodes, helpers compared in depth (heap pointers and never-written tail +0x24..+0x33 of mesh faces skipped); non-empty arrays on entry: ours only |
| 00446C10 | LoadFakeUnitScene | scene_loader.cpp | ours only: one figure and bone-link array per mesh, names equal, unit +0x1C = (0, 0, 500) |
| 0044B980 | LoadFigureRegistry (.reg: texture mappings, material types) | scene_registry.cpp | original first, results recorded and undone, then ours; all compared |
| 0044CFF0 | IndexMeshCorners | scene_meshes.cpp | original first, then ours over its results; +0x108 / +0x118 / +0x128 compared |
| 004492F0 | BuildBone (recursive) | scene_bones.cpp | ours only: name, frame count, array sizes, parent links |
| 0044C4A0, 0044DCF0, 0044E3B0, 0044E8A0, 0044EB60, 00448A40, 00449030, 004491C0, 00449240, 0044A2A0 | vertex welding, normals, PlaceMeshes, mesh bounds, relative frames, skeleton, node hierarchy / rest pose, root bone, material name parser | scene_weld / _normals / _bone_frames / _skeleton / _material.cpp | see `verify_scene_*` |
| 004466D0, 00446910, 004468F0, 00446B20, 00446BA0, 004468E0 | CFakeUnit ctor, dtor, deleting dtor, Destroy (slot 3), scene load (slot 62: 00446C10, then a class 0x45 object per figure from the world's slot 21, 0059F390), class id | fake_unit.cpp | ours only: vtable, four empty arrays, figures emptied by Destroy |
| 0044C480-00454800 (11) | CFigure / CFakeFigure names, ids, destructors, clearing constructors | figure_slots.cpp | |
| 0044F1B0-00454A30, 00474C80, 00475F40, 00476DD0 | the engine's plain-element arrays in 32-byte aligned blocks (SetSize, Add, SetAtGrow, copy, allocation, destructors) | aligned_arrays.cpp | one instantiation per template |
| 0045B210, 0045C560 | GameOptions::LoadFromRegistry / SaveToRegistry | options.cpp | load: original on a copy of the 0x26C bytes (CString fields copy-constructed), bytes and texts compared; save: the registry import slots point at recorders, event lists (subkey; name, type, data) must match |
| 00458DA0, 00458730 | options defaults, destructor | options_defaults.cpp | differential (bytes and strings) |
| 0045EA20 | CMapDatabase::Load (map.txt / map-LMP.txt through the text manager 007AF788; directives in `map_database.h`) | map_database.cpp | a shadow database (ctor 0045E820) loaded by the original alongside on every call, compared in depth; a database first seen non-empty: ours only |
| 00477B00 | UnitRecord::ApplyTo | unit_record.cpp | ours only: the byte blocks the record sets (controller +0x270, +0x8A8) |
| 00457D40, 00457970, 00458220, 00456140 | app::WindowProc, DispatchInput, PumpMessages, StartFirstMap | app.cpp | ours only: WM_ACTIVATE flags (0079BC5C active, 0079BC5D seen), input messages return 1, counted, result 1 without a command-line map |
| 00461CB0 | a dead unit's loot | hoover_loot.cpp | |
| 00469CA0-0046E490, 0046A4B0, 0046E5E0, 0045D5B0..0045DFB0 | the zone file's records (base, object, flame, magic trap, lever, unit with behaviours / patrols / waypoints, particle source, light, sound) and their chunk readers / writers, editor quaternion rotation, zone diplomacy table constructor | map_spec*.cpp | `verify_map_spec` |

`FindCommandByKey` (00462F70) loops back to 00462F73 inside the 5 bytes the hook's jump overwrites, so
its wrapper copies the whole 61-byte body (no calls, short internal jumps) to executable memory when
the DLL loads, before the hooks are installed, and runs that copy as the original (`stolenBytes` 0).
Constructors allocate nothing, so the original runs safely on a copy.

## Original behaviour kept on purpose

- `CDiplomacy::SetRelation` with a relation outside 0..2 leaves the table alone but still clears the
  bit in the units' hostile masks (the unit pass only checks for 2); player indices are not checked.
  `GetRelation` does not check its indices (`GetHostileMask` does). `OnAttack` stops at the first
  party entry with a null unit and sets the victim's 0x80000 flag a second time after 0052E940 did.
- `CMemoryStream::Reset` drops the buffer pointer without freeing it and `ConstructBase` leaves the
  positions uninitialised (only used from constructors).
- `LightningSegment::Animate` reads the curve value before the first one (0079C1F4) when the segment
  has no points; `SampleNoise` caps the keys added per call at 100 but leaves the key time behind by
  the rest.
- `LoadFakeUnitScene` passes the scene flags to 00448A40 as a dword minus one whose upper word the
  original never initialises (ours 0; consumers read the low word); it builds three CStrings, a
  CStringArray and a CFileRegistry it never uses. The scene loader allocates one CMap per material
  group and never uses them; the skipped records read only names, keys, normals and morph keys.
- `LoadFigureRegistry`: a mapping with X, Y, Width or Height missing (-1) logs "CorrectMappings ..."
  (message box with debug flag 0x10000 of 007AF5C0) and gives (0, 0, 0, 0); the material type of the
  last group name carries over to a mesh without groups; the coordinate map's operator[] is inlined
  twice (both kept). `BuildBone`: a pivot not (0, 0, 0) is fatal ("node %s have NONULL pivot"); the
  morph frame array is sized twice. `IndexMeshCorners`: maps are walked with GetNextAssoc inlined;
  qsort is the game's (006E88FC) with the original comparators.
- 00449030: without a root node (-1) the original copies the rest pose of the element before the node
  array and walks its children. CFakeUnit's destructor walks the figure array after SetSize(0, -1)
  (so never).
- Options: "Joystick" always defaults to and is written as 0; "LightingFreq" is read under two subkeys
  into two fields; light colours convert 0..1 <-> 0..255 with a truncating `_ftol`; camera flips are
  stored as -1 / 1 floats; "Difficulty Level" goes to both +0x250 and +0x254 and is written from +0x254.
- Map database: the map index continues from the last map of the earlier file (lines before the first
  "#zone" go to it), the exit index restarts with every "#zone"; "#allod" reads word 0 of its five
  following lines even without words; "#view" / "#deployangle" append a "0" word.
- WindowProc's WM_ACTIVATE passes 005DAE70 a second argument built from AL only (upper bytes are those
  of the device pointer 0x007AF080, which ours rebuilds); states above 2 are ignored. DispatchInput logs
  every WM_SYSKEYUP ("<key>:<lParam>"), turns Alt+Tab into a WM_ACTIVATE with the window's iconic state,
  keyboard positions use an arithmetic shift, mouse ones a logical shift. PumpMessages hands messages
  straight to WindowProc (no TranslateMessage / DispatchMessage).

## Skipped

- STLport / MFC template instantiations (hashtable begin / ++ 00455830, 004558B0, 004614C0, 00462680;
  string compares 00445780, 00445810; `__stl_hash_string` 00460BA0; CArray / CMap SetSize, destructors,
  element constructors in 0044F000-00455400, 0045E130-004607E0, 00463000-00465000) and trivial bodies
  too short for the 5-byte jump.
- Pure forwarders to another object's virtual (CUnit slots 31, 47, 56-58, 00455950): rewriting adds
  nothing and a side-by-side check would call the target twice.
- No references in game.exe, never run: 00466270 (curve sample), 0045E8B0, 0045E960, 0046F1F0 (older
  copy of 00471B10), 00459100 and 0045A380 (options from / to a .reg file), 00466E60 (copy of `Reset`).
- Networking (00440000-00444000), WinMain, animation file loading (00446C10-0044C470 beyond the
  scene helpers), CInventory: larger, stateful, built on unrewritten CString / CArray / stream code.
  Candidates: map database loader helpers once CStringArray parsing exists.
- 00458010 (frame present + screenshot), 00458270 (startup movies; the harness empties the list),
  0045E310 (DirectX version probe), 00458570 (STLport hash_map registration): mapped, not rewritten.
- Library-classified CArray / CMap members around the loaders (00453240, 004545C0, 00454F80, 00476430,
  00476640, 004773F0, 00477270, 00476050, CArray::ElementAt 00450750).

## Players, parties and player messages (0065F4B0-00671280)

Code: `player_messages.h/.cpp` (CPlayerServer message writers, `CPlayer::Tick`, `PartyRecords::Find`), `party.h`
(CParty item list; `party_items.cpp` is in `items_spells.md` "Area ticket A-H"). Verify:
`verify_player_messages.cpp`. Scenarios reaching it: `combat.txt`, `models.txt`, `units.txt` (party units'
weapons through `PartyRecords::Find`); 0058B750 runs for every player every world tick and sends the state
messages whose dirty bits are set; `sandbox_actions.txt`'s GSSetVar calls SetVariable.

| Range | Contents |
|---|---|
| 0065F4B0-00661F30 | **CParty** (vtable 00743000, GetClassName 0065F560, 54 slots; a second interface at +0x2C): ctor 00661050, dtor 006611D0, slots 2 / 3 006614F0 / 00661640, member list +0x40 / +0x44 (00661DB0 add, 00661F30 remove), serialisation 00661300 / 006618C0 / 00661C10; 00660100-00661050 party record list helpers used by the hero screens (0058FD90, 00590C70, 0060FCA0, 00617A50) |
| 006620F0-00662AC0 | party records: 0x784-byte records of the party's units (items, stats), `PartyRecords::Find` 006621B0 (by unit id; CUnit::GetWeapon / GetSpell call it), read / write 006621F0 / 006625B0 / 006628D0 |
| 00662AC0-00663000 | multiplayer map transfer: "temp.mp" 00662AC0, 00662DD0, 00662FB0 ("\*.mp" list; Rebirth-patched) |
| 00664DE0-00666170 | **CPlayer** (vtable 00743104, 16 slots): ctor 00664E20, slots 2 / 3 00665010 / 006650E0, 14 / 15 00666170 / 00666050; items 00665470 receive, 00665640 HasItem (script), 00665780, 00665BE0 item changed; units 00665A70 / 00665B50 remove; 00665DE0 per tick (ticks the objects whose ids are in the list at +0x114, slot 6, dropping those gone or finished, then slot 21 of the connection at +0x20) |
| 00666EE0-0066C700 | **CPlayerClient** (vtable 00743180, 48 slots): slots 2 / 3 / 6 00667230 / 00667510 / 00667680 (per frame; "Very top secret" / "Tango" are the session's cipher keys), send 00667810, receive slot 14 00667BA0 (packet dispatcher: "Wrong packet ID", "Pkt read error"), handlers 00667EC0-0066C1E0 (00667EC0 creates an object the server announced: unit prototype "unmogo" gets rh3.axeth00 / rh3.sword00 and rh3.pike00 or sling, "unmowi" gets particle 0x204D, humans' "hr.%.2d" model part from the party record, "mo%dp" / "mo%ds" model and skeleton names by +0x0C; multiplayer only, 3 KB; single-player model selection is in 00535CB0, 005381B0, 00536670, 00568800), 00669A30 "party received on client", 0066AE70 / 0066AED0 local view orders |
| 0066C730-00671080 | **CPlayerServer** (vtable 00743280, 24 slots): slots 2 / 3 0066C9A0 / 0066CC50, 0066D270 (23), 0066D6E0 message 0x0B, 0066E130 per tick (rebuilds the "Heroes" group), send slot 17 0066E430 / 0066E7D0, variables and state 0066EE10 / 0066EE80 / 0066EF30 / 0066EB90 / 0066EF80 / 0066F310 (zone exit), 0066F450 "Resending party of player", receive slot 20 0066F4C0 ("Serv wrong packet ID", "SrvErrMsg", "WrongClientVersion"); command handlers 0066F840-00671060 (walk 0066FFF0, attack 00670190, group action 00670310, zone exit 00670C20, variables 00670E00 / 00670EB0, party 00670F50) |

CPlayerServer messages: each player's server connection (single-player has one too) queues messages for its
client in the CMemoryStream at +0x48: a type byte and a fixed payload. The world player (+0x1C) keeps the dirty
bits the state messages clear (+0x130: 1 state 0x02, 2 0x70, 4 0x71, 8 0x72, 0x10 game time 0x07, 0x20 0x73),
the script variables (+0xC4, a NamedFloatTable) and +0x100. 0058B750 sends 0x70 / 0x71 / 0x72 / 0x73 / 0x07 when
their dirty bits are set and sets state 4 (disconnect); 0058D280 writes every player's "gtime" variable (0x81);
scripts set and delete variables (GSSetVar 0x81, GSSetVarMax, GSDelVar 0x82); unit code sends 0x0B (22 callers),
0x04 / 0x0F positions (also remembered at +0x38), 0x0A / 0x14 / 0x0C object ids with their +0x28 float.

| Address | Name | Verify |
|---|---|---|
| 0066EE10, 0066EF30, 0066F180, 0066EBD0, 0066EC10, 0066EC70, 0066ECD0, 0066ED30, 0066EDA0, 0066EA10, 0066EA30, 0066EA70, 0066EAC0, 0066EB40, 0066EB90, 0066F1F0, 0066F280, 0066F2C0, 0066D6A0, 0066D6E0, 0066D770, 0066D7B0, 0066D250, 0066D260 | CPlayerServer SetVariable, DeleteVariable, SendGameTime, SendState02 / 70 / 71 / 72, SendPosition04 / 0F, Send17, SendObject0A, SendPair16, SendObjects0C, SendObject14, SendText0D / 08, SendValue15, SendPair18, SendObject43 / 0B, SendText13, SendText, SetDisconnect, SetFlag70 | ours into the stream, the stream (header and bytes), +0x1C..+0x48, +0x70 and the player's dirty bits saved, restored, original, compared with the result (skipped when ours grew the buffer; the variable table is set to the same value twice) |
| 0066EE80, 0066F240, 0066D230 | SetVariableMax, SendState73, WriteTo | ours only (they read what they change or call other objects' writers) |
| 006621B0 | PartyRecords::Find | pure |
| 00665DE0 | CPlayer::Tick (every world tick, each player) | ours only: the object list is consistent afterwards (count, links, tail) |

Forwarders: variables table 00460BC0 (set) / 00460AF0 (get) / 00460DB0 (delete), 0047DA80 (state payload),
006D4570 (an object writes its state). Skipped: the rest of the player classes (packet dispatchers,
compression and sending, party records and their CString / CArray / hash_map members, the client's handlers;
mostly multiplayer): next candidates are 0066E130 (the "Heroes" group; forwarder in world_update.cpp) and
0066F310 (zone-exit state; forwarder in script_exec.cpp); 0066D730 (message 0x12) writes one uninitialised
stack byte, so it cannot be reproduced exactly; STLport instantiations (006632B0-00664D00).
