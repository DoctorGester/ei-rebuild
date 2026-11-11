# Particles and world effects

Code: `particle_manager.cpp` (the manager, attach, draw, tick), `particle_create.cpp` (the spec table, Create), the
effects by family (`particle_casting.cpp`, `particle_marks.cpp`, `particle_blasts.cpp`, `particle_world.cpp`),
`particle_common.h`, `effects.h`, `effects_weather.cpp`, `world_effect*.cpp`. Scenarios:
`particles.txt` (every source type it can make), `effects.txt`, `spells*`, `effects_bow`.

## Particle sources

- `ParticleSource` (class id 0xC, an engine object; made by `NewParticleSource`). `Create` resets the source, then
  takes its type's entry of `kSpecs` (particle_create.cpp; `type` 0x2000-0x2052, one entry per type still in code; a
  type on data takes its record: "The original effects on data" below; the script names are a table in the game's
  data: name, type, lifetime): flags, emission rates, lifetime, spread, sizes, the count of
  elements (made zeroed) and the texture (kind 6, by name, one UV rectangle per frame of a grid of `frameColumns`
  columns). Code runs only for the types whose elements start other than zero or whose set-up draws (`Setup*`,
  `SetupElements`).
- An unknown type (a mod's or a newer build's effect, from a peer's update or a save) makes no source:
  `ParticleManager::CreateSource` asks `ShownParticleType`, which logs "particles: unknown particle type 0x..., no
  effect shown" once per type and answers `kNoParticleType`, and gets null; `EffectParticle::Spawn` (also a wall's and
  an arrow's trail) then shows nothing. The world object stays, so the server's lifetime (`IsOneShotType`: none for
  an unknown type, the object lives until removed) and replication are unchanged. The original ended the game
  ("CParticle::Create(). Unknown ETypeID."), which an original client still would, but none gets a data effect's id
  ("In multiplayer and saves"). Check: `ei-convert --lua-check`'s `CheckUnknownParticleType`.
- Each effect is a section of its family's file: its set-up, `Emit*` (fill one particle; 0 returns it to the pool),
  `Update*` (move one; 0 drops it) and `Tick*` (one element of the source's array, or the whole array for Tornado,
  Casting*, Cylinder* and ShapeChange), free functions over the source declared in
  `particle_common.h`. The tick picks them by type in `EmitParticle`, `UpdateParticle` and `TickSource`
  (particle_manager.cpp); a type without an element tick has none.
- `Tick` computes the source motion, runs the type's element tick and its update per particle (saving / restoring
  the "prev" fields), tracks the bounding box and emits up to `max(emitCount, distance moved /
  emitSpacing)` particles with `emitChance`% each. Particles are pooled.
- The types are `ParticleType` (effects.h): the script table's names, and descriptive ones for the types it lacks.
- Per-type data has one name per meaning: an element's fields through its type's view of `ParticleElement` (the views
  carry no padding and never alias each other), a particle's `velocity` / `aux` / `drift` parts through local references
  at the top of each function, and the source's own `tint` (Tornado, CurseHolder ARGB; FeetCloud RGB), `rayCount`
  (Shower) and `glowKind` (ShapeChange, set by the path markers). The original kept these three in `frameDelay`,
  `lifetime` and `alpha`.
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
- An update that counts the life down then reads its life table at index -1 (Shower, Casting, CastSwirl, Orbit;
  ClayRing its countdown table): the original read the previous table's last entry; `ByLife` (effects.h) gives
  that value. FireBlast's flame frame can step to 16 (read as alpha 0); Link sparks are emitted on a frame up to
  frameCount, one past the grid (drawn with the first frame).
- `EmitLightning` never counts its bursts; CastSwirl reads its locals one slot off (an element gets sign * carrier
  radius instead of the size); Mushroom's spread is ln(1) = 0; Falling's direction spread is ftol(10 degrees) = 0;
  Teleport adds its alpha step as an unsigned sum capped at 255 (a fade out below 19 jumps back to opaque; its file);
  LightningBlast sparks shift the alpha table by 22, not 24; Shower and FireBlast keep odd counters (see the code);
  Casting's swirl pick 0 is element 0, the effect's state, read as a swirl (its radius and size as x and y, z 0).

## Data effects

An effect written as data (MF15c; plan: the MF15 study, approved by the user 2026-11-11): `Res\effects\<name>.effect.toml`,
read through the mod layers and run by one C++ runtime inside the generic source tick above. The end state (MF15g)
is every original effect on this path and the per-type `Emit*` / `Update*` / `Tick*` deleted: the original effects
and a modder's use the same runtime, so nothing below is mod-only (twenty-two are there now: "The original effects on
data"). Game code (and later Lua) drives an effect only
through the typed inputs it declares; the data maps them to rates, curves, stages, anchors and sub-effects.

Code: `particle_expr.*` (expressions), `particle_record.*` (the layers merged, the schema read, the effect's state
machine; no game header, so `tests/unit/particle_record.cpp` runs it alone), `particle_data.*` (the registry, the
particle side, the inputs). The guide for modders: docs/modding.md, "Effects".

### Files, names and ids

- The name is the file's stem (`showcase_aura` for `showcase_aura.effect.toml`), matched without case. Each layer's
  file of that name is read (`FindRecordFile(..., false)`: the TOML files alone) and merged field by field, the
  bottom one first (`MergeOver`): a table merges key by key, an array of tables (`[[stage]]`, `[[anchor]]`,
  `[[particle]]`, `[[on]]`) by its elements' `name` (`event` for `[[on]]`), matched to the first not yet merged, the
  others appended; any other value replaces. `replace = true` ends the search as for every record file. So a mod
  retunes one particle kind of an effect with `[[particle]] name = "spark" lifetime = 20` alone.
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
  name)`, `effectdata::Fire(source, event)` (particle_data.h); the harness's `effect` step does the same.
- **Vars** (`name = { start, tick }` or a value): the effect's own state. `start` at the first tick, `tick` from the
  second, in the file's order, each seeing the ones before it already changed.
- **Stages** (`[[stage]]`, the state machine; the first is entered at the start): `ticks` (its length from its entry)
  then `next`; `until` (a condition) then `next`; `on = { event = "stage" }`; `next = "end"` ends the effect. While it
  lasts its `rate`, `chance`, `emitting` stand over `[emit]`'s; `enter` is a list of actions run at its entry. One
  move per tick; moves made by the tick's actions count.
- **Reactions** (`[[on]] event = "start" | "stop" | "tick" | <event input>`, `when`, `do = [actions]`): "stop" when the
  game stops the source's emission (StopEmitting, Stop); "tick" every tick (after the event reactions, before the
  stage's move). `when` (a condition) runs the reaction only while it holds.
- **Actions**: `{ emit = n, kind = "<particle>", at = "<anchor group>" }` (n particles of the kind, at each live
  anchor of the group; without `kind` n more tries of the regular emission this tick, which then emits only while the
  source emits), `{ set = "<input>", value = ... }`, `{ stage = "<name>" | "end" }`, `{ effect = "<name>" }` (another
  effect started at the source's origin, free, on its own), `{ end = true }`, `{ stop = true }` (the source stops as
  the game's Stop stops it: no emission, not kept alive, its carrier let go; the effect goes on and the game may
  start it again), `{ stop_emitting = true }` (the emission flag cleared, as the game's StopEmitting without its
  KeepAlive: the game may set it again). `set` names an input or one of the source's values `size`, `height`,
  `growth`, `alpha`, `param` (written back to the source after the step, so the next tick, ScaleSize and the
  particles' fields see it; Banshee keeps its ring's speed in `height`).
- **Anchors** (`[[anchor]]`): a group of points the source owns (rings, orbs, bolt points, swirls), made at the
  start: `count`, `ticks` (each one's life; none: it lasts), `vars` (per anchor), and its position relative to the
  source's origin, `x`, `y`, `z` or `radius`, `angle` (x = cos(angle) * radius, y = sin(angle) * radius), `z`. An
  anchor that ends stays readable (its last values) for the particles it carries.
- **Particle kinds** (`[[particle]]`): the regular emission takes the first kind whose `when` holds (none: always;
  `false`: only by actions). `lifetime`, `vars` (at most 8; `{ start, tick, after_spawn = true }`: its start runs
  after the spawn shape draws and may read `p.spawn.x/y/z`, the shape's point; the ticks run in the file's order,
  each seeing the ones before it already changed), `at` (an anchor group: it starts at one of its live
  anchors, `pick = "cycle" | "random"`), `space` (`world`: moves on its own; `source`: its offset rides the source;
  `anchor`: rides its anchor), `spawn = { shape = point | box | ball | shell | disc | ring, radius, min, max (a box's;
  else the source's spread), offset = [x, y, z], on_ground (z = the ground's height plus it) }`, `position = [x, y, z]
  | { start, tick }` (the start: the particle's place instead of the shape's point plus offset and centre, on_ground
  still applied; the tick: its place each tick instead of the move by its velocity, the offset in `source` and
  `anchor` space; the three read this tick's values before any changes), `velocity = [x, y,
  z]`, `speed` (outward from the spawn's centre: offset * speed / its length), `drag` (a factor per tick), `gravity =
  [x, y, z]`, `wind` (times the world's wind; each tick the particle moves by its velocity, then drag, gravity and
  wind change the velocity for the next tick, the originals' order; a world-space kind with none of velocity, speed,
  gravity, wind stays put), `size = value | { start, tick, growth }`, `color = "RRGGBB" | [r, g, b]` (expressions,
  each tick),
  `alpha = value | { start, tick }` (0..255), `frame = value | { start, delay (ticks per frame, 0 or none: still),
  mode = loop | hold | die, tick }`, `kill` (it dies once this holds).

### Expressions

A number, `true` / `false`, or a string compiled once at load (`particle_expr.h`): numbers, names, `+ - * / %`,
comparisons, `!`, `&&`, `||` (both short), `a ? b : c`, functions `sin cos atan2 sqrt abs floor ceil round min max
clamp lerp pow exp log`, random `range(lo, hi)` (IsaacRandom::Range), `below(k)`, `chance(percent)`, `pick(n)` (Next() % n),
a curve `name(t)`, a table `name[i]` (truncated, held to the ends). Constants `pi`, `deg`. Names, by where the
expression runs:
- everywhere: the inputs, the vars, the source's values (`size`, `height`, `growth`, `alpha`, `param`, `base_size`,
  `age`, `stage_age`, `stage`, `carrier_radius`, `carrier_z`, `attached`, `distance`, `motion.x/y/z`,
  `origin.x/y/z`, `count`, `index`, `total`, `wind.x/y/z`, `wind_strength`, `extent.x/y/z`, `turn_cos`, `turn_sin`,
  `emitting`: the source's emission flag, which the game's StopEmitting and Stop and an effect's `once` clear,
  `spread_min.x/y/z`, `spread_max.x/y/z` (the spread box, scaled by ScaleSize), `side.x/y/z`, `up.x/y/z` (the
  motion's side and up axes this tick, zero while the source moves 0.001 or less: the trails' across directions)),
  `anchors.<group>` (live count), `stage.<name>`;
- in an anchor's and its particles' fields: `a.index`, `a.count`, `a.age`, `a.ticks` (left), `a.x/y/z`, `a.<var>`;
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
chance / emitting), the `set` source values written back, a `stop` action's Stop, a `stop_emitting`, then the source's `emitCount` and `emitChance` set for the generic
emission (not emitting: no tries, none from the movement either; the spacing is the source's, `[emit] spacing` scaled
by ScaleSize as an original type's) and the started sub-effects made. `UpdateParticles` → `UpdateData` per particle
(life, frame, vars, the move, then the velocity's change, size, colour, kill). A `die` particle past its last frame
ends at the frame step, before it moves and before its frame timer starts again, as the originals' (so the update a
dead particle still gets, "Kept" above, steps no frame and may move it, as theirs). `Emit` → `EmitQueued` (the actions'
particles, emitting or not), then the generic tries → `EmitData` (with `once`, the first particle clears the
emission flag; the game may set it again). A data source with `keep_alive` lasts until it ends (`next = "end"`, an
`end` action, the game's Stop), then while it has particles; without it, as an original type: a free source while it
has particles (or the game's StopEmitting / StartEmitting set KeepAlive), an attached one while it emits or has
particles. A particle's life counts down before its update and it dies at 0 (an original that dies below 0 gives its
kind one tick more of `lifetime`). Per particle: `Particle::age`, `kind`, `vars`; `state` its anchor, `drift` its
offset in `source` / `anchor` space.

### The original effects on data

FireStar, AcidStar, CurseStars, PortalStar (MF15d), Sparks, FeebleMind, ZoneExit and the trails FireBall, FireArrow,
RikArrowSmoke, AcidRay (MF15g), the smoke family CampFire, Fire, Smoke, Tendril, then VulcanSmoke, Portal,
TeleportRing, Teleport, Banshee, BlueGas and Nuke (MF15g2) run from `assets/effects/<Name>.effect.toml`, built into the executable
(src/game/effects/embed_effects.cmake writes them as bytes at build time; `effectdata::BuiltinText`). Their C++
emitters and their `kSpecs` entries are gone. The next batches are ticket MF15g3 (the other plain types)
and MF15g4 (the last plain ones and, after MF15j, the state machines).
- **The built-in file is the bottom layer.** The registry reads it below the mod layers' files of the same name
  (`Res\effects\FireStar.effect.toml` in a mod retunes the field it names), unless a mod's file says `replace = true`.
- **The id is the original type** (`ParticleTypeByName`; 0x2041 for FireStar), so the world objects, the delta field,
  the saves and the game code that names the type (`ParticleType::FireStar` in the spell impacts) are unchanged.
  `FindById` answers an original type's id from its built-in file; a type still in code has none, and a mod's file of
  such a type's name is left out (and not listed by `Scan`).
- **What the C++ tables held moved into the file**: `[server] one_shot` (FireStar, AcidStar, Sparks, Banshee, Nuke:
  60; Teleport 120;
  `OneShotTicks` reads the record), `script_lifetime` (FireStar, AcidStar: 20; Sparks 60; Teleport, Nuke 200; the others -1, the default;
  their `kScriptParticleTypes` entries are gone, and CreateParticleSource finds the name in the registry), the spec's
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
  refused).
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

### The original state machines on inputs (MF15g's path)

The 14 behaviours (34 types) the study left as code become anchors, vars, stages and kinds, with what game code
decides today as inputs. Two written out (equivalence to the C++ is MF15d's check, not claimed here):

Healing (rings of flames rising around the carrier):
```toml
texture = "Healing"
blend = "additive"
attach = "attached_only"
[server]
one_shot = 60
script_lifetime = 200
[source]
size = 0.5
[vars]
step = { start = "carrier_radius * 1.1 / 18" }            # the rings' radius step (the C++ keeps it in height)
flame_growth = { start = "(size + 1) / 144" }
[emit]
rate = "anchors.ring * 3"                                 # sparks at random rings
[[stage]]
name = "rings"
until = "anchors.ring == 0"                               # the emission ends with the rings
next = "end"
[[on]]
event = "start"
do = [{ emit = 1, kind = "flame", at = "ring" }]          # one flame per ring
[[anchor]]
name = "ring"
count = "round(size * 6 + 2)"
ticks = 36
vars.lift = { start = "range(-0.2 * carrier_radius, 0.2 * carrier_radius)" }
vars.radius = { start = 0, tick = "a.ticks >= 18 ? a.radius + step : a.radius - step" }
vars.angle = { start = "a.index * 2 * pi / a.count", tick = "a.angle + 10 * deg" }
vars.height = { start = "carrier_z - carrier_radius + a.lift", tick = "a.height + (2 * carrier_radius - a.lift) / 36" }
x = "sin(a.angle) * a.radius"
y = "cos(a.angle) * a.radius"
z = "a.height"
[[particle]]
name = "flame"                                            # rides its ring 36 ticks, then burns out for 8
when = false
at = "ring"
space = "anchor"
lifetime = 44
size = { start = "(size + 1) * 0.1", tick = "p.age < 18 ? p.size + flame_growth : p.age < 36 ? p.size - flame_growth : p.size" }
alpha = { tick = "p.age < 36 ? 255 : p.alpha - 30" }
frame = { tick = "p.age < 36 ? p.age % 8 : p.frame" }
[[particle]]
name = "spark"
at = "ring"
pick = "random"
lifetime = 8
spawn = { shape = "box", min = ["-0.15 * a.radius", "-0.15 * a.radius", "-0.15 * a.radius"], max = ["0.15 * a.radius", "0.15 * a.radius", "0"] }
size = "(size + 1) * 0.07"
frame = { start = 8, delay = 1 }
```

Casting: the same rings (three, 45 ticks; the radius `tick` grows to 27, shrinks to 9, then eases to `step * 20`; the
angle and height turn only while `a.ticks >= 9`) with the source's phase as two stages, `turn` (`ticks = 36`, `rate =
27`, sparks at random rings) and `close` (`ticks = 9`, `rate = 72`, `next = "end"`), and two spark kinds chosen by
`when = "stage == stage.close"`: the close sparks spawn on ring 0's circle (`at = "ring"` with `pick = "first"`, an
extension below; `spawn = { shape = "ring", radius = "range(inner, a.radius)" }`).

The others, by what drives them:

| Behaviour | Anchors / vars / stages | Inputs game code drives |
|---|---|---|
| CastingFire ... Divination (swirls) | swirls: anchors random-walking (vars with `range` kicks); the envelope a var | `casting` (bool: the C++'s "attached and emitting" freeze) |
| CastSwirl orbiters | one anchor added per emission (needs the action below) | |
| Cylinder1 / 2 | 2 star anchors, orbit vars; rate falling with `age` | |
| Modifier1-12 | ring-point anchors contracting or expanding for 10 ticks | `direction` (enum in / out, today the size's sign), `axis` (vector, per kind) |
| Link | sparks along the beam (`offset = "range(0, 1) * extent.x"`...) | `target` (vector: the beam, today `extent`) |
| Silence, Bag | orb anchors on a tilted circle; 80 ball anchors kicked (vars), colour by age tables | |
| FireBlast, LightningBlast | fragment / bolt-point anchors with velocity vars, sub-effects | |
| ClayRing, Geyser | ground-point anchors (needs `ground`); vents with eruption / pause vars | |
| Tornado, CurseHolder | 6 funnel-ring anchors leaning with `motion.x/y` | `tint` (vector: the sun's colour, today read by the C++) |
| ShapeChange | one glow per path point | `path` (a list input, below) |

Extensions these still need (MF15j): a `ground(x, y)` function (anchors on the ground, kill planes); an action that
adds an anchor; `pick = "first"` and an anchor by index; `scorch` and `shake` actions (FireBlast's and
LightningBlast's start, `EffectParticle::Spawn` today); a list input (ShapeChange's path); a sub-effect or emission on
a particle's death; a `segment` spawn shape.

### Checks

- `ei-convert --effect-check` (build checks; ctest `port_effects`; tools/convert/effect_check.cpp): every original type
  on data against the run recorded from its C++ (tests/effects/reference.txt, built into the tool): 52 cases
  (carried, free, a fast carrier's distance tries, rescaled, StopEmitting / StartEmitting / Stop, the carrier removed
  and back, the world's wind), each tick's particles (position, size, colour, frame, their previous values, the dead flag), origin,
  bounds, flags, RunTick's answer and the particle generator's draws, hashed per tick. No game data (no texture is
  loaded).
- `tests/unit/particle_record.cpp` (ei_unit_check): expressions (precedence, short evaluation and its draws, tables,
  curves, errors), a record read and merged over a lower layer (a field by name, an element appended), problems named
  with file and line, the state machine driven by its inputs (stages by ticks, by `until` on an input, by an event;
  enter and event actions; anchors; `next = "end"`), a tick reaction's `when` and the stop action, `exp` / `log`, a source value
  `set`, `stop_emitting`, the carrier measured once and only when read, and every whole
  effect written out in this file and in docs/modding.md read without a problem (EI_SOURCE_DIR).
- `mod_effect` (both installs; the showcase mod's `Res\effects\showcase_aura.effect.toml`): CreateParticleSource by
  name, the client's source and its stages (`effect ... check`), inputs set by value and by enum name, the burst's
  `set` seen (`effect ... value`), an event fired; captures `calm`, `storm`; an unknown name makes nothing.
- `tests/unit/mod_hash.cpp`: Res\effects counts.

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
