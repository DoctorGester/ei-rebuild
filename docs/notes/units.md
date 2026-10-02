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
