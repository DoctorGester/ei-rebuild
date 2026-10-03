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
