# Map objects (00500000-00522000)

Code: `src/game/map_object.h` (CMapObject, MapEffect), `src/game/map_object.cpp` (the base class),
`src/game/map_object_companion.cpp` (CMapObjectServer / CMapObjectClientSpecific methods).
Verify: `tests/verify/verify_map_object.cpp`. Scenario: `tests/scenarios/map_objects.txt`.

Shared files touched: `src/game/unit.h`, additive only: method declarations in the existing classes
`CMapObjectServer` (Update, SetData, TakeHit) and `CMapObjectClientSpecific` (Update). No layout
changed.

## What lives in this range

units.md mapped 0050E000-00522000 roughly; in more detail:

| Range | Contents |
|---|---|
| 00500000-0050E000 | rendering and animation (units.md; not looked at here) |
| 0050E100-0050E2D0 | small helpers: a CPlex free-list node allocator (0050E120), AIRef Load/Save (0050E220 / 0050E290, unit_ai.cpp) |
| 0050E2D0-0050F730 | **CMapObject**, the base of every map object (vtable 0073DAA0, 67 slots): constructor 0050E2D0, destructor body 0050E660, Create (slot 2, 0050E770), Destroy (slot 3, 0050E840), attach companion (slot 19 0050E890, 0050E8F0), MarkChanged (0050E9F0), network state write / read (slots 50 / 49, 0050EAF0 / 0050EC30, part by part through the stream at 0047FBF0..), effects (AddEffect 0050F1E0 slot 45, ClearEffects, HasEffect, EffectValue), scale (slots 27 / 30), position (slots 48, 51-53), Load / Save (slots 9 / 10, 0050F7A0 / 0050F900) |
| 0050F900-00510340 | CArray instantiations: MapEffect (0x14 bytes, vtable 0073DBA0, SetSize 0050FE70, Serialize 0050FFD0) and the 0xA0-byte records at +0x1C4 (vtable 0073DB94, SetSize 0050FA30) |
| 00510450-00512600 | **CMapObjectClientSpecific** (0x3A, vtable 0073DBEC; its base 0073DC54 is the abstract "map object companion"): constructor 00510450, per-tick Update (slot 6), effect particles (00510B80) and looping sounds (005112A0, called together by slot 13 005117D0 when the effect list changes), effect sounds by type (005114D0: "magic\Fireshield" ... "magic\Shrink" + "_start.wav" / "_end.wav"), the light (+0x50: 00511830 create, 00511950 show, 005119B0 hide, 00511A00 slot 19 mode 1/2) |
| 005125E0-00512730 | CMapObjectManager ("map_objects", class id 0x2D, vtable 0073DCD8); 00512730 is its object factory (operator new(0x4DC) for units) |
| 00512730-00515600 | map object loading and the map object specs (not looked at) |
| 00515610-00515BD0 | **CMapObjectServer** (0x39, vtable 0073DD6C, 21 slots): constructor 00515610, per-tick Update (slot 6), SetData, TakeHit (slot 20), Load / Save (slots 9 / 10), IsDead / IsOverkilled (unit.cpp) |
| 00515BD0-0051AC30 | **CObject3D**, the 3D object base under CMapObject (vtable 0073DDD4, 33 slots; constructor 00515BD0, destructor body 00515D70): position, orientation, figure parts and their CList (+0xA8), scale (+0xC4, set by 00517700), figure loading ("CObject3D::LoadFigures") |
| 0051AC30-0051F240 | CObject3DClientSpecific (0x37, vtable 0073DE90; base 0073DEFC): drawing ("CObject3DClientSpecific::Draw") |
| 0051F240-0051F340 | CObject3DServer (0x38, vtable 0073DFBC): every slot is a stub (return 0 / 1, empty) |
| 0051F340-00522000 | the perks database ("perks.pdb", load 00520220 / save 0051FF40) and its STLport vector instantiations |

### Classes

- **CMapObject** (0x238 bytes; CUnit's own fields start at +0x238): CEngineObject -> CTransformObject
  (0073BA38) -> CObject3D (0x170 bytes) -> CMapObject. Fields: +0x1C position, +0x34 orientation,
  +0xA4 name, +0xC4 scale, +0x128 a point offset, +0x170 the companion, +0x194 the effect CArray
  (+0x198 data, +0x19C count, +0x1A0 capacity, +0x1A4 grow-by), +0x1A8 a second name, +0x1AC the
  base scale, +0x1B8 / +0x1BC sector cell, +0x1C0 change stamp, +0x1C4 a CArray of 0xA0-byte records,
  +0x1DC changed-part bits, memory streams at +0x1E8 and +0x210.
- **MapEffect** (0x14): type byte, power byte, +4 ticks left, +8 a value (a float for EffectValue;
  the client keeps a sound handle there), +0x10 a byte that silences the end sound. Same record as
  unit.h's UnitEffect.
- **Companions** (+0x170): CMapObjectServer on the server, CMapObjectClientSpecific on the client;
  their +0x1C points back at the object (CompanionOwner). Units have one too: CUnit's slot 6
  (0052F7D0) runs its controller's update and then CMapObject::Update, which runs the companion's.
  CMapObjectServer keeps a 0x58-byte statistics block at +0x20 (hit points +0x28 / +0x2C).
  CMapObjectClientSpecific keeps the looping effect sounds at +0x40 (0x14 each: kind, -1, handle,
  timer, present), the effect particles at +0x2C (0x10 each), the light at +0x50 and the end
  sound handle at +0x54.
- flags_ bits seen: 1 dead/destroyed (TakeHit), 2 dirty, 8 (network part 2), 0x10000 invisible
  (HideObject, the client for effect 0x0F), 0x40000 stunned (client, effect 0x19).
- Virtual slots used here: 6 update, 17 / 18 IsDead / IsOverkilled (companions), 19 attach
  companion, 21 IsInvisible, 27 RecomputeScale, 28 effects changed, 29 flag 0x20000, 30 ResetScale,
  33 scale effect changed (argument 0/1), 36 SetServerData, 45 AddEffect, 46 stunned flag
  (CUnit::SetFlag1000000), 48 GroundPoint, 51 PlaceOnGround, 52 / 53 GetPosition.

### The effect tick

Each world update the server's CMapObjectServer::Update counts every effect of its object down
(00688530) and removes those that reach 0 (memmove, count - 1); after each removal: slot 27 when it
was a scale effect (strengthen 0x1D, weaken 0x1E, enlarge 0x25, shrink 0x26), MarkChanged(3),
slot 33(scale effect?), slot 28, and slot 46(0) unless a 0x19 or 0x1C effect is left. The client
counts its copy down too (network part 3 resends the list) and, for text-key objects (scripted
units), plays "magic\end1.wav" while an effect has fewer than 30 ticks left (unless the previous
one is still playing); the stench effect plays one of "magic\Stench\1..3.wav" every 17 ticks (an
ISAAC draw picks it); feeble mind's loop follows the object; the light (if shown) is put 2 above
the object and aimed at it whenever it is not there.

RecomputeScale: scale = base scale (slot 30), then with enlarge/shrink (net level n = has(0x25) -
has(0x26), plus the strengthen level) every component grows by 0.4 * level, or without them
strengthen/weaken changes x and (except for heroes, type_ 0x32) z; components are clamped at 0
with the original's branch-free select (NaN kept).

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 0050F1C0 | CMapObject::Update (slot 6, per tick) | ours only: returns 1 (the companion's update is verified on its own) |
| 00515820 | CMapObjectServer::Update (slot 6, per tick) | when no effect runs out (the common case: it only counts down): ours, effect records and tracked bytes restored, original; compared. Otherwise ours only: the effect list must end as predicted (every count - 1, the ones at 0 removed, order kept) |
| 00510890 | CMapObjectClientSpecific::Update (slot 6, per tick) | ours only: positive effect counts - 1, stench timers - 1 (16 after going negative), others unchanged, a shown light unchanged or at the object + (0, 0, 2) |
| 0050F5B0 | CMapObject::ClearEffects | ours only: no array, count and capacity 0, part 3 changed, dirty |
| 0050F690 | CMapObject::EffectValue | pure (float bits) |
| 0050F2D0 | CMapObject::RecomputeScale (slot 27) | objects whose slot 30 is 0050E440: ours, snapshot of the first 0x1E0 bytes, restore, original; bytes and EAX compared. Units (slot 30 rebuilds the figures through 00517700, which must not run twice): ours only, part 1 and dirty |
| 0050E440 | CMapObject::ResetScale (slot 30) | snapshot compare, EAX (the z bits) too |
| 0050E4B0 | CMapObject::GetPosition (slot 52, RET 8) | output only: original into a scratch Vector3; both return their `out` |
| 0050E4D0 | CMapObject::GetPosition2 (slot 53, RET 4) | same |
| 0050F730 | CMapObject::GroundPoint (slot 48) | output only (the world's ground height is a query) |
| 0050F020 | CMapObject::PlaceOnGround (slot 51) | ours, z restored, the original through a hand-written stub (below); z compared |
| 0050F050 | CMapObject::SetServerData (slot 36) | snapshot of the object (0x1E0) and a CMapObjectServer companion (0x78); EAX too |
| 0050E8F0 | CMapObject::AttachCompanion | ours only: attached, points back, one more reference |
| 0050FA10 / 0050FA20 / 0050E430 | CMapObject::IsInvisible / IsStunned / Flag20000 | pure |
| 00515950 | CMapObjectServer::SetData | snapshot (companion 0x78, object 0x1E0); EAX too |
| 00515980 | CMapObjectServer::TakeHit (slot 20) | ours only: returns 1; a dead object has no effects and flags_ bit 1 |

0050F020 starts PUSH ESI; MOV ESI,ECX; CALL 00455950, so no stolen-byte count works: the wrapper
runs `OriginalPlaceOnGround`, a naked stub that repeats those three instructions (the call through
a register) and jumps to 0050F028.

The compiled x87 code of RecomputeScale, EffectValue, GroundPoint, PlaceOnGround and the light
code of the client update was read back (`dumpbin /disasm`): the stores to float happen where the
original stores, NaN comparisons keep the original's sense (`!(a >= b)` for C0 selects, `x < 0 ||
x > 0` for C3 tests). The compiler adds offset + position instead of position + offset in
GroundPoint, which gives the same result. Not run in game or offline yet; the scenario exercises
the per-tick updates, effects, a scale effect, death and removal.

All were renamed in Ghidra, with 0050E2D0 CMapObject::Construct, 0050E660 DestructBase, 00515BD0
CObject3D::Construct, 0050F1E0 AddEffect, 0050FE70 MapEffectArray::SetSize, 00688530
MapEffect::Tick, 0050EAF0 / 0050EC30 Write/ReadNetState, 005112A0 / 00510B80 / 00511A00 (client
effect sounds, particles, light mode).

## MarkChanged and HasEffect

They are CMapObject methods (they touch nothing beyond +0x1DE) but stay declared and rewritten on
CUnit (unit.h / unit.cpp / verify_unit.cpp): moving them means editing three shared files that the
units area owns. CMapObject reaches them through their game addresses (forwarders to 0050E9F0 /
0050F630, which land on the CUnit rewrites). For the standalone build, move the two bodies to
CMapObject and let CUnit inherit them (CUnit should then derive from CMapObject; its fields up to
+0x238 duplicate CMapObject's).

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- This range: 0050E9F0 / 0050F630 (above), 0050F5B0 and 00515950 (rewritten here; called through
  their addresses so the verify wrappers see those calls).
- 00455950 CEngineObject::World, 00688530 MapEffect::Tick, 00524FE0 (units range: applies a hit to
  the companion's statistics block).
- Sound system (global 007AF784): 006B8600 play a 3D sound (7 arguments), 006B7C10 / 006B7CB0
  handle status tests, 006BA400 move a sound. Light manager (007AF738): 006CA830 aim a light.
- Called directly (rewritten elsewhere, not patched by Rebirth): IsTextKey, IsaacRandom::Next,
  mfc::CString (Format, destructor).
- Virtual calls: map object slots 6, 27, 28, 30, 33, 46; the world's slot 27 (ground height, ST0,
  declared double); companion slots 6 and 17; GetClassId; Release.

## Original bugs and oddities kept on purpose

- The client's stench sound tests the record's kind for 0x13 a second time after drawing the
  random number (always true there); the timer runs 0x10 .. -1, so the period is 17 ticks.
- RecomputeScale adds the net enlarge/shrink level to the strengthen level when enlarge or shrink is
  present (a strengthened, enlarged object grows by 0.8), and leaves y alone for strengthen/weaken.
  Heroes keep their height under strengthen/weaken.
- CMapObjectServer::Update passes slot 33 a byte flag whose upper bytes are stack garbage in the
  original; ours passes 0 or 1 (units' slot 33 forwards it to the controller, which is not
  rewritten; assumed to read the byte only).
- GetPosition (slot 52) takes a second argument it never reads.
- PlaceOnGround leaves the ground height call's EAX as its result; ours returns nothing.
- TakeHit treats a 0 from 00524FE0 as "not hit", but 00524FE0 always returns 1.

## Skipped and why

- 0050F1E0 AddEffect: patched by Rebirth (docs/rebirth-patched-functions.txt).
- 0050F170 (fade mode from a float, slot 47 with 0/1/2), 0050E890 (slot 19, attaches only type 0x47
  companions, else CObject3D's 00516D50): small, next candidates.
- Constructors / destructors (0050E2D0, 0050E660, 00510450, 00510560, 00515610, 00515720, 00515BD0,
  00515D70) and Create / Destroy (0050E770, 0050E840, 00510770, 005107B0, 005157C0, 005157E0): many
  container members, exception frames.
- Network state (0050EAF0 / 0050EC30), Load / Save (0050F7A0 / 0050F900, 005159D0 / 00515AC0) and
  the CArray Serialize/SetSize instantiations: stateful, stream classes not rewritten.
- The client's effect visuals (00510B80 particles, 005112A0 sounds, 005114D0 sound names) and light
  helpers (00511830, 00511950, 005119B0, 00511A00): large, many callees; next candidates after the
  per-tick update is confirmed in game.
- 0050E970 / 0050E9B0 (slots 32 / 34, statistics rebuild through 00522C50 / 005136D0) and 0050F080:
  depend on the UnitStats builders.
- CObject3D (00515BD0-0051AC30), CObject3DClientSpecific (drawing) and CObject3DServer (stubs);
  the perks database (0051F340-00522000).
- Trivial bodies too short for the 5-byte jump (`MOV AL,1; RET`, `RET`, `XOR EAX,EAX; RET`, FLD
  constant; RET).

## Rebirth combat report (all 18 in tests/verify/disabled.txt)

With these hooked, combat.txt's goblin fight was lost in 2 of 5 Rebirth runs (13 of 13 won
without them). Reviewed again against the disassembly without finding a divergence:
- Precision: no rewrite rounds a value the original keeps in ST0. EffectValue only loads stored
  floats (exact at any precision); GroundPoint/PlaceOnGround keep the ground height unrounded
  (VCall<double>, no spill); RecomputeScale's 0.4 product is exact and the sums are stored to float
  where the original stores; the client's light code only feeds a zero test and float stores.
- Slot 33's argument: every receiver reads only the byte (005533B0 `MOV AL,[EBP+8]`; the base
  controller's slot 19 00533BD0 and the map object base 0050E960 ignore it).
- Rebirth: addon.dll / EI_Plugin.asi reference none of the 18 bodies except EffectValue, called
  through a plain thiscall helper whose result is stored to a float; their patch-address tables
  hit neighbours (AddEffect, Create, net state, client particles/sounds), not these functions. No
  game.exe jump lands inside the 5 bytes the hooks overwrite.
- TakeHit (slot 20) has no caller in the combat path; crippling (00525070) does not use the effect
  list.
To get evidence instead of a consistency check, CMapObjectServer::Update now runs both versions
differentially on every tick in which no effect runs out. Suggested bisection: re-enable the pure
and differential ones first (EffectValue, GetPosition/GetPosition2, GroundPoint, PlaceOnGround,
ResetScale, the flag getters, SetServerData, SetData, CMapObjectServer::Update), then the
ours-only ones (CMapObject::Update, ClearEffects, AttachCompanion, TakeHit, RecomputeScale,
CMapObjectClientSpecific::Update).

## Risks

- None of this has run in game yet. The per-tick functions run for every map object every world
  update, so a mistake shows at once (map_objects.txt and any other scenario).
- CMapObjectServer::Update's verify predicts the list from the old one; a slot it calls that changes
  the list (a unit's slot 27 reaches its controller's slot 44, which can kill a unit that cannot
  stand after a scale change, clearing its effects) would report a false mismatch.
- The client update's sound and light calls are checked only for their effect on the companion's
  fields, not for their arguments.
- unit.h gained method declarations in CMapObjectServer / CMapObjectClientSpecific; a branch that
  moves those classes must take them along.
- The scenario's spell strings ("speed{d1}", "shrink{e2;e2;d2;d1}") were taken from database.res
  without running them; CastSpellUnit with them only exercises the effect tick if they parse.

## Second round: figures, shadows, lifetime

Code: `src/game/map_object_figures.h/.cpp` (CObject3D: figure parts, scale, bounds),
`src/game/map_object_shadow.h/.cpp` (CObject3DClientSpecific::DrawShadow),
`src/game/map_object_lifetime.cpp` (constructors and destructor bodies of the family),
`src/game/map_object.cpp` (slots 19 / 32). Verify: `tests/verify/verify_map_object_figures.cpp`,
`verify_map_object_lifetime.cpp`, `verify_map_object.cpp`. Scenario `tests/scenarios/unit_create.txt`
(also world_load, models and every spawn reach this code). units.md "Fourth round" has the unit
side (prototypes, the factory, the client's armour models).

Shared files touched, additive: `map_object.h` (CMapObject::AttachObject, PackVitals, Construct,
DestructBody). `map_object_figures.h` and `map_object_shadow.h` are new.

### Figures (CObject3D)

Layout (0x170): +0x34 / +0x44 two orientations, +0x94 the attached object (type 0x46; slot 13
"part added", slot 21 "figure loaded", slot 26), +0x98 the animation state, +0xA4 the figure's
name, +0xA8 an MFC CPtrList, +0xC4 the scale, +0xD0 the parts and +0xE0 the pending parts
(AlignedPtrArray: 32-byte aligned storage with the block at data[-1]; SetAtGrow 00519E80, SetSize
00519D70), +0xF0 the figure (counted; slot 0 deletes), +0x110 a CStringArray, +0x124 bounds flags,
+0x128 the centre (= CMapObject::pointOffset), +0x134 radius, +0x138 / +0x144 the half extents
below / above it, +0x15C a CArray of 0x38-byte animation frames. A part (FigurePart, a scene
node) keeps its position at +0x1C and the piece's offset and box at +0x18C..+0x1B0; the figure's
piece map (figure + 0x14, an STLport map<string, piece>, find 005197B0) supplies them.

- LoadFigures (00517500, slot 26): the model named +0xA4 (figure manager = ManagerContext slot
  15, 005AA5F0), each pending part built (005AC2F0) and added like AddPart (005164B0: referenced,
  owner set 0059F3D0, animation 0050D0E0, attached slot 13); non-units mark their parts static.
- SetFigureScale (00517700): only when the scale changes (ordered compare): the figure is
  rebuilt at the new scale by the cache (ManagerContext slot 23, slot 6), the parts take their
  pieces' records, the animation is rescaled (0050D7C0), the bounds recomputed.
- ComputeBounds (00518310): the box of the parts within sqrt(1e9) of the object (NaN counts as
  near) and of the pending parts (their positions are zeroed first), with the original's
  branch-free min / max selects; the radius keeps the low x half extent unrounded.

### Shadows (CObject3DClientSpecific::DrawShadow, 0051CE60)

Fields of the client 3D object: +0x08 bit 8 "render the shadow again", +0x1C the object, +0x20
-> material (+0x60 the colour key when its flags have 0x2000), +0x4C / +0x50 / +0x5C / +0x6C /
+0x70 the projected geometry (filled by 0056C750, drawn by 004EA4A0), +0x7C the 64x64 shadow
texture ("SR<id>", created by 00692010), +0x80 the bounds in the light's view, +0x90 the bounding
sphere (scene object's slot 7) and its centre relative to the object.

When 0079BD0C > 0 and the class's switch is on (units 0079BD14, class 0x52 0079BD1C, others
0079BD18), the object is shown and the shadow must be rendered again (not a static figure, or
bit 8): the lights of 007AF748 are summed (point lights 0x31: direction from the light times
brightness 0.1 b + 0.6 g + 0.3 r times 1 - falloff * distance squared, ten times for flag
0x800, only within range; others: their direction times brightness), normalised and tilted to
at least 20 degrees below the horizontal (FPATAN against pi / 180 * 20); the object is turned to
the light (004F15C0, Quaternion::PreMultiply and back), its parts projected (figure slot 13, or
CFigureMesh::DrawOffset 005A0CD0) into the point buffer (007AF1F8), mapped to texture pixels
(scale 63 / extent, FISTP) in 007AF1E0, rasterised into the locked render surface (figure slot
14 or 005A0E80; blurred twice through the device's slots 11 / 12 when 0079BD0C is 2), copied to
the texture (the D3D device's slot 43 under the lock at 007AEE78) and the texture's square laid
on the ground (0056C750 with the quad in 007AF208). Then the geometry is drawn with alpha 0x80
(units: their controller's +0x78 times 128).

### Lifetime

| Address | Name | Verification |
|---|---|---|
| 00515BD0 | CObject3D::Construct | the original on a scratch copy of the raw memory, ours on the object: 0x170 bytes and both results compared |
| 0050E2D0 | CMapObject::Construct | the same, 0x238 bytes |
| 0052E110 | CUnit::Construct | the same, 0x4DC bytes |
| 00515610 | CMapObjectServer::Construct | the same, 0x94 bytes (its allocation size) |
| 00510450 | CMapObjectClientSpecific::Construct | the same, 0x54 bytes (allocated 0x58) |
| 0052E5B0 | CUnit::Reset | ours only: the blank state |
| 00515D70 / 0050E660 / 0052E310 / 00515720 / 00510560 / 00546230 | destructor bodies | ours only: the engine object's base state (vtable 0073B8E0, no owner) |
| 0052E2A0 | CUnit::DeletingDestructor (slot 0) | ours only (the memory may be gone): counted |

The constructors allocate nothing: their sub-constructors (quaternion identity, CStringArray,
memory streams 0047A200 / 00468C20, the record array reset 0047FB70, the unit's 004CD080 /
0052E230 / 005C6270) only set fields, so running the original on a copy is safe; nested
constructors (CUnit -> CMapObject -> CObject3D) each run through their own wrapper.

### Drawing (CObject3DClientSpecific::Draw, 0051B7E0)

The object's slot 26 and its animation (0050D450) bring it up to date; the lights of 007AF748
(other than the context's own, +0x1C) whose range +0xE0 plus 10 reaches the object go into an
aligned list (0051EF90 / 0051EE50; a moving light, flag 0x100, makes the object non-static); the
object's matrix and, unless all 1, its extra scale (+0xF0..+0xF8) are pushed on the scene's view
(2) and world (1) stacks and, for non-static objects, the object stack (3, inlined); then each part
of the two part sets (+0x20, +0x24: material, groups of parts) within sqrt(1e9) is drawn through
the context's slot 13 (part, lights, the group's material, the light test's AL, the dirty
rectangle or null, 0, 0), the first set with the texture +0xDC. A figure whose drawn parts all
have flag 0x10 becomes static (object flag 0x400, +0xEC the time, flags_ bit 8); the object's
flag 0x20 tells whether a part flagged 0x40 was drawn; the dirty rectangle (+0xC8) is copied to
the object's +0x100 and the caller's.

### Animation tracks and cages

- AnimationTrack (0050ACD0 Evaluate): key frames of positions (+0x24 count / +0x28), rotations
  (+0x2C / +0x30, Quaternion::Slerp 0049ED20) and morph vertex sets (+0x34 vertices, +0x38 frames,
  +0x3C count, output +0x68); the frame is the CRT's modf of the time rounded with FISTP; values
  at +0x4C / +0x58, the time at +0x48; past the last frame its values, without frames the
  defaults at 007AF110 / 007AF100.
- CageInterpolator (0050B8C0 cube of 8 points, 0050BC00 prism of 6; called by the figure
  manager's 005ACB80): a point at (t, u, w) (+0x68..+0x70) into +0x5C, the point records read
  from a stream that advances by 0x60 / 0x48.

### Other functions rewritten (second round)

| Address | Name | Verification |
|---|---|---|
| 0050E890 | CMapObject::AttachObject (slot 19) | ours only: a type 0x47 object attached both ways with the returned count |
| 0050E970 | CMapObject::PackVitals (slot 32) | ours, snapshot of the tracked bytes, original; EAX too |
| 00518310 | CObject3D::ComputeBounds | ours, the bounds saved and restored, the original; +0x124..+0x150 and EAX |
| 00517700 | CObject3D::SetFigureScale | ours only: the scale as given; after a change with parts valid bounds and EAX = the centre's z |
| 00517500 | CObject3D::LoadFigures | ours only: loaded, nothing pending, every pending part in the list |
| 005164B0 | CObject3D::AddPart | ours only: the part in the list |
| 00519E80 / 00519D70 | AlignedPtrArray::SetAtGrow / SetSize | ours only: value stored, size and alignment |
| 0051CE60 | CObject3DClientSpecific::DrawShadow | ours only: result 1 |
| 0051B7E0 | CObject3DClientSpecific::Draw | ours only: result 1, the scene's three matrix stacks as deep as before |
| 0050ACD0 | AnimationTrack::Evaluate | ours, the track's values and morph vertices saved and restored, the original; compared |
| 0050B8C0 / 0050BC00 | CageInterpolator::InterpolateCube / InterpolatePrism | ours, the point and the stream saved and restored, the original; compared |

The x87 code of ComputeBounds and the shadow's light sum, tilt, rotation matrix, ground quad and
shadow point was read back (`dumpbin /disasm`): operand order, float stores where the original
stores, unrounded values kept as doubles, NaN behaviour of the compares (`a >= b`, `!(a >= b)`,
`a < b || a > b`) as the original's FCOMP tests.

### Forwarders added (second round)

- Figures: 005AA5F0, 005AC2F0, 0059F3D0, 0050D0E0, 0050D7C0, 005197B0, 00516080 (release the
  parts), 00516D50 (CObject3D slot 19).
- Shadows: 00692010, 004F1440, 004F15C0, 004D9880, 004D9AA0, 004545A0 / 004F65B0 / 00452E70
  (aligned buffers), 005A0E80, 00692140 / 00690FC0 / 00691030 (render surface), 0056C750,
  00501AF0, 006CB020, 004EA4A0; through their addresses 004018A0 and 005A0CD0; Win32
  Enter/LeaveCriticalSection through the import table; globals 0079BD0C..0079BD1C, 007AF060,
  007AF080, 007AF1E0, 007AF1F8, 007AF208, 007AF230, 007AF5B8, 007AF738, 007AF744, 007AF748,
  007AEE78.
- Lifetime: 0040CB00, 006FE925 / 006FE958 / 006FE9B8 (CStringArray), 00468C20, 0047A200,
  0047FB70, 00510340, 005121B0, 006CA720, 00511FB0, 00511B90; through their addresses 005233B0,
  00523370, 00523550, 00455930 and the constructors / destructor bodies themselves.

### Original bugs and oddities kept (second round)

- SetFigureScale returns the FPU status word in AX when the scale did not change; ours returns 0.
  ComputeBounds returns the centre's z bits (0 without parts), which SetFigureScale passes on.
- LoadFigures drops the pending parts' reference without the release check.
- DrawShadow: the point and pixel buffers grow without keeping their contents; the rasteriser's
  work area (its first 0x30 bytes) is uninitialised stack in the original (ours zero); the shadow
  quad's first corner rounds its x and y to float before adding the position, the other three
  only y (the original builds a Vector3 for the first).
- The client companion's destructor releases its light after assigning it null (a no-op).
- Draw passes the context's slot 13 the light test's result as a dword whose upper three bytes are
  stack garbage in the original (the byte was stored with MOV BYTE); ours zero-extends it.
- Draw starts its light list with SetSize(16) and SetSize(0) (room for 16, empty); the paths for
  an existing list cannot run.
- The forwarders and helpers of the second round also include 0050D450 (animation update),
  0051EF90 / 0051EE50 (light list), 004D9B80 / 0051C110 (scene matrix stack push / pop), 004EB170
  (device texture), 00457720, the CRT's modf 006EAD0B, and through their addresses 0049ED20,
  00410750 / 00448A10 / 0044A230 (the cage's vector helpers are inlined as float steps).

### Skipped (second round) and why

- 0050C230 (the bone hierarchy update, recursive), 005179E0 / 00516DA0 (animated figures),
  00510B80 / 005114D0 (the client's effect particles and sounds), 0051C3D0 (wounds texture),
  0050EAF0 / 0050EC30 (network state): drawing and stateful; next candidates.
- 00500000-0050E000 (device and animation code): not looked at in detail.

### Risks (second round)

- DrawShadow and the figure loading run for every visible object and every spawn; they are
  checked ours-only (DrawShadow only for its result), so a wrong shadow shows only on screen.
- ApplyPrototype's statistics start from zeroed stack copies (units.md, "Fourth round").
