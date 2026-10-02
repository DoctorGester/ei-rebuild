# Combat and skills (00540000-00560000)

Code: `src/game/combat.h` (CUnitServer layout, UnitAction, AttackParams, the CombatStats overlay),
`src/game/combat.cpp`. Verify: `tests/verify/verify_combat.cpp`. Scenario:
`tests/scenarios/combat.txt`.

Shared files touched: none (no shared header was changed; combat.h only includes unit.h, spell.h,
and combat.cpp item.h, object_tables.h, object_diplomacy.h, random.h, world.h, lib/crt.h).

## What lives in this range

Most of the range is the server-side unit controller, **CUnitServer** (GetClassName at 005460C0,
class id 0x3E at 005460D0, vtable 0073EF38, 54 slots; its abstract base is vtable 0073EDBC, the one
units.md calls the controller; constructor 00546230). Every creature on the server has one at
CUnit + 0x240.

| Range | Contents |
|---|---|
| 00540000-00545100 | STLport vector instantiations (unit prototypes 0x120, records with CStrings), node pool |
| 00545100-00546480 | small containers (CPlex lists, vtables 0073EF2C / 0073F01C ...), CUnitServer slots 0-12 and constructor |
| 00546480-0054B220 | orders and movement: animation queue (00546B40), path following (00547890, 0054A150), move/stand/turn |
| 0054A900, 0054B390 | the per-tick order update (attack / cast / use state machine) |
| 0054B220-0054CA40 | weapon and spell queries, action distance, line of sight checks for orders (rewritten in part) |
| 0054CA40-0054F000 | order execution: attack, cast (0054E600), use items, target selection |
| 0054F000 | skill use: science / stealing / taming ("is using skill", "Item has been stolen from") |
| 0054F450-0054F8E0 | attack execution: missiles for bows (0054F450), the attack hit (rewritten), item spell triggers |
| 00550050-005518F0 | animation choice by weapon and movement (0x95C key), resistance displays |
| 00551AE0-00552D00 | taming (00551AE0), side changes, "claims that player is enemy" (00552D00) |
| 00552E00-005543B0 | hostility propagation, stats/state slots (19-29, 44-49), death (00553AC0), TakeHit (rewritten), heal (005543B0) |
| 00554450-00555A00 | order setters (move, follow, attack, use, cast; all reset action2 the same way) |
| 00555A00-00555D10 | attack parameters and the attack roll (rewritten), Load (00555D10) / Save (005561C0) |
| 00556760-0055C000 | slots 24/36/37, CArchive-serialised lists, more container instantiations |
| 0055C000-00560000 | world managers ("CPlayerManager", "CEffectManager", "CItemManager", maps), World::FindUnit (0055FB00) |

### CUnitServer fields (offsets from the object)

- `+0x1C` the unit; `+0x108` DamageInfo of the attack in progress (0054F580 fills it).
- `+0x164` state (5, 6 ...); `+0x168`/`+0x170` two {mode, flag4, wasMode3} movement records
  (00554450 fills them from CUnit +0x264/+0x268). `+0x174` (flag4 of the second) is also what the
  hit code reads as "cannot be backstabbed".
- `+0x190` and `+0x1B4` two UnitAction orders (0x24 bytes: kind, source, target (referenced),
  point, flag; reset by 0054B230 to kind 9). Kinds seen: 1 move (has a point), 3 attack, 4/5
  cast a spell / quick item, 6 use or a skill on a target (source 0x18A95), 9 none; 7/8 (00555700),
  10 (005557E0) and 11 (00555200) are set by other order setters. Sources: spell slot, or quick
  item slot | 0x80000000.
- `+0x1E0` a UnitStats copy (0x704 bytes, to +0x8E4; 00552F40 copies it in, 00553360 out). Its
  start, which unit.h leaves unnamed, is the CombatStats overlay: +0x04 experience (level), +0x30
  a scale (15 / x), +0x70 reach, +0x74 attack, +0x78 attack spread, +0x7C defence, +0x80 speed,
  +0x84 damage, +0x88 damage spread, +0x8C, +0x90 seven damage weights, +0xAC natural weapon type
  (0x10: none). Its dword +0x6F8 gets bit 0x10 when a hit lands (TakeHit).
- `+0x8E4` a short from the attack stats (00553450), `+0x8F8..+0x914` movement factors (005536B0),
  `+0x95C` animation key, `+0x97C` claimed-hostile player mask, `+0x980` attacker memory
  (005D7E90), `+0xA78` list of nearby units.

### The hit (DamageInfo, spell.h)

How a melee/natural attack works, from the rewritten functions:

1. PrepareAttack (0054F580) decides backstab (00530A60: the attacker is behind the target; not for
   ranged weapons, nor against a target whose controller has +0x174) and calls BuildAttackHit into
   +0x108.
2. BuildAttackHit (0054F630): damage weights (+0x00) from the weapon or the natural attack; +0x1C
   the weapon's +0x4C; **+0x20 the attack roll**: for units with a text-key id and a weapon,
   2 * ((rand() * 5 / 32767) % 5) + 2 - 6 (so -4..4), else the stats' attack times the difficulty
   factor (007C54AC[0079BEC0]; 1 for text-key units); **+0x24 damage**: the weapon's RollDamage,
   or damage + (random % (spread * 100 + 1)) * 0.01, then scaled by UnitStats::ScaleDamage
   (00523D30); +0x28 1; **+0x2C hit location** (0 head, 1 body, 2 left hand, 3 right hand, 4 left
   leg, 5 right leg, 6 all); +0x34 backstab multiplier (UnitStats 00525D60, else 1); +0x38 called
   shot. In games where the player's +0x108 byte is 2 or more the hit is reset (ResetDamageInfo).
3. The target's TakeHit (00553F40, slot 20) resolves the hit unless +0x20 is already +-FLT_MAX:
   ResolveHit (00553DF0) replaces +0x20 by FLT_MAX (hit) or -FLT_MAX (miss): elemental hits
   always hit; otherwise AttackRoll (00555BD0) = max(0, 2r - (spread + 1) + attack - distance
   penalty + speed ratio (not for a backstab)), r uniform in 0..round(spread), minus 25 for a
   called shot at the head or 10 at a limb (not the body); the hit lands unless the defender's
   defence (0 against a backstab) is greater.
4. TakeHit logs it to the console ("Unit 12 was attacked by 34 skill=<+-FLT_MAX> damage=<+0x24>
   destHL=Body", plus " backstab with power=" and " called shot" when they apply), refuses it for protected players, applies it through UnitStats::ApplyHit
   (005254B0), records the damage source at CUnit +0x350, recomputes the stats, and calls
   OnDamaged (00553AC0: death, alarms). A miss makes the victim hostile (CDiplomacy::OnAttack)
   and remembered.

AttackParams (slot 50, 00555A00) is what both rolls compare: weapon type, damage type, speed,
min/max damage (scaled), attack spread, attack and defence (truncated; text-key units use the
weapon's stats plus the weapon skill bonus 00526C70, others their stats scaled by difficulty in
0052E0D0; no defence with a bow or crossbow).

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 0054B620 | CUnitServer::AttackRange | pure (float bits) |
| 0054B650 | CUnitServer::WeaponType | pure |
| 0054B680 | CUnitServer::HasRangedWeapon | pure |
| 0054B6C0 | CUnitServer::AttacksFromRange | pure (AL) |
| 0054B760 | CUnitServer::ScaledItemValue | pure |
| 0054B9C0 | CUnitServer::ScaledAttackValue | pure |
| 0054B7C0 | CUnitServer::SpellCarrier | pure |
| 0054B820 | CUnitServer::IsScrollSource | pure (AL) |
| 0054B880 | CUnitServer::SpellOfSource | pure |
| 0054BF80 | CUnitServer::CanCast | pure (AL) |
| 0054C070 | CUnitServer::ActionDistance | pure (float bits) |
| 0054C800 | CUnitServer::CanReachTarget | pure (AL) |
| 00550050 | CUnitServer::WeaponTypeMask | pure |
| 005500A0 | CUnitServer::MoveModeMask | pure |
| 00556760 | CUnitServer::Level (slot 24) | pure |
| 0054BA30 | CUnitServer::GetDamageByType | output-only: original into a scratch copy of the 16 bytes |
| 0054BB20 | CUnitServer::GetSpellDamage | output-only, 16 bytes |
| 0054B520 | CUnitServer::GetMovePoint | output-only, 12 bytes and the result |
| 00555A00 | CUnitServer::GetAttackParams (slot 50) | output-only, 0x20 bytes (scratch pre-filled: the damage type is left alone when no weight is set) |
| 00553220 | CUnitServer::UpdateSpellBlockFlag (slot 49) | stateful: ours, the unit's flags_ saved and restored, original; flags compared |
| 00555BD0 | CUnitServer::AttackRoll | random: ours, generator state saved and restored, original; result bits and generator state compared |
| 00553DF0 | CUnitServer::ResolveHit | random + state: the hit (0x3C bytes) and the generator, as above |
| 0054F630 | CUnitServer::BuildAttackHit | random + state: the hit, ISAAC and rand()'s seed (_getptd + 0x14) |
| 0054F580 | CUnitServer::PrepareAttack | random + state: pendingHit (+0x108), ISAAC and rand()'s seed |
| 00553F40 | CUnitServer::TakeHit (slot 20) | ours only (applies damage, kills, logs, diplomacy): an unresolved hit comes back as +-FLT_MAX; a result of 1 has set bit 0x10 of stats +0x6F8 |

The random-number wrappers keep their snapshots on the stack: the originals call other wrapped
functions (AttackRoll inside ResolveHit, BuildAttackHit inside PrepareAttack, RollDamage of the
items area) that do the same, and each nested wrapper leaves the generator as one original run
would.

All were renamed in Ghidra, together with the forwarders below and 005460C0/005460D0.

Not verified offline: unlike units.md there is no scratch harness here. The compiled x87 code of
AttackRoll, ResolveHit, BuildAttackHit, ScaledAttackValue and Level was read back from the object
file (`dumpbin /disasm`) and checked against the original's operation order (operands of each
FADD/FSUB/FMUL/FDIV, float stores where the original stores, NaN behaviour of the compares). The
wrappers only run in game (tests/scenarios/combat.txt).

## Floating point

`combat.cpp` uses `#pragma float_control(precise, on)` like the unit area: values the original
keeps on the FPU stack are `double` locals, values it stores with FSTP are `float` locals. Specific
points:

- The original's branch-free `max(0, v)` (FCOMP, then SHL/SAR of the status word as a mask) picks v
  when 0 < v *or v is NaN* (`MaxZero`); the same trick picks the melee reach floor in
  ActionDistance.
- Zero tests written `TEST AH,0x40` skip on equal *or unordered* (`x < 0 || x > 0` in C++).
- Random draws: `random % (n + 1)` with n = FISTP of a float (round to nearest), then FILD of the
  zero-extended remainder (`Unsigned`, a volatile int64, as unit_stats.cpp does).
- `_ftol` (006E853C) is called through `GameFtol` (the modern conversion returns 0x80000000 for
  out-of-range values where the game's keeps the low 32 bits).
- Level uses FLDLN2/FYL2X in inline assembly, as the original.

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- Unit range: 00523D30 `UnitStats::ScaleDamage` (returns a float in ST0), 00526C70
  `UnitStats::WeaponSkillBonus`, 0052E0D0 `AttackParams::ScaleByDifficulty`, 00530A60
  `CUnit::IsBehind` (AL), 005254B0 `UnitStats::ApplyHit`, 00524560 `UnitStats::Recompute`.
- This range, not rewritten: 0054F8E0 `CUnitServer::TriggerItemSpells`, 00553AC0
  `CUnitServer::OnDamaged`.
- Elsewhere: 005911C0 `CWorldServer::AwardKill`, 005D7E90 attacker memory (CUnitServer + 0x980),
  the game's ostream inserters 006FC31D (const char*), 006FC9C6 (long), 006FC7FA (double),
  006FC0B0 (char) and 00434FE0 (manipulator), the log stream pointer 007AF3E8 (= the console
  ostream at 007C5810, whose streambuf 007C5868 the harness hooks), and in the verify file 006F00D1
  `_getptd`.
- Called directly (rewritten elsewhere, none on the Rebirth list): CUnit::GetWeapon / GetQuickItem
  / GetSpellObject / GetPrototype / GetPrototypeModel / HasEffect / MarkChanged, UnitStats::Period20
  / Value6E9Arg, CItem::GetPrototype / GetCategory, CItemWeapon::GetType / GetDamageTypes /
  RollDamage, WeaponDamageType / WeaponStat84 / WeaponStat88, CSpell::GetDamageType,
  IsTextKey, IsaacRandom::Next, gamecrt::rand, ResetDamageInfo / HasElementalDamage,
  CWorldServer::AddToUpdateMap, IsMultiplayerClient, CDiplomacy::OnAttack.
- Virtual calls through the objects' vtables: CUnitServer slot 50 (GetAttackParams), CUnit slots
  55 (dead, +0x274) and 60 (radius, +0x328), GetClassId (slot 12).
- 005301A0 (CUnit "has a player") is a one-line getter; inlined as `unit->player != nullptr`.

## Original bugs and oddities kept on purpose

- The melee reach floor for two party units is 0x3E4CCCCE (0.20000002), not 0.2.
- GetAttackParams leaves the damage type as it was when the natural attack has no non-zero weight.
- BuildAttackHit's weapon branch rolls the attack with the CRT's rand() (2 * (rand * 5 / 32767 % 5)
  + 2 - 6), the rest of the combat code with the ISAAC generator.
- TakeHit logs " skill=" followed by the resolved +0x20, which is always +-FLT_MAX by then.
- ResolveHit ignores the hit location of a called shot at the body (1) and penalises every other
  location, including "all" (6) and invalid ones, by 10.
- CanCast compares the mana only for units with a player (005301A0).
- IsScrollSource calls GetCategory up to three times (pure; kept).

## Skipped and why

- 0054B580 and 0055E990: patched by Rebirth (docs/rebirth-patched-functions.txt).
- 0054F000 (skill use: science, stealing, taming): builds a CTypedPtrList on the stack for the
  stolen item message (FUN_004621a0 nodes, CPlex), many callees with side effects (00531400 take a
  random item, 005942D0, 005935C0, 00551AE0 taming, 0066D6E0 messages); verification would be ours
  only. A good next candidate once the item lists exist. 00551AE0 (taming) likewise.
- 0054F450 (attack execution: bow missiles via the world's object factory, 0x5B), 0054F850 /
  0054F8E0 (item spells on hit: cast through 0058E850): stateful, create objects.
- 00553AC0 (death / alarm), 005538D0 ("has seen murder"), 00552D00/00552E00 (hostility claims with
  CString formatting): stateful with many callees; forwarded.
- 005543B0 (heal, logs " was healed"): small but needs UnitStats 00525900 and the same log
  forwarders; next candidate.
- 00552FB0 (slot 23, size category shorts), 005536B0 (slot 45, movement factors), 00553450 /
  00553500 (stat refresh): stateful but self-contained; candidates for snapshot verification.
- 0054BBC0, 0054BCB0, 0054C2F0, 0054C8B0, 0054C960, 0054CA40 (line of sight and target checks for
  orders; 0054C6B0 builds a path list): longer x87 code with AI map calls.
- The order setters (00554450-00555840), the per-tick order machine (0054A900, 0054B390,
  0054DD70 ...), path following, animation queue: large and stateful.
- Load/Save (00555D10 / 005561C0), container and STLport instantiations, CArchive lists
  (00556E10-00558700), and the world managers at 0055C000-00560000: not combat.
- 00551A40 / 00551A90 (resistance displays via 00530240, which logs through the DebugLog stub):
  not used in fights.

## Risks

- TakeHit is checked only for consistency; its log line goes through forwarded ostream code and
  must match the original character for character (the expects in combat.txt look for parts of it).
- The CombatStats overlay names bytes of UnitStats that unit.h leaves unnamed; if the units area
  names them later the two should be merged into unit.h.
- `+0x174` is read as a byte both by the hit code and (as the second movement record's flag) by
  005500A0; the naming is a guess, the behaviour is copied.
- The scenario's second enemy (GoblinF1) was chosen from database.res without running it; the
  starting hero may lose to it (see the comment in the scenario for alternatives).
