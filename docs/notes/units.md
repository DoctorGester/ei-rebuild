# Units, creature statistics, levers and magic traps (00500000-00540000)

Code (layouts, vtable slots and per-function comments are in `unit.h`, `unit_db.h`, `magic_trap.h`, `lever.h`,
`unit_client_models.h`; each file's header says what it does): `unit.cpp`, `unit_stats.cpp`, `unit_vitals.cpp`
(recomputation, regeneration, experience, stat raises), `unit_stats_pack.cpp`, `unit_hits.cpp`, `unit_update.cpp`
(CUnit virtual slots), `unit_items.cpp`, `unit_save.cpp`, `unit_create*.cpp`, `unit_db*.cpp`, `unit_equipment.cpp`,
`unit_params_skill.cpp`, `unit_skill_price.cpp`, `unit_armour_layers.cpp`, `unit_hit_location.cpp`, `unit_steal.cpp`,
`unit_size_class.cpp`, `unit_figure_queries.cpp`, `unit_points.cpp`, `unit_client_*.cpp`, `unit_lever.cpp`,
`magic_trap.cpp`, `net_state.cpp`, `map_object_create.cpp`. Verify: `tests/verify/verify_<file>.cpp`. Scenarios:
`units.txt`, `unit_create.txt` (eight prototypes, replace, kill, remove), `magic_traps.txt`, `world_load`
(save / load), and every scenario that starts the game (serialisers, factory, constructors). Names are provisional.

## Address map

| Range | Contents |
|---|---|
| 00500000-00522000 | rendering / animation, map object base, CObject3D (`map_objects.md`) |
| 00522000-00529000 | creature statistics (UnitStats, 0x704 bytes, often a stack copy), DamageInfo helpers |
| 00529700-0052E000 | CTree, CBuilding, CStone, CStuffObject, CTrapObject, CMagicTrapObject (id 0x3B), CLeverObject, CTorchObject (0x3A) |
| 0052E000-00533E00 | CUnit (id 0x50, vtable 0073EA8C; CFakeUnit 0073BFE8 shares every rewritten slot) |
| 00533E00-0053A000 | CUnitClientSpecific (id 0x3D, vtable 0073ECD8): the controller of party units |
| 0053A000-00540000 | STLport instantiations of the unit database (library, `config/library.csv`) |

## Facts

### Layouts and conventions

- Party units keep their items in the owning player's records (player + 0x70, 0x784-byte records found by unit id,
  006621B0; weapons +0x730, quick items +0x740, spells +0x760) instead of CUnit +0x440 / +0x450 / +0x460.
- Prototype tables (STLport vectors with a name -> record hash_map at +0x0C): 007AF358 prototypes (0x120; +0x08 indexes
  models), 007AF378 NPCs (0x60), 007AF398 models (0x168), 007AF268 skills, 007AF274 skill levels (0x44), 007AF3B8. The
  database object at 007AF358 is four 0x20-byte tables. 00514930 / 005144D0 / 005145D0 are `operator[]` on the name maps
  (an unknown name is inserted with a null record).
- CUnit vtable 0073EA8C slots rewritten: 6 Tick 0052F7D0, 19 AttachCompanion, 26 LoadUnitFigures, 27 RecomputeUnitScale, 28
  EffectsChanged, 30 ResetUnitScale, 32 StatsChanged, 33 ScaleEffectChanged, 35 SetServerStats, 37 UpdateSizeClasses, 48
  StandPoint, 49 / 50 network state, 51 PlaceUnitOnGround, 52 / 53 unit points, 5 ReleaseUnit 0052E740, 3 DestroyUnit 0052E590.
  Not rewritten: 25 0052FC40, 34 0052EB70 (Create 0052E400 is `CreateUnit`: a member named Create would override CEngineObject's
  virtual).
- Controller slots are called through `CUnitController` (unit.h; `CUnit::Controller()`): an interface declaration the controller
  classes do not derive from, so their registered slot members stay non-virtual. Named: 16 Path, 18 PlayerChanged, 38
  HostileMaskChanged.
- ST0 results are `double` (the original returns floats unrounded and 00532A20 truncates `Reduce` with `_ftol`): UnitStats
  Period20, AttributeWithBonus, SkillWithBonus, Value6E8, Value6E9, Value6E9Arg, Reduce, ReduceKind1, CUnit MoveSpeed2,
  MoveSpeedFor, ScaledValue, BaseSpeed, ModeSpeed, TotalItemWeight, AnimationRatio, UnitParams::Period. UnitStats level
  values are unsigned dwords the original loads with FILD of a zero-extended qword (volatile int64 here). `unit*.cpp` use
  `float_control(precise, on)`. Rewrites call each other through game addresses (`Call*` forwarders) so nested wrappers see
  the calls.

### Statistics in play

- `UnitVitals::Regenerate` (00523740, every world update, from the controller 00552300): every 15th call mana (unless the unit
  has an order) gains `(1 + 0.01 * skill 19) * regen * maximum`, hit points heal the slots by `maximum * (1 + 0.01 * skill 18) *
  regen` (times effect 0x1F when positive) through `HealSlots`, then hit points = Reduce(maximum); returning 1 triggers
  `Recompute` (00524560): stats from bases plus skills 23-25, protection and value pairs from the strongest effect of each type,
  maxima from the experience curves `base ^ (ln(experience / divisor) / ln(logBase)) * stat / divisor * scale * (1 + 0.01 *
  skill)` (FYL2X and `_CIpow`, inline assembly in the original's order), strengthen / weaken scale (at least 1), slot maxima,
  capacity, load, legs / arms factors, rate. Constants: curves 007C5454..007C5480, slot ratio thresholds 007C543C..007C544C, wear
  threshold 007C549C, difficulty table 007C54A0 indexed by 0079BEC0, hit log stream 007AF284.
- Hits: `ApplyHit` (005254B0, from CUnitServer::TakeHit): damage per type beyond the vitals' protection (times the difficulty
  without a player), `WearItem` (005253E0: shared by +0x48 pieces; remarks 0x24 / 0x26), `DamageSlot` (00525070) on the hit location
  or all six slots by their factors for 6: layers (text-key units; `WearArmour` 00525720, tables 0073E09C / A0 / A4, remarks 0x23 /
  0x25), backstab multiplier, sever (-5 x maximum; remark 0x1E arm / 0x1F leg) or disable, log "<type> passed damage = <n>[ HL was
  crippled| HL is dead]"; then 00532AC0 drops broken equipment. `IsBehind` (00530A60): units with a player and no bow / crossbow;
  behind when the dot with the target's facing is below -0.5 (or NaN). `PickLocation` (00525B20): the aimed slot is kept while its
  kind is 3; else a neighbour table (head 1, 2, 3; body 0, 2, 3, 4, 5; hand 2: 3, 1; hand 3: 2, 1; leg 4: 5, 1; leg 5: 4, 1), then a
  random working slot (inlined `IsaacRandom::Next`), none gives 1.
- Raises: GiveStrength / Dexterity / Intelligence (script 0xCE-0xD0) -> RaiseStrength / Dexterity / Intelligence (00529090 / 00529220 /
  00529400): base + amount, maxima recomputed (strength and dexterity round the curve product to float before the skill percentage),
  strength widens the unit by 0.2, dexterity lowers y by 0.2 (at least 0), then Recompute. GiveSkill -> RaiseSkill (00528AB0) /
  RaiseAttribute (00528DA0): the level byte grows and `truncate(1.61 ^ (new / 007C5480) * 007C547C - 1.61 ^ (old / 007C5480) * 007C547C)` is
  booked as experience and spent experience. AddScaledExperience (00523960) is times 1.04 ^ (intelligence - 25) (kills, console).
- Skills: `SkillPrice` (00528050, skill pages): worth = 1.61 ^ (record +0x30 / 007C5480) * 007C547C; cost = _ftol(worth - worth of the record
  before it (read unchecked)) when index % 3 > 0, else worth - worth of 0; price = 007C5484 ^ (byte sum of the 0x24 skill level bytes) * cost,
  kept to one significant digit (two when the first is 1-3), small prices rounded up. `LearnSkill` (00526450) heals the slots to keep the hit
  point proportion; `CanLearn` (005260A0) checks the learnable byte (+0x40), stat minima, required attribute and skill level, that no level of the
  same skill id (+0x10) is known in another skill, and that the level is new. `WeaponSkillBonus` (00526C70; 00527850 on the packed `UnitParams` for
  the client's figures, "Weapon skill on the packed parameters"): types 0-4 skill 0 and the weapon prototype's +0x7B, 5-6 skill 1 and +0x7C, each plus
  (stat named by the skill's first letter - 25) plus the level value of skill `type`; type 8 skill 0 and +0x7B; others 0. `AttributeWithItems` (00525E90)
  and `SpellSchoolSkill` (00527EB0: school 4 -> 3, 6 / 7 -> 5; skill byte +0x6CE / +0x6CF / +0x6D0 plus level value of skill school + 7 plus
  intelligence - 25) feed orders and the interface.
- Armour layers: table 0073E048 gives three body slots per armour type (7: none); `AddArmourLayer` (005230C0) adds the piece's seven absorb floats (+0x4C
  first slot, +0x68 others) to layer `GetTypeGroup()` (2 for types 0-2 else 1), ORs the mask (+0x84 / +0x88), state 3; `RebuildArmourLayers` zeroes
  layers 1 and 2 (0x90 bytes) and adds each worn piece whose +0x20 rounds above 0; `PackBodyProtection` (00523020): out[1 + k] = round(current protection of
  type k + absorb values of the body slot's layers with state >= 2, layer 2 down to 0 for text-key units else layer 0); out[0] is the caller's attack value.

### Creation, databases, save, network

- Factory (CMapObjectManager slot 14, 00512730) by class id: 0x32-0x34 units (0x4DC, 0052E110), 0x35-0x38 (0x238), 0x39 (0x580, 004466D0), 0x3A (0x23C,
  0052BEE0), 0x3B (0x290, 00529DD0), 0x3C (0x264), 0x300D (0x78), 0x3010 (0x23C). The zone loader then calls `ApplyPrototype` (00513B60): without a prototype
  defaults (005134F0) and "Can't find prototype %s for object %d"; units get vitals / statistics from the records (005136D0: hit points / mana from prototype
  +0x48 / +0x4C, regeneration from the model +0x08 / +0x0C, protection = model's seven values x prototype +0x58; stats from the NPC record or a third of the
  hit points / mana; NPC experience and skills) via CMapObject::SetServerData and slot 35, stand lower by the model's +0x104, take the prototype's scale
  (00517700) and second prototype (005308A0). 00513DA0 builds an NPC's statistics alone; `ResetToBase` (00524CD0) refreshes current values from bases.
- `GiveStartingEquipment` (005141F0, zone loader record kind 5): a type 0x32 unit gets each armour of prototype +0x10C (006AC970; Rebirth's FixCreateItem runs;
  equipped), weapon +0x118 unless "none" (slot to +0x270 unless -1) and +0x11C; every unit each spell of +0x100 (006AD1E0, AddSpell); an unmakeable piece is
  FatalError "Can't create armor / weapon / spell".
- **Database records**: `units.udb` is loaded once at startup by 0053A200 (Rebirth-patched) through the tagged-chunk archive: chunk 1 skill levels (0x44), 2 models
  (0x168, -> 0052DC70), 3 prototypes (0x120, -> 0052D740), 4 NPCs (0x60, -> 0052DA60); then name indexes are filled, a prototype's +0x08 becomes the model's index
  and each model slot's +0x04 the skill level record's. Serialisers call the archive's field / CString field / enter / leave (004092B0 / 004093A0 / 004098D0 / 00409980)
  in the original's order (matters when writing; unused by the game); bytes 0x38..0x47 of a prototype and 0x128..0x137 / 0x144..0x14F of a model are not
  serialised. 0057DEE0 re-creates the database when the file changes (database.res <-> databaseLMP.res, multiplayer hero screens).
- **Save / Load** (`SaveUnit` 005321C0 slot 10, `LoadUnit` 00531A80 slot 9) order: map object part (0050F7A0 / 0050F900), controller, name (+0x238), relations, player
  id (-1), side, +0x254, +0x255, +0x258..+0x278 (active weapon among them), +0x27C sub-object (005C7860 / 005C79B0), +0x320, +0x324, +0x328, active weapon again,
  weapons / quick items / spells / armour as item ids, +0x4C8 (0x14 bytes), +0x248, carried items, +0x32C, +0x438 (version 0x49), prototype names (version 0x4D; the
  second from 0x6A, before that looked up by the first name with 00514930). Load resolves items in the world's item manager (ManagerContext slot 18), moves the unit
  between players (00665B50 / 00665A70, MarkChanged(0xB), controller slot 18) and calls slot 32.
- **Network state** (DeltaState at +0x1C4; slots 49 / 50 and map objects 0050EC30 / 0050EAF0, free functions): map object fields 0 the 0x18 bytes at +0x17C, 1 scale
  +0xC4 (read: SetPosition 00517700), 2 hidden bit (flag 8), position +0x1C, orientation +0x34 (read: world slots 41 / 51), 3 effects (count byte, then per 0x14-byte effect
  bytes +0, +1, word +4, byte +0x10; read: CArray::SetSize, then the companion +0x170's slot 13 with `first == 0` unless a unit). Units add 4 stats block +0x330 (read:
  stat fractions before / after give controller slot 46 per changed stat, slot 31 when any changed, a hit point drop or flag 0x10 in +0x437 gives the companion's slot 16
  with `_ftol(lost * max)`), 5 the 11 equipment ids (weapon -1 with flag 0x40000000; writing refreshes a class 0x3E controller, slot 29), 6 +0x264 / +0x268 / +0x26C (+0x270
  = +0x26C, slot 25 on a change), 7 packed bits of +0x25C / +0x260 / +0x274 / +0x278, 8 +0x254, +0x255, +0x258, +0x256, 9 the CString +0x238, 10 path (005C7DB0 / 005C7FC0)
  and motion speeds, 11 masks, +0x250, player id, 12 +0x4C8, 13 +0x438, then re-equip (slots 32 / 33) and refresh (25, 29). Verify lessons: compare only the bytes the reader writes of each effect (+0, +1, dword +4, +0x10) because the companion's slot 13
  changes the others and the original's second read runs it again; with `first == 0` the controller's slot 41 (00535850) can snap the orientation to the path by
  clearing byte +0x76, so the wrapper saves that byte before ours and puts it back before the original's read. The field 3 count is one byte read into a word.
- **Unit points** (slots 52 / 53): `UnitActionPoint` (0052FDF0, RET 4) always returns `out` in EAX although Ghidra shows void (CUnitServer::ApproachTarget reads it;
  returning nothing crashed combat, sound and units scenarios); a missing animation entry logs "Can't find animation entry index"; it falls back to the position where
  size class 3 cannot stand (005BA790). `UnitPredictPosition` (0052FCC0, RET 8) samples the motion for client units following a path order (kind 2).
- **Client path placement** (`ClientPlaceOnPath` 00535850, RET 8; from slot 41 and from 00535B30): details in `unit_client_path.cpp`. Lesson: the two heading angles
  must go through a volatile float store (`Store`) as the original's FSTP: unrounded angles moved the heading by an ULP in ~10% of calls and flipped the orientation
  quaternion's sign near +-pi.
- Size class and figure queries: `UpdateSizeClass` (00530570) relies on controller slot 45 (server's UpdateMoveFactors; empty on client / base controllers) so verify
  runs it twice; `FindAnimation` (005301B0) works only for type_ 0x32..0x34 and draws with `rand()` through 005190C0 / 00519380; `GroundHeight` (00530280) has a
  body too short for a trampoline (verify uses a copy of its 98 bytes made at DLL load); `BodyExtent` (005304A0) feeds CUnitServer::UpdateSizeClasses.
- Client models (CUnitClientSpecific id 0x3D; +0xA0 packed ids of the seven armour slots, +0xBC four weapons): armour part rows at 0073EC24 (body "bd", "rh1".."lh3";
  trousers "hp", "rl1".."ll3"; helmet "hr" ("hr.<nn>" with the prototype's +0x10) and "hd"; armour slots 0 / 1 / 2 use rows 2 / 0 / 1; slots 3-6 have no parts);
  skin composition through the texture manager (007AF744, 00693180): layers 0x32 skin, 0x3005 armour, 0x3004 weapon, names "mo<id>p" / "prot<prototype>p" (...s for
  two layers). `ClientGetAttackParams` (00538960, slot 50; HUD info and hero panels) reuses 005285D0, a byte-for-byte copy of 00523D30 registered with the same function.
- Client controller fields (unit_client_models.h): +0x2C last animation time, +0x38 motion order (slot 16), +0x74 / +0x75 weapon /
  arrow hidden, +0x78 fade-in (new units fade in over 15 ticks), +0x7C colour flash countdown, +0x90 waiting speech sound, +0x9C speech
  queue (STLport list of ClientSpeech; a speech's delay counts down, plays at 0, is erased once its sound ended; kinds 2 / 0x1C skip
  while the previous one plays; `talking` sets the server unit's flag 0x800000), +0xD0..+0xD8 blink (every 2..9 ticks, rand()).
  A unit dead for more than 50 ticks past its +0x254 byte is not drawn (flags_ 8).
- **Levers**: lever table 0079C1A0 (`lever_table.cpp`), records from `Levers.ldb` (00463030), found by model name through 0052CB40 (unknown names add a null record).
  `SetState` (0052B7A0): a negative (or NaN) duration takes the model's default (map 0079C1AC) or 10; a negative state is the next (wraps when cycling); with a live
  world units within radius + 0.5 leave the sector map around the move, the end tick is (tick + duration) + 1, MarkChanged(5), lifted units that cannot stay go to
  a free cell and are resynced (00554450). `UseSpots` (0052BBE0, slot 44, from FindUseSpot 0054DF50) trusts the record's spot count.
- **Magic traps** (class 0x3B, 0x290 bytes, vtable 0073E63C, derived from CMapObject with members +0x238..+0x28C; flags_ 0x80000 armed, a new trap is armed until a
  script disarms it; 0x40 kind): `UpdateTrap` (slot 6): server only (world slot 26: 1 for CWorldServer 0058A380, 0 for CWorldClient 0055EB30; `world_effect_internal.h`'s
  IsClientSide is this slot, misnamed); period <= 0: `UpdateVictims` (0052A680, armed traps ask `FindTrapVictims` 0052A920 for newcomers and cast), else the countdown
  runs and `FireAtOccupants` (0052AB30) fires every tick until a cast resets it. `CastAtUnits` (0052A4B0) casts at each point {x, y, 0} (spells without flag 0x10000000
  and a trap with points) or at each unit; with +0x28C narrowed to the last unit; casts through 0058E850 (007AFF60) with caster 0. `SetUp` (0052A2C0, per MagicTrap
  record at +0x8C from the zone loader). `ForgetObject` (slot 56, from the world's RemoveObject 005935C0) removes every node equal to the object (null included). Zone
  loader registers traps in no name map (005927C0, record type 7): scripts use `GetObject(id)` (00593AA0).
- Torch (class 0x3A, vtable 0073E834) `TorchCreate` (0052BFC0); `CreateUnit` (0052E400) calls CUnit::Reset and sets 14 network fields (SetSize inlined = 0050FA30).
  `TakeRandomItem` (00531400, stealing): carried items first, else a random filled quick item slot emptied without release.

## Rewritten

"Pure" = original and ours on the same input (float bits); "output" = original into a scratch copy; "stateful" = ours, object snapshotted and restored, original, compared
(unit 0x4DC, stats 0x704, vitals 0x58); "ours only" = consistency check on the resulting state.

| Address | Name | File | Verify |
|---|---|---|---|
| 0050E9F0, 0050F630 | CUnit::MarkChanged, HasEffect | unit.cpp | stateful (first 0x1E0 bytes, EAX) / pure |
| 0052E940, 00530180, 00530120, 0052E9B0, 0052EA90, 0052EAD0, 0052EAF0, 0052EB10, 0052EBE0, 0052F7F0 (slot 46) | AddHostileMask, SetRelationsB, SetSide, SetUnknown438 / 254 / 25C / 260, SetMoveModes, SetFlag40000000Off, SetFlag1000000 | unit.cpp | stateful, whole unit |
| 00531070, 00531180, 00531820 / 005310D0, 005311E0 | AddWeapon, AddQuickItem, AddSpell / RemoveWeapon, RemoveQuickItem | unit.cpp | stateful + item refcount / ours only (slot empty, dirty flag) |
| 00530150, 005318B0, 00531920, 005319A0, 00531A10, 00530FA0, 005307B0, 0052F820, 0052F8B0, 0052FA20, 0052FAF0, 0052FBC0, 00530370, 005302F0, 00530930, 00530970, 005309E0, 005327D0, 00532DA0, 00532E00, 005271C0 | IsHostileTo, GetWeapon, GetQuickItem, GetSpell, GetSpellObject, HasRangedWeapon, TotalItemWeight, CanCastSpell, CanUseQuickItem, MoveSpeed2, MoveSpeedFor, ScaledValue, BaseSpeed, ModeSpeed, GetPrototype, GetPrototypeModel, GetPrototype23C, ResistanceTo, AcceptsTarget (slot 18), AnimationRatio, UnitParams::Period | unit.cpp | pure |
| 0052EC10, 0052F920, 005308A0 | GetEquipmentIds, GetMoveSpeeds, SetPrototype23C | unit.cpp | output / output / stateful |
| 005156A0, 005156C0, 005104D0, 005104F0 | CMapObjectServer / ClientSpecific IsDead, IsOverkilled (slots 17 / 18) | unit.cpp | pure |
| 00523CD0, 00524020, 00524280, 005259B0, 00525CD0, 00525D60, 00525DF0, 00525E10, 00526780, 005267E0, 00527030, 00527050, 00528270, 00522A80, 00522B80 | UnitStats Period20, ReduceKind1, Value6E8, Reduce, Value6E9, Value6E9Arg, AttributeBase, AttributeWithBonus, WeaponSkill, SkillLevelValue, SkillBase, SkillWithBonus, SkillLevelRecord; LockKey::Opens, HasElementalDamage | unit_stats.cpp | pure |
| 00528840, 00522B40 | GetFactors, ResetDamageInfo | unit_stats.cpp | output |
| 00523740 | UnitVitals::Regenerate | unit_vitals.cpp | stateful (vitals + stats; AL) |
| 00524560, 005243D0, 00525800, 00525900, 00525930, 00525A00, 00525A70, 00525AF0, 00523C80, 00523920, 00523960, 00523C20, 005239B0 | Recompute, RecomputeHitPoints, HealSlots, Heal, RegenerateSlots, SetSlotMaxima, ScaleSlotMaxima, RefillSlots, RestoreSlots, AddExperience, AddScaledExperience, TakePendingExperience, GainExperience | unit_vitals.cpp | stateful (stats, vitals) |
| 00524060, 00524110, 005241C0, 00523E80, 00523F50, 00524320, 005282D0, 00523D30, 00523E00, 00523DB0 | LegsFactor, ArmsFactor, MovementState, HitPointCurve, ManaCurve, RateValue, SkillLevelValueCopy, ScaleDamageByEffects, ScaleHitPointsByEffects, HitPointEffectScale | unit_vitals.cpp | pure |
| 00528AB0, 00528DA0, 00529090, 00529220, 00529400 | RaiseSkill, RaiseAttribute, RaiseStrength / Dexterity / Intelligence | unit_vitals.cpp | stateful (stats, vitals, unit) |
| 00522C50, 00522CE0, 00523370, 005233B0, 00523550, 005233E0, 005232B0, 00523600, 005236A0 | UnitVitals Pack / Reset, UnitStats Pack / Reset / ClearOuterLayers, UnitVitalsPacked / UnitParams / UnitStatSlot / UnitStatLayer Reset | unit_stats_pack.cpp | output (end offsets compared) |
| 005254B0, 00525070, 005253E0, 00525720 | ApplyHit, DamageSlot, WearItem, WearArmour | unit_hits.cpp | ours only (hit points = Reduce(max); wear drops condition by the shared amount) |
| 00524FE0, 00530A60 | UnitVitals::TakeHit, CUnit::IsBehind | unit_hits.cpp | stateful / pure |
| 0052F7D0 (6), 00532A80 (27), 00532AB0 (28), 00532AA0 (30), 00530A50 (37), 0052EB30 (33), 00530A20 (35), 0052E910 (26), 0052E870 (19), 0052EB50 (32) | CUnit virtual slots | unit_update.cpp | ours only (Tick returns 1; LoadUnitFigures: flags_ 0x80; AttachCompanion: attached both ways, result = reference count) |
| 005303A0, 0052F740, 00532A20, 00532D40 | StandPoint (48), PlaceUnitOnGround (51), SetFromStats, Level | unit_update.cpp | output / stateful / stateful / pure |
| 00530730, 00530840, 005315B0, 00531220, 005313B0, 00531110, 005308D0, 00530920 | EquipArmour, UnequipArmour, ReleaseAllItems, AddCarried, RemoveCarried, SendEquipment, SetPrototype23CByName / Index | unit_items.cpp | ours only (slots, carried array; prototype setters compared with the original after ours) |
| 005321C0, 00531A80 | SaveUnit, LoadUnit | unit_save.cpp | save: both into capture streams / load: ours only |
| 0050EAF0, 0052EC70, 0050EC30, 0052EF90 | map object / unit net state write / read | net_state.cpp | differential, sampled (state copied, original, state put back, ours; reads: everything put back and the original reads again) |
| 00514930, 005144D0, 005145D0, 00513240 | prototype / NPC FindByName, PrototypeObjectClass | unit_create*.cpp | both when the name is indexed, else ours only |
| 005134F0, 005136D0, 00513DA0 | SetDefaults, BuildFromPrototype, BuildNpcStats | unit_create_stats.cpp | output (vitals, stats) |
| 00524CD0, 00526450, 005260A0, 00526C70 | ResetToBase, LearnSkill, CanLearn, WeaponSkillBonus | unit_create_stats.cpp | stateful / stateful / pure / pure |
| 00513B60, 00512730 | ApplyPrototype, CMapObjectManager::CreateObject | unit_create_stats.cpp | ours only |
| 00552F40, 00552780, 00532AC0 | CUnitServer::SetStats, Shout, CUnit::DropBrokenEquipment | unit_create.cpp | ours only |
| 0052D740, 0052DC70, 0052DA60 | Prototype / Model / Npc record Serialize | unit_db.cpp | loading: ours into the record, then the original reads the same record again, bytes (CString / vector slots left out) and string digest compared; writing: ours only |
| 0053A050, 00539CE0, 0051FB50, 0053EE70 | DestroyUnitDatabase, NpcTable, SkillDatabase, ModelRecord | unit_db_destroy.cpp | ours only (name map counts 0) |
| 005141F0 | GiveStartingEquipment | unit_equipment.cpp | ours only (result 1 with a prototype; slot counts) |
| 00527850, 00528050, 005285D0, 00538960 | WeaponSkillBonus (packed), SkillPrice, ScaleDamageByEffects copy, ClientGetAttackParams | unit_params_skill / _skill_price / _client_attack.cpp | pure (CheckNear) / pure (ints exact) / pure / output (floats within 4 ULPs) |
| 005230C0, 00523300, 00523020, 00525E90, 00527EB0 | AddArmourLayer, RebuildArmourLayers, PackBodyProtection, AttributeWithItems, SpellSchoolSkill | unit_armour_layers.cpp | differential on a scratch 0x704 copy / output / pure (CheckNear) |
| 00530570, 00525B20, 00531400 | UpdateSizeClass, PickLocation, TakeRandomItem | unit_size_class / _hit_location / _steal.cpp | differential (class exact, floats within 4 ULPs; the generator 007A9D40 saved and put back) |
| 005301B0, 00530240, 005304A0, 00530280, 00532860 | FindAnimation, AnimationEntry, BodyExtent, GroundHeight, EffectPoint | unit_figure_queries.cpp | differential (rand() / ISAAC saved; GroundHeight through a copy of the 98-byte body; EffectPoint output, skipped where the original rotates uninitialised stack) |
| 0052FCC0, 0052FDF0, 00535850 | UnitPredictPosition, UnitActionPoint, ClientPlaceOnPath | unit_points.cpp, unit_client_path.cpp | differential within 8 ULPs (quaternion strict) |
| 0052E400, 0052BFC0 | CUnit::CreateUnit, TorchCreate | map_object_create.cpp | ours only (result 1, 14 / 5 network fields, mode idle) |
| 0052E970, 0052E9D0, 005300D0 | SetHostileMask, RefreshMotion (path forgets the unit, motion +0x27C takes GetMoveSpeeds and is rebuilt; part 10), SetPlayer | unit_controller.cpp | ours only (mask / speeds / player owns the unit; parts marked) |
| 005341A0 | CUnitClientSpecific::UpdateClient (slot 6: motion order shift, weapon hidden by key, flags_ 8, fade-in, colour flash, blink, arrow, step sounds, speech queue, special idle) | unit_client_update.cpp | ours only (result 1, flags_ 8 by the rule, fade <= 1, blink timer 0..9) |
| 00537CB0, 00538C20 | ShowWeaponParts, UpdateArrow (bow arrow "rh3_arrow00" between the animation's frames +0x28 / +0x2C) | unit_client_models.cpp | ours only (+0x74 / +0x75; part flag agrees) |
| 005344E0, 005344A0, 00539160, 00537360 | FiguresLoaded (slot 13: parts take the fade-in), RestartFadeIn (43: from 0; EAX 0), Level (24: ln(packed experience / 007C5488) / ln(007C548C), float, rounded), RefreshWounds (31: "mo<id>w" through 005370F0 / 0051C3D0, else 0051C920) | unit_client_update.cpp | ours only / Level compared (std::log: a ratio within an ULP of .5 may round differently; experience 0 gives ln 0 = -inf, and FISTP's 0x80000000 is reproduced, X11) |
| 00536100, 00536220 | RefreshSkins (slot 29), RefreshSimpleSkin (slot 30): heroes' composed skins into the client half's +0x20 / +0x24 textures, reloaded when new or renamed | unit_client_equipment.cpp | ours only (result 1) |
| 00538140, 00538860, 005388C0, 00538920 | SetArmourModels (slot 32), SetWeaponModels (33), AddWeaponModel (34), RemoveWeaponModel (35) | unit_client_equipment.cpp | ours only (ids among the given) / weapon ids compared with the original |
| 00534590, 00534760 | FootstepSound (model sound names +0x138 / counts +0x144 per ground kind; platform -> kind 2; crawling), ActionSound (sound frame +0x44 in [from, to), sounds +0x48; folder: second prototype +0x34 or model +0x124; idle / attack chances +0x150 / +0x154) | unit_client_sounds.cpp | rand() saved, original into a copy of `out`; result, string, rand state compared |
| 00534B60 | PlayStepSounds (attack / death / skill sounds at priority 1000, idle / hit at 0; camera shake by prototype +0x30 at the attack moment and steps; footprints through 0056D150 with model +0x10C / +0x110 / +0x11C and prototype +0xB0 as scale) | unit_client_sounds.cpp | ours only (animation and frame remembered; footprint / countdown change by one of the original's steps, countdown -1 until the first footprint, X11) |
| 0051B6D0, 0051B6F0 | CObject3DClientSpecific::FirstDrawnPart / NextDrawnPart (+0x38 / +0x3C, cursor +0x48) | unit_client_update.cpp | result and cursor compared |
| 0052E740, 0052E590 | ReleaseUnit (slot 5: SetPlayer(null), controller slot 5, item slots, carried items), DestroyUnit (slot 3) | unit_controller.cpp | ours only (no player, empty slots; no controller) |
| 00535590, 00535CB0, 00536300, 00536670, 005381B0, 005385A0 | ChangeWeaponModel, CollectArmourPartNames, BuildSkinLayers, ComposeSkin, WearArmourModel, RemoveArmourModel | unit_client_models.cpp | ours only / output |
| 0052B7A0, 0052BBE0 | Lever::SetState, UseSpots | unit_lever.cpp | ours only / output (coordinates within 8 ULPs) |
| 00529DD0, 00529F90, 0052A100, 0052A200, 0052AC20, 0052AB30, 0052AC90, 0052A2C0, 0052A4B0, 0052A680, 0052ADC0 | MagicTrap Construct, DestructBody, CreateTrap, DestroyTrap, UpdateTrap, FireAtOccupants, ForgetObject, SetUp, CastAtUnits, UpdateVictims, LoadTrap | magic_trap.cpp | ours only (LoadTrap through a recording stream with a round trip through the original Save) |
| 0052B070, 0052A920 | MagicTrap::SaveTrap, FindTrapVictims | magic_trap.cpp | both into capture streams / output (empty scratch CPtrList) |
| 00535B30, 005352B0, 00535480 | CUnitClientSpecific AdvanceTime (slot 40), RestartAnimation (14), StartIdleAnimation | unit_client_animation.cpp (A-U2) | ours only (last time, queue length) |
| 00537500, 00537C00, 005373D0, 00534140 | HitEffect (46: hit sound by weapon class and material, blood particles, decal), SparkEffect (47), DeathEffect (48), OwnerPlayerChanged (18: party light) | unit_client_effects.cpp (A-U2) | ours only (effect source count; controller unchanged) |
| 00537D80, 00538040, 005380D0, 00537F60, 00537FD0 | Say (51: queue filters, kinds 0x1D-0x2A queue behind), StopSpeech (52), Speak, SpeechReplaced / SpeechFinished (filters) | unit_client_speech.cpp (A-U2) | filters: original vs ours; the rest ours only |

Other callees stay forwarders (`originals_units.h`); only 006AC970 `CreateItem` (called through its address by GiveStartingEquipment) and FatalError 00480080 are Rebirth-patched.

## Original bugs and oddities kept on purpose

- Client `Level` (00539160) of a unit with experience 0: ln 0 = -inf and FISTP turns it into 0x80000000 (INT_MIN), not 0;
  ours returns the same (X11: the CRT's lrint gave 0, 4 of 8 calls differed).
- PlayStepSounds: a step frame of 1 ends the step list (the frame is the entry minus 1, tested for 0), so an entry 0 never matches;
  the footprint scale reads the prototype unchecked; the "shown" flag goes to 0056D150 with stack garbage in its upper bytes (ours 0 / 1);
  FootstepSound divides by a missing ground kind's count of 0 when a platform turns the kind into 2. Our calls between the client
  controller's rewrites go through the game addresses so the verify wrappers see them.
- AddWeapon / AddQuickItem / AddSpell return 1 when the item is already in any slot (indistinguishable from "added to slot 1"); AddSpell has no null check; GiveStartingEquipment adds a
  weapon that could not be made (AddWeapon(null) returns 0, +0x270 becomes 0). GetSpellObject reads a party spell entry's +0x44 unchecked; GetQuickItem returns null for a party item whose
  +0x48 is not positive but the non-party path returns the slot as is; SkillWithBonus reads the record name without a range check; several setters return what MarkChanged leaves in EAX;
  CanCastSpell / CanUseQuickItem take an unread second argument (RET 8), GetFactors a third, 00525D60 is a copy of 00525CD0 with an unused argument.
- RaiseSkill / RaiseAttribute store the level as a byte (wraps) but book the worth of the unwrapped sum; RaiseStrength computes the maximum from the current strength; HealSlots returns AL = 1
  with FPU status bits in the rest of EAX; Regenerate's third argument is a byte whose upper bytes are the controller's +0x190 bits; UnitStats::Reset leaves +0x6F7, +0x34..+0x4C,
  +0x58..+0x6C, +0x8C and +0x6FC alone.
- DamageSlot logs "termical" (sic) and one line per damage type; ApplyHit wears the weapon by the part each damage type the protection absorbed; IsBehind is never true without a player;
  SetPrototype23CByName returns the low half of its division product in EAX.
- Save writes the active weapon twice (the second read wins) and copies the prototype name from address 0 for an out-of-range index; name lookups insert unknown names (null record);
  BuildNpcStats reads the prototype's model index before testing the prototype for null.
- **Known divergence**: ApplyPrototype hands the object vitals and statistics built in uninitialised stack copies (the first dword of each OR 1, statistics +0x58, +0x5C and the last skill
  level byte are stack garbage); ours start from zero, in fields nothing seen reads before they are recomputed. `EffectPoint` on a server world (or a hero without "rh3") turns a vector the
  original never wrote (ours zero; the wrapper does not compare those calls).
- CollectArmourPartNames pushes the five empty names of the helmet row; WearArmourModel stores the id even for armour slots 3-6; RemoveArmourModel empties the slot before looking up the figure;
  DropBrokenEquipment gives broken pieces to the player and releases the unit's reference after; CUnit::Reset clears item slots without releasing; packed WeaponSkillBonus reads the level
  byte at +0xE3 + type for any type below 5, negative ones too.
- UseSpots trusts the record's spot count (a count above 2 reads the count and factor as offsets); UpdateSizeClass reads the prototype's +0x24 unchecked (an out-of-range index reads
  address 0x24); CastAtUnits returns without restarting the countdown when +0x28C finds no unit and changes the caller's list; ChangeWeaponModel leaves +0x75 when the new slot holds no
  weapon; UpdateVictims tests only x and y against the circles while FindTrapVictims uses the sector map (a unit in two circles is found twice); a periodic trap whose countdown reached 0
  and found nobody searches again every tick; PickLocation reads a slot kind before its range check; the client attack figures keep the damage type for a weaponless unit whose model weighs
  no damage type; FindAnimation's last fallback gets the kind without the movement substitution.
- LoadTrap reads the spell id and counts into its argument's stack slot (a failed read leaves the previous value, at first the stream pointer; ours starts its local with the stream pointer)
  and appends to the list instead of replacing it; the trap constructor releases a spell and empties lists it has just set empty; VictimList::Serialize (0052C740, library) writes raw
  pointers; 00539CE0 returns junk in EAX.

## Skipped and why

- 0052EA40 (SetMotionSpeeds with given speeds): only the multiplayer client calls it (00669FA0).
- Large or stateful, next candidates: 005316F0, 0052EB70 and 0050E9B0 (slot 34:
  pack stack copies that 005136D0 fills, stack garbage), 0052FC40, 00526860 / 005271E0 (skill / attack builders), 00522AE0 / 00522B20 (results depend on leftover register halves), record
  destructors 0053EBC0 and others, 00535B30 / 00537D80 / 005380D0 (client timers, speech: audio-adjacent), 00538ED0 / 00539010 / 005352B0 / 00533980 / 00533EA0 (client arrow
  part, figure, animation code, destructor), 0053A8E0 (counts each model's .wav files), 005190C0 / 00519380 (random animations; forwarded locally).
- Not unit logic: 00500000-0050E000 (`map_objects.md`), figure loading, bounds and wound textures (005179E0, 00516DA0, 0051C3D0, 005370F0, 00536EA0), map object effect visuals (00510B80, 005112A0,
  005114D0, 00511A00), 00529B80 (class 0x3010's update: a script CreateParticleSource), 0051FF40 (writes "perks.pdb", no references: dead), unit database STLport code (library: 0053F530, 0053FCC0,
  0053F950, 00520FA0, 00521220, 0053E2A0, 00521C70, 0051F790, 005225E0, 00522480, 00522010, 00522160, 005222F0, 0052C740), 0053A200 and 00520220 (Rebirth-patched).

## Risks

- X11 (2026-10-05): PlayStepSounds' rewrite was right; its check wrongly required the footprint countdown (+0x84) in 0..7, but
  00533FE0 starts it at -1 and the original leaves it there until a footprint is made (it only decrements above 0). The check now
  follows the original's steps. It stays ours only: the original plays sounds, shakes the camera and adds world footprints, so a
  second run on a copy would double them. Level's experience-0 case is fixed (above). Both are hooked again (off disabled.txt).
- Recompute and Regenerate run for every unit (Regenerate every world update, Recompute after every 15th and after hits and heals): a mistake shows at once; both are verified side by side.
  Constructors compare every byte with the original on a copy; destructors, the factory, the slot-style functions and the order / hit paths are consistency checks only. Under 64-bit FPU
  precision (not the game's mode) `TotalItemWeight`'s double accumulator rounds differently.
- Not reached by every scenario (the verify counts show): TakeRandomItem (stealing), TorchCreate, UseSpots (no lever on zone1), AttributeWithItems, ClientGetAttackParams (interface showing a party
  unit's figures), SkillPrice, packed WeaponSkillBonus (party unit holding a weapon), RemoveArmourModel (Rebirth's GiveUnitArmor refuses text-key units), FireAtOccupants (only traps with a period
  above 0), DestroyNpcTable / DestroyModelRecord. `magic_traps.txt` loads `traps.mob`, written by `tools/make_trap_mob.py` into the run's harness_saves (a `# prepare:` line,
  `tools/prepare_scenario.py`), with `AddMob("..\saves\traps.mob")`: one 0xA000 root with 0xD000 (leaves zone1's diplomacy alone), zone2's 0xDDDDDDD1 diplomacy table and a 0xB000 with only the
  seven trap records (0xBBAB0000) renumbered 30001..30007 (zone2's own ids collided with zone1's and crashed RemoveObject at 0059362A). AddMob's name is relative to the maps folder; a missing
  file hangs vanilla (message box).
- Shared-header hazards: `CUnit` replaced the definition in ai_map.h (merge: keep the include of unit.h); CUnit's old `speed*` fields are now `boundsFlags` / `center[2]` / `radius` /
  `above[2]`; CWorld declares virtual slots 13-30 (`Slot13..Slot25`, IsServer 26, `Slot27`, `Slot28`, GroundHeightAt 29, SurfaceHeightAt 30), so a later non-virtual CWorldServer / CWorldClient
  member with one of these names would silently override (check_thunks catches it when hooked).
- SkillPrice may show a rare off-by-one at a truncation edge (rule 7); the serialisers' verify relies on loading being repeatable (fields found by id, archive left as it was); the scenario
  prints MaxHP / MaxMana around the stat raises instead of checking them; `ComposeSkin` and `WearArmourModel` build CString / STLport temporaries in the original's order.

A-U2 notes: HitEffect's third argument is a flags word whose stack slot the original reuses for the material CString;
heroes index the armour table 0073EC78 by the hit location without a range check (kept). Forwarded: 0056CE90 (decal),
00511830 / 005119B0 (the companion's light); 00498400 and 0051C9F0 moved to originals_effects.h / originals_units.h.
Say's new queue entry has three padding bytes of stack garbage in the original (ours zero); StopSpeech frees the nodes
through stlp::deallocate where the original pushes them on the 32-byte free list (0079B230) itself.
