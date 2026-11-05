# Particles and world effects

Code: `particle_manager.cpp`, `particle_create.cpp`, `particle_emitters.cpp`, `particle_callbacks.cpp`,
`particle_common.h`, `effects.h` / `effects.cpp`, `effects_weather.cpp`, `world_effect*.cpp`. Scenarios:
`particles.txt` (every source type it can make), `effects.txt`, `spells*`, `effects_bow`.

## Particle sources

- `ParticleSource` (class id 0xC, an engine object; made by `NewParticleSource`). `Create` resets the source, then fills
  the per-type parameters by `type` (0x2000-0x2052; the script names are a table in the game's data: name, type,
  lifetime): emission rates, lifetime, spread, sizes, the element array some types keep, the texture (kind 6, by name,
  one UV rectangle per frame of a grid of `frameColumns` columns) and three callbacks, member pointers to our code:
  `EmitCallback` (emit one particle), `UpdateCallback` (update one particle; 0 drops it), `ElementCallback` (update one
  element of the source's array). Callbacks of ~50 effects live in `particle_callbacks.cpp` / `particle_emitters.cpp`;
  the no-op element callbacks are one member, `TickNone`.
- `Tick` computes the source motion, runs the element callback per element and the update callback per particle
  (saving / restoring the "prev" fields), tracks the bounding box and emits up to `max(emitCount, distance moved /
  emitSpacing)` particles with `emitChance`% each. Particles are pooled.
- The types are `ParticleType` (effects.h): the script table's names, and descriptive ones for the types it lacks.
- Per-type data has one name per meaning: an element's fields through its type's view of `ParticleElement` (the views
  carry no padding and never alias each other), a particle's `velocity` / `aux` / `drift` parts through local references
  at the top of each callback, and the source's own `tint` (Tornado, CurseHolder ARGB; FeetCloud RGB), `rayCount`
  (Shower) and `glowKind` (ShapeChange, set by the path markers). The original kept these three in `frameDelay`,
  `lifetime` and `alpha`.
- `ParticleManager`: free particles, `held` (sources by id, one reference each), a quiet-start flag that stops
  CampFire, Fire and Smoke. `TickSources` ticks every source (paused: only `TicksWhilePaused`; a held source whose
  tick returned 0 is released); `ReleaseUnheld` releases held sources with no other reference (the client's zone
  load). `DrawAll` sorts the sources by depth and draws each through the renderer's sprite draw (models.md,
  "Renderer").
- The world's script particle source (class 0x4A, script `CreateParticleSource`) makes its `ParticleSource` on its
  first tick and every tick copies the position and calls `ScaleSize(size / baseSize)`.
- Floating point: `double` for values the original kept on the FPU stack, a store to float where it stored one; every
  "below" / "equal" test takes the unordered case the original's way.
- Random draws (all from `g_clientRandom`): the original stored word * k + lo with k = (hi - lo) * 2^-32; where k is
  exactly a float's (hi - lo) times 2^-32 the code calls `Range(lo, hi)`, the same value bit for bit (a check:
  `float(hi - lo) * 2^-32 == k` and the low end the same float). Seven scales carry a stray low bit (k is no round
  number's): they stay `WordToFloat(Next()) * kWordTo...` (`particle_common.h`, each with its bits asserted).

Kept:
- `Tick` unlinks and pools a particle the previous update rejected, then still updates it and counts it in the bounds.
- Tables read past their data where the original read zeros: Modifier markers by their element's tick (to 25),
  AcidRay's spread and size (to 16), ClayRing's alpha at the first ticks (12, 13), Modifier's fade (10), FireBlast's
  frame (to 16), TeleportRing's column alpha in the update after death; the tables carry the zeros or the reads are guarded.
- An update that counts the life down then reads its life table at index -1 (Shower, Casting, CastSwirl, Orbit,
  ZoneExit; ClayRing its countdown table): the original read the previous table's last entry; `ByLife` (effects.h) gives
  that value. FireBlast's flame frame can step to 16 (read as alpha 0); Link sparks are emitted on a frame up to
  frameCount, one past the grid (drawn with the first frame).
- `EmitLightning` never counts its bursts; CastSwirl reads its locals one slot off (an element gets sign * carrier
  radius instead of the size); Mushroom's spread is ln(1) = 0; Falling's direction spread is ftol(10 degrees) = 0;
  Teleport adds its alpha step as an unsigned sum capped at 255 (a fade out below 19 jumps back to opaque);
  LightningBlast sparks shift the alpha table by 22, not 24; Shower and FireBlast keep odd counters (see the code);
  Casting's swirl pick 0 is element 0, the effect's state, read as a swirl (its radius and size as x and y, z 0).

## Missiles and visibility

`EffectMissile` (abstract): arrived flag, target id, "from caster". `EffectArrow` (0x4E) embeds a particle, a point
light and a sound, then the sound name, light colour / range, particle type / size, caster id. `Steer` (shared with
class 0x5B, which doubles the speed 0.6667) aims at the target's position with z + above.z / 2 + center.z; arrived when
the planar distance is below one step (or NaN). On a client the missile is kept 1 above the world's surface height.
Tick moves, and on a client moves the parts. Start copies the settings into the parts (light velocity (0.0001, 0, 0),
flags 0x840, sound radius 30, looping) and starts them. Kept: Load and Unpack do not recompute the moving flag.

`EffectArrowFigure` (0x5B): the figure object (class 0x38, model "efar1", texture "arrow01" swapped for "", filed with
WorldClient::AddObject), the shooter, the DamageInfo the attack copies in. Tick: on the server, once arrived, the
target's ReceiveHit gets (hit, shooter, shooter); the shooter is looked up before the target is tested. On the client
the figure moves in the object grid and rolls along the velocity.

`EffectShowVisibility` (0x5E): observer, seer (vision fog 0x17 with caster and target), a counted fog object (client),
`FogGrid` (an origin and a `SearchGrid<uint8_t>` of marks; `AIMap::MarkVisibleCells` grows it). The server's Tick
follows the seer, recomputes the grid and marks the effect changed; Pack / Unpack send the grid; Start reveals it in the
fog (`VisibilityFog::Reveal`). Script casts have no caster, so only `spells_hero` reaches it.

## Sound sequences

The first name (when not empty) and the others go to `SoundManager::AddSequence` with a chooser for their count (ten
pickers); an update moves the sequence through SetSoundPosition with the sequence id. The chooser gets the number of
sounds started so far (`SoundSequence::PlayNext`).

## Moshka

`EffectMoshka` (100): the client world's bird, told to follow the session's leader. A figure (class 0x38, "unmobi" /
"bird00", lit, animation 0), a Bag particle source (scale 0.5, attached), the followed id, a timer, a circle angle, a
perch height and a state (0 perched 200 frames, 1 circling within 5 of the leader for 100, 2 flying to the perch for 150,
then Reset). Update (from the draw pass with the animation time) steps at most 5 frames, keeps the bird 0.5 above the
surface (platforms too), moves it at sqrt(sqrt(distance)) * 0.03 + 0.3 per frame and advances its animation. FindPerch
draws ISAAC angles until the AI map's line of sight is above 0.5. No scenario makes one.

## Tornado

`EffectTornado` (0x65): a Tornado particle source (client), a looping sound "nature\tornado\circle.wav" (near 10, far 40),
ticks left, the launch tick. Launch (from the server weather) aims at a random speed 0.096..0.216 per tick, ticks =
lrint(distance / speed). Tick moves it (source and sound follow), stops the source for the last 110 ticks, and on the
server every 8th tick above 20 hands each player-owned unit within 5 a hit of 5 (weight 1 on type 6) through its
controller's TakeHit. Start catches a new source up by at most 29 ticks. `zone_gz15h_tornado` sees the server launch tornados; only twenty minutes of game time (`zone_gz15h_tornado_client`,
17 s of a run) bring one within 100 units of the hero, the reach of a timed object to a player, so the client unpacks
it (Start, Unpack). Load and Save need a save made while one flies.

## Scenario notes (spells_hero)

- The server drops a blocked unit's orders, casts included, so `spells_hero` waits for the intro script's `BlockUnit`
  (flag 0x800000), lifts it and waits for the arrow to hurt either wolf. `ui_battle_spells` blocks the hero the same way.
- A party unit's spells come from its player's party record, not Unit::AddSpell, so GiveUnitSpell never reaches the
  hero's column. The scenario gives "spell container[...]" items (GiveItem) and puts them on in the camp's magic page
  (CanEquip wants the school skill, hence GiveSkill). "Or" is a Rebirth function (vanilla: "Parameter of type float
  expected").
