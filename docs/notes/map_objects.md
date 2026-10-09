# Map objects (00500000-00522000)

Code: `src/game/map_object.h/.cpp` (CMapObject, MapEffect: layouts, per-slot comments),
`map_object_companion.cpp` (CMapObjectServer / CMapObjectClientSpecific), `map_object_figures.*`, `map_object_parts.cpp`
(CObject3D figure parts, scale, bounds), `map_object_shadow.*`, `map_object_draw.cpp`,
`map_object_animation.*`, `map_object_cage.*`, `map_object_lifetime.cpp` (constructors, destructor
bodies), `map_object_create.cpp` (slot 2 of units / torches). Verify: `tests/verify/verify_map_object*.cpp`.
Scenarios: `map_objects.txt`, `unit_create.txt` (also world_load, models and every spawn).

## Address map

| Range | Contents |
|---|---|
| 00500000-0050E000 | rendering and animation (tracks, cages, the selection marker and the gamma ramp rewritten; renderer slots in models.md) |
| 0050E2D0-0050F730 | **CMapObject** (vtable 0073DAA0, 67 slots): ctor 0050E2D0, Create (slot 2) 0050E770, Destroy (3) 0050E840, attach (19), MarkChanged 0050E9F0, network write / read (50 / 49, 0050EAF0 / 0050EC30, net_state.cpp), AddEffect (45), scale (27 / 30), position (48, 51-53), Load / Save (9 / 10, 0050F7A0 / 0050F900) |
| 00510450-00512600 | **CMapObjectClientSpecific** (0x3A, vtable 0073DBEC): Update (slot 6), effect particles 00510B80 and looping sounds 005112A0 (called by slot 13 005117D0 when the effect list changes), sound names by type 005114D0, the light (+0x50: 00511830 create, 00511950 show, 005119B0 hide, 00511A00 slot 19) |
| 005125E0-00512730 | CMapObjectManager ("map_objects", id 0x2D, vtable 0073DCD8); factory 00512730 |
| 00515610-00515BD0 | **CMapObjectServer** (0x39, vtable 0073DD6C, 21 slots): Update (6), SetData, TakeHit (20), Load / Save (9 / 10) |
| 00515BD0-0051AC30 | **CObject3D** (vtable 0073DDD4, 33 slots) under CMapObject |
| 0051AC30-0051F340 | CObject3DClientSpecific (0x37, vtable 0073DE90: drawing), CObject3DServer (0x38, 0073DFBC: every slot a stub) |
| 0051F340-00522000 | perks database ("perks.pdb", load 00520220 / save 0051FF40), STLport vectors |

## Facts

- Companions: CUnit slot 6 (0052F7D0) runs its controller's update, then CMapObject::Update, which
  runs the companion's. Server: statistics block at +0x20 (0x58 bytes, hit points +0x28 / +0x2C).
  Client: looping effect sounds at +0x40 (0x14 each: kind, -1, handle, timer, present), effect
  particles +0x2C (0x10 each), light +0x50, end-sound handle +0x54.
- flags_: 1 dead/destroyed (TakeHit), 2 dirty, 8 (network part 2), 0x10000 invisible, 0x40000 stunned
  (client, effect 0x19).
- MarkChanged and HasEffect are CMapObject methods but stay rewritten on CUnit (unit.cpp); CMapObject
  reaches them via forwarders to 0050E9F0 / 0050F630. Standalone: move them to CMapObject and derive
  CUnit from it (its fields up to +0x238 duplicate CMapObject's).
- Effect tick: the server's Update counts effects down (00688530) and removes those at 0; per
  removal: slot 27 for a scale effect (strengthen 0x1D, weaken 0x1E, enlarge 0x25, shrink 0x26),
  MarkChanged(3), slot 33, slot 28, slot 46(0) unless a 0x19 / 0x1C effect is left. The client counts
  its copy down too (network part 3 resends the list); for text-key objects it plays
  "magic\end1.wav" under 30 ticks left, stench plays "magic\Stench\1..3.wav" every 17 ticks (ISAAC
  draw), a shown light sits 2 above the object.
- **Shadows**: gates 0079BD0C > 0 and the class switch (units 0079BD14, class 0x52 0079BD1C, others
  0079BD18); shadow globals 007AF1E0 / 007AF1F8 / 007AF208 (pixel, point, quad buffers), lights
  007AF748, render lock 007AEE78. The algorithm is in `map_object_shadow.h`.
- **Drawing**: `CObject3DClientSpecific::Draw` is described in `map_object_shadow.h` and
  `map_object_draw.cpp` (lights within range + 10, matrix stacks, part sets +0x20 / +0x24).
- **Figures**: layouts in `map_object_figures.h`; LoadFigures reads the model named at +0xA4
  through the figure manager (ManagerContext slot 15, 005AA5F0); SetFigureScale rebuilds the figure
  only when the scale changes (ManagerContext slots 23 / 6); the piece map at figure + 0x14 is an
  STLport map<string, piece> (find 005197B0).
- The side half at +0x94 (`CObject3DSpecific`: client 0x37 vtable 0073DE90, server 0x38 0073DFBC) is declared as an interface;
  slot 18 GetScale gives the client's +0xF0..+0xF8 or 1, 1, 1 on the server. Scenarios reaching the part code: units, unit_create, combat
  (weapon / armour models), spells_cast, effects (EffectPoint, particle callbacks).
- **Skeleton**: the animation state (class 0x300D) holds the bones (class 0x300C, FigureBone) in `bones` (+0x24); each
  bone's animator (+0x50) and placing track (+0x4C) are AnimationTracks (class 0x300B, an engine object; it was declared
  twice as FigurePartAnimator until A-U10). The parts list once typed FigurePart there holds bones.
- **Animation tracks and cages**: AnimationTrack::Evaluate uses the CRT's modf of the time rounded
  with FISTP, `Quaternion::Slerp` 0049ED20, defaults at 007AF110 / 007AF100; CageInterpolator cube /
  prism are called by the figure manager's 005ACB80 and read point records from a stream advancing
  0x60 / 0x48.

## Rewritten

"Ours only" = consistency check on the resulting state. "Snapshot compare" = ours runs, the object
is snapshotted and restored, the original runs, bytes and EAX compared.

| Address | Name | Verify |
|---|---|---|
| 0050F1C0 | CMapObject::Update (slot 6) | ours only: returns 1 |
| 00515820 | CMapObjectServer::Update (slot 6) | no effect runs out: ours, records restored, original, compared; otherwise ours only: list ends as predicted |
| 00510890 | CMapObjectClientSpecific::Update (slot 6) | ours only: counts - 1, stench timers - 1 (16 after going negative), light unchanged or at object + (0, 0, 2) |
| 0050F5B0, 0050F690 | CMapObject::ClearEffects, EffectValue | ours only / pure |
| 0050F1E0 | CMapObject::AddEffect (slot 45) | Rebirth-patched: reproduces FixSpellEffectsOverflow behind the flag |
| 0050F2D0 | CMapObject::RecomputeScale (slot 27) | snapshot compare (0x1E0 bytes) when slot 30 is 0050E440; units (slot 30 rebuilds figures through 00517700, must not run twice): ours only |
| 0050E440 | ResetScale (slot 30) | snapshot compare |
| 0050E4B0, 0050E4D0, 0050F730 | GetPosition (slot 52, RET 8), GetPosition2 (53, RET 4), GroundPoint (48) | output only |
| 0050F020 | PlaceOnGround (slot 51) | z compared; the original runs through a naked stub repeating its first three instructions (PUSH ESI; MOV ESI,ECX; CALL 00455950) and jumping to 0050F028 |
| 0050F050, 00515950 | SetServerData (slot 36), CMapObjectServer::SetData | snapshot compare (object 0x1E0, companion 0x78) |
| 0050E8F0, 0050E890 | AttachCompanion, AttachObject (slot 19, type 0x47) | ours only: attached both ways, count |
| 0050E970 | PackVitals (slot 32) | snapshot compare of the tracked bytes |
| 0050FA10, 0050FA20, 0050E430 | IsInvisible, IsStunned, Flag20000 | pure |
| 00515980 | CMapObjectServer::TakeHit (slot 20) | ours only: returns 1; a dead object has no effects and flags_ bit 1 |
| 00518310 | CObject3D::ComputeBounds | ours, bounds restored, original; +0x124..+0x150 and EAX |
| 00517700, 00517500, 005164B0 | SetFigureScale, LoadFigures, AddPart | ours only: scale as given / nothing pending, parts listed |
| 00519E80, 00519D70 | AlignedPtrArray::SetAtGrow, SetSize | ours only: value, size, alignment |
| 00516A80, 005169A0 | CObject3D::FindPartByPrefix (strstr at the name's start, case-sensitive), FindPart (strcmp); parts, then pending parts | pure |
| 005167E0, 005168F0 | RemovePart (side half's slot 15 first; a placed part forgets its owner; animation 0050D350; released), RemovePartNamed | ours only: list one shorter, next entry moved down, result |
| 005188B0 | PartWorldPosition (part position turned by the orientation, times the side half's slot 18 scale, plus the position) | output, CheckNear |
| 00518CA0, 00518F80, 00518FC0 | Animations ("<name>.adb" through the owner's GetFile on first use, +0xA0), AnimationAt (key & 0xFF), FindSkillAnimation (kind 0x40000, number in bits 22-29) | pure once loaded (00518F80 through a stub repeating its first three instructions) |
| 0050E770, 0050E840 | CMapObject::CreateMapObject / DestroyMapObject (slots 2 / 3; map_object_create.cpp) | ours only: blank state / nothing left |
| 00515F70, 00516080 | CObject3D::Create3D / Destroy3D (slots 2 / 3; object3d_lifetime.cpp; Destroy3D drops the parts' references without releasing them) | ours only |
| 00518A00, 00518AE0, 00519580 | AddEffectSource / RemoveEffectSource (particle sources at +0xA8, attached through 004849F0), CountedRef::Assign | ours only: list / reference counts |
| 00518D30, 00517990 | AnimationFactor (units only: (start + (end - start) t) / length, 1 for a 0 / NaN length), FindPartIndex (_mbscmp over +0x114) | pure (CheckNear) |
| 0051CE60, 0051B7E0 | DrawShadow, Draw | ours only: result 1; Draw: scene matrix stacks as deep as before |
| 0050ACD0 | AnimationTrack::Evaluate | ours, values restored, original, compared |
| 0050B8C0, 0050BC00 | CageInterpolator::InterpolateCube, Prism | ours, point and stream restored, original, compared |
| 00515BD0, 0050E2D0, 0052E110, 00515610, 00510450 | Construct of CObject3D (0x170), CMapObject (0x238), CUnit (0x4DC), CMapObjectServer (0x94), CMapObjectClientSpecific (0x54) | original on a scratch copy of raw memory (they allocate nothing), ours on the object, bytes and results compared |
| 0052E5B0 | CUnit::Reset | ours only: blank state |
| 00515D70, 0050E660, 0052E310, 00515720, 00510560, 00546230 | destructor bodies | ours only: engine object base state (vtable 0073B8E0, no owner) |
| 0052E2A0 | CUnit::DeletingDestructor (slot 0) | ours only: counted |
| 0050CA50, 0050CBA0, 0050D600, 0050D5E0 | CFigureAnimationState Play, Queue, SetTime, Advance (map_object_animation_state.cpp; A-U2) | SetTime: original vs ours on +0x40..+0x78 when the animation does not end, else ours only; the rest ours only |
| 00516370, 005161E0, 00516D50, 00515E90, 00516460, 00516420, 00516490 | CObject3D slots 14 CombineRotation3D (0040CB80's body), 20 CopyFigure, 19 AttachSpecific, 5 RemoveAllEffectSources, 22-24 DrawFigure / DrawCollectDirty / DrawFigureShadow (object3d_slots.cpp; A-U2) | 14: original vs ours (CheckNear); the rest ours only |
| 0050F7A0, 0050F900, 005159D0, 00515AC0 | CMapObject LoadMapObject / SaveMapObject, CMapObjectServer LoadServer / SaveServer (slots 9 / 10; map_object_save.cpp; A-U2) | save: both into capture streams; load: ours through a recorder, round trip through the original Save |
| 005157C0, 005157E0, 0050E460, 005391B0, 005391E0 | CMapObjectServer CreateServer / DestroyServer (slots 2 / 3), CMapObject::CompanionQuery (31), CUnitClientSpecific HasWeaponModels / HasArmourModels (36 / 37) (object_small_slots.cpp; A-U2) | create / destroy ours only; the rest pure |
| 0050F0F0, 0050F120, 00510770, 00533E80 | CMapObject ApplyScience (41: logs, false) / FreeArray (44), CMapObjectClientSpecific::CreateClientCompanion (2), CUnitClientSpecific::DeleteClient (0) (object_small_slots.cpp; A-U2) | ours only |
| 0051CC20, 0051CCD0 | CObject3DClientSpecific InvisiblePulse / StunnedPulse (unit_client_animation.cpp) | original, parts put back, ours; CheckNear |
| 005117D0, 00510B80, 005112A0, 005114D0 | CMapObjectClientSpecific EffectsChanged (slot 13), UpdateEffectParticles, UpdateEffectSounds, PlayEffectSound (map_object_client_effects.cpp; A-U3) | ours only: records present, one per merged type at the highest power / per effect otherwise, one sound per type; stench timer 0, feeble mind handle toggles |
| 0051CCA0, 0051CD60 | CObject3DClientSpecific ResetPartAlpha / ClearPartTints (invisibility / stun ended) | original, parts put back, ours; bitwise |
| 00511830, 00511950, 005119B0, 00511A00, 005107B0 | CMapObjectClientSpecific CreateLight, ShowLight, HideLight, SetLightMode (slot 19; mode 1 holds the light on and lists its id at *007AF748 + 0xD4, mode 2 drops it), DestroyClientCompanion (slot 3) | ours only: hidden flag toggles as the rules say; new light class 0x31 with the radius at +0xE0; held light listed / no light |
| 0050C230 | FigureBone::Place (class 0x300C skeleton bone, map_object_figures.h; figure_bones.cpp; A-U9): child = parent position + offset turned by the parent's rotation, rotation pose * parent (Hamilton; 00507E80 multiplies the other way); root = pose (times the animation's time factor) - origin; the mesh gets both and the pose's vertices (005A0B10) | the original's recursion goes straight to it inside the wrapper; subtree snapshotted, original, kept, restored, ours; bone and mesh poses within 4 ULPs |
| 0050AF40, 0050B370, 0050B510, 0050B1D0, 0050B1B0 | FigureBone Construct, CreateBone (slot 2: reset; the matrices +0x90 emptied without freeing, as 0050DD40), DestroyBone (slot 3: mesh forgotten then released, pose only forgotten, animator / +0x54 / +0x58 released), DestructBody, DeleteBone (figure_bone_lifetime.cpp; A-U9; slots 7 / 11 / 12 0050B180 / 90 / A0 are trivial) | ctor: original into a raw copy, bytes compared except +0x90 and the unwritten offset; the rest ours only (blank state / engine object vtable) |
| 00503A40, 00504A60 | CSceneRenderer DrawSelectionMarker (the green arrow under selected units; renderer_marker.cpp), SetGamma (slot 22: the options' brightness / contrast / gamma as the primary surface's gamma ramp; renderer_gamma.cpp) (A-U9) | marker: original with 004EA4A0's call recorded and skipped (flat_draw_capture.h), ours recorded and drawn, arguments compared (positions 4 ULPs); gamma: original, ramp read back (GetGammaRamp), ours, read again; results, kept values and ramps (entries within 1) compared |
| 0050C5F0, 0050C730, 0050C710, 0050C860, 0050C8E0, 0050C990, 0050C9E0 | CFigureAnimationState Construct, DestructBody, DeleteState (slot 0), CreateState / DestroyState (slots 2 / 3), ReleaseBones, Reset (map_object_animation_bones.cpp; A-U10) | Construct: original into a raw copy, bytes +0..+0x74 compared; the rest ours only (blank state) |
| 0050CC20, 0050CC60, 0050CD70, 0050D2E0, 0050D320 | FindBone, LinkPiece (a bone of class 0x300C from the figure manager), LinkBone (child of the first bone named as its parent unless it has a child of that name; list, reference, name3 = model, morph, 005ACB80), FindRoot (first bone without a parent name), SetModel (005AC880) | FindBone pure; FindRoot original vs ours (result, root, changed); the rest ours only |
| 0050D0E0, 0050D350, 0050D3A0 | AttachMesh (the bone named as the part's slot 7 carries it: referenced, given the bone's position / rotation, old one released), DetachMesh, DetachMeshes | ours only |
| 0050D3E0, 0050D450, 0050D5A0, 0050D7C0, 0050D920 | PlaceIfChanged / UpdatePose (tracks evaluated at the frame, then the root places the skeleton at owner AnimationFactor(scale.z) * defaultScale: x, y from the factor stored to float, z unrounded), Stop, Rescale (morph to every bone and the state, key frames reloaded, `changed` cleared, root placed), CollectBoneNames | ours only (Place compared by its own wrapper) |
| 0050DFF0 | AlignedPtrArray::SetSize copy (the tracks' arrays) | ours only |

Other callees stay forwarders: 00455950 `CEngineObject::World`, 00688530 `MapEffect::Tick`, 00524FE0
(hit onto the statistics block); sound system (global 007AF784) 006B8600 / 006B7C10 / 006B7CB0 /
006BA400; light manager (007AF738) 006CA830; see `originals_*.h`.

## Original bugs and oddities kept on purpose

- The client's stench sound tests the record's kind for 0x13 a second time after drawing the random
  number (always true); the timer runs 0x10 .. -1, so the period is 17 ticks.
- RecomputeScale adds the net enlarge/shrink level to the strengthen level when enlarge or shrink is
  present (strengthened + enlarged grows by 0.8) and leaves y alone for strengthen/weaken; heroes keep
  their height.
- CMapObjectServer::Update passes slot 33 a byte flag whose upper bytes are stack garbage in the
  original; ours passes 0 or 1 (receivers read only the byte).
- GetPosition (slot 52) takes a second argument it never reads. PlaceOnGround leaves the ground height
  call's EAX as its result; ours returns nothing. TakeHit treats 0 from 00524FE0 as "not hit", but it
  always returns 1.
- SetFigureScale returns the FPU status word in AX when the scale did not change; ours returns 0.
  ComputeBounds returns the centre's z bits (0 without parts), which SetFigureScale passes on.
- LoadFigures drops the pending parts' reference without the release check.
- DrawShadow: point and pixel buffers grow without keeping their contents; the rasteriser's work area
  (first 0x30 bytes) is uninitialised stack (ours zero); the shadow quad's first corner rounds x and y
  to float before adding the position, the other three only y.
- The client companion's destructor releases its light after assigning it null (no-op).
- Draw passes slot 13 the light test's result as a dword with stack garbage in the upper three bytes
  (ours zero-extends); its light list starts with SetSize(16) / SetSize(0), the paths for an existing
  list cannot run.

- A-U3 effect visuals: the scale effects' rings (0x11 / 0x1D / 0x1E) are made, attached and scaled
  but never referenced by the record; a source of an unknown type logs and uses 0x2013 without a
  null check; feeble mind's start plays the bare "\start.wav" (the folder is empty for type 0x20)
  even when `first` is set. Ours builds the merged effect list in one allocation (the original
  grows a CArray).
- Create3D drops the side half (+0x94) without releasing it; Destroy3D only decrements the placed
  parts' counts (no release, nothing freed at 0) while it releases the pending ones.

## Skipped and why

- CArray Serialize / SetSize instantiations (library).
- A-U9: 005015A0 (renderer slot 25: the projection / viewport / axis-swap stacks for a rectangle and a field of view,
  like the creation's FillMatrixStacks) has no caller (no `call [reg+0x64]` on the renderer in game.exe); 00508A30
  (sets the skin clock, rewinds) is unreferenced. Next here: 00506710 / 005068C0 (CAnimationNode ctor / dtor
  body), the renderer's 00504860, 00503F40, 005040A0, 00504E50, 00504F70, 00505270; 0050D950 / 0050DA10 (reset / reload
  the bones' tracks; need AnimationTrack's arrays declared), 0050DD40 (the bones' matrix SetSize, a discarding copy with
  growBy).
- 0050E9B0 (slot 34): packs a stack UnitVitals the builder leaves unwritten when the prototype exists (garbage in the
  original; cannot be reproduced). 0050F080 is rewritten (A-U5, map_object_vitals.cpp: CMapObject::ClearProtectionBases,
  verified ours vs original on the protection pairs; reached by every unit spawn through 0058EF20).
- Dead (A-U5 sweep, config/dead_code.csv): 0050BFA0, 0051FCD0, 005476A0 (PlayIdle's body after 0052E9D0), 0054A0A0, 0054B950.
- 0050C230 (bone hierarchy update, recursive), 005179E0 / 00516DA0 (animated figures), 0051C3D0
  (wounds texture): drawing, stateful.
- CObject3DServer stubs, the perks database, 00500000-0050E000 apart from tracks / cages.
- Trivial bodies too short for the 5-byte jump (`MOV AL,1; RET`, `RET`, `XOR EAX,EAX; RET`, FLD
  constant; RET).

## Risks

- The per-tick functions run for every map object every world update; DrawShadow and figure loading
  for every visible object and spawn. Most are checked ours-only (DrawShadow only for its result), so a
  wrong shadow shows only on screen.
- CMapObjectServer::Update's verify predicts the list from the old one; a slot it calls that changes
  the list (a unit's slot 27 reaches its controller's slot 44, which can kill a unit that cannot stand
  after a scale change) would report a false mismatch.
- The client update's sound and light calls are checked only for their effect on the companion's
  fields, not for their arguments.
- unit.h carries declarations of CMapObjectServer / CMapObjectClientSpecific methods; a branch that
  moves those classes must take them along.
- The scenario's spell strings ("speed{d1}", "shrink{e2;e2;d2;d1}") exercise the effect tick only if
  they parse.

A-U2: the effect / string counts of Load are read into the argument's stack slot (a failed read leaves the stream
pointer as the count; ours starts the local with it); the flags_ read starts from stack garbage (ours: the current
flags). CMapObjectServer now declares its vitals block (+0x20, UnitVitals), names and string array.

## Virtual slot names with low confidence (DR-C2)

The placeholder slots of map_object.h, unit.h, world.h, map_object_figures.h and party.h were named from the vtables
(evidence in the headers' comments). Guesses to re-check when a caller needs them: CMapObjectVirtuals 29
`IsSynchronised` (flags_ 0x20000, set by the zone loader for records with +0x6C or a script name; units, levers, traps,
torches answer 1; SendFreshObjects moves the objects answering 0 out of the map-object maps), 39 `IsDirectUseBlocked`
(levers: byte +0x260; a plain click uses an object only when 38 IsUsable is set and 39 is 0), 43 `KeepsMapFlag` (levers:
byte +0x261); CMapObjectCompanion 14 `Unused14`; CUnitController 15 / 17 `Unused*`, 36 `HasOtherArmour` (server: armour
slots 3..6, client: any weapon model; they differ); CWorld 37 / 38 `ClientManager194` / `ClientManager198` (not
identified), 39 `Unused39`; CObject3DSpecific 25 `Unused25`. CLinkMesh's slot 4, declared `HasPiece`, is LoadPart
(004F30D0 -> 004F7480). Placeholders still left (16): engine_object.h, map_spec.h, resource.h, ui_camp.h, unit_ai.h
(`AIStream::Slot0` is CStream's deleting destructor).
