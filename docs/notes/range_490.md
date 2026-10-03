# game.exe 0x00490000-0x004B0000

427 Ghidra functions (plus 28 EH funclets). Code: `src/game/particle_tick.cpp`,
`particle_emitters.cpp`, `particle_render.cpp`, `quaternion.cpp`, `random.cpp`. Verify wrappers:
`tests/verify/verify_particle_tick.cpp`, `verify_particle_emitters.cpp`, `verify_particle_render.cpp`,
`verify_quaternion.cpp`, `verify_random_init.cpp`. Scenario: `tests/scenarios/particles.txt`.
Layouts in `effects.h` (CParticleSource, Particle, ParticlePool, the render arrays).

Ghidra's HTTP disassembly stops at jump tables (00490130 came back truncated); use
`dumpbin /disasm /range:` on game.exe instead.

## Map

| Range | What |
|---|---|
| 00490130-00496990 | CParticleSource per-type callbacks (emit / update / element, see effects.md). Many callbacks are labels, not Ghidra functions, and do not count. Types from the name table 0078B120 (0x201B-0x2026 Modifier1-12, 0x2030 LightningBlast, 0x2038 ZoneExit, 0x203E Portal, 0x2043 Sparks, 0x2049 FeebleMind, 0x204A/B FeetCloud, 0x2051 StartTrans; 0x2039/0x203A/0x203B unnamed). 00496C50: emit-one helper with no callers. |
| 00496A20-00497EB0 | CParticleSource::AddParticle, Tick (slot 6), Draw and its helpers (shared arrays 007A9BD0 / 007A9BE0). |
| 00497EB0-00498E80 | element allocator; particle manager pieces: 00497FE0 (manager destructor), 004981B0 source factory, 00498310 source destructor, 00498400 create source (13 callers), 00498480 / 00498630 (save/load-ish, STLport), particle pool 00498870 / 004987C0, 00498940 draw all sources (depth sort through 00499200 / 00499380 / 004994E0, an STLport introsort on a vector<float, source*> at 007A9C54), 00498B20 / 00498BB0 stop/start all (no callers). |
| 00498C40-00499DC0 | STLport/MFC instantiations for the manager (hash containers, CArchive serialisation 00498D00 / 00498EA0). |
| 00499DC0-0049B140 | global-map helpers (0062Exxx callers) and a small procedural-texture class (vtable 007420D4: 0049A090 / 0049A220). |
| 0049B140-0049E790 | 0049B140 (17 callers), 0049B4B0 / 0049B780 byte-array builders; 0049BEA0 (1563, called once per zone load from the map loader 00481660): median-cut colour quantisation (an STLport list of boxes, hash_map of colours, vectors and introsorts by channel; 0049BB70, 0049C*, 0049D*, 0049E0D0..0049E5F0 are its instantiations). |
| 0049E790-0049EE70 | quaternion helpers (axis/angle, conjugate products, log 0049EAD0 (no callers), slerp). |
| 0049EE70-0049F810 | ISAAC: Refill, Init, seeding 0049F660 (1 KB from a random file on C:\ via the recursive CFileFind 0049F4B0, mixed with rand()). |
| 0049F810-004A0350 | not Ghidra functions: static initialisers, a keyframe track evaluator (0049F890, called from 0044875A) and a 3DS chunk loader (0049FFD1, 004A02A8, 004A0314) with no callers. |
| 004A0350-004A4800 | 3DS readers (Read3dsMaterial 004A0BA0, Read3dsPercentage 004A09E0, mesh/keyframe chunks 004A1220..004A2690) and their STLport tree/exception helpers (004A29B0..004A47D0). Only reachable from the uncalled loader above: dead code. |
| 004A48F0-004A5700 | small helpers with many callers (refcounted strings / vectors: 004A54D0 50 callers, 004A5490, 004A5650...). |
| 004A5700-004B0000 | spell/item text tables: SSC command helpers (004A5E10, 004A5E90, 004A5FA0 from SSCCmd::Execute), the nine 0x28C-byte class templates built by 004A7630 / 004A7080 / 004A6580 (new game, from 0057F280), loaders 004A8310 / 004A8560 (from 0057ABA0 / 0057B3B0), and their STLport hash_map / vector / string instantiations (most of 004A8B70-004AFFF0). |

## Rewritten

| Address | Size | Name | Verify |
|---|---|---|---|
| 00496EB0 | 2045 | CParticleSource::Tick | ours on the real source; the original replays on a scratch copy (same particles as ours left them, a keep-everything update callback, no elements, no emission): wind, motion, basis, bounds compared |
| 004976B0 | 1575 | CParticleSource::Draw | original first with a stand-in renderer capturing the arrays, then ours draws; arrays, flag, hidden ticks compared |
| 00490130 | 2051 | CParticleSource::EmitModifier | both, state compared (source, particle, element array, generator); first emission against the original on a scratch source |
| 004923B0 | 1986 | CParticleSource::EmitLightning | same |
| 0049F150 | 863 | IsaacRandom::Init | original on a scratch copy, the seed material replayed through a wrapper on 0049F660 |
| 00496A20 | 549 | CParticleSource::AddParticle | ours only: tail, prev fields, bounds |
| 00495A30 | 583 | EmitFeetCloud | both |
| 00492D30 | 519 | EmitZoneExit | both |
| 00494770 | 439 | UpdatePortal | both |
| 004957E0 | 354 | EmitFeebleMind | both |
| 00497D50 | 341 | ParticleSpriteArray::SetSize | ours only: size, kept and zeroed elements |
| 0049ED20 | 306 | Quaternion::Slerp | pure |
| 00494CD0 | 285 | UpdateSparks | both |
| 004933F0 | 255 | UpdateGroundDust | both |
| 0049E870 | 221 | Quaternion::PreMultiplyConjugate | scratch copy |
| 0049E950 | 217 | MultiplyByConjugate | pure |
| 004987C0 / 00498870 | 173 / 152 | ParticlePool::Release / Acquire | ours only: list checks |
| 00496990 | 129 | TickStartTrans | both (ours only when it detaches) |
| 004934F0 | 108 | TickGroundPlane | both |
| 00493280 | 104 | UpdateGlow | both |
| 0049E790 / 0049E800 | 98 / 98 | Quaternion::SetAngleAxis / SetAxisAngle | pure |
| 00497EB0 | 36 | AllocateElements | ours only: alignment |
| 00497CE0 | 35 | BindParticleTexture | ours only |
| 00494930 | 30 | TickPortal | both |
| 00497D40 | 16 | NextParticle | pure |

## Forwarders added

00484C00 AttachmentPoint, 00485280 AttachmentOffset, 0049F660 IsaacRandom::Seed, 00488970 Range and
00564070 SurfaceHeight (declared `double`: they leave ST0 unrounded), 00561150 ground plane (map
007AF78C), 0044C470 debug message, 006E853C _ftol, 006E727B atexit, 00468F70 aligned vertex
allocation, 00468D60 sprite array constructor, 004F16A0 clip test, 00500970 texture load,
006E7320 _CIacos. None is on the Rebirth patch list.

## Original bugs kept on purpose

- Tick unlinks and pools a particle the previous update rejected, then still updates it and counts
  it in the bounds.
- EmitLightning never counts its bursts (+0x34 of point 0 is raised elsewhere, if at all).
- The renderer passes the blend flag with garbage in the upper bytes of the pushed register (the
  callee reads only the low byte; ours passes 0/1).

## Floating point

All files use `float_control(precise, on)`, `double` for FPU-stack values and a volatile `Store`
for every FSTP to a float; run-time constants are read from the exe through volatile reads.
Remaining differences only at 64-bit precision control (doubles passed to fsin/fsqrt/_ftol).

## Risks

- Tick's emission count, element calls and particle list surgery are checked only by list
  consistency; its floats are checked by the replay. Draw's comparison does not see the return
  value of the real draw.
- The Modifier needs a carrier: the scenario's script source only hits the no-carrier path; the
  buffs in the scenario may or may not attach Modifier sources.
- Not done (large): 0049BEA0 colour quantisation (zone load, STLport containers; see below),
  004A7630 / 004A7080 class templates (exception-state heavy, new game), the dead 3DS readers.

## Second pass: Create, draw-all, the remaining callbacks, factory

Code: `src/game/particle_create.cpp` (Create 00485540 as a per-type setup switch, Reset, Clear,
factory, destructor), `particle_draw.cpp` (draw-all and its depth sort), `particle_callbacks.cpp`
(every remaining per-type emit / update / element callback; only the `RET 4` / `RET 8` no-op
stubs 00485530, 00492FB0, 00493DD0, 00494A00, 00494DF0, 004950A0, 00495310, 004957D0, 00495A20,
00496260, 004965D0 are left), shared helpers in `particle_common.h`. Verify:
`tests/verify/verify_particle_create.cpp`, `verify_particle_draw.cpp`,
`verify_particle_callbacks.cpp`, shared wrappers in `particle_verify.h` (RunBoth / RunBothEmit /
`RunBothElement` / `RunBothSetup`: capture source, particle, element array and generator, run ours,
restore, run the original, compare everything). The scenario creates twenty more source types.

Sizes: Ghidra sizes where the callback is a Ghidra function, else (~) the span to the next
known start (it may include a following no-op stub).

| Address | Size | Name | Verify |
|---|---|---|---|
| 00484710 | 206 | Clear | ours only (pool, detach): empty list, no texture / attachment / callbacks / elements |
| 004847E0 | 525 | Reset | original on a scratch copy without texture / elements, all bytes compared |
| 00484AE0 | 95 | WorldPosition | output compared |
| 00485540 | 12920 | CreateForType | original first on a scratch source (element array seeded from ours), all 0x1F0 bytes and the elements compared |
| 004889D0 | ~992 | EmitMushroom | both; the setup call (element array) against the original on a scratch source |
| 00488DB0 | ~336 | UpdateMushroom | both |
| 00488F90 | ~512 | EmitNuke | both |
| 00489320 | ~1840 | EmitCastRing | both; the setup call (element array) against the original on a scratch source |
| 00489A50 | ~240 | UpdateCastRing | both |
| 00489B40 | ~160 | TickCastRing | both |
| 00489BE0 | ~784 | EmitBanshee | both; the setup call (element array) against the original on a scratch source |
| 00489EF0 | ~64 | UpdateBanshee | both |
| 00489F30 | ~64 | TickBanshee | both |
| 00489F70 | 914 | EmitTornado | both |
| 0048A310 | 577 | UpdateTornado | both |
| 0048A560 | ~912 | TickTornado | both |
| 0048A8F0 | ~1200 | EmitGeyser | both |
| 0048ADA0 | ~288 | UpdateGeyser | both |
| 0048AEC0 | ~448 | TickGeyser | both |
| 0048B080 | 1262 | EmitPoisonFog | ours only (moves particles from the shared pool): nothing emitted, the new disc inside the bounds |
| 0048B630 | 1456 | EmitHealing | both; the setup call (element array) against the original on a scratch source |
| 0048BBE0 | ~256 | UpdateHealing | both |
| 0048BCE0 | ~112 | TickHealing | both |
| 0048BD50 | 694 | EmitVulkan | both |
| 0048C010 | ~128 | UpdateVulkan | both |
| 0048C090 | ~1280 | EmitBlast | both |
| 0048C590 | 456 | UpdateBlast | both |
| 0048C760 | ~160 | TickBlast | both |
| 0048C800 | ~720 | EmitFireBall | both |
| 0048CAD0 | ~208 | UpdateFireBall | both |
| 0048CBA0 | ~816 | EmitArrowSmoke | both |
| 0048CED0 | 379 | UpdateArrowSmoke | both |
| 0048D050 | ~736 | EmitAcidRay | both |
| 0048D330 | ~240 | UpdateAcidRay | both |
| 0048D420 | ~976 | EmitStench | both; the setup call (element array) against the original on a scratch source |
| 0048D7F0 | ~224 | UpdateStench | both |
| 0048D8D0 | ~1376 | EmitLink | both; the setup call (element array) against the original on a scratch source |
| 0048DE30 | 519 | UpdateLink | both |
| 0048E040 | ~48 | TickLink | both |
| 0048E070 | 1871 | EmitOrbit | both; the setup call (element array) against the original on a scratch source |
| 0048ED60 | ~192 | EmitTendril | both |
| 0048EEA0 | 1113 | EmitShower | both |
| 0048F300 | ~128 | UpdateShower | both |
| 0048F380 | ~48 | TickShower | both; ours only from the fifth tick (detaches) |
| 0048F3B0 | ~704 | EmitFalling | both |
| 0048F6F0 | 245 | TickFalling | both; ours only from the second tick (detaches) |
| 0048F7F0 | ~704 | EmitClayRing | both |
| 0048FAB0 | ~304 | UpdateClayRing | both |
| 0048FBE0 | ~464 | TickClayRing | both |
| 0048FDB0 | 676 | EmitTeleport | both |
| 00490060 | ~176 | UpdateTeleport | both |
| 00490110 | ~32 | TickTeleport | both |
| 00490990 | ~368 | UpdateModifier | both |
| 00490B00 | ~272 | TickModifier | both |
| 00490C10 | ~1568 | EmitCastSwirl | both |
| 00491230 | ~352 | UpdateCastSwirl | both |
| 00491390 | ~144 | TickCastSwirl | both |
| 00491420 | ~1040 | EmitColumn | both; the setup call (element array) against the original on a scratch source |
| 00491830 | ~160 | UpdateColumn | both |
| 004918D0 | ~16 | TickColumn | both |
| 004918E0 | ~1872 | EmitCasting | both; the setup call (element array) against the original on a scratch source |
| 00492030 | ~416 | UpdateCasting | both |
| 004921D0 | ~480 | TickCasting | both |
| 00492B80 | ~320 | UpdateLightning | both |
| 00492CC0 | ~112 | TickLightning | both |
| 00492F40 | ~112 | UpdateZoneExit | both |
| 00492FC0 | ~704 | EmitGlow | both; the setup call (element array) against the original on a scratch source |
| 004932F0 | ~16 | TickGlow | both |
| 00493300 | ~240 | EmitGroundDust | both |
| 00493560 | ~528 | EmitBag | both |
| 00493770 | ~336 | UpdateBag | both |
| 004938C0 | ~1104 | TickBag | both |
| 00493D10 | ~64 | EmitPortalStar | both |
| 00493D50 | ~128 | UpdatePortalStar | both |
| 00493DE0 | ~912 | EmitCylinder | both |
| 00494170 | ~464 | UpdateCylinder | both |
| 00494340 | ~304 | TickCylinder | both |
| 00494470 | ~768 | EmitPortal | both |
| 00494950 | ~96 | EmitFireStar | both |
| 004949B0 | ~80 | UpdateFireStar | both |
| 00494A10 | ~704 | EmitSparks | both |
| 00494E00 | ~304 | EmitVisionStar | both |
| 00494F30 | ~368 | UpdateVisionStar | both |
| 004950B0 | ~112 | EmitRegeneration | both |
| 00495120 | ~496 | UpdateRegeneration | both |
| 00495320 | ~688 | EmitSilence | both |
| 004955D0 | ~512 | UpdateSilence | both |
| 00495950 | ~208 | UpdateFeebleMind | both |
| 00495C80 | ~384 | UpdateFeetCloud | both |
| 00495E00 | ~432 | TickFeetCloud | both |
| 00495FB0 | ~512 | EmitStun | both; ours only without a carrier or once its stun is over (Attach) |
| 004961B0 | ~176 | UpdateStun | both; ours only without a carrier (Attach) |
| 00496270 | ~576 | EmitCurseStars | both |
| 004964B0 | ~288 | UpdateCurseStars | both; ours only without a carrier (Attach) |
| 004965E0 | ~608 | EmitStartTrans | both |
| 00496840 | ~336 | UpdateStartTrans | both |
| 004981B0 | 281 | NewParticleSource | ours only (allocates): vtable, type, empty list, Reset defaults |
| 00498310 | 214 | Destruct | ours only (frees): empty block-less list |
| 00498940 | 473 | ParticlePool::DrawAll | ours only (draws): sorted order and draw calls through the original sorts |
| 00499200 | 321 | ParticleDepthArray::SetAtGrow | ours only (grows the shared array) |
| 00499380 | 340 | IntroSortByDepth | both on copies of the array, compared |
| 004994E0 | 438 | FinalSortByDepth | both on copies, compared |
| 00499C20 | 173 | PopHeapByDepth | both on copies, compared |
| 00499CD0 | 240 | MakeHeapByDepth | both on copies, compared |

### How the callbacks were checked offline

Besides the in-game verify build, every callback was run against the original before committing
in a lab (not in the repo): a 32-bit exe linked at 0x10000 maps game.exe's sections at 0x400000
in a static arena, stubs the heap, texture load, FatalError, SurfaceHeight, the attachment
helpers, a fake unit carrier (vtable slots 12, 52, 55, 60; orientation and +0x134 / +0x264) and
the terrain vtable, installs the repo's verify wrappers over the original code, and runs the
original Tick on every type 0x2000-0x2052 (with and without a carrier, three seeds, 90 ticks)
at 24- and 53-bit precision control: 104 functions, 4.8 million calls, no mismatch. The
Create, Reset, Clear, sort and draw-all wrappers were run the same way.

### Forwarders added (second pass)

0040CB20 engine object base constructor, 00455930 base destructor body (through its address),
00497EB0 element allocator (through its address), 00516A80 figure part by name, 005188B0 part
position, 00560F00 platform surface height (007AF78C), the unit's vtable slots 52 (point by
index) and 55 (stun over), the terrain's (007AF5B8) slot 28, 0049EE70 / 004667F0 generator,
00488940 DivideVector, 00401DE0 Vector3 constructor, 004849F0 Attach, 00500A00 texture load,
00480080 FatalError (Rebirth-patched: called through its address). Sorts 00499380 / 004994E0
and the heap helpers are called by draw-all through their addresses.

### Original bugs kept (second pass)

- CastSwirl reads its locals one slot off after `POP EDI`: element +0x14 gets sign * carrier
  radius instead of the size.
- Mushroom's spread is ln(1) = 0; Falling's direction spread is ftol(10 degrees) = 0.
- Teleport adds its alpha step as an unsigned sum capped at 255: a fade out below 19 jumps back
  to opaque.
- LightningBlast sparks shift the alpha table by 22, not 24.
- FeetCloud uses the source's lifetime as the particles' RGB.
- The ring sparks (Casting / Healing) use the z range quirk of the original; Shower and FireBlast
  keep their odd counters (see the comments in the code).
- Every FCOMP "below" / "equal" test is written to take the unordered case the same way.

### Skipped

- 0049BEA0 (1563 bytes, once per zone load): median-cut colour quantisation built on STLport
  list / hash_map / vector and introsort instantiations (0049BB70, 0049C*, 0049D*, 0049E0D0..).
  Reproducing it exactly means rewriting those container instantiations with the game's node
  allocator and its exact iteration order; left for a separate task.
- 004A7630 / 004A7080 (new-game class templates): long exception-state chains over STLport
  strings and vectors; not attempted.

### Risks (second pass)

- 64-bit precision control is not reproduced (doubles in between); the game runs at 24 bits.
- Paths the lab could not reach with its fake carrier (real figures and platforms, a stun that
  ends, units without parts) are checked only in game by the verify wrappers.
- Callbacks that let the source go (Attach) and the factory / destructor are verified ours only.
- The Stun verify wrapper asks the carrier's slot 55 once more before running the callback.

Shared headers changed (additive): `effects.h` (callback, factory and destructor declarations,
field renames frameColumns / frameRect*, ParticlePool refCount / sources / DrawAll, depth-sort
types); new `particle_common.h` and `tests/verify/particle_verify.h`.
