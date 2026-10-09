# DR-C1: cleanup draft of world_client_maps.cpp, world_units.cpp, world_game.cpp

Written as an uncompiled draft; built and finished by the second pass ("Finishing pass" at the
end). Slot names below are the draft's; after the merge with DR-C2 they are main's (see the end).

Baseline counts (config/cleanup_baseline.csv) before: world_client_maps 160 At / 39 VCall / 27 raw;
world_game 155 / 3 / 70; world_units 155 / 29 / 30 (+ 4 asm, 2 float_control, 4 byte arithmetic).

## world_units.cpp (done, uncompiled)

Now: 0 At<>, 0 VCall, 0 byte-offset arithmetic, 1 raw address (inside the `__asm` of the CIacos
forwarder, left with the asm), asm 4 and float_control 2 unchanged (out of the ticket's scope).

The creation "message" is the .mob record (map_spec.h): `mapspec::UnitSpec` for CreateUnits,
`ObjectSpec` for UseQuickItem, and in CreateObject per kind: ObjectSpec (1, 5-9), UnitSpec (5),
FlameSpec (6), TrapSpec (7), LeverSpec (8), SoundSpec (3), LightSpec (2), ParticleSpec (4). The
party record is `PartyUnitRecord` (unit.h; texts[0] script name, [2] name, [3]/[4] the companion
texts, [5] prototype, [6] the 005308D0 text, unknown_01C id, unknown_77C leader, unknown_780,
unknown_770 -> moveMode2, weaponIndex -> CUnit::unknown_270). Objects are CMapObject / CUnit /
CTorchObject; timed objects CEffectSound / CEffectAmbientSound / CEffectPointLight /
CEffectParticle (world_effect.h); the brain is CUnitServer::brain (combat.h).

Header changes (all offsets from existing static_asserts or the disassembly noted):
- map_spec.h: EffectSpec, ParticleSpec, LightSpec, SoundSpec moved in from map_spec_effect.cpp,
  UnitSpec from map_spec_unit.cpp (unchanged layouts; static_assert on UnitSpec::stats 0x210 and
  SoundSpec::unknown_84 added). ObjectSpec::parentId (+0x50) is really the object's type for the
  world (CreateObject passes it to the manager's CreateObject); not renamed (map_spec.cpp uses it).
- map_object.h CMapObjectVirtuals: slot 29 IsDynamicSlot (bool; 0050E430 `flags >> 17 & 1`, units
  004467E0 `MOV AL,1`), 33 ScaleEffectSlot(int) (units 0052EB30), 35 SetStatsSlot(const UnitStats*)
  (units 00530A20 -> 00552F40), 37 UpdateSizeClassesSlot() (units 00530A50 -> controller slot 23,
  00552FB0). Vtables read from the exe: CUnit 0073EA8C, CMapObject 0073DAA0, CUnitServer 0073EF38.
  "Slot" suffix so the names do not collide with CUnit's plain members (ScaleEffectChanged,
  UpdateSizeClasses) or CUnitServer::SetStats.
- map_object.h: new `CTorchObject : CMapObject` with `spec` at +0x238 (sizeof 0x23C, from the
  TorchCreate comment). UNVERIFIED that the case-6 object's +0x238 is always a CTorchObjectSpec;
  the slot signatures match (16/17/18 RET 0xC/0xC/4).
- server_specific_manager.h: `CTorchObjectSpecSlots` (vtable 0073D558 read from the exe: 13-15
  empty RETs 004D6B20/30/40, 16 SetLightSlot, 17 SetFlameSlot, 18 SetNameSlot) and TorchSpecSlots().
- world.h CWorld: slot 18 ItemManager(), slot 24 CopyItemSlot(CEngineObject*) (server 00594CE0;
  not named CopyItem, CWorldServer::CopyItem is the hooked plain member).
- world.h CWorldServer: +0x11D loadedFresh_, +0x11E keepsState_ (world_map.cpp's LoadedFresh /
  KeepsState helpers still use byte offsets; convert them in a later pass).
- item.h: `ItemSourceRef` (12 bytes: vtable, refs dword, itemId), the record at CItem +0x1C.
- New world_data.h: named accessors for the strings, float constants (read from the exe), vtables
  and the prototype name map world_units.cpp used by address.

Behaviour risks to check when building / running:
- The torch's slot 16 now takes `float value` (flame->volume) where VCall pushed the dword: a
  signalling NaN would be quietened if MSVC moves it through the FPU. Slot 17's colour array is
  passed as const float* (same pointer).
- Unit record counts are uint32 in PartyUnitRecord; the loops cast them to int as the original
  compared signed (At<int>).
- Item charge copy (+0x28 = +0x2C) and spec scale/position zeroing use memcpy/memset to keep dword
  moves.
- PrototypeNamePair (local, 0x10 bytes) replaces the 16-byte buffer for the name map pair.
- `UnitSet18` forwarder now declared with `const char*` (same dword).

## world_client_maps.cpp (done, uncompiled)

Now: 0 At<>, 0 VCall, 0 raw addresses outside the EI_ORIGINAL / EI_REPLACE lines.

Second pass (the rest of the file):
- CWorldClient members (world.h, static_asserts on each): countedA0_ (+0xA0, MFC CArray of counted
  pointers; SetSize 00570000 forwarded), heroes_ / objects_ (+0xB4 / +0xB8), dynamicObjects_
  (+0xBC, mfc::CList), dynamicNodes_ (+0xD8, PendingMap) and dynamicMaxId_ (+0xEC), screenEntries_
  (+0xF0, stlp::list<ScreenEntry>), hits_ (+0xF4, ScreenHits) and hitCursor_ (+0x100), the range
  fields +0x138..+0x14C, secondGrid_ (+0x150), manager188_ / manager190_ / manager19C_, listManager_,
  arrayManager_, owned1A8_ / owned1AC_ / owned1B0_, list1B8_ / list1BC_ / list1C0_, unknown1C4_,
  handle_ (RefPtr +0x1C8), texts_ (+0x1D0), texturePath_ (+0x224), unknown84_, unknown6DC_,
  unknown6E4_, sessionJoins_ (+0x6EC), ribbons_ (+0x6F4), list7BC_, cursor_ (+0x7D0); Session()
  views session_ as a WorldSession (+0x20 localView, +0x100 gameKind). Evidence: AddObject's
  disassembly (005647E0: +0xBC CList head/tail/count at +0xC0/+0xC4, the id map at +0xD8), Setup's
  decompilation (00561E50) for the owned objects and their sizes.
- THE OVERLAP: world.h's listA_ (+0xD0), listACount_ (+0xD4) and listB_ (+0xE0) were not client
  members: 00564AF0 (CWorldClient::ListItem) is called on a platform OBJECT (AddObject: ECX = the
  object) and reads CObject3D::parts (+0xD0 data, +0xD4 size) and pendingParts (+0xE0). Moved to
  `FootprintPart* CObject3D::PartBox(int)` (map_object_figures.h, defined in world_client.cpp,
  same registration; verify_world.cpp's wrapper renamed). The three client members are gone.
- THE "PENDING" MFC LIST +0x1F0 is the platform set's face list: PlatformSet (+0x1D4) is an engine
  object (vtable at +0) followed by an mfc::CList<Platform> at +0x1C (00573000 carves 0x54-byte
  nodes; AddFace is AddHead). RemoveObject's +0x1F0 walk drops the object's platform faces
  (Platform +0 is the id; Platform::id / z named). PlatformSet now has `list` instead of `head`;
  world_client.cpp's SurfaceHeight / Contains and verify_world_client_maps.cpp updated.
- Slots: CObject3DSpecific 22 HitsDirtyRect / 24 HitsPartRect (map_object_figures.h; client
  0051C130 / 0051C160 read from vtable 0073DE90), CUnitController 41 PlaceOnPath (client 00535B20),
  42 ObjectDeleted (server 00552670, base 00533CE0), 43 StartFadeIn (client 005344A0, base 00533CF0)
  (unit.h; client vtable 0073ECD8, server 0073EF38). These five are NEW names in DR-C2's headers.
  CLinkFigureLoader::ClearCache (slot 5) for ClearMaps' +0x34 call; CBaseManager::Init (slot 13)
  for the managers' "attach world"; CBaseManager::FreeSpares (00404410) for ManagerFreeRecords;
  slot 22 of the world is SpellManager() (the vtable 0073F41C + 0x58 is 0055EBA0, +0x4C), walked
  with ObjectListIterator (the notes called it the player list).
- Deleting destructors: `delete static_cast<CEngineObject*>(p)` (slot 0 with 1, as the VCall did).
  Slot 3 / slot 2 on the embedded platform set and ribbons through `Engine()` views (no vtable is
  declared on those structs, so the calls cannot be devirtualised).
- Terrain (terrain.h) now derives from CEngineObject and declares the members Setup's inlined
  constructor initialises (sizeof 0xAFC asserted). New world.h types: OpaqueRecord<N>, ScreenEntry,
  ScreenHits, RadiusMarker, WorldSession, ClientRibbons, ClientTexts, ClientListManager,
  ClientArrayManager, ClientManager19C, MatrixDevice (moved from world_client_view.cpp, which now
  uses it) with TheMatrixDevice() (007AF068). world.h now includes base_manager.h and
  core_refcount.h. world_data.h: Setup's strings, folders, the shadow setting and vtables.
- camera::TheCamera()->SetTarget(nullptr) replaces the 0040D3B0 forwarder on 0079B2A8.

Behaviour risks (world_client_maps.cpp):
- AddObject now calls CObject3D::PartBox and PlatformSet::AddBox directly (they were reached
  through their hooked addresses: the verify wrappers no longer see these calls).
- Setup writes the handle's refs / flags as three stores (the original: one dword store).
- ClearMaps / Teardown call CBaseManager::FreeSpares, CRefObject::Shutdown / Release and
  RefPtr::Share by name (rewritten ones; before, their addresses).
- UNVERIFIED: owned1B0_ and owned1A8_ are engine objects (only slot 0 / slot 2 are called);
  the +0xA0 array's elements are engine objects (00570000 calls their slot 4).

First pass:
- CreatePartyUnits (00566F70) fully: PartyUnitRecord fields (texts[2] model, [4] skin, [5]
  prototype, [6] second prototype, [0] script name, unknown_01C id, unknown_724 scale, the four
  item lists), the name map pair (NamePair::second is now UnitPrototypeRecord*), the prototype
  index from unitdb::Prototypes() (RecordTable, unit.h; kPrototypeSize 0x120) into
  CEngineObject::unknown_18, the figure as CObject3D::attached (+0x94), base scale through
  CUnit::MapObject()->baseScale, the part names / "hr" / "hr.%.2d" from world_data.h, "none"
  from game::NoneText(). Slots: client 19 MapObjectManager(), manager 23
  CBaseManager::CreateObject, unit 35 SetStatsSlot, 30 ApplyBaseScale, 61 SetScriptName,
  controller 25 ShowWeapon, 29 UpdateHeroTextures (NEW name in unit.h CUnitController, client
  00536100 read in Ghidra), 30 RefreshModel.
- Everywhere: slot 4 -> CEngineObject::Release(), slot 12 -> GetClassId(), object slot 29 ->
  ObjectSlots()->IsDynamicSlot(), client slot 19 -> MapObjectManager() (Pick).

(The first pass's "left" list is done by the second pass above.)

Files touched outside the three .cpp files (all need the build to confirm): world.h, terrain.h,
unit.h, map_object_figures.h, object_tables.h, world_data.h, new game_flow_data.h,
world_client.cpp (PartBox, platform loops), world_client_view.cpp (MatrixDevice from world.h),
tests/verify/verify_world.cpp, tests/verify/verify_world_client_maps.cpp. Compile risks to watch:
world.h now includes base_manager.h and core_refcount.h (name clashes in files including both);
Terrain is now polymorphic (derives CEngineObject); unit.h includes object_tables.h.

## world_game.cpp (partly done, uncompiled)

Counted by grep after this pass: 80 At<> (was 155), 3 VCall (unchanged), raw addresses down to
the three inside the two `__asm` blocks (was 70), byte arithmetic 3 (was 5).

Converted:
- Every `constexpr std::uintptr_t kX = 0x...` string / float / vtable constant moved to the new
  src/game/game_flow_data.h (namespace flowdata, `using namespace flowdata;` at the top of the
  file); the names are unchanged. kNetLogStream added for StartServer's 0079B91C store.
- Zone records: MapRecord (object_tables.h) gains mob (+0x08), map (+0x0C), setting2C, setting30;
  ZoneRecords() / ZoneRecordCount() read GameMapDatabase()->maps; FindZone() forwards to
  0045E850 (as before) typed. RecordName / RecordAllod / RecordMob / RecordMap view the CString
  fields as mfc::CString (the original copies them as CStrings, sharing the buffer).
  SaveGame, CopyVisitedZones, RestoreZone, EnterZone, StartNetworkZone, StartZone, LoadGame use them.
- The party record builder: PartyRecords (unit.h) gains variables (+0x54, NamedFloatTable),
  zone (+0x68), entrance (+0x6C), sizeof 0x70 asserted (unit.h now includes object_tables.h).
  LocalRecords holds a PartyRecords. EnterZone / LeaveZone keep their `std::uint8_t*` signature
  (verify_world_game.cpp declares it) and view it as PartyRecords; EnterZoneAt / LeaveZoneAt
  forwarders typed. WorldPlayer::Party() (world.h) is the player's PartyRecords at +0x70.
- GameFlow (world.h) members: state_ (+0), requestTarget_ (+4 CString), requestParty_ (+8,
  PartyRecords bytes; RequestParty()), requestArg_ (+0x78), zone7C_ (+0x7C), flag80_ (+0x80).
- MapExit::deploy for RestoreZone's catch-up point (exit +4..+0x10).

Left (where to continue in world_game.cpp):
- The network sessions by offset: ClientSession() / ServerSession() (0079B5D8 / 0079B5DC) are
  std::uint8_t* with +4 flags, +0x25A0 host record, +0x25C8, +0x25E0, +0x25EC peer, +0x25F4 zone,
  +0x25F8 briefing list, +0x25FC variable list, +0x2600 request record (+8 count, +0x24, +0x28
  kind), +0x261C game type: check net_classes.h's net::ClientSession / ServerSession for these
  members first (the network cleanup already named many).
- The server world's player list (007AFF98, +0x2C count), LocalView() + 0x1C players (use
  LocalViewData::player), WorldPlayer +0x20 / +0x100 / +0xC4 (variables) and the server
  connection fields (+0x114C .. +0x1160) in SwitchMultiplayerZone.
- The global party 007C82B0 (a PartyRecords: +0x30 records, +0x34 count, +0x54 variables, +0x68
  zone) in BeginMultiplayerGame / AfterServerStart.
- The variable list nodes in UpdateFromServer (+8 record: +0xC name, +0x18 bits, +0x1C stamp) and
  the saved parties' entries in SavedParties::Reload (0x74-byte entries: a PartyRecords + a
  CString at +0x70).
- The three VCalls (session release slot 0 with 1, ...), the `__asm` _ftol / 24.0 helpers (out
  of scope), the accessor functions with literal addresses (GameMode, Campaign, ...; not counted
  as they end in `)`, but they belong in game_flow_data.h).

## Finishing pass (builds; not run in the game)

Merge with main (DR-C2, DR-C3, A-U7, ...): where both named the same slot, DR-C2's name was kept
with the draft's signature: CMapObjectVirtuals 29 IsSynchronised() (bool), 33
ScaleEffectChanged(int), 35 SetStats(const UnitStats*), 37 UpdateSizeClasses(); CWorld 18
ItemFactory(), 24 DuplicateItem(CEngineObject*) -> CEngineObject*; CUnitController 29
RefreshEquipment() -> int; CObject3DSpecific 22 BoundsContainPoint / 24 PartsContainPoint
(const long*) -> int. Controller 41-43 keep the draft's names (main had none). The draft's
CTorchObject in map_object.h was a duplicate of main's torch.h (A-U6): removed, world_units.cpp
includes torch.h (spec is CTorchObjectSpec*, TorchSpecSlots takes it as CEngineObject*).

Everything else compiled as drafted: build.cmd verify / release with no errors or warnings,
check_thunks / check_returns / check_forwarders / check_cleanup / check_stolen / check_x87 /
check_signatures at 0. forwarder_duplicates.txt lost 00404410 (now CBaseManager::FreeSpares).

Callees the draft now calls by name instead of through their hooked address were checked
against docs/rebirth-patched-functions.txt (none is patched): 004031B0, 00404410, 00404920,
00404960, 0040D3B0, 00417DF0, 00564AF0, and in world_game.cpp 00413250 (NewRefObject),
00413270 / 00418BE0 (RefPtr::Destroy / Share), 004A53F0 (CMemoryStream::Write). Their verify
wrappers no longer see these calls. Setup's handle store (three stores for the one dword) leaves
the same memory; kept.

world_game.cpp, continued:
- The sessions: net::GameClient() / GameServer() (net_classes.h) replace the byte views. The
  client's +0x2600 "request record" is player_ (a LoginMessage): state_ (+0x28) the game state
  request, version_ (+8) bumped with it, host_ (+0x24), name_ / text_ (AfterServerStart).
  +0x25EC is info_ (HostMessage: address_ +0xC, zone_ +0x18), +0x25F4 map_ (MapMessage text_),
  +0x25F8 zones_ / +0x25FC variables_ (lists of ZoneMessage / VariableMessage: name_, value_
  moved as its dword, serial_ the stamp; MessageNode<T> in the file), +0x261C renamed
  ClientSession::gameType_. Server: +0x25A0 renamed ServerSession::hostInfo_ (the HostMessage
  AnnounceHost builds; net_server.cpp / net_server_life.cpp follow), name_, maxPlayers_,
  unknown_25FC, clients_ (ClientNode list). SwitchMultiplayerZone's +0x114C..+0x1160 is the
  connection's reliableIn_ GameBuffer (begin; flags = 6, high = cur = begin).
- Active() tests CRefCounted::flags7_ & kDetached (the original tests the dword's bit 31).
  ReleaseSession deletes through the virtual destructor (`delete`, slot 0 with 1) instead of
  VCall. Session / record allocation through net::NewRefObject (operator new + the count cleared,
  as inlined). net::NetLog() now returns a reference (StartServer stores it).
- Players: LocalViewData::player, new LocalViewData::partyTaken (+0x85), new
  WorldPlayer::sessionKind (+0x100), WorldPlayer::server->flag70_ / lastPosition_
  (player_messages.h), the server world's player count as CBaseManager::objects_.Size() of
  World()->players_.
- GlobalParty() is a PartyRecords*; PartyRecords gains recordArrayVtable (+0x2C, the records'
  CArray) and array40 (+0x40). SavedParties gains folder_ / entries_ / finish_ / eos_ over the
  new SavedPartyEntry (records + fileName, 0x74, defined in world_game.cpp).
- AcceptNewPlayers' list is CTypedPtrList<netlink::NetLink>.

Counts now (config/cleanup_baseline.csv): world_units 0 At / 0 VCall / 1 raw (asm forwarder),
asm 4, float_control 2; world_client_maps all 0; world_game 1 At / 1 VCall / 2 asm / 3 raw (in
the asm) / 1 *At view struct (TextOpsAt) / 0 byte arithmetic.

Left in world_game.cpp:
- World()->+0x5C (`At<int>`, and 007AFFBC read directly twice): CWorld::unknown_5C is a RefPtr
  (world_managers.cpp drops it); on the server it is the pending connection source that
  world_update.cpp steps (its slot 6 is the lobby's TakeNew, the remaining VCall in
  AcceptNewPlayers) and hands to 0058DAF0; ClientSetPlayer (00566CE0, originals_world.h, declared
  with an int) passes it to 004782E0 as a link. Needs a class for that object and the forwarder
  retyped (app.cpp uses it too).
- The two `__asm` helpers (_ftol, 24.0), TextOpsAt, the accessors with literal addresses
  (GameMode, Campaign, ...; not counted, but they belong in game_flow_data.h).
- Behaviour risks still open from the draft: the torch slot 16 float argument (a signalling NaN
  could be quietened), PrototypeNamePair, the overlap fix (PartBox on CObject3D), and the
  UNVERIFIED notes above (owned1A8_ / owned1B0_, the +0xA0 array, CTorchObject's spec).
