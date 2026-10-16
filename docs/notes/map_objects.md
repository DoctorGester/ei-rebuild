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
  only when the scale changes (ManagerContext slots 23 / 6); the figure (+0xF0) is a CLinkMesh
  (link_figure.h) and its piece map is `headers_` (+0x14, map<string, LinkPartHeader>; find 005197B0).
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

Every rewritten function carries its address in its source comment (grep the address); the per-function table that used to be
here is gone. Facts a worker needs:
- Slot numbers: `CMapObject::Update` 0050F1C0 (6; returns 1), `CMapObjectServer::Update` 00515820, `CMapObjectClientSpecific::Update`
  00510890 (counts - 1, stench timers - 1, 16 after going negative, light unchanged or at object + (0, 0, 2)), AddEffect 0050F1E0
  (45; Rebirth-patched, reproduces FixSpellEffectsOverflow behind the flag), RecomputeScale 0050F2D0 (27), ResetScale 0050E440
  (30; for units slot 30 rebuilds figures through 00517700 and must not run twice), GetPosition 0050E4B0 (52, RET 8) /
  GetPosition2 0050E4D0 (53, RET 4), GroundPoint 0050F730 (48), PlaceOnGround 0050F020 (51; the original's wrapper runs through a
  naked stub repeating its first three instructions PUSH ESI; MOV ESI,ECX; CALL 00455950 and jumping to 0050F028), SetServerData
  0050F050 (36), AttachObject 0050E890 (19, type 0x47), PackVitals 0050E970 (32), TakeHit 00515980 (20; ours returns 1; a dead
  object has no effects and `flags_` bit 1), CreateMapObject / DestroyMapObject 0050E770 / 0050E840 (2 / 3), Create3D / Destroy3D
  00515F70 / 00516080 (2 / 3; Destroy3D drops the parts' references without releasing them), AddEffectSource / RemoveEffectSource
  (particle sources at +0xA8, attached through 004849F0).
- CObject3D: ComputeBounds 00518310 (+0x124..+0x150 and EAX), FindPartByPrefix 00516A80 (strstr at the name's start,
  case-sensitive) / FindPart 005169A0 (strcmp), both parts then pending parts; RemovePart 005167E0 (side half's slot 15 first; a
  placed part forgets its owner; animation 0050D350; released); PartWorldPosition 005188B0 (part position turned by the
  orientation, times the side half's slot 18 scale, plus the position); Animations 00518CA0 ("<name>.adb" through the owner's GetFile on
  first use, +0xA0), AnimationAt 00518F80 (key & 0xFF), FindSkillAnimation 00518FC0 (kind 0x40000, number in bits 22-29; the
  wrappers go through a stub repeating the first three instructions), AnimationFactor 00518D30 (units only: (start + (end - start)
  t) / length, 1 for a 0 / NaN length), FindPartIndex 00517990 (_mbscmp over +0x114). Constructs: CObject3D 0x170, CMapObject 0x238, CUnit
  0x4DC, CMapObjectServer 0x94, CMapObjectClientSpecific 0x54 (destructor bodies leave the engine object base state: vtable
  0073B8E0, no owner). CObject3D slots 14 CombineRotation3D (0040CB80's body), 20 CopyFigure, 19 AttachSpecific, 5
  RemoveAllEffectSources, 22-24 DrawFigure / DrawCollectDirty / DrawFigureShadow (object3d_slots.cpp). Map object Load / Save
  0050F7A0 / 0050F900, server 005159D0 / 00515AC0 (slots 9 / 10); CMapObjectServer Create / Destroy 005157C0 / 005157E0 (2 / 3),
  CompanionQuery 0050E460 (31), CUnitClientSpecific HasWeaponModels / HasArmourModels 005391B0 / 005391E0 (36 / 37),
  ApplyScience 0050F0F0 (41: logs, false) / FreeArray (44), CreateClientCompanion 00510770 (2), DeleteClient 00533E80 (0).
- Animation: `AnimationTrack::Evaluate` 0050ACD0 (class 0x300B, vtable 0073D9D8, 0x78 bytes; `frames` +0x38 and `vertices` +0x68 are
  AlignedArrays; LoadKeyFrames 0050AAF0: rotations, positions, frame and vertex counts from the model entry, arrays sized without
  keeping contents, frame pointers into the entry), `CageInterpolator::InterpolateCube` / `Prism` 0050B8C0 / 0050BC00,
  CFigureAnimationState Play / Queue / SetTime / Advance 0050CA50 / 0050CBA0 / 0050D600 / 0050D5E0 (map_object_animation_state.cpp),
  Construct / DestructBody / DeleteState (0) / CreateState / DestroyState (2 / 3) / ReleaseBones / Reset (0050C5F0..0050C9E0),
  FindBone 0050CC20, LinkPiece 0050CC60 (a bone of class 0x300C from the figure manager), LinkBone 0050CD70 (child of the first bone
  named as its parent unless it has a child of that name; list, reference, name3 = model, morph, 005ACB80), FindRoot 0050D2E0 (first
  bone without a parent name), SetModel 0050D320 (005AC880), AttachMesh 0050D0E0 (the bone named as the part's slot 7 carries it:
  referenced, given the bone's position / rotation, old one released) / DetachMesh / DetachMeshes, PlaceIfChanged / UpdatePose
  0050D3E0 / 0050D450 (tracks evaluated at the frame, then the root places the skeleton at owner AnimationFactor(scale.z) *
  defaultScale: x, y from the factor stored to float, z unrounded), Stop, Rescale 0050D7C0 (morph to every bone and the state, key
  frames reloaded, `changed` cleared, root placed), CollectBoneNames, ClearBoneTracks / LoadBoneTracks 0050D950 / 0050DA10 (005ACCF0).
  `FigureBone::Place` 0050C230 (class 0x300C skeleton bone, figure_bones.cpp): child = parent position + offset turned by the
  parent's rotation, rotation pose * parent (Hamilton; 00507E80 multiplies the other way); root = pose (times the animation's time
  factor) - origin; the mesh gets both and the pose's vertices (005A0B10); FigureBone Construct 0050AF40, CreateBone 0050B370 (slot 2:
  reset; the matrices +0x90 emptied without freeing, as 0050DD40), DestroyBone 0050B510 (slot 3: mesh forgotten then released, pose
  only forgotten, animator / +0x54 / +0x58 released), DestructBody, DeleteBone. CAnimationNode Construct 00506710 (cursors -1, identity
  rotations, +0x9C / +0xAC / position_ from 007AF0E0 / 007AF0F0) / DestructBody 005068C0 (the mesh only loses a reference; children
  destroyed and deleted); AttachMeshes 00507C50 (the first part named as the node becomes its mesh, placed at the pose; recursive),
  SceneSkin::BindMeshes 00508BF0 (skin_rewind.cpp), MultiplyQuaternions 00506C70 (fastcall out, a, b: a * b, Hamilton).
- Client companion: EffectsChanged 005117D0 (slot 13), UpdateEffectParticles 00510B80, UpdateEffectSounds 005112A0, PlayEffectSound
  005114D0 (map_object_client_effects.cpp: records present, one per merged type at the highest power / per effect otherwise, one
  sound per type; stench timer 0, feeble mind handle toggles), CreateLight 00511830, ShowLight 00511950, HideLight 005119B0, ApplyLightMode 00511A00
  (slot 19; mode 1 holds the light on and lists its id at *007AF748 + 0xD4, mode 2 drops it; new light class 0x31 with the radius at
  +0xE0), DestroyClientCompanion 005107B0 (slot 3), InvisiblePulse / StunnedPulse 0051CC20 / 0051CCD0, ResetPartAlpha / ClearPartTints
  0051CCA0 / 0051CD60, ShowWounds 0051C3D0 (unit_client_wounds.cpp: the wounds texture (kind 6) found / made, reloaded unless its
  .mmp extra block is unchanged; the material WoundsMaterial: blend 5 / 6, helper preset 4, stages: part set texture, wounds
  texture), picking slots 22-24 (0051C130, 0051C160, 0051C1D0: dirtyRect / own-mesh parts' screen rects; PtInRect, IntersectRect +
  IsRectEmpty), part lookups 16 / 17 (CObject3D::FindPart / FindPartOfType: type_, parts then pending).
- CSceneRenderer: DrawShadow 0051CE60 and Draw 0051B7E0 (client half), BeginScene / EndScene / DrawTexture 00503F40 / 005040A0 /
  00504860 (slots 4 / 5 / 21; renderer_scene.cpp: frame statistics +0x34..+0x58, device counters +0x16C..+0x178, debug panel
  "FPS" / "num verts" / "num trias"), DrawSelectionMarker 00503A40 (the green arrow under selected units; renderer_marker.cpp),
  SetGamma 00504A60 (slot 22: the options' brightness / contrast / gamma as the primary surface's gamma ramp; renderer_gamma.cpp),
  lifetime and small slots (renderer_lifetime.cpp: base class vtable 0073D8A0: ctor, deleting dtor, dtor, DestroyBase,
  SetBaseColour; slots 3 DestroyRenderer, 8 ClearFrame, 9 ZEnable, 11 SetTexture, 7 SetColour, 15 DrawLineList, 14 DrawFigure, 13
  DrawFigureLit, 23 GetGamma, 24 RestoreSurfaces). AlignedArray copies registered on the template: TLVertex SetSize /
  SetSizeDiscardStep, SpriteSortRecord, its pointers, the index Add, the bones' Matrix4 SetSizeDiscardStep.

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

- A-U11: RestoreSurfaces (00504780) passes an unwritten local as the display mode when a full-screen device has no
  primary surface (ours 0); AttachMeshes (00507C50) overwrites a held mesh without releasing it; the renderer's
  destructor releases the helper without clearing it.

## Skipped and why

- CArray Serialize / SetSize instantiations (library).
- A-U9: 005015A0 (renderer slot 25: the projection / viewport / axis-swap stacks for a rectangle and a field of view,
  like the creation's FillMatrixStacks) has no caller (no `call [reg+0x64]` on the renderer in game.exe); 00508A30
  (sets the skin clock, rewinds) is unreferenced. A-U11: 0050A210 is library (CArray::Serialize); 00505380 (an aligned
  buffer's free) runs only in an unwind funclet of 005045D0, which is rewritten; 00505410 / 005054E0 / 005055F0 (key record
  writers) are called only from dead 0044A7B0 (the animation exporter; Ghidra's bodies for them overlap live readers, so
  find_dead_code.py keeps them); 00501980 / 005019D0 only from slot 25 (005015A0, no caller); 00500A90 (4 bytes) too
  short; 00503F20 (a Vector3 copy) and 005000xx-005003D0 (STLport copy helpers) left.
- Next after A-U11: CObject3DClientSpecific's part sets (+0x20 / +0x24: objects holding arrays of arrays of parts; needs
  their layout declared): slots 13 / 14 / 15 (0051B050 / 0051B210 / 0051B160) with 0051B2F0 / 0051B470 / 0051B5B0 /
  0051B630, ctor / dtor / Create / Destroy (0051AB00 / 0051AD90 / 0051AEC0 / 0051AF70), 0051CB30, the colour stack
  0051C250 / 0051C9F0 / 0051C730-0051C890; CObject3DServer stubs (0051F240-0051F330); 0051E300-0051F0D0 (materials);
  005101A0 / 005102A0, 005143E0, 00512660 / 00512A30, 0050E580, 0050FBC0, 0050BF00, 00508870; 5- and 6-byte constant
  slots (byte-identical to rewritten stubs, but registering them under unrelated names was avoided).
- 0050E9B0 (slot 34): packs a stack UnitVitals the builder leaves unwritten when the prototype exists (garbage in the
  original; cannot be reproduced). 0050F080 is rewritten (A-U5, map_object_vitals.cpp: CMapObject::ClearProtectionBases,
  verified ours vs original on the protection pairs; reached by every unit spawn through 0058EF20).
- Dead (A-U5 sweep, config/dead_code.csv): 0050BFA0, 0051FCD0, 005476A0 (PlayIdle's body after 0052E9D0), 0054A0A0, 0054B950.
- 0050C230 (bone hierarchy update, recursive), 005179E0 / 00516DA0 (animated figures): drawing, stateful (since
  rewritten).
- CObject3DServer stubs; the perks database's records and lookups are rewritten (units.md, A-U10), its loader and STLport
  vector instantiations are not.
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
identified), 39 `Unused39`; CObject3DSpecific 25 `Unused25`. CLinkMesh's slot 4 (once declared `HasPiece` on a
view) is LoadPart (004F30D0 -> 004F7480); CObject3D::LoadPart calls it (F4c). Placeholders still left (16): engine_object.h, map_spec.h, resource.h, ui_camp.h, unit_ai.h
(`AIStream::Slot0` is CStream's deleting destructor).

## A-U12 (the range's leftovers, 0x00500000-0x00520000)

After this ticket nothing in 0x00500000-0x00520000 is unported. Every rewrite has a wrapper; the scenario that reaches
each one is in the head comment of its verify file.

- **The 3D object's halves** (`object3d_client_parts.cpp`, `object3d_halves.cpp`; `verify_object3d_client_parts.cpp`):
  the client half's part sets, lifetime, skins, lit and shadow state (0051A370, 0051AAA0, 0051AB00, 0051AD70, 0051AD90,
  0051AEC0, 0051AF70, 0051B050, 0051B160, 0051B210, 0051B2F0, 0051B470, 0051B5B0, 0051B630, 0051C250, 0051C9D0,
  0051C9E0, 0051C9F0, 0051CB30, 0051C110, 0051E1C0, 0051E200, 0051E280, 0051E300), the base half's empty slots
  (0051AC60-0051ACD0), the server half (0051F240-0051F330), the wounds material's destructors (0051C710, 0051C730) and
  the named texture / material stage copies (0051C800, 0051E430, 0051E970, 0051E830). Side by side: FindMaterial,
  ShadowsShown, ShadowInView, MatrixSetIdentity, the server half's name / id / Create; the rest ours only (zone load,
  equipment changes, highlighting, wounds). The older files of the client unit (unit_client_*.cpp, map_object_draw /
  shadow / client_effects, world_rules.cpp) now call these directly instead of through local forwarders.
- **The map object's small slots** (`map_object_stub_slots.cpp`; `verify_map_object_stub_slots.cpp`): the empty and
  constant slots (0050E400-0050E560, 0050E760, 0050F0C0-0050F110), DeleteMapObject 0050E560, NotifyCompanion 0050E930,
  ScaleEffectChanged 0050E960, TakePrototype 0050E9B0, the network state wrappers 0050EA40 / 0050EA70 / 0050EAB0,
  SetSecondName 0050F150, the transform slots 00516350 / 00516360. Side by side: CompanionIsDead, KeyId,
  ScaleEffectChanged, MoveSpeed, Radius; the rest ours only (plain objects of any zone). The one-byte slots cannot be
  hooked.
- **The manager, the companions, the 3D object** (`map_object_manager.cpp`, `map_object_companion_slots.cpp`,
  `object3d_small_methods.cpp`; `verify_map_object_manager.cpp`): CMapObjectManager's lifetime, names, CreateManager,
  CreateMapObject, FindAnimation, GiveDefaults, PrototypeEquipment (005125E0-00512660, 00512A30, 00512B20, 00513630,
  005143E0); the companions' lifetime and small slots (005104B0-00510760, 00510340, 00511290, 005117F0, 00512490,
  005156E0-00515810); CObject3D's small methods (00515D50, 005161A0, 005174E0, 00517910, 00517950, 005187B0-00518860,
  00519500, 00519530). Side by side: the class names / ids and the folder; the rest ours only (game start, zone load,
  hits and effects on a hosted game's objects, interface models).
- **0x00500000-0x0050E000** (`small_slots_500.cpp`, `scene_skin_lifetime.cpp`, `renderer_base_create.cpp`,
  `renderer_init.cpp`; `verify_small_slots_500.cpp`): the animation classes' names and ids (0050A6E0, 0050A6F0,
  0050B180-0050B1A0, 0050C6F0, 0050C700), VectorCopy 00503F20, RendererFrameTime 00500A90, the fake unit skin's lifetime
  (scene::SceneSkin, 0x50 bytes: 00508700, 005087B0, 00508870, 005088E0, 00508930), CSceneRenderer::CreateBase 00500540
  (the transform pipeline, CPUID(1) into the globals at 007CAED0, the MMX check and `2DintMMX.dll` from the work path; it
  only goes through the renderer's own interfaces, no device calls), MatrixTransposeInPlace 00501980,
  CTransformPipeline::Top 005019D0 and CSceneRenderer::SetView 005015A0 (slot 25 of a live vtable, never called).
  Side by side: the names / ids, VectorCopy, MatrixTransposeInPlace, Top (no scenario reaches the last three); ours
  only: the skin's lifetime (the main menu's figures), CreateBase (every start), SetView (none).
- **config/library.csv**: 0051C890, 0051E840, 0051EFD0, 0051F0D0, 0050E580, 0050FBC0, 00510160, 005101A0, 005102A0,
  00511FB0, 005124D0, 00512520, 00519FC0, 00500040, 005002D0, 00500320, 00500350, 00500380, 005003B0, 005003D0
  (STLport / MFC instantiations and unwind-only helpers; each row says why). **config/dead_code.csv**: 00505410,
  005054E0, 005055F0 (only called from the dead 0044A7B0), 0050BF00.
- **Risks**: the lifetime and part-set functions are consistency checks only. CreateBase's CPU detection uses the
  compiler's `__cpuid` / `__readeflags` where the original has inline CPUID; the globals it fills were compared by hand
  with the disassembly, not by a run (this worker could not run the game).
