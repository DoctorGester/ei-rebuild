# Units (00500000-00540000)

Code: `src/game/unit.h` (CUnit layout and the small classes below), `src/game/unit.cpp` (unit
methods, map object state), `src/game/unit_stats.cpp` (creature statistics, lock and hit helpers).
Verify: `tests/verify/verify_unit.cpp`, `tests/verify/verify_unit_stats.cpp`.

Shared files touched: `src/game/ai_map.h` now includes `unit.h` instead of defining `CUnit`,
`kClassUnit` and `kInSectorMap` itself (same names, same offsets; `CUnit` only gained fields).

## What lives in this range

| Range | Contents |
|---|---|
| 00500000-0050E000 | rendering: device states, textures, animation tracks/nodes (CAnimationTrack, CAnimationNode, CAnimationSystem2) |
| 0050E000-00510000 | base of the server-side map objects: MarkChanged, effects list, +0x170 server object |
| 00510000-00512700 | CMapObjectClientSpecific (vtable 0073DBEC), CMapObjectManager ("map_objects") |
| 00515600-00516000 | CMapObjectServer (class id 0x39, vtable 0073DD6C): hit points at +0x28/+0x2C |
| 00516000-0051F300 | figure/part helpers, CObject3DClientSpecific |
| 0051F300-00522000 | CObject3DServer |
| 00522000-00529000 | creature statistics (UnitStats, 0x704 bytes, often a stack copy), DamageInfo helpers |
| 00529700-0052E000 | CTree, CBuilding, CStone, CStuffObject, CTrapObject, CMagicTrapObject, CLeverObject, CTorchObject |
| 0052E000-00533E00 | CUnit (class id 0x50, vtable 0073EA8C; CFakeUnit 0073BFE8 derives from it) |
| 00533E00-0053A000 | CUnitClientSpecific (class id 0x3D, vtable 0073ECD8): the controller of party units |
| 0053A000-00540000 | STLport vector/hashtable instantiations |

## Structures recovered

All names provisional; offsets are `static_assert`ed in `unit.h`.

- **CUnit** (0x4DC bytes, `operator new(0x4DC)` in 00512730): `+0x18` prototype index,
  `+0x1C` position, `+0xA4` prototype name, `+0xCC`/`+0x1B4` animation times, `+0x124` speed flags
  and `+0x130/+0x134/+0x14C` speed parameters, `+0x17E/+0x180` hit points / maximum (int16),
  `+0x182` mana, `+0x198/+0x19C` active effects (0x14-byte records, type in the low byte),
  `+0x1C0` change stamp and `+0x1DC` changed-part bits (MarkChanged), `+0x23C` second prototype
  index, `+0x240` controller (the script notes call it the perception object; class 0x3D for party
  units), `+0x244/+0x248` relation masks (hostile mask / second mask, as in spell.h), `+0x24C` owning
  player, `+0x250` side, `+0x264/+0x268` movement modes, `+0x26C` active weapon slot,
  `+0x324/+0x32C` size class and standing height (AI map), `+0x330` parameter block (byte `+0x33E`
  attack rate, `+0x33F` movement kind, `+0x340..+0x344` speed bytes), `+0x440` weapons[4],
  `+0x450` quick items[4], `+0x460` spells[8], `+0x480` armour[7], `+0x4A0` CArray of further
  items, `+0x4B0` six resistances.
- Party units (controller class 0x3D) keep their items in the owning player's records instead:
  player + 0x70 is a list of 0x784-byte records found by unit id (006621B0); weapons at
  +0x730/+0x734, quick items at +0x740/+0x744, spells at +0x760/+0x764.
- **Unit prototype tables** (STLport vectors, same lookup rule as item_db.h): 007AF358 (0x120
  bytes, unit prototypes; +0x08 indexes 007AF398 with 0x168-byte records), 007AF268 (0x18, skills;
  +0x0C leads to the name, whose first letter picks the stat 's'/'d'/'i'), 007AF274 (0x44, skill
  levels; +0x34 an unsigned value).
- **UnitStats** (0x704): `+0x10/+0x18/+0x20` stats ('s', 'd', 'i'), six 0xF4-byte slots at
  `+0x110` (tail fields kind/current/maximum/factor at +0xE0..+0xF0), attribute bytes at `+0x6C8`,
  skill bytes at `+0x6CB`, 1-based skill levels at `+0x6D3` (level record index = skill * 3 - 1 +
  level).
- **LockKey** `{mask, id, level}`: levers and traps keep their lock at +0x244 (0052BB50).
- **DamageInfo** (spell.h): 00522B40 resets it, 00522B80 tests its elemental weights.
- **CMapObjectServer** / **CMapObjectClientSpecific**: virtual slots 17 and 18 are IsDead and
  IsOverkilled (hit points below -5 times the maximum).

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 0050E9F0 | CUnit::MarkChanged (map object base) | stateful: ours, snapshot the first 0x1E0 bytes, restore, original; bytes and EAX compared |
| 0050F630 | CUnit::HasEffect | pure |
| 0052E940 | CUnit::AddHostileMask | stateful: whole unit (0x4DC) snapshot/restore/compare |
| 00530180 | CUnit::SetRelationsB | stateful, whole unit |
| 00530120 | CUnit::SetSide | stateful, whole unit |
| 00530150 | CUnit::IsHostileTo | pure |
| 0052E9B0 | CUnit::SetUnknown438 | stateful, whole unit |
| 0052EA90 | CUnit::SetUnknown254 | stateful, whole unit |
| 0052EAD0 | CUnit::SetUnknown25C | stateful, whole unit |
| 0052EAF0 | CUnit::SetUnknown260 | stateful, whole unit |
| 0052EB10 | CUnit::SetMoveModes | stateful, whole unit |
| 0052EBE0 | CUnit::SetFlag40000000Off | stateful, whole unit |
| 0052F7F0 | CUnit::SetFlag1000000 (vslot 46) | stateful, whole unit |
| 0052EC10 | CUnit::GetEquipmentIds | output-only: original into a scratch int[11] |
| 00531070 | CUnit::AddWeapon | stateful: whole unit plus the item's reference count |
| 00531180 | CUnit::AddQuickItem | same |
| 00531820 | CUnit::AddSpell | same |
| 005310D0 | CUnit::RemoveWeapon | ours only (releases the item): slot empty and dirty flag set, or the slot returned when out of range |
| 005311E0 | CUnit::RemoveQuickItem | same |
| 005318B0 | CUnit::GetWeapon | pure |
| 00531920 | CUnit::GetQuickItem | pure |
| 005319A0 | CUnit::GetSpell | pure |
| 00531A10 | CUnit::GetSpellObject | pure |
| 00530FA0 | CUnit::HasRangedWeapon | pure (AL only; the one caller tests AL) |
| 005307B0 | CUnit::TotalItemWeight | pure (float bits) |
| 0052F820 | CUnit::CanCastSpell | pure |
| 0052F8B0 | CUnit::CanUseQuickItem | pure |
| 0052F920 | CUnit::GetMoveSpeeds | output-only: original into scratch floats (aliased outputs kept aliased) |
| 0052FA20 | CUnit::MoveSpeed2 | pure (float bits) |
| 0052FAF0 | CUnit::MoveSpeedFor | pure (float bits) |
| 0052FBC0 | CUnit::ScaledValue | pure (float bits) |
| 00530370 | CUnit::BaseSpeed | pure (float bits) |
| 005302F0 | CUnit::ModeSpeed | pure (float bits) |
| 00530930 | CUnit::GetPrototype | pure |
| 00530970 | CUnit::GetPrototypeModel | pure |
| 005309E0 | CUnit::GetPrototype23C | pure |
| 005308A0 | CUnit::SetPrototype23C | stateful: ours, restore the field, original; field and EAX compared |
| 005327D0 | CUnit::ResistanceTo | pure (float bits) |
| 00532DA0 | CUnit::AcceptsTarget (vslot 18) | pure (AL) |
| 00532E00 | CUnit::AnimationRatio | pure (float bits) |
| 005271C0 | UnitParams::Period | pure (float bits) |
| 005156A0 | CMapObjectServer::IsDead (vslot 17) | pure |
| 005156C0 | CMapObjectServer::IsOverkilled (vslot 18) | pure |
| 005104D0 | CMapObjectClientSpecific::IsDead (vslot 17) | pure |
| 005104F0 | CMapObjectClientSpecific::IsOverkilled (vslot 18) | pure |
| 00523CD0 | UnitStats::Period20 | pure (float bits) |
| 00525DF0 | UnitStats::AttributeBase | pure |
| 00525E10 | UnitStats::AttributeWithBonus | pure |
| 00527030 | UnitStats::SkillBase | pure |
| 00527050 | UnitStats::SkillWithBonus | pure |
| 00526780 | UnitStats::WeaponSkill | pure |
| 005267E0 | UnitStats::SkillLevelValue | pure (returned as double: the original's exact FILD qword) |
| 00528270 | UnitStats::SkillLevelRecord | pure |
| 00524280 | UnitStats::Value6E8 | pure (float bits) |
| 00525CD0 | UnitStats::Value6E9 | pure (float bits) |
| 00525D60 | UnitStats::Value6E9Arg (copy with an unused argument, RET 4) | pure |
| 005259B0 | UnitStats::Reduce | pure (float bits) |
| 00524020 | UnitStats::ReduceKind1 | pure (float bits) |
| 00528840 | UnitStats::GetFactors | output-only: original into scratch floats |
| 00522A80 | LockKey::Opens | pure |
| 00522B40 | ResetDamageInfo (thiscall, as fastcall) | output-only: original into a scratch DamageInfo |
| 00522B80 | HasElementalDamage (thiscall, as fastcall) | pure |

All 62 were also checked offline (scratch harness, not committed): game.exe mapped at 0x400000 in
a child process of the harness (the range reserved by the parent before the child's loader runs;
the game itself is never started), original and rewrite called side by side on synthetic units,
items, party records and databases with random values (including NaN/inf/denormal bit patterns),
about 1.26 million comparisons per FPU precision setting: no mismatches under precision control 24
and 53 bits. Under 64 bits only TotalItemWeight differs (see Risks).

All were renamed in Ghidra; also 00455950 `CEngineObject::World`, 00518D30
`CUnit::AnimationFactor`, 006621B0 `PartyRecords::Find`.

## Floating point

`unit.cpp` and `unit_stats.cpp` are compiled with `#pragma float_control(precise, on)`: under
`/fp:fast` the compiler turned `(stat - 25) + skill` into `(stat + skill) - 25` and distributed
`(y * 0.8 + x * 0.2) * 0.8` into new constants. In precise mode an assignment to a float variable
rounds it, so values the original keeps on the FPU stack across statements (accumulators, the
speed base, the GetFactors terms) are held in `double` locals; values the original stores to a
float (FSTP to memory) are float locals or `volatile float`. Return values are rounded to float at
the return in precise mode (the original returns ST0 unrounded); with the game's single-precision
FPU control word that changes nothing for finite results within float range.

UnitStats level values are unsigned dwords that the original loads with FILD of a zero-extended
qword. A plain uint32-to-double conversion compiles to FILD dword plus an FADD of 2^32 for values
>= 2^31, which precision control rounds; `LevelValue` loads them through a volatile int64.

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- 00455950 `CUnit::World` (object's owner + 0xAC, its slot 13), 00518D30
  `CUnit::AnimationFactor`, 006621B0 `PartyRecords::Find`.
- Called directly (rewritten by other areas): `CItemWeapon::GetType` (006A07F0),
  `CItem::GetCategory` (006A4230).
- Virtual calls through the objects' own vtables: GetClassId (slot 12) of the controller and of
  items, CUnit slot 55 (0xDC, +0x274), controller slots 36/37 (0x90/0x94), Release (slot 4) of
  items.

## Original bugs and oddities kept on purpose

- AddWeapon / AddQuickItem / AddSpell return 1 when the item is already in any slot, which is
  indistinguishable from "added to slot 1". AddSpell has no null check: a null item "is already
  there" at the first empty slot.
- GetSpellObject reads +0x44 of a party record's spell entry without checking it for null.
- CanCastSpell and CanUseQuickItem take a second argument they never read (RET 8); GetFactors a
  third (RET 0xC); 00525D60 is a copy of 00525CD0 with an unused argument.
- SkillWithBonus reads the skill record's name without a range check (a null record faults).
- GetQuickItem returns null for a party item whose +0x48 is not positive, but the non-party path
  returns the slot as is.
- Several setters return what MarkChanged leaves in EAX (the world pointer); reproduced as
  `std::uintptr_t` results in case a caller reads them.

## Skipped and why

- 00530280 (unit ground height; the script area forwards to it): its first instructions are
  PUSH ESI; PUSH EDI; MOV ESI,ECX; CALL - no 5-byte trampoline. A good candidate with a
  consistency-check verify.
- 00530570 (UpdateSizeClass: size class, standing heights from the controller and the figure box):
  calls controller slot 0xB4 with unknown side effects, so it cannot be run twice for a
  snapshot compare.
- 00530A60 (facing test against an attacker's quaternion): long x87 sequence; next candidate for
  the offline harness.
- 0052E970 (SetHostileMask, calls controller slot 0x98), 0052E9D0/0052EA40 (path speeds, AI path
  calls), 00531220/00531400/005313B0 (the carried-items CArray: growth and memmove), 005315B0 and
  0052E740 (release all items), constructors/destructors (0052E110, 0052E310, 0052E5B0),
  Load/Save (00531A80, 005321C0): stateful with allocation or many callees.
- The UnitStats builders (00522C50, 00523020, 005230C0, 005233E0, 00524560, 00524CD0, ...) and the
  big stat recomputations (00526450, 00528AB0, 00529090..): large; the leaf getters above are done.
- 00523D30 / 00523DB0 / 00523E00 call 0050F690, which Ghidra shows as void but which returns a
  float in ST0 that its callers use; needs that function first.
- 00500000-0050E000 (rendering, animation) and the CUnitClientSpecific code (00533E00-0053A000):
  out of scope for this pass.

## Risks

- `CUnit` replaced the definition in ai_map.h; a merge with another branch that edits that block
  will conflict (resolution: keep the include of unit.h).
- MarkChanged and HasEffect are base-class methods declared on CUnit; MarkChanged's verify only
  snapshots the first 0x1E0 bytes so it is safe on smaller map objects.
- Under 64-bit FPU precision (not the game's mode) TotalItemWeight's double accumulator rounds
  differently from the original's extended one.

## Second round: statistics in play, hits, stat raises, unit slots, own items

Code: `src/game/unit_vitals.cpp` (recomputation, regeneration, slot heals, experience curves and
gain, the stat raises of GiveStrength/GiveDexterity/GiveIntelligence/GiveSkill, the
strengthen/weaken scales), `src/game/unit_stats_pack.cpp` (resets, packing into UnitParams and
UnitVitalsPacked), `src/game/unit_hits.cpp` (UnitStats::ApplyHit / DamageSlot, armour and weapon
wear, UnitVitals::TakeHit, CUnit::IsBehind), `src/game/unit_update.cpp` (CUnit's virtual slots:
the per-tick update and the slots that forward map object events to the controller, StandPoint,
PlaceUnitOnGround, SetFromStats, Level), `src/game/unit_items.cpp` (armour, the carried array,
releasing everything). Verify: `tests/verify/verify_unit_vitals.cpp`, `verify_unit_hits.cpp`,
`verify_unit_update.cpp`, `verify_unit_items.cpp`. Scenario: `tests/scenarios/units.txt`.

Shared files touched: `src/game/combat.cpp` (one line: the double returned by
`UnitStats::Value6E9Arg` is cast to float where it is stored). `unit.h` (this area's header):
UnitStats' unknown fields are named (no offset moved), UnitStatSlot / UnitStatLayer / UnitVitals /
UnitVitalsPacked / UnitParams (full layout, 0x108 bytes) / UnitParamsSlot added, CUnit's
`unknown_270` and `unknown_4A8` split into named fields (`dead` +0x274, `heightOffset` +0x320,
`carriedCapacity` +0x4A8, `carriedGrowBy` +0x4AC), new method declarations.

### ST0 results are now returned as double

Under Rebirth the FPU was thought to run above 24-bit precision (see `CItemWeapon::RollDamage`;
not confirmed: measured 24-bit at every frame boundary in both installs, docs/notes/rebirth.md), and
the original returns float results unrounded in ST0. Rounding them to float at the return changed what
callers that keep using ST0 compute: 00532A20 truncates `Reduce`'s result with `_ftol` for the
displayed hit points. These first-round functions now return `double` (bits compared as 64-bit
doubles by their wrappers): UnitStats Period20, AttributeWithBonus, SkillWithBonus, Value6E8,
Value6E9, Value6E9Arg, Reduce, ReduceKind1; CUnit MoveSpeed2, MoveSpeedFor, ScaledValue,
BaseSpeed, ModeSpeed, TotalItemWeight, AnimationRatio (and the AnimationFactor forwarder);
UnitParams::Period. Functions whose result is a float loaded from memory (ResistanceTo,
AttributeBase, ...) keep `float`.

### Structures

- **UnitStats** (names provisional): +0x04 experience, +0x08 spent experience, +0x0C/+0x10,
  +0x14/+0x18, +0x1C/+0x20 the three stats as base / current, +0x24/+0x28 speed base / current
  (times the legs factor), +0x2C/+0x30 a rate (15 or dexterity * 0.2 + 10 / at least 1 with haste),
  +0x34 four mode speeds, +0x44 -2 for heroes, +0x50 load, +0x54 capacity (1e6 for units without a
  text key), +0x58 legs factor, +0x5C arms factor, +0x60 (1: body slots used), +0x70..+0xAC the
  combat values (combat.h), +0xB0 and +0xE0 six base/current pairs each (effects 12-14 raise the
  first three of +0xB0; effects 15-17 lower +0xE0[0], [3], [2], effect 19 raises [4]; skill 15
  raises +0xB0[1] by its percentage), +0x110 six body slots, +0x6F8 flags (1 multiplied hit, 2 hit
  at the head, 4, 8 alive after RecomputeHitPoints, 0x10 hit landed), +0x6FC the fixed hit point
  maximum of units without a text key, +0x700 pending experience.
- **UnitStatSlot** (0xF4): +0x00 type (2 arm, 3 leg; reset to 4), +0x04 damage types whose full
  hit severs it, +0x08 three armour layers (UnitStatLayer, 0x48: absorb per damage type at +0x04,
  state at +0x3C, > 1 present), +0xE0 kind (0 unused, 1 severed, 2 disabled, 3 working), +0xE4/+0xE8
  current/maximum, +0xEC its share (the body slot's share is the reference), +0xF0 its factor in
  the hit points (Reduce).
- **UnitVitals** (CMapObjectServer + 0x20, 0x58): +0x04 regeneration counter, +0x08/+0x0C hit
  points / maximum, +0x10 hit point regeneration, +0x14/+0x18 mana / maximum, +0x1C mana
  regeneration, +0x20 seven base/current protection pairs (current: what a hit loses per damage
  type).
- **UnitParams** (CUnit + 0x330, 0x108) and **UnitVitalsPacked** (CUnit + 0x17C, 0x18): UnitStats and
  UnitVitals rounded with FISTP (bytes and shorts; the mode speeds as `* 255 / 2` and the speed as
  `* 25.5`, clamped to 255 unsigned; damage and its spread truncated with `_ftol`).

### Statistics in play

- Every world update the server controller (00552300) calls **UnitVitals::Regenerate** (00523740)
  for a living unit: every 15th call mana (unless the unit has an order) gains
  `(1 + 0.01 * skill 19) * regen * maximum`, and the hit points heal the slots by
  `maximum * (1 + 0.01 * skill 18) * regen` (times effect 0x1F when positive) through
  **HealSlots** (00525800: shared among the slots below their maximum by factor; a slot above 0.001
  works again), then hit points = Reduce(maximum). When it returns 1 the controller calls
  **Recompute** (00524560): stats from bases plus skills 23-25, the protection and the value pairs
  from the strongest effect of each type, the hit point / mana maxima from the experience curves
  `base ^ (ln(experience / divisor) / ln(logBase)) * stat / divisor * scale * (1 + 0.01 * skill)`
  (FYL2X and the CRT's `_CIpow`, run as inline assembly in the original's order), the
  strengthen/weaken scale (at least 1), the slots' maxima, capacity, load, legs/arms factors, rate.
- Hits (CUnitServer::TakeHit -> **ApplyHit** 005254B0): flags, the damage per type beyond the
  vitals' protection (times the difficulty for units without a player), wear of the attacker's
  weapon (**WearItem** 005253E0: condition shared by +0x48 pieces; remarks 0x24 broken / 0x26 worn
  through the controller), then **DamageSlot** (00525070) on the hit location, or on all six slots
  by their factors for location 6. DamageSlot takes each type through the slot's armour layers
  (text-key units; **WearArmour** 00525720 wears the pieces from the tables at 0073E09C/A0/A4,
  remarks 0x23 / 0x25), applies the backstab multiplier, severs the slot (-5 times its maximum;
  remark 0x1E arm / 0x1F leg) or disables it, and logs "<type> passed damage = <n>[ HL was
  crippled| HL is dead]" to the stream at 007AF284. Then 00532AC0 (not rewritten) drops broken
  equipment.
- **CUnit::IsBehind** (00530A60, backstab test of PrepareAttack): only units with a player and
  without a bow/crossbow in hand; the normalised direction to the target against the target's
  facing from its quaternion; behind when the dot product is below -0.5 (or NaN).
- Stat raises: GiveStrength / GiveDexterity / GiveIntelligence (script commands 0xCE-0xD0) call
  **RaiseStrength / RaiseDexterity / RaiseIntelligence** (00529090 / 00529220 / 00529400):
  base + amount, the maxima recomputed (each with its own rounding steps: strength and dexterity
  round the curve product to float before the skill percentage; dexterity goes through 00523DB0
  and 005282D0), strength widens the unit by 0.2 (base and figure scale x), dexterity lowers it by
  0.2 (y, at least 0), then Recompute. GiveSkill calls **RaiseSkill** (00528AB0, skills) or
  **RaiseAttribute** (00528DA0, "science": attributes): the level byte grows and
  `truncate(1.61 ^ (new / 007C5480) * 007C547C - 1.61 ^ (old / 007C5480) * 007C547C)` is booked
  as experience and spent experience.
- Experience: **AddExperience** (00523920), **AddScaledExperience** (00523960, times
  1.04 ^ (intelligence - 25); kills and the console's experience command), **GainExperience**
  (005239B0, through the controller's 00553380) also refreshes the maxima.

### CUnit's virtual slots (vtable 0073EA8C)

6 Tick (0052F7D0), 19 AttachCompanion (0052E870), 26 LoadUnitFigures (0052E910), 27
RecomputeUnitScale (00532A80), 28 EffectsChanged (00532AB0), 30 ResetUnitScale (00532AA0), 32
StatsChanged (0052EB50), 33 ScaleEffectChanged (0052EB30), 35 SetServerStats (00530A20), 37
UpdateSizeClasses (00530A50), 48 StandPoint (005303A0), 51 PlaceUnitOnGround (0052F740). Not
rewritten: 0 destructor (0052E2A0), 2/3 Create/Destroy (0052E400/0052E590), 5 release (0052E740),
9/10 Load/Save (00531A80/005321C0), 25 (0052FC40), 34 (0052EB70), 49/50 network state
(0052EF90/0052EC70), 52/53 (0052FCC0/0052FDF0). CFakeUnit (0073BFE8) shares every rewritten slot.
(Later: 49/50 and 52/53 are rewritten, see "Network state" and "Unit points" below.)

### Functions rewritten (second round)

| Address | Name | Verification |
|---|---|---|
| 00523740 | UnitVitals::Regenerate (every world update) | stateful: ours, vitals (0x58) and stats (0x704) snapshot, restore, original; bytes and AL compared |
| 00524560 | UnitStats::Recompute | stateful, stats and vitals |
| 005243D0 | UnitStats::RecomputeHitPoints | stateful, stats and vitals |
| 00525800 | UnitStats::HealSlots | stateful, stats; AL |
| 00525900 | UnitStats::Heal | stateful, stats and vitals |
| 00525930 | UnitStats::RegenerateSlots | stateful, stats and vitals |
| 00525A00 / 00525A70 | UnitStats::SetSlotMaxima / ScaleSlotMaxima | stateful, stats |
| 00525AF0 / 00523C80 | UnitStats::RefillSlots / RestoreSlots | stateful, stats; EAX (end pointer) |
| 00524060 / 00524110 | UnitStats::LegsFactor / ArmsFactor | pure (float bits) |
| 005241C0 | UnitStats::MovementState | pure |
| 00523E80 / 00523F50 | UnitStats::HitPointCurve / ManaCurve | pure (double bits) |
| 00524320 | UnitStats::RateValue | pure (double bits) |
| 005282D0 | UnitStats::SkillLevelValueCopy | pure (double bits) |
| 00523D30 / 00523E00 | ScaleDamageByEffects / ScaleHitPointsByEffects (__stdcall) | pure (float bits) |
| 00523DB0 | HitPointEffectScale (__stdcall) | pure (double bits) |
| 00523920 / 00523960 / 00523C20 | UnitStats::AddExperience / AddScaledExperience / TakePendingExperience | stateful, stats |
| 005239B0 | UnitStats::GainExperience | stateful, stats and the server object's vitals |
| 00528AB0 / 00528DA0 | UnitStats::RaiseSkill / RaiseAttribute | stateful: stats, vitals and the unit (0x4DC); AL |
| 00529090 / 00529220 / 00529400 | UnitStats::RaiseStrength / RaiseDexterity / RaiseIntelligence | stateful: stats, vitals, unit; AL |
| 00522C50 | UnitVitals::Pack | output only: original into a scratch record; bytes and the returned end offset |
| 00522CE0 | UnitStats::Pack | output only (0x108 bytes) |
| 00523370 | UnitVitals::Reset | output only; returned end offset |
| 005233B0 / 00523550 | UnitVitalsPacked::Reset / UnitParams::Reset | output only |
| 005233E0 / 005232B0 | UnitStats::Reset / ClearOuterLayers | output only (0x704 bytes) |
| 00523600 / 005236A0 | UnitStatSlot::Reset / UnitStatLayer::Reset | output only |
| 005254B0 | UnitStats::ApplyHit | ours only: a result of 1 leaves hit points = Reduce(maximum) |
| 00525070 | UnitStats::DamageSlot | ours only: the same for a usable slot |
| 005253E0 | WearItem (__fastcall) | ours only: the condition drops by exactly the shared wear |
| 00525720 | WearArmour (__stdcall) | ours only (counted) |
| 00524FE0 | UnitVitals::TakeHit | stateful, vitals |
| 00530A60 | CUnit::IsBehind | pure (EAX) |
| 0052F7D0 | CUnit::Tick (slot 6, every world update) | ours only: returns 1 |
| 00532A80 / 00532AB0 / 00532AA0 / 00530A50 | CUnit slots 27 / 28 / 30 / 37 | ours only (counted) |
| 0052EB30 / 00530A20 | CUnit slots 33 / 35 | ours only: result 0 without a controller / the class id for other controllers |
| 0052E910 | CUnit::LoadUnitFigures (slot 26) | ours only: flags_ 0x80 afterwards |
| 0052E870 | CUnit::AttachCompanion (slot 19) | ours only: a controller-type companion is attached both ways, result = its reference count |
| 0052EB50 | CUnit::StatsChanged (slot 32) | ours only: returns 1 |
| 005303A0 | CUnit::StandPoint (slot 48) | output only: original into a scratch point |
| 0052F740 | CUnit::PlaceUnitOnGround (slot 51) | stateful, whole unit |
| 00532A20 | CUnit::SetFromStats | stateful, whole unit |
| 00532D40 | CUnit::Level | pure |
| 00530730 / 00530840 | CUnit::EquipArmour / UnequipArmour | ours only: slot contents and the returned piece |
| 005315B0 | CUnit::ReleaseAllItems | ours only: every slot and the carried array empty |
| 00531220 / 005313B0 | CUnit::AddCarried / RemoveCarried | ours only: count, order, reference |
| 00531110 | CUnit::SendEquipment | ours only (counted) |
| 005308D0 / 00530920 | CUnit::SetPrototype23CByName / SetPrototype23CIndex | field and EAX compared (original after ours, field restored) |

The rewrites call each other (and the stateful callees in this range) through their game
addresses (`Call*` EI_ORIGINAL forwarders), so the nested verify wrappers see those calls; the
pure lookups (SkillLevelRecord, Reduce, TotalItemWeight) are called directly. None of the
addresses is on the Rebirth list, and no callee is. The compiled x87 code of the experience
curves, Recompute, IsBehind and RaiseLevel was read back (`dumpbin /disasm`); the curves run in
inline assembly. Not run in game yet.

### Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- Library code: `_CIpow` 006E81A0 and `_ftol` 006E853C (inline assembly), the ostream inserters
  006FC31D / 006FC7FA / 006FC0B0 and the manipulator call 00434FE0 (`UnitLogStream`, unit_hits.cpp).
- Not rewritten: 00517700 (CObject3D: set the figures' scale), 00517500 (CObject3D: load the
  figures), 0050E890 / 0050E970 (CMapObject slots 19 / 32), 00552F40 (CUnitServer: copy stats
  in), 00552780 (CUnitServer: party remark), 00532AC0 (CUnit: drop broken equipment after a hit),
  00514930 (find a unit prototype by name).
- Rewritten, called through their addresses: 0050F690 EffectValue, 0050F1C0 CMapObject::Update,
  0050F2D0 RecomputeScale, and this area's 00525800, 00525A70, 00524060, 00524110, 00524560,
  00523F50, 00523DB0, 005282D0, 00522CE0, 00523E80, 00523E00, 005253E0, 00525720, 00525070.
- Globals read by address: the experience curve constants 007C5454..007C5480, the slot ratio
  thresholds 007C543C..007C544C, the wear threshold 007C549C, the difficulty table 007C54A0
  indexed by 0079BEC0, the hit log stream pointer 007AF284, the armour tables 0073E09C / 0073E0A0 /
  0073E0A4, the damage type names 0078F694..0078F6D8 and the log texts 0078F658 / 0078F664 /
  0078F678.
- Controller and world virtual slots through `ei::VCall` (controller 6, 13, 19, 22-24, 26-28, 32,
  33, 40, 44, 49; world 29 / 30 ground and water surface height).

### Original bugs and oddities kept on purpose (second round)

- RaiseSkill / RaiseAttribute store the level as a byte (it wraps) but book the worth of the
  unwrapped sum.
- RaiseStrength computes the new maximum from the current strength (the raised base only counts
  after the Recompute that follows); the three raises round their intermediate products
  differently.
- HealSlots returns AL = 1 with the FPU status word and float bits in the rest of EAX; ours
  leaves other upper bytes. 00526450 and 00626FA0 pass that EAX on; no caller found reads more
  than AL.
- Regenerate's third argument is a byte whose upper bytes are the controller's +0x190 bits.
- UnitStats::Reset leaves the last skill level byte (+0x6F7), +0x34..+0x4C, +0x58..+0x6C, +0x8C and
  +0x6FC alone; the slot and layer resets leave the layers' +0x20 values.
- DamageSlot logs "termical" (sic) and one line per damage type; ApplyHit wears the weapon by the
  part of each damage type the protection absorbed.
- IsBehind is never true for units without a player.
- SetPrototype23CByName returns the low half of its division product in EAX.
- 00522AE0 and 00522B20 (not rewritten) return sums/maxima of shorts with the upper halves of their
  registers left over (this pointer bits); see Skipped.

### Skipped and why (second round)

- 0052EC70 / 0052EF90 (network state write / read, slots 50 / 49), 00531A80 / 005321C0 (Load /
  Save): the memory stream classes (0047FBF0, 0047DCA0, ...) are not rewritten; no verification
  short of capturing streams.
- 0052E740 (slot 5: release, player removal 00665B50 / 00665A70), 00532AC0 (drops broken
  equipment through a CTypedPtrList to the player), 00531400 (random item for stealing; CArray
  SetSize 00533740 and ISAAC), 005316F0 (collects items into two lists), 0052EB70 (slot 34: packs
  stack copies that 005136D0 fills; whatever it leaves unset is stack garbage), 0052FC40 (slot 25:
  controller slot 40 and 0056D740).
- 00523020 / 005230C0 / 00523300 / 00522BC0 (armour layers from the equipped pieces; next
  candidates), 00526450, 005260A0, 00526860, 00526C70, 005271E0, 00527850, 00525E90, 00527EB0,
  00524CD0 (skill and attack builders), 005136D0 (prototype statistics, map object range).
- 00522AE0 / 00522B20: results depend on leftover register halves.
- 00530570 / 00530280 (first round's notes still apply).

### Risks (second round)

- Recompute and Regenerate run for every unit (Regenerate every world update, Recompute after
  every 15th and after hits and heals): a mistake shows at once. Both are verified side by side,
  so a mismatch is reported rather than hidden; the x87 code depends on the FPU precision as
  everywhere (double locals equal the original's extended values at 24- and 53-bit precision).
- The slot heals, ApplyHit / DamageSlot and the item functions are checked for consistency only.
- The type changes (float -> double) of the first-round functions are ABI compatible (both in ST0);
  C++ callers in other areas: combat.cpp (`Period20`, `Value6E9Arg`), unit.cpp.
- The scenario prints MaxHP / MaxMana around the stat raises instead of checking them: when the
  displayed maxima follow the server's depends on code outside this area.

## Third round: Load / Save

Code: `src/game/unit_save.cpp`; verify `tests/verify/verify_unit_save.cpp`. unit.h gained
`UnitStream` (slot 1 Read, slot 2 Write, version at +8) and `CUnit::LoadUnit` / `SaveUnit`.

| Address | Name | Verification |
|---|---|---|
| 005321C0 | CUnit::SaveUnit (slot 10) | both into capture streams; bytes and sizes compared |
| 00531A80 | CUnit::LoadUnit (slot 9) | ours only: item slots referenced, carried array consistent, prototype indices valid |

Format: the map object part (0050F7A0 / 0050F900), the controller (slot 9 / 10), the name (+0x238,
zero-terminated), relations, the player's id (+0x24 of the player, -1), side, +0x254, +0x255,
+0x258..+0x278 (the active weapon among them), the +0x27C sub-object (005C7860 / 005C79B0), +0x320,
+0x324, +0x328, the active weapon again, weapons / quick items / spells / armour as item ids, +0x4C8
(0x14 bytes), +0x248, the carried items, +0x32C, +0x438 (version 0x49), the prototype names
(version 0x4D; the second from 0x6A, before that looked up by the first name with 00514930). Load
resolves items in the world's item manager (ManagerContext slot 18), moves the unit between players
(00665B50 / 00665A70, MarkChanged(0xB), controller slot 18), and calls slot 32. The world_load
scenario saves (new game autosave) and loads.

Oddities kept: the active weapon is saved twice (the second read wins); Save copies the prototype
name from address 0 when the index is out of range (crashes as the original would); the name lookup
through 005335E0 inserts unknown names into the prototype name map.

## Fourth round: creation, prototypes, lifetime, the client's armour models

Code: `src/game/unit_create.h/.cpp` (prototype name index, CUnitServer::SetStats / Shout, broken
equipment), `src/game/unit_create_stats.cpp` (starting statistics from the prototype records,
applying a prototype to a new object, the map object factory, UnitStats::ResetToBase),
`src/game/unit_client_models.h/.cpp` (CUnitClientSpecific: armour figure parts and the composed
skin), `src/game/map_object_lifetime.cpp` (constructors and destructor bodies; see
map_objects.md "Lifetime"). Verify: `tests/verify/verify_unit_create.cpp`,
`verify_unit_client_models.cpp`, `verify_map_object_lifetime.cpp`. Scenario:
`tests/scenarios/unit_create.txt` (spawns eight prototypes, replaces one, kills and removes).

Shared files touched, additive declarations only: `unit.h` (CUnit::DropBrokenEquipment,
Construct, DestructBody, DeletingDestructor, Reset; UnitStats::ResetToBase;
CMapObjectServer / CMapObjectClientSpecific::Construct / DestructBody), `combat.h`
(CUnitServer::SetStats, Shout, DestructBody), `map_object.h` (see map_objects.md). Comments of the
forwarders in unit_update.cpp / unit_items.cpp / unit_hits.cpp now name the rewrites.

### How a unit is made

The map object manager's factory (vtable 0073DCD8 slot 14, 00512730) allocates and constructs by
class id: 0x32-0x34 units (CUnit, 0x4DC, 0052E110), 0x35-0x38 (0x238: 005296E0 / 005297C0 /
005298A0 / 00529980), 0x39 (0x580, 004466D0), 0x3A (0x23C, 0052BEE0), 0x3B (0x290, 00529DD0),
0x3C (0x264, 0052B1F0), 0x300D (0x78, 0050C5F0), 0x3010 (0x23C, 00529A80). The zone loader then
gives the object its prototype (00513B60, ECX = an object the builders never use but pass on):

- The unit prototype table 007AF358 is a vector of 0x120-byte records plus, at +0x0C, an STLport
  hash_map<string, record*> from names to records; 00514930 and its copy 005144D0 are operator[]
  on it (an unknown name is inserted with a null record). The NPC table 007AF378 is laid out the
  same way (005145D0, its own instantiation 00514A20). 00513240 gives a prototype's object class
  (its model record's +0x04, 0x33 without one).
- Without a prototype: defaults (005134F0: statistics 50, values 100, slots of type 1 working at
  100; or, without statistics, vitals 1000 / regeneration 10 / protection 100), slot 33 (0),
  prototype index 0 and the debug line "Can't find prototype %s for object %d".
- Units: vitals and statistics from the records (005136D0): hit points and mana from the
  prototype (+0x48 / +0x4C), regeneration from the model (+0x08 / +0x0C), protection = the model's
  seven values times the prototype's +0x58; stats from the NPC record or a third of the hit points
  / mana (and 25), mode speeds, values, size classes, slots (type and vital mask from the skill
  level record of the slot's index), combat values and damage weights; attributes and skills
  from the NPC record or rounded from the prototype; the NPC's experience (AddScaledExperience)
  and skills (00526450). The object gets them through CMapObject::SetServerData and its slot 35,
  stands lower by the model's +0x104 (+0xF4..+0xFC), takes the prototype's scale (00517700, the
  base scale +0x1AC, slot 30) and its second prototype (005308A0).
- 00513DA0 builds an NPC's statistics alone (stdcall; a fatal error without a prototype, a debug
  line without an NPC record), then ResetToBase (00524CD0, scale 1) around RefillSlots.
- 00524CD0 (UnitStats::ResetToBase): current values from the bases, skill 15 raises valuesB0[1]
  by its percentage, skills 23-25 the stats, unknown_044 = -2, slot maxima from the hit point
  curve (current clamped), the load (its argument), capacity, legs / arms factors, rate.
- The skill database (007AF268: skills, 0x18 each, +0x0C -> the name; level records 0x44 each at
  +0x0C, index = skill * 3 + level - 1): LearnSkill (00526450) sets a skill's level from a level
  record (0051FF00 finds its index) and, when the curve's hit point maximum is above 1 before and
  after, heals the slots so the hit points keep their proportion; CanLearn (005260A0) checks the
  record's learnable byte (+0x40), stat minima (+0x24..+0x2C), required attribute (+0x14 name,
  +0x18 index, +0x20 value) and skill level (+0x0C, by name through 0051FE80), that no level of
  the same skill id (+0x10) is known in another skill, and that the level is new.
- WeaponSkillBonus (00526C70; GetAttackParams uses it): weapon types 0-4 use skill 0, 5-6 skill
  1: for text-key units skill byte + the weapon prototype's +0x7B / +0x7C + (the stat named by the
  skill's first letter - 25) + the level value of skill `type`; others the skill byte + that
  level value. Type 8: SkillWithBonus(0) + the weapon's +0x7B (the byte alone without a text key);
  other types 0.

### The client's armour models (CUnitClientSpecific, class id 0x3D)

+0x1C the unit, +0xA0 the packed ids of the seven armour slots, +0xBC the four weapons. The figure
part names of the three armour rows (0073EC24, char[4] each): body armour "bd", "rh1".."lh3";
trousers "hp", "rl1".."ll3"; helmet "hr" (hair, "hr.<nn>" with the prototype's +0x10) and "hd".
Armour slots 0 / 1 / 2 use rows 2 / 0 / 1.

- 005381B0 (slot 26) WearArmourModel: an armour piece replaces each part of its row by
  "<part>.armor<nn>" when the unit's figure has that entry, else restores the plain part.
- 00535CB0 (fastcall) collects the same names for a prototype, armour ids and weapons
  (006A1780), and every plain part of the three rows.
- 00536300 / 00536670 compose the skin texture of party units through the texture manager
  (007AF744, 00693180): layers 0x32 (the skin), 0x3005 (armour), 0x3004 (weapon); textures
  "<prototype>skin_<nn>.mmp" and "<prototype><armour or weapon texture>.mmp"; names
  "mo<id>p" / "prot<prototype>p" (detailed) or ...s (two layers only).

### Functions rewritten (fourth round)

| Address | Name | Verification |
|---|---|---|
| 00514930 / 005144D0 | UnitPrototypeTable::FindByName | name indexed: both, same record and count; else ours only, the name then indexed with null |
| 005145D0 | NpcPrototypeTable::FindByName | the same |
| 00513240 | PrototypeObjectClass (stdcall) | both (the second run finds what the first inserted) |
| 005134F0 | UnitStatsBuilder::SetDefaults | output only: the original into scratch copies; bytes and EAX |
| 005136D0 | UnitStatsBuilder::BuildFromPrototype | output only (vitals 0x58, statistics 0x704) |
| 00513DA0 | BuildNpcStats (stdcall) | output only (statistics) |
| 00524CD0 | UnitStats::ResetToBase | ours, snapshot, restore, original; 0x704 bytes |
| 00526450 | UnitStats::LearnSkill | the same |
| 00526C70 | UnitStats::WeaponSkillBonus | pure (double bits) |
| 005260A0 | UnitStats::CanLearn | pure (EAX) |
| 00513B60 | UnitStatsBuilder::ApplyPrototype | ours only: result 1, index -1 or valid |
| 00512730 | CMapObjectManager::CreateObject | ours only: an object with a vtable for the known ids, else null |
| 00552F40 | CUnitServer::SetStats | ours only: companion's hit points / mana at their maxima, part 4, result the companion |
| 00552780 | CUnitServer::Shout | ours only (counted) |
| 00532AC0 | CUnit::DropBrokenEquipment | ours only: no broken piece left, weapons packed as predicted; untouched without a player |
| 005381B0 | CUnitClientSpecific::WearArmourModel | ours only: a worn piece is in an armour slot |
| 00535CB0 | CollectArmourPartNames (fastcall) | output only: the original into fresh vectors, the appended strings compared |
| 00536300 | CUnitClientSpecific::BuildSkinLayers | output only: the original into a fresh array |
| 00536670 | CUnitClientSpecific::ComposeSkin | ours only (counted) |
| 0052E110 / 0052E310 / 0052E2A0 / 0052E5B0 | CUnit constructor, destructor body, deleting destructor, Reset | see map_objects.md "Lifetime" |
| 00546230 | CUnitServer::DestructBody | ours only: base state (combat.md called it the constructor) |

The x87 code of BuildFromPrototype (the cosines are computed at run time: pi / 180 and 90 are
volatile so the products are not folded at compile time) and ResetToBase was read back
(`dumpbin /disasm`): stores to float where the original stores, the original's operand order.

### Forwarders added (fourth round; the standalone build must replace these)

- Unit range: 00526450 (learn a skill), 0051FE00 (skill database lookup), 0052E230 / 004CD080 /
  005C6270 / 0047FB70 / 00510340 (sub-object constructors / record reset and destructor),
  0052E740 (CUnit slot 5, called through the vtable entry 0073EAA0), 0050E840 (CMapObject
  Destroy).
- Elsewhere: 00665470 (the player receives items), 00539580 (skin layer array SetSize),
  006A3480 / 006A0AA0 (armour / weapon texture names), 00693180 (texture manager: compose),
  005AA5F0 (figure manager: model by name), 00516A80 / 00517240 / 005167E0 (find / add / remove
  a figure part), the CUnitServer member destructors of 00546230 (00546480, 005D4F70,
  00545CB0, 00557410, 00557890, 0040A3A0, 005575F0, 0052B030, 004CB770, 00557E70, 00558020,
  00539890), the game's `_ftol` (006E853C) and `_CIpow` (006E81A0, the hit point curve).
- Called through their addresses (rewritten elsewhere): 00525AF0 RefillSlots, 00523370 /
  005233E0 / 00523550 / 005233B0 resets, 00523960 AddScaledExperience, 00524060 / 00524110,
  0050F050 SetServerData, 005308A0, 0066EA70 / 0066D6E0 player messages, 006A1780, 006A3200,
  00674850, 00455930.
- Globals by address: the prototype / model / NPC / skill tables (007AF358, 007AF378, 007AF398,
  007AF268, 007AF3B8), the armour part names 0073EC24, the figure manager 007AF5F4, the texture
  manager 007AF744, the hit point curve constants 007C5454..007C5464, format strings
  0078FB0C..0078FB7C, 0078F22C / 0078F258 / 0078F288.

### Original bugs and oddities kept (fourth round)

- The prototype name lookups insert unknown names (null record).
- BuildNpcStats reads the prototype's model index before testing the prototype for null.
- ApplyPrototype hands the object vitals and statistics built in uninitialised stack copies: what
  the builders leave alone (the vitals' and statistics' first dword, then OR 1; statistics +0x58,
  +0x5C, the last skill level byte) is stack garbage in the original. Ours start from zero:
  **a known divergence** in fields nothing seen reads before they are recomputed.
- CollectArmourPartNames pushes the five empty names of the helmet row into the plain names (its
  loop tests the name's address, never null).
- WearArmourModel stores the id even for armour slots 3-6, which have no figure parts.
- DropBrokenEquipment gives broken pieces to the player and releases the unit's reference after;
  the weapons above a broken one move down.
- The unit's Reset clears the item slots without releasing the items.

### Skipped (fourth round) and why

- 0050E9B0 (CMapObject slot 34): packs vitals the builder never fills when the prototype exists
  (stack garbage).
- 0052EB70 (CUnit slot 34) and the other UnitStats builders (005260A0, 00526860, 00526C70,
  005271E0, 00527850): next candidates, all output-only.
- 0052E400 / 0052E590 / 0050E770 (Create / Destroy slots), 0052EF90 / 0052EC70 (network state):
  stream classes and many callees.

### Risks (fourth round)

- The constructors run for every object made; their verify compares every byte with the
  original's run on a copy, so a difference is reported at once. The destructors and the
  factory run ours only.
- ComposeSkin and WearArmourModel build CString / STLport temporaries in the original's order
  (the allocations match); not run in game yet.

# Network state

Code: `src/game/net_state.cpp` (new); verify wrappers: `tests/verify/verify_net_state.cpp`, with the
DeltaState save / restore in `tests/verify/delta_snapshot.h` (new). No shared files changed. The
four functions are virtual slots, registered as free functions (thiscall as fastcall; rule 4).

| Address | Function | Verify |
| --- | --- | --- |
| 0050EAF0 | MapObjectWriteNetState (map objects' writer) | differential, sampled: state copied, original writes, its result copied, state put back, ours writes; states and delta globals compared |
| 0052EC70 | UnitWriteNetState (slot 50) | the same |
| 0050EC30 | MapObjectReadNetState (RET 4) | differential, sampled, ours first: then the DeltaState, the received stream (state +0x1C) and the globals are put back and the original reads again; result, states, stream, globals and the decoded fields compared |
| 0052EF90 | UnitReadNetState (slot 49, RET 4) | the same, with the unit's fields |

Fields written / read (DeltaState at +0x1C4):
- **Map objects:** 0 is the 0x18 bytes at +0x17C. 1 is the position +0xC4 (read: SetPosition 00517700). 2 is the hidden bit (flag 8), the
  position +0x1C and the orientation +0x34 (read: world slot 41 moves the object, slot 51 with
  +0x24). 3 is the effects: a count byte, then per 0x14-byte effect bytes +0, +1, the word +4 and the byte +0x10
  raw (read: CArray::SetSize, then the companion +0x170's slot 13 with `first == 0` unless a
  unit). The reader's result says whether each field's snapshot stream was consumed (slot 5 ==
  slot 4); field 0 sets it, the rest AND it; field 0 calls slot 58 when hit points changed (or
  `first == 1`).
- **Units**, after the map object's fields:
  - 4 is the stats block +0x330 (0x108). On read: the six stat fractions before / after give
    controller slot 46 per changed stat, slot 31 when any changed (or `first == 0`), and on a hit point drop
    (or flag 0x10 in +0x437) the companion's slot 16 with `_ftol(lost * max)` and the kind bits.
  - 5 is the 11 equipment ids (the weapon -1 with flag 0x40000000); writing then refreshes a class
    0x3E controller (slot 29).
  - 6 is +0x264, +0x268, +0x26C. On read +0x270 = +0x26C, and slot 25 runs on a change.
  - 7 is the packed bits of +0x25C / +0x260 / +0x274 / +0x278. 8 is +0x254, +0x255, +0x258, +0x256.
  - 9 is the CString +0x238. 10 is the path (005C7DB0 / 005C7FC0) and the motion's speeds.
  - 11 is the masks, +0x250 and the player id (a new player through the local view). 12 is +0x4C8. 13 is +0x438.
  - The reader then re-equips (slots 32 / 33) and refreshes (25, 29) when something changed.

The first reader check compared whole effect entries. About 1% of the unit reads with `first == 0`
mismatched, while the result agreed and map objects never mismatched. Only the unit reader's own
field 3 calls the companion's slot 13 (the effects' particles and sounds) with `first == 0`, and
that slot can change the bytes of an entry the reader does not read. Ours runs it, then the
original's second read runs it again over the same entries, so those bytes differ. The check now
compares only the bytes the reader writes (+0, +1, the dword +4, +0x10). The mismatch message now
names the first part that differs (result, delta state, received stream, globals, or a named
field), so a further mismatch can be traced.

With named parts the remaining 1% (all `first == 0`) showed "orientation +0x34 differs". This is
also a side effect of the second read. After field 10 (the path), the controller's slot 41 runs. For
a party unit's client controller (class 0x3D, 00535B20) that is 00535850 with rate 0, which puts the
unit on its path. When the controller's byte +0x76 is set, 00535850 clears the byte and snaps the
unit's orientation to the path direction (00401F00 on the unit). Ours consumes the byte. The
original's second read then finds it clear and keeps the orientation it just read. The check now
saves that byte before ours and puts it back before the original's read. Ours was not changed.

Kept from the original: the field 3 count is one byte read into a word; the companion's slot 13
and the stat slot get their bool / flag arguments as 0/1 and the byte (the original pushes
registers whose upper bytes are left over). The stat fractions and hit point ratio are plain C++
(rule 7).

# Unit points (slots 52 / 53)

Code: `src/game/unit_points.cpp` (new); verify wrappers: `tests/verify/verify_unit_points.cpp`
(differential: the original into a scratch point, within 8 ULPs). Registered as free functions.

| Address | Function | Notes |
| --- | --- | --- |
| 0052FCC0 | UnitPredictPosition (slot 52, RET 8: out, time) | server units (slot 55): slot 53. Client units following a path order (kind 2, no flag 0x8000000): the motion sampled at (float)time, x / y back from half-cells, z kept. Otherwise the position. Returns out. |
| 0052FDF0 | UnitActionPoint (slot 53, RET 4: out; returns out in EAX) | server units only (else the position). The animation entry of +0x254 (00518F80, its low byte) of kind 0x1C0000 offsets the position: 0x400000 -Y x +0x148, 0x800000 +Y x +0x148, 0xC00000 +X x +0x144, 0x1000000 -X x +0x144 (axes 0040F560 / 0040D940). A missing entry logs "Can't find animation entry index". When the world is live, a point where size class 3 cannot stand (005BA790) gives the position instead. |

The first version of UnitActionPoint returned nothing. Ghidra shows the function as void, but every
path of the original leaves `out` in EAX, and the combat rewrites use the returned pointer (slot 53
is called as `Vector3* (Vector3* out)`). CUnitServer::ApproachTarget read through the leftover EAX
and crashed (address 9) in the combat, sound and units scenarios. It now returns `out`, and the
check compares both returns.

# Client path placement

Code: `src/game/unit_client_path.cpp` (new); verify wrapper: `tests/verify/verify_unit_client_path.cpp`.

| Address | Function | Verify |
| --- | --- | --- |
| 00535850 | ClientPlaceOnPath (RET 8; CUnitClientSpecific, from slot 41 with (+0x34, 0) and from 00535B30 with the tick's time and rate) | differential: the original first; orientation and snap byte put back; ours; position, orientation and +0x178 within 8 ULPs, snap byte exact |

Steps:
1. Skips units with flag 0x4000000.
2. Samples the controller's motion order (+0x38) with the unit's motion (+0x27C, 005C7430) at
   `time`. The position goes in as half-cells (x2) and comes back out (x0.5), set on the ground
   (00530280) plus +0x320, and the world's slot 41 moves the unit there.
3. A set +0x76 byte is cleared and snaps the facing once (00401F00 with flag 0). With unit flag
   0x10000000 the facing is set at once (00401F00 with 1). Otherwise, for `rate` > 0, the heading
   blends: current = atan2(-axis.y, -axis.x) of the orientation's facing axis (0040F560), target =
   atan2(direction). The smaller is raised by the full turn at 007AF330 until they are within
   3.1416. heading = (1 - w) target + w current with w = exp(-0.3 rate), then cos / sin go to
   00402220.
4. The animation speed factor +0x178 depends on the figure's current animation (+0x98, its +0x40):
   - none: 1;
   - an animation of length 0: 005327D0;
   - otherwise: |direction| x speed x 15 / (2 x frames x length), where frames comes from the
     animation entry's start / end / span and the unit's +0xCC.

atan2, exp, cos and sin are the C++ library's (rule 7). The original uses FPATAN / F2XM1 / FSCALE
/ FCOS / FSIN at the game's 24-bit precision.

Quaternion sign flip (ticket X3). The ui_hud run had one mismatch in 6329 calls, "q0 -0.707107 vs
0.707107" (a quaternion and its negative are the same rotation). 00402220 builds the orientation
from atan2(direction) + pi/2, so the quaternion jumps to its negative when the heading is exactly
+-pi (direction y flips sign around a heading of pi), and any one-ULP difference in the heading
there shows. The cause was in ours, not in the maths library: the original stores `current` and
`target` as floats (FSTP [ebp-8] / [ebp-4]) before the loop and the blend, but under /fp:fast MSVC
compiled `static_cast<float>(std::atan2(..))` straight into FPATAN and kept both angles unrounded
on the FPU stack, so the blend (1 - w) target + w current was fed 64-bit angles instead of floats.
That moved the heading by one float ULP in about 10% of calls (invisible inside the 8-ULP check)
and, near pi, flipped the sign of sin(heading) and with it the quaternion. Both angles now go
through a volatile float store (`Store`), like the original's FSTP. Checked offline with the
original's whole sequence (FPATAN, the wrap loop, F2XM1 / FSCALE blend, FCOS / FSIN) next to ours
at 24-bit precision on 3 million random directions plus 3 million within a few ULPs of +-pi: before
the fix the heading differed in 10% of calls and sin(heading) flipped sign in 1.4% of the edge
cases; after it both counts are 0. The verify wrapper is unchanged (still strict on q0..q3).

# Levers

Code: `src/game/unit_lever.cpp`, `src/game/lever.h` (new); verify wrappers:
`tests/verify/verify_unit_lever.cpp`. No shared files changed.

| Address | Function | Verify |
| --- | --- | --- |
| 0052B7A0 | Lever::SetState (RET 8; the script's lever commands) | ours only: state in range, shown position = state / (states - 1), 1 / duration stored for a timed move |
| 0052BBE0 | Lever::UseSpots (slot 44; seventh round below) | output only, against the original |

Fields: +0xA4 model name, +0xC8 shown position (0..1), +0x134 radius, +0x238 state, +0x23C state
count, +0x240 cycling, +0x250 / +0x254 start / end position, +0x258 end tick, +0x25C 1 / duration,
+0x261 the AI map flag (copied to the map's +0x172 while it moves).

SetState: a negative (or NaN) duration takes the model's default from the map 0079C1AC (operator[]
00464750 by the model name; the record's +0x1C as an unsigned number) or 10. A negative state is
the next one: it wraps when cycling (signed modulo), otherwise it stops at the last. Other states
are clamped to [0, states - 1]. Then, when the world is live (slot 26), the following happens:
1. The units within radius + 0.5 come off the sector map, and so does the lever; the AI map is refreshed.
2. The position is state / (states - 1), or 0 with one state. Plain C++ (rule 7).
3. A move with duration > 0 is timed: start at the old position, end tick (tick + duration) + 1,
   1 / duration. Otherwise it happens at once.
4. The figure is animated (005179E0), MarkChanged(5), the lever goes back on the map.
5. Every lifted unit that cannot stay in its cell is moved to a free cell
   (FindFreeCellNear) and its controller resynced (00554450) before it goes back.

# Fifth round (U3): database records, the packed weapon skill, armour removal, magic traps

Code: `src/game/unit_db.h/.cpp` (new), `src/game/unit_params_skill.cpp` (new),
`src/game/magic_trap.h/.cpp` (new), `src/game/unit_client_models.cpp` (RemoveArmourModel added).
Verify: `tests/verify/verify_unit_db.cpp`, `verify_unit_params_skill.cpp`, `verify_magic_trap.cpp`
(new), `verify_unit_client_models.cpp` (one wrapper added). Shared headers, additive only: `unit.h`
(UnitParams::WeaponSkillBonus), `unit_client_models.h` (RemoveArmourModel).

Picked as the largest live game-logic functions of the range not yet rewritten. Skipped on the way:
00534B60 / 00537500 / 00534760 (excluded by the ticket), 00510B80 (map objects' spell effect
particles: effect-adjacent like 00534B60), 005341A0 (CUnitClientSpecific slot 6, the client tick: it
queues speech through 005380D0 / 006B8250 and checks sound handles, audio-adjacent), 0053A8E0
(counts each model's .wav files), 005179E0 / 00516DA0 / 0051C3D0 / 005370F0 (figure loading and
bounds, wound textures: renderer side), 005114D0 (spell effect sounds), 0051FF40 (saves perks.pdb,
no callers), 0053F530 / 0053FCC0 / 0053F950 / 00520FA0 / 00521220 / 0053E2A0 (STLport vector
insert / resize instantiations). Next candidates: 0052DA60 (the NPC record's serialiser, same
pattern), 00538960 (the client's GetAttackParams).

| Address | Function | Verify |
| --- | --- | --- |
| 00527850 | UnitParams::WeaponSkillBonus (RET 8: type, weapon prototype record or null) | pure: original and ours, doubles within a few ULPs |
| 0052D740 | UnitPrototypeRecord::Serialize (RET 4: archive) | loading: ours into the record, its fields taken, then the original reads the same record again; bytes (CString / vector slots left out) and a digest of every string compared. Writing: ours only |
| 0052DC70 | UnitModelRecord::Serialize (RET 4) | the same |
| 005385A0 | CUnitClientSpecific::RemoveArmourModel (slot 27, RET 4: armour slot; AL) | ours only: an invalid slot / id gives false and no slot changes; a valid one empties exactly that slot, false only for armour slots 0-2 |
| 0052A680 | MagicTrap::UpdateVictims (no arguments) | ours only: the list of units inside well formed (links, tail, count), no unit twice; when not armed every entry alive and inside a circle |

All five renamed in Ghidra, with 00538960 CUnitClientSpecific::GetAttackParams, 0053A200
UnitDatabase::Load, 0052AC20 MagicTrap::Update, 0052A920 MagicTrap::FindVictims, 0052A4B0
MagicTrap::CastAtUnits. None of them or their callees is on the Rebirth list.

## Database records

`units.udb` (in the database resource) is loaded once at startup by 0053A200 (UnitDatabase::Load)
through the tagged-chunk archive: chunk 1 the skill levels (0x44 records, 0053E1B0, table 007AF3B8),
2 the models (0x168, 0053DF50 -> 0052DC70, table 007AF398), 3 the prototypes (0x120, 0053DC10 ->
0052D740, 007AF358), 4 the NPCs (0x60, 0053DD00 -> 0052DA60, 007AF378); each record is a chunk 1
picked by its occurrence. Afterwards each name index (hash_map) is filled, a prototype's +0x08
becomes the index of the model named by +0x04, and each model slot's +0x04 the index of the skill
level record named by its +0x00. The serialisers call the archive's field / CString field / enter /
leave (004092B0 / 004093A0 / 004098D0 / 00409980, rewritten in chunk_archive.cpp, called through
their addresses) in the original's order (the order matters when writing). The record layouts
(unit_db.h) name what the statistics builders read; the rest are `unknown_XX` with their field ids.
Bytes 0x38..0x47 of a prototype and 0x128..0x137 / 0x144..0x14F of a model are not serialised.

Every scenario reaches both serialisers at startup (several hundred records); the writing direction
is not used by the game.

## Weapon skill on the packed parameters (00527850)

The client's GetAttackParams (CUnitClientSpecific slot 50, 00538960) computes the attack figures of
party units for the interface (the unit panel, the statistics page) from the packed parameters
(CUnit + 0x330) and the weapon in hand; for text-key units holding a weapon it adds this function's
result to two weapon stats. It is UnitStats::WeaponSkillBonus (00526C70) on the packed bytes: types
0-4 use skill 0 and the weapon prototype's +0x7B, 5-6 skill 1 and +0x7C, each plus the stat named by
the skill's first letter ('s' +0x0C, 'd' +0x0D, 'i' +0x0E) minus 25 and the level value of skill
`type` (the record at type * 3 - 1 + level; the unsigned +0x34 loaded as a qword). Type 8 uses skill
0 and +0x7B without a level value; other types give 0. Types 0-4 keep the level value on the FPU,
5-6 store it to a float first; the other terms are small integers (exact). Plain C++ (rule 7).
Reached only when the interface shows a party unit holding a weapon; no scenario is known to do
that, the verify count shows it.

## Armour removal (005385A0)

The counterpart of WearArmourModel (slot 26): armour slot `slot` (0 helmet -> part row 2, 1 body ->
row 0, 2 trousers -> row 1; slots 3-6 have no parts) is emptied, then for each part name of the row
(the hair as "hr.<hair>", skipped for a prototype without hair) the figure's parts of that name
(00516B50, an aligned array freed with the block at data[-1]) are removed and the plain part added
from the unit's model (00517240 with the unit's flags_ & 0x80), except when there is exactly one and
it is already the plain part. Unlike WearArmourModel it removes before adding. No scenario is known
to take armour off a party unit (Rebirth's GiveUnitArmor refuses text-key units); the verify count
shows whether one does.

## Magic traps

Class id 0x3B (0x290 bytes, vtable 0073E63C, constructor 00529DD0). Its update (slot 6, 0052AC20)
runs CMapObject::Update, then, when the world is live and the trap has a spell (+0x23C): with a
period (+0x284) below 1, UpdateVictims; otherwise it counts +0x288 down and casts at its points when
it runs out (0052AB30). UpdateVictims:
1. Each unit in the inside list (+0x268, an MFC CList of counted references) is kept only if it is
   inside a circle of the area (+0x244 / +0x248, {x, y, radius}: r * r > dx * dx + dy * dy on x and
   y; NaN is outside) and alive (CUnit slot 55 returns 0); the first circle that holds it decides.
   Dropped nodes are unlinked inline and freed through 0052CE20 (which releases the reference).
2. An armed trap (flags_ 0x80000, the script's ActivateTrap) asks 0052A920 (fastcall: the map
   object context in ECX, the area in EDX, then the side mask +0x238 and the output list) for the
   living units of its sides in the circles. Each one not yet inside is appended (0052CDB0, then
   the counted assignment 0052A8F0) and put on a list of newcomers; when there are newcomers the
   spell is cast (0052A4B0: at the trap's points, or at the newcomers, or with +0x28C at the one
   last found close to the trap). Both stack lists are MFC CPtrLists (vtable 0073D454) freed with
   0052CA10.

No magic trap is known on zone1; the verify count shows whether a scenario reaches one.

## Forwarders added (fifth round; the standalone build must replace these)

- 0052DF40 (a vector<CString> as a chunk), 0052D6A0 (four dwords), 0052D6F0 (a model slot); the
  archive primitives 004092B0 / 004093A0 / 004098D0 / 00409980 (rewritten, through their addresses).
- 00516B50 (a 3D object's figure parts by name); FindModel 005AA5F0, AddPart 00517240 and RemovePart
  005167E0 were already forwarders of unit_client_models.cpp.
- The trap's: 00455940 (ManagerContext, rewritten), 0052A920, 0052A4B0, 0052CDB0, 0052CE20,
  0052A8F0, 0052CEF0, 0052CA10.

## Original bugs and oddities kept (fifth round)

- RemoveArmourModel empties the slot before looking up the figure, so an unknown figure returns false
  with the slot already empty; the hair lookup reads the prototype record without a range check.
- UnitParams::WeaponSkillBonus reads the level byte at +0xE3 + type for any type below 5, negative
  ones too, and the skill table's first / second record without a check.
- UpdateVictims tests only x and y against the circles, while 0052A920 finds the newcomers through
  the sector map.

## Risks (fifth round)

- The serialisers run for every record at startup: a mistake shows at once as mismatches, and the
  record keeps the original's reading either way. Their verify relies on loading being repeatable
  (fields found by id, the archive left as it was; 0052DF40 empties and refills the vectors).
- RemoveArmourModel and UpdateVictims are checked for consistency only and may not be reached by
  any scenario; UnitParams::WeaponSkillBonus only when the interface shows a party unit holding a
  weapon.

# Sixth round (U4): NPC records, the client's attack figures, skill prices, trap victims, starting equipment

Code: `src/game/unit_db.h/.cpp` (UnitNpcRecord added; the prototype record's +0x100..+0x11C named),
`src/game/unit_client_attack.cpp`, `src/game/unit_skill_price.cpp`, `src/game/unit_equipment.cpp`
(new), `src/game/magic_trap.h/.cpp` (FindTrapVictims added). Verify: `verify_unit_db.cpp` (one
wrapper added), `verify_unit_client_attack.cpp`, `verify_unit_skill_price.cpp`,
`verify_unit_equipment.cpp` (new), `verify_magic_trap.cpp` (one wrapper added). Shared headers,
additive only: `unit.h` (UnitStats::SkillPrice), `unit_client_models.h` (ClientGetAttackParams),
`unit_create.h` (CMapObjectManager::GiveStartingEquipment). `config/library.csv`: 006E7F7E is
`floor` (control word 0x173F, round down), 006E8B1F `ceil` (0x1B3F); both were unidentified.

After the two the ticket names, the largest live game-logic functions of the range not yet
rewritten were 00528050 (544 bytes), 0052A920 (519), 0052BBE0 (490) and 005141F0 (488). Skipped on
the way: 005179E0 / 00516DA0 / 0051C3D0 / 005370F0 / 00518100 / 00517240 / 00516B50 (figure
loading, bounds, parts, wound textures), 00536EA0 (the wound levels 005370F0 turns into textures),
005190C0 (picks a random animation of a figure), 0050C230 and the rest of 00500000-0050E000
(animation nodes, rendering), 005112A0 (effect sounds), 0053A200 / 00520220 (Rebirth-patched),
0053E730 / 0053E520 / 0053E9B0 / 0053B130 / 00521630 (STLport vector resize / destroy / insert
instantiations), the ticket's exclusions, and 0052BBE0. That one is a lever slot (vtable entry
0073E7F0) that builds the points around the lever from a name-keyed shape table (0052CB40) and the
facing's axes; it is the next candidate. 005141F0 was taken instead: it is two bytes smaller and
runs at every zone load.

| Address | Function | Verify |
| --- | --- | --- |
| 0052DA60 | UnitNpcRecord::Serialize (RET 4) | as the other serialisers: loading, ours then the original into the same record; bytes (CString / vector slots left out) and a digest of the strings. Writing: ours only. Startup, every scenario |
| 00538960 | ClientGetAttackParams (CUnitClientSpecific slot 50, RET 4; a free function) | output only: the original into a copy of the caller's record, ours into it; integers exact, floats within 4 ULPs; the record keeps the original's figures. Reached when the interface shows a party unit's figures (the HUD info panel for the unit under the cursor, the hero panels) |
| 005285D0 | ScaleDamageByEffects again (EI_REPLACE of the 00523D30 function) | pure, CheckNear. From 00538960 only |
| 00528050 | UnitStats::SkillPrice (RET 4) | pure, the ints compared exactly (see below). From the interface's skill pages (005F1590, 00625880) |
| 0052A920 | FindTrapVictims (fastcall, RET 8) | output only: the original into an empty scratch CPtrList, ours into the caller's; same units in the same order. From an armed trap's UpdateVictims |
| 005141F0 | CMapObjectManager::GiveStartingEquipment (RET 8) | ours only (makes items): result 1 exactly with a prototype; spell slots min(8, before + names); type 0x32: weapons min(4, before + named weapons), armour not fewer, +0x270 a filled slot. Every unit added from a zone file (newgame) |

Renamed in Ghidra: 0052DA60, 00528050, 005285D0, 005141F0, 006E7F7E floor, 006E8B1F ceil.

## The NPC records (0052DA60)

0x60 bytes (unit_db.h): +0x00 the name (the prototype it belongs to; field 0), +0x04 (1), +0x08
experience (2), +0x0C three stats (3-5), +0x18 three attribute bytes (6), +0x1B eight skill bytes
(7), +0x24 the skill level names it learns (8, through 0052DF40), three more vector<CString>s at
+0x30 / +0x3C / +0x48 (9-11), +0x54 / +0x58 (12, 13), +0x5C a byte (14). Fields 9-11 inline the
vector code of 0052DF40, emptying the vector with 0052E010 (vector<CString>::clear) instead of
0051F970's erase: the same result. Ours calls the same pieces (00409A60 count, 0052E010,
0051F5B0 resize) and sets the chunk occurrence before each string as the original does.

## The client's attack figures (00538960)

The client-side counterpart of CUnitServer::GetAttackParams (combat.cpp), from the packed
parameters (CUnit + 0x330) and the controller's weapon ids (+0xBC):
- A text-key unit with a weapon in hand (weaponIds[activeWeapon], slot below 4, id not -1):
  - damage min / max, type, damage type and speed come from the weapon functions of item.cpp
    (the speed +0x08 is 006A2BD0, the weapon's attack times its material's);
  - attack = _ftol(weapon +0x84 + (float) UnitParams::WeaponSkillBonus(type, weapon record));
  - defence is the same with +0x88.
- Otherwise:
  - damage = the parameters' damage byte, max = damage + spread bytes;
  - attack / defence / speed are the combat bytes 1 / 3 / 4;
  - with a weapon, its type and damage type; else type 8 and the first of the prototype model's
    seven damage weights (+0x50) that is not zero (NaN counts as zero; none leaves the field
    alone).
- Then, for both:
  - bows and crossbows (types 5, 6) get defence 0;
  - the damage max, then min, is scaled by the unit's strengthen / weaken effects (005285D0, at
    least 1);
  - the attack spread is combat byte 2;
  - 0052E0D0 scales attack and defence by the difficulty unless the unit has a text key.

005285D0 is a byte-for-byte copy of 00523D30 (ScaleDamageByEffects): thiscall on the parameters,
which it never reads, RET 8. It is registered with the same function. WeaponSkillBonus and
005285D0 are called through their addresses, so their wrappers count these calls (00527850 has no
other caller).

## Skill prices (00528050)

UnitStats::SkillPrice(record) is the experience a skill level costs, shown by the interface's
skill pages:
1. index = 0051FF00(record) (its position among the level records 007AF274). The level's worth is
   1.61 ^ (record +0x30 / 007C5480) * 007C547C (RaiseSkill's curve). When index % 3 > 0 the cost
   is _ftol(worth - the worth of the record before it: the level below, read unchecked), else
   _ftol(worth - the worth of 0). It is stored to a float.
2. price = 007C5484 ^ (the byte sum of the 0x24 skill level bytes +0x6D3..+0x6F6) * cost.
3. p = 10 ^ floor(log10(price)) (a float), r = price / p.
   - r >= 4: the result is floor(r + 0.5) * p, truncated.
   - r < 4 (or NaN, a price of 0): a p of 10 or less gives ceil(price). A larger p is first
     multiplied by 0.1f, then the result is as above.
   So the price keeps one significant digit, or two when the first is 1-3; small prices are
   rounded up.

Plain C++ (rule 7): std::pow / log10 / floor / ceil. MSVC turns log10 into FLDLG2; FYL2X and
pow(10, n) into its _CIpow. The original's 10 ^ n is exact for |n| <= 10 even at 24-bit
precision: 2 ^ frac = 10 ^ n / 2 ^ int fits 24 bits, and the rounding error of frac stays below
half an ULP. So p matches. The remaining risk is a worth difference that lands within an ULP or
so of a whole number before it is truncated: there the original's 24-bit _CIpow and ours may round
to different sides. That is rare; the wrapper reports it.

## Trap victims (0052A920)

FindTrapVictims(context, area, sides, out):
1. Empties `out` (CPtrList RemoveAll, inlined).
2. For each circle of the trap's area, asks the world's sector map (context + 0x294, 005C0BD0,
   which empties its list first) for the units within the radius of {x, y, 0}, into a CPtrList of
   its own (vtable 0073D454, blocks of 10).
3. Appends to `out` (AddTail, inlined) each living one (slot 55 returns 0) whose side bit is in
   `sides` (`1 << side`, the shift count taken mod 32 as SHL by CL).
4. Empties its own list.

mfc::CTypedPtrList's AddTail / RemoveAll are the same MFC code. UpdateVictims still calls it
through its address.

## Starting equipment (005141F0)

After ApplyPrototype (00513B60), the world's zone loader (005927C0, record kind 5) calls
GiveStartingEquipment on the map object manager with the unit and its prototype name:
1. The world (+0xAC of the manager) gives the spell owner (slot 22, asked first) and the item
   manager (slot 18).
2. The prototype record comes from 005144D0 (which inserts unknown names). Without one:
   FatalError "Can't find prototype" and 0.
3. A unit of type 0x32 gets:
   - each armour name of +0x10C, made (006AC970; Rebirth's FixCreateItem runs) and equipped
     (EquipArmour);
   - the weapon +0x118 unless it is "none" (007846F0, _mbscmp) or empty (AddWeapon; the slot goes
     to +0x270 unless it is -1);
   - the second weapon +0x11C (AddWeapon).
4. Every unit gets each spell name of +0x100 (006AD1E0 with the spell owner, then AddSpell).
5. A piece that cannot be made gets FatalError "Can't create armor / weapon / spell". Returns 1.

## Forwarders added (sixth round; the standalone build must replace these)

- Not rewritten: 0052E010 (vector<CString>::clear), 0051F5B0 (vector<CString>::resize), 0052E0D0
  (attack / defence times the difficulty), 0051FF00 (a level record's index), 006AD1E0 (the item
  manager's spell by name).
- Rewritten, called through their addresses: 00409A60 CountChunks, 00527850
  UnitParams::WeaponSkillBonus, 005285D0 (this round), 0052A920 (this round, from UpdateVictims),
  005C0BD0 the sector map query, 005144D0 the prototype lookup, 006AC970 CreateItem (Rebirth),
  00480080 FatalError (Rebirth), CUnit 00530730 / 00531070 / 00531820.
- Globals by address: 007C547C / 007C5480 / 007C5484 (worth scale, divisor, price growth), the
  weapon table 007C86F8, the skill tables 007AF268 / 007AF274, the prototype table 007AF358, the
  texts 007846F0 and 0078F2B4..0078F330.

## Original bugs and oddities kept (sixth round)

- GiveStartingEquipment adds a weapon that could not be made: AddWeapon(null) returns 0, and the
  first weapon's slot +0x270 becomes 0.
- A unit in two of a trap's circles is found twice.
- The client's attack figures leave the damage type alone for a unit without a weapon whose model
  weighs no damage type.

## Risks (sixth round)

- The NPC serialiser runs for every NPC record at startup and GiveStartingEquipment for every unit
  of a zone: a mistake shows at once (the serialiser differentially; the equipment by its counts).
- SkillPrice may show a rare off-by-one at a truncation edge (rule 7).
- ClientGetAttackParams, SkillPrice and FindTrapVictims may not be reached by any scenario (the
  verify counts show it).
- 0052DA60's verify relies on loading being repeatable (fields found by id; the vectors emptied
  and refilled), as for the other two serialisers.

# Seventh round (U5): lever use spots, the client's weapon model, trap casting, size classes

Code: `src/game/unit_lever.cpp` / `lever.h` (UseSpots, LeverSpots), `src/game/unit_client_models.cpp`
(ChangeWeaponModel; its AddPart forwarder now returns the part), `src/game/magic_trap.cpp`
(CastAtUnits; UpdateVictims' forwarder renamed CastAtNewcomers), `src/game/unit_size_class.cpp`
(new). Verify: `verify_unit_lever.cpp`, `verify_unit_client_models.cpp`, `verify_magic_trap.cpp`
(one wrapper added to each), `verify_unit_size_class.cpp` (new). Shared headers, additive only:
`unit.h` (CUnit::UpdateSizeClass), `unit_client_models.h` (ChangeWeaponModel), `magic_trap.h`
(CastAtUnits), `lever.h` (now includes maths.h).

After 0052BBE0, the largest live game-logic functions of the range not yet rewritten were taken in
size order, skipping: 005218E0 / 0051E5D0 (STLport vector insert-overflow instantiations),
0051F5B0 (vector<CString>::resize), 0053AD70 / 0053AF60 / 00539B20 / 00539E70 (destructors of the
unit database's vectors and tables, element destructors and the node pool inlined), 00537D80
(queues the client's speech, 005380D0 / 006B7C10: audio-adjacent like 005341A0), 00534590 (picks a
footstep .wav name), 00504860 / 0050AAF0 / 0050B510 / 0050D600 (00500000-0050E000, animation and
rendering), 00532860 (a point from the figure's part matrices, 005188B0), and the exclusions of the
earlier rounds. That gave 00535590 (480 bytes), 0052A4B0 (452) and 00530570 (442).

| Address | Function | Verify |
| --- | --- | --- |
| 0052BBE0 | Lever::UseSpots (lever slot 44, vtable 0073E740; RET 8: out, user radius) | output only: the original into an empty scratch CArray, ours into the caller's; same count, coordinates within 8 ULPs. From FindUseSpot (0054DF50) when a unit is ordered to use a lever; no lever on zone1 |
| 00535590 | CUnitClientSpecific::ChangeWeaponModel (slot 25, RET 4; returns 1) | ours only: result 1, active weapon = slot, +0x74 clear, weapon ids untouched, +0x75 and the arrow part's flag as below (005169A0 / 006A2B80 as the original's lookups). From the network state reader when +0x26C changes, and DropBrokenEquipment |
| 0052A4B0 | MagicTrap::CastAtUnits (RET 4: the newcomers' CPtrList) | ours only: countdown restarted unless nothing was cast; the +0x28C path leaves one unit that was in the list; otherwise the list is unchanged. From an armed trap's UpdateVictims; no magic trap on zone1 |
| 00530570 | CUnit::UpdateSizeClass (no arguments) | differential: the original first, its size class / radius / standing height taken and the old values put back, then ours; class exact, floats within 4 ULPs. Combat code (several forwarders), every movement mode change |

Renamed in Ghidra: 0052BBE0, 00535590, 00530570, 0052CB40 LeverTable::RecordByName, 0052CC50
SpotArray::SetAtGrow (0052A4B0 already had its name).

## Lever use spots (0052BBE0)

The lever table (0079C1A0, lever_table.cpp) keeps one 0x2C-byte record per lever model, read from
`Levers.ldb` by 00463030: +0 the name, +4 two spot offsets {along, across} (field 1, 0x10 bytes), +0x14
the spot count, +0x18 a distance factor, +0x1C the default duration of a state change (SetState),
+0x20..+0x28 three CStrings. 0052CB40 finds a record by the lever's model name (+0xA4) through the
map's operator[], so an unknown name adds a null record and gives none.
UseSpots empties the output CArray<Vector3> (RemoveAll inlined: operator delete, size and capacity
0), then for each offset: length = |offset|, the offset normalised when the length is above zero
(FCOM; TEST AH,0x41), distance = ((1 - phase) + phase x factor) x length + the user's radius, where
phase is the lever's +0xCC, and the spot = position + AxisX(orientation) x along x distance +
AxisY x across x distance, appended with SetAtGrow (0052CC50). The count is re-read every time and
not checked against the two offsets the record holds (a count above 2 would read the count and
the factor as offsets). The user's radius is its slot 60 (+0x328, set by UpdateSizeClass below).
Plain C++ (rule 7): FSQRT and the products at the game's 24-bit precision.

## The client's weapon model (00535590)

CUnitClientSpecific slot 25, the client side of a weapon change. The parts of the weapon in hand
(weaponIds[activeWeapon], none for a slot beyond 3 or -1) come from 006A0FF0 (WeaponModelParts,
item_parts.cpp, into a CStringArray it empties first) and are removed by name (005168F0: the first
part of that name in either of the figure's lists). Then the unit's active weapon (+0x26C) becomes
the new slot and the controller's byte +0x74 is cleared. A new weapon's parts are added from the
unit's model (005AA5F0 by the prototype name, looked up before anything is removed; 00517240 with
the unit's flags_ & 0x80), each new part taking up the figure's animation (00518820). For a bow
(006A2B80 type 5) the part "rh3_arrow00" (0078FAF8, found by 005169A0) gets flag 0x80 and +0x75 is
set; +0x75 is cleared first, but only when there is a new weapon. Last, the controller's slot 30.

## Trap casting (0052A4B0)

The cast's origin is the trap's position with z raised by the world's ground height at (x, y)
(world slot 28, ST0, kept as a double until the store). Spells without flag 0x10000000 (prototype
+0x3C) and a trap with points (+0x258 / +0x25C, {x, y} pairs) are cast at each point {x, y, 0},
the count re-read every time, with no target. Otherwise they are cast at each unit of the list
(target the unit, point its position). With +0x28C the list is first narrowed to the last unit
whose squared distance from the trap is below 0073E73C (1e30, so in practice the last unit; NaN
counts as below): the list is emptied (RemoveAll inlined, after a walk to its end that does
nothing) and that unit appended (AddTail through 0052CEF0; mfc::CTypedPtrList's AddTail is the
same code). If no unit qualifies, nothing is cast and the countdown is not restarted. Every other
path ends with +0x288 = +0x284 (copied as a dword). Casts go through the world server's 0058E850
(007AFF60) with caster 0 and the last argument 0.

## Size classes (00530570)

The first round skipped this because of the controller's slot 45. That slot is the server's
UpdateMoveFactors (005536B0, a recomputation from state) and empty (00533D10) on the client
controller and the abstract base, so the function can run twice and the wrapper compares it
differentially. With a server controller the size class is its CombatStats size class for the
movement mode, read unsigned (mode 0: +0x24C = sizeClass20, mode 1: +0x24E = sizeClass60, else
+0x248 = sizeClass). The extent is the larger of +0x144 / +0x148 (the unit's half extents,
UnitActionPoint's offsets), chosen by a bitwise select on FCOMP's C0 (+0x148 when +0x144 is below
it or either is NaN); the standing height is 0.9 x extent, the radius (+0x328) 0.9 x extent +
0.05, the height 0.9 x extent + 0.2 with a player, then at most 2 (the same select: 2 when below
it or NaN). Size class 0 with a prototype (+0x18) whose +0x24 is above 1.5 stands at 0.01. Other
controllers: size class 0 for movement kind 2 (on water), else 3, radius 0.75; the height is left
alone.

## Forwarders added (seventh round; the standalone build must replace these)

- Not rewritten: 0052CB40 (the lever table's record by name), 0052CC50 (CArray<Vector3>::SetAtGrow),
  005168F0 / 005169A0 / 00518820 (figure parts by name: remove, find; a new part's animation),
  0058E850 (the world server's spell cast; also forwarded by combat_actions.cpp), 00455950.
- Rewritten, called through their addresses: 006A0FF0 WeaponModelParts, 006A2B80 WeaponType,
  0052A4B0 (this round, from UpdateVictims). Called directly: Quaternion::AxisX / AxisY
  (core_maths.cpp). 00530570 was already called through its address by the combat files.

## Original bugs and oddities kept (seventh round)

- UseSpots trusts the record's spot count (two offsets are stored).
- UpdateSizeClass reads the prototype's +0x24 without a range check: a prototype index out of
  range reads address 0x24 (size class 0 only).
- CastAtUnits returns without restarting the countdown when +0x28C finds no unit, and the +0x28C
  path changes the caller's list (UpdateVictims then frees it).
- ChangeWeaponModel leaves +0x75 as it was when the new slot holds no weapon.

## Risks (seventh round)

- UpdateSizeClass runs often and is compared with the original at every call; a mismatch shows at
  once. Its verify runs the controller's slot 45 twice (idempotent, see above).
- UseSpots and CastAtUnits are not reached on zone1 (no levers or magic traps there); UseSpots is
  compared with the original when it is, CastAtUnits only checked for consistency.
- ChangeWeaponModel is checked for consistency only; a wrong part name or a missed part would not
  be caught by the wrapper (the figure would show it).


# Eighth round (U6): hit locations, stealing, the Create slots of units and torches

Code: `src/game/unit_hit_location.cpp`, `src/game/unit_steal.cpp`, `src/game/map_object_create.cpp`
(new). Verify: `verify_unit_hit_location.cpp`, `verify_unit_steal.cpp`, `verify_map_object_create.cpp`
(new). Shared headers, additive only: `unit.h` (UnitStats::PickLocation, CUnit::TakeRandomItem,
CUnit::CreateUnit), `map_object.h` (TorchCreate). Forwarder comments in combat_steps.cpp /
combat_actions.cpp name the rewrites (both still call them through their addresses).

The largest functions of the range left are mostly library-like: STLport vector insert / resize /
destroy instantiations of the unit database's records (0053F530, 0053FCC0, 0053F950, 0053E2A0,
00520FA0, 00521220, 00521C70 copy, 0051F790, 0051FB50, 00539CE0, 0053A050) and should go to
config/library.csv in a cleanup ticket. Also skipped: 0051FF40 (writes "perks.pdb", the skill
database saver: no reference in the exe, a dead-code candidate), 00519380 / 0051C250 (figure
animations and parts), 00511A00 (CMapObjectClientSpecific::SetLightMode, rendering), 00529B80
(class 0x3010's update: a world script CreateParticleSource when units come near; effect-adjacent)
and 0052C740 (Serialize of a list of counted pointers, vtable 0073E730: on loading it reads raw
pointer values from the archive and dereferences them, so it can only work with a count of 0; no
caller found besides the vtable). That gave 0052BFC0 (400 bytes), 00525B20 (400), 0052E400 (396)
and 00531400 (385).

| Address | Function | Verify |
| --- | --- | --- |
| 00525B20 | UnitStats::PickLocation (RET 4) | differential with the random generator (007A9D40, 0x814 bytes) saved and put back between the two; result exact, generator state equal. Every attack on a creature (BuildAttackSteps; combat scenarios) |
| 00531400 | CUnit::TakeRandomItem (no arguments) | differential: carried items, quick items, changedParts / flags_ / changeStamp and the generator saved and put back between the two; item and state equal. Stealing only; probably no scenario |
| 0052E400 | CUnit::CreateUnit (virtual slot 2, Create) | ours only (allocates): result 1, 14 network fields, buffer at least 14, mode idle, no carried items. Every unit made (newgame) |
| 0052BFC0 | TorchCreate (CTorchObject slot 2; fastcall) | ours only: result 1, 5 network fields, mode idle, +0x238 null. Zones with torches |

Renamed in Ghidra: 00525B20, 00531400, 0052E400 (CUnit__Create), 0052BFC0 (CTorchObject__Create).

## Hit locations (00525B20)

UnitStats::PickLocation(location), from BuildAttackSteps: the slot kind (+0x1F0 + 0xF4 x location,
UnitStatSlot::kind) 3 means working. The aimed location is kept while it works (read for any
location: the original only guards its jump table, unsigned `location <= 5`). Otherwise a jump
table tries neighbours: head 1, 2, 3; body 0, 2, 3, 4, 5; hand 2: 3, 1; hand 3: 2, 1; leg 4: 5, 1;
leg 5: 4, 1. Then the working slots are collected in order and one is picked with an inlined
IsaacRandom::Next (the same code as 004667F0; our Next is called) modulo their count; none gives 1.

## Stealing (00531400)

TakeRandomItem, from CUnitServer's actions (kind 1, combat_actions.cpp): with carried items
(+0x4A0, count +0x4A4) the first is taken, the rest moved down (CArray::RemoveAt(0) inlined,
memmove only when some remain), count - 1, MarkChanged(5). Otherwise the filled quick item
slots (+0x450) are collected (in the original into a CArray<int>, vtable 0073EBD4, grown with
00533740 and freed again: heap traffic only), one is picked with an inlined Next modulo the count,
emptied (no release; the caller takes the reference) and MarkChanged(5). Null and no change when
there is nothing.

## The Create slots (0052E400, 0052BFC0)

Virtual slot 2 of CEngineObject is Create. CMapObject's (0050E770, not rewritten) resets the map
object and gives its DeltaState (+0x1C4) 4 fields (0050FA30, CArray<DeltaField>::SetSize) before
DeltaState::Reset. CUnit's then calls CUnit::Reset (0052E5B0) and sets 14 fields; CTorchObject's
(class id 0x3A, vtable 0073E834, "CTorchObject") releases and clears its +0x238, sets 5 fields
and calls DeltaState::Reset (0047FB70) again. Both inline SetSize(n, -1); the inlined code is
exactly 0050FA30 (allocate and construct with 005101A0 when empty; construct the new tail or
destroy the surplus with 00510340 within the capacity; else grow by the grow step or an eighth of
the size, 4..1024, at least to n, copying the old records bytewise), so ours calls 0050FA30. The
C++ member is named CreateUnit: Create would override CEngineObject's virtual (rule 4).

## Forwarders added (eighth round; the standalone build must replace these)

- Not rewritten: 0050E770 (CMapObject::Create), 0050FA30 (CArray<DeltaField>::SetSize).
- Rewritten, called directly: IsaacRandom::Next (004667F0), CUnit::MarkChanged (0050E9F0),
  CUnit::Reset (0052E5B0), DeltaState::Reset (0047FB70). None is Rebirth-patched.

## Original bugs and oddities kept (eighth round)

- PickLocation reads a slot kind for any location before its range check.

## Risks (eighth round)

- PickLocation and CreateUnit run in the combat scenarios and newgame: a mistake shows at once
  (PickLocation differentially, including the random draw).
- TakeRandomItem (stealing) and TorchCreate (torch zones) may not be reached by any scenario; the
  verify counts show it. TorchCreate and CreateUnit are checked for consistency only.

## X2: unit database vector code classified

Library (config/library.csv): 0053F530 / 0053FCC0 / 0053F950 / 00520FA0 / 00521220 are
vector<T>::_M_fill_insert for 0x120 / 0x168 / 0x60 / 0x18 / 0x44-byte records, 0053E2A0 is
vector<0x120 record>::resize, 00521C70 is the 0x18-byte-record copy loop, 0051F790 is
vector<CString>::_M_insert_overflow. Not library: 0051FB50 (destructor of an object holding a
vector<0x44 record> and a vector<0x18 record>), 00539CE0 (vector<0x60 record> plus a hash table),
0053A050 (the database's destructor, ~10 member vectors): these are the game's own class
destructors and stay in the rewrite queue. 0051FF40 (perks.pdb saver) has no references (no
rel32 call/jmp, no dword in game.exe, addon.dll, EI_Plugin.asi; tools/find_dead_code.py keeps it
live only through the coincidental dword 0051FFE8 inside 006DE120), appended to dead_code.csv.
No address of the ticket is registered in src/ or tests/ (0051F790 is only an EI_ORIGINAL call
in ui_objectives.cpp).

# Ninth round (U7): the unit databases' destructors, magic trap set-up

Code: `src/game/unit_db_destroy.cpp` (new), `src/game/magic_trap.cpp` (SetUp). Verify:
`verify_unit_db_destroy.cpp` (new), `verify_magic_trap.cpp` (one wrapper added). Shared headers,
additive only: `magic_trap.h` (MagicTrap::SetUp, TrapPoints, MagicTrapRecord).
`config/library.csv`: 0052C740 (the trap victim list's CList::Serialize, the shape of 00405190),
00522160 / 005222F0 (uninitialized_copy / uninitialized_fill_n of the skill database's 0x18-byte
level records).

The three destructors the previous round left, then the largest function of the range not yet
rewritten after them that is not library: 0052A2C0 (362 bytes). Skipped on the way: 0052C740,
00522160, 005222F0 (library, above), 00519380 / 0051C250 / 00511A00 (the ticket's exclusions),
0052B070 / 0052ADC0 / 00529DD0 / 00529F90 (the magic trap's Save, Load, constructor and destructor:
next candidates in this family; Load reads registers the decompiler loses), 0053EE70 (the 0x168
model record's destructor, element code), 00535B30 (a client-side timer that drives figure
animation speeds, 0051CC20 / 0051CCD0: animation-adjacent).

| Address | Function | Verify |
| --- | --- | --- |
| 0053A050 | unitdb::DestroyUnitDatabase (thiscall, the database at 007AF358) | ours only: each table's name map count 0. At exit (atexit 0053A040), if hooks still run |
| 00539CE0 | unitdb::DestroyNpcTable (thiscall, 007AF378) | ours only: the name map count 0. When the database file changes (0057DEE0: database.res <-> databaseLMP.res, from the multiplayer hero screens; probably no scenario) |
| 0051FB50 | unitdb::DestroySkillDatabase (thiscall, 007AF268) | ours only, reported when it runs. At exit (atexit 0051FB40) |
| 0052A2C0 | MagicTrap::SetUp (RET 4: the zone record's MagicTrap part, record + 0x8C) | ours only (makes a spell, releases references): sides, position, circles, points, period bitwise; countdown 0; +0x28C 0/1; list of units inside empty, no blocks. The zone loader 005927C0; no magic trap on zone1 |

Renamed in Ghidra: 0053A050 UnitDatabase__Destroy, 00539CE0 UnitNpcTable__Destroy, 0051FB50
SkillDatabase__Destroy, 0052A2C0 MagicTrap__SetUp, 0052C740 VictimList__Serialize.

## The unit database's layout (007AF358)

Four 0x20-byte tables: prototypes (+0, 0x120-byte records), NPCs (+0x20, 0x60), models (+0x40,
0x168), skill levels (+0x60, 0x44). Each is a record vector (+0) and a hash_map<CString, int> from
name to index (+0xC: the empty functors, the bucket vector +0x10..+0x18, the count +0x1C, which is
also the node allocator's address). 0053A050 destroys them last to first, each with its own
template instantiations: hashtable::clear (0053C5E0 / 0053C430 / 0053C280 / 0053C0D0), the bucket
vector's destructor (0053BF00 / 0053B930 / 005150D0 / 0053BE70), each record's destructor with the
end read once (00540840(0) / 0053EE70 / 0053ED30 / 005407E0(0)), then the storage (0053BEB0(p, n)
with the vector's eos as `this` / 0053B7A0 / 0053B600 / 0053B460). 0057DEE0 (the database reload
when the file name changes between database.res, its initial name, and databaseLMP.res, which
the multiplayer hero screens ask for) runs the four tables'
destructors one by one (0057E1A0, 00539E70, 00539CE0, 00539B20), constructs the database again
(00539A10) and loads it (0053A200). 00539CE0 is the NPC table's with all of that inlined: the clear
(per node 0053D990(0) on the value and 0053CAA0(node, 1); every bucket emptied, the bucket count
read again each pass; count 0), the buckets freed (00404D70), each record's members in reverse
order (+0x48 / +0x3C: 005214B0 _Destroy and 00520EB0; +0x30 / +0x24: 0053F020; the name), the
storage freed with the byte count rounded down to whole records (signed IMUL division).

The skill database (007AF268): a vector of 0x18-byte level records (+0: two CStrings, a dword, a
vector<CString>) and one of 0x44-byte skill records (+0xC; CStrings at +0, +4, +0xC, +0x14).
0051FB50 destroys the skill records and frees them, then the level records; the second free
inlines the node pool's deallocate (identical to 00404D70, which ours calls through
stlp::deallocate).

## Magic trap set-up (0052A2C0)

From the zone loader for each MagicTrap record (the record's part at +0x8C: +0 the sides, +4 the
spell's text, +8 the position, +0x14 a CArray of circles, +0x28 a CArray of {x, y} points, +0x3C
the period, +0x40 a byte, cast at one unit only). The sides go to +0x238; the spell comes from the
map object context's slot 22 (the spell manager) and 00689030 CreateSpellFromText, its count raised
and the old spell released (slot 4); the position, circles (SetSize 0046EAD0 with grow -1) and
points (0052C8E0) are copied as dwords, the bounds read again each pass. The list of units inside
is emptied (RemoveAll inlined: each reference released and cleared, then count, free list, tail
and head 0 and the blocks freed with CPlex::FreeDataChain). +0x284 = the period, +0x288 = 0,
+0x28C = byte != 0.

## Forwarders added (ninth round; the standalone build must replace these)

- Library (config/library.csv): 0053C5E0 / 0053C430 / 0053C280 / 0053C0D0, 0053BF00 / 0053B930 /
  005150D0 / 0053BE70, 0053BEB0, 0053B7A0 / 0053B600 / 0053B460, 00540840 / 005407E0, 0053CAA0,
  00520EB0, 0046EAD0, 0052C8E0.
- Game code, not rewritten: 0053EE70 / 0053ED30 (model and NPC record destructors), 0053D990 (a
  name map value's destructor), 005214B0 (_Destroy of CStrings), 0053F020 (a vector<CString>'s
  destructor), 00689030 (CreateSpellFromText; also forwarded by item_constructor.cpp).
- Called directly: 00455940 ManagerContext (the existing forwarder in magic_trap.cpp), MFC
  CString::~CString and CPlex::FreeDataChain, stlp::deallocate (00404D70). None is Rebirth-patched.

## Original bugs and oddities kept (ninth round)

- 00539CE0 returns junk in EAX (declared void; its callers ignore EAX).
- VictimList::Serialize (0052C740, now library) writes raw pointers; no caller besides the vtable.

## Risks (ninth round)

- The destructors are checked for consistency only; at exit the verify report may already be
  written, so their counts may stay 0. A wrong free would show as heap corruption at exit or at a
  database reload.
- SetUp is not reached on zone1 (no magic traps); the wrapper compares every field against the
  record when it is.
