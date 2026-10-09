# DR-C3: cleanup draft of unit_motion_tick.cpp, combat_motion.cpp, net_state.cpp

Branch draft-dr-c3. Drafted without building; finished (merged with main after A-U7, builds `verify` and
`release` clean, all build checks at 0, baseline updated). Not run yet: the scenarios below are next.
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

Converted: At<> 86 -> 0, VCall 28 -> 14. The exported signatures (`std::uint8_t*` objects) are
unchanged; the bodies view the object as CMapObject / CUnit.
- Named now: GetClassId (slot 12, 2x), the controller's Path (16, 3x), ShowWeapon (25, 3x: the
  old kSlotSetSpeedMode is the weapon slot), RestartAnimation (14; the time passed as a float, not
  its bits), PlayerChanged (18, 2x), CWorld::MoveObject (41), CStream::Position / Size (5 / 4) of
  the read snapshot, and the unit's slot 55 as `ObjectSlots(unit)->IsDead()` (the old name was
  kSlotIsRemote; map_object.h documents 55 as IsDead, units' +0x274).
- Left as VCall (14), all on slots that are placeholders in this branch or past the declared
  interface: map object 58 (AppearanceChanged; DR-C2: RefreshWounds) and 51 (SetHeading, takes the
  position's z bits; DR-C2: PlaceOnGround); companion 13 (EffectsChanged, 2x) and 16 (Hit, past
  CMapObjectCompanion's 15); controller 29 (2x; DR-C2 RefreshEquipment), 31 (2x; RefreshWounds),
  32 / 33 (SetArmourModels / SetWeaponModels), 41 (MotionChanged), 46 (StatChanged), 47 (Wound),
  past CUnitController's 40. After DR-C2 merges, declare their signatures and convert them.
- Members: field 0 packedVitals / PackedVitals()->hitPoints, 1 scale, 2 flags_ / positionX /
  orientation (the heading slot gets positionZ's bits), 3 effects / effectCount / MapEffect fields
  (type, power, ticksLeft (2 bytes), ending) and the effect CArray at &effectsVtable; unit 4
  Params() (+0x330; +0x437 is Params()->flags), body parts (+0x378 + k * 0x1C is
  bodyParts[k].durability / maxDurability), 6 moveMode / moveMode2 / activeWeapon / unknown_270,
  7 unknown_278 / dead / unknown_260 / unknown_25C, 8 unknown_254..258, 9 scriptName (new), 10 the
  unit's Motion() (segment, speedScale, baseTurnRate: the same fields CUnit::RefreshMotion uses),
  11 relationsA / relationsB / side / player (the players' +0xC is CEngineObject::id_ of CPlayer;
  the local view is LocalViewData, +0x84 new), 12 levels4C8, 13 unknown_438.

Unverified: field 7's `unknown_278` is uint32 (written through a cast from the sign-extended bit);
the reads of a CPlayer through LocalViewData::player (declared WorldPlayer*) reinterpret it.

## Where I stopped

All three files are drafted (nothing built). Next for the finishing worker: build both
configurations, fix compile errors (likely spots: includes added to the three files —
ai_map.h, ai_motion.h, map_object(_figures).h, player_messages.h, unit_ai.h, world.h — and the
CUnitServer layout edit in combat.h), run check_cleanup and `--update`, then the scenarios. After
DR-C2 merges, convert net_state.cpp's remaining 14 VCalls.

## Finished (builds)

- Merged main (A-U7). CUnitServer keeps main's names and types where both named a member: `queue`,
  `motion` (AIPath, `MotionPath()` returns it), `savedQueue`, `armourSpells` (`ArmourSpell` /
  `ArmourSpellList`, plain element policy; the draft's UnitItemSpell / UnitItemSpellElements are
  gone), `moveAnimationKey` / `moveAnimation`, `idleKey` / `idleAnimation`, `actionDone` (+0x978)
  and `voice` (int32[4]). From this draft only: stoodBlocked, movingAnimation, sneakWait,
  actingCheckTime, unknown_188 / 18C / 18D, ownSide, tamerSide, unknown_918 (void*), waitFrame.
- ResetController releases each armour spell's item itself, then `armourSpells.RemoveAll()` (same
  calls and stores in the same order as the original's walk).
- Compile fixes: unit_motion_tick.cpp's local `MotionOrder()` clashed with ai_motion.h's struct
  (now `MotionPathOf()`); CUnit::AttachCompanion's parameter hid the new `companion` member
  (renamed `attached`).
- Bool caveat settled: the three reads of AIPath::failed that the original compares with 1
  (MotionOver, Replan, PathTo) read the byte (`reinterpret_cast<const std::uint8_t&>`), so a value
  other than 0 / 1 behaves as in the original. Float points checked: RestartAnimation's float
  argument is the same 4 bytes the old code pushed as bits; the heading slot still gets
  positionZ's bits.
- Cleanup baseline: unit_motion_tick.cpp At 109 -> 0, byte arithmetic 1 -> 0; combat_motion.cpp
  gone from the baseline (all 0); net_state.cpp At 86 -> 0, VCall 28 -> 14 (the 14 wait for
  DR-C2's slot names).

## net_state.cpp: the last 14 VCalls (cleanup ticket)

All 14 are now named virtual calls (VCall 14 -> 0; the `ei/vcall.h` include is gone): map object
58 `RefreshWounds()` and 51 `PlaceOnGround(float offset)` (passes positionZ itself, same 4 bytes);
companion 13 `EffectsChanged(int first)` (2x), 16 `Hit(int damage, int kind)`; controller 29
`RefreshEquipment()` (2x), 31 `RefreshWounds()` (2x), 32 / 33 `SetArmourModels` / `SetWeaponModels`
`(const uint32_t* ids, uint32_t count)`, 41 `MotionChanged()`, 46 `StatChanged(uint32_t stat, float
change, uint8_t flags)` (RET 0xC; the byte is the unit's Params()->flags), 47 `Wound(float lost)`.
Signatures from the disassembly of the base / client implementations (RET n and the argument reads).
CUnitController now declares 41..47 (42 ObjectDeleted, 43 RestartFadeIn, 44 Resettle, 45
UpdateMoveFactors are place-holders by their existing names; not called here).
