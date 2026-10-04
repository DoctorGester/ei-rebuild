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
  (0x10: none), +0x60 (2: no size classes), +0x68/+0x6C/+0x6E size classes, +0xBC a movement
  factor scale. Its dword +0x6F8 gets bit 0x10 when a hit lands (TakeHit).
- `+0x164` state (0 idle, 5 casting, ...), `+0x8E4` a short from the attack stats (00553450),
  `+0x8F8..+0x904` movement factors and `+0x914` speed class (005536B0),
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
   destHL=Body", plus " backstab with power=" and " called shot" when they apply), refuses it
   for protected players, applies it through UnitStats::ApplyHit (005254B0), records the damage source at CUnit +0x350, recomputes the stats, and calls
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
| 00552FB0 | CUnitServer::UpdateSizeClasses (slot 23) | stateful: ours, the 8 bytes at CombatStats +0x68 saved and restored, original; bytes compared |
| 005536B0 | CUnitServer::UpdateMoveFactors (slot 45) | stateful, the 0x20 bytes at +0x8F8 likewise; the original runs through a hand-written trampoline (below) |
| 00555BD0 | CUnitServer::AttackRoll | random: ours, generator state saved and restored, original; result bits and generator state compared |
| 00553DF0 | CUnitServer::ResolveHit | random + state: the hit (0x3C bytes) and the generator, as above |
| 0054F630 | CUnitServer::BuildAttackHit | random + state: the hit, ISAAC and rand()'s seed (_getptd + 0x14) |
| 0054F580 | CUnitServer::PrepareAttack | random + state: pendingHit (+0x108), ISAAC and rand()'s seed |
| 00553F40 | CUnitServer::TakeHit (slot 20) | ours only (applies damage, kills, logs, diplomacy): an unresolved hit comes back as +-FLT_MAX; a result of 1 has set bit 0x10 of stats +0x6F8 |

The random-number wrappers keep their snapshots on the stack: the originals call other wrapped
functions (AttackRoll inside ResolveHit, BuildAttackHit inside PrepareAttack, RollDamage of the
items area) that do the same, and each nested wrapper leaves the generator as one original run
would.

005536B0 begins with 4 bytes of PUSH/MOV and then a relative CALL, so no stolen-byte count works.
Its wrapper (`stolenBytes` 0) calls `OriginalUpdateMoveFactors`, a naked stub in
verify_combat.cpp that repeats PUSH ECX; PUSH ESI; MOV ESI,ECX, calls 00455940 through a register
and jumps to 005536B9 (past the hook's 5 bytes; the code there writes EDX before reading it).

All were renamed in Ghidra, together with the forwarders below and 005460C0/005460D0.

Not verified offline: unlike units.md there is no scratch harness here. The compiled x87 code of
AttackRoll, ResolveHit, BuildAttackHit, ScaledAttackValue, Level, UpdateSizeClasses and
UpdateMoveFactors was read back from the object
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
  `CUnit::IsBehind` (AL), 005254B0 `UnitStats::ApplyHit`, 00524560 `UnitStats::Recompute`,
  005304A0 unit load and 00530560 unit size (2 * +0x14C). Forwarders whose ST0 result the
  original keeps on the stack are declared `double` so the compiler does not round them.
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
- 00553450 / 00553500 (stat refresh), 005533B0 (slot 19, rescales a timer after a stats change):
  stateful but self-contained; candidates for snapshot verification.
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

## Second round: ends of orders, missiles, item spells, healing, death, skills, order setters

Code: `src/game/combat_actions.cpp` (FinishAction, DeliverAttack, the item spell triggers,
UseItemCharge, Heal, OnDamaged and its helpers, UseSkill, Idle, SetDead) and
`src/game/combat_orders.cpp` (FinishCast and the queued order's setters); `combat_internal.h`
holds what they share with combat.cpp (the game's ostream, ServerObjectOf / ServerStatsOf, a few
forwarders). Verify: `tests/verify/verify_combat_actions.cpp`, `tests/verify/verify_combat_orders.cpp`.
Scenario: `tests/scenarios/combat_more.txt`.

Shared files touched: none outside the combat files. combat.h gained fields (+0x20/+0x21,
+0x144 keepHitDirection, +0x160 hitDirection, +0x17C effectObject, +0x97C claimedHostile, +0x980
the brain as bytes, CombatStats +0xFC noise) and methods; combat.cpp lost its private copy of the
ostream declarations (now in combat_internal.h) and calls OnDamaged / TriggerItemSpells through
their game addresses.

This supersedes the corresponding entries of "Skipped and why" above (0054F000, 0054F450,
0054F850, 0054F8E0, 00553AC0, 005538D0, 00552E00, 005543B0 and part of the order setters).

### How an order ends (00549EC0)

The animation code (00549160) calls FinishAction(location, unused) at the order's moment. By the
current order's kind (+0x190): 3 attack: DeliverAttack(target, pendingHit); then, for a unit
target and a location below 6, when the target's body slot `location` (controller + 0x3D0 +
0xF4 * location, the slot's kind) is no longer 3 and the order still aims at it, the order aims at
6 (all). 4/5: FinishCast. 6: UseSkill(target). 10: the quick item of slot `source`, when of
category 2, is taken from the unit and applied by the world (0058ECE0, potions). Kinds 4, 5, 6
then clear the order (00545F40, +0x1C/+0x20 zeroed) and call Idle when the queued order is of
kind 9; kind 10 clears it inline and drops the queued order the same way.

DeliverAttack: with a bow or crossbow the world's object factory (world slot 14, its slot 23)
creates object 0x5B (a missile) at the unit's slot 48 position, aimed at the target's id
(0067AC70), with +0x154 = 1, +0x15C = the shooter's id and the 0x3C-byte hit copied to +0x160, and
registers it with AddTimedObject; otherwise the target's CUnit slot 31 forwards the hit to its
controller's TakeHit (slot 20) at once. Then the weapon's own spell (TriggerWeaponSpell) and, when
the hit's backstab multiplier is exactly 1.0, a noise of 2 x CombatStats +0xFC (005C98C0 kind
0x1A, the AI events at world + 0x2838).

Item spells: a spell carried by an item (+0x44) with targetFlags_ bit 0x10000 fires when the
item's charge covers the spell's cost (+0x40; NaN fires); UseItemCharge (00552C60) takes the cost
off the charge, tells the world (00594F30) and copies the new charge to the party's copy of the
item (player + 0x70, its slot 18 manager, FindObject by item +0x1C's +8; 00665BE0). The weapon's
spell is cast at the target's slot 53 aim point; the seven armour pieces' spells (00530830 =
armor[slot]) at the unit itself, on a hit (TakeHit passes 1) or, for spell prototype 0x18, when
not hit (OnDamaged passes 0).

### Damage and death (00553AC0)

OnDamaged(attacker, hitKind) after damage was applied: units whose type_ is outside 0x32..0x36
only get flags_ bit 1. A living unit: armour spells (onHit 0), the attacker turns hostile
(CDiplomacy::OnAttack) and is remembered (005D7E90), flags_ bit 0x80000 and PropagateHostility,
and the direction of the hit (HitDirection) goes to +0x160 unless +0x144 is set. A dead one:
the killer's party gets message 0x30 (or, when the killer has no player, a party victim's killer
says remark 9), WitnessMurder on both units' nearby lists (brain + 0xF4), effects cleared
(0050F5B0), SetDead(1) (sector map re-registration, the effect object at +0x17C removed and
released), "Unit 12 damaged to death", the death animation (an AnimationList filled by 00550A80
from the hit direction, copied and played by 00548DE0), the AI events' death notice (005C97B0),
and for a party unit the first other member's memory hears of it (005CBE90); flags_ bit
0x20000000 in a multiplayer client.

HitDirection(attacker, hitKind): 0 without an attacker or with a non-zero hit kind; else the
attacker's offset is projected on the unit's rotated Y axis (AxisY, negated: "front") and X axis
(kept unrounded on the FPU stack: "side"); |side| < |front| (or NaN) gives 0x800000 in front /
0x400000 behind, else 0xC00000 / 0x1000000 by the side's sign.

PropagateHostility: the nearby list (brain + 0xF8 head) is walked; for each allied unit (CDiplomacy
allied_[side] has its side's bit) closer than the brain's +0x38 radius (squared through the
global 007AF40C and compared as ints), the two units exchange hostility claims (00552D00 with the
masks the other has not claimed yet).

WitnessMurder(list, victim, murderer): each unit in `list` allied with this one gets flags_ bit
0x2000000 and a perception update (005CAA00); if its watched list (brain + 0x110) holds the victim,
the murderer is added to it (NewNode 0052CDB0, counted reference; "Unit 12 has seen murder and is
forced to see unit 34") unless already there, and the murderer turns hostile to it.

Heal (slot 21, 005543B0; the healing spell effect at 00681652 calls it): UnitStats 00525900 on the
server stats, Recompute, MarkChanged(0) and (4), "Unit 12 was healed".

### Skills (0054F000)

UseSkill(target) for a kind 6 order. Source 0x18A95 (talk): a unit target meets the player
(005942D0) and its dialog starts (005935C0, patched by Rebirth, called through its address), with a
noise of 1.5 x +0xFC. Sources 0 (science) and 1 (stealing) compare the unit's skill (00525E90 with
0) with the target's (0 for a non-unit) and log "Unit12 is using skill science his_skill = ...
target_skill = ...". Stealing fails (the victim turns hostile) unless the thief's skill is
greater; it then takes a random item from a target without a player (00531400; "Item has been
stolen from", given to the thief's player in a CPtrList through 00665470) or tries 005942D0;
message 0x22 on failure. Science looks up the order's +0x1C object in the world's item manager,
reads its record (slot 21) and raises its third int by the rounded skill, then asks the target
(slot 41); message 0x11 when refused. No script command reaches these; the player's orders do.

### Order setters (combat_orders.cpp)

All replace the queued order (+0x1B4) after resetting it (kind 9, target released, +0x18/+0x1C/+0x20
cleared; some resets go through 0052B030, which does the same): OrderTurn (005546B0, kind 2: point,
+0x1C = time), OrderMove (00554740, kind 1: point, source = walk flag, +0x20 = run, z replaced by
the fourth argument; a party unit with a real point either gets +0x1C = 1 or goes on to 00554830),
OrderAttack (00554B30, kind 3; location 6 unless the target's slot kind is 3; the reach test is
CanReachTarget inlined, message 0x0C when out of reach; then the planner 0054E600), OrderCastAt /
OrderCast (00554D00 / 00554F50, kinds 4 / 5: a scroll is cast at once and used up via 00595020; a
party unit's own spell needs the spell's +0x50 against UnitStats 00527EB0 of the spell's school,
message 0x13 otherwise or while flags_ has 0x10000000; the planner gets CanCast ? 3 : -1),
StopQueued (00555200, kind 11 unless the current order is 11) and Idle (00555180). FinishCast
(0054EE10): a charged carrier pays from its charge (or the cast is dropped: source -1), a party
unit pays mana Period20 x cost from its server object (+0x34, clamped to 0..+0x38 with the
original's branch-free selects), the world casts it at the target's aim point or the order's point,
noise unless the spell prototype's +0x10 is 1, source -1.

### Functions rewritten (second round)

| Address | Name | Verification |
|---|---|---|
| 005518F0 | CUnitServer::HitDirection | pure |
| 00549EC0 | CUnitServer::FinishAction | ours only: kinds 4/5/6/10 end with the order cleared (9) and no queued order of kind 9; an attack keeps its location or aims at 6; other kinds untouched |
| 0054F450 | CUnitServer::DeliverAttack | ours only: the weapon's spell is paid exactly when it can be cast (with a bow the missile may fail to spawn: unchanged accepted) |
| 0054F850 | CUnitServer::TriggerWeaponSpell | ours only: same charge check |
| 0054F8E0 | CUnitServer::TriggerItemSpells | ours only: each armour piece's charge as predicted from onHit, the spell's flags and cost |
| 00552C60 | CUnitServer::UseItemCharge | ours only: charge = float(charge - cost) bit for bit |
| 005543B0 | CUnitServer::Heal (slot 21) | ours only: returns 1, the hit points (server object +0x28) not lower for a non-negative amount |
| 00553AC0 | CUnitServer::OnDamaged | ours only: other types get flags_ bit 1; a dead unit ends with +0x274 = 1; a living one has bit 0x80000 clear and +0x160 = HitDirection unless +0x144 |
| 005538D0 | CUnitServer::WitnessMurder | ours only: every allied witness that watches the victim also watches the murderer |
| 00552E00 | CUnitServer::PropagateHostility | ours only: with bit 0x80000 it ends clear and the claims include the old hostile mask; else nothing changed |
| 0054F000 | CUnitServer::UseSkill | ours only: science and stealing leave the order alone (talk not checked) |
| 00555180 | CUnitServer::Idle | ours only: queued order untouched, or kind 0 with everything cleared |
| 00546720 | CUnitServer::SetDead | ours only: +0x274 as asked; a unit that just died has no effect object |
| 0054EE10 | CUnitServer::FinishCast | ours only: source unchanged or -1; a carrier's charge as predicted |
| 005546B0 | CUnitServer::OrderTurn | ours only: queued order exactly kind 2, point, time, no target |
| 00554740 | CUnitServer::OrderMove | ours only: kind 1 with the point, z, flags (not checked on the 00554830 path) |
| 00554B30 | CUnitServer::OrderAttack | ours only: kind 3 on the target, or dropped (9, no target), or untouched for no target / itself |
| 00554D00 | CUnitServer::OrderCastAt | ours only: kind 4 with the source, dropped, or untouched |
| 00554F50 | CUnitServer::OrderCast | ours only: kind 5 with the source and target, dropped, or untouched |
| 00555200 | CUnitServer::StopQueued | ours only: kind 11 cleared, or untouched when the current order is 11 |

The rewrites call each other through the game's addresses (`Call*` members, EI_ORIGINAL to the
hooked entry): most have no other caller, so a direct C++ call would hide them from the verify
wrappers. None of these addresses is on the Rebirth list; the patched callee 005935C0 is reached
through its address. All were renamed in Ghidra (with 0054E600 PlanOrder, 00554830 TurnTo,
00550A80 BuildDeathAnimation, 00548DE0 PlayAnimation, 00552D00 ClaimHostility, 00545F40
UnitAction::Clear).

The compiled x87 code of HitDirection, PropagateHostility and FinishCast was read back
(`dumpbin /disasm`): the operand order of the products and sums matches; FinishCast's
`-needed + mana` compiles to FSUBR (mana - needed), which is the same value.

### Forwarders added (second round)

- This range, not rewritten: 0054E600 PlanOrder, 00554830 TurnTo, 00552D00 ClaimHostility,
  00550A80 BuildDeathAnimation, 00548DE0 PlayAnimation (AnimationList by value; it destroys it),
  00557B60 / 00556870 AnimationList constructor / copy constructor, 00545F40 UnitAction::Clear,
  0052CDB0 the brain lists' NewNode.
- Unit range: 00525900 UnitStats heal, 00525E90 skill value, 00527EB0 spell school skill (both
  ST0, declared double), 00531400 take a random item, 0050F5B0 clear effects, 005AEA60 voice.
- World and players: 0058E850 cast spell, 0058ECE0 use quick item, 00594F30 item changed,
  005942D0 meet unit, 005935C0 start dialog (Rebirth), 00595020 use scroll, 0067AC70 missile
  target, 00665BE0 / 00665470 player item changed / receive items, 0066D6E0 party message,
  005C98C0 / 005C97B0 / 005CAA00 AI events (noise, death, memory update), 005CBE90 memory: party
  member died, 00551AE0 taming, 005C1010 / 005C0DF0 CSectorMap::Remove / Add (rewritten in
  sector_map.cpp; called through their addresses).
- Inlined trivial getters / helpers: 00530830 (armor[slot]), 005301A0 (has a player), 0052B030
  (release a counted reference and clear it), the party hash_map iteration (00455830 /
  00455890 / 004558B0, as object_diplomacy.cpp does).
- Called directly (rewritten elsewhere, not patched): Quaternion::AxisY, CBaseManager::FindObject,
  CWorldServer::AddTimedObject / RemoveTimedObject, CUnit::GetWeapon / GetQuickItem /
  RemoveQuickItem / GetSpellObject / GetPrototype / MarkChanged, CItem::GetCategory,
  CItemWeapon::GetType, CSpell::GetPrototype, UnitStats::Period20, CDiplomacy::OnAttack,
  IsMultiplayerClient, mfc::CTypedPtrList.

### Original bugs and oddities kept on purpose (second round)

- FinishAction's kind 10 and Idle reset the queued order to kind 9 and then to kind 0 (an inlined
  reset followed by a second one); the check before it (current order not 0) is always true in
  FinishAction.
- UseSkill returns for skill 2 (taming) before its switch, so the taming case (00551AE0) is dead.
  It dereferences the target without a null check outside the talk branch, and calls
  CDiplomacy::OnAttack with a null victim when the target is not a unit. Its log prints "Unit"
  without a space before the id.
- OrderMove stores its fourth argument over the point's z.
- OrderCast asks the target for its aim point (slot 53) and ignores the result.
- OrderCastAt checks the item's category for 5 or 8 and then for 8.
- WitnessMurder takes the diplomacy table from the unit's own manager and the one it changes from
  the controller's (the same world in practice).
- PropagateHostility compares the squared distance with the squared radius as ints (both are
  non-negative floats, so this orders them correctly except for NaN).
- OnDamaged's death branch asks for ManagerContext and ignores it.

### Skipped and why (second round)

- 0054B580 and 0055E990: patched by Rebirth.
- 00554450 (stop: resets both orders, movement records, AI map flags, animation; many callees),
  00555270-00555840 (other order setters: use, pick up, kinds 7/8/10), 00554830 TurnTo (path
  planning), 0054E600 PlanOrder (1.2 KB, path search), 0054DD70 / 0054A900 / 0054B390 (the
  per-tick order machine), 00549160 (the animation step that calls FinishAction): large and
  stateful; next candidates are the remaining setters, which follow the patterns above.
- 00551AE0 (taming), 00552D00 (hostility claims with CString formatting), 00552670 (slot 42),
  00553260 (slot 44, placement after a move, kills a unit that cannot stand), 005533B0 / 00553450
  / 00553500 (stat refreshes through the UnitStats builders), 00553610 (slot 25, weapon switch):
  stateful with UnitStats builders not yet rewritten.
- 0054BBC0, 0054BCB0, 0054C2F0, 0054C6B0, 0054C8B0, 0054C960, 0054CA40 (line of sight and target
  checks): x87 with AI map calls; still candidates for pure verification.
- Load/Save (00555D10 / 005561C0), the containers and the world managers: not run in a fight.

### Risks (second round)

- Everything but HitDirection is checked for consistency only. The checks were chosen to hold for
  any correct run, but they cannot see a wrong branch that leaves the same state; the scenario
  (combat_more.txt) exercises a kill, the archer's attacks, the heal and the archer's death.
- The scenario's archer, "Human Gipath Brigand A1", was picked from database.res (its first weapon
  is "bone short bow.decayed bones") without running it. AddUnitToServer's second argument is the
  unit prototype name, as WolfTutor is; if the archer does not shoot (e.g. it walks up and
  hits), the missile path is not exercised but the scenario still passes. A wrong name makes the
  `waitfor IsAlive(Archer1)` fail.
- OnDamaged keeps the death animation in a local AnimationList; the original destroys it from an
  EH frame if anything throws, ours does not (nothing in that path throws in practice).
- The verify wrappers of OnDamaged and TriggerItemSpells now also see the calls from TakeHit
  (through the game's addresses).
- FinishCast's clamps select through float loads where the original selects dwords with masks:
  only a signalling NaN in the mana could differ.

## Third round: Load/Save, sight checks, the planner, animation steps, the order machine

Code: `src/game/combat_save.cpp` (Load/Save), `combat_sight.cpp` (what an order must see, whether
it can be carried out from a point, the planner), `combat_steps.cpp` (animation steps of orders,
casts, attacks, transitions, death), `combat_tick.cpp` (the per-tick order machine), structures in
`combat_plan.h` (AnimationStep, AnimationSteps, OrderPlan at +0xC4, SightTarget). Verify:
`tests/verify/verify_combat_save.cpp`, `verify_combat_sight.cpp`, `verify_combat_steps.cpp`,
`verify_combat_tick.cpp`, helpers `combat_verify_util.h`, `unit_capture_stream.h`.

Shared files touched: combat.h (method declarations, UnitAction::Assign/Reset; the death animation
forwarder renamed `CallBuildDeathAnimation`, its one caller in combat_actions.cpp updated). unit.h
gained `UnitStream` (the Load/Save stream interface) and two CUnit declarations (units.md).

### Functions rewritten (third round)

| Address | Name | Verification |
|---|---|---|
| 005561C0 | CUnitServer::SaveServer (slot 10) | both into capture streams, bytes and write sizes compared, the original's writes forwarded |
| 00555D10 | CUnitServer::LoadServer (slot 9) | ours only: +0x918 cleared, effect object referenced, attackers list count matches its nodes |
| 0054BCB0 | CUnitServer::FindSightTarget | output only: original into a scratch SightTarget; bytes and AL |
| 0054C2F0 | CUnitServer::CanActFrom | pure (AL) |
| 0054CA40 | CUnitServer::PlanAction | random + state: generators saved, ours into the real plan, restored, original into a deep copy; result, plan (steps without byte 3) and generator state compared |
| 0054C6B0 | CUnitServer::CanPlanAction | random: result, the target's flags and generator state |
| 00551A40 / 00551A90 | CUnitServer::AnimationDuration / AnimationMoment | pure |
| 005516D0 | CUnitServer::BuildTransition | random + output (as PlanAction; ours stands) |
| 00550100 | CUnitServer::BuildOrderSteps | the same, plus the duration |
| 00550620 | CUnitServer::BuildCastSteps | the same |
| 0054F970 | CUnitServer::BuildAttackSteps | the same, plus plan +0x2C and the target's reference count |
| 00550A80 | CUnitServer::BuildDeathAnimation | the same |
| 0054B320 / 0054B230 | UnitAction::Assign / Reset | ours only: fields as copied / kind 9 and no target |
| 00546830 | CUnitServer::SetUnitMoveMode2 | ours only |
| 0054A620 | CUnitServer::SyncMoveModes | ours, snapshot of +0x168..+0x177, restore, original, compare |
| 0054A6E0 | CUnitServer::TakeQueuedOrder | ours only: the queued order is empty afterwards |
| 0054A900 | CUnitServer::StartQueuedOrder | ours only: never true with an empty queue |
| 0054B390 | CUnitServer::UpdateOrders (every tick) | ours only (counted) |
| 0054DD70 | CUnitServer::ContinueAction | ours only (counted) |
| 0054D960 | CUnitServer::ExecuteOrder | ours only (counted) |
| 0054D140 | CUnitServer::ApproachTarget | ours only (counted) |

### How an order runs

Every tick the controller calls UpdateOrders: StartQueuedOrder takes the queued order over by kind
(0 stand, 1 move with a path planned around lifted nearby units, 2 turn, 3-6 ExecuteOrder, 7 play
animation +0x1C, 8 emote, 10 use (plan from BuildOrderSteps(6, 0)), 11 stop); else action orders
go through ContinueAction (target alive and seen) to ExecuteOrder. ExecuteOrder plans the action
(PlanAction into +0xC4): when it plans, the attack interval (+0x184) is advanced, the hit is built
and resolved for attacks (backstab unless ranged or the defender's +0x174), and 005494D0 starts
the action; when it does not, ApproachTarget closes in: waits when the target is in reach (or
comes into reach before the next attack), else plans a path to where the (party leader's) target
will be in 2..64 ticks.

PlanAction builds the order's animation (BuildAttackSteps / BuildOrderSteps / BuildCastSteps),
then steps through the ticks of a half turn plus 3 for the first moment the target (predicted for
moving targets and missile spells) is in reach, the unit can turn to it in time, and CanActFrom
holds: AI units only within 1.05 times their sight range, melee and skill orders not with a living
unit in the way (0.1..0.9 along, 0.3 aside), and a line of sight (CAIMap) from the eye height when
the target is beyond FindSightTarget's minimum distance.

Animation keys (005301B0 looks a unit's animation up): weapon bit (1 << weapon type, natural
weapons below 7), movement bits (0x30000 / 0x20000 / 0x10000 / 0x8000 by mode), action bits
(0x90000 attack with the height band 0x1000000 high / 0x2000000 middle / 0x3000000 low | 0x800000,
0x40000 skill, 0xC0000 cast, 0x240000 mode transition, 0x6000000 death...). A step is {animation,
0xFF, 0, uninitialised, duration, +8}; the duration is the entry's frames (+0x20; +0x40 the
action's moment) over the unit's speed factor for it (005327D0, which the first round named
ResistanceTo), truncated.

### Forwarders added (third round)

- Containers and helpers: 00557B80 SetSize, 00557CC0 Add, 00558950 SetAtGrow, 00544CE0 AddStep,
  00544DD0, 00544D50, 00544E70, 00545110, 0054B260 / 00557E70 copy / destroy, 005582A0 / 00558420
  list load / save, 0052CA10 CPtrList RemoveAll, 00545F40 UnitAction::Clear.
- Not rewritten in this range: 0054A150 follow path, 00550F30 idle animation, 00546C40, 00547280,
  00547090, 005474B0, 0054EB30, 005467B0, 00548700 path to a point, 005468D0, 005494D0 start the
  action, 00548DE0 / 00548FA0 play a list by value, 00549690, 0054C760, 0054C8B0 (AL may be the FPU
  status: always called through its address), 00552780 shout, 00544EF0 / 00544F60 plan load/save,
  005585F0 / 00545430 attackers list.
- Unit range: 005301B0 / 00530240 animation lookup / entry, 00525B20 pick a hit location, 00530560
  height, 00530280 ground height, 0052E9D0, 00530570, 005287A0 / 00528800 stats load/save.
- Elsewhere: 005B2430 / 005B2570 / 005B1DE0 / 005B23A0 / 005C7B00 motion, 0050E220 / 0050E290
  counted references, 005D8310 / 005D8860 brain, 005AEC40 / 005AEC80 voice, 005BBC70 map cell flag,
  005C1010 / 005C0DF0 sector map (also the global one at 007B01F4), 0058E5D0, 005CB7F0, 0066E110,
  0066D6E0. Rewritten ones called through their addresses: ActionDistance, CanReachTarget, CanCast,
  BuildAttackHit, ResolveHit, CUnit::BaseSpeed / ModeSpeed / ScaledValue / IsBehind / ResistanceTo,
  CAIMap::LineOfSight, FindObject, UnitParams::Reset.
- Globals: 007A9D40 ISAAC, 007AFFC4 frame, 007B00D2 path flag, 007AFF60 world, 007AFFA0 unit
  manager, 0078FD48 the "can't find animation" text.

### Original bugs and oddities kept on purpose (third round)

- LoadServer *writes* +0x93C for version 0x73 and later instead of reading it.
- Save writes the constant 2 after +0x180; Load reads it into a local.
- BuildOrderSteps uses the address of the step list as the source of an unknown skill (kind 6).
- BuildAttackSteps reads the plan's address as a float for an invalid hit location.
- Steps carry an uninitialised byte 3 (00550A60); ours write 0 and the checks ignore it.
- CanActFrom's "in the way" test divides by the squared distance, not the distance.
- PlanAction's duration local is not initialised by the original (the builders always set it).
- StartMove passes the goal's z as the path search's range.
- ApproachTarget returns with the path flag (007B00D2) cleared when the goal is under 0.1 away.
- ExecuteOrder passes 005494D0 dwords whose upper bytes are the time / garbage (only bytes read).

### Skipped and why (third round)

- 0054E600 PlanOrder, 00554830 TurnTo, 0054A150 path following, 00547890 (2.8 KB), 00552300
  (tick slot), 00549160 / 00549690 / 005499F0 (animation step and the end of an order), 00550F30
  idle animation, 00551AE0 taming, 00552D00 claims: next candidates; their callees are now known.
- 0054C8B0 / 0054C960: return the FPU status word in AX on one path (callers test AL).
- 00536670 / 00535CB0 / 005381B0 (CUnitClientSpecific figure and armour model lists: CString and
  STLport vector building, model loading): not done this round.

### Risks (third round)

- The order machine (UpdateOrders, ContinueAction, ExecuteOrder, ApproachTarget, StartQueuedOrder)
  runs every tick for every unit and is checked for consistency only; the scenarios combat,
  combat_more, unit_ai and units exercise it. None of it has run in game yet.
- x87: values the original keeps on the FPU stack are double locals (exact under 24/53-bit
  precision, not under 64-bit); operand orders follow the disassembly.
- Save's checks run the nested saves (map object, controller, brain) twice into capture streams;
  they only write through the stream's virtual Write.

## Movement tick (00547890)

Code: `src/game/unit_motion_tick.cpp`. Verify: `tests/verify/verify_unit_motion_tick.cpp`.
Scenarios: `movement.txt`, `combat.txt`, `unit_ai.txt` (every moving unit runs it each tick).
Shared files changed: none.

| Address | Name | Verification |
|---|---|---|
| 00547890 | `UpdateMotion` (__fastcall on the controller, called twice by 005499F0) | ours only: the controller keeps its unit, the "waiting for" link (+0x1D8 / +0x1DC) is set with a unit or clear, the obstacle flag (CUnit +0x278) is 0 or 1 |

What it does, per tick, for the controller `c` (CUnitServer) of unit `u`:

1. Samples the motion order (+0x3C) with UnitMotion::Sample (u + 0x27C) at the world's time
   (ManagerContext +0x64, FILD / FSTP float): the position comes back in half-cells and is scaled
   back (factor 1 / (0073EEE4 * 0.5), then 0073EEE4 * 0.5, unrounded in between: naked helpers).
2. Not turning and +0x147 clear: the order's next animation (00551400) goes into the queue (+0x24,
   an AnimationSteps whose `extra` +0x38 is the start time; -1 becomes the world's time), the queue
   restarts (00546B40, 00546C10), +0x147 = 1.
3. A pending hit (+0x160 != -1): while turning on the spot the death / hit animation of the blow is
   played when it fits (00549D00 on its duration; the queue is copied to +0x148 first) and the tick
   ends; otherwise, unless the queue's first animation is an attack still running (entry flags
   0x200000 under 0x3C0000, its +0x20 plus the start beyond the world's time, unsigned), the
   current animation (005511A0) is copied step by step behind the fall of the blow (pauses as
   emotes, open steps through AddStep, others SetAtGrow) and assigned to the queue (twice).
4. With a reached cell: off the sector map, CanStandAtFromHere on it. If not, the blocking unit
   (BlockingUnitAt) decides: a party member that follows this unit as its brain's leader (for 2
   frames after +0x93C), or the unit already waited for (+0x1DC): wait (2). Both attacking / casting
   at the same target with the same side and size class, the target more than sqrt(0073F090) away
   ((dz^2 + dy^2) + dx^2 unrounded) and the other not casting: wait behind it (counted link), unless
   the links lead back to this unit: both links cleared, replan (1) with the units around lifted.
   Otherwise a slower blocker (virtual slot 59 below this unit's, rounded to float) with reason 2
   (water) makes it replan with the units lifted. Back on the sector map.
5. An idle unit in a kind-3 occluder (while +0x4C is below the world's +0x2804 and +0x174 is clear)
   replans. Otherwise a standable cell and CSectorMap::Move: the unit is an obstacle (+0x278) only
   while idle, faces the sampled direction (00402220), and when the motion is over a turn order
   (+0xA8) or the path's end stops it facing backwards (AxisY negated, AIPath::Start) or the path
   continues (ContinuePath), else the idle animation.
6. Replan: the pending step list (+0x78, CList) emptied, FindPath to +0x9C within +0xA4 (u +0x43C
   saved around it; the order's +0x6E flag set for units without a player), the units around the
   step lifted around it (005B7EF0) when asked; no path: 00547280, 00547090(6), +0x145 = (+0xA9 == 1).
   Wait: +0x21 (mode +0x240 == 2) or +0x20 set, and the unit stops being an obstacle.
7. Running party units (both move modes 2, +0x16D, the player's +0x108 clear, game mode 007C2CC4
   not 3) drain stamina: the server stats' +0x14 -= +0x18 * (double)0073F088; below 0 (unrounded,
   FST then FCOMP) they get message 0x1D, stamina 0 and walk (+0x268 from +0x170, MarkChanged 6);
   MarkChanged 0. At 0 stamina only the mode is reset. Then 00548520.

### Original quirks kept

- The "blocked" local is not initialised: when no cell was reached (cell x < 0) or the cell lies
  outside the AI map (CanStandAtFromHere returns before writing the reason) and the move then fails
  (or an idle unit stands in an occluder), the original decides on whatever the stack held there.
  The rewrite reads the same stack dword (entry ESP - 0x98) in a naked entry before its own frame
  exists, and the verify wrapper does the same.
- The animation key passed to 00530240 keeps the queue count's upper 24 bits (MOV AL over EAX).
- An emote step is passed as the misaligned dword at step + 1.
- The fall animation is assigned to the queue twice.

### Forwarders

00455940 / 00455950, 005C7430 (Sample), 00551400, 00546B40, 00546C10, 00549D00, 00546C40,
005511A0, 00550F30, 00547280, 00547090, 005467B0, 00548520, 00550A80, the AnimationSteps helpers
00556850 / 00557E70 / 005483C0 / 00556950 / 00544E40 / 00558920 / 00544D50 / 00544CE0 / 00558950,
00530240, 0050E9F0, 0052E9D0, 00402220, 005C1010 / 005C0DF0 / 005C11D0 (Move: patched by Rebirth),
005B81B0, 005B7BB0, 005BBDB0, 005BB350, 005BB900, 005B7EF0, 005B23A0, 005C7B00, 0040F560,
00402200, 005D5E90, 0066D6E0, MFC CPlex::FreeDataChain.

### Risks

- Not run in game. It runs for every moving unit every tick; a mistake shows in movement.txt.
- The stale-local trick depends on nothing between the hook's JMP and the entry touching the stack
  below ESP (true for the 5-byte JMP).

## Fourth round: the update, the animation queue, the end of an order (2026-10-04)

Code: `src/game/combat_update.cpp` (plain C++ float maths, rule 7). Verify:
`tests/verify/verify_combat_update.cpp`. Scenarios: every creature runs these each update
(combat.txt, combat_more.txt, movement.txt, unit_ai.txt). Shared files changed: combat.h
(declarations only).

### The update (slot 6, 00552300)

Living unit: UnitVitals::Regenerate on the server stats (in combat while an order of kind other
than 0 is current; a change recomputes the stats and marks parts 0 and 4); every (+0x914 speed
class + 1)-th frame the AI events (world + 0x2838, 005C9770) get the unit's position (the record
at +0x918 is updated, then replaced); the armour's periodic spells (00552B90); a party unit's
perception memory (+0xA50, 005CB850; marks part 0xC) and boredom (+0x938 counts updates; past 500
one ISAAC draw in 1001 sends remark 0x29 and sets 400, or 0 when the player's +0x5120 is set); the
alarm (+0xB10), AIBrain::Think (+0x980), flags_ bit 0x8000 cleared, PropagateHostility. Then,
unless frozen (flags_ 0x1000000), reacting to a hit (+0x144) or in state 3..5, UpdateOrders; an
acting unit (5) with a queued move ends its order at once (EndOrder(1)); UpdateState and
AdvanceAnimation in either case. Dead unit: the body sinks (z -= the falling speed +0x8F4, which
grows by 0073C548 each update; never below 0) and, once the animation queue is empty, the death
is reported once (+0x18D): 00593AF0, the unit leaves its player (005300D0), the player's message
object (slot 20) or the brain and alarm resets, both orders emptied.

### Controller fields (this round)

- `+0x020` frozen this update, `+0x021` held by a hit reaction (UpdateState sets them;
  AdvanceAnimation shifts the motion, queue and plan times by one while they are set).
- `+0x024` the animation queue (AnimationSteps; +0x38 the time its first step started, -1 until
  started), `+0x03C` the motion (AIPath), `+0x09C` / `+0x0A4` a point to path to and its range
  (state 0 with `+0x146`), `+0x0C4` the plan (OrderPlan; +0xF0 its location, +0xF4 notSkill).
- `+0x144` a hit reaction plays (combat.h calls it keepHitDirection), `+0x148` the queue saved
  meanwhile (its time at +0x15C), `+0x160` the hit direction to react to (-1 none).
- `+0x180` the update at which the size class is refreshed while acting (state 5 + 10),
  `+0x184` the next attack's time, `+0x18D` death reported, `+0x8F4` sinking speed, `+0x914`
  speed class mask, `+0x918` the AI events' presence record, `+0x920` the armour spell list
  ({next, ?, item, countdown}), `+0x938` boredom, `+0x978` a callback run when state 3 ends.
- States (+0x164): 0 idle, 1 moving, 2 turning, 3 the rest of an action's animation, 4 (as 2 for
  hit reactions), 5 acting (the plan runs), 6 what an order leaves (combat_tick.cpp: "chasing").

### Functions rewritten (fourth round)

| Address | Name | Verification |
|---|---|---|
| 00552300 | CUnitServer::UpdateServer (slot 6) | ours only: returns 1; a living unit ends with hitDirection -1 |
| 005499F0 | CUnitServer::UpdateState | ours only: hitDirection -1 afterwards |
| 00546F30 | CUnitServer::AdvanceAnimation | ours only: the unit's +0x25C is the first step's kind (1 frozen), +0x260 whether held |
| 00546B40 | CUnitServer::PopFinishedSteps | the queue (steps, count, time) snapshot, ours, restored, original; result and queue compared |
| 00546C10 | CUnitServer::ShowCurrentStep | ours only: a shown step's animation and time on the unit |
| 00546C40 | CUnitServer::PlaySteps | ours only: the queue started, no more steps than given |
| 00547090 | CUnitServer::SetState | ours only: the state; 5 sets +0x180, others drop the effect object |
| 005467B0 | CUnitServer::SetStanding | ours only: CUnit +0x278 |
| 00547280 | CUnitServer::StandStill | ours only: not standing afterwards |
| 00547100 | CUnitServer::PlayHitReaction | ours only: a started reaction holds the unit (+0x21) |
| 00549690 | CUnitServer::EndOrder | ours only: the plan reset, state 3 or 6 |
| 00549160 | CUnitServer::TickAction | ours only: an action at its end time leaves the plan reset |
| 00549D40 | CUnitServer::ActionInReach | reads only: ours and the original compared |
| 00552B90 | CUnitServer::TickItemSpells | ours only (counted) |

Movement modes, animations and paths (`src/game/combat_motion.cpp`, verify
`tests/verify/verify_combat_motion.cpp`):

| Address | Name | Verification |
|---|---|---|
| 00549D00 | CUnitServer::CanReactFor | reads only: compared |
| 00548DE0 | CUnitServer::PlayAnimationOrder (steps by value) | ours only: state 3, the queue started |
| 00548FA0 | CUnitServer::PlayEmoteOrder (the same code, state 4) | ours only: state 4, the queue started |
| 00550D90 | CUnitServer::BuildIdle | random (the lookup 005301B0 -> 00519380 may pick an entry with rand()): the generators saved, ours, copied, restored, original into the same list; steps (without byte 3), extra and generator state compared |
| 00550F30 | CUnitServer::IdleAnimation | ours only: +0x960 returned, the key is the unit's |
| 005474B0 | CUnitServer::PlayIdle | ours only (counted) |
| 005511A0 | CUnitServer::BuildMoveSteps | as BuildIdle |
| 00551400 | CUnitServer::NextAnimation | ours only: +0x944 returned |
| 005494D0 | CUnitServer::StartAction | ours only: state 5 |
| 00548520 | CUnitServer::UpdateSneak | ours only (counted) |
| 0054A150 | CUnitServer::ChangeMoveMode | ours only: a change leaves both records equal |
| 00548700 | CUnitServer::PathTo | ours only: state 0 with a path, 6 without |
| 005468D0 | CUnitServer::LiftNearby | ours only: the list is well formed (count, links, tail) |
| 0054EB30 | CUnitServer::ReportPath | ours only (counted) |
| 00546480 | CUnitServer::ResetController | ours only: the reset fields |
| 00554450 | CUnitServer::ResyncWithUnit | ours only: no current order, the records follow the unit's modes |
| 005528F0 | CUnitServer::RefreshItemSpells (combat_update.cpp) | ours only: every entry worn, the count matches |

Planning an order's way (`src/game/combat_planning.cpp`, verify
`tests/verify/verify_combat_planning.cpp`):

| Address | Name | Verification |
|---|---|---|
| 0054DF50 | CUnitServer::FindUseSpot | ours only: only use orders fail |
| 0054E250 | CUnitServer::TimeUntilAction (ST0) | reads only (simulates a copy of the motion): compared (CheckNear; steps of 5, exact) |
| 0054E600 | CUnitServer::PlanOrderRoute (combat_orders.cpp's PlanOrder forwarder) | ours only: units without a player succeed except for use orders |
| 00554830 | CUnitServer::PlanMove (combat_orders.cpp's TurnTo forwarder) | ours only: the movement mode is the same afterwards |

How an order's way is planned (0054E600, called by the order setters with the shout to make):
the target's aim point (or the order's point); a use order (kind 6, source 0) goes to the
reachable spot its target offers (slot 44 for the unit's size, slot 60) with the cheapest path
ending within 0073EF28 of it (the spot becomes the order's point, flag18 set); a party unit that
can already act stops there. Otherwise the path is searched with the target lifted off the sector
map (the path flag 007B00D2 cleared meanwhile; CUnit +0x43C = 1), turned into a motion order
(005B22C0), and the unit is put at its destination for CanReachTarget / CanPlanAction; the time
until the action (TimeUntilAction: the motion simulated in steps of 5 ticks until the target is
in reach and, when needed, in sight) goes to the player's path message (0066D490: kind 2 for
attacks and spells without a target point, 1 otherwise, flag "failed"). Shouts 0x0B (attacks) /
0x0E when it fails. Stack objects: an AIPath (0x88 bytes: order, step and cell lists 00545750 /
00539890, a point array 00557230 / 00557250) and a motion order (0x3C bytes: order and step list);
a UnitMotion (0xA4 bytes; its members 00556D70 / 00556810 / 004D5990 / 0054E5D0 / 004CB050, the
speed scale 0073EF20, the turn limit 1e10).

More controller fields: `+0x0A9` / `+0x0AA` the path's flags (+0x6D, +0x6E: no player), `+0x145`
the last path was blocked, `+0x146` a pending path to +0x9C, `+0x147` moving (the movement
animation plays), `+0x178` updates in mode 3 without a cheap cell, `+0x238` (stats +0x58) a
hero's stamina for the walk / run animations (limits 007C5448 / 007C5440), `+0x940` / `+0x944`
the movement animation key and list, `+0x95C` / `+0x960` / `+0x974` the idle key, list and its
time (0x666DEAD: no key yet), `+0x1DC` the unit waited for (referenced), `+0x984` the brain's
mode (6: no path reports). Animation keys: weapon bit | movement bits (0x30000 / 0x20000 /
0x10000 / 0x8000) | 0x80580000 idle; | 0x100000 (unit mode 3) or 0x140000, and 0x40000000 once or
0x80000000 looped (heroes | 0x800000 / 0x400000 when tired) for movement.

### Forwarders added (fourth round)

00455940 / 00455950; CUnit 0050E9F0, 0052EA90, 0052EAD0, 0052EAF0, 00530570, 00530970, 005300D0
(SetPlayer), 0052E9D0, 00401F00; 0040F560 (AxisY); 00523740 / 00524560 (vitals); the AI 005C9770,
005CB850, 005AEA80, 005AEA40, 005D53B0, 005D4E80; the world 00593AF0, 0058E520, 0058E5D0;
00665440 (the player has the unit); 00679280; 005C1010 / 005C0DF0; the motion 005C7CB0, 005B23A0,
005C7B00, 005C7430; the controller's 0054B390, 00547890, 00552E00, 00549EC0, 00552C60, 00550A80,
0054B880, 00548700; the CArray<AnimationStep> instantiations 00556950 / 005483C0 (Copy, two copies
of the same code), 00556870, 00557B60, 00557E70, 00557B80, 00557E20 (library code: not ported) and
OrderPlan's reset 00544FD0. combat_motion.cpp adds: 005301B0 (animation entry for a key), 00402200
(NegateVector), 005241C0 / 005233E0 (UnitStats), 005BA6E0 / 005BA830 / 005BA5A0 / 005BB350 (AI
map), 005C0BD0 / 005C1010 / 005C0DF0 (sector map), 004B3330 (ScriptGroup::Contains), 005B1F10 /
005B2050 / 005B22C0 (AIPath), 005D5E90 / 005D4F70 (brain), 0066D490 (a player's path message),
00551A40 (AnimationDuration), 005516D0 (BuildTransition), 00550050 / 005500A0, the CArray
instantiations 00558950 (SetAtGrow), 00557CC0 (Add), 00544D50 (add an emote), 00550A60 (a blank
step), and MFC 006FE90F (CPlex::FreeDataChain), 0052CEF0 / 005588A0 (CList node allocation).

### Original bugs and oddities kept (fourth round)

- UpdateState and PlayHitReaction copy the saved / new animation into the queue twice; PlaySteps
  resizes and copies twice; EndOrder and TickAction assign the new queue twice.
- ActionInReach: a cast (kind 5) whose target is gone but whose spell has flag 0x20000000 goes on
  to read the (null) target's position: an access violation in both versions.
- TickAction's plan reset is the inlined 00544FD0 (EndOrder calls it); both leave the same state.
- Boredom draws from the ISAAC generator only once +0x938 passes 500.
- ReportPath and PlanOrder pass the player's path message a stack motion order (0x3C bytes: order
  and step list) whose order part is uninitialised when no path was copied into it (kind 3, which
  the message does not serialise); ours zeroes it.
- PlanOrder looks up the spell of `action` (+0x194), not of the order it plans, for the message
  kind of a cast.
- FindUseSpot writes z = -1 into every order's point it is given, use order or not.
- ResyncWithUnit tests "current order not 0" right after setting it to 9 (always true), and
  "state not 2" right after setting state 6 (always true).
- RefreshItemSpells keeps the previous item's spell for a list node without an item (the original
  reads a stale stack slot there; ours starts from none).
- 005483C0 (the queue's assignment) copies the extra dword (the start time) too; 00556950 copies
  only the steps: callers of the latter set the time themselves.
- PathTo saves CUnit +0x43C, writes the same value back and restores it after FindPath.
- NextAnimation passes a pause step's emote as the misaligned dword at step + 1 (as the movement
  tick does); the idle builders and the movement steps duplicate the same key code inline
  (00550D90 / 00550F30 / 005474B0 / 005511A0 / 00551400); ours shares it.
- LiftNearby's first loop over the list does nothing (an inlined RemoveAll walking the nodes).

### Risks (fourth round)

- Not run in game. Every creature runs UpdateServer, UpdateState and AdvanceAnimation every
  update: a mistake shows at once in any scenario with units (smoke.txt onwards).
- Most checks are ours-only consistency checks; PopFinishedSteps and ActionInReach are compared
  with the original.
