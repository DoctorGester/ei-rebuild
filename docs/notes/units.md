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

Kept from the original: the field 3 count is one byte read into a word; the companion's slot 13
and the stat slot get their bool / flag arguments as 0/1 and the byte (the original pushes
registers whose upper bytes are left over). The stat fractions and hit point ratio are plain C++
(rule 7).

# Levers

Code: `src/game/unit_lever.cpp`, `src/game/lever.h` (new); verify wrappers:
`tests/verify/verify_unit_lever.cpp`. No shared files changed.

| Address | Function | Verify |
| --- | --- | --- |
| 0052B7A0 | Lever::SetState (RET 8; the script's lever commands) | ours only: state in range, shown position = state / (states - 1), 1 / duration stored for a timed move |

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

