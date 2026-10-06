# World objects, scenes, options, players and shared helpers (00440000-00480000, 0065F4B0-00671280)

Code: `object.*` (CEngineObject helpers, CTransformObject / CHoover constructor bodies, unit and figure getters, CMemoryStream), `object_diplomacy.*`, `object_maths.*`, `object_tables.*`, `lightning*.cpp`,
`scene_*.cpp` (fake unit scenes), `fake_unit.cpp`, `figure_slots.cpp`, `aligned_arrays.cpp`, `options*.cpp`, `app.*`, `map_database.*`, `map_spec*.cpp`, `unit_record.*`, `hoover_loot.cpp`,
`player_messages.*`, `party.h`. Verify: `tests/verify/verify_<file>.cpp`. Scenarios: `objects_more.txt` (lightning bolts, power 5 / 0 / -7, then the lightning spell on a wolf); every other scenario loads zone1,
which reaches scene, options, map database, unit record and (via 0058B750 every world tick) player message code. Network, table and application functions of the range are in `network.md`, `app.md` and `core.md`.
Names are provisional.

## Address map

| Range | Contents |
|---|---|
| 00440000-00446000 | network sessions, debug text overlay, STLport helpers |
| 004466D0-00446C10 | CFakeUnit (0073BFE8, id 0x50, 64 slots; embeds four CArrays, vtables 0073C0E4..0073C108) |
| 00447F50-0044C470 | .anm / .bon loading and splitting, CArray instantiations; 0044C470 `DebugLog` stub (81 callers) |
| 0044F000-00455400, 00453240-00454A70 | CArray / CMap template instantiations; CFigure / CFakeFigure helpers |
| 004554B0-00455980 | CDiplomacy; CEngineObject base (destructor body, ManagerContext, SetOwner; Release in base_manager.cpp) |
| 00456440, 00457D40, 00458220 | WinMain, window procedure, message pump |
| 0045B210-0045C560, 0045D5B0-00460BA0 | game options (global 0079BC70); map database (map.txt / map_LMP.txt), name -> float tables |
| 00461360-004613A0, 00461C10-00461CA0, 004621A0-00462A00, 00462F70 | text keys; CHoover; CInventory (vtable 0073C474, slots 16-21 shared with CItem); key -> command table |
| 00466190-00468A00 | CLightning (0073C550), CLightningManager (0073F5B8) |
| 00469630-0046F160 | map-editor object specs (base vtable 0073C5C4, then 0073C5F0; magic trap 0073C650, fire 0073C620, particle source 0073C758, light 0073C788, sound 0073C7B8) saved through the chunked stream 0042BF50 |
| 0065F4B0-00661F30, 006620F0-00662AC0 | **CParty** (vtable 00743000, 54 slots; second interface +0x2C; member list +0x40 / +0x44, 00661DB0 add / 00661F30 remove); party records (0x784 bytes per unit; `PartyRecords::Find` 006621B0) |
| 00664DE0-00666170 | **CPlayer** (vtable 00743104, 16 slots): receive items 00665470, HasItem 00665640, item changed 00665BE0, remove unit 00665A70 / 00665B50, per tick 00665DE0 (ticks the objects whose ids are in the list at +0x114, slot 6, dropping those gone or finished, then slot 21 of the connection at +0x20) |
| 00666EE0-0066C700 | **CPlayerClient** (vtable 00743180, 48 slots; session cipher keys "Very top secret" / "Tango"; receive slot 14 00667BA0 packet dispatcher; 00667EC0 creates an object the server announced: multiplayer only, 3 KB; single-player model selection is in 00535CB0, 005381B0, 00536670, 00568800) |
| 0066C730-00671080 | **CPlayerServer** (vtable 00743280, 24 slots): message 0x0B 0066D6E0, per tick 0066E130 (rebuilds the "Heroes" group; forwarder in world_update.cpp), send slot 17 0066E430 / 0066E7D0, variables / state 0066EE10..0066F310 (zone exit 0066F310, forwarder in script_exec.cpp), receive slot 20 0066F4C0, command handlers 0066F840-00671060 |
| 00471B10, 00477B00 | scene reader; `UnitRecord::ApplyTo` |

## Facts

- CEngineObject inline constructor pattern: refCount_ = 0, id_ = -1, unknown_18 = type_ = flags_ = owner_ = 0. `owner_` is the manager; the manager's +0xAC (`CBaseManager::context_`) is what `ManagerContext` returns
  (00455950 calls its slot 13). Base vtable 0073B8E0 (13 slots), restored by 00455930. There are 156 engine-object vtables (class name slot 11, id slot 12). `ObjectListIterator` {manager, position} walks a manager's
  CPtrList at +0x34 (00404920 / 00404960) and holds a reference on the manager; the world object manager is the pointer at 007AFFA0.
- CFakeUnit and CUnit (0073EA8C, 77 slots) share: slot 7 GetName (+0x238 CString), 29 / 43 true, 31 / 56 / 57 / 58 forward to the controller at +0x240 (its slots 20, 42, 53, 31), 47 forwards to +0x170 slot 19,
  54 returns +0x278 ("large unit"), 55 +0x274 (skip unit layers), 59 calls 0052F920, 60 float +0x328, 61 assigns the name. CFigure slot 7 GetName = +0x120 (also CSkyObject, CI3DFigure), CFakeFigure slot 7 = +0x94.
  CUnit +0x244 hostile mask (bit p: hostile to player p; set by 0052E970, or-ed by 0052E940, both also set flags_ 0x80000), +0x24C party (members in a hash_map at +0x40).
- CDiplomacy (global 007C2BC4, 0x100 bytes): `allied_[32]` (+0), `hostile_[32]` (+0x80), bit q of row p; relation codes 0 allied, 1 neutral, 2 hostile (script GetDiplomacy maps them to 1 / 0 / -1). CMemoryStream
  (vtable 0073BD90, 0x28 bytes): +5 ok, +0x10 grow step, +0x18 data, +0x20 read position, +0x24 end; slot 1 Read (004A5380), slot 2 Write. CMapDatabase (global 0079BF00, ctor 0045E820): allods (0x2C) at +0,
  maps (0x5C) at +0x14.
- Getters and constructors registered as `__fastcall` free functions are thiscall methods with no stack arguments; CEngineObject virtual names must not be reused by non-virtual members.
- Lightning: CLightning 0x514 bytes (id 0x1E, created by manager slot 14 at 00468680; slot 2 `Register` 00466EA0, slot 3 `Reset` 00468790, named so they do not override Create / Destroy); noise table 0079CE78 (32
  rows of 400 floats, built at start-up by 004660B0 / 00465EE0), curves 0079C1F8 / 0079C838, random 007A9D40, branch sway 00787C8C, world clock 007AF61C / 007AFCA8. The manager's second list at +0xB0 holds the bolts
  registered since the last frame; 00468940 `DrawAll` (called by the renderer at 0056A252) draws and drops them; slot 2 (004689D0) runs the Rebirth-patched 00468A00 once. Callers: script `CreateLightning` (004BCFF0)
  and the weather create a client object (class 0x4D, setter 0067A6B0) whose update 0067A8F0 calls `SetEndpoints` every frame while the bolt follows units.
- **Scene loader**: `LoadFakeUnitScene` (00446C10, `__stdcall`, from CFakeUnit slot 62 at 00446BA0) opens the .amf scene, loads it through `SceneInfo::Load` (00471B10), requires one top-level node, reads the figure's
  .reg (`Ini's\<name>.reg` for scenes under `scenes\`), links nodes, builds one 0x220-byte figure per mesh (unit +0x530), the skeleton (00448A40), bone links (+0x558) and textures from the .reg. The scene reader's inlined
  `CArray::SetSize` is `SetSizeInline` / `SetSizeAligned`; out-of-line template code is reached through original addresses; floats the original copies through the FPU go through `FpuCopyBits`, products it keeps there
  through small naked helpers.
- **Options**: ~120 values under HKCU\<path>\<subkey> through the game's registry helper (006972A0-006988A0); the writer uses the setter matching each getter (00697D40 int, 00697CA0 dword, 00697E80 floats as REG_BINARY bits,
  00697FC0 strings). 00458870 (constructor) runs from a static initializer before the hooks exist; 00458DA0 (defaults) is reached only from 00455B50 / 00455BA0 (a 1% random branch in CPlayerClient::OnNewObject).
- **Unit record**: `UnitRecord::ApplyTo` (00477B00) applies the world server's unit record (005927C0): stats, controller fields, protection from the prototype model, items, spells, armour, weapons; a record without stats
  gives only the items.
- Party item lists (`party_items.cpp`, `CParty::AddItem` / `RemoveItem`) are in `items_spells.md` ("Area ticket A-H").
- **Player messages**: each player's server connection queues messages for its client in the CMemoryStream at +0x48 (a type byte and a fixed payload). The world player (+0x1C) keeps the dirty bits the state messages clear
  (+0x130: 1 state 0x02, 2 0x70, 4 0x71, 8 0x72, 0x10 game time 0x07, 0x20 0x73), the script variables (+0xC4, a NamedFloatTable) and +0x100. 0058B750 sends 0x70 / 0x71 / 0x72 / 0x73 / 0x07 when their bits are set and
  sets state 4 (disconnect); 0058D280 writes every player's "gtime" variable (0x81); scripts set / delete variables (GSSetVar 0x81, GSSetVarMax, GSDelVar 0x82); unit code sends 0x0B (22 callers), 0x04 / 0x0F positions
  (remembered at +0x38), 0x0A / 0x14 / 0x0C object ids with their +0x28 float.

## Rewritten

"Pure" = original and ours on the same input (floats as bits). "Copy" = the original runs on a scratch copy of the bytes the function touches, ours on the real object, bytes and result compared.

| Address | Name | File | Verify |
|---|---|---|---|
| 00455940, 00455970, 00455930, 00453400, 00461C10, 00461C80, 00468C20 | CEngineObject ManagerContext, SetOwner, DestructBase; CTransformObject::ConstructBase (0x54), CHoover Construct / Destruct; CMemoryStream::Reset | object.cpp | pure / copy |
| 00446820, 00446880, 00446890, 004468A0, 00453440, 004547C0 | UnitGetName, UnitGetFlag274 / 278, UnitGetValue328, FigureGetName, FakeFigureGetName | object.cpp | pure |
| 004554B0, 00455630, 00455660, 004557C0, 004557F0, 004554E0, 00455680 | CDiplomacy Reset, GetRelation, GetHostileMask, ReadFrom, WriteTo, SetRelation, OnAttack | object_diplomacy.cpp | copy / pure / recorded stream calls; SetRelation: table against the original without the unit pass, with applyToUnits every unit has the other's bit exactly when relation is 2; OnAttack ours only |
| 004489D0, 0044A230, 0044CC10, 00448A10, 004679B0, 004679D0 | CrossProduct, SubtractVectors (2D 0044CC10), ScaleVector copy (identical to 00410770), Vector3::DivideInPlace, Vector4::FromPoint | object_maths.cpp | pure / copy |
| 00461360, 004613A0, 0045E850, 0045E910, 00460AF0, 00462F70 | TextKeyFromName, IsTextKey, CMapDatabase FindMap / FindAllod, NamedFloatTable::Get, FindCommandByKey | object_tables.cpp | pure (Get needs the game's CRT heap: in game only) |
| 00460BC0, 00460DB0, 00460EE0, 00460FE0, 00461260 | NamedFloatTable Set, Remove, CopyPrefixed, Load, Save (`core.md`) | object_tables.cpp | `verify_named_float_table.cpp` |
| 00466190, 004662C0, 00466CE0, 00466820, 00466FD0, 00467150, 004672E0, 00467E10 | NoiseKey::Randomize, LightningSegment SampleNoise / Animate / Init, CLightning ExpandBounds / UpdateBounds / Animate / SetEndpoints | lightning.cpp | original on a copy (random state saved and restored; deep copies with own point buffers where it allocates); fields, points, generator, global curves compared (SetEndpoints points / bounds only when no uninitialised elements are possible) |
| 00466EA0, 00468790, 00468940 | CLightning Register, Reset; DrawAll | lightning.cpp | ours only (list / reference counts, textures cleared, power 1; DrawAll returns 1 and the new-bolt list is empty) |
| 004680D0, 00468680, 00468810, 00467AD0, 00467590 | CLightning start (power, branches, textures "SmtgLight" / "Lightning" or "DemonLight" / "DemonLightning" via 00500A00), factory, destructor body, draw, glow | lightning_start.cpp, lightning_draw.cpp | `verify_lightning_start / _draw` |
| 00471B10, 00446C10 | SceneInfo::Load, LoadFakeUnitScene | scene_loader.cpp | Load: ours reads through a recording proxy, the original then runs on empty arrays against a replaying proxy, meshes / nodes / helpers compared in depth (heap pointers and the never-written tail +0x24..+0x33 of mesh faces skipped; non-empty arrays on entry: ours only); LoadFakeUnitScene ours only (figure and bone-link array per mesh, unit +0x1C = (0, 0, 500)) |
| 0044B980, 0044CFF0, 004492F0 | LoadFigureRegistry (.reg texture mappings, material types), IndexMeshCorners, BuildBone (recursive) | scene_registry / _meshes / _bones.cpp | original first, results recorded and undone, then ours / original first then ours / ours only (name, frame count, array sizes, parent links) |
| 0044C4A0, 0044DCF0, 0044E3B0, 0044E8A0, 0044EB60, 00448A40, 00449030, 004491C0, 00449240, 0044A2A0 | vertex welding, normals, PlaceMeshes, mesh bounds, relative frames, skeleton, node hierarchy / rest pose, root bone, material name parser | scene_weld / _normals / _bone_frames / _skeleton / _material.cpp | `verify_scene_*` |
| 004466D0, 00446910, 004468F0, 00446B20, 00446BA0, 004468E0 | CFakeUnit ctor, dtor, deleting dtor, Destroy (slot 3), scene load (slot 62: 00446C10, then a class 0x45 object per figure from the world's slot 21, 0059F390), class id | fake_unit.cpp | ours only (vtable, four empty arrays, figures emptied by Destroy) |
| 0044C480-00454800 (11), 0044F1B0-00454A30, 00474C80, 00475F40, 00476DD0 | CFigure / CFakeFigure names, ids, destructors; the engine's plain-element arrays in 32-byte aligned blocks (SetSize, Add, SetAtGrow, copy, destructors) | figure_slots.cpp, aligned_arrays.cpp | one instantiation per template |
| 0045B210, 0045C560, 00458DA0, 00458730 | GameOptions LoadFromRegistry / SaveToRegistry, defaults, destructor | options*.cpp | load: original on a copy of the 0x26C bytes (CString fields copy-constructed), bytes and texts compared; save: the registry import slots point at recorders, event lists (subkey; name, type, data) must match; defaults differential |
| 0045EA20 | CMapDatabase::Load (map.txt / map-LMP.txt through the text manager 007AF788; directives in `map_database.h`) | map_database.cpp | a shadow database (ctor 0045E820) loaded by the original on every call, compared in depth; a database first seen non-empty: ours only |
| 00477B00 | UnitRecord::ApplyTo | unit_record.cpp | ours only (the byte blocks the record sets: controller +0x270, +0x8A8) |
| 00457D40, 00457970, 00458220, 00456140 | app WindowProc, DispatchInput, PumpMessages, StartFirstMap | app.cpp | ours only (WM_ACTIVATE flags 0079BC5C active / 0079BC5D seen, input messages return 1, result 1 without a command-line map) |
| 00461CB0 | a dead unit's loot | hoover_loot.cpp | |
| 00469CA0-0046E490, 0046A4B0, 0046E5E0, 0045D5B0..0045DFB0 | the zone file's records (base, object, flame, magic trap, lever, unit with behaviours / patrols / waypoints, particle source, light, sound), chunk readers / writers, editor rotation, zone diplomacy table constructor | map_spec*.cpp | `verify_map_spec` |
| 0066EE10, 0066EF30, 0066F180, 0066EBD0, 0066EC10, 0066EC70, 0066ECD0, 0066ED30, 0066EDA0, 0066EA10, 0066EA30, 0066EA70, 0066EAC0, 0066EB40, 0066EB90, 0066F1F0, 0066F280, 0066F2C0, 0066D6A0, 0066D6E0, 0066D770, 0066D7B0, 0066D250, 0066D260 | CPlayerServer SetVariable, DeleteVariable, SendGameTime, SendState02 / 70 / 71 / 72, SendPosition04 / 0F, Send17, SendObject0A, SendPair16, SendObjects0C, SendObject14, SendText0D / 08, SendValue15, SendPair18, SendObject43 / 0B, SendText13, SendText, SetDisconnect, SetFlag70 | player_messages.cpp | ours into the stream; the stream (header and bytes), +0x1C..+0x48, +0x70 and the player's dirty bits saved, restored, original, compared with the result (skipped when ours grew the buffer) |
| 0066EE80, 0066F240, 0066D230, 006621B0, 00665DE0 | SetVariableMax, SendState73, WriteTo; PartyRecords::Find; CPlayer::Tick | player_messages.cpp | ours only (they read what they change or call other writers) / pure / ours only (object list consistent) |
| 0066DBA0, 0066E020, 0066F080 | CPlayerServer SendObjectsChanged (0x41) / SendTimedChanged (0x51), SendItems | player_sync.cpp (A-H8) | ours, the stream restored, original, stream compared (a pending delta is written once, by ours) |
| 0066D7F0, 0066DDE0, 0066DAD0, 0066DF40, 0066D100 | SendObjectsAdded (0x40) / SendTimedAdded (0x50), SendObjectsRemoved (0x42) / SendTimedRemoved (0x52), Flush | player_sync.cpp | ours only: objects in / out of the player's map, counted once, stream grown / result 0 or 1, messages emptied |

Player sync (A-H8): CPlayer (the world player) +0x1C link, +0x28 / +0x58 hash_maps of the map / timed objects the
client knows (each counted once; +0x3C / +0x6C the highest id), +0x70 an embedded CParty (slot 18 its item manager),
+0x10C log flag (Flush prints both sizes to cout). CPlayerServer +0x88 / +0x28B0 the send / receive coders. The
"changed" senders compare the object's stamp with the world's update count; a pending delta (flags_ bit 1) is written
first (0050EA70 for map objects, CEffect::SendDelta for timed ones). world_sync.cpp names the six senders by the
DiffObjectSets output they get (Kept / Added / Removed): its "Kept" array gets the removals, "Removed" the changes.
Second group: 0066E430 SendPartyState (0x60; ours, stream and +0x5120 restored, original, compared), 0066DC90
ForgetObject (ours only: not in the map, the sent list +0x50D8 or the groups +0x50F8 / +0x510C), 0066E110 IsPartyUnit
(pure), 0066EF80 SendParty (0x80 and the refresh timer; ours only). CPlayerServer +0x50D8 / +0x50E8 sent object arrays,
+0x50F8 / +0x510C script groups, +0x5120 refresh flag, +0x5124 timer; CPlayer +0x24 id, +0x40 the player's units
(hash_map). CUnitServer::IsFighting (0054B580) is Rebirth-patched: called through its address. Fixed on the way:
CWorldServer::CollectSharedObjects (00594A30, world_server.cpp) had its two stack arguments swapped (its only caller,
0066E430, pushes the player id first), and so had its verify wrapper; it only runs for a multiplayer client.
Third group: 0066D270 ReceiveMessages (slot 23: packets decompressed into 007C83D0, handlers by type from the table at
+0x28 / +0x2C; ours only, result 0 or 1) and 0066E130 UpdatePartyUnits (the party units from group +0x50F8, the
player's units and what their brains see / know; each brain's +0x12C list then becomes the gathered group through
004B3200; ours only). The receive errors go through 00664B70 (fatal) with the previous and current type.
Client side (player_client.h / .cpp, A-H8): CPlayerClient (vtable 00743188) +0x1C player, +0x20 state, +0x24 / +0x28
handler table (00667230), +0x34 received stream, +0x5C requests, +0x85 send-party flag, +0x88 / +0x28B0 coders.
00667680 Pump (slot 4: login message 0 with version 100, "Tango", "Very top secret", the player's +0xFC; state 2 sends the
camera anchor as 0x40), 00667810 SendToServer, 00667970 DispatchMessages, 00667A30 ReceiveMessages (slot 21, one packet per
call): ours only (`verify_player_client.cpp`). The login's two 64-byte text buffers are sent with uninitialised bytes after
the texts in the original; ours zeroes them. Error formats via 00664B70 (PlayerMessageErrorOriginal, originals_gameplay.h).
Client handlers: 00667D80 ReadText (side by side, stream restored), 00669850 0x81 set variable (ours; the variable
reads back as sent), 00669960 0x82 delete variable, 00669EB0 0x08 zone change (GameFlow::RequestZoneChange), 00669F40
0x0D jingle (CMusicSystem::PlayJingle): ours only, result 1 and the stream moved. The quest-variable notifications
005F7FC0 / 00565380 moved to originals_world.h (shared with world_briefing.cpp).
00669010 0x02 zone (player cleared, client world 00567AB0, Rebirth-patched, through its address), 006690F0 / 006691C0
0x03 / 0x05 world blocks (00567A60 / 00567950), 00669290 0x0A item removed (00665870, then the quest log 005F7F30 with an
empty CPtrList, vtable 0073BE80): ours only (result, stream moved). 00665870 moved to originals_gameplay.h.
SendItems calls SendObject14 directly (the original goes through slot 22 of 00743280, which is it). PartyRecords
moved to party_record_lookup.h (unit.h declares a second one).
A-H10 handlers (message types from 00667230's table): 00669490 0x06 items received (money, items gathered in reverse
with AddHead, 00665470 and the quest log with the money gained; the flag byte goes out zero-extended, the original's
upper three bytes are stack garbage), 00669A30 0x80 party (sized block into LoadParty(1); the recharge list +0x114 gets
the items with charge < max; a multiplayer client restores its variables from a copy (004A8A50 / 00461540 / 004AA950);
005652E0, "party received on client", GameFlow::ReturnLocalParty), 00669C90 0x83 party update (LoadParty(0)),
00669FA0 0x0E path markers (scratch unit class 0x32 reads the path through a delta field; sources 0x2039 / 0x203A-B
when 0079BE70 is set, handed to 00535240 unreleased), 00667E60 0x13 string event (005F8070 through its address,
Rebirth-patched; world_client.cpp now uses the shared InterfaceStringEventOriginal), 00669710 / 00669750 start / stop
the link's recording: ours only (result, stream moved). New shared forwarders: FloatTable* and BucketVectorDestroy
(originals_library.h), PathWrite/ReadOriginal (originals_ai.h, net_state.cpp uses them), InterfaceStringEventOriginal.
Server side: 0066E7D0 CPlayerServer::SendItemsGiven (slot 17, RET 8; player_sync.cpp): messages 0x05 (the items' and
their spells' states, 0069FB60 / 0055E8A0 forwarded) and 0x06 (money, flag byte, ids, -1; the original stores each value
in its first argument's stack slot before writing it). Verify: appends only, ours / stream restored / original.
00667EC0 CPlayerClient::OnNewObject (0x40, multiplayer only, so no scenario reaches it): see player_client.h. The world's
slot 19 is read as its manager40_; the client world's 00566310 / 00566320 (empty) are not called. Kept: a figure or state
that cannot be read returns 0 with the object made and unreleased. New names: CMapObjectVirtuals slots 30
(ApplyBaseScale), 34 (TakePrototype), 61 (SetScriptName); CUnit::modelHeight (+0xF4); CPlayer::sessionKind100_ (+0x100).
Verify: ours only (0, or 1 with the stream moved).
00667510 (slot 3) / 00667600 (slot 15, the same bytes) CPlayerClient::Clear and 006671A0 CoderState::Reset (out.End, then
Construct): ours only (fields cleared, coders restarted). +0x50D8 named array50D8_ (a PlayerObjectArray, aligned storage).
Left in CPlayerClient: 00666D10 / 00666FD0 / 00666FB0 (constructor / destructor / slot 0), 00667230 (Create: the handler
table), 00667B40 (RET 4, a stream copy), 00667BA0 (slot 14: unit added on the client, the quest log and the session's
+0x2600 record), the 3-byte stubs 00666EE0..00666F60.

`FindCommandByKey` (00462F70) loops back to 00462F73 inside the 5 bytes the hook's jump overwrites, so its wrapper copies the whole 61-byte body (no calls) to executable memory at DLL load, before the hooks are
installed, and runs that copy as the original (`stolenBytes` 0). Constructors allocate nothing, so the original runs safely on a copy.

## Original behaviour kept on purpose

- CDiplomacy: SetRelation with a relation outside 0..2 leaves the table alone but still clears the bit in the units' hostile masks (the unit pass checks only for 2); player indices unchecked; GetRelation unchecked
  (GetHostileMask checks); OnAttack stops at the first party entry with a null unit and sets the victim's 0x80000 flag a second time after 0052E940 did. CMemoryStream::Reset drops the buffer pointer without freeing it
  and ConstructBase leaves the positions uninitialised (only used from constructors).
- Lightning: Animate reads the curve value before the first one (0079C1F4) when the segment has no points; SampleNoise caps the keys added per call at 100 but leaves the key time behind by the rest.
- Scenes: LoadFakeUnitScene passes the scene flags to 00448A40 as a dword minus one whose upper word is uninitialised (ours 0; consumers read the low word) and builds three CStrings, a CStringArray and a CFileRegistry it
  never uses; the scene loader allocates one CMap per material group and never uses them; the skipped records read only names, keys, normals and morph keys. LoadFigureRegistry: a mapping with X, Y, Width or Height
  missing (-1) logs "CorrectMappings ..." (message box with debug flag 0x10000 of 007AF5C0) and gives (0, 0, 0, 0); the material type of the last group name carries over to a mesh without groups; the coordinate map's
  operator[] is inlined twice. BuildBone: a pivot not (0, 0, 0) is fatal ("node %s have NONULL pivot"); the morph frame array is sized twice. IndexMeshCorners: maps walked with GetNextAssoc inlined; qsort is the game's
  (006E88FC) with the original comparators. 00449030 without a root node (-1) copies the rest pose of the element before the node array. CFakeUnit's destructor walks the figure array after SetSize(0, -1) (never).
- Options: "Joystick" always defaults to and is written as 0; "LightingFreq" is read under two subkeys into two fields; light colours convert 0..1 <-> 0..255 with a truncating `_ftol`; camera flips are stored as -1 / 1
  floats; "Difficulty Level" goes to both +0x250 and +0x254 and is written from +0x254. Map database: the map index continues from the last map of the earlier file (lines before the first "#zone" go to it), the exit
  index restarts with every "#zone"; "#allod" reads word 0 of its five following lines even without words; "#view" / "#deployangle" append a "0" word.
- App: WindowProc's WM_ACTIVATE passes 005DAE70 a second argument built from AL only (upper bytes are those of the device pointer 0x007AF080, which ours rebuilds); states above 2 are ignored. DispatchInput logs every
  WM_SYSKEYUP ("<key>:<lParam>"), turns Alt+Tab into a WM_ACTIVATE with the window's iconic state, keyboard positions use an arithmetic shift, mouse ones a logical shift. PumpMessages hands messages straight to
  WindowProc (no TranslateMessage / DispatchMessage).

## Skipped

- STLport / MFC template instantiations (hashtable begin / ++ 00455830, 004558B0, 004614C0, 00462680; string compares 00445780, 00445810; `__stl_hash_string` 00460BA0; CArray / CMap members in 0044F000-00455400,
  0045E130-004607E0, 00463000-00465000, 00476430, 00476640, 004773F0, 00477270, 00476050) and trivial bodies too short for the 5-byte jump. Pure forwarders to another object's virtual (CUnit slots 31, 47, 56-58,
  00455950): rewriting adds nothing and a side-by-side check would call the target twice.
- Never run (no references in game.exe): 00466270, 0045E8B0, 0045E960, 0046F1F0 (older copy of 00471B10), 00459100 and 0045A380 (options from / to a .reg file), 00466E60 (copy of `Reset`).
- Networking (00440000-00444000), WinMain, animation file loading beyond the scene helpers, CInventory: larger, stateful, built on unrewritten CString / CArray / stream code. 00458010 (frame present +
  screenshot), 00458270 (startup movies; the harness empties the list), 0045E310 (DirectX version probe), 00458570 (hash_map registration): mapped, not rewritten.
- Players: the rest of the player classes (packet dispatchers, compression and sending, party records with CString / CArray / hash_map members, the client's handlers; mostly multiplayer); next candidates 0066E130
  and 0066F310; 0066D730 (message 0x12) writes one uninitialised stack byte and cannot be reproduced exactly; STLport instantiations 006632B0-00664D00.

A-H8 later: 00669350 0x17 (00665550, quest log refresh), 0066A480 / 0066A5A0 0x15 / 0x18 experience numbers (amount *
0073E128 ^ (unit +0x33E - 25) through a float, FISTP; std::pow for _CIpow, so a value on a .5 edge may round apart):
ours only. SendTimedChanged's 310 central mismatches were a wrapper artifact: CEffect::SendDelta leaves flags_ bit 1
set, so the original's second run packed the effects again against the snapshots ours had just updated; the wrapper
now saves and restores the changed effects' four state streams (0066E020 back on).

X16 (2026-10-06): that save/restore crashed most scenarios (C0000005 in RewindAndEmpty under BeginWriteBVerify). The
saved stream headers were RawCopy'd into a local, then copied into a std::vector by push_back: CMemoryStream is
__declspec(novtable), so its implicit copy constructor copies the members but not the vtable pointer (a fresh
vector slot holds 0), and RawCopy then wrote that null vptr back into the real effects' four streams. The original's
re-run then reached BeginWriteBVerify (IsMemoryField false, so ours only) and RewindAndEmpty's virtual Seek called
through vtable 0. Rule: never copy a game stream (or any novtable class) by value; RawCopy / memcpy only. The wrapper
no longer touches the delta fields: ours runs, the player's stream is restored, the original runs with flags_ bit 1
cleared on every listed object (so it writes the messages from the fields ours packed, without packing again), the
flags are put back, the streams compared; skipped when ours grew the stream or a packed effect is listed twice.
SendObjectsChanged's wrapper was not involved (0050EA70 clears bit 1 itself); both are hooked again.
