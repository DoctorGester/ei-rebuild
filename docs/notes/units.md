# Units, creature statistics, levers and magic traps

Code (layouts and per-function comments are in `unit.h`, `unit_db.h`, `objects/traps.h`, `lever.h`,
`unit_client_models.h`; each file's header says what it does): `unit*.cpp`, `objects/traps.cpp`, `objects/lever.cpp`,
`objects/lever.cpp`, `torch_*.cpp`, `objects/map_object.cpp`, `object3d_*.cpp`, `net_state.cpp`. Scenarios: `units.txt`,
`unit_create.txt` (eight prototypes, replace, kill, remove), `magic_traps.txt`, `levers.txt`, `world_load` (save / load),
and every scenario that starts the game.

## Layout and conventions

- Party units keep their items in the owning player's party records (found by unit id) instead of the unit's own lists.
- Prototype tables (`UnitDatabase`, unit_db.h): prototypes (a prototype's `modelIndex` indexes models), NPCs, models,
  skill level records; the skill (perk) database separately. The name lookups (`FindByName`) find; an unknown name gives null
  and adds nothing.
- `Unit` (class 0x50) is a MapObject; `CreateUnit` is its Create (a member named Create would override EngineObject's
  virtual). Its controller (`Unit::controller`, a `UnitController`) is `UnitServer` (combat.md) on the server and
  `UnitClientSpecific` (class 0x3D) on the client; `ServerController()` / `ClientController()` give it as either
  through the controller's `ServerSide()` / `ClientSide()` virtuals (null on the other side). The map object's
  companion (`MapObjectCompanion`), the 3D object's side half (`Object3DSpecific`) and the player's connection
  (`PlayerConnection`) answer the same two virtuals.
- Floats: Period20, Reduce, the curves, the speeds, TotalItemWeight, the world's heights (`GroundHeight`, `StandPoint`)
  and `MoveSpeed` are floats; `_ftol` of `Reduce` is the int64 truncation. The skill level values are floats
  (percentages and stat bonuses, exact below 2^24).

## Statistics in play

- `UnitVitals::Regenerate` (every world update): every 15th call mana (unless the unit has an order) gains `(1 + 0.01 *
  skill 19) * regen * maximum`, hit points heal the slots by `maximum * (1 + 0.01 * skill 18) * regen` (times the effects'
  Regeneration when positive) through `HealSlots`; returning 1 triggers `Recompute`: stats from bases plus skills 23-25, protection
  and value pairs plus the active effects' totals (their kinds' OnStats: items_spells.md, "Active effects on scripts"), maxima from the experience curves `base ^ (ln(experience /
  divisor) / ln(logBase)) * stat / divisor * scale * (1 + 0.01 * skill)` (in the original's order), strengthen / weaken scale (at least 1), slot maxima, capacity, load, legs / arms factors, rate. The
  difficulty table scales hits on units without a player.
- Hits: `ApplyHit` (from UnitServer::TakeHit): damage per type beyond the vitals' protection (times the difficulty
  without a player), `WearItem`, `DamageSlot` on the hit location or all six slots for 6: layers (text-key units;
  `WearArmour`), backstab multiplier, sever (-5 x maximum; ArmCrippled / LegCrippled) or disable, the log line "<type>
  passed damage = <n>[ HL was crippled| HL is dead]"; then broken equipment is dropped. `IsBehind`: units with a player
  and no bow / crossbow; behind when the dot with the target's facing is below -0.5 (or NaN). `PickLocation`: the aimed
  slot is kept while its kind is 3; else a neighbour table (head 1, 2, 3; body 0, 2, 3, 4, 5; hand 2: 3, 1; hand 3: 2, 1;
  leg 4: 5, 1; leg 5: 4, 1), then a random working slot, none gives 1.
- Raises: GiveStrength / Dexterity / Intelligence (script) -> Raise*: base + amount, maxima recomputed (strength and
  dexterity round the curve product to float before the skill percentage), strength widens the unit by 0.2, dexterity
  lowers y by 0.2 (at least 0), then Recompute. GiveSkill -> RaiseSkill / RaiseAttribute: the level byte grows and the
  difference of the worths `1.61 ^ (level / divisor) * scale` is booked as experience and spent experience.
  AddScaledExperience is times 1.04 ^ (intelligence - 25).
- Skills: `SkillPrice`: worth = 1.61 ^ (record value / divisor) * scale; cost = _ftol(worth - worth of the record before
  it (read unchecked)) when index % 3 > 0, else worth - worth of 0; price = base ^ (byte sum of the skill level bytes) *
  cost, kept to one significant digit (two when the first is 1-3), small prices rounded up. `CanLearn` checks the
  learnable byte, stat minima, required attribute and skill level, that no level of the same skill id is known in
  another skill, and that the level is new; `LearnSkill` keeps the hit point proportion. `WeaponSkillBonus`: types 0-4
  skill 0 and the weapon prototype's first bonus, 5-6 skill 1 and the second, each plus (stat named by the skill's first
  letter - 25) plus the level value of skill `type`; type 8 skill 0 and the first; others 0. `SpellSchoolSkill`: school
  4 -> 3, 6 / 7 -> 5; the skill byte plus the level value of skill school + 7 plus intelligence - 25. `VisionRange`:
  perception[0] x ((1 - day) + perception[1] % x day).
- Armour layers: three body slots per armour type; `AddArmourLayer` adds the piece's seven absorb values to layer
  `GetTypeGroup()` (2 for types 0-2 else 1) and ORs the mask; `RebuildArmourLayers` zeroes layers 1 and 2 and re-adds
  each worn piece whose condition rounds above 0; `PackBodyProtection`: out[1 + k] = round(current protection of type k
  + absorb values of the body slot's layers with state >= 2, layer 2 down to 0 for text-key units else layer 0); out[0]
  is the caller's attack value. `ProtectionBlock` (the panel): text-key units weight worn slots and add the packed vitals;
  others packed x difficulty.

## Creation, databases, save, network

- Factory (`MapObjectManager::NewObject`, "Creation helpers" below) by type (`MapObjectType` in objects/map_object_manager.h; the side
  halves' types are `SideObjectType` in base_manager.h, the flag bits `MapObjectFlag` in map_object.h). The zone loader
  then calls `ApplyPrototype`: without a prototype defaults and "Can't find prototype %s for object %d"; units get vitals
  and statistics from the records (hit points / mana from the prototype, regeneration from the model, protection = the
  model's seven values x the prototype's factor; stats from the NPC record or a third of the hit points / mana), stand
  lower by the model's offset, take the prototype's scale and second prototype. `ResetToBase` refreshes current values
  from bases. `GiveStartingEquipment` (below).
- **Database records**: `units.udb` is loaded once at startup (and again when the resource database switches) through
  the chunk archive, four passes over one memory stream: level records (chunk 1), models (2), prototypes (3), NPCs (4)
  (a TOML file's records merged over them: mods.md, "Databases as TOML"), each entered into its table's name index;
  then each prototype's `modelIndex` is the place of the model it names and each model slot's `levelIndex` the place
  of the level record it names (-1: none). The skill load reads `perks.pdb`:
  chunk 1 the skills, chunk 2 the level records. Both return 1, or the fatal error report's result when the archive or
  the file is missing (the archive is then not closed). Some bytes of a prototype and a model are not serialised.
  `CountSoundFiles`: model sound names lower-cased, "*.wav" files counted by prefix.
- **Save / Load** order: the map object part, controller, name, relations, player id (-1), side, flags, the weapon
  block (active weapon among them), the motion sub-object, three fields, the active weapon again, weapons / quick items
  / spells / armour as item ids, the effects block, carried items, two fields (from version 0x49), prototype names (from
  0x4D; the second from 0x6A, before that looked up by the first name). Load resolves items in the world's item manager,
  moves the unit between players and refreshes the statistics. `UnitStats::LoadFrom` sizes by format version 0x45 / 0x50
  / 0x63.
- **Network state** (`net_state.cpp`; a DeltaState per object): map object fields 0 the 0x18-byte block, 1 scale, 2
  hidden bit (flag 8), position, orientation, 3 effects (count byte, then per effect four fields; read into the
  companion's EffectsChanged unless a unit). Units add 4 the stats block (read: stat fractions before / after give the
  controller's StatChanged per changed stat, a refresh when any changed, a hit point drop or `UnitStatsFlag::Struck`
  gives the companion's hit with `_ftol(lost * max)`), 5 the 11 equipment ids (the helmet -1 with
  `MapObjectFlag::HelmetHidden`), 6 three motion fields, 7 packed bits, 8 small state bytes, 9 the name, 10 path and
  motion speeds, 11 masks, side, player id, 12 the effects block, 13 a field; then re-equip and refresh. The field 3
  count is one byte read into a word.
- **Unit points**: `UnitActionPoint` always returns `out` (UnitServer::ApproachTarget reads it); a missing animation
  entry logs "Can't find animation entry index"; it falls back to the position where size class 3 cannot stand.
  `UnitPredictPosition` samples the motion for client units following a path order (kind 2).
- **Client path placement** (`ClientPlaceOnPath`, unit_client_update.cpp): the two heading angles go through a float
  store (`Store`) as the original's FSTP (the quaternion's sign flips near +-pi).
- `UpdateSizeClass` relies on the server controller's UpdateMoveFactors; `FindAnimation` works only for types
  0x32..0x34 and draws with `rand()`; `BodyExtent` feeds UnitServer::UpdateSizeClasses.

## Client controller

`UnitClientSpecific` (unit_client_models.h): packed ids of the seven armour slots and four weapons; armour part rows
(body "bd", "rh1".."lh3"; trousers "hp", "rl1".."ll3"; helmet "hr" ("hr.<nn>") and "hd"; armour slots 0 / 1 / 2 use rows
2 / 0 / 1; slots 3-6 have no parts); skins composed through the texture manager (items_spells.md, "Composed textures":
layers 0x32 skin, 0x3005 armour, 0x3004 weapon); wounds "<model><part>w<level>.mmp" (levels 0-3 by durability ratio;
heroes' armour can hide them). Fields: the last animation time, the motion order, weapon / arrow hidden, fade-in (15
ticks), colour flash countdown, footprint countdown, the waiting speech sound, the speech queue (a delay counts down,
plays at 0, erased once its sound ended; kinds 2 / 0x1C skip while the previous one plays; kinds 0x1D-0x2A queue
behind; `talking` sets the server unit's flag 0x800000), the blink. Level is ln(packed experience / a) / ln(b), rounded
(experience 0: ln 0 = -inf rounds to INT_MIN, kept). The client's idle animation picks draw from `g_clientRandom`.

The "blink" is the speaking mouth: the face texture is "<model>mouth[_NN]", and only the briefing's speakers blink. Its
rectangles go to `render::TexturePatch` (renderer.md), which draws them over the skin on every level. Check:
`render_blink`.

Torches (class 0x3A): the client half shows / hides the particles, light and looping sound and rebuilds them from the
spec (particle type + offset + scale, point light offset + radius + colour, sound name). TrapObject (placed trap, class
0x55) is armed when a trap item is used and sets off "Mushroom" particles through a world script. `TakeRandomItem`
(stealing): carried items first, else a random filled quick item slot emptied without release.

## Levers

The lever table (`objects/lever.cpp`, records from `Levers.ldb` in database.res: chunk 1, entered into a name index;
missing: "Can't open Levers database"), found by model name (null for an unknown one). `SetState`: a negative (or
NaN) duration takes the model's default or 10; a negative state is the next (wraps when cycling); with a live world
units within radius + 0.5 leave the sector map around the move, the end tick is (tick + duration) + 1, lifted units that
cannot stay go to a free cell and are resynced. `CollectUseSpots` (from FindUseSpot). `levers.txt` switches zone1's gate
"VGate" (id 1889, the only lever on zone1) and saves / loads it.

## Unreached in the shipped game

`TrapObject` (class id 0x55, objects/traps.h) stays: the factory makes one for map object kind `Trap`, but no mob record
kind does.

## Magic traps

Class 0x3B (flags 0x80000 armed: a new trap is armed until a script disarms it; 0x40 kind). `UpdateTrap`: server only;
period <= 0: `UpdateVictims` (armed traps ask `FindTrapVictims` for newcomers and cast), else the countdown runs and
`FireAtOccupants` fires every tick until a cast resets it. `CastAtUnits` casts at each point {x, y, 0} (spells without
flag 0x10000000 and a trap with points) or at each unit, with caster 0. `ForgetObject` removes every node equal to the
object (null included). The zone loader registers traps in no name map: scripts use `GetObject(id)`. `magic_traps.txt`
loads `traps.mob`, written by `tools/make_trap_mob.py` into the run's folder (a `# prepare:` line), with
`AddMob("..\saves\traps.mob")`: zone2's diplomacy table and its seven trap records renumbered 30001..30007 (zone2's own
ids collide with zone1's). AddMob's name is relative to the maps folder. A trap's side mask reads 1 at run time (the hero's side; the record holds -1) and
a unit that spawns beside it walks off before the first tick, so those two scenarios never put a
victim in a circle (`magic_traps_victims` passes without UpdateVictims ever finding one). `magic_traps_fire` moves the
circle to the hero's start with `make_trap_mob.py --circle X Y R` and fires a period 0 trap (units inside kept, dropped
by StaysInside when they die or leave) and a period 3 one (FireAtOccupants) at the hero and at wolves, disarmed and
armed again. The placed `TrapObject` (class 0x55) has no creator: no map record kind makes one (a kind 1, 5 or 8
record's `parentId` would make it as a `MapObject`, `Lever` or `Unit`), so its RunTick, Disarm and Save never run.

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9; C84 triaged the list, with a
recommendation per entry). Crashes, undefined behaviour and invisible bookkeeping are fixed, not listed.

- DamageSlot logs one line per damage type; ApplyHit wears the weapon by the part each damage type the protection
  absorbed.
- CastAtUnits returns without restarting the countdown when it finds no unit and changes the caller's list; a periodic
  trap whose countdown reached 0 and found nobody searches again every tick; FindAnimation's last fallback gets the
  kind without the movement substitution.
- The lever's ApplyKey disables the lever it has just switched.

By design (user 2026-11-03): IsBehind is never true without a player (NPCs never backstab players).

RaiseStrength raises the strength in use with its base before the maxima, so the slots scale to the new maximum (a
hero at full health stays there; a wounded one keeps the fraction).

## Ack kinds

`AckKind` (`ack_kind.h`) is the one enum of the codes in message 0x0B (`PlayerServer::SendUnitSpeech`, `UnitServer::Shout`,
`WorldServer::TellParties`; the client's `OnUnitSpeech` reads it back and `acks::SayAck` picks the list). The names are
the folders of acks.db's sounds (Acks\<unit>\<folder>.wav), read from the unit table's chunks (`kUnitChunks` /
`kUnitKinds`; the models table's 0x2E..0x32 are Agress / Suspect / Kill / Rest / InAgress). Where the code's old names
disagreed with the data, the data won: 0x0B is `NoWayToAttack` ("I cannot reach there"; 0x12 `NoTarget` is "where did he
go"), 0x0E `NoPath`, 0x0F `NoMana` (acks.db "CantCast") against 0x13 `SpellTooComplex` ("ComplSp"), 0x14 / 0x15
`Overloaded` / `Injured` (what UpdateSneak says when running is refused). 0x0C `OutOfReach` has no speech list in any unit
record (silent); 0x11 `ScienceFailed` is also what a quick item without a charge says. 0x16 and 0x18..0x1B have no list
either. `Shout` and the planner pass only the kinds up to `Bored` and `Kill` (`IsShoutable`); `None` (-1) is the
planner's "say nothing".

## Creation helpers

`MapObjectManager::NewObject`: a new object of class id `classId` (0x32..0x34 units, 0x35..0x3C the other map objects,
0x300D, 0x3010), constructed; null for other ids. `GiveStartingEquipment` gives `unit` the starting equipment of prototype
`name`, made by the world's item manager: a unit of type 0x32 equips each armour piece and gets the weapon (unless named
"none" or empty; the slot it lands in becomes the active weapon) and the second weapon; every unit gets each spell. A
piece that cannot be made gets a warning in the log (a weapon is added even then: AddWeapon(null) returns 0, so the
active weapon becomes 0). Without the prototype (a map's unit of a prototype the database lacks: Lost in Astral's
gz8g and gz17g) a warning and 0, the unit bare; else 1.

## CUnitClientModels per-tick update

The client controller's per-tick update: the motion order's times shift by one tick while its flag is 1, a counter runs
while another is 1, the weapon in hand is hidden during animations whose key has no low 15 bits (ShowWeaponParts); flags
8 (not drawn) for a unit dead for more than 50 ticks or without an animation, or in one of movement bits 0x30000 that is
not a mode transition (0x240000); the fade-in goes to the drawn parts and grows by 1/15 up to 1; the colour flash
countdown; the blink (every 2..9 ticks, rand()); the bow's arrow ("rh3.arrow00"); the step sounds; the speech queue.
Returns 1.

## StatChanged on the client

A hit on body location `location` (0 head .. 5 right leg) of strength `amount`; nothing for flag 8 or an amount up to
0.0001. The hit sound "weapons\<class>\<material>\1.wav" (class by the attacker's weapon: a kind below 0x10, else the
prototype's hitSoundKind; material: the model's hitMaterial, for heroes the first armour worn over the location as
"stone" (crystal, bone, stone), "Metal" or "leather") plays at 1000 from the unit; for a location's part ("hd", "bd",
"lh2", "rh2", "ll2", "rl2") of loaded figures, blood particles of the model's bloodKind (0x200F, 0x2031..0x2033) attach to
the unit, and blood kinds 1..4 leave a decal near the part (two rand offsets within the unit's size, a rand variant).

## Animation sounds

The sounds of the animation playing between the last update's frame (0 when the animation changed) and now: attacks
(stop the idle sound; at the action's moment the prototype's shake value shakes the camera; heroes play the miss sound at
1000) and deaths (stop the idle and speech sounds) and skills play ActionSound at priority 1000, idles and hits at 0;
run / walk (0x100000 / 0x140000) at a step frame play the footstep of the ground (unless silenced, effect 0x10) and,
when not crawling, shake and leave a footprint (part "ll<n>" / "rl<n>" by the step) through the client world. An
attack's sound waits for the speech sound and becomes it.
