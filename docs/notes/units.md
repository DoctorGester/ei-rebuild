# Units, creature statistics, levers and magic traps (00500000-00540000)

Code (layouts, vtable slots and per-function comments are in `unit.h`, `unit_db.h`, `magic_trap.h`, `lever.h`,
`unit_client_models.h`; each file's header says what it does): `unit*.cpp`, `magic_trap.cpp`, `lever_object.cpp`,
`torch_*.cpp`, `trap_object.cpp`, `map_object_kinds.cpp`, `object3d_*.cpp`, `net_state.cpp`, `map_object_create.cpp`.
Verify: `tests/verify/verify_<file>.cpp`. Scenarios: `units.txt`, `unit_create.txt` (eight prototypes, replace, kill,
remove), `magic_traps.txt`, `levers.txt`, `world_load` (save / load), and every scenario that starts the game
(serialisers, factory, constructors). Shared forwarders are in `originals_units.h`; of the callees only 006AC970
`CreateItem` (called by GiveStartingEquipment through its address) and FatalError 00480080 are Rebirth-patched.

## Address map

| Range | Contents |
|---|---|
| 00500000-00522000 | rendering / animation, map object base, CObject3D (`map_objects.md`) |
| 00522000-00529000 | creature statistics (UnitStats, 0x704 bytes, often a stack copy), DamageInfo helpers |
| 00529700-0052E000 | CTree, CBuilding, CStone, CStuffObject, CTrapObject, CMagicTrapObject (id 0x3B), CLeverObject, CTorchObject (0x3A) |
| 0052E000-00533E00 | CUnit (id 0x50, vtable 0073EA8C; CFakeUnit 0073BFE8 shares every rewritten slot) |
| 00533E00-0053A000 | CUnitClientSpecific (id 0x3D, vtable 0073ECD8): the controller of party units |
| 0053A000-00540000 | STLport instantiations of the unit database (library) |

## Facts

### Layouts and conventions

- Party units keep their items in the owning player's records (player + 0x70, 0x784-byte records found by unit id,
  006621B0; weapons +0x730, quick items +0x740, spells +0x760) instead of CUnit +0x440 / +0x450 / +0x460.
- Prototype tables (STLport vectors with a name -> record hash_map at +0x0C): 007AF358 prototypes (0x120; +0x08 indexes
  models), 007AF378 NPCs (0x60), 007AF398 models (0x168), 007AF268 skills, 007AF274 skill levels (0x44), 007AF3B8. The
  database object at 007AF358 is four 0x20-byte tables. 00514930 / 005144D0 / 005145D0 are `operator[]` on the name maps
  (an unknown name is inserted with a null record).
- CUnit slots rewritten: 3 DestroyUnit 0052E590, 5 ReleaseUnit 0052E740, 6 Tick 0052F7D0, 19 AttachCompanion, 25
  AdvanceUnitTime 0052FC40, 26 LoadUnitFigures, 27 RecomputeUnitScale, 28 EffectsChanged, 30 ResetUnitScale, 32
  StatsChanged, 33 ScaleEffectChanged, 34 PackPrototypeStats 0052EB70, 35 SetServerStats, 37 UpdateSizeClasses, 48
  StandPoint, 49 / 50 network state, 51 PlaceUnitOnGround, 52 / 53 unit points. Create 0052E400 is `CreateUnit`: a member
  named Create would override CEngineObject's virtual.
- Controller slots are called through `CUnitController` (unit.h; `CUnit::Controller()`): an interface declaration the
  controller classes do not derive from, so their registered slot members stay non-virtual. Its unit_ is +0x1C; slots 48
  Died, 49 EffectsChanged, 50 GetAttackParams, 51 Say, 52 StopSpeech; UpdateSizeClasses and Resettle return EAX
  (std::uintptr_t), Level int; slot 19 RefreshVitals takes an int (0052EB30 pushes a whole dword, the bodies read the low
  byte). UnitPrototypeRecord +0x50 is `level` (CUnit::Level without a text key). World heights: CWorld slots 29 / 30.
  unit_update.cpp is clean (C10; its `_ftol` `__asm` and float_control stay).
- ST0 results are `double` (the original returns floats unrounded and 00532A20 truncates `Reduce` with `_ftol`): UnitStats
  Period20, AttributeWithBonus, SkillWithBonus, Value6E8, Value6E9, Value6E9Arg, Reduce, ReduceKind1, CUnit MoveSpeed2,
  MoveSpeedFor, ScaledValue, BaseSpeed, ModeSpeed, TotalItemWeight, AnimationRatio, UnitParams::Period. UnitStats level
  values are unsigned dwords the original loads with FILD of a zero-extended qword (volatile int64 here). `unit*.cpp` use
  `float_control(precise, on)`. Rewrites call each other through game addresses (`Call*` forwarders) so nested wrappers
  see the calls.

### Statistics in play

- `UnitVitals::Regenerate` (00523740, every world update, from the controller 00552300): every 15th call mana (unless the
  unit has an order) gains `(1 + 0.01 * skill 19) * regen * maximum`, hit points heal the slots by `maximum * (1 + 0.01 *
  skill 18) * regen` (times effect 0x1F when positive) through `HealSlots`; returning 1 triggers `Recompute` (00524560):
  stats from bases plus skills 23-25, protection and value pairs from the strongest effect of each type, maxima from the
  experience curves `base ^ (ln(experience / divisor) / ln(logBase)) * stat / divisor * scale * (1 + 0.01 * skill)` (FYL2X
  and `_CIpow`, inline assembly in the original's order), strengthen / weaken scale (at least 1), slot maxima, capacity,
  load, legs / arms factors, rate. Constants: curves 007C5454..007C5480, slot ratio thresholds 007C543C..007C544C, wear
  threshold 007C549C, difficulty table 007C54A0 indexed by 0079BEC0, hit log stream 007AF284.
- Hits: `ApplyHit` (005254B0, from CUnitServer::TakeHit): damage per type beyond the vitals' protection (times the
  difficulty without a player), `WearItem` (005253E0; remarks 0x24 / 0x26), `DamageSlot` (00525070) on the hit location or
  all six slots for 6: layers (text-key units; `WearArmour` 00525720, tables 0073E09C / A0 / A4, remarks 0x23 / 0x25),
  backstab multiplier, sever (-5 x maximum; remark 0x1E arm / 0x1F leg) or disable, log "<type> passed damage = <n>[ HL
  was crippled| HL is dead]"; then 00532AC0 drops broken equipment. `IsBehind` (00530A60): units with a player and no bow /
  crossbow; behind when the dot with the target's facing is below -0.5 (or NaN). `PickLocation` (00525B20): the aimed slot
  is kept while its kind is 3; else a neighbour table (head 1, 2, 3; body 0, 2, 3, 4, 5; hand 2: 3, 1; hand 3: 2, 1; leg 4:
  5, 1; leg 5: 4, 1), then a random working slot (inlined `IsaacRandom::Next`), none gives 1.
- Raises: GiveStrength / Dexterity / Intelligence (script 0xCE-0xD0) -> RaiseStrength / Dexterity / Intelligence (00529090 /
  00529220 / 00529400): base + amount, maxima recomputed (strength and dexterity round the curve product to float before
  the skill percentage), strength widens the unit by 0.2, dexterity lowers y by 0.2 (at least 0), then Recompute.
  GiveSkill -> RaiseSkill (00528AB0) / RaiseAttribute (00528DA0): the level byte grows and `truncate(1.61 ^ (new /
  007C5480) * 007C547C - 1.61 ^ (old / 007C5480) * 007C547C)` is booked as experience and spent experience.
  AddScaledExperience (00523960) is times 1.04 ^ (intelligence - 25).
- Skills: `SkillPrice` (00528050): worth = 1.61 ^ (record +0x30 / 007C5480) * 007C547C; cost = _ftol(worth - worth of the
  record before it (read unchecked)) when index % 3 > 0, else worth - worth of 0; price = 007C5484 ^ (byte sum of the 0x24
  skill level bytes) * cost, kept to one significant digit (two when the first is 1-3), small prices rounded up. `CanLearn`
  (005260A0) checks the learnable byte (+0x40), stat minima, required attribute and skill level, that no level of the same
  skill id (+0x10) is known in another skill, and that the level is new; `LearnSkill` (00526450) keeps the hit point
  proportion. `WeaponSkillBonus` (00526C70; 00527850 on the packed `UnitParams`): types 0-4 skill 0 and the weapon
  prototype's +0x7B, 5-6 skill 1 and +0x7C, each plus (stat named by the skill's first letter - 25) plus the level value
  of skill `type`; type 8 skill 0 and +0x7B; others 0. `SpellSchoolSkill` (00527EB0: school 4 -> 3, 6 / 7 -> 5; skill byte
  +0x6CE / +0x6CF / +0x6D0 plus level value of skill school + 7 plus intelligence - 25). `VisionRange` (005271E0):
  valuesB0[0] x ((1 - day) + valuesB0[1] % x day).
- Armour layers: table 0073E048 gives three body slots per armour type (7: none); `AddArmourLayer` (005230C0) adds the
  piece's seven absorb floats (+0x4C first slot, +0x68 others) to layer `GetTypeGroup()` (2 for types 0-2 else 1), ORs the
  mask (+0x84 / +0x88), state 3; `RebuildArmourLayers` zeroes layers 1 and 2 (0x90 bytes) and re-adds each worn piece whose
  +0x20 rounds above 0; `PackBodyProtection` (00523020): out[1 + k] = round(current protection of type k + absorb values of
  the body slot's layers with state >= 2, layer 2 down to 0 for text-key units else layer 0); out[0] is the caller's attack
  value. `ProtectionBlock` (005270E0, the panel): text-key units weight worn slots by 0073E0FC and add the packed vitals;
  others packed x difficulty 007C54A0.

### Creation, databases, save, network

- Factory (CMapObjectManager slot 14, 00512730) by class id: 0x32-0x34 units (0x4DC, 0052E110), 0x35-0x38 (0x238), 0x39
  (0x580, 004466D0), 0x3A (0x23C, 0052BEE0), 0x3B (0x290, 00529DD0), 0x3C (0x264), 0x300D (0x78), 0x3010 (0x23C). The zone
  loader then calls `ApplyPrototype` (00513B60): without a prototype defaults (005134F0) and "Can't find prototype %s for
  object %d"; units get vitals / statistics from the records (005136D0: hit points / mana from prototype +0x48 / +0x4C,
  regeneration from the model +0x08 / +0x0C, protection = model's seven values x prototype +0x58; stats from the NPC record
  or a third of the hit points / mana) via CMapObject::SetServerData and slot 35, stand lower by the model's +0x104, take the
  prototype's scale (00517700) and second prototype (005308A0). 00513DA0 builds an NPC's statistics alone; `ResetToBase`
  (00524CD0) refreshes current values from bases. `GiveStartingEquipment` (005141F0, zone loader record kind 5): a type 0x32
  unit gets each armour of prototype +0x10C (006AC970; Rebirth's FixCreateItem runs; equipped), weapon +0x118 unless "none"
  (slot to +0x270 unless -1) and +0x11C; every unit each spell of +0x100 (006AD1E0, AddSpell); an unmakeable piece is
  FatalError.
- **Database records**: `units.udb` is loaded once at startup by 0053A200 (Rebirth-patched) through the tagged-chunk
  archive: chunk 1 skill levels (0x44), 2 models (0x168, -> 0052DC70), 3 prototypes (0x120, -> 0052D740), 4 NPCs (0x60, ->
  0052DA60); then name indexes are filled, a prototype's +0x08 becomes the model's index and each model slot's +0x04 the
  skill level record's. Serialisers call the archive's field / CString field / enter / leave (004092B0 / 004093A0 /
  004098D0 / 00409980) in the original's order; bytes 0x38..0x47 of a prototype and 0x128..0x137 / 0x144..0x14F of a model
  are not serialised. 0057DEE0 re-creates the database when the file changes (database.res <-> databaseLMP.res).
  `CountSoundFiles` (0053A8E0): model sound names lower-cased, "*.wav" files counted by prefix.
- **Save / Load** (`SaveUnit` 005321C0 slot 10, `LoadUnit` 00531A80 slot 9) order: map object part (0050F7A0 / 0050F900),
  controller, name (+0x238), relations, player id (-1), side, +0x254, +0x255, +0x258..+0x278 (active weapon among them),
  +0x27C sub-object (005C7860 / 005C79B0), +0x320, +0x324, +0x328, active weapon again, weapons / quick items / spells /
  armour as item ids, +0x4C8 (0x14 bytes), +0x248, carried items, +0x32C, +0x438 (version 0x49), prototype names (version
  0x4D; the second from 0x6A, before that looked up by the first name with 00514930). Load resolves items in the world's
  item manager (ManagerContext slot 18), moves the unit between players (00665B50 / 00665A70, MarkChanged(0xB), controller
  slot 18) and calls slot 32. UnitStats::LoadFrom (005287A0) sizes by format version 0x45 / 0x50 / 0x63 (pending experience
  0 first).
- **Network state** (DeltaState at +0x1C4; slots 49 / 50 and map objects 0050EC30 / 0050EAF0, `net_state.cpp`): map object
  fields 0 the 0x18 bytes at +0x17C, 1 scale +0xC4 (read: SetPosition 00517700), 2 hidden bit (flag 8), position +0x1C,
  orientation +0x34 (read: world slots 41 / 51), 3 effects (count byte, then per 0x14-byte effect bytes +0, +1, word +4,
  byte +0x10; read: CArray::SetSize, then the companion +0x170's slot 13 with `first == 0` unless a unit). Units add 4 stats
  block +0x330 (read: stat fractions before / after give controller slot 46 per changed stat, slot 31 when any changed, a
  hit point drop or flag 0x10 in +0x437 gives the companion's slot 16 with `_ftol(lost * max)`), 5 the 11 equipment ids
  (weapon -1 with flag 0x40000000; writing refreshes a class 0x3E controller, slot 29), 6 +0x264 / +0x268 / +0x26C (+0x270
  = +0x26C, slot 25 on a change), 7 packed bits of +0x25C / +0x260 / +0x274 / +0x278, 8 +0x254, +0x255, +0x258, +0x256, 9
  the CString +0x238, 10 path (005C7DB0 / 005C7FC0) and motion speeds, 11 masks, +0x250, player id, 12 +0x4C8, 13 +0x438,
  then re-equip (slots 32 / 33) and refresh (25, 29). The field 3 count is one byte read into a word. The wrapper compares
  only the effect bytes the reader writes (the companion's slot 13 changes the others) and restores byte +0x76, which slot 41
  (00535850) clears when it snaps the orientation to the path.
- **Unit points** (slots 52 / 53): `UnitActionPoint` (0052FDF0, RET 4) always returns `out` in EAX although Ghidra shows
  void (CUnitServer::ApproachTarget reads it; returning nothing crashed combat, sound and units scenarios); a missing
  animation entry logs "Can't find animation entry index"; it falls back to the position where size class 3 cannot stand
  (005BA790). `UnitPredictPosition` (0052FCC0, RET 8) samples the motion for client units following a path order (kind 2).
- **Client path placement** (`ClientPlaceOnPath` 00535850, RET 8; from slot 41 and 00535B30; `unit_client_path.cpp`): the
  two heading angles must go through a float store (`Store`) as the original's FSTP (unrounded, the quaternion's sign flips
  near +-pi).
- Size class and figure queries: `UpdateSizeClass` (00530570) relies on controller slot 45 (server's UpdateMoveFactors;
  empty on client / base controllers) so verify runs it twice; `FindAnimation` (005301B0) works only for type_ 0x32..0x34
  and draws with `rand()` through 005190C0 / 00519380; `GroundHeight` (00530280) has a body too short for a trampoline
  (verify uses a copy of its 98 bytes made at DLL load); `BodyExtent` (005304A0) feeds CUnitServer::UpdateSizeClasses.
- Client models (CUnitClientSpecific id 0x3D; +0xA0 packed ids of the seven armour slots, +0xBC four weapons): armour part
  rows at 0073EC24 (body "bd", "rh1".."lh3"; trousers "hp", "rl1".."ll3"; helmet "hr" ("hr.<nn>" with the prototype's +0x10)
  and "hd"; armour slots 0 / 1 / 2 use rows 2 / 0 / 1; slots 3-6 have no parts); skin composition through the texture
  manager (007AF744, 00693180): layers 0x32 skin, 0x3005 armour, 0x3004 weapon, names "mo<id>p" / "prot<prototype>p" (...s
  for two layers); wounds "<model><part>w<level>.mmp", cache "mo<id>w" (levels 0-3 by durability ratio; heroes' armour with
  ArmorRecord +0xB4 = 0 hides). `ClientGetAttackParams` (00538960, slot 50) reuses 005285D0, a byte-for-byte copy of
  00523D30 registered with the same function. Controller fields (unit_client_models.h): +0x2C last animation time, +0x38
  motion order (slot 16), +0x74 / +0x75 weapon / arrow hidden, +0x78 fade-in (15 ticks), +0x7C colour flash countdown, +0x84
  footprint countdown (-1 from 00533FE0 until the first footprint; it only decrements above 0), +0x90 waiting speech sound,
  +0x9C speech queue (STLport list of ClientSpeech; delay counts down, plays at 0, erased once its sound ended; kinds 2 /
  0x1C skip while the previous one plays; kinds 0x1D-0x2A queue behind; `talking` sets the server unit's flag 0x800000),
  +0xD0..+0xD8 blink (every 2..9 ticks, rand()). A unit dead for more than 50 ticks past its +0x254 byte is not drawn
  (flags_ 8). Blink textures: model +0x158..+0x164. Sounds: footsteps by the model's names +0x138 / counts +0x144 per ground
  kind (platform -> kind 2); action sounds at frame +0x44 in [from, to), sounds +0x48, folder second prototype +0x34 or model
  +0x124, idle / attack chances +0x150 / +0x154; attack / death / skill sounds at priority 1000, idle / hit at 0; camera
  shake by prototype +0x30; footprints through 0056D150 with model +0x10C / +0x110 / +0x11C and prototype +0xB0 as scale.
- **Levers**: lever table 0079C1A0 (`lever_table.cpp`), records from `Levers.ldb` (00463030), found by model name through
  0052CB40 (unknown names add a null record). `SetState` (0052B7A0): a negative (or NaN) duration takes the model's default
  (map 0079C1AC) or 10; a negative state is the next (wraps when cycling); with a live world units within radius + 0.5
  leave the sector map around the move, the end tick is (tick + duration) + 1, MarkChanged(5), lifted units that cannot stay
  go to a free cell and are resynced (00554450). `UseSpots` (0052BBE0, slot 44, from FindUseSpot 0054DF50).
- **Magic traps** (class 0x3B, 0x290 bytes, vtable 0073E63C, derived from CMapObject with members +0x238..+0x28C; flags_
  0x80000 armed, a new trap is armed until a script disarms it; 0x40 kind): `UpdateTrap` (slot 6): server only (world slot
  26: 1 for CWorldServer 0058A380, 0 for CWorldClient 0055EB30; `world_effect_internal.h`'s IsClientSide is this slot,
  misnamed); period <= 0: `UpdateVictims` (0052A680, armed traps ask `FindTrapVictims` 0052A920 for newcomers and cast),
  else the countdown runs and `FireAtOccupants` (0052AB30) fires every tick until a cast resets it. `CastAtUnits` (0052A4B0)
  casts at each point {x, y, 0} (spells without flag 0x10000000 and a trap with points) or at each unit; with +0x28C
  narrowed to the last unit; casts through 0058E850 (007AFF60) with caster 0. `SetUp` (0052A2C0, per MagicTrap record at
  +0x8C from the zone loader). `ForgetObject` (slot 56, from RemoveObject 005935C0) removes every node equal to the object
  (null included). The zone loader registers traps in no name map (005927C0, record type 7): scripts use `GetObject(id)`.
- Torch (class 0x3A, vtable 0073E834; spec at +0x238) `TorchCreate` (0052BFC0); `CreateUnit` (0052E400) calls CUnit::Reset
  and sets 14 network fields. `TakeRandomItem` (00531400, stealing): carried items first, else a random filled quick item
  slot emptied without release. CTrapObject (placed trap, class 0x55) is armed by 0058ECE0 when a trap item is used and sets
  off "Mushroom" particles through a world script.

## Rewritten

V: P pure (float bits); O output (original into a scratch copy); S stateful (ours, object snapshotted and restored,
original, compared: unit 0x4DC, stats 0x704, vitals 0x58); C capture streams; ours = consistency checks.

| Address | Name | File | V |
|---|---|---|---|
| 0050E9F0, 0050F630 | CUnit::MarkChanged, HasEffect (CMapObject methods kept on CUnit: forwarders from CMapObject; standalone: move them) | unit.cpp | S / P |
| 0052E940, 00530180, 00530120, 0052E9B0, 0052EA90, 0052EAD0, 0052EAF0, 0052EB10, 0052EBE0, 0052F7F0 (46) | AddHostileMask, SetRelationsB, SetSide, SetUnknown438 / 254 / 25C / 260, SetMoveModes, SetFlag40000000Off, SetFlag1000000 | unit.cpp | S |
| 00531070, 00531180, 00531820, 005310D0, 005311E0 | AddWeapon, AddQuickItem, AddSpell, RemoveWeapon, RemoveQuickItem | unit.cpp | S / ours |
| 00530150, 005318B0, 00531920, 005319A0, 00531A10, 00530FA0, 005307B0, 0052F820, 0052F8B0, 0052FA20, 0052FAF0, 0052FBC0, 00530370, 005302F0, 00530930, 00530970, 005309E0, 005327D0, 00532DA0, 00532E00, 005271C0 | IsHostileTo, GetWeapon, GetQuickItem, GetSpell, GetSpellObject, HasRangedWeapon, TotalItemWeight, CanCastSpell, CanUseQuickItem, MoveSpeed2, MoveSpeedFor, ScaledValue, BaseSpeed, ModeSpeed, GetPrototype, GetPrototypeModel, GetPrototype23C, ResistanceTo, AcceptsTarget (18), AnimationRatio, UnitParams::Period | unit.cpp | P |
| 0052EC10, 0052F920, 005308A0 | GetEquipmentIds, GetMoveSpeeds, SetPrototype23C | unit.cpp | O / O / S |
| 005156A0, 005156C0, 005104D0, 005104F0 | CMapObjectServer / ClientSpecific IsDead, IsOverkilled (17 / 18) | unit.cpp | P |
| 00523CD0, 00524020, 00524280, 005259B0, 00525CD0, 00525D60, 00525DF0, 00525E10, 00526780, 005267E0, 00527030, 00527050, 00528270, 00522A80, 00522B80, 00528840, 00522B40 | UnitStats Period20, ReduceKind1, Value6E8, Reduce, Value6E9, Value6E9Arg, AttributeBase, AttributeWithBonus, WeaponSkill, SkillLevelValue, SkillBase, SkillWithBonus, SkillLevelRecord; LockKey::Opens, HasElementalDamage; GetFactors (O), ResetDamageInfo (O) | unit_stats.cpp | P |
| 00523740, 00524560, 005243D0, 00525800, 00525900, 00525930, 00525A00, 00525A70, 00525AF0, 00523C80, 00523920, 00523960, 00523C20, 005239B0 | UnitVitals::Regenerate, Recompute, RecomputeHitPoints, HealSlots, Heal, RegenerateSlots, SetSlotMaxima, ScaleSlotMaxima, RefillSlots, RestoreSlots, AddExperience, AddScaledExperience, TakePendingExperience, GainExperience | unit_vitals.cpp | S |
| 00524060, 00524110, 005241C0, 00523E80, 00523F50, 00524320, 005282D0, 00523D30, 00523E00, 00523DB0 | LegsFactor, ArmsFactor, MovementState, HitPointCurve, ManaCurve, RateValue, SkillLevelValueCopy, ScaleDamageByEffects, ScaleHitPointsByEffects, HitPointEffectScale | unit_vitals.cpp | P |
| 00528AB0, 00528DA0, 00529090, 00529220, 00529400 | RaiseSkill, RaiseAttribute, RaiseStrength / Dexterity / Intelligence | unit_vitals.cpp | S |
| 00522C50, 00522CE0, 00523370, 005233B0, 00523550, 005233E0, 005232B0, 00523600, 005236A0, 005287A0, 00528800 | UnitVitals Pack / Reset, UnitStats Pack / Reset / ClearOuterLayers, UnitVitalsPacked / UnitParams / UnitStatSlot / UnitStatLayer Reset, UnitStats::LoadFrom (ours) / SaveTo (C) | unit_stats_pack.cpp, unit_stats_io.cpp | O |
| 005254B0, 00525070, 005253E0, 00525720, 00524FE0, 00530A60 | ApplyHit, DamageSlot, WearItem, WearArmour (ours), UnitVitals::TakeHit (S), CUnit::IsBehind (P) | unit_hits.cpp | |
| 0052F7D0 (6), 00532A80 (27), 00532AB0 (28), 00532AA0 (30), 00530A50 (37), 0052EB30 (33), 00530A20 (35), 0052E910 (26), 0052E870 (19), 0052EB50 (32), 005303A0 (48), 0052F740 (51), 00532A20, 00532D40 | CUnit virtual slots (Tick returns 1; LoadUnitFigures sets flags_ 0x80; AttachCompanion returns the reference count), StandPoint, PlaceUnitOnGround, SetFromStats, Level | unit_update.cpp | ours / O / S / P |
| 00530730, 00530840, 005315B0, 00531220, 005313B0, 00531110, 005308D0, 00530920, 005316F0 | EquipArmour, UnequipArmour, ReleaseAllItems, AddCarried, RemoveCarried, SendEquipment, SetPrototype23CByName / Index, CollectItems (items and their spells; armour spell refresh 005528F0; O) | unit_items.cpp, unit_item_lists.cpp | ours |
| 005321C0, 00531A80 | SaveUnit (C), LoadUnit (ours) | unit_save.cpp | |
| 0050EAF0, 0052EC70, 0050EC30, 0052EF90 | map object / unit net state write / read | net_state.cpp | differential, sampled |
| 00514930, 005144D0, 005145D0, 00513240, 005134F0, 005136D0, 00513DA0, 00524CD0, 00526450, 005260A0, 00526C70, 00513B60, 00512730 | prototype / NPC FindByName, PrototypeObjectClass, SetDefaults, BuildFromPrototype, BuildNpcStats (O), ResetToBase, LearnSkill (S), CanLearn, WeaponSkillBonus (P), ApplyPrototype, CMapObjectManager::CreateObject (ours) | unit_create*.cpp | |
| 00552F40, 00552780, 00532AC0, 005141F0 | CUnitServer::SetStats, Shout, CUnit::DropBrokenEquipment, GiveStartingEquipment | unit_create.cpp, unit_equipment.cpp | ours |
| 0052D740, 0052DC70, 0052DA60, 0053A050, 00539CE0, 0051FB50, 0053EE70, 0053A8E0 | Prototype / Model / Npc record Serialize (loading: ours, then the original reads the same record again, compared), DestroyUnitDatabase, NpcTable, SkillDatabase, ModelRecord, CountSoundFiles (S) | unit_db*.cpp, unit_sound_counts.cpp | |
| 00539A10, 00539B20, 00539E70, 0053EBC0, 0053ED30, 0053A7B0 | ConstructUnitDatabase (the tables' STLport constructors forwarded), DestroyPrototypeTable / ModelTable (0057DEE0's reload; the name map clear shared with NpcTable as `DestroyNames`), DestroyPrototypeRecord / NpcRecord, ChunkArchive::Construct (= ConstructLocal; every database load) | unit_db_destroy.cpp, chunk_archive.cpp | ours |
| 00527850, 00528050, 005285D0, 00538960, 005230C0, 00523300, 00523020, 00525E90, 00527EB0, 005270E0, 00526860 | WeaponSkillBonus (packed), SkillPrice, ScaleDamageByEffects copy, ClientGetAttackParams (O, 4 ULPs), AddArmourLayer, RebuildArmourLayers, PackBodyProtection, AttributeWithItems, SpellSchoolSkill, ProtectionBlock, WeaponPrototypeSkillBonus | unit_params_*.cpp, unit_skill_price.cpp, unit_armour_layers.cpp | P / O |
| 00530570, 00525B20, 00531400, 005301B0, 00530240, 005304A0, 00530280, 00532860, 0052FCC0, 0052FDF0, 00535850 | UpdateSizeClass, PickLocation, TakeRandomItem, FindAnimation, AnimationEntry, BodyExtent, GroundHeight, EffectPoint (skipped where the original rotates uninitialised stack), UnitPredictPosition, UnitActionPoint, ClientPlaceOnPath | unit_size_class.cpp, unit_hit_location.cpp, unit_steal.cpp, unit_figure_queries.cpp, unit_points.cpp, unit_client_path.cpp | differential (random generators saved; 8 ULPs, quaternion strict) |
| 0052E400, 0052BFC0, 0052E970, 0052E9D0, 005300D0, 0052E740, 0052E590, 0052EB70 | CUnit::CreateUnit, TorchCreate, SetHostileMask, RefreshMotion (path forgets the unit, motion +0x27C rebuilt from GetMoveSpeeds; part 10), SetPlayer, ReleaseUnit, DestroyUnit, PackPrototypeStats (differential) | map_object_create.cpp, unit_controller.cpp, unit_prototype_stats.cpp | ours |
| 0052FC40, 0050F170 | AdvanceUnitTime (controller slot 40, then a party unit's sight on the client world, 0056D740), CMapObject::SetVisibilityLevel (47) | unit_vision.cpp | ours |
| 005341A0, 005344E0, 005344A0, 00539160, 00537360, 0051B6D0, 0051B6F0 | CUnitClientSpecific::UpdateClient (6), FiguresLoaded (13), RestartFadeIn (43; EAX 0), Level (24: ln(packed experience / 007C5488) / ln(007C548C), rounded; compared), RefreshWounds (31: through 005370F0 / 0051C3D0, else 0051C920), CObject3DClientSpecific::FirstDrawnPart / NextDrawnPart (cursor +0x48) | unit_client_update.cpp | ours |
| 00537CB0, 00538C20, 00535590, 00535CB0, 00536300, 00536670, 005381B0, 005385A0 | ShowWeaponParts, UpdateArrow (bow arrow "rh3.arrow00" between frames +0x28 / +0x2C), ChangeWeaponModel, CollectArmourPartNames, BuildSkinLayers, ComposeSkin, WearArmourModel, RemoveArmourModel | unit_client_models.cpp | ours / O |
| 00536100, 00536220, 00538140, 00538860, 005388C0, 00538920 | RefreshSkins (29), RefreshSimpleSkin (30), SetArmourModels (32), SetWeaponModels (33), AddWeaponModel (34), RemoveWeaponModel (35) | unit_client_equipment.cpp | ours |
| 00534590, 00534760, 00534B60 | FootstepSound, ActionSound (rand() saved, compared), PlayStepSounds (ours: it plays sounds, shakes the camera and adds footprints) | unit_client_sounds.cpp | |
| 00535B30, 005352B0, 00535480, 00537500, 00537C00, 005373D0, 00534140, 00533FE0, 005340C0, 00533AD0 (raw copy compared), 00533EA0 | AdvanceTime (40), RestartAnimation (14), StartIdleAnimation, HitEffect (46: hit sound by weapon class and material, blood particles, decal), SparkEffect (47), DeathEffect (48), OwnerPlayerChanged (18: party light), CreateClient / DestroyClient (2 / 3), ConstructClient (factory 0042D700), DestructClientBody (base vtable 0073EDBC) | unit_client_animation.cpp, unit_client_effects.cpp, unit_client_lifetime.cpp | ours |
| 00538ED0, 00539010, 00533980, 00537D80, 00538040, 005380D0, 00537F60, 00537FD0 | BlinkClose / BlinkOpen, BlitMipChain, Say (51), StopSpeech (52), Speak, SpeechReplaced / SpeechFinished (filters compared) | unit_client_blink.cpp, unit_client_speech.cpp | ours |
| 00536EA0, 005370F0, 0051C920, 00535770, 00538DB0 | WoundLevels (O), ComposeWoundsTexture, HideWounds (+0xD8 texture, +0xDC material), WeaponPoint (node by WeaponBaseFigure or "baserh3", its +0x74), LoadFaceTexture ("<model>mouth[_NN]" kind 0x3D) | unit_client_wounds.cpp | ours / O |
| 005179E0, 00516DA0, 00518100, 00516B50, 00517240, 00517110, 00519A10, 005190C0, 00519380, 0051A010, 0051A190 | CObject3D::SetUpAnimatedFigure, MakeFigure (from the cache: whole for an animated object without names (slot 4), else from the piece names (slot 3); LoadParts, bounds, flags_ 0x80), UpdatePartBounds (S, CheckNear), CollectPartsByPrefix, LoadPart (piece box, BASE / CROWN / EMPTY flags), LoadParts, AlignedPtrArray::Add, RandomAnimation (variant bits, chance +0x1C narrowing; S with rand saved), RandomAnimationOfKind, the candidate list's SetAtGrow / SetSize copies | object3d_figure_setup.cpp, object3d_animations.cpp | ours |
| 0051AD20, 0051AC30, 0051C970 | CObject3DClientSpecific::ExtraScale (18: +0xF0..+0xF8), CObject3DServer's (1, 1, 1), RegisterPartMaterials (world slot 33 material manager, 0068EAF0) | object3d_specific_slots.cpp | P / ours |
| 0052B7A0, 0052BBE0, 0052B1F0-0052BE40 (22) | Lever::SetState, UseSpots (O, 8 ULPs); Lever Construct, DestructBody / DeletingDestruct, Create (2), Update (6), Load / Save, net state, SetUp (zone record), Enable, SetLock (S), slots 29 / 38-43 / 60, class name / id | unit_lever.cpp, lever_object.cpp | |
| 00529DD0, 00529F90, 0052A100, 0052A200, 0052AC20, 0052AB30, 0052AC90, 0052A2C0, 0052A4B0, 0052A680, 0052ADC0, 0052B070 (C), 0052A920 (O) | MagicTrap Construct, DestructBody, CreateTrap, DestroyTrap, UpdateTrap, FireAtOccupants, ForgetObject, SetUp, CastAtUnits, UpdateVictims, LoadTrap (recording stream, round trip through the original Save), SaveTrap, FindTrapVictims | magic_trap.cpp | ours |
| 0052BEE0-0052C2C0 (13), 0052C490-0052C680 (7) | CTorchObject lifetime, Destroy, Update, Load / Save, AttachTorchObject, net state; CTorchObjectSpec Create / Destroy / Load / Save / setters 16-18 | torch_object.cpp, torch_spec.cpp | C / S / P |
| 005296E0-00529A10 (20), 0052B030 / 0052B050 / 0052A8F0 / 0050E220 / 0050E290 | CTree / CBuilding / CStone / CStuffObject (PlainMapObject<vtable>) lifetimes and class slots; AIRef Reset / Assign / Load / Save | map_object_kinds.cpp, counted_ref.cpp | |
| 00529A80-00529DA0 (14), 00529F50 / 00529F60 / 00529F70, 00522AD0 | CTrapObject (class 0x55), the magic trap's class slots and deleting destructor, LockKey::AcceptedByTrap | trap_object.cpp | |

## Original bugs and oddities kept on purpose

- Client `Level` (00539160) of a unit with experience 0: ln 0 = -inf and FISTP turns it into 0x80000000 (INT_MIN), not 0;
  ours returns the same (X11: the CRT's lrint gave 0). A ratio within an ULP of .5 may round differently (std::log).
- PlayStepSounds: a step frame of 1 ends the step list (the frame is the entry minus 1, tested for 0), so an entry 0 never
  matches; the footprint scale reads the prototype unchecked; the "shown" flag goes to 0056D150 with stack garbage in its
  upper bytes (ours 0 / 1); FootstepSound divides by a missing ground kind's count of 0 when a platform turns the kind into 2.
- AddWeapon / AddQuickItem / AddSpell return 1 when the item is already in any slot (indistinguishable from "added to slot
  1"); AddSpell has no null check; GiveStartingEquipment adds a weapon that could not be made (AddWeapon(null) returns 0,
  +0x270 becomes 0). GetSpellObject reads a party spell entry's +0x44 unchecked; GetQuickItem returns null for a party item
  whose +0x48 is not positive but the non-party path returns the slot as is; SkillWithBonus reads the record name without a
  range check; several setters return what MarkChanged leaves in EAX; CanCastSpell / CanUseQuickItem take an unread second
  argument (RET 8), GetFactors a third, 00525D60 is a copy of 00525CD0 with an unused argument.
- RaiseSkill / RaiseAttribute store the level as a byte (wraps) but book the worth of the unwrapped sum; RaiseStrength
  computes the maximum from the current strength; HealSlots returns AL = 1 with FPU status bits in the rest of EAX;
  Regenerate's third argument is a byte whose upper bytes are the controller's +0x190 bits; UnitStats::Reset leaves +0x6F7,
  +0x34..+0x4C, +0x58..+0x6C, +0x8C and +0x6FC alone.
- DamageSlot logs "termical" (sic) and one line per damage type; ApplyHit wears the weapon by the part each damage type the
  protection absorbed; IsBehind is never true without a player; SetPrototype23CByName returns the low half of its division
  product in EAX.
- Save writes the active weapon twice (the second read wins) and copies the prototype name from address 0 for an
  out-of-range index; name lookups insert unknown names (null record); BuildNpcStats reads the prototype's model index
  before testing the prototype for null.
- **Known divergence**: ApplyPrototype hands the object vitals and statistics built in uninitialised stack copies (the first
  dword of each OR 1, statistics +0x58, +0x5C and the last skill level byte are stack garbage); ours start from zero, in
  fields nothing seen reads before they are recomputed. `EffectPoint` on a server world (or a hero without "rh3") turns a
  vector the original never wrote (ours zero; the wrapper does not compare those calls).
- CollectArmourPartNames pushes the five empty names of the helmet row; WearArmourModel stores the id even for armour slots
  3-6; RemoveArmourModel empties the slot before looking up the figure; DropBrokenEquipment gives broken pieces to the
  player and releases the unit's reference after; CUnit::Reset clears item slots without releasing; packed WeaponSkillBonus
  reads the level byte at +0xE3 + type for any type below 5, negative ones too.
- UseSpots trusts the record's spot count (a count above 2 reads the count and factor as offsets); UpdateSizeClass reads the
  prototype's +0x24 unchecked (an out-of-range index reads address 0x24); CastAtUnits returns without restarting the
  countdown when +0x28C finds no unit and changes the caller's list; ChangeWeaponModel leaves +0x75 when the new slot holds
  no weapon; UpdateVictims tests only x and y against the circles while FindTrapVictims uses the sector map (a unit in two
  circles is found twice); a periodic trap whose countdown reached 0 and found nobody searches again every tick;
  PickLocation reads a slot kind before its range check; the client attack figures keep the damage type for a weaponless
  unit whose model weighs no damage type; FindAnimation's last fallback gets the kind without the movement substitution.
- LoadTrap reads the spell id and counts into its argument's stack slot (a failed read leaves the previous value, at first
  the stream pointer; ours starts its local with it) and appends to the list instead of replacing it; the trap constructor
  releases a spell and empties lists it has just set empty; VictimList::Serialize (0052C740, library) writes raw pointers;
  00539CE0 returns junk in EAX.
- Client effects and figures: the blink's rectangles mix units (closed: top = the face offset itself, unscaled; open: top =
  (eye height + offset) x scale); `BlitMipChain` never releases the attached mip surfaces it gets and copies nothing when a
  texture has no DirectDraw surface (the original would crash); integer edges follow `_ftol` of double maths (the
  original's x87 at 24 bits could differ by one). `HitEffect`'s third argument is a flags word whose stack slot the original
  reuses for the material CString; heroes index the armour table 0073EC78 by the hit location without a range check.
  `Say`'s new queue entry has three padding bytes of stack garbage (ours zero); `StopSpeech` frees nodes through
  `stlp::deallocate` where the original pushes them on the 32-byte free list (0079B230). `CObject3D::LoadPart` returns null
  for a part the figure has no piece of without releasing the new part object (leak).
- CTrapObject's +0x238 is saved and loaded but never initialised; a trap's 0x100000 flag changes nothing but is cleared when
  nobody is near; the torch's Load, Save, AttachTorchObject (type_ read first) and network writer use the spec without a
  null test; the lever's ApplyKey disables the lever it has just switched.

## Skipped

- 0052EA40 (SetMotionSpeeds with given speeds): only the multiplayer client calls it (00669FA0). 0050E9B0 (CMapObject slot
  34) packs vitals the builder never writes when the prototype exists: uninitialised stack, cannot be compared.
- Candidates: 00522AE0 / 00522B20 (results depend on leftover register halves),
  0052C390 (torch spec net read: calls the spec's slots 13-18 through the vtable, which a client spec may override), 0052CA40
  / 0052CB40 / 0052CC50 (STLport / CArray instantiations).
- Not unit logic: 00500000-0050E000 (`map_objects.md`), map object effect visuals (00510B80, 005112A0, 005114D0, 00511A00),
  the client half's material sets and colour stack (0051C3D0, 0051C250, 0051B050, 0051B160, 0051C9F0: renderer data),
  0051FF40 (writes "perks.pdb", no references: dead), unit database STLport / MFC code (library: 0053F530, 0053FCC0,
  0053F950, 00520FA0, 00521220, 0053E2A0, 00521C70, 0051F790, 005225E0, 00522480, 00522010, 00522160, 005222F0, 0052C740;
  005335E0, 00539440, 00512060, 0051E5D0), 0053A200 and 00520220 (Rebirth-patched).
- Inlined, no own function: 00519FC0 (zero part boxes), 0069FB60 (list AddTail pair), STLport `vector<int>::resize`
  (004FE0C0). Ghidra calls 00700EFE MakeUpper; it is MakeLower (_mbslwr at 006EC8A8).

## Risks and lessons

- Destructors, the factory, the slot-style functions and the order / hit paths are consistency checks only. Under 64-bit FPU
  precision `TotalItemWeight`'s double accumulator rounds differently; SkillPrice may show a rare off-by-one at a truncation
  edge (rule 7). 005179E0: the animation state's parts appear during the call (0050D320 gives it the model).
- Not reached by every scenario: TakeRandomItem (stealing), TorchCreate, UseSpots (no lever on zone1), AttributeWithItems,
  ClientGetAttackParams, SkillPrice, packed WeaponSkillBonus (party unit holding a weapon), RemoveArmourModel (Rebirth's
  GiveUnitArmor refuses text-key units), FireAtOccupants (only traps with a period above 0), DestroyNpcTable /
  DestroyModelRecord, the lever's key slots 38-42 (science on a lever), the torch (none on zone1 known), the placed trap (use
  a trap item), the player mode's tamed slots (taming). `levers.txt` switches zone1's gate "VGate" (id 1889, the only lever
  on zone1; found by parsing the .mob records 0xBBAC0000) and saves / loads it. `magic_traps.txt` loads `traps.mob`, written
  by `tools/make_trap_mob.py` into the run's harness_saves (a `# prepare:` line, `tools/prepare_scenario.py`), with
  `AddMob("..\saves\traps.mob")`: one 0xA000 root with 0xD000 (leaves zone1's diplomacy alone), zone2's 0xDDDDDDD1 diplomacy
  table and a 0xB000 with only the seven trap records (0xBBAB0000) renumbered 30001..30007 (zone2's own ids collided with
  zone1's and crashed RemoveObject at 0059362A). AddMob's name is relative to the maps folder; a missing file hangs vanilla
  (message box).
- Shared-header hazards: CUnit's old `speed*` fields are `boundsFlags` / `center[2]` / `radius` / `above[2]`; CWorld
  declares virtual slots 13-41 (named, world.h), so a later non-virtual CWorldServer / CWorldClient member with one of these
  names silently overrides (check_thunks catches it when hooked). The serialisers' verify relies on loading being
  repeatable; `ComposeSkin` and `WearArmourModel` build CString / STLport temporaries in the original's order.
