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
