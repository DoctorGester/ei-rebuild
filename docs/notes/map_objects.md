# Map objects

Code: `map_object.h/.cpp` (MapObject, MapEffect), `map_object_companion.cpp` (MapObjectServer /
MapObjectClientSpecific), `map_object_client_effects.cpp`, `map_object_manager.cpp`, `object3d.*`,
`object3d_parts.cpp` (Object3D parts, scale, bounds), `object3d_client.cpp`, `map_object_shadow.*`,
`map_object_draw.cpp`, `map_object_animation*.cpp`, `models/animation_bones.cpp`. Scenarios: `map_objects.txt`,
`unit_create.txt` (also world_load, models and every spawn).

## Classes

- `MapObject` (under `Object3D`, under `EngineObject`) is every object on a map; `Unit` derives from it. Its side
  halves: a companion (`MapObjectServer` on the server: the statistics block, hit points; the client's
  `MapObjectClientSpecific`: looping effect sounds, effect particles, the light, the end sound) and the 3D object's
  side half (`Object3DSpecific`: `Object3DClientSpecific` draws, `Object3DServer` is all stubs; GetScale gives the
  client's scale or 1, 1, 1 on the server).
- An Object3D's `parts` hold a reference each (LoadPart takes it, RemovePart and Destroy release it); the client
  half's part groups hold their own. A server part goes back to the figure manager's spares at Destroy.
- `Unit`'s update runs its controller's, then `MapObject::Update`, which runs the companion's.
- Flags: 1 dead / destroyed, 2 dirty, 8 (network part 2), 0x10000 invisible, 0x40000 stunned (client,
  effect 0x19). A dead object has no effects.
- `MapObjectManager` ("map_objects") makes objects by class; `PrototypeEquipment`, `GiveDefaults` and the perks
  database ("perks.pdb", units.md) sit beside it.

## Effects

- The server's Update counts effects down and removes those at 0; per removal: RecomputeScale for a scale effect
  (strengthen 0x1D, weaken 0x1E, enlarge 0x25, shrink 0x26), MarkChanged(3), and the speed and visibility slots unless
  a 0x19 / 0x1C effect is left. For a unit, a scale change rebuilds the figures and can kill a unit that cannot stand.
- AddEffect renews an effect already present (as Rebirth's FixSpellEffectsOverflow does, without its 20-effect cap or
  its skip after a removal).
- The client counts its copy down too (network part 3 resends the list). For text-key objects it plays
  "magic\end1.wav" under 30 ticks left; a kind's loop by ticks (stench's "magic\Stench\1..3.wav" every 17 ticks)
  draws the client generator; a shown light sits 2 above the object. `EffectsChanged` rebuilds the look and sound
  records: one per effect kind at its highest power, from the kind's record (items_spells.md, "Active effects on
  scripts").
- Light modes: 1 holds the light on and lists it with the light manager, 2 drops it.

## Shadows

The renderer's shadow map draws the shadow (renderer.md); `DrawPartsShadow` hands the object over as a caster
(`render::CastsShadow`) and fits the shadow's sphere, which `ShadowInView` tests for objects out of view
(`CollectVisible` lists them for shadows and updates their pose). The sphere: the object's bounding sphere swept along
the lights' direction down to the ground, taken as level at the lower of the object's origin and the sphere's bottom;
a box around the sweep, the sphere around the box. The original projected the figure's parts from the light into a
64x64 texture laid on the ground as a decal and took the decal's sphere (0.3-1.1 ms a frame for a value only
ShadowInView reads); the sweep lists a few more objects out of view (more poses updated, the same casters drawn).
`shadowMade` means "the sphere was fitted" (the first draw after Create / ClearShadow fits it; until then ShadowInView
lists the object).

## Drawing

`Object3DClientSpecific::DrawParts` is described in `map_object_shadow.h` and `map_object_draw.cpp` (lights within range +
10, matrix stacks, the part sets). Picking: the parts' screen rectangles. Wounds: a texture of kind 6 made per object,
reloaded unless its .mmp extra block is unchanged, drawn with a two-stage material.

## Figures

LoadFigures reads the model named by the object through the figure manager; SetFigureScale rebuilds the figure only
when the scale changes. The figure is a `LinkMesh` (link_figure.h, models.md) whose piece map is `headers`
(map<string, LinkPartHeader>). Part lookups: FindPartByPrefix (strstr at the name's start, case-sensitive), FindPart
(strcmp), FindPartIndex (_mbscmp). Animations come from "<name>.adb" on first use:
`MapObjectManager::AnimationDatabaseOf` reads the file into an `AnimationDatabase` (its header's time factor and the
0x58-byte entries, each a `FigureAnimation` that points back at its database) and keeps it by file name until the
manager's Destroy, so Object3D::animations, FindAnimation and FindFigureAnimation share one database per name. What
an object plays need not come from its own database (UiModel's and the main menu stones' rest pose come from
FindAnimation by name), so the database of a playing animation is its entry's `database`, never the object's. Four of
database.res's files (unmozozo, unmoco3, unmoco4, inmm1) are of an older, shorter entry and end inside their last
0x58-byte entry: they are read at the 0x58 stride like the original, the bytes past the file as zero (the original
read past the file, and found an entry's database by its index byte, which these files do not hold).
`AnimationFactor` (units only) is (start + (end - start) t) / length, 1 for a 0 / NaN length.

## Skeleton

The animation state (class 0x300D) holds the bones (class 0x300C, `FigureBone`) in `bones`; each bone's animator and
placing track are `AnimationTrack`s (class 0x300B). `FigureBone::Place`: a child is the parent's position plus its
offset turned by the parent's rotation, its rotation pose * parent (Hamilton); the root is the pose (times the
animation's time factor) less the origin; the mesh gets both and the pose's vertices. LinkBone makes a bone the child
of the first bone named as its parent unless that one already has a child of the name; FindRoot is the first bone
without a parent name.

**Animation tracks and cages**: AnimationTrack::Evaluate uses the CRT's modf of the time rounded to an integer and
`Quaternion::Slerp`; LoadKeyFrames copies the key frames (rotations, positions, the frames' vertex sets) out of the
.anm entry into the track's own vectors. The bone's cage (`LoadFigureBones`: its offset at its morph
coefficients) is `fig::MorphPrism` / `fig::MorphCube` (figure_mesh.h) over a 0x48-byte / other record. UpdatePose evaluates the tracks at the frame,
then places the skeleton at AnimationFactor(scale.z) * defaultScale, one float factor for x, y and z.

**Blends** (client side only): a switch of animation blends the pose last placed into the new one's over 100-250 ms
(BeginBlend, FigureBone::Place's `blend`); renderer.md section 14, "Animation blends".

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9; C84 triaged the list, with a
recommendation per entry). Crashes, undefined behaviour and invisible bookkeeping are fixed, not listed.

- The client's stench sound tests the record's kind a second time after drawing the random number (always true); the
  timer runs 0x10 .. -1, so the period is 17 ticks.
- A source of an unknown type logs and uses 0x2013.

By design (user 2026-11-03): RecomputeScale adds the net enlarge / shrink level to the strengthen level when enlarge or
shrink is present (strengthened + enlarged grows by 0.8; strong units get wider) and leaves y alone for strengthen /
weaken; heroes keep their height.

## Virtual slot names with low confidence

Guesses to re-check when a caller needs them: MapObject 29 `IsSynchronised` (flags 0x20000, set by the zone loader for
records with a script name; units, levers, traps, torches answer 1; SendFreshObjects moves the objects answering 0 out
of the map-object maps), 39 `IsDirectUseBlocked` (levers; a plain click uses an object only when 38 IsUsable is set and
39 is 0), 43 `KeepsMapFlag` (levers); UnitController 15 / 17 `Unused*`, 36
`HasOtherArmour` (server: armour slots 3..6, client: any weapon model; they differ); World 37 / 38 `ClientManager194` /
`ClientManager198` (not identified).
