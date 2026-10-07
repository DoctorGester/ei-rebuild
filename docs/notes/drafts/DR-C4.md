# DR-C4 draft: cleanup of world_savefile.cpp, world_save.cpp, item_stock.cpp; server handlers

Branch draft-dr-c4 (worktree agent-a116a5a6e1c9b6cb7). Nothing here was compiled or run (no-build pass,
2026-10-07). A later worker builds, fixes and finishes it.

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

## Depends on DR-C2 (merge DR-C2 first)

The drafts call DR-C2's slot names, which DR-C2 declared as `void Name()`. Give them these signatures when merging
(on main they are still SlotNN, so this branch does not compile alone):
- `CWorld::PlayerList()` (slot 20): `CEngineObject* PlayerList();` (returns players_, 0055EB50).
- `CWorld::ItemFactory()` (slot 18): `CEngineObject* ItemFactory();` (itemManager_, 004BA000). DR-C1 called the
  same slot `ItemManager()` returning CEngineObject*; take DR-C2's name.
- `CUnitController::RefreshVitals()` (slot 19): `void RefreshVitals(char withHitPoints);` (PrepareSave passes 0;
  the original's VCall pushed an int 0, one stack slot either way; check RET 4 of 005533B0).
- `CParty::ItemConstructor()` (slot 16, party.h): `CItemConstructor* ItemConstructor();` (0065F540,
  itemConstructor_). item_stock.cpp reinterpret_casts it to its local `Constructor` forwarder struct.
Already named on main and used as is: CWorld::SetTime / SetTimeSpeed (31 / 32), CMapObjectVirtuals::TakePrototype
(34) and IsDead (55) through ObjectSlots(), CBaseManager::CreateObject (23), CStream Read / Write / Seek / Size /
Position, CParty::ItemManager (18) / SpellManager (22), CEngineObject::Release (4) / Load (9).

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

## Where I stopped

Everything in the ticket is drafted. Next: merge DR-C2, give the four slots their signatures (above), build
verify and release, fix compile errors, run check_cleanup --update, then the scenarios that reach the saves
(ui_saveload, mission save / load) and the trader stock (the camp).
