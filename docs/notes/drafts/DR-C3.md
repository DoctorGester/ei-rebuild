# DR-C3: cleanup draft of unit_motion_tick.cpp, combat_motion.cpp, net_state.cpp

Branch draft-dr-c3. Written without building (draft pass): nothing here has been compiled or run.
The finishing worker builds `verify` and `release`, runs check_cleanup (then `--update`), and the
scenarios that reach the three files (movement tick / combat motion: any fight or walk scenario;
net_state: a client scenario, the network state of map objects and units).

## Header members added (shared headers; data members only, so DR-C2's slot renames merge)

combat.h, CUnitServer (offsets from the At<> uses being replaced, cross-checked with combat_save.cpp's
Load/Save order and combat_taming.cpp; static_asserts added for each):
- `queue` +0x24 (AnimationSteps; its `extra` +0x38 is the queue's start time), split from unknown_022.
- `stoodBlocked` +0x145 (was unknown_145), `movingAnimation` +0x147, `savedQueue` +0x148
  (AnimationSteps, extra +0x15C), split from unknown_147[].
- `sneakWait` +0x178, `actingCheckTime` +0x180, `unknown_188` +0x188, `unknown_18C` / `unknown_18D`.
- `ownSide` +0x90C, `tamerSide` +0x910 (combat_taming.cpp's +0x90C / +0x910), `unknown_918`,
  `itemSpells` +0x91C (`UnitItemSpellList`, see below), `waitFrame` +0x93C, `moveKey` +0x940,
  `moveSteps` +0x944, `idleKey` +0x95C, `idleSteps` +0x960 (extra +0x974), `unknown_978`.
- New types before the class: `UnitItemSpell` (the 0xC-byte element of the armour spell list, the
  same record as combat_update.cpp's local SpellElement) and `UnitItemSpellElements` (Construct
  zero-fills: 00558EA0 from NewNode 00558B10; Destruct releases a non-null item: 00552B80; both
  read in Ghidra). `UnitItemSpellList = mfc::CList<UnitItemSpell, UnitItemSpellElements>`.
  combat.h now includes lib/mfc.h and <cstring>.
- Existing members reused: followsLeader / followedLeader (+0x1D8 / +0x1DC) are also the movement
  tick's "waiting for" link; stats.unknown_060 is the tick's `c + 0x240`; stats.loadFactor (+0x238)
  is what combat_motion called the hero's "stamina".

unit.h, CUnit: `companion` +0x170 (split from unknown_164), `scriptName` +0x238 (CString; split from
unknown_1DE). static_asserts added also for unknown_254, unknown_278, motion, unknown_330,
unknown_43C, levels4C8.

world.h: `LocalViewData::unknown_84` (+0x84; net_state's local view flag);
`CWorldServer::occluderReplanTime_` +0x2804 (split out of the tail of sectorMap_, which now ends
at +0x2804; whether it belongs to the sector map is unknown).

Other members used as they are: CPlayer (player_messages.h: connection_ +0x20, units_ +0x40 the
party, sessionKind100_ +0x100, unknown_108), CPlayerServer::partyUnits_ +0x510C, AIPath (ai_map.h:
cells +0x3C, goal +0x60, reached / failed / flag6E +0x6C..+0x6E), MotionOrder::startTime,
CMapObjectServer::vitals (mana +0x14 / maxMana +0x18), FigureAnimation (key +0x14, frameCount +0x20,
index +0x10), CWorld::tick_, CWorldServer::AIMap() / Sectors().

## unit_motion_tick.cpp (baseline: At 109, asm 8, raw 1, byte arithmetic 1)

Converted: every At<> (109 -> 0). The local At template is gone; `ei/vcall.h` dropped (unused).
- The world is `CWorldServer*` (ManagerContextOf) / `CWorld*` (ObjectWorld) instead of void*.
- Replan's inlined RemoveAll of the path's cell list is now `MotionPath()->cells.RemoveAll()` (the
  template's order of stores is the original's: count, free, tail, head, free chain, blocks).
- The exhaustion code's "stamina" is the server object's vitals.mana, its rate vitals.maxMana.
- Left: the 8 `__asm` blocks (x87 sequences and the naked entry; out of this ticket's scope),
  CurrentFrame() / GameMode() address accessors, kQueueWaitLimit.

Unverified / risks:
- AIPath::reached / failed / flag6E are `bool`; the tick tests `failed == 1` and the original
  compares the byte with 1. Same result as long as the byte only holds 0 or 1 (it is written by
  setcc-style code as far as known); check if the build's code differs.
- `unknown_278` is uint32 in CUnit: compared as `static_cast<int>` and assigned through a cast.

## combat_motion.cpp (baseline: At 88, VCall 7, byte arithmetic 9)

Converted: At<> 88 -> 0, VCall 7 -> 0, byte arithmetic 9 -> 0 (the local At template and ei/vcall.h
are gone).
- VCalls: six releases (slot 4) are `CEngineObject::Release()`; the controller's slot 25 (0x64 / 4)
  in ResyncWithUnit is `CUnit::Controller()->ShowWeapon(unknown_270)`.
- kWorldServer / kAIMap / kSectorMap became `WorldServer()`, `WorldServer()->AIMap()`,
  `->Sectors()` (world_server.cpp's accessor of 007AFF60); Random() is `IsaacRandom::Global()`.
- The local PartyNode / PartyMap / FirstNode / NextNode walk is `CPlayer::units_` (+0x40) with
  `stlp::hash_map::First()` / `After()`: the same bucket walk (After starts at key % buckets + 1).
- LocalMotionOrder is now `{ MotionOrder header; PathCellList steps; }` (0x3C, static_asserted);
  the destructor writes `PathCellListVtable()` and calls the same RemoveAll forwarder.
- ResetController's spell list emptying is `itemSpells.RemoveAll()` (the element policy releases
  each item, then the stores in the original's order); AlarmReset gets the new `voice` member
  (+0xB10; CUnitServer is now 0xB20 bytes, static_asserted, as server_specific_manager.h says).
- The hero's "stamina" (+0x238) is `stats.loadFactor` (UnitStats +0x58: below 007C5448 / 007C5440
  the movement animation gets the limp bits 0x800000 / 0x400000).
- The player's +0x20 is `CPlayer::connection_`, +0x100 `sessionKind100_`, +0x108 `unknown_108`;
  the group at the connection's +0x510C is `CPlayerServer::partyUnits_`.
- ReportPath: the brain's +4 is `AIBrain::action` (unit_ai.h, now included), the path's +0x58 /
  +0x5C / +0x60 are `targetX`, `targetY`, `goal`.
- Left: HalfCell / HeroWalkLimit / HeroRunLimit and two `0x0073....` constants read through
  accessors (not counted), forwarders as they were.

Unverified / risks: the same bool caveat (`failed` read into a byte and compared with 1 in PathTo);
`WorldServer()` is an out-of-line call where the constants were (no side effects).

## net_state.cpp (baseline: At 86, VCall 28)

Not started yet.

## Where I stopped

See the per-file sections.
