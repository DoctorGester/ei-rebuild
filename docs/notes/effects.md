# Particles and world effects

Code: the particle core (part of the engine library: file-layout.md, "The engine library"): `particle_source.h`
(the types, the manager, the sources), `particle_manager.cpp` (the manager, attach, draw, tick), `particle_create.cpp`
(the types' names, Create), the data runtime (`particle_expr.*`, `particle_record.*`, `particle_data.*`: "Data
effects" below) and `particle_host.h`, what the core asks of the world ("The host" below). The
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
- A particle that dies is drawn once more where it was (its previous values restored), then `UpdateParticles` pools
  it at the next tick without an update (MF15l1: the original still updated it once and counted it in the bounds).
- Tables read past their data where the original read zeros: Modifier markers by their element's tick (to 25),
  AcidRay's spread and size (to 16; its file's tables), ClayRing's alpha at the first ticks (12, 13), Modifier's fade (10), FireBlast's
  frame (to 16), TeleportRing's column alpha at its last ages (its file's fade table); the tables carry the
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

## The host

- `particlehost::Host` (particle_host.h) is what the core asks of the world its sources are in: the ground's height,
  the platforms' plane, the wind, the tick fraction, scorch marks, camera shakes, and the carriers. A carrier is a
  `FigureInstance` (`ParticleSource::attachedTo`; the game's map objects are figures, map_objects.md "Figures"). The
  host keeps each carrier's sources (Follow, LetGo), says whether it was taken out (Removed) or is hidden, where a
  part of it is (PartPoint, with ParticleSourceFlag::OnPart), its measure (radius, centre height) and what it is to
  the effects (`CarrierState`: a unit, running, dead, its radii, its feet). The core asks only for a carrier that is
  there: a free source's values (no carrier: measure 0.1, no unit, feet at the origin) and the carrier's axes (its
  rotation) are the core's own (particle_data.cpp, WriteCarrier).
- One host per program, set with `particlehost::Use` before a source is made (`Current()` answers it). Two exist:
  - the game's (game/effects/effects_particles.h, `WorldParticleHost`): the client world, its platforms, the camera,
    Object3D's effect sources, Unit's values. The client world uses it where it makes its particle manager
    (world_client_maps.cpp), and ei-convert's checks where they make theirs (effect_check, lua_check);
  - `particlehost::FigureHost` (particle_figure_host.h), a world of its own for a program without the game's (the
    effect viewer's exe, MF15k4): the ground flat at `groundHeight`, no platforms, its own `wind` and
    `tickFraction`, no scorch marks or shakes; its own list of carried figures and their sources (a reference each);
    a figure's parts by name (FigureInstance::PartPoint, unscaled; the effect point its "rh3"), or with `namedPoint`
    set a bone (else a part) by name for every source on a part (`NamedPointOf`: the bone's figure-space position
    turned and moved; the viewer's pick of a point no AttachPoint names), its measure the box of its parts' points; `unit`, `running`, `dead`, `unitRadius` say what every figure is. `Remove` / `Restore` take
    a figure out and back as a map object's Removed flag does; `LetGoAll` before a figure is destroyed.
- Check: `ei-convert --effect-check`'s world-less case (RunWithoutWorld: Stun on a figure's hand, walking, running,
  moved, taken away; ZoneExit on a ground at 2) with its own particle manager, the host not the client world. The
  textures load by name through the scene renderer's `world` (LoadNamedTexture), a ManagerContext: the effect viewer's
  scene answers it as the client world does ("The effect viewer").

## Data effects

An effect written as data (MF15c; plan: the MF15 study, approved by the user 2026-11-11):
`Res\effects\<name>.effect.toml`, read through the mod layers and run by one C++ runtime inside the generic source tick
above. The end state (MF15g) is every original effect on this path and the per-type `Emit*` / `Update*` / `Tick*`
deleted: the original effects and a modder's use the same runtime, so nothing below is mod-only (all 83 types are
there since MF15g6: "The original effects on data"). Game code (and an effect's Lua hooks) drives an effect only
through the typed inputs it declares; the data maps them to rates, curves, stages, anchors and sub-effects.

Code: `particle_expr.*` (expressions), `particle_record.*` (the layers merged, the schema read, the effect's state
machine; no game header, so `tests/unit/particle_record.cpp` runs it alone), `particle_data.*` (the registry, the
particle side, the inputs). The guide for modders: docs/modding.md, "Effects".

### Files, names and ids

- The name is the file's stem (`showcase_aura` for `showcase_aura.effect.toml`), matched without case. Each layer's
  file of that name is read (`FindRecordFile(..., false)`: the TOML files alone) and merged field by field, the
  bottom one first (`MergeOver`): a table merges key by key, an array of tables (`[[stage]]`, `[[anchor]]`,
  `[[particle]]`, `[[on]]`) by its elements' `name` (an `[[on]]` without one by its `event`), matched to the first not
  yet merged, the others appended; an array of tables none of which has a `name` or `event` (an action list: `do`,
  `enter`, `on_death`) and any other value replace. A var's or an element's table with `remove = true` takes the one
  below out; a new one with `before = "<name>"` goes before that one (both keys are dropped as they merge, so `Read`
  never sees them). `replace = true` ends the search as for every record file. So a mod retunes one particle kind of
  an effect with `[[particle]] name = "spark" lifetime = 20` alone.
- **Variants** (`like = "<effect>"`, MF15g5a): after the layers merge, a `like` takes that effect's tree (its own
  layers merged, its own `like` resolved first) and merges the effect's over it by the same `MergeOver`
  (`effectdata::ResolveLike`, particle_record.cpp; the registry's `MergedLayers` answers a base's layers, and the
  viewer's document resolves through the same function). The variant keeps its name, id and file; `like` is taken
  out before `Read`. A missing base, a `like` that is not a name and a chain that comes back to an effect already in
  it (`a -> b -> a`) are errors: the effect does not load. A mod's layer over a base reaches every variant of it
  (accepted, user 2026-11-11). The registry caches records, not trees: a base edited in the viewer (`Put`) reaches its
  variants only once its file is saved into an active layer and the registry resets.
  37 original effects are variants: CastingDomination / Enchantment / Healing on CastingIllusion, CastingElectricity /
  Acid / Divination on CastingFire (Divination's marker kind `before` the spark), Falling2-4, Shower2-4, Modifier2-5
  and 9 on Modifier1, 6-8 on Modifier5, 10-12 on Modifier9, AcidStar on FireStar, RikArrowSmoke on FireArrow,
  FeetCloud2, VisionStar2-4 on VisionStar1, PoisonCloud on PoisonFog, SphereAcid / Electricity and Antimagic on
  SphereFire, Cylinder2, GroundDust2, CampFire on Fire (its roll var `before` the rise that reads it), Transform on
  Mushroom (the named shake reaction removed).
- The id: `0x40000000 | (FNV-1a 32 of the lower-case name & 0x3FFFFFFF)` (`IdOf`), apart from the original
  `0x2000` range. It is the source's `type`, and the world object's (`EffectParticle::type`): it travels in the delta
  field and saves as the original types do. Two names with one id: the second is left out with a warning.
- `effectdata::g_effects` (`Registry`): `Find(name)` loads an effect at its first use; `FindById(id)` lists
  `Res\effects` through the layers (`FilesByLayer`) at the first id it has not met (a peer's update, a save).
  `Reset` at `LoadDatabases` (a switch of mods). Each problem is a `warning: effects: <file>:<line>: ...`; an
  unknown key is logged and the effect loads; an expression that does not compile, an unknown name or a record
  without a particle kind makes it unusable: it shows nothing, as an unknown type does (`ShownParticleType`).
- The server reads `[server]`: `one_shot` (the world object lives that many ticks; `OneShotTicks` beside the
  original's 60 / 120), `script_lifetime` (CreateParticleSource's, as the script table's 200 or -1) and `wall` (the
  world object is an `EffectWall`, which carries a direction: `WallEffect`, world_effect.h; a spell's and a scripted
  cast's `direction` only for such an effect; Tendril and Link).
- **Walls** (`EffectWall`): its direction travels after the particle effect's fields and becomes the source's
  `extent` (`EffectWall::Shape`) after the catch-up ticks and before the "spawn" reactions, which is where the fire
  wall's scorch is (Tendril's file: a scorch with a `length` and an `angle` from the extent, as long as the wall and
  as wide as half its size). Script
  `CreateParticleSource` takes a name its table lacks from the registry; sub-effects (`effect = ...`) name a data
  effect or an original type by its ParticleType name (`ParticleTypeByName`).
- Res\effects counts in the mod's content hash (`modhash::Counted`, mods.md "Mods in multiplayer"): peers with other
  effect files are refused, since `[server]` decides when the world objects go.
- Records name the effects they show (MF15h; docs/modding.md, "Records that name effects"): a spell's `cast_look`
  (else spells.sdb's `[cast_looks]` by school), `look`, `look_ring`; a model's `blood_look` (else units.udb's
  `[blood_looks]` by blood kind) and `shock_look` (else the file's); a map flame's and a particle object's `effect`.
  The game's choices are built-in data (assets/spells/spells.sdb.toml, assets/units/units.udb.toml, the flame's
  default). Game code turns a name into the type through `EffectTypeNamed` (world_effect.h; a missing name warns once).
  Still chosen in code by type: the world's own visuals with no record behind them (the bird's Bag, the weather's
  Tornado), the interface's markers (path ShapeChange, flag GroundDust1 / 2, wisp Stun, the zone exits' ZoneExit),
  the unreached link spell's Link and the unreached placed trap's Mushroom.

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
- **Inputs the server sets** (game/effects/effect_inputs.h): the server's `EffectParticle::SetInput` (a float, bool
  or enum index; a vector; an enum by its value's name) and `Fire` (an event) keep the value in `inputs`, one entry
  per input of the record in its order (`set`, the value; an event's x counts its firings), and mark the object
  changed. Only for a data id: `Pack` writes the entries after the particle fields (a wall's direction after them),
  `UnpackFields` reads them, `Save` / `Load` the same, so the original types' packets and saves are as they were (a
  data id is ours alone: "No fallback for original clients"). The entries go as they are (`WriteBoth` / `ReadRaw`),
  not as delta words: a client's new object reads the server's first channel against the snapshot its own creation
  wrote (`Effect::Create`'s SendFull, with type 0 and no entries), so entries the server added after its first write
  would read as unchanged words of nothing (the first try lost the connection in mod_effect).
  - A client gives its source the entries that changed since it took them (`ApplyInputs`, in index order:
    `effectdata::SetInput`, an event fired as often as its count grew, 16 at most at once): in `Spawn`, so before the
    source's next tick. A source made new (the object's first update, a load, a zone entered) takes them all before
    its catch-up ticks: the values as they stand, each event fired as often as the server fired it. A value the
    effect changed itself (showcase_aura's burst sets `charge` back to 0) stays until the server sets another.
  - List inputs are not sent (the server refuses them).
  - Script: `SetParticleInput(id, input, value [, y, z])` and `FireParticleEvent(id, event)` on a script's particle
    source (ours, in the extended keyword table after Rebirth's: script_addon.cpp; Lua mission scripts by the same
    names); a spell's Lua: `cast:SetInput(handle, input, value)` (number, boolean, value name or point) and
    `cast:Fire(handle, event)` on its `cast:Effect`.
- Checks: `net_pair_mod_effect` (both peers on the showcase mod, in z3q2: the host's server starts `showcase_aura` by
  name with the harness's `serverscript`, both clients run it, stages by ticks, nothing logged unknown; capture
  `peer_effect`; then the server sets `mode`, `charge` and fires `pulse`, and both clients' sources read storm, one
  burst and one pulse: the aura's `bursts` / `pulses` vars, `effect ... value`), `mod_effect_save` (started, inputs
  set, saved, loaded: the source made again from the id with the saved inputs, caught up into glow with its burst),
  `tests/unit/effect_inputs.cpp` (the entries packed and read back whole and as a change, saved and loaded).

### The schema

Top level: `texture` (an Effects texture, kind 6), `script` (its Lua hooks' file in Res\effects: "Lua hooks"
below), `frames` (16), `columns` (4), `blend` (`alpha` | `additive`),
`attach` (`free` | `attached_only`), `part` (`effect_point`, `head`, `above_head`, `left_hand`, `right_hand`,
`left_leg`, `right_leg`, `body`, `body00`..`body04`: the source sits on that part of a unit), `flags`
(`ticks_while_paused`, `idles_off_screen`, `keep_alive`: a free source lasts without particles until it ends, else
only while it has some, as every original type). Tables: `[server]`, `[source]` (`size`,
`height`, `growth`, `alpha`, `param`, `spread_min`, `spread_max`, `extent`: the source's starting values, which
`ScaleSize` and the world object scale as an original type's), `[emit]` (`rate`, `chance`, `emitting`, `spacing`,
`max`, `once`), `[inputs]`, `[vars]`, `[[stage]]`, `[[on]]`, `[[anchor]]`, `[[particle]]`, `[tables]` (name = list of
numbers), `[curves]` (name = `[[t, value], ...]`, linear, held at the ends).

- **Inputs** (`name = { type, default, values }`): `float`, `vector` (a vector `name`), `bool` (0 / 1),
  `event` (its value is the times it fired before this tick, for one tick), `enum` (`values` listed; its value is
  the index, `name.value` the constants). Game code: `effectdata::SetInput(source, name, float | Vector3 | value
  name)`, `effectdata::Fire(source, event)` (particle_data.h); the harness's `effect` step does the same. On the
  server the world object keeps them for every client (`EffectParticle::SetInput` / `Fire`, "Inputs the server sets"). `list`:
  points (`default = [[x, y, z], ...]`, empty without one), read as `name.count` and `name[i]`, a point (truncated,
  held to the ends as a table's entries; 0 while empty); `SetInput(source, name, std::vector<Vector3>)`.
- **Fields** (a var, a particle's `position`, `velocity`, `size`, `alpha`, `frame`, an anchor's `position`): a value
  (its start) or `{ start, tick }`; a `tick` alone starts too, with the same expression. A particle's start reads its
  defaults as `p.*` (`p.position` the centre plus `p.spawn`, `p.velocity` 0, `p.size` the source's, `p.alpha` 255,
  `p.frame` 0), so a tick alone starts one step from them (`size = { tick = "p.size * 0.9" }` starts at 0.9 of the
  source's size). `color` stays an expression run at the start and each tick.
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
  <radius>, alpha = <0..1, default 1>, length = <radius along its turn>, angle = <its turn> }` (a scorch mark on the
  ground at the source's origin, `World::AddScorchMark`, its alpha held to 1; round without a `length`, turned by a
  draw of the client's own generator, `g_clientRandom`, without an `angle`). After a stop or a
  let go the source's `attached`, `origin` and carrier values are written again, so the tick's particle updates and
  emission see the source free. `set` names an input, a var or one of the source's values `size`, `height`, `growth`,
  `alpha`, `param`, `spread_min`, `spread_max`, `extent` or one lane of them (`extent.z`) (written back to the source after the step,
  so the next tick, ScaleSize and the particles' fields see it; Banshee keeps its ring's speed in `height`).
- **Anchors** (`[[anchor]]`): a group of points the source owns (rings, orbs, bolt points, swirls), `count` of them
  made at the start (0: only `add_anchor` makes them; `at_create = true`: when the source is made, before the first
  tick, which then steps them: FireBlast's fragments): `spawn` (a point expression, drawn first when one is made:
  `a.spawn`; none: 0), `ticks` (each one's life; none: it lasts), `vars` (per anchor, started after the spawn), and
  its `position` relative to the source's origin, a field (its start by default `a.spawn`; without a tick it stays).
  `a.index` is its number in the group, `a.count` the group's anchors made so far (all of the group's are told when
  more are added). An anchor that ends stays readable (its last values) for the particles it carries.
- **Particle kinds** (`[[particle]]`): the regular emission takes the first kind whose `when` holds (none: always;
  `false`: only by actions). `lifetime` (ticks, at least 1: it dies at that age, before its update), `spawn` (a point
  expression, `p.spawn`, drawn after the lifetime and before the vars: `spawn = "ball() * size"`; none: 0), `vars` (at
  most 8 floats; the ticks run in the file's order, each seeing the ones before it already changed), `at` (an anchor
  group: it starts at one of its live anchors, `pick = "cycle" | "random" | "first"`, or `anchor_index` (an
  expression: the live anchor of that number, wrapped, chosen after the vars; the lifetime, the spawn and the vars may
  not read `a.*`)), `space` (`world`: moves on its own; `source`: its offset rides the source; `anchor`: rides its
  anchor), `position` (a field in its space: the world's place, or in `source` and `anchor` space the offset from the
  source's origin or the anchor's place; by default it starts at the centre (the anchor's place, the source's origin,
  a dying particle's place) plus `p.spawn` and moves by its velocity each tick; its start and tick read this tick's
  values before any changes), `velocity` (a field: its start, 0 without one, and its tick after the move, which reads
  `p.velocity` from before it, so a slowing, a fall or the wind is written in it: `{ tick = "p.velocity * 0.9 +
  vec3(0, 0, -0.01)" }`), `size = value | { start, tick }`, `color = "RRGGBB"` or a vector expression (0..255 a lane,
  run at the start and each tick), `alpha = value | { start, tick }` (0..255), `frame = value | { start, delay (ticks
  per frame, 0 or none: still), mode = loop | hold | die | free, tick }` (a start past the grid stays: the update's
  step wraps or holds it by the mode, `free` keeps it, and it draws as the first frame meanwhile, as Link's `pick(16)`
  of 15), `kill` (it dies once this holds), `drop` (it holds at the end of the emission: the particle is not kept, its
  draws made; the "emit" reactions still run), `source = { size = ..., ... }` (each update sets these source values
  after the colour and before `kill`, in order; the particles after it and the emission read them: Regeneration's
  shrinking source; an effect var by its name, and `a.<var>` a var of the particle's anchor: Silence's orb size),
  `jump = "<when>" | { when, size }` (after the update, where it holds, the previous place and colour are the new ones
  and the previous size `size`'s value, else the size: the particle is drawn at its new place, not moving from the
  old; Bag's thrown balls), `on_death` (actions at its death; `{ emit = n, kind = ... }` queues n particles of the kind
  whose spawn's centre is its place, emitted at this tick's emission, emitting or not; `{ effect = ... }` starts one
  there at once; `set` an input or a var; the counts and values may read `p.*`; nothing else).

### Expressions

A number, `true` / `false`, or a string compiled once at load (`particle_expr.h`) into a stack program over four-lane
values (`Lanes`). The compiler knows each value's width (1 to 4): a scalar fills all four lanes, so it combines lane by
lane with a vector; two vectors of different widths are a load error; a scalar context (a condition, a size, an index)
reads lane x; a random draw draws once per lane it fills. A swizzle on a name or a value (`origin.x`, `sun.rgb`,
`(a + b).xy`, `aim.yx.x`) picks lanes, and on a name it narrows the load (`origin.x` compiles to the one float, as the
scalar names did). `vec2/3/4(...)` join values (`Op::Construct`; one scalar repeats); `dot length normalize cross
distance` (normalize as `v * (1 / length)`), `ball()` (`RandomInBall`, ten tries), `shell()` (the ball's point scaled
to length 1), `disc()` (an angle, then `sqrt(below(1))`), `circle(angle)`; the maths functions and `range`, `below`
work per lane. A var takes the widest width of its start and tick
(the reader reads the record again while a var widens, since a var may read one declared after it; those reads take a
swizzle past a value's lanes as its last lane, so a var whose expression swizzles itself still widens, and one strict
read then refuses a swizzle past a value that stayed narrow); vars sit one after another in their scope's floats
(`Var::slot`), a particle's in its eight.
Fast forms (`Classify`): a program whose every value is a scalar runs on one float a value (`Expr::scalar`), and `[x,
y, z]` of scalars the same way, its three read off the stack (`Expr::scalarParts`); the scalar functions (`sin` to
`pick`, `ScalarCall`) take floats. Each gives the four-lane run's lane x and draws (a unit check runs both). The v4
stack alone had made effect_check's coarse run (146 cases, 8 seeds) 17% slower; release builds on one machine
(MF15l1): 1.57 s after MF15l, 1.55 s with MF15l1's field forms and no update after death, 1.26 s with the fast forms;
MF15l2's files (vectors, plain lifetimes, velocity ticks): 1.37 s before, 1.11 s after (the least of 8 runs each,
interleaved).
Numbers, names, `+ - * / %`,
comparisons, `!`, `&&`, `||` (both short), `a ? b : c`, functions `sin cos atan2 sqrt abs floor ceil round min max clamp
lerp pow exp log`; of a point (its x and y): `ground(point)` (the ground's height there; 0 in the unit checks and in
effect_check's empty terrain), `on_ground(point, height)` (the point with z that height over the ground),
`platform_height(point)`, `platform_normal(point)` (the plane of a platform there, `PlatformSet::SurfacePlane`: 0 and
the normal (0, 0, 1) where there is none; GroundDust), `surface(point)` (the height of what stands there, a platform
over the ground or the ground, `PlatformSet::SurfaceHeight`: ShapeChange's glows; the ground without a platform in
the unit checks); random `range(lo, hi)`
(IsaacRandom::Range), `below(k)`, `chance(percent)`, `pick(n)` (Next() % n), a curve `name(t)`, a table `name[i]`
(truncated, held to the ends), a list input's point `name[i]`, an anchor group's values `<group>.<var>[i]`,
`<group>.position[i]`, `<group>.age[i]` ... (its anchor number i in the order made, truncated and held to the ends; 0
without one). Constants `pi`, `deg`. Names, by where the expression runs:
- everywhere: the inputs, the vars, the source's values (`size`, `height`, `growth`, `alpha`, `param`, `base_size`,
  `age`, `stage_age`, `stage`, `carrier_radius`, `carrier_z`, `attached`, `distance`, the vectors `motion`, `origin`,
  `wind`, `extent`, `count`, `index`, `total`, `wind_strength`, `turn_cos`, `turn_sin`, `emitting`: the source's
  emission flag, which the game's StopEmitting and Stop and an effect's `once` clear, `spread_min`, `spread_max` (the
  spread box, scaled by ScaleSize), `sun` (the scene's sun colour, r g b 0..1; 0 without a sun), `side`, `up` (the
  motion's side and up axes this tick, zero while the source moves 0.001 or less: the trails' across directions), the
  carrier as a unit: `carrier_unit`, `carrier_running`, `carrier_dead` (0 / 1), `carrier_unit_radius` (the unit's own
  radius, 0 else), `carrier_axis`, `carrier_axis_y` (its own x and y axes; 0 when free), `carrier_bounds_radius` (its
  bounds' radius as it stands, not measured again), `carrier_left_foot`, `carrier_right_foot` (its "ll3" / "rl3"
  parts, else its body, else its position raised by its bounds' top, as the host's DescribeCarrier finds a part; the
  origin without a unit carrier); written at the tick's start only for an effect that reads one of them),
  `anchors.<group>` (live count), `stage.<name>`;
- in an anchor's and its particles' fields: `a.index`, `a.count`, `a.age`, `a.ticks` (left), `a.position`, `a.spawn`,
  `a.<var>`;
- in a particle's: `p.age`, `p.life` (lifetime - age), `p.t` (age / (age + life)), `p.position`, `p.velocity`,
  `p.size`, `p.alpha`, `p.color` (0..255 a lane), `p.frame`, `p.anchor`, `p.index` (the values before this tick's
  update), `p.spawn` (at the emission; 0 in the update), `p.<var>` (as the vars' ticks left them). A particle var may
  not take one of these names (`age`, `life`, `t`...): a load error, since `p.life` would read the var.
A name the context lacks is a load error. Every operation runs lane by lane in the written order, so the draws come
in the written order too; an emission's parts draw in the order lifetime, spawn, vars, position, velocity, size,
colour, frame. Every draw is `g_particleRandom`'s.

### One tick

`TickSource` → `TickData`: the source's values written (the carrier's radius and centre measured at an expression's
first read in the tick, once: measuring recomputes the carrier's bounds, so an effect that reads them behind a
condition, `first ? carrier_radius : 0`, measures only when it holds), `State::Step` (events of this tick, the start's work or the
vars' and anchors' ticks, "stop" reactions, event reactions, "tick" reactions, the stage's move, the tick's rate /
chance / emitting), the `set` source values written back, a `stop` action's Stop, a `stop_emitting`, a `let_go`
(`ApplyActions`), then the source's `emitCount` and `emitChance` set for the generic
emission (not emitting: no tries, none from the movement either; the spacing is the source's, `[emit] spacing` scaled
by ScaleSize as an original type's) and the started sub-effects made. `UpdateParticles` → `UpdateData` per live
particle (life, frame, vars, the move, then the velocity's tick, size, colour, the `source` writes, kill; a particle
whose life ran out at the last tick is drawn once more where it was and pooled without an update). A `die` particle past its last frame ends
at the frame step, before it moves, as the originals'. `Emit` → `EmitQueued` (the actions'
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

### Lua hooks

MF15f (the MF15 study's §3: Lua at the emitter level, per particle only as a budgeted opt-in). An effect's `script =
"<file>.lua"` names a file in Res\effects (the top layer's of the name, `platform::FilesByLayer`) that returns a table
of hooks; how to write one: docs/modding.md, "Effect hooks". Code: `particle_lua.*` (the state, the scripts, `e` and
`p`, the budget), `particle_lua_api.inc` (the hooks and methods, declared once: the methods register from it, and
game/script/script_lua_api.h takes its rows for ei-convert's LuaLS file and docs/modding.md's tables).

- **The state**: one per program (`effectlua::Runtime`, made at the first effect with a script), apart from the
  mission scripts' (no game binding, never persisted, never on the server). The sandbox is the scripts'
  (engine/core/lua_sandbox.h); each file's top level runs with its own globals over the shared ones; `print` logs.
  Memory capped at 64 MB, each call at a million VM instructions (a count hook every 1000): past either, an error.
  Lua is compiled as C++, so every call is a `lua_pcall`; an error logs a `warning: effects:` line and turns that
  script's hooks off (`Script::broken`), the effect running on as its data. A Reset of the registry (a switch of mods)
  closes the state; the sources' `effectlua::Instance` (`ParticleSource::script`, its `e` table's reference) then
  belong to an older generation and run no hook.
- **The order in a tick** (`TickData`): the source's values written, `BeforeStep` (`on_start` at the first tick,
  `on_tick`, `on_event` for "stop" and for each event input with fires pending, `State::Pending`), the effects the hooks
  started made (the step forgets `effects`), `State::Step`, `AfterStep` (`on_event "end"` once the state ended). So an
  input set or an event fired in these hooks counts in the same step. `on_spawn` at the end of `EmitKind` (after
  `drop`; a kill there drops it, its reactions still run); `on_update` after `UpdateKind` of a live particle (a kill
  is a death: `on_death` runs). A position set outside world space moves the particle's `drift` too (its space's
  origin: the source's, its anchor's). `effectdata::Spawned` tells `on_event "spawn"`.
- **The budget** (`kParticleBudget`, 2,000): every `on_spawn` and `on_update` call between two renewals; past it the
  calls are skipped (the particle keeps the data's values; logged once per script). Renewed at each
  `TickSources` and each of the effect viewer's own ticks (`Viewer::Step`, so a replay spends it as the play did). A
  catch-up (`EffectParticle::Spawn`'s, a zone's exits, the viewer's scrub) runs many ticks between two renewals, so
  its Lua stays bounded: the plan's measures put a hook call at 0.2-1 µs, so 2,000 calls are 0.4-2 ms.
- **What a hook may do**: `e:set` / `e:fire` are `State::SetValue` / `SetVector` / `SetEnum` / `SetList` / `Fire`,
  the inputs' one path (MF15j2's replicated inputs take the same); `e:get` reads inputs, vars, the stage and the
  source values by their expression names (`FindSourceValue`; the carrier measured at its first read as an
  expression's); `e:random` / `e:pick` draw `g_particleRandom` (the viewer's own generator in the viewer); `e:effect`
  queues a sub-effect as an `effect` action. Hooks never nest (a hook's work starts no other).
- **Not saved, not in goldens**: the hooks touch only the client's particles and their generator; a loaded game makes
  the source again and `on_start` runs again. No original effect has a script, so effect_check's runs are unchanged.
- Checks: `ei-convert --lua-check`'s `CheckEffectHooks` (on_start setting each input type, a refused unknown input,
  e:get of inputs, vars and source values, an event fired in on_tick reaching on_event and the same step, on_spawn
  placing and dropping, on_update after the data's update, "stop", "end"; a catch-up of ten ticks of 300 particles
  stopping at the budget and the next renewal; a failing hook and an endless loop logged once, their effects running
  on; the live methods against the declarations); the `mod_effect_lua` scenario (the showcase mod's showcase_hooks:
  the script's inputs, an event answered, stages; captures `spiral`, `flare`).
- **The cost** (MF15f1, `effect_hooks_cost`, a timing run: `work.py test <ID> effect_hooks_cost --vanilla-only
  --quiet`; tools/make_lua_mod.py effecthooks): one source of 2,100 sparks (100 a tick, 21 ticks each) as its data
  alone and with a script whose on_spawn places each spark and on_update turns each round the centre through `e:get`
  and `p:get` / `p:set`, 2,100 calls a tick against the budget of 2,000. `renderstats` gives the client ticks' CPU
  time (five ticks a frame on the virtual clock). Two quiet runs, 2026-11-11, per tick: the zone alone 0.04-0.05 ms,
  the data 0.16 ms (0.11 ms over the zone: about 55 ns a particle-tick, as the study's "game: ~55 ns"), with the
  hooks 0.69-0.70 ms (0.54 ms more for 2,000 calls: about 270 ns a call, the study's "accessor" row of 155-250 ns a
  particle; the whole source about 6x its data). The budget's 2,000 calls are then about 0.55 ms a tick, at the low
  end of the 0.4-2 ms the study gives. A frame of the run: 2.1 ms with the data, 4.8-5.2 ms with the hooks. The
  world updates' share (the server, its mission script) is no part of it: 0.21-0.29 ms a frame in every segment
  (lua-scripting.md, "The collector's pause").

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
- **Written to say what they mean** (MF15l, MF15l2): the files once matched their C++ run for run; since the format
  review they say what the effect does (vectors, plain lifetimes, velocity ticks, variants) and may drift where it
  still looks the same: effect_check's exact reference is recorded from the data, and its coarse compare
  (tests/effects/coarse.txt, recorded before the review) holds the look ("Checks" below).
- **Changes by judgement**: Silence's wisps read their orb's last step, which the original read from the particle's
  pool slot before the orb had made one (a stale value of whatever used the slot before): a new orb's step is zero
  here. ShapeChange's glow reads its alpha table at its life left, which a path with repeated points (counted in the
  order, no glow made) takes past the table's 16 entries; the original read the data after it, the file holds the
  last entry (fully lit). Casting's close sparks of a restart past the close live one tick where the original's
  (a life of 0 or below) never died. Modifier's retry of a projection of length zero is left out (it practically
  never happens); PoisonFog's grid is laid about the origin where the C++ took the local position, the same for the
  free source the spell makes. FireBlast's fragments fly in a ball's direction turned up where it points well down
  (the original drew a ball shifted up by 0.7); the Tornado's and CurseHolder's pieces start between the two rings
  about their height as they move (the original rounded the height at the start).
- **Writing an effect from C++ arithmetic**: an expression's operations are float operations in the written order
  (`(below(0.4) + 0.8) * size * 0.04` is `(Below(0.4f) + 0.8f) * s.size * 0.04f`); `pos -= v; v *= f; pos += v` is a
  velocity start `v * (f - 1)` and tick `p.velocity * f`; `min(a, b)` is `b < a ? b : a` and `max(a, b)` `b > a ? b
  : a`; an integer division (ShapeChange's `order * 8 / count`) is `floor(...)` of the float one. Work done once at the
  first particle is a "before_emit" reaction under `made == 0` or `anchors.<group> == 0`; element state (rings, orbs,
  swirls, ground points) is anchor vars, read by number from another element (`swirl.p[k]`, `orb.r[0].x`); a C++ count
  set in the emission is a var `set` by an "emit" reaction; a draw made when the source is made is an `at_create` var
  or anchor group; what the world object did when it made a source (a scorch, a shake) is a "spawn" reaction.

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
| Tornado, CurseHolder | on data (MF15g6): 6 funnel-ring anchors leaning with the motion; the colour an `at_create` var from `sun` | |
| ShapeChange | on data (MF15g6): one glow per path point | `path` (a list input), `kind` (an enum: green, grey, orange) |

The runtime has what they need from MF15j: `ground` (anchors on the ground, kill planes), `add_anchor` (with
"before_emit", CastSwirl's orbiters), `pick = "first"` and `anchor_index`, a `list` input (ShapeChange's path),
`on_death` (a particle's sub-emission or sub-effect). `shake` came with Mushroom (MF15g4). Inputs the server sets
("In multiplayer and saves" below) came with MF15j2; no original type needs them.

### The format review (MF15l)

The 83 files say each thing one way (MF15l-MF15l3, user 2026-11-11: an effect may drift if it looks the same
overall): vector values and vars (`origin + p.spawn * 0.5`, `normalize`, `length`, swizzles), one field form (a value
or `{ start, tick }`, a tick alone starting too), the spawn as one point expression drawn before the vars, the
velocity's tick for every change of speed, a plain `lifetime` (a dead particle is drawn once more where it was and
pooled without an update), an anchor placed by its `position`, and variants (`like`, `before`, `remove`) for the
families. Each step was checked by effect_check's coarse compare and the viewer's captures (build/captures/mf15l1/,
build/captures/mf15l2/), then the exact reference recorded again.

### Checks

- `ei-convert --effect-check` (build checks; ctest `port_effects`; tools/convert/effect_check.cpp): every original type
  against its recorded run (tests/effects/reference.txt, built into the tool; recorded from the data again when an
  effect changes on purpose): 146 cases, 5539
  lines (carried, free, a fast carrier's distance tries, rescaled, StopEmitting / StartEmitting / Stop, the carrier
  removed and back, the world's wind, a sun; the carrier a stone, a stone with a path (`PathStone`, ShapeChange's
  points as OnPathMarker reads them), or a hero that runs, walks, dies and turns), each tick's
  particles (position, size, colour, frame, their previous values, the dead flag), origin,
  bounds, flags, RunTick's answer and the particle generator's draws, hashed per tick. No game data (no texture is
  loaded). Then the coarse compare (tests/effects/coarse.txt, also built in; `--coarse` alone, `--record-coarse
  <file>`): every case run with 8 seeds, per quarter of its ticks the births, deaths and age at death, the particle
  count, size, colour and the mean and spread of the places, each within a tolerance (`kTolerances`; a quarter with
  under one particle a tick compares its count alone). An effect changed on purpose passes it before its exact
  reference is recorded again.
- `tests/unit/particle_record.cpp` (ei_unit_check): expressions (precedence, short evaluation and its draws, tables,
  curves, errors), a record read and merged over a lower layer (a field by name, an element appended), problems named
  with file and line, the state machine driven by its inputs (stages by ticks, by `until` on an input, by an event;
  enter and event actions; anchors; `next = "end"`), a tick reaction's `when` and the stop action, `exp` / `log`, a
  source value `set`, `stop_emitting`, the carrier measured once and only when read, `set` on a var, `let_go`, "emit"
  reactions and their refused actions, `at_create` vars, `anchor_index` and the vars it refuses, MF15j's "before_emit"
  reactions adding anchors with a ball spawn (their draws in turn), `ground` of a point, a list input by default and
  set, `pick = "first"`, a spawn between two points, `on_death` queueing placed particles and an effect, the actions refused in
  `on_death` and "before_emit", MF15g4's "emit" reactions queueing a kind (and refused a kindless emit), the `shake`
  action and its form refused without a time, the `platform_*` functions, MF15g5's `ball()`,
  `<group>.<var>[i]`, the particle writes to vars and anchor vars and `jump`, MF15g6's "spawn" reaction with its
  scorches and shake (and a refused emit), MF15h1's scorch with a length and an angle (no draw) and `[server] wall`, `at_create` anchors and their spawns, `surface` of a point, `sun` and the
  frame mode `free`, and every whole effect written out in
  docs/modding.md (and in this file, which has none now) read without a problem (EI_SOURCE_DIR); MF15g5a's `like`:
  a variant over its base (a var and the texture replaced, a kind merged by name and one appended, a number array and
  an `[[on]]`'s action list replaced whole), a chain of two, a mod's layer over the base reaching the variant, and a
  missing base, a `like` to itself, a circle of two and a `like` that is no name refused with file and line, a var and
  a kind put `before` the base's and a var, a kind and an `[[on]]` taken out by `remove`; MF15l's vectors (a vector
  input, `normalize`, `length`, `dot`, `cross`, chained swizzles, `ball()`, a per-lane `lerp`, a var reading a later
  vector var, a var swizzling itself, vectors of two widths and a swizzle past a scalar var refused, a particle's vars
  over eight floats refused); MF15l1's field forms (a tick alone starting a source var, a particle's var, size, alpha
  and frame and an anchor's position; a spawn expression read by a var; an anchor at its spawn's point by default and
  its position's start and tick; a velocity tick). The POSIX port check (`port_platform`) runs the same checks.
- `mod_effect` (both installs; the showcase mod's `Res\effects\showcase_aura.effect.toml`): CreateParticleSource by
  name, the client's source and its stages (`effect ... check`), inputs set by value and by enum name, the burst's
  `set` seen (`effect ... value`), an event fired; captures `calm`, `storm`; an unknown name makes nothing.
- `tests/unit/mod_hash.cpp`: Res\effects counts.
- The goldens hold no particle, but a save's visuals.sav keeps the client particle generator (`visuals.particleGenerator`):
  a change of the client's particle draws (MF15l1: 110 vanilla and 27 Rebirth goldens) differs there alone; re-record
  only the saves (`golden.py saves --runs <pass1> <pass2>`, standalone-verification.md, "Recording goldens").

## The effect viewer

`ei-effects [<effect>] [--mod <folder>]` (src/tools; how to use it: docs/modding.md, "The effect viewer"; MF15k4) is
a program of its own on the engine (ei_engine, ei_render, ei_platform; no ei_game) with dear imgui's docked windows
over a scene of its own. The game's exe has none of it: no src/tools, no imgui (its map file names no imgui symbol).

- **The program** (tools/effects_main.cpp): the game folder as the game finds it (EI_GAME_ROOT, else
  `platform::DefaultGameFolder`), the engine's folders as the game's settings give them by default (`.\`, `.\res\`),
  `--mod` through `platform::SwitchMod` (a folder or a .zip, a relative one from the exe's folder), a 1600x900
  window that resizes freely (the projection keeps the 4:3 part's proportions, as screen_layout does), vsync. It reads
  no settings file and writes only its log (ei-effects.log in the output folder: the build gives log.cpp the name,
  `EI_LOG_NAME`), the windows' layout (`<user data>\ei-rebuild-effects\layout.ini`) and the files the user saves.
- **The scene** (tools/effect_viewer_scene.h, `ViewerScene`): a `ManagerContext` of its own with the install's texture,
  material, light and figure managers and a particle manager. The engine finds them as it finds the client world's:
  the scene renderer's `world` is the scene, and ManagerContext's getters are virtual, null by default (the game's
  World, WorldClient and Party answer theirs). So `LoadNamedTexture` and the figures' loader need no game code.
  - The ground: a 48x48 checkerboard of flat blocks (DrawFlatBlocks) at height 0, the host's `groundHeight`.
  - The figure: the hero's (`unhuma`, the game's hero parts), at half scale, in its bare skin (a texture composed of
    redress.res's `unhumaskin_00.mmp` alone, as a dressed unit's is of its skin and items), with the animations of its
    database (database.res, `<model>.adb`); the first idle animation plays, `figure animation <name>` picks another.
    It can walk a circle (radius 2.5, `figure walk on`) and run (`figure run on`: twice as fast, the host's `running`);
    each gait plays its first walk / run / idle animation (SetGait).
  - The points of the figure (`ViewerScene::Points`, the Place window's Point and Other bone lists; `Viewer::point`):
    the file's `part` (SetUp's flags as they are), the origin (OnPart cleared), the game's points (OnPart and the
    AttachPoint, the way a file's `part` and the game's code put a source on a unit: `effect_point` is a hero's
    Unit::EffectPoint, its "rh3"; body00-04 only when the figure has such parts), the feet the effects read ("ll3",
    "rl3") and a creature's weapon point without a weapon ("baserh3", UnitClientSpecific::WeaponPoint's hand node),
    then the bones that carry a part and the other bones (a hero's skeleton has about 350: every armour's and weapon's
    node). A bone goes through the host's `namedPoint` (OnPart set). The pick holds across effects; a sub-effect is
    free (StartEffects), so it never sits on the point.
  - The host is `particlehost::FigureHost` ("The host"); the free camera turns on the right mouse button, pans on the
    middle one, zooms on the wheel and moves on WASD / the arrows, built as the game's camera is (Camera::ApplyView).
  - The clock is the game's: a tick every 55 ms, at most five caught up in a frame; the frame's fraction goes to the
    host, so the particles interpolate as in the game.
- **The windows** (tools/effect_viewer_panel.cpp): a dockspace over the main window (its centre passes the scene
  through), with Effects, Timeline, Inputs, Fields and Text as dockable windows. The first layout (no layout file)
  puts Effects over Timeline on the left and Inputs over Fields / Text (tabs) on the right (`DefaultLayout`,
  DockBuilder); dear imgui saves the layout as it changes and at the exit.
- **The harness** (tests/verify/effects/effects_harness.cpp, the verify build only; tools/effects_app.h's hooks):
  run_all plays a scenario with ei-effects when it has the line `# exe: ei-effects`. Its steps are `mod`, `effectviewer
  <command>` (among them `place point <key>`, `points` into the log, `camera <distance>` on the figure, `mark` and
  `expect at <bone> [dz]` / `expect moved <min>`: the source's carrier point within 0.05 of a bone's, since a blend
  moves the drawn pose between ticks), `wait`, `frames`, `capture`, `timeout`, `report` and `quit`; it writes harness_report.txt and
  harness_quit as the game's harness does, and no digest (run_all asks none of such a run). A save outside the run's
  output folder fails the run.
  - tests/scenarios/effect_viewer_standalone.txt starts the exe with `Portal` (run_all's `# arguments:` line).
- History: MF15i put the viewer over the running game (`ei-rebuild --effects`, its hooks in the main loop, a new
  game in a `--zone`); MF15k1-k3 split the engine from the game and MF15k4 made the exe and dropped that mode.

- **The source**:
  - The scene's particle manager makes the source (`CreateObjectAs`, the effect's shown type), and only the viewer
    holds it (AddRef, not in the manager's `held`). DrawAll draws it like any other source.
  - TickSources ticks every source in its list, held or not, so the source carries `ParticleSourceFlag::OwnClock`,
    which TickSources skips. The viewer clears the flag before it lets the source go back to the spares.
  - The viewer ticks it after each of the scene's ticks (`SceneTicked`), so the frame's tick fraction interpolates it
    as it interpolates the others.
  - Its draws come from the viewer's own IsaacRandom, swapped with `g_particleRandom` around its set-up and its ticks.
    The game's generator is untouched, and every replay draws the same.
  - It is placed as EffectParticle::Spawn places a spawned effect (localPosition, ScaleSize(size / baseSize),
    Attach).
  - The viewer lets go of it before the scene stops (`Close`).
- **Sub-effects** (a data effect's `effect` actions, `on_death` too): StartEffects makes those of a source with
  OwnClock through `effectdata::g_startOwnClock`, which the viewer sets at Open (`StartChild`): unheld, one reference
  of the viewer's, OwnClock too, so their own sub-effects come the same way.
  - Step ticks them before the source, with the viewer's generator (those made at a tick begin at the next), and lets
    go of each whose tick says it is done. The effect has ended when the source and every sub-effect have.
  - Restart, a pick and `Close` let go of them with the source; Freeze holds their particles too.
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
  - Scripts (MF15f1): the Lua runtime keeps a script from its first use until `effectlua::Reset`, so the viewer resets
    it at a pick, a Scan (the Scan again button, the `scan` command) and a save (`Viewer::ReadScripts`), and makes
    the picked effect's source again up to its tick (the old source's hooks belong to the closed state). An edit saved
    in another editor takes effect there; the registry's records are untouched (the document is the TOML's).
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
  - Dear ImGui v1.93.0, its docking branch (third_party/imgui, unmodified, no backends of its own).
  - Its platform side is tools/imgui_layer.cpp: platform events, the texture protocol (`RendererHasTextures`), a
    scale of frame height / 1080, docking on and the layout file.
  - Its renderer side is the renderer's overlay (renderer.md, "render_overlay.cpp: the tools' overlay").
- **Check**: the `effect_viewer` scenario (ei-effects on the showcase mod).
  - Portal from its built-in file: pause, scrub, step, a field edited and the file saved into the run's output folder.
  - showcase_aura on the figure, with its enum, float and event inputs and its stages.
  - showcase_hooks with its script rewritten in the run's copy of the mod (the harness's `write` step): a scan plays
    the new script up to the same tick (its on_start's `burst`: the stage flare), a save the next (no hooks: glow).
  - Tornado from its built-in file, on the figure walking its circle, running.
  - A new effect from the template with a tick reaction that starts Teleport: the sub-effect's particles hold while
    paused and are the same after a scrub to the same tick.
  - Captures `viewer_portal`, `viewer_portal_edited`, `viewer_aura`, `viewer_tornado`, `viewer_new`.
  - `effect_viewer_standalone`: the exe started on Portal (on the figure), scrubbed and stepped, an animation picked.
  - `effect_viewer_points`: CastingFire's own `effect_point` at the hand (rh3), the game's points (head, above_head,
    left_foot) and a bone (lh3) followed standing, walking and running; captures `viewer_points_hand`,
    `viewer_points_walk`.

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
