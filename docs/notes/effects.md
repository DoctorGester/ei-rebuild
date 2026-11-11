# Particles and world effects

Code: the particle core (part of the engine library: file-layout.md, "The engine library"): `particle_source.h`
(the types, the manager, the sources), `particle_manager.cpp` (the manager, attach, draw, tick), `particle_create.cpp`
(the types' names, Create), the data runtime (`particle_expr.*`, `particle_record.*`, `particle_data.*`: "Data
effects" below) and `particle_host.h`, what the core asks of the world (the ground, the platforms, the wind, the tick
fraction, the scorch marks, the carriers), answered on the client world by the game's `effects_particles.cpp`. The
game's: `effects.h` (the rain and snow, the visibility fog), `effects_weather.cpp`, `world_effect*.cpp`. Scenarios:
`particles.txt` (every source type it can make), `effects.txt`, `spells*`, `effects_bow`.

## Particle sources

- `ParticleSource` (class id 0xC, an engine object; made by `NewParticleSource`). `Create` resets the source, then
  sets it up from its effect's record (`effectdata::SetUp`: an original type's `type` 0x2000-0x2052 is its built-in
  file, "The original effects on data" below; a mod's its id): flags, emission, the starting values and the texture
  (kind 6, by name, one UV rectangle per frame of a grid of `frameColumns` columns).
- An unknown type (a mod's or a newer build's effect, from a peer's update or a save) makes no source:
  `ParticleManager::CreateSource` asks `ShownParticleType`, which logs "particles: unknown particle type 0x..., no
  effect shown" once per type and answers `kNoParticleType`, and gets null; `EffectParticle::Spawn` (also a wall's and
  an arrow's trail) then shows nothing. The world object stays, so the server's lifetime (`IsOneShotType`: none for
  an unknown type, the object lives until removed) and replication are unchanged. The original ended the game
  ("CParticle::Create(). Unknown ETypeID."), which an original client still would, but none gets a data effect's id
  ("In multiplayer and saves"). Check: `ei-convert --lua-check`'s `CheckUnknownParticleType`.
- Every effect runs on data (MF15g6): the tick calls `effectdata::TickData` (the effect's step), `UpdateData` per
  particle (0 drops it) and `EmitData` per try (0 returns it to the pool); "One tick" below.
- `Tick` computes the source motion, runs the effect's step and its update per particle (saving / restoring
  the "prev" fields), tracks the bounding box and emits up to `max(emitCount, distance moved /
  emitSpacing)` particles with `emitChance`% each. Particles are pooled.
- The types are `ParticleType` (particle_source.h): the script table's names, and descriptive ones for the types it lacks.
- `ParticleManager`: free particles, `held` (sources by id, one reference each), a quiet-start flag that stops
  CampFire, Fire and Smoke (by type id; never set). `TickSources` ticks every source (paused: only `TicksWhilePaused`; a held source whose
  tick returned 0 is released); `ReleaseUnheld` releases held sources with no other reference (the client's zone
  load). `DrawAll` sorts the sources by depth and draws each through the renderer's sprite draw (models.md,
  "Renderer").
- The world's script particle source (class 0x4A, script `CreateParticleSource`) makes its `ParticleSource` on its
  first tick and every tick copies the position and calls `ScaleSize(size / baseSize)`.
- Floating point: `double` for values the original kept on the FPU stack, a store to float where it stored one; every
  "below" / "equal" test takes the unordered case the original's way.
- Random draws (all from `g_particleRandom`, the particles' own client generator: core.md, "Random generators"): the original stored word * k + lo with k = (hi - lo) * 2^-32; where k is
  exactly a float's (hi - lo) times 2^-32 the code calls `Range(lo, hi)`, the same value bit for bit (a check:
  `float(hi - lo) * 2^-32 == k` and the low end the same float). The seven scales with a stray low bit are now
  `Below(limit) + lo` (the client's picture only; no frame is a golden).

Kept:
- `Tick` unlinks and pools a particle the previous update rejected, then still updates it and counts it in the bounds.
- Tables read past their data where the original read zeros: Modifier markers by their element's tick (to 25),
  AcidRay's spread and size (to 16; its file's tables), ClayRing's alpha at the first ticks (12, 13), Modifier's fade (10), FireBlast's
  frame (to 16), TeleportRing's column alpha in the update after death (its file's fade table); the tables carry the
  zeros or the reads are guarded.
- An update that counts the life down then reads its life table at index -1 (ClayRing its countdown table; the
  spheres', Shower's and the casting family's files): the original read the previous table's last entry; the files
  give that value. FireBlast's flame frame can step to 16 (read as alpha 0; its frame `mode = "free"`); Link sparks are
  emitted on a frame up to frameCount, one past the grid (drawn with the first frame).
- `EmitLightning` never counts its bursts; CastingIllusion ... CastingHealing read their locals one slot off (a light
  gets sign * carrier radius instead of the size; their files); Mushroom's spread is ln(1) = 0; Falling's direction spread is ftol(10 degrees) = 0 (its files);
  Teleport adds its alpha step as an unsigned sum capped at 255 (a fade out below 19 jumps back to opaque; its file);
  LightningBlast sparks shift the alpha table by 22, not 24; FireBlast keeps odd counters (its file), Shower its
  rays' age from -100 before they are chosen (a free Shower stops after 106 ticks; its file);
  CastingFire ... Divination's swirl pick 0 is the effect's state read as a swirl (its radius and size as x and y, z
  0; their files).

## Data effects

An effect written as data (MF15c; plan: the MF15 study, approved by the user 2026-11-11):
`Res\effects\<name>.effect.toml`, read through the mod layers and run by one C++ runtime inside the generic source tick
above. The end state (MF15g) is every original effect on this path and the per-type `Emit*` / `Update*` / `Tick*`
deleted: the original effects and a modder's use the same runtime, so nothing below is mod-only (all 83 types are
there since MF15g6: "The original effects on data"). Game code (and later Lua) drives an effect only through the
typed inputs it declares; the data maps them to rates, curves, stages, anchors and sub-effects.

Code: `particle_expr.*` (expressions), `particle_record.*` (the layers merged, the schema read, the effect's state
machine; no game header, so `tests/unit/particle_record.cpp` runs it alone), `particle_data.*` (the registry, the
particle side, the inputs). The guide for modders: docs/modding.md, "Effects".

### Files, names and ids

- The name is the file's stem (`showcase_aura` for `showcase_aura.effect.toml`), matched without case. Each layer's
  file of that name is read (`FindRecordFile(..., false)`: the TOML files alone) and merged field by field, the
  bottom one first (`MergeOver`): a table merges key by key, an array of tables (`[[stage]]`, `[[anchor]]`,
  `[[particle]]`, `[[on]]`) by its elements' `name` (`event` for `[[on]]`), matched to the first not yet merged, the
  others appended; an array of tables none of which has a `name` or `event` (an action list: `do`, `enter`,
  `on_death`) and any other value replace. `replace = true` ends the search as for every record file. So a mod
  retunes one particle kind of an effect with `[[particle]] name = "spark" lifetime = 20` alone.
- **Variants** (`like = "<effect>"`, MF15g5a): after the layers merge, a `like` takes that effect's tree (its own
  layers merged, its own `like` resolved first) and merges the effect's over it by the same `MergeOver`
  (`effectdata::ResolveLike`, particle_record.cpp; the registry's `MergedLayers` answers a base's layers, and the
  viewer's document resolves through the same function). The variant keeps its name, id and file; `like` is taken
  out before `Read`. A missing base, a `like` that is not a name and a chain that comes back to an effect already in
  it (`a -> b -> a`) are errors: the effect does not load. A mod's layer over a base reaches every variant of it
  (accepted, user 2026-11-11). The registry caches records, not trees: a base edited in the viewer (`Put`) reaches its
  variants only once its file is saved into an active layer and the registry resets.
  33 original effects are variants (the ones that differed only in values): CastingDomination / Enchantment /
  Healing on CastingIllusion, CastingElectricity / Acid on CastingFire, Falling2-4, Shower2-4, Modifier2-5 and 9 on
  Modifier1, 6-8 on Modifier5, 10-12 on Modifier9, AcidStar on FireStar, RikArrowSmoke on FireArrow, FeetCloud2,
  VisionStar3 / 4, PoisonCloud on PoisonFog, SphereAcid / Electricity and Antimagic on SphereFire, Cylinder2,
  GroundDust2. Their resolved trees are the old files' (values replaced in place, so vars keep their order), and
  effect_check holds unchanged. Left whole: CastingDivination (its marker kind must come before the spark: a new kind
  is appended), VisionStar2 and CampFire (new vars that must tick before the base's), Transform (it drops one of
  Mushroom's reactions; a variant cannot remove).
- The id: `0x40000000 | (FNV-1a 32 of the lower-case name & 0x3FFFFFFF)` (`IdOf`), apart from the original
  `0x2000` range. It is the source's `type`, and the world object's (`EffectParticle::type`): it travels in the delta
  field and saves as the original types do. Two names with one id: the second is left out with a warning.
- `effectdata::g_effects` (`Registry`): `Find(name)` loads an effect at its first use; `FindById(id)` lists
  `Res\effects` through the layers (`FilesByLayer`) at the first id it has not met (a peer's update, a save).
  `Reset` at `LoadDatabases` (a switch of mods). Each problem is a `warning: effects: <file>:<line>: ...`; an
  unknown key is logged and the effect loads; an expression that does not compile, an unknown name or a record
  without a particle kind makes it unusable: it shows nothing, as an unknown type does (`ShownParticleType`).
- The server reads `[server]`: `one_shot` (the world object lives that many ticks; `OneShotTicks` beside the
  original's 60 / 120) and `script_lifetime` (CreateParticleSource's, as the script table's 200 or -1). Script
  `CreateParticleSource` takes a name its table lacks from the registry; sub-effects (`effect = ...`) name a data
  effect or an original type by its ParticleType name (`ParticleTypeByName`).
- Res\effects counts in the mod's content hash (`modhash::Counted`, mods.md "Mods in multiplayer"): peers with other
  effect files are refused, since `[server]` decides when the world objects go.

### In multiplayer and saves

- The id is the only reference: the server's world object carries it in the int an original type's number takes (the
  delta field of an `EffectParticle`, `EffectWall` and `EffectArrow`, and their saves), and every peer resolves it
  in its own registry (`FindById`); no name and no format change. A loaded save finds the effect the same way.
- Every peer has the same effects: Res\effects counts in the mod's content hash, so our peers with other effect files
  are refused at the join (network.md, "Mods"). An id a peer still does not know (loose effect files in an install
  without a mod, outside the hash) logs once and shows nothing (`ShownParticleType`).
- **No fallback for original clients.** An original client cannot receive a data effect's id: a server that plays a mod
  refuses unmarked clients at the join (network.md, "Mods"), and the original game's own content uses only the
  original numbers. So effects declare no fallback type and the server writes the same int to every connection (the
  study's per-connection fallback was dropped, user rule: one uniform path). When the original effects move to data
  (MF15g) they must keep their 0x2000 numbers as their ids on the wire, since a server without a mod admits original
  clients.
- Checks: `net_pair_mod_effect` (both peers on the showcase mod, in z3q2: the host's server starts `showcase_aura` by
  name with the harness's `serverscript`, both clients run it, stages by ticks, nothing logged unknown; capture
  `peer_effect`), `mod_effect_save` (started, saved, loaded: the source made again from the id, caught up into glow).

### The schema

Top level: `texture` (an Effects texture, kind 6), `frames` (16), `columns` (4), `blend` (`alpha` | `additive`),
`attach` (`free` | `attached_only`), `part` (`effect_point`, `head`, `above_head`, `left_hand`, `right_hand`,
`left_leg`, `right_leg`, `body`, `body00`..`body04`: the source sits on that part of a unit), `flags`
(`ticks_while_paused`, `idles_off_screen`, `keep_alive`: a free source lasts without particles until it ends, else
only while it has some, as every original type). Tables: `[server]`, `[source]` (`size`,
`height`, `growth`, `alpha`, `param`, `spread_min`, `spread_max`, `extent`: the source's starting values, which
`ScaleSize` and the world object scale as an original type's), `[emit]` (`rate`, `chance`, `emitting`, `spacing`,
`max`, `once`), `[inputs]`, `[vars]`, `[[stage]]`, `[[on]]`, `[[anchor]]`, `[[particle]]`, `[tables]` (name = list of
numbers), `[curves]` (name = `[[t, value], ...]`, linear, held at the ends).

- **Inputs** (`name = { type, default, values }`): `float`, `vector` (read as `name.x`, `.y`, `.z`), `bool` (0 / 1),
  `event` (its value is the times it fired before this tick, for one tick), `enum` (`values` listed; its value is
  the index, `name.value` the constants). Game code: `effectdata::SetInput(source, name, float | Vector3 | value
  name)`, `effectdata::Fire(source, event)` (particle_data.h); the harness's `effect` step does the same. `list`:
  points (`default = [[x, y, z], ...]`, empty without one), read as `name.count` and `name.x[i]`, `.y[i]`, `.z[i]`
  (truncated, held to the ends as a table's entries; 0 while empty); `SetInput(source, name, std::vector<Vector3>)`.
- **Vars** (`name = { start, tick }` or a value): the effect's own state. `start` at the first tick, `tick` from the
  second, in the file's order, each seeing the ones before it already changed. `at_create = true`: its start runs when
  the source is made (from the record's starting values; Falling's count of gusts, drawn as the original drew it;
  the Tornado's colour, the sun's as the source is made).
- **Stages** (`[[stage]]`, the state machine; the first is entered at the start): `ticks` (its length from its entry)
  then `next`; `until` (a condition) then `next`; `on = { event = "stage" }`; `next = "end"` ends the effect. While it
  lasts its `rate`, `chance`, `emitting` stand over `[emit]`'s; `enter` is a list of actions run at its entry. One
  move per tick; moves made by the tick's actions count.
- **Reactions** (`[[on]] event = "start" | "stop" | "tick" | "emit" | "before_emit" | "spawn" | <event input>`, `when`,
  `do = [actions]`): "spawn" once when the world's effect object makes the source (`EffectParticle::Spawn`, after its
  catch-up ticks: `effectdata::Spawned`; a script's or game code's own sources get none), its actions applied at once,
  no emission among them (FireBlast's and LightningBlast's scorch marks and shake); "stop" when the game stops the
  source's emission (StopEmitting, Stop); "tick" every tick (after the event
  reactions, before the stage's move); "emit" after each particle the emission makes, kept or dropped, its actions
  applied at once (the emission's later tries and particles see them; it sets, adds anchors, shakes, stops, lets go or
  ends, and emits a kind: `{ emit = n, kind = ... }` makes those particles right after it, before the emission's next
  try, `p.index` counting them from 0, with no reactions of their own and within `max` (PoisonFog's grid of puffs at its
  first particle); it refuses a kindless emit, stage and effect); "before_emit" the same before each particle (of the
  regular emission: after its try's chance roll, before the kind is chosen; and of the actions' particles), so a
  particle sees what it did (Shower's rays, made at the first particle). `when` (a condition) runs the reaction only
  while it holds.
- **Actions**: `{ emit = n, kind = "<particle>", at = "<anchor group>" }` (n particles of the kind, at each live anchor
  of the group; without `kind` n more tries of the regular emission this tick, which then emits only while the source
  emits), `{ set = "<input>" | "<var>", value = ... }`, `{ stage = "<name>" | "end" }`, `{ effect = "<name>" }` (another
  effect started at the source's origin, free, on its own), `{ end = true }`, `{ stop = true }` (the source stops as the
  game's Stop stops it: no emission, not kept alive, its carrier let go; the effect goes on and the game may start it
  again), `{ stop_emitting = true }` (the emission flag cleared, as the game's StopEmitting without its KeepAlive: the
  game may set it again), `{ let_go = true }` (the source lets go of its carrier and stays where it is, its flags
  unchanged), `{ add_anchor = "<group>", count = n }` (n anchors of the group made now, 1 without a count; each as one
  made at the start: "Anchors" below), `{ shake = <strength>, time = <ticks>, decay = <factor> }` (the camera shaken
  about the source's origin, `Camera::Shake`, when the tick's actions are applied; Mushroom's burst), `{ scorch =
  <radius>, alpha = <0..1, default 1> }` (a scorch mark on the ground at the source's origin, `World::AddScorchMark`,
  turned by a draw of the client's own generator, `g_clientRandom`, and its alpha held to 1). After a stop or a
  let go the source's `attached`, `origin` and carrier values are written again, so the tick's particle updates and
  emission see the source free. `set` names an input, a var or one of the source's values `size`, `height`, `growth`,
  `alpha`, `param`, `spread_min.x/y/z`, `spread_max.x/y/z`, `extent.x/y/z` (written back to the source after the step,
  so the next tick, ScaleSize and the particles' fields see it; Banshee keeps its ring's speed in `height`).
- **Anchors** (`[[anchor]]`): a group of points the source owns (rings, orbs, bolt points, swirls), `count` of them
  made at the start (0: only `add_anchor` makes them; `at_create = true`: when the source is made, before the first
  tick, which then steps them: FireBlast's fragments, drawn as the original's set-up drew them): `spawn` (a shape as a
  particle's, below, drawn first when one is made: `a.spawn.x/y/z`), `ticks` (each one's life; none: it lasts), `vars`
  (per anchor), and its position relative
  to the source's origin, `x`, `y`, `z` or `radius`, `angle` (x = cos(angle) * radius, y = sin(angle) * radius), `z`.
  `a.index` is its number in the group, `a.count` the group's anchors made so far (all of the group's are told when
  more are added). An anchor that ends stays readable (its last values) for the particles it carries.
- **Particle kinds** (`[[particle]]`): the regular emission takes the first kind whose `when` holds (none: always;
  `false`: only by actions). `lifetime`, `vars` (at most 8; `{ start, tick, after_spawn = true }`: its start runs after
  the spawn shape draws and may read `p.spawn.x/y/z`, the shape's point; the ticks run in the file's order, each seeing
  the ones before it already changed), `at` (an anchor group: it starts at one of its live anchors, `pick = "cycle" |
  "random" | "first"`, or `anchor_index` (an expression: the live anchor of that number, wrapped, chosen after the vars
  and before the spawn shape; the lifetime and the vars before the spawn may not read `a.*`)), `space` (`world`: moves
  on its own; `source`: its offset rides the source; `anchor`: rides its anchor), `spawn = { shape = point | box | ball
  | shell | disc | ring | flat_ball | segment, radius, min, max (a box's; else the source's spread), from, to (a
  segment's; else 0 and the source's extent: one draw t, from + (to - from) * t), offset = [x, y, z], on_ground (z = the
  ground's height plus it) }`, `position = [x, y, z] | { start, tick }` (the start: the particle's place instead of the
  shape's point plus offset and centre, on_ground still applied; the tick: its place each tick instead of the move by
  its velocity, the offset in `source` and `anchor` space; the three read this tick's values before any changes),
  `velocity = [x, y, z]`, `speed` (outward from the spawn's centre: offset * speed / its length), `drag` (a factor per
  tick), `gravity = [x, y, z]`, `wind` (times the world's wind; each tick the particle moves by its velocity, then drag,
  gravity and wind change the velocity for the next tick, the originals' order; a world-space kind with none of
  velocity, speed, gravity, wind stays put), `size = value | { start, tick, growth }`, `color = "RRGGBB" | [r, g, b]`
  (expressions, each tick), `alpha = value | { start, tick }` (0..255), `frame = value | { start, delay (ticks per
  frame, 0 or none: still), mode = loop | hold | die | free, tick }` (a start past the grid stays: the update's step
  wraps or holds it by the mode, `free` keeps it, and it draws as the first frame meanwhile, as Link's `pick(16)` of
  15; until MF15g4 the start was held to the last frame, a change of the schema's behaviour), `kill` (it dies once
  this holds), `drop` (it holds at the end of the emission: the particle is not kept, its draws made; the "emit"
  reactions still run), `source = { size =
  ..., ... }` (each update sets these source values after the colour and before `kill`, in order; the particles after it
  and the emission read them: Regeneration's shrinking source; an effect var by its name, and `a.<var>` a var of the
  particle's anchor: Silence's orb size), `jump = "<when>" | { when, size }` (after the update, where it holds, the
  previous place and colour are the new ones and the previous size `size`'s value, else the size: the particle is
  drawn at its new place, not moving from the old; Bag's thrown balls), `on_death` (actions at its death, the first only: the
  update a dead particle still gets fires nothing; `{ emit = n, kind = ... }` queues n particles of the kind whose
  spawn's centre is its place, emitted at this tick's emission, emitting or not; `{ effect = ... }` starts one there at
  once; `set` an input or a var; the counts and values may read `p.*`; nothing else). `flat_ball` draws x and y in [-1,
  1) until y * y + x * x < 1 (ten tries, the last kept), z 0, times `radius`. `ball` and `shell` take `tries` (10: the
  draws of the cube before the last is kept; 0: until 0 < length squared <= 1, CastingFire's) and `shift = [x, y, z]`
  (added to each draw of the cube before its test, so the ball is the shifted one: FireBlast's fragments, up 0.7; an
  anchor's spawn takes it too).

### Expressions

A number, `true` / `false`, or a string compiled once at load (`particle_expr.h`): numbers, names, `+ - * / %`,
comparisons, `!`, `&&`, `||` (both short), `a ? b : c`, functions `sin cos atan2 sqrt abs floor ceil round min max clamp
lerp pow exp log`, `ground(x, y)` (the ground's height there, as `on_ground` reads it; 0 in the unit checks and in
effect_check's empty terrain), `platform_height(x, y)`, `platform_normal_x(x, y)`, `_y`, `_z` (the plane of a platform
there, `PlatformSet::SurfacePlane`: 0 and the normal (0, 0, 1) where there is none; GroundDust), `surface(x, y)` (the
height of what stands there, a platform over the ground or the ground, `PlatformSet::SurfaceHeight`: ShapeChange's
glows; the ground without a platform in the unit checks), random `range(lo, hi)`
(IsaacRandom::Range), `below(k)`, `chance(percent)`, `pick(n)` (Next() % n), a curve `name(t)`, a table `name[i]`
(truncated, held to the ends), a list input's `name.x[i]`, an anchor group's values `<group>.<var>[i]`,
`<group>.x[i]`, `<group>.age[i]` ... (its anchor number i in the order made, truncated and held to the ends; 0 without
one), `ball_x()` (a point drawn in the unit ball, `effectdata::RandomInBall`; its x) with `ball_y()`, `ball_z()` (the
last drawn point's y and z). Constants `pi`, `deg`. Names, by where the expression runs:
- everywhere: the inputs, the vars, the source's values (`size`, `height`, `growth`, `alpha`, `param`, `base_size`,
  `age`, `stage_age`, `stage`, `carrier_radius`, `carrier_z`, `attached`, `distance`, `motion.x/y/z`, `origin.x/y/z`,
  `count`, `index`, `total`, `wind.x/y/z`, `wind_strength`, `extent.x/y/z`, `turn_cos`, `turn_sin`, `emitting`: the
  source's emission flag, which the game's StopEmitting and Stop and an effect's `once` clear, `spread_min.x/y/z`,
  `spread_max.x/y/z` (the spread box, scaled by ScaleSize), `sun.r/g/b` (the scene's sun colour, 0..1; 0 without a
  sun), `side.x/y/z`, `up.x/y/z` (the motion's side and up axes this
  tick, zero while the source moves 0.001 or less: the trails' across directions), the carrier as a unit:
  `carrier_unit`, `carrier_running`, `carrier_dead` (0 / 1), `carrier_unit_radius` (the unit's own radius, 0 else), `carrier_axis.x/y/z`, `carrier_axis_y.x/y/z` (its own x and
  y axes; 0 when free), `carrier_bounds_radius` (its bounds' radius as it stands, not measured again),
  `carrier_left_foot.x/y/z`, `carrier_right_foot.x/y/z` (its "ll3" / "rl3" parts, else its body, else its position
  raised by its bounds' top, as particlehost::WriteCarrier finds a part; the origin without a unit carrier); written at the tick's
  start only for an effect that reads one of them), `anchors.<group>` (live count), `stage.<name>`;
- in an anchor's and its particles' fields: `a.index`, `a.count`, `a.age`, `a.ticks` (left), `a.x/y/z`,
  `a.spawn.x/y/z`, `a.<var>`;
- in a particle's: `p.age`, `p.life`, `p.t` (age / (age + life)), `p.x/y/z`, `p.vx/vy/vz`, `p.size`, `p.alpha`,
  `p.frame`, `p.anchor`, `p.index` (the values before this tick's update), `p.spawn.x/y/z` (at the emission; 0 in the
  update), `p.<var>` (as the vars' ticks left them). A particle var may not take one of these names (`age`, `x`,
  `life`...): a load error, since `p.life` would read the var.
A name the context lacks is a load error. Every operation is a float operation in the written order, so the draws come
in the written order too; an emission's parts draw in the order lifetime, vars, the shape, the vars after it, the
offset or position, velocity, size, colour, frame. Every draw is `g_particleRandom`'s.

### One tick

`TickSource` → `TickData`: the source's values written (the carrier's radius and centre measured at an expression's
first read in the tick, once: measuring recomputes the carrier's bounds, so an effect that reads them behind a
condition, `first ? carrier_radius : 0`, measures only when it holds), `State::Step` (events of this tick, the start's work or the
vars' and anchors' ticks, "stop" reactions, event reactions, "tick" reactions, the stage's move, the tick's rate /
chance / emitting), the `set` source values written back, a `stop` action's Stop, a `stop_emitting`, a `let_go`
(`ApplyActions`), then the source's `emitCount` and `emitChance` set for the generic
emission (not emitting: no tries, none from the movement either; the spacing is the source's, `[emit] spacing` scaled
by ScaleSize as an original type's) and the started sub-effects made. `UpdateParticles` → `UpdateData` per particle
(life, frame, vars, the move, then the velocity's change, size, colour, the `source` writes, kill). A `die` particle past its last frame
ends at the frame step, before it moves and before its frame timer starts again, as the originals' (so the update a
dead particle still gets, "Kept" above, steps no frame and may move it, as theirs). `Emit` → `EmitQueued` (the actions'
particles, emitting or not), then the generic tries → `EmitData` (with `once`, the first particle clears the
emission flag; the game may set it again; after each particle kept or dropped the "emit" reactions and
`ApplyActions`; the "before_emit" reactions and `ApplyActions` before each, after the try's roll). A particle's
first death runs its `on_death` (`UpdateData`): the particles join the queue, which `EmitQueued` empties at the
same tick's emission. A data source with `keep_alive` lasts until it ends (`next = "end"`, an
`end` action, the game's Stop), then while it has particles; without it, as an original type: a free source while it
has particles (or the game's StopEmitting / StartEmitting set KeepAlive), an attached one while it emits or has
particles. A particle's life counts down before its update and it dies at 0 (an original that dies below 0 gives its
kind one tick more of `lifetime`). Per particle: `Particle::age`, `kind`, `vars`; `state` its anchor, `drift` its
offset in `source` / `anchor` space.

### The original effects on data

FireStar, AcidStar, CurseStars, PortalStar (MF15d), Sparks, FeebleMind, ZoneExit and the trails FireBall, FireArrow,
RikArrowSmoke, AcidRay (MF15g), the smoke family CampFire, Fire, Smoke, Tendril, then VulcanSmoke, Portal,
TeleportRing, Teleport, Banshee, BlueGas and Nuke (MF15g2), the carrier readers Stun, Regeneration, VisionStar1-4,
FeetCloud1 / 2, StartTrans, Falling1-4, SphereAcid, SphereElectricity, SphereFire and Antimagic (MF15g3), Shower1-4
(MF15g3b), PoisonFog, PoisonCloud, Mushroom, Transform, GroundDust1 / 2, Link, Modifier1-12, ClayRing and Cylinder1 / 2
(MF15g4), Casting, Healing, CastingFire, CastingElectricity, CastingAcid, CastingDivination, CastingIllusion,
CastingDomination, CastingEnchantment, CastingHealing, Silence and Bag (MF15g5), FireBlast, LightningBlast, Geyser,
Tornado, CurseHolder and ShapeChange (MF15g6): all 83, from `assets/effects/<Name>.effect.toml`, built into the
executable (src/engine/particles/embed_effects.cmake writes them as bytes at build time; `effectdata::BuiltinText`). The
C++ emitters, the spec table (`kSpecs`), the per-type dispatch and the elements' views are gone (MF15g6).
- **The built-in file is the bottom layer.** The registry reads it below the mod layers' files of the same name
  (`Res\effects\FireStar.effect.toml` in a mod retunes the field it names), unless a mod's file says `replace = true`.
- **The id is the original type** (`ParticleTypeByName`; 0x2041 for FireStar), so the world objects, the delta field,
  the saves and the game code that names the type (`ParticleType::FireStar` in the spell impacts) are unchanged.
  `FindById` answers an original type's id from its built-in file under the mods' layers.
- **What the C++ tables held moved into the file**: `[server] one_shot` (FireStar, AcidStar, Sparks, Banshee, Nuke,
  Shower1-4, Mushroom, Modifier1-12, ClayRing, Cylinder1 / 2, Casting, Healing: 60; Teleport 120;
  `OneShotTicks` reads the record), `script_lifetime` (FireStar, AcidStar: 20; Sparks 60; Teleport, Nuke, StartTrans,
  Mushroom, Transform, Modifier1-12, ClayRing, Cylinder1 / 2, Casting, Healing: 200; the others -1, the default;
  the script table `kScriptParticleTypes` is gone, and CreateParticleSource finds the name in the registry), the spec's
  texture, flags, part, rates, limits and starting values (`[source]`: ZoneExit's alpha and param 5, the trails' size,
  growth and spread).
- **Moving a type** (MF15e-g): first add its cases to `tools/convert/effect_check.cpp` and record them from the C++
  (`ei-convert --effect-check --record tests/effects/reference.txt`: only the new cases' lines may change in the diff);
  then write its file, run the check until it holds (`--dump <file>` writes every tick's particles; dump both builds
  and diff), delete its C++ and spec entry, and move its server and script values as above. Where the data needs an
  engine feature to match, it is added to the runtime generally (MF15d's four needed `[[on]] event = "tick"` with
  `when`, the `stop` action, `emitting`, `once` clearing the flag, `keep_alive` off by default, the move before the
  drag, `speed` scaled as `offset * speed / length`, no additions of zero to a still or unplaced particle; MF15g's
  seven `position` start and tick, vars `after_spawn` with `p.spawn.*`, 8 vars, the spread and the side / up axes as
  source values, the frame step first with a `die` particle's timer left run out; MF15g2's `wind.z`, `exp` and `log`,
  `set` on the source's values, `stop_emitting`, the carrier measured at its first read, the shadowing var names
  refused; MF15g3's carrier unit values, particle `source` writes, "emit" reactions, `set` on a var, `let_go`,
  `drop`, `anchor_index`, `flat_ball`, `at_create` vars, the attachment's values written again after a stop; MF15j's
  "before_emit" reactions, `add_anchor`, an anchor's `spawn` with `a.spawn.*`, and the shared ball draw
  `effectdata::RandomInBall`, for Shower; MF15g4's kinds emitted by "emit" reactions, the `shake` action, the
  `platform_*` functions, `carrier_axis_y`, the spread and extent settable, the frame start past the grid, and TOML
  integers read exactly, so `max = 2147483647` is not rounded through a float; MF15g5's `ball_x()` / `ball_y()` /
  `ball_z()`, a spawn's `tries`, an anchor group's values by number `<group>.<var>[i]`, particle `source` writes to
  effect vars and to the anchor's vars `a.<var>`, a kind's `jump` and `carrier_unit_radius`; MF15g6's "spawn" reaction
  and `scorch` action (MF15j1's rest), `at_create` anchors, a ball's `shift`, `surface(x, y)`, `sun.r/g/b` and the
  frame mode `free`).
- **Exactness rules found**: an expression's operations are float operations in the written order, so
  `(below(0.4) + 0.8) * size * 0.04` is the C++'s `(Below(0.4f) + 0.8f) * s.size * 0.04f` bit for bit; a draw the C++
  makes before the position goes into a var (vars draw before the position: CurseStars's `cell = "pick(16)"`, which
  is `Next() & 15`); a C++ update that returns before drawing when it drops a particle needs the draw behind a
  condition (`emitting ? ... : p.size`) and the drop as `kill`; a C++ life that dies below 0 is `lifetime + 1` with its
  table read at `p.life - 1`. A draw the C++ makes after the spawn shape's (Sparks's part of the first step after its
  direction) is an `after_spawn` var. C++ arithmetic on the position (`pos -= v; v *= 0.95f; pos += v`) is a `position`
  tick written as that sum (`p.x - p.vx + p.vx * 0.95`, with `drag = 0.95` for the velocity), and a place built in
  another order (ZoneExit's ellipse, the trails' `origin + t * motion + velocity`) a `position` start. A count the C++
  sets in its emission (`s.emitCount = ...`) is the count of the last tick that emitted: a var `tries` taken when
  `total > seen`, from the values `seen_*` vars kept at that tick (FeebleMind, ZoneExit). A test the C++ makes on the
  stepped frame is `p.frame + 1` where the delay is 1 (the arrows' swelling); the first-ever particle is
  `total == 0`, a tick's first `index == 0`. A draw the C++ makes only on one path goes behind the same condition
  (`moving ? range(...) : 0`).
- **MF15g2's rules**: a C++ particle that dies by `--life == 0` but whose update in the tick after its death still
  moves it ("Kept") is a long `lifetime` with `kill = "p.age == <its end>"` and every change of state the dead update
  must not see (a var's step, TeleportRing's `drag = "p.age == 13 ? 1 : 1.05"`) guarded on that age, so the dead update
  runs as the C++'s (the smoke family, TeleportRing, Teleport). The vars tick in file order and the frame tick before
  them, so a var that must read another's old value comes first. Velocity changed by `drag` after the move is
  per-particle storage that grows by a factor (Portal's rise, `drag = 1.03`). A C++ effect that sets itself up at its
  first emitting tick (Banshee, TeleportRing, BlueGas) is vars `first` and `set_up` with the carrier read only behind
  `first`; a C++ try that is rejected but draws is a `pick(1)` (one draw, value 0) in a var on that tick. Two kinds
  by a chance are `when = "chance(60)"` on the first (VulcanSmoke). A C++ count set in its tick (Nuke's tries, halved to 8) is
  a var, a power (Nuke's alpha start, 4 to the tick) `exp(log(4) * ticks)`, and the C++'s own end of its emission a
  `stop_emitting` reaction (Teleport, Banshee, Nuke).
- **MF15g3's rules**: a count the C++ sets in its emission is a var `set` by an "emit" reaction (Stun's 4 or 1,
  FeetCloud's size * 8); a C++ emission that draws and then refuses is a `drop` (Stun without a unit), and its Stop
  there an "emit" reaction's `stop`. A C++ update that writes the source (VisionStar's zeroed size, Regeneration's
  shrink) is a particle `source` write, computed into a var first when the particle's own size or kill needs it.
  Work the C++ does once a tick at its first emission and that draws nothing (the spheres' follow) moves into the
  tick's vars under the condition that a try is made (`emitting && attached && tries > 0`: a source above 50%
  emits at least once when it tries), the particles' updates reading the values from before it (`last_*` vars).
  The C++'s per-particle values beyond the eight vars go into `velocity` (FeetCloud's spread, slowed by `drag`; a
  `position` tick leaves the velocity unused). A part of the way kept a tick is a var that holds the factor for this
  tick (`p.age == 1 ? p.ease : p.ease * 0.98`). A C++ update that returns before a change on its last tick
  but makes it in the update after its end needs the change guarded on that tick (the spheres' turn,
  `p.l == 0 ? p.shell_x : ...`). A C++ draw made when the source is made (SetupFalling's gust count) is an
  `at_create` var.
- **MF15g3b's rules** (Shower): state the C++ makes inside its first emission, after that try's roll (the rays), is a
  "before_emit" reaction under the condition that it is not made yet (`anchors.ray == 0`), here an `add_anchor` whose
  anchors draw their `spawn` ball; directions kept as `v / sqrt(lengthSquared)` are `a.spawn.x / sqrt(a.spawn.x *
  a.spawn.x + ...)`, the same float operations as `LengthSquared`. A C++ element age that counts every tick and is
  reset there is a var (`ray_age`, set to 0 by the reaction). The per-particle values beyond the vars' order are split
  by when they draw: the jitter before the anchor pick (plain vars), the direction, the speed's one-in-eight and the
  first step's part after it (`after_spawn` vars, which may read `a.*`).
- **MF15g4's rules**: a C++ emission that lays out a whole grid at its first particle (PoisonFog's width x width
  puffs) is an "emit" reaction with `when = "count == 0"` emitting that many of a kind, which reads its cell from
  `p.index`; the first particle itself a kind that is dropped. The C++'s element state (Modifier's ring points,
  ClayRing's ground points, Cylinder's stars, Link's beam frame) is anchor vars or effect vars; a value two vars need
  in its old form is kept first in a helper var (`ox`, `odx`, `or0`), since the ticks run in order, each seeing the
  ones before it changed. State the C++ sets up at the first particle (Link's frame, Cylinder's stars) is a
  "before_emit" reaction under `made == 0` or `anchors.<group> == 0`. A countdown the C++ keeps in element 0 and that
  drives the whole effect (ClayRing's, Cylinder's) is one effect var; a table index the C++ reads past its end (the
  value of the table after it) is the value it gives (0 alpha). Modifier's retry of a projection of length zero is
  left out (it practically never happens); PoisonFog's grid is laid about the origin where the C++ took the local
  position, the same for the free source the spell makes.
- **MF15g5's rules**: state the C++ keeps per element and a particle reads from another element (the swirl a
  CastingFire spark is pulled by, the first light's orbit a later CastingIllusion light takes, the Bag ball a spark
  sits on) is anchor vars read by number, `swirl.px[k]`, `orb.rx[0]`. A C++ ball draw whose three values feed three
  places (Bag's kick, CastingFire's swirls) is `ball_x()` (the draw) then `ball_y()`, `ball_z()` (its other two); an
  unbounded draw loop is `tries = 0`. A C++ update that writes its element (Silence's orb size, which the wisps read)
  is a `source` write to `a.<var>`; one that writes the effect's state (Casting's emission phase) an effect var. A
  particle the C++ moves without a trail (Bag's balls thrown again) is a `jump` with the previous size it shows. A
  carrier's own radius the C++ asks of a unit (CastingFire's three times it) is `carrier_unit_radius`, 0 for a stone.
  Two changes: Casting's restarted close sparks wrap their frame at 16 where the C++'s grew without a bound (past 15
  updates of a restart, which no script makes); Silence's wisps read their orb's last
  step, which the original read from the particle's pool slot before the orb had made one (a stale value of whatever
  used the slot before): a new orb's step is zero here, a fix by judgement (MF15g5; unseen in effect_check, whose
  pool is fresh).
- **MF15g6's rules**: what the world object did once it made a source of one type (FireBlast's and LightningBlast's
  scorch mark, the lightning's shake) is the file's "spawn" reaction, which draws the mark's turn from the client's
  generator where the old code drew it; elements the C++ set up when the source was made (FireBlast's fragments) are
  `at_create` anchors, a ball draw it shifted before the test (`p + {0, 0, 0.7}`) a spawn `shift`. A value game code
  set on the source for one type (ShapeChange's colour in `glowKind`, the path in `typeParam` and the walker's future
  places read by the emitter) is an input the game sets (`kind`, an enum; `path`, a list of the points:
  `PlayerClient::OnPathMarker`); a colour the C++ read when the source was made (the Tornado's, the sun's) an
  `at_create` var on `sun.r/g/b`. The funnels' per-particle state takes all eight vars, every tick guarded at the
  death age ("Kept": `p.age == p.l0 ? p.ang : p.ang + p.sp`); their rings are anchors whose vars read the previous
  tick's ring 0 (`ring.rx[0]`). `min(a, b)` is the C++'s `b < a ? b : a` and `max(a, b)` `b > a ? b : a`, so a C++
  `std::min` / `std::max` keeps its operands' order in the expression. A C++ integer division (ShapeChange's `order * 8
  / count`) is `floor(...)` of the float one. A frame the C++ stepped past the grid in a particle's last update
  (FireBlast's flames, 13 to 16) is `mode = "free"`. One change, by judgement: ShapeChange's glow read its alpha table
  at its life left, which a path with repeated points (counted in the order, no glow made) takes past the table's 16
  entries; the original read the data after it and our C++ whatever the compiler put there, so the file holds the last
  entry (fully lit). Its three cases with repeats were recorded from the data build after `--dump` showed the only
  differences were those glows' alpha (`effect_check`'s PathStone repeats every point three times).

### The original state machines on inputs (MF15g's path)

The 14 behaviours (34 types) the study left as code become anchors, vars, stages and kinds, with what game code
decides today as inputs. Healing and Casting (rings of flames rising around the carrier) are on data since MF15g5
(assets/effects/Healing.effect.toml, Casting.effect.toml: ring anchors, the emission's phase an effect var advanced
by "emit" reactions).

The others, by what drives them:

| Behaviour | Anchors / vars / stages | Inputs game code drives |
|---|---|---|
| CastingFire ... Divination (swirls) | on data (MF15g5): 5 swirl anchors random-walking, the fade an effect var | |
| CastingIllusion ... Healing (orbiters) | on data (MF15g5): one light anchor added per emission | |
| Cylinder1 / 2 | on data (MF15g4): 2 star anchors added at the first particle, a countdown var | |
| Modifier1-12 | on data (MF15g4): ring-point anchors; the direction still the size's sign | |
| Link | on data (MF15g4): the beam's frame from `extent` (game code still sets the extent) | |
| Silence, Bag | on data (MF15g5): orb anchors on a tilted circle; 80 ball anchors kicked (vars), colour by tables | |
| FireBlast, LightningBlast | on data (MF15g6): 15 fragment anchors made with the source; ten bolt-point anchors added at the first particle; the scorch and shake a "spawn" reaction | |
| ClayRing, Geyser | on data: ten ground-point anchors on `ground` (MF15g4); three vent anchors with eruption / pause vars (MF15g6) | |
| Tornado, CurseHolder | on data (MF15g6): 6 funnel-ring anchors leaning with `motion.x/y`; the colour `at_create` vars from `sun.r/g/b` | |
| ShapeChange | on data (MF15g6): one glow per path point | `path` (a list input), `kind` (an enum: green, grey, orange) |

The runtime has what they need from MF15j: `ground(x, y)` (anchors on the ground, kill planes), `add_anchor` (with
"before_emit", CastSwirl's orbiters), `pick = "first"` and `anchor_index`, a `list` input (ShapeChange's path),
`on_death` (a particle's sub-emission or sub-effect), the `segment` shape (Link). `shake` came with Mushroom (MF15g4).
Still to add: inputs the server sets and every client replicates (today only the client's sources take SetInput /
Fire; MF15j2). No original type needs them.

### The format review (MF15l)

Found in the 83 files (user 2026-11-11: exactness dropped, an effect may drift if it looks the same overall):
- **Vectors written as three floats**: `origin.x` 100 times in 45 files, 29 `position` start / tick pairs of three
  strings, var triples (`px py pz`, `vx vy vz`, `dx dy dz`), 22 normalisations spelt `1 / sqrt(x * x + y * y + z * z)`,
  `ball_x() / ball_y() / ball_z()`, `platform_normal_x / _y / _z`, `path.x[i]`, three `at_create` vars for the sun.
- **Start equal to tick**: 36 vars and 8 positions repeat one expression as both.
- **The kept update after death** ("Kept" above): 72 guards `p.age == <end> ? old : new` in 24 files, and 35 kinds with
  `lifetime = 1000000000` or `2147483647` plus a `kill` on their age, only so the update a dead particle still got
  matched the original's.
- **Spawn shapes as a special table** (`shape`, `radius`, `min`, `max`, `from`, `to`, `tries`, `shift`, `offset`,
  `on_ground`) and the motion fields `speed`, `drag`, `gravity`, `wind`, `growth`: each a fixed formula the
  expression language can say; `after_spawn` only orders vars around the shape's draw.
- **Anchors placed two ways** (`x / y / z` or `radius / angle`).
- **Whole files that are variants but for order**: CastingDivination (a kind before the base's), VisionStar2 and
  CampFire (vars before the base's), Transform (a reaction less).

Proposals (implemented in this order; each step re-records effect_check's reference where effects move, checked by
the coarse compare and the viewer's captures):
1. effect_check's coarse mode: per case and quarter of its ticks, particle counts, lifetimes, bounds, average size and
   colour, within a tolerance (tests/effects/coarse.txt, recorded before any change).
2. Vector values: every value is four floats (a v4 stack, scalars broadcast to all lanes; the compiler knows each
   value's width, so a random draw draws once per lane it fills and a scalar context reads `.x`). Swizzles (`.x`,
   `.xy`, `.rgb`) on any name or `(...)` / `[...]`; `vec2() vec3() vec4()`; `dot length normalize cross distance`;
   math functions per lane; `range(lo, hi)` on vectors draws each lane; `ball() shell() disc()` and `circle(angle)`
   return points; `ground surface platform_height platform_normal on_ground` take a point. Names: `origin`, `motion`,
   `wind`, `extent`, `spread_min / max`, `side`, `up`, `sun`, the carrier's axes and feet, `p.position`, `p.velocity`,
   `p.color`, `p.spawn`, `a.position`, `a.spawn`, a vector input, `path[i]`, `group.var[i]`; vars take the width of
   their expressions (a particle's eight floats hold its vars, a vector three).
3. One field form: a field is a value (its start) or `{ start, tick }`; `tick` alone starts with the same expression.
   `spawn` is one point expression (the default `0`), drawn before the vars; `position` defaults to the centre plus
   `p.spawn` and to moving by the velocity; `velocity = { start, tick }` replaces `speed`, `drag`, `gravity`, `wind`;
   `size`'s `growth` is a tick; an anchor's `position` replaces `x / y / z / radius / angle`; `color` is `"RRGGBB"` or a
   vector expression. `after_spawn`, `tries`, `shift`, `offset`, `on_ground`, `ball_x/y/z`, `platform_normal_x/y/z`
   and the scalar `p.x / p.vx / a.x` names go.
4. The update after death goes: a dead particle is drawn once more where it was and then pooled without an update, so
   the age guards go and those kinds take a plain `lifetime`.
5. Variants: a new keyed element (`[[particle]]`, `[[anchor]]`, `[[stage]]`, `[[on]]` with a `name`) or var takes
   `before = "<name>"`, and `remove = true` takes the base's out: the four whole files become variants.
6. The files rewritten in the new form (shorter, saying what they mean); the showcase mod and docs/modding.md
   "Effects" to match, with a migration note for older mod files.

### Checks

- `ei-convert --effect-check` (build checks; ctest `port_effects`; tools/convert/effect_check.cpp): every original type
  on data against the run recorded from its C++ (tests/effects/reference.txt, built into the tool): 146 cases, 5539
  lines (carried, free, a fast carrier's distance tries, rescaled, StopEmitting / StartEmitting / Stop, the carrier
  removed and back, the world's wind, a sun; the carrier a stone, a stone with a path (`PathStone`, ShapeChange's
  points as OnPathMarker reads them), or a hero that runs, walks, dies and turns), each tick's
  particles (position, size, colour, frame, their previous values, the dead flag), origin,
  bounds, flags, RunTick's answer and the particle generator's draws, hashed per tick. No game data (no texture is
  loaded).
- `tests/unit/particle_record.cpp` (ei_unit_check): expressions (precedence, short evaluation and its draws, tables,
  curves, errors), a record read and merged over a lower layer (a field by name, an element appended), problems named
  with file and line, the state machine driven by its inputs (stages by ticks, by `until` on an input, by an event;
  enter and event actions; anchors; `next = "end"`), a tick reaction's `when` and the stop action, `exp` / `log`, a
  source value `set`, `stop_emitting`, the carrier measured once and only when read, `set` on a var, `let_go`, "emit"
  reactions and their refused actions, `at_create` vars, `anchor_index` and the vars it refuses, MF15j's "before_emit"
  reactions adding anchors with a ball spawn (their draws in turn), `ground(x, y)`, a list input by default and set,
  `pick = "first"`, the segment's one draw, `on_death` queueing placed particles and an effect, the actions refused in
  `on_death` and "before_emit", MF15g4's "emit" reactions queueing a kind (and refused a kindless emit), the `shake`
  action and its form refused without a time, the `platform_*` functions, MF15g5's `ball_x/y/z`, a spawn's `tries`,
  `<group>.<var>[i]`, the particle writes to vars and anchor vars and `jump`, MF15g6's "spawn" reaction with its
  scorches and shake (and a refused emit), `at_create` anchors, a ball's `shift`, `surface(x, y)`, `sun.r/g/b` and the
  frame mode `free`, and every whole effect written out in
  docs/modding.md (and in this file, which has none now) read without a problem (EI_SOURCE_DIR); MF15g5a's `like`:
  a variant over its base (a var and the texture replaced, a kind merged by name and one appended, a number array and
  an `[[on]]`'s action list replaced whole), a chain of two, a mod's layer over the base reaching the variant, and a
  missing base, a `like` to itself, a circle of two and a `like` that is no name refused with file and line. The
  POSIX port check (`port_platform`) runs the same checks.
- `mod_effect` (both installs; the showcase mod's `Res\effects\showcase_aura.effect.toml`): CreateParticleSource by
  name, the client's source and its stages (`effect ... check`), inputs set by value and by enum name, the burst's
  `set` seen (`effect ... value`), an event fired; captures `calm`, `storm`; an unknown name makes nothing.
- `tests/unit/mod_hash.cpp`: Res\effects counts.

## The effect viewer

`ei-rebuild --effects [<effect>] [--zone <zone>]` (src/tools; how to use it: docs/modding.md, "The effect viewer") is a
dear imgui panel over the running game, and the viewer plays its source in the client world. That one choice brings
in the install's textures and figures, the mods' layers, the terrain, the lights, units with their animations and the
camera, all without a second start-up. It also lets the harness drive the viewer like any game.

- **Its own scene** (MF15i3): `--effects` sets `app::g_toolScene` (app/tool_hooks.h) before GameMain. The start
  then skips the "Start" movies, and StartFirstMap asks for a new game (flow state 3) where it would open the main
  menu. StartNewGame puts the party into `--zone` when one is given (the party's zone set before the redeploy), else
  the campaign's first zone. The hero is the unit the first pick goes on. The zone entry's autosave is off in this
  mode (ui_battle.cpp, AutoSaveEnabled), so the player's `savea` survives the viewer.
  - Not a separate exe yet: an `ei-effects` entry point needs GameMain's whole start (the worlds, the databases,
    the window, the renderer, the interface) and a zone with a unit, which is what the new game gives. The user
    (2026-11-11) wants it as its own exe all the same: on the engine library (file-layout.md, "The engine library";
    ei_engine with the particle core, MF15k1), once the particle core's world calls (`particle_host.h`) are an
    interface (MF15k2) and a figure plays without a unit (MF15k3), the viewer is MF15k4's exe.
  - The in-game panel (the harness's `effectviewer open`, over a game started as usual) is the same viewer.
  - A base zone (map list kind 2) has no 3D scene, so `--zone` wants a battle zone.
  - tests/scenarios/effect_viewer_standalone.txt starts the exe with `--effects Portal` (run_all's `# arguments:` line).

- **The source**:
  - The client world's particle manager makes the source (`CreateObjectAs`, the effect's shown type), and only the
    viewer holds it (AddRef, not in the manager's `held`). DrawAll draws it like any other source.
  - TickSources ticks every source in its list, held or not, so the source carries `ParticleSourceFlag::OwnClock`,
    which TickSources skips. The viewer clears the flag before it lets the source go back to the spares.
  - The viewer ticks it after each client tick (`app::g_toolHooks.clientTicked`), so the frame's tick fraction
    interpolates it as it interpolates the others.
  - Its draws come from the viewer's own IsaacRandom, swapped with `g_particleRandom` around its set-up and its ticks.
    The game's generator is untouched, and every replay draws the same.
  - It is placed as EffectParticle::Spawn places a spawned effect (localPosition, ScaleSize(size / baseSize),
    Attach). On a unit outside the party it draws only while its unit is shown (`particlehost::Hidden`).
  - The viewer lets go of it before the worlds are emptied (`worldsEnding`, from StopGame).
- **Sub-effects** (a data effect's `effect` actions, `on_death` too): StartEffects makes those of a source with
  OwnClock through `effectdata::g_startOwnClock`, which the viewer sets at Open (`StartChild`): unheld, one reference
  of the viewer's, OwnClock too, so their own sub-effects come the same way.
  - Step ticks them before the source, with the viewer's generator (those made at a tick begin at the next), and lets
    go of each whose tick says it is done. The effect has ended when the source and every sub-effect have.
  - Restart, a pick and `worldsEnding` let go of them with the source; Freeze holds their particles too.
- **Time**:
  - What the panel changes (emission, the source's alpha / param / extent / turn, the inputs, the events) goes on a
    timeline at the tick it was made. Changes dragged within one tick count as one.
  - A restart, a scrub or an edit makes the source again and runs it silently up to the tick, making each change at
    its tick.
  - Paused, the particles' previous values are set to their current ones (`Freeze`), so the interpolation holds
    still. The next tick's SavePrevious overwrites them first.
- **Editing**:
  - The document is the effect's top layer: a mod's file, else the built-in file. The layers below it are merged under
    it as `Registry::Load` merges them (tools/effect_document.cpp), and a `like` resolves over its base's layers
    (`effectdata::ResolveLike`): a variant plays resolved, while its Fields show its own file.
  - Each edit that reads gives a new record, which goes into the registry (`Registry::Put`), so any source of the name
    made afterwards plays it too.
  - The Fields tab edits the parsed tree and writes it back as TOML (`WriteToml`), which drops the text's comments.
    The Text tab keeps the text as typed.
  - A text that fails to parse, or reads with errors, keeps the last record and lists the problems.
- **Saving**:
  - The file goes to `<folder>\Res\effects\<name>.effect.toml`, and an edited built-in file gets `replace = true`.
  - The default folder is the active mod's folder when it lies outside the game folder, else
    `<user data>\ei-rebuild-effects` (`platform::UserDataFolder`). It is never the install unless the player types it
    in.
- **New effects** (an entry's `madeText`, played through `EffectDocument::Start`; kept through a Scan, a pick again
  comes back to its text as typed): `kTemplate` (docs/modding.md's first example) under a name no entry and no original
  type has. (Convert to data, which wrote a type still in code as a starting file, went with the last code types,
  MF15g6.)
- **Limits**: the viewer's source is in no save and on no network.
- **Interface**:
  - Dear ImGui v1.93.0 (third_party/imgui, unmodified, no backends of its own).
  - Its platform side is tools/imgui_layer.cpp: platform events, the texture protocol (`RendererHasTextures`), a
    scale of frame height / 1080, and no ini file.
  - Its renderer side is the renderer's overlay (renderer.md, "render_overlay.cpp: the tools' overlay").
- **Check**: the `effect_viewer` scenario (the showcase mod).
  - Portal from its built-in file: pause, scrub, step, a field edited and the file saved into the run's output folder.
  - showcase_aura on the hero, with its enum, float and event inputs and its stages.
  - Tornado from its built-in file, on the hero.
  - A new effect from the template with a tick reaction that starts Teleport: the sub-effect's particles hold while
    paused and are the same after a scrub to the same tick.
  - Captures `viewer_portal`, `viewer_portal_edited`, `viewer_aura`, `viewer_tornado`, `viewer_new`.

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
sounds started so far (`SoundSequence::PlayNext`). The choosers draw `g_clientRandom`: PlayNext runs when a voice ends
(the device's clock) or Play3D finds the listener out of range, so the number of draws follows playback and the camera.

## Moshka

`EffectMoshka` (100): the client world's bird, told to follow the session's leader. A figure (class 0x38, "unmobi" /
"bird00", lit, animation 0), a Bag particle source (scale 0.5, attached), the followed id, a timer, a circle angle, a
perch height and a state (0 perched 200 frames, 1 circling within 5 of the leader for 100, 2 flying to the perch for 150,
then Reset). Update (from the draw pass with the animation time) steps at most 5 frames, keeps the bird 0.5 above the
surface (platforms too), moves it at sqrt(sqrt(distance)) * 0.03 + 0.3 per frame and advances its animation. FindPerch
draws `g_clientRandom` angles until the AI map's line of sight is above 0.5 (Update runs once per drawn frame as well as per tick,
so the frame at which the timer runs out decides where the bird is and how many tries it takes). Reached by
`GSSetVar(0,"q.gz1g.q0g",2)`.

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
