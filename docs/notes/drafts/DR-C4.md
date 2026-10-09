# DR-C4: cleanup of world_savefile.cpp, world_save.cpp, item_stock.cpp; server handlers

Branch draft-dr-c4 (worktree agent-a116a5a6e1c9b6cb7). Drafted without building (2026-10-07), finished
2026-10-09: merged with main (DR-C2), builds verify and release without errors or warnings, all checks at 0
(see "Finish" at the end). Not run in the game.

## Status

All three files are converted: no At<>/Field<>, VCall, float_control pragma, byte-offset arithmetic or literal
game addresses left outside hook registrations (EI_ORIGINAL / EI_REPLACE). Commits: ff77245 (world_savefile),
then world_save, then item_stock (see `git log draft-dr-c4`).

Counts before (config/cleanup_baseline.csv) -> after, by grep (run check_cleanup to confirm):
- world_savefile.cpp: VCall 23 -> 0, address 1 -> 0, byte arithmetic 1 -> 0.
- world_save.cpp: At<> 35 -> 0, VCall 17 -> 0, float_control 2 -> 0, address 8 -> 0, byte arithmetic 1 -> 0.
- item_stock.cpp: At<> 36 -> 0, VCall 19 -> 0, address 17 -> 0.
- The headers that got the named globals go UP in the "raw game address" column (world.h +5, game_strings.h +11,
  item_stock.h +12, item.h +1, lib/mfc.h +1, lib/stlp.h +1): that is the rewrite guide's "constexpr address in one
  header, or an accessor"; run `python tools/check_cleanup.py --update` after the build.

## DR-C2 slots

Main's DR-C2 has exactly the names and signatures the draft used (CWorld::PlayerList / ItemFactory returning
CEngineObject*, CUnitController::RefreshVitals(char) virtual slot 19, CParty::ItemConstructor returning
CItemConstructor*); nothing to adapt. PrepareSave's release code calls [vtable+0x4C] with a pushed 0.

## Header changes (all additive except where noted)

- world.h:
  - `WorldPlayer`: unknown_040 split: `party[0x30]` at +0x70 (+ `Party()` returning CParty*; it is the CParty
    006618C0 / 00661C10 load and save; mercs / variables are its fields seen from the player); unknown_0FC ->
    `unknownFC` (+0xFC), `unknown100` (+0x100); unknown_108 -> `unknown108` (byte) + unknown_109; unknown_114 ->
    `mfc::CList<std::uint32_t> ids` (+0x114, 0x1C bytes, AddTail 0059CC60). static_asserts added. Evidence: the old
    kPlayerFC / kPlayer100 / kPlayer108 / kPlayerIds / kPlayerRecords offsets of world_save.cpp; no other code used
    the replaced padding names (player.cpp's unknown_108 is CPlayer's).
  - `Projectile`: `void* vtable; std::uint32_t references;` (+4, the 16-bit count LoadSave clears as a dword).
  - `LoadingProjectileWorld()` (007C84EC), `WorldItemManager()` (007AFFB4), `SaveLastUncompressedVersion()`
    (00790264), forward declaration of CWorldServer near the top.
  - `SharedObject` and `SharedObjectList` moved here from world_save.cpp, with `kObjectsVtable` (0073FAC8) and
    `kObjectsBaseVtable` (0073FABC).
  - CWorldServer (changed, not only added): `unknown_12E60[4]` + `void* sharedObjects_` (+0x12E64, unused) became
    `SharedObjectList sharedObjects_` at +0x12E60 (its static_assert moved to 0x12E60). world_loot.cpp (112) and
    world_map.cpp (183, 253) still reach the list with byte arithmetic on +0x12E60: DR-C1 can now use the member.
  - CWorldServer: `sectorMap_` shortened to end at +0x2468, new `aiLayer_` (+0x2468 .. +0x280C) for 005B1380's
    argument. Unverified whether the AI layer is part of the sector map (sizes unknown either way).
- item.h: `CItemLink` (0xC bytes, vtable 0073FA88: vtable, int16 references, unknown_06, value) for the record an
  item's unknown_1C points at; world_units.cpp / world_quests.cpp (kItemRecordVtable) can use it too.
- item_db.h: `SpellRecord::code` (+0x04 CString; unknown_00 split), `SpellModifierRecord::code[8]` (+0x04 inline)
  and `strength` (+0x14, was unknown_14, unused elsewhere), `MaterialRecordHead::unknown_0C` + `quality` (+0x10),
  `ItemRecordSlots` (record vtable slot 0 `Kind()`) with `RecordSlots()`.
- item_stock.h: `StockStringVector`, `SpellRecipe` (0x34), `StockRecipeEntry` / `StockRecipeNode` (the recipe
  hash_map's pair and node), `StockModifierEntry`, and namespace `stock` with the scratch vectors (007C8640 /
  007C8650 / 007C8660 / 007C8670 / 007C8680), the recipe vectors (007C8568 / 007C855C / 007C8550), the maps by
  name (007C851C / 007C853C) and the two price factors (00743D78, 0073BAD4).
- game_strings.h: GTimeText (0073F920), CantLoadSaveMapText (00790BB8), and the stock's names WandText,
  MaterialText, InstructionWeapon/Armor/QuickItemText, ScrollText, PrototypeText, RuneText, SpellContainerText.
- lib/mfc.h: `mfc::DWordArrayVtable()` (0073C3D0). lib/stlp.h: `stlp::SmallNodeFreeList()` (0079B228; camera.h has
  the same accessor as NodeFreeList, merge them).

## Behaviour notes and unverified points

- world_savefile.cpp: the coder (0042FCA0 / 00430450 / 00430960 / 0041D930) and CStream::ReadHeader / WriteHeader
  are now called directly (all rewritten, none Rebirth-patched) instead of through EI_ORIGINAL forwarders, so their
  verify wrappers no longer see these calls. World.h's SaveFileRead parameter names `minimum` / `maximum` are
  swapped in meaning (ReadHeader takes newest, oldest; LoadMission passes 00790260, 00790268); positions unchanged.
- world_save.cpp: `#pragma float_control` removed from UpdatePlayerTimes (rule 7). Its verify wrapper checks ours
  only, but SaveWorld's wrapper compares the stream byte for byte with the original, and gtime feeds no saved bytes
  directly; still, watch for a rounding mismatch there.
- world_save.cpp: the item link record's dword clear (`At<uint32>(record, 4) = 0` before the null test) is now two
  16-bit stores (references, unknown_06). Same memory, same crash on a null allocation.
- world_save.cpp: AddSharedObject calls `ServerSession::FindPlayer` directly (0041A2E0, rewritten in
  net_server_players.cpp, not Rebirth-patched) and reads `PlayerMessage::serial_` (+0x30) for the entry's player.
- item_stock.cpp: the offered spells' and modifiers' records (map values of 007C851C / 007C853C) are treated as
  itemdb::SpellRecord / SpellModifierRecord because the offsets read match those tables' layouts (code at +4,
  mana +0x10, strength +0x14, skill +0x18). Not confirmed that the maps hold pointers into those tables.
- item_stock.cpp: 007C8778 is `itemdb::QuestItems()` in item_db.h but `LootItemRecord`'s comment and the stock
  call it the class-0x15 (loot) table; one of the two names is wrong. Check before the merge.
- item_stock.cpp: record slot 0 is named `Kind()` from the old comment ("virtual slot 0 the item kind"); not
  re-checked in Ghidra.
- Projectile / CItemLink / LinkOf use static_cast / reinterpret_cast on the item manager's objects (assumed CItem).

## Server message handler table (0066C9A0, player_messages.cpp kServerHandlers)

25 handlers (thiscall on the CPlayerServer, plain RET; they read the received stream 007C83D0 through its slot 1,
the player is +0x1C, the state +0x20, the send stream +0x48). 13 have no function in config/functions.csv (and no
Ghidra function either; sizes measured from the raw bytes up to the last RET; functions.csv not edited):

| Type | Address | Size | What it does |
|---|---|---|---|
| 0x00 | 0066F890 | 488 (+16: its jump table at 0066FA78 follows) | Login: reads 0x88 bytes; in any state but 0 returns 0. A first dword other than 0x64 (the client version): "SrvErrMsg" / "WrongClientVersi..." (0x794F50 / 0x794F5C) text (00694DC0 on world +0x144, formatted by 006E8715) sent as message 0x11, state 3, returns 0. Otherwise two strings of up to 0x3F (0047ADA0) and by the game kind (007B0098): kind 1 PartyManagerEndRedeploy (004A8880 on 007AA5A0) and 006629D0 on the player's party; kinds 2 / 3 PartyMapLoad (004A8790 on 007AA5A0 with the two strings, player +0xFC and the party; failure returns 0); kind 4 party Clear(1, 0). Then sends byte 0 and the player's id (+0x0C of the player), SendParty (0066EF80), state 1, returns 1. |
| 0x01 | 0066FA90 | 11 | Leave: the player state's slot 16 (Leave, 0066D020); returns 1. |
| 0x04 | 00670CE0 | 191 | Only in state 1 (else 0): reads a dword into the player's +0x100, CWorldServer::CollectParty (00590A40 on 007AFF60) with this +0x74, SendParty, sends byte 7, the player's +0x100 and the dword 007AFFC4; clears bit 0x10 of the player's dirty flags (+0x130); state 2; returns 1. |
| 0x10 | 0066FAC0 | 162 | Pause request: reads a byte. When 0057F440 is true, the game server (0079B5DC) exists and is attached, and the byte is set: only when the session's leader id (0041A020) is the player's first mercenary's unit id (mercs +0x1C; else returns 1 doing nothing), and the leader's PlayerMessage is found and attached, AnnounceText 0xC with its name. Then the world's SetUnknown6C (005942C0) with byte != 0. Returns 1. |
| 0x32 | 006708D0 | 137 | Cast at a point: reads {spell, unit id, point (3 floats), flag byte} (0x15 bytes); the unit (CWorld::FindUnit 0055FB00) not flagged 0x800000, not dead (slot 55), with a Player mode (005D5E60 on controller +0x980): 005D3D90 (spell, point, flag). Returns 1. |
| 0x33 | 00670820 | 164 | Cast at an object: reads {spell, unit id, target id, flag} (0xD bytes); target by world slot 40 (ObjectWithId), the unit as above: 005D3D20 (spell, target, flag). Returns 1. |
| 0x34 | 00670960 | 107 | Wanted weapon: reads {unit id, slot byte}; a living unit not flagged 0x800000: controller SetWantedWeapon (005552E0). Returns 1. |
| 0x35 | 006709D0 | 98 | Wanted movement style: reads {unit id, byte}; a living unit (no 0x800000 test): controller 00555270. Returns 1. |
| 0x37 | 00670810 | 3 | Refused: returns 0 (XOR EAX, EAX; RET). |
| 0x38 | 00670A40 | 80 | Reads two dwords and looks the first up as a unit (0055FB00); the result is not used. Returns 1. |
| 0x39 | 00670A90 | 132 | Follow: reads {unit id, leader id}; with the unit, the leader looked up (may be null) and, when the unit's brain has a Player mode, 005D3DE0 (Follow) with it. No dead test. Returns 1. |
| 0x3B | 0066FB70 | 96 | Reads a unit id and a byte; with the unit, CUnit::SetUnknown438 (0052E9B0) with the signed byte. Returns 1. |
| 0x40 | 00670B20 | 51 | Reads 8 bytes into this +0x38 / +0x3C. Returns 1. |

The other 12 (0066FAA0, 00670C20, 00670DA0, 00670E00, 00670EB0, 00670F50, 00671060, 0066FFF0, 00670190,
00670310, 0066FBD0, 00670B60) are in functions.csv. The disassembly used is in the session's scratchpad only; redo
it with capstone (or create the functions in Ghidra) when porting.

## Finish (2026-10-09)

- Merge: world.h's only conflict was CWorldServer +0x2468..+0x280C: aiLayer_ (this branch) now ends at
  +0x2804 where main's occluderReplanTime_ (+0x2804) and unknown_2808 follow; both static_asserts kept.
- Compile fixes for the +0x12E60 list: world_server.cpp (IsObjectShared, CollectSharedObjects) and
  tests/verify/verify_world_loot.cpp walked the old `void* sharedObjects_` (+0x12E64, the list's head);
  they walk `sharedObjects_.head` and read node->data (SharedObject: objectId +0, player +4, the same
  offsets as their local node structs had). world_server.cpp's local SharedObjectNode removed.
  world_loot.cpp / world_map.cpp / verify_world_save.cpp still use +0x12E60 byte arithmetic (left for DR-C1).
- check_cleanup: the static constexpr vtable addresses in world.h (SharedObject, 2) and item.h (CItemLink, 1)
  counted as raw addresses in headers allowed 0; they are accessors now (SharedObject::ObjectsVtable() /
  ObjectsBaseVtable(), CItemLink::Vtable()). Baseline lowered with --update (world_save.cpp,
  world_savefile.cpp and item_stock.cpp drop out: all counts 0); config/forwarder_duplicates.txt lowered
  (4 fixed).
- Risks checked:
  - UpdatePlayerTimes without the float_control pragma: release code is fild elapsed, fmul secondsPerTick_,
    fadd offset (as double), faddp gtime, fstp float: the same operations in the same order as main's
    precise build (which only also spilled the exact int-to-double conversion). No change.
  - 007C851C / 007C853C values: 006AC970 turns them into table indices as (value - 007C8510) / 0x84 and
    ((value - 007C8530) / 0x24), and 004A7080 tests their +0x80 / +0x20 class masks (the last dwords of the
    0x84 / 0x24 records): they are pointers into the spell and spell-modifier tables. Confirmed.
  - sharedObjects_ at +0x12E60: main had +0x12E64 as the MFC list's head (the list object starting at +0x12E60,
    as world_loot.cpp / world_map.cpp already read it); last member, no sizeof(CWorldServer) use.
- Server handlers: the 13 rows are in config/functions.csv (gap to the next function; the four rows they
  split, 0066F840 / 0066FAA0 / 006707D0 / 00670C20, shortened to their own gap), config/extra_function_sizes.csv
  (code sizes, capstone re-measured from run/vanilla/game.exe: all as in the table above) and
  src/hook/function_sizes.inc (13 rows; no other entry regenerated).
- Still open: the 007C8778 name (QuestItems() vs the loot table), stlp::SmallNodeFreeList vs camera.h's
  NodeFreeList (same address, not merged), record slot 0 `Kind()` not re-checked.
- Scenarios for the suite: ui_saveload and mission save / load (world_save, world_savefile: SaveWorld's
  wrapper compares the stream with the original), the trader stock (camp; item_stock).
