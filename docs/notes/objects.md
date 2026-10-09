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
| 00469630-0046F160 | map-editor object specs (base vtable 0073C5C4; magic trap 0073C650, fire 0073C620, particle source 0073C758, light 0073C788, sound 0073C7B8) saved through the chunked stream 0042BF50 |
| 0065F4B0-00661F30, 006620F0-00662AC0 | **CParty** (vtable 00743000, 54 slots; second interface +0x2C; member list +0x40 / +0x44, 00661DB0 add / 00661F30 remove); party records (0x784 bytes per unit; `PartyRecords::Find` 006621B0) |
| 00664DE0-00666170 | **CPlayer** (vtable 00743104, 16 slots): receive items 00665470, HasItem 00665640, item changed 00665BE0, remove unit 00665A70 / 00665B50, per tick 00665DE0 (ticks the objects listed at +0x114, dropping gone / finished ones, then slot 21 of the connection at +0x20) |
| 00666EE0-0066C700 | **CPlayerClient** (vtable 00743180, 48 slots; receive slot 14 00667BA0 packet dispatcher; 00667EC0 creates an object the server announced: multiplayer only; single-player model selection is in 00535CB0, 005381B0, 00536670, 00568800) |
| 0066C730-00671080 | **CPlayerServer** (vtable 00743280, 24 slots): message 0x0B 0066D6E0, per tick 0066E130 (rebuilds the "Heroes" group; forwarder in world_update.cpp), send slot 17 0066E430 / 0066E7D0, variables / state 0066EE10..0066F310 (zone exit 0066F310, forwarder in script_exec.cpp), receive slot 20 0066F4C0, handlers 0066F840-00671060 |
| 00471B10, 00477B00 | scene reader; `UnitRecord::ApplyTo` |

## Facts

- CEngineObject inline constructor pattern: refCount_ = 0, id_ = -1, unknown_18 = type_ = flags_ = owner_ = 0. `owner_` is the manager; the manager's +0xAC (`CBaseManager::context_`) is what `ManagerContext` returns
  (00455950 calls its slot 13). Base vtable 0073B8E0 (13 slots), restored by 00455930. `ObjectListIterator` {manager, position} walks a manager's
  CPtrList at +0x34 (00404920 / 00404960); the world object manager is the pointer at 007AFFA0.
- CFakeUnit and CUnit (0073EA8C, 77 slots) share: slot 7 GetName (+0x238 CString), 29 / 43 true, 31 / 56 / 57 / 58 forward to the controller at +0x240 (its slots 20, 42, 53, 31), 47 forwards to +0x170 slot 19,
  54 returns +0x278 ("large unit"), 55 +0x274 (skip unit layers), 59 calls 0052F920, 60 float +0x328, 61 assigns the name. CFigure slot 7 GetName = +0x120 (also CSkyObject, CI3DFigure), CFakeFigure slot 7 = +0x94.
  CUnit +0x244 hostile mask (bit p: hostile to player p; set by 0052E970, or-ed by 0052E940, both also set flags_ 0x80000), +0x24C party (members in a hash_map at +0x40).
- CDiplomacy (global 007C2BC4, 0x100 bytes): `allied_[32]` (+0), `hostile_[32]` (+0x80), bit q of row p; relation codes 0 allied, 1 neutral, 2 hostile (script GetDiplomacy maps them to 1 / 0 / -1). CMemoryStream
  (vtable 0073BD90, 0x28 bytes): +5 ok, +0x10 grow step, +0x18 data, +0x20 read position, +0x24 end; slot 1 Read (004A5380), slot 2 Write. CMapDatabase (global 0079BF00, ctor 0045E820): allods (0x2C) at +0,
  maps (0x5C) at +0x14.
- Lightning: CLightning 0x514 bytes (id 0x1E, created by manager slot 14 at 00468680; slot 2 `Register` 00466EA0, slot 3 `Reset` 00468790, named so they do not override Create / Destroy); noise table 0079CE78 (32
  rows of 400 floats, built at start-up by 004660B0 / 00465EE0), curves 0079C1F8 / 0079C838, random 007A9D40, branch sway 00787C8C, world clock 007AF61C / 007AFCA8. The manager's second list at +0xB0 holds the bolts
  registered since the last frame; 00468940 `DrawAll` (called by the renderer at 0056A252) draws and drops them; slot 2 (004689D0) runs the Rebirth-patched 00468A00 once. Callers: script `CreateLightning` (004BCFF0)
  and the weather create a client object (class 0x4D, setter 0067A6B0) whose update 0067A8F0 calls `SetEndpoints` every frame while the bolt follows units.
- **Scene loader**: `LoadFakeUnitScene` (00446C10, `__stdcall`, from CFakeUnit slot 62 at 00446BA0) opens the .amf scene, loads it through `SceneInfo::Load` (00471B10), requires one top-level node, reads the figure's
  .reg (`Ini's\<name>.reg` for scenes under `scenes\`), links nodes, builds one 0x220-byte figure per mesh (unit +0x530), the skeleton (00448A40), bone links (+0x558) and textures from the .reg. Floats the original copies through the FPU
  go through `FpuCopyBits`, products it keeps there through small naked helpers.
- **Options**: ~120 values under HKCU\<path>\<subkey> through the game's registry helper (006972A0-006988A0); the writer uses the setter matching each getter (00697D40 int, 00697CA0 dword, 00697E80 floats as REG_BINARY bits,
  00697FC0 strings). 00458870 (constructor) runs from a static initializer before the hooks exist.
- **Unit record**: `UnitRecord::ApplyTo` (00477B00) applies the world server's unit record (005927C0): stats, controller fields, protection from the prototype model, items, spells, armour, weapons; a record without stats
  gives only the items.
- Party item lists (`party_items.cpp`) are in `items_spells.md`.
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
| 004554B0, 00455630, 00455660, 004557C0, 004557F0, 004554E0, 00455680 | CDiplomacy Reset, GetRelation, GetHostileMask, ReadFrom, WriteTo, SetRelation, OnAttack | object_diplomacy.cpp | copy / pure / recorded stream calls; SetRelation: table against the original without the unit pass, with applyToUnits each unit has the other's bit exactly when relation is 2; OnAttack ours only |
| 004489D0, 0044A230, 0044CC10, 00448A10, 004679B0, 004679D0 | CrossProduct, SubtractVectors (2D 0044CC10), ScaleVector copy (identical to 00410770), Vector3::DivideInPlace, Vector4::FromPoint | object_maths.cpp | pure / copy |
| 00461360, 004613A0, 0045E850, 0045E910, 00460AF0, 00462F70 | TextKeyFromName, IsTextKey, CMapDatabase FindMap / FindAllod, NamedFloatTable::Get, FindCommandByKey | object_tables.cpp | pure (Get needs the game's CRT heap: in game only) |
| 00460BC0, 00460DB0, 00460EE0, 00460FE0, 00461260 | NamedFloatTable Set, Remove, CopyPrefixed, Load, Save (`core.md`) | object_tables.cpp | `verify_named_float_table.cpp` |
| 00466190, 004662C0, 00466CE0, 00466820, 00466FD0, 00467150, 004672E0, 00467E10 | NoiseKey::Randomize, LightningSegment SampleNoise / Animate / Init, CLightning ExpandBounds / UpdateBounds / Animate / SetEndpoints | lightning.cpp | original on a copy (random state restored; deep copies where it allocates); fields, points, generator, curves compared |
| 00466EA0, 00468790, 00468940 | CLightning Register, Reset; DrawAll | lightning.cpp | ours only (list / reference counts, textures cleared, power 1; DrawAll returns 1 and the new-bolt list is empty) |
| 004680D0, 00468680, 00468810, 00467AD0, 00467590 | CLightning start (power, branches, textures "SmtgLight" / "Lightning" or "DemonLight" / "DemonLightning" via 00500A00), factory, destructor body, draw, glow | lightning_start.cpp, lightning_draw.cpp | `verify_lightning_start / _draw` |
| 00471B10, 00446C10 | SceneInfo::Load, LoadFakeUnitScene | scene_loader.cpp | Load: ours reads through a recording proxy, the original runs on empty arrays against a replaying proxy, compared in depth (heap pointers and the unwritten tail +0x24..+0x33 of mesh faces skipped; non-empty arrays: ours only); LoadFakeUnitScene ours only (figure and bone-link array per mesh, unit +0x1C = (0, 0, 500)) |
| 0044B980, 0044CFF0, 004492F0 | LoadFigureRegistry (.reg texture mappings, material types), IndexMeshCorners, BuildBone (recursive) | scene_registry / _meshes / _bones.cpp | original first, results recorded and undone, then ours / ours only (name, frame count, array sizes, parent links) |
| 0044C4A0, 0044DCF0, 0044E3B0, 0044E8A0, 0044EB60, 00448A40, 00449030, 004491C0, 00449240, 0044A2A0 | vertex welding, normals, PlaceMeshes, mesh bounds, relative frames, skeleton, node hierarchy / rest pose, root bone, material name parser | scene_weld / _normals / _bone_frames / _skeleton / _material.cpp | `verify_scene_*` |
| 004466D0, 00446910, 004468F0, 00446B20, 00446BA0, 004468E0 | CFakeUnit ctor, dtor, deleting dtor, Destroy (slot 3), scene load (slot 62: 00446C10, then a class 0x45 object per figure from the world's slot 21, 0059F390), class id | fake_unit.cpp | ours only (vtable, four empty arrays, figures emptied by Destroy) |
| 0044C480-00454800 (11), 0044F1B0-00454A30, 00474C80, 00475F40, 00476DD0 | CFigure / CFakeFigure names, ids, destructors; the engine's plain-element arrays in 32-byte aligned blocks (SetSize, Add, SetAtGrow, copy, destructors) | figure_slots.cpp, aligned_arrays.cpp | one instantiation per template |
| 0045B210, 0045C560, 00458DA0, 00458730 | GameOptions LoadFromRegistry / SaveToRegistry, defaults, destructor | options*.cpp | load: original on a copy of the 0x26C bytes, bytes and texts compared; save: registry import slots point at recorders, event lists must match; defaults differential |
| 0045EA20 | CMapDatabase::Load (map.txt / map-LMP.txt through the text manager 007AF788; directives in `map_database.h`) | map_database.cpp | a shadow database loaded by the original on every call, compared in depth; a non-empty database: ours only |
| 00477B00 | UnitRecord::ApplyTo | unit_record.cpp | ours only (the byte blocks the record sets: controller +0x270, +0x8A8) |
| 00457D40, 00457970, 00458220, 00456140 | app WindowProc, DispatchInput, PumpMessages, StartFirstMap | app.cpp | ours only (WM_ACTIVATE flags 0079BC5C / 0079BC5D, input messages return 1) |
| 00461CB0 | a dead unit's loot | hoover_loot.cpp | |
| 00469CA0-0046E490, 0046A4B0, 0046E5E0, 0045D5B0..0045DFB0 | the zone file's records (base, object, flame, magic trap, lever, unit with behaviours / patrols / waypoints, particle source, light, sound), chunk readers / writers, editor rotation, zone diplomacy table constructor | map_spec*.cpp | `verify_map_spec` |
| 0066EE10, 0066EF30, 0066F180, 0066EBD0, 0066EC10, 0066EC70, 0066ECD0, 0066ED30, 0066EDA0, 0066EA10, 0066EA30, 0066EA70, 0066EAC0, 0066EB40, 0066EB90, 0066F1F0, 0066F280, 0066F2C0, 0066D6A0, 0066D6E0, 0066D770, 0066D7B0, 0066D250, 0066D260 | CPlayerServer SetVariable, DeleteVariable, SendGameTime, SendState02 / 70 / 71 / 72, SendPosition04 / 0F, Send17, SendObject0A, SendPair16, SendObjects0C, SendObject14, SendText0D / 08, SendValue15, SendPair18, SendObject43 / 0B, SendText13, SendText, SetDisconnect, SetFlag70 | player_messages.cpp | ours into the stream; stream, +0x1C..+0x48, +0x70 and dirty bits saved, restored, original, compared (skipped when ours grew the buffer) |
| 0066EE80, 0066F240, 0066D230, 006621B0, 00665DE0 | SetVariableMax, SendState73, WriteTo; PartyRecords::Find; CPlayer::Tick | player_messages.cpp | ours only (they read what they change or call other writers) / pure / ours only (object list consistent) |
| 0066DBA0, 0066E020, 0066F080 | CPlayerServer SendObjectsChanged (0x41) / SendTimedChanged (0x51), SendItems | player_sync.cpp (A-H8) | ours, the stream restored, original, stream compared (a pending delta is written once, by ours) |
| 0066D7F0, 0066DDE0, 0066DAD0, 0066DF40, 0066D100 | SendObjectsAdded (0x40) / SendTimedAdded (0x50), SendObjectsRemoved (0x42) / SendTimedRemoved (0x52), Flush | player_sync.cpp | ours only: objects in / out of the player's map, counted once, stream grown / result 0 or 1, messages emptied |


### Players: sync, client handlers (player_sync.cpp, player_client.*)

Layout: CPlayer (the world player) +0x1C link, +0x24 id, +0x28 / +0x58 hash_maps of the map / timed objects the client
knows (each counted once; +0x3C / +0x6C the highest id), +0x40 the player's units (hash_map), +0x70 an embedded CParty
(slot 18 its item manager), +0x100 `sessionKind100_`, +0x10C log flag (Flush prints both sizes to cout). CPlayerServer
+0x88 / +0x28B0 send / receive coders, +0x50D8 / +0x50E8 sent object arrays, +0x50F8 / +0x510C script groups, +0x5120
refresh flag, +0x5124 timer. CPlayerClient (vtable 00743180) +0x1C player, +0x20 state, +0x24 / +0x28 handler table
(00667230), +0x34 received stream, +0x5C requests, +0x85 send-party flag, +0x88 / +0x28B0 coders, +0x50D8 `array50D8_`.

The "changed" senders compare the object's stamp with the world's update count; a pending delta (flags_ bit 1) is
written first (0050EA70 for map objects, `CEffect::SendDelta` for timed ones). world_sync.cpp names the six senders by
the DiffObjectSets output they get: its "Kept" array gets the removals, "Removed" the changes. Receive errors go through
00664B70 (fatal) with the previous and current type. `CUnitServer::IsFighting` (0054B580) is Rebirth-patched: called
through its address. `CWorldServer::CollectSharedObjects` (00594A30) takes the player id first (its two stack arguments
were once swapped, also in its verify wrapper; multiplayer only).

| Address | Name | Verify |
|---|---|---|
| 0066DBA0, 0066E020, 0066F080 | CPlayerServer SendObjectsChanged (0x41) / SendTimedChanged (0x51), SendItems (calls SendObject14 directly) | ours, stream restored, original, stream compared (see the wrapper rule below) |
| 0066D7F0, 0066DDE0, 0066DAD0, 0066DF40, 0066D100 | SendObjectsAdded (0x40) / SendTimedAdded (0x50), SendObjectsRemoved (0x42) / SendTimedRemoved (0x52), Flush | ours only: objects in / out of the player's map, counted once, stream grown / result 0 or 1, messages emptied |
| 0066E430, 0066DC90, 0066E110, 0066EF80 | SendPartyState (0x60), ForgetObject, IsPartyUnit, SendParty (0x80, refresh timer) | SendPartyState: ours, stream and +0x5120 restored, original, compared; ForgetObject ours only (not in the map, sent list or groups); IsPartyUnit pure; SendParty ours only |
| 0066D270, 0066E130 | ReceiveMessages (slot 23: packets decompressed into 007C83D0, handlers by type from the table at +0x28 / +0x2C), UpdatePartyUnits (party units from group +0x50F8, the player's units and what their brains see; each brain's +0x12C list becomes the gathered group via 004B3200) | ours only, result 0 or 1 |
| 0066E7D0 | SendItemsGiven (slot 17, RET 8): 0x05 (items' and spells' states, 0069FB60 / 0055E8A0 forwarded), 0x06 (money, flag byte, ids, -1; each value stored in its first argument's stack slot before writing) | appends only: ours / stream restored / original |
| 00669350, 0066A480, 0066A5A0 | client 0x17 (00665550 quest log refresh), 0x15 / 0x18 experience numbers (amount * 0073E128 ^ (unit +0x33E - 25) through a float, FISTP; std::pow for _CIpow, so a .5 edge may round apart) | ours only |
| 00667510 (slot 3) / 00667600 (slot 15), 006671A0 | CPlayerClient::Clear, CoderState::Reset (out.End, then Construct) | ours only: fields cleared, coders restarted |
| 00667680, 00667810, 00667970, 00667A30 | CPlayerClient Pump (slot 4: login message 0 with version 100, "Tango", "Very top secret", the player's +0xFC; state 2 sends the camera anchor as 0x40), SendToServer, DispatchMessages, ReceiveMessages (slot 21, one packet per call) | ours only (`verify_player_client.cpp`) |
| 00667D80, 00669850, 00669960, 00669EB0, 00669F40 | client handlers: ReadText (side by side, stream restored), 0x81 set variable, 0x82 delete variable, 0x08 zone change (`GameFlow::RequestZoneChange`), 0x0D jingle (`CMusicSystem::PlayJingle`) | ours only, result 1 and the stream moved (0x81: the variable reads back as sent) |
| 00669010, 006690F0, 006691C0, 00669290 | 0x02 zone (player cleared, client world 00567AB0, Rebirth-patched, through its address), 0x03 / 0x05 world blocks (00567A60 / 00567950), 0x0A item removed (00665870, then quest log 005F7F30 with an empty CPtrList, vtable 0073BE80) | ours only (result, stream moved) |
| 00669490, 00669A30, 00669C90 | 0x06 items received (money, items gathered in reverse with AddHead, 00665470 and the quest log with the money gained), 0x80 party (sized block into LoadParty(1); recharge list +0x114 gets items with charge < max; a multiplayer client restores its variables from a copy 004A8A50 / 00461540 / 004AA950; 005652E0, `GameFlow::ReturnLocalParty`), 0x83 party update (LoadParty(0)) | ours only |
| 00669FA0, 00667E60, 00669710, 00669750 | 0x0E path markers (scratch unit class 0x32 reads the path through a delta field; sources 0x2039 / 0x203A-B when 0079BE70 is set, handed to 00535240 unreleased), 0x13 string event (005F8070 through its address, Rebirth-patched), start / stop the link's recording | ours only (result, stream moved) |
| 00667EC0 | CPlayerClient::OnNewObject (0x40; multiplayer only, no scenario reaches it) | ours only (0, or 1 with the stream moved) |
| 0066F4C0 | CPlayerServer::OnLeaderDied (slot 20, from CUnitServer::UpdateServer when the dead unit IsLeader): single player 0x09 + 0x0B (hero id, 0xFF); a network game revives a one-hero party (money / experience shares 007C5490 / 007C5494 over the NPC record's +0x54 `experienceFloor`, shared object, new id, CollectParty / SendParty / Redeploy, the old brain's +0x12C units and the old hero into the new one's) | ours only: stream +1 / +9; network: flag70_ 1, lastPosition_.z -1313 (hero_death) |
| 00670310, 00670670, 006706A0, 00670730, 00670760, 006707D0 | CPlayerServer::OnUseOrder (0x36: units, flag, kind, value, object; nearest unit asked by CheckOrder / CheckOrderAt from its path end, else the nearest; Player mode CommandUse), UseOrderDistanceSquared, AIPath::Construct / TargetPoint, AIOrder::MakeUse / AimPoint | handler and the two builders ours only (result 1, stream moved by 4 * (n + 5)); the rest against the original (ui_battle_commands' dead-wolf clicks: one unit only) |
| 0066F0C0, 0066F450, 0066F840 | CPlayerServer SendPartyUpdate (0x83), ResendParty (log line, 00590C70, SendParty, 00591070 now `WorldAfterPartyCopyOriginal` in originals_world.h), SendPair19 (RET 8) | ours only: messages appended |
| 0066A7D0, 0066A720, 0066A880, 0066AC70, 0066ACD0, 0066AD40, 0066ADD0, 0066AE70, 0066AED0, 0066AF30, 0066AFE0, 0066B040 | CPlayerClient local view orders (player_client_orders.cpp): 0x30 walk, 0x3A go to, 0x31 attack, 0x33 / 0x32 spell or quick item (slot | 0x80000000; a potion is counted off at once) on a unit / at a point, 0x34 weapon (+0x270), 0x35 move mode, 0x3B (SetUnknown438), 0x39 follow, 0x10 flag | side by side: ours, stream header put back, original, bytes compared (verify_player_client_orders.cpp) |
| 0066F310, 0066FD70 | CPlayerServer::LeaveToZone (RET 8; the `zone` harness step), FormationOffsets (`__fastcall`) | LeaveToZone ours only (state 3, party zone / entrance); FormationOffsets side by side (CheckNear) |
| 0066AA20, 0066AB80, 0066B280, 0066B300 | CPlayerClient SendUseOrder (0x36, one unit; the original's heap CArray not built), SendUseDead (0x36, kind 0x18A95, value -1), SendLeave04 (state 3), SendResume (0x09) (A-H15) | side by side as the other orders (SendLeave04's state put back before the original) |
| 0066B080, 0066B140, 0066B1B0, 0066B2C0 | CPlayerClient SendPartyUpdate (0x50, SaveParty draws an ISAAC key), TakeExit (0x03), RequestZone (0x06), Rejoin (state 3 only: ReturnLocalParty, 0x50, 0x05, state 2); the zone requests return the stream position (004A53F0's EAX) | side by side outside state 3; else ours only (state 2, the request at the stream's end) |
| 0066C540, 0066C770 / 0066C750, 0066C730 / 0066C740 | CPlayerServer Construct, DestructBody / DeletingDestruct, class name "CPlayerServer" / id 0x3B | ours only (vtables, cleared state); name / id against the original |
| 0066CC50 = 0066CF20, 0066CE50, 0066D090, 0066D020 | ClearState (slots 3 / 15; CreateServer's first part), ForgetSentObjects (13), ClearGroup50F8 (21), Leave (16: CollectParty when deployed, SendParty in multiplayer, 0x01 and state 3, Flush) | ours only |
| 006625B0, 00663E40, 00663F60 / 00663A70 | CParty::EquipFromPrototype (party_members.cpp; A-H16): a new multiplayer hero's spells and weapons released and removed, then the NPC record's (unit_db.h, now named) start spells / quick items / weapons, money (+0x58) and experience (+0x54); the spell list's Add and the lists' RemoveAt(index, count) copies (`AlignedArray::RemoveRange`) | ours only: list counts against the NPC record, money, text 6 (ui_hero_screen, 00616AC0); Add / RemoveRange: ours only / original on a copy, but their only caller is 006625B0, so no scenario reaches them |
| 00662AC0, 00662DD0, 006632B0 | MpHeroRecord::WriteFile / ReadFile (party_file.cpp; the 0x74-byte hero records, `<folder><n>.mp`, magic 0x114: a name that is not digits before its last '.' is deleted instead; the old file is moved to temp.mp around the write), SavedParties::SaveAll (folder emptied by ClearSaveFolder, every record written) | ours only: files present / gone through the game's imports (ui_hero_delete writes and reloads a hero; SaveAll when the hero screen closes with heroes) |
| 0066D490 | CPlayerServer::SendPathMessage (0x0E; path through a stack DeltaField, 005C7DB0 forwarded) | side by side (EI_MESSAGE_VERIFY) |
| 0066C0F0, 0066C110, 0066C1D0, 0066C1E0, 0066C340 / 0066C3C0 | CPlayerManager (player_manager.cpp, vtable 0073F328): CreatePlayers (slot 2), NewPlayer (slot 14), ReleasePlayer, ChangeId (client message 0x00), SendValueToAll / SendTextToAll (0x12 via 0066D730 forwarded / SendText13) | ours only |
| 0066FAA0, 00670C20, 00670DA0, 00670E00, 00670EB0, 00670F50, 00671060, 0066FFF0, 0066FBD0, 00670190, 00670B60 | server handlers 0x02 refresh party, 0x03 take exit, 0x05 joined, 0x06 leave to zone, 0x07 briefing done, 0x08 new party, 0x09 resume, 0x30 walk, 0x3A go to, 0x31 attack, 0x50 party update (A-H14) | ours only: result, the stream moved past fixed payloads; 0x05 / 0x09 states |

Kept in CPlayerClient / CPlayerServer: the login's two 64-byte text buffers are sent with uninitialised bytes after the
texts (ours zeroes them); 0x06's flag byte goes out zero-extended (the original's upper three bytes are stack garbage);
OnNewObject reads the world's slot 19 as its `manager40_` (the client world's empty 00566310 / 00566320 are not called)
and a figure or state that cannot be read returns 0 with the object made and unreleased. OnLeaderDied in a network game with a party
of more than one record reads two uninitialised stack ids (ours: -1, no object); the 0x36 unit array is
`mfc::CArray<CUnit*>` (vtable 007402AC; 00671200 RemoveAt in library.csv); the AIPath is destroyed through 00546100. Names: CMapObjectVirtuals slots
30 ApplyBaseScale, 34 TakePrototype, 61 SetScriptName; `CUnit::modelHeight` (+0xF4).

Verify rule for the changed-sender wrappers: `CEffect::SendDelta` leaves flags_ bit 1 set, so the original's second run
would pack the effects again against snapshots ours just updated (this once gave 310 false SendTimedChanged mismatches).
The wrapper therefore does not touch the delta fields: ours runs, the player's stream is restored, the original runs
with flags_ bit 1 cleared on every listed object (it writes from the fields ours packed), the flags are put back, the
streams compared; skipped when ours grew the stream or a packed effect is listed twice. Lesson: never copy a game
stream (or any `__declspec(novtable)` class) by value, its implicit copy leaves the vtable pointer null; RawCopy /
memcpy only (a push_back copy of saved headers once wrote a null vptr back into live effects and crashed most
scenarios in RewindAndEmpty).

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
- Players: the rest of the player classes (packet dispatchers, compression and sending, party records with CString / CArray / hash_map members, the client's handlers; mostly multiplayer); next candidates 0066E130,
  (0066F0C0..0066F840, 0066F310 and the server handlers are done, A-H14); 0066D730 (message 0x12) writes one uninitialised stack byte and cannot be reproduced exactly; STLport instantiations 006632B0-00664D00.
- 0066A280 (unwind-only DeltaField destructor) and the hash_map members 0066B320-0066B510 / 0066BAD0 went to library.csv; 0066BE00 is a copy of 0047B940.

### Message handlers not yet ported (research from DR-H16 / DR-C4)

Reached only through the handler tables, so Ghidra made no functions for them; they are rows in config/functions.csv
(gap to the next function), config/extra_function_sizes.csv (code sizes, capstone) and src/hook/function_sizes.inc.

Client (CPlayerClient table +0x24, filled by 00667230; thiscall, +0x1C the player, +0x34 the received stream read through
its slot 1 Read(buffer, n); no stack arguments; returns 1, 0 on a failed lookup or read where noted):

| Type | Address | Size | What it does |
|---|---|---|---|
| 0x00 | 00667DA0 | 52 | login accepted: a dword, state +0x20 = 2, 0066C1E0(player, value) on the player's +0x14 |
| 0x01 | 00667DE0 | 14 | the player's 00665A10 |
| 0x04 | 00667C20 | 228 | camera placement: a point and two dwords; client world 005662B0(point); unless the player's +0xD8 string equals 007900F4 the camera (0079B2A8) 00411470(point, 23.0, 00743148 * 0073BA30 * 0073DF70), else 004108D0(6 if the second dword is 3, else 7); then 004109E0(first dword); returns the last Read's result |
| 0x07 | 00669D50 | 340 | game state (CreateScreen 005F5BA0, ui.md section 1) |
| 0x09 | 00669F30 | 16 | the interface's (007C7D50) 005F7FA0 |
| 0x0B | 00667DF0 | 48 | two dwords to the interface's 005F82A0 (a numbered message with a value) |
| 0x0C | 006696A0 | 103 | (id, float) pairs until id -1: object manager 007AF60C's object (00403F40) gets the float at +0x28; 0 when the client's +0x39 flag is clear |
| 0x0F | 00667D10 | 97 | two points and a byte: the camera's 00411060(a, b, 0) and 00410E50(byte) |
| 0x12 | 00667E20 | 52 | a byte and a dword: the player's world (00455940) 00566C10(dword) |
| 0x14 | 0066A420 | 94 | (id, float): the party's (+0x70) slot 18 object table's object gets the float at +0x28 and goes to the player's 00665BE0 |
| 0x16 | 00669400 | 133 | (unit id, index): the party member 006621B0 takes record `index` of its +0x730 list (00660A50); the client unit's brain (+0x240) slot 25 with -1 |
| 0x19 | 0066A620 | 225 | kind byte and unit id: 0 the client world controls the unit (00566030), 1 / 2 set / clear unit flag 0x100 (not for flag 0x10) and its +0x94 object's slot 26 with 1 / 0, 3 clears the selection (00566100(0)); other kinds return 0; jump table 0066A704 |
| 0x41 | 006687B0 | 160 | map object changed: the id in the player's +0x28 hash map reads its delta (0050EAB0(1, stream), 0 on failure); kinds 0x54 / 0x5D also go to the world's 005651A0 |
| 0x42 | 00668850 | 237 | map object removed: a unit of the player's +0x40 map gets 005300D0(0); the +0x28 entry erased (0059BCB0 / 0059BC70 / 00598E40 / 0059B750), the object flagged 1 and released (slot 4), client world 00564BE0(id); 0 when unknown |
| 0x43 | 0066A3B0 | 74 | an id: the object manager's (007AF5F8) object of kind 0x50 gets its +0x240 slot 48 |
| 0x50 | 00668940 | 238 | timed object added: (kind, id); the world's factory (slot 14) makes it (slot 23), it reads itself (00675BA0(0, stream)), goes into the player's +0x58 hash map (highest id kept) with one more reference |
| 0x51 | 00668A30 | 105 | timed object changed: the +0x58 entry reads its delta (00675BA0(1, stream)); 0 when unknown or the read fails |
| 0x52 | 00668AA0 | 157 | timed object removed: the +0x58 entry erased, flagged 1 and released (slot 4); 0 when unknown |
| 0x70 | 00669760 | 70 | a byte and a 16-bit value: 007AFCAC's 004E3FC0(byte, (float)value) |
| 0x71 | 006697B0 | 68 | two dwords: client world 0055FB50(second), 0055FB30(first), 00566360() |
| 0x72 | 00669800 | 66 | a point and a dword: client world 00564190(point, dword) |
| 0x73 | 0066A400 | 20 | the client world's 005683B0(stream) reads a block |

Server (table built by 0066C9A0; thiscall on the CPlayerServer, plain RET; the received stream 007C83D0 through its
slot 1, the player +0x1C, the state +0x20, the send stream +0x48; returns 1 unless noted):

| Type | Address | Size | What it does |
|---|---|---|---|
| 0x00 | 0066F890 | 488 (+16 jump table 0066FA78) | login: reads 0x88 bytes; returns 0 in any state but 0. A first dword other than 0x64 (client version): "SrvErrMsg" / "WrongClientVersi..." (00794F50 / 00794F5C, 00694DC0 on world +0x144, formatted by 006E8715) sent as message 0x11, state 3, returns 0. Else two strings of up to 0x3F (0047ADA0) and by game kind (007B0098): 1 PartyManagerEndRedeploy (004A8880 on 007AA5A0) and 006629D0 on the player's party; 2 / 3 PartyMapLoad (004A8790 with the two strings, player +0xFC and the party; failure returns 0); 4 party Clear(1, 0). Then byte 0 and the player's id (+0x0C), SendParty (0066EF80), state 1 |
| 0x01 | 0066FA90 | 11 | leave: the state's slot 16 (Leave 0066D020) |
| 0x04 | 00670CE0 | 191 | only in state 1 (else 0): a dword into the player's +0x100, CWorldServer::CollectParty (00590A40) with this +0x74, SendParty, byte 7, the player's +0x100 and dword 007AFFC4; clears dirty bit 0x10 (+0x130); state 2 |
| 0x10 | 0066FAC0 | 162 | pause request: a byte. When 0057F440, the game server (0079B5DC) exists and is attached and the byte is set: only when the session's leader id (0041A020) is the player's first mercenary's unit id (else nothing) and the leader's PlayerMessage is found and attached, AnnounceText 0xC with its name. Then the world's SetUnknown6C (005942C0) with byte != 0 |
| 0x32 | 006708D0 | 137 | cast at a point: {spell, unit id, point, flag byte} (0x15 bytes); a unit (0055FB00) not flagged 0x800000, not dead (slot 55), with a Player mode (005D5E60 on controller +0x980): 005D3D90(spell, point, flag) |
| 0x33 | 00670820 | 164 | cast at an object: {spell, unit id, target id, flag} (0xD bytes); target by world slot 40; as above: 005D3D20(spell, target, flag) |
| 0x34 | 00670960 | 107 | wanted weapon: {unit id, slot byte}; a living unit not flagged 0x800000: SetWantedWeapon (005552E0) |
| 0x35 | 006709D0 | 98 | wanted movement style: {unit id, byte}; a living unit (no 0x800000 test): controller 00555270 |
| 0x37 | 00670810 | 3 | refused: returns 0 |
| 0x38 | 00670A40 | 80 | two dwords, the first looked up as a unit (0055FB00); result unused |
| 0x39 | 00670A90 | 132 | follow: {unit id, leader id}; with the unit, the leader (may be null) and a Player mode: 005D3DE0 (Follow); no dead test |
| 0x3B | 0066FB70 | 96 | a unit id and a byte: CUnit::SetUnknown438 (0052E9B0) with the signed byte |
| 0x40 | 00670B20 | 51 | 8 bytes into this +0x38 / +0x3C |

