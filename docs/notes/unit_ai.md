# Unit AI: brain, modes and actions

Code: `src/game/ai/unit_ai.h` (layouts and per-function comments: the structure reference), `unit_ai*.cpp` (in
unit_ai.cpp the brain's Think, lookups, actions and movement, resets, calls, memory, behaviour records and save; modes,
senses, targets). Scenarios: `unit_ai.txt`, `unit_ai_fear.txt`, `script_exec` (UMClear, UM*, UMAg, Guard / Sentry),
`combat_more` (a death), `world_load`, every spawn; `unit_ai_flight` (an archer in Fear mode steps away from the hero: FindFlightPoint), `party_leader_switch`
(PlayerMode::SetLeader through the battle screen's follow button, CommandGoTo with Ctrl, StaysWithParty, SeeCorpse of a
party member), `unit_ai_bridge` (zone1's bridge 299: a walk over it, then RemoveObjectFromServer lifts its deck cells:
RestoreCell, BridgeDeckAt).

## Structure

- The server unit controller (combat.md) holds the brain; its update calls `AIBrain::Think`. The brain owns its modes
  (`AIBrain::modes`, `std::unique_ptr`; `current` points at the one in charge). The script's UM* commands build modes
  and add them with `AddMode`; `StandardGuard` / `StandardSentry` add exactly five modes in order.
- Modes: Guard, Patrol, Sentry, Follow, Suspection, Aggression (UMRevenge: `fixed`), CorpseWatcher, Fear, Player (party
  units, type 6), Tamed (type 8). A mode's type is a const member (`AIMode::type`). `Exclusive` (true for Guard, Patrol,
  Sentry and Follow): AddMode replaces a mode of another type when both are exclusive. RemoveModesOfType (UMAg) taking
  out the mode in charge shuts it down (`AIMode::Detach`: references dropped, constructor values back) and leaves it in
  charge, outside the list, until another mode takes over (`AIBrain::removedCurrent`); the save then writes current index -1.
  Load creates modes by type; an unknown type leaves Load at once (kept).
- The perception memory `AIMemory`: lists of counted object references (nearby, visible, seen corpses, corpses),
  reports (`AIReports`) and heard calls (`AIHeardCalls`), `sightCells`, `nearbyCountdown`; the alarm records
  (`AIAlarmRecordArray`, the map spec's behaviour record); `AIBrain::Init` (the controller's reset) assigns a new brain,
  attention and sight-cell cache included (a reset controller is only created or torn down); `callRadius` (10 after
  Init; equal to [Logic] CallForHelpRadiusDefault becomes CallForHelpRadius on load, NaN too); FindStandPoint's cooldown (after a failure, a third of the distance in calls, at
  most 10); UpdateFollow's timer (acts from 8; an order sets 0 or 6). The attention: two arrays of `AIAttackOption`
  (`weapons`: the weapon and the attack spells, kinds 1..4; `spells`: the helping ones, 5..7) and the heal weight.
- Calls for help: `CallAllies` lists the units within the call radius of the owner through the sector map; each allied
  one's memory hears a call of delay 0x1A (earlier unflagged records of delay >= 0x1A are dropped, then {point, delay, 0,
  text} appended). A heard call's `delay` is a countdown: the call is heard when it runs below 0, printing "Delayed sound:
  <text>" (sound.md).
- Names: `AIMemory::CanTrack` answers true for the same side, the same player or a unit in sight;
  `AIReport::ticksLeft` counts a report down (`kCastStoppedTicks` 0xF, `kRejectedTicks` 0x14); `AIMemory::noticedDecay`
  is the noticed level's decay per update (-1..-3); `AIBrain::nearRange` / `farRange` (digest keys nearX / nearY) are
  two distances (35 / 80 plus Guard's radius) that nothing reads but the save, and `decay` only feeds
  itself and the save. `PlayerServer::danger` is the party's danger the script's IsPlayerInDanger / IsPlayerInSafety
  read; the Player mode's units act on their own while it is InDanger (`PartyInDanger`).
- The AI's eye `AIViewer` (on the world server): Update, Refresh (nearby list every 8..11 ticks from the sector cells of
  the memory's spiral cache), See (sight range x line of sight inside the cone, peripheral, sense life), Prepare (eye
  height, facing from the orientation). `AIViewer::Noise` (attacks, skills, explosions): hostile units within 50 whose
  hearing x loudness reaches hear it at the source's position. `MeasureExposure` (each server tick) writes five exposure
  ratios into the unit.
- Targets: `AIBrain::Choose` scores candidates; `CanReach`: a spell aimed at no object vs a large size-class-0 unit: no;
  else the order check from the unit, or from the end of a path search with the target lifted. `AIAttention::Build`
  makes the option lists when a unit's controller is set up and on ApplyBehaviour.
- FearMode's unset x is -1313.0f (0xC4A42000). AIMemory::Update takes over a noticed point inline (a delayed sound: level
  900, decay -3; a corpse seen: 800 / -3; an enemy lost from sight: 1000 / -2), so the final add uses the new level and
  decay. AIMode::Enter draws a random decay in [0.8, 1.2]; AIMode::Evaluate gives 50; Aggression evaluates to 10000 with
  chosen = enemy, or -1000.

## Scenario know-how

- Units far from the hero hardly update (UpdateCountdownFor): walk the hero there (`MoveToPoint(GetLeader(), x, y)`,
  then `waitfor 300 ...`: 170 s of game time is 4 s of run) instead of spawning units at a distance.
- A unit made by AddUnitToServer with player 0 is in the hero's party (a second portrait) with a Player mode; UMClear on
  it removes that mode and with it every script move (`MoveToPoint` does nothing afterwards).
- Battle screen buttons by layout position (vanilla; Rebirth's widescreen moves them): the action column's first button
  (20, 470) is "follow" (pending action 7, with a unit selected; the next click on a unit sends the follow order of every
  selected member), the second (20, 440) the hero's science (pending 8). Portraits: 360 and 440 at y 560 for two members.
  `clickunit #<id>` finds a unit with an empty name. A Ctrl click on the ground is a go to (CommandGoTo).
- GodMode of player 0 makes its units immune to damage (also the party's wolves); `InflictDamage` has no attacker and so
  no direction of the blow (HitDirection None): it never starts a hit reaction in UpdateMotion.

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9). Crashes, undefined behaviour and
invisible bookkeeping are fixed, not listed.

- Moves: PlanLooks' wait of the seventh (untested) draw is kept when all six line-of-sight tests fail.
- `CanTrack` answers true for the same side, the same player, or an object in a list, so UpdateAttack gives up only on a
  unit of another side and player that is not listed.
- Guard's Execute counts failed searches near its point; after 100 it also searches around the unit, on every tick until
  one succeeds (only success resets the count). Patrol with no points logs a warning "Unit %i has no patrol points,
  adding one at its position" and adds a point at the unit. Suspection's Execute computes the "near the point" distance
  only while a move is under way; its level drops by 5 per tick near the point, by 50 in combat.
- AppendPoint rounds 2x to nearest, so its point is (n + 0.5) / 2, 0..0.5 above the asked one, not its half-cell's
  middle. Score's group bonus counts the group minus one for kinds 1 and 4..7, the whole group for 2 and 3; kind 3
  multiplies by (effect / 4) / 2.
- FindEnemiesNear asks the visible unit's IsHostileTo(self) where the others ask the unit's; ForceSee forces when at or
  beyond either of the observer's ranges, or when the object attacks the unit. See skips a dead hostile unit unless the
  unit sees all.
- AIViewer::Refresh (every 8-11 ticks) sets the unit's flag 0x80000, which on a unit is
  `MapObjectFlag::RelationsChanged`: the same tick's `PropagateHostility` then swaps hostility claims with the allies in
  the nearby list, relations changed or not.
- A target removed mid-attack leaves the brain at AIAction::Attack with its target cleared; UpdateCombat (finding
  nobody) leaves it so (its code runs in the fights).

Facts behind fixed entries: Player mode's UpdateCombat carries out its choice like the enemy modes (ApplyAttack: a
spell choice is cast); it matters for the 11 numbered hub NPCs in Player mode with attack spells (BabaSit in bz14h,
OrcCamp7 in bz6g, ...), since named units get no spell options. The noticed level stops falling at kPriorityNone
(-1000), not at 0: Suspection claims with the level plus the current mode's 100, so a search ends only once the level
is below -50; no reader tells -1000 from lower.

## FearMode::Evaluate

Remembers a seen object (notice 1000 / -2; `distance` grows to 1.5 times its distance) and calms `spread` towards 3.
15000 when a ranged unit (size class below 6, no spell in slot 0, not slowed, load factor above 0.6) has its nearest
enemy within [Logic] RangedAttackDist (RangedRunDist while aggressive) and is walking on command or finds a way back 5
from it (a random point within 3, path search, the order check from the path's end, which must lie within 1; then kept
in `fleePoint`, z 1). 3000 while it sees an enemy; 350 in shelter (a kind 3 occluder at the unit, a small or non-0 size
class unit, or at the last shelter). With `watchful` the nearest player unit in sight inside `distance` is noticed.
Then noticedLevel / 6 when it is above 50 and the mode is in charge or the noticed point is within `distance`; else
`distance` moves a 31st back to the default and -1000.

## AIBrain::ApplyBehaviour

The modes of behaviour `record`: the list emptied, the call radius (the default one becomes [Logic]
CallForHelpRadius) and alert mode taken; type Guard guards the record's point, GuardAlarm the point of world alarm
`slot`, Sentry a sentry there, Patrol patrols (each patrol's start on its half-cell's middle, its waypoints as looks
from there; without `loop` the points are walked back too), Party the party's Player mode. Except for types None and
Party the reaction (`alarm`) adds Aggression (Fight) or Revenge with Suspection, CorpseWatcher and Fear(0); Watch
CorpseWatcher and Fear(0); WatchFearful CorpseWatcher and Fear(1). Then the alarm (kind, `slot`, countdown at least 1,
not alerted), no action or target, the attack options rebuilt, and with flag bit 3
the unit joins the world's update map.
