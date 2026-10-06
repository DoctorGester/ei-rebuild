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
| 00500000-0050E000 | rendering and animation (tracks and cages rewritten; the rest not looked at) |
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
| 0051CC20, 0051CCD0 | CObject3DClientSpecific InvisiblePulse / StunnedPulse (unit_client_animation.cpp) | original, parts put back, ours; CheckNear |

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

- Create3D drops the side half (+0x94) without releasing it; Destroy3D only decrements the placed
  parts' counts (no release, nothing freed at 0) while it releases the pending ones.

## Skipped and why

- Companion Create / Destroy (00510770, 005107B0, 005157C0, 005157E0), Load / Save (0050F7A0 /
  0050F900, 005159D0 / 00515AC0), CArray Serialize/SetSize instantiations: container members,
  exception frames, stream classes not rewritten.
- 0050F170 (fade mode from a float, slot 47): small, next candidate.
- Client effect visuals (00510B80, 005112A0, 005114D0) and light helpers (00511830, 00511950,
  005119B0, 00511A00): large, many callees; next candidates.
- 0050E9B0 (slot 34) and 0050F080 (stats refresh): depend on the UnitStats builders (00522C50 /
  005136D0).
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
