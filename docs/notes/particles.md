# Particle sources (0x00484710-0x004A0350)

Code: `particle_tick.cpp` (Tick, AddParticle, pool), `particle_emitters.cpp` (EmitModifier, EmitLightning and a few
more), `particle_render.cpp` (Draw), `particle_create.cpp` (`Create` 00485540 as a per-type setup switch, Reset, Clear,
factory, destructor), `particle_draw.cpp` (draw-all and its depth sort), `particle_callbacks.cpp` (every other per-type
callback), `effects.cpp` (the callbacks and source setters listed in `effects.md`), `quaternion.cpp`, `random.cpp`;
layouts in `effects.h` (CParticleSource, Particle, ParticlePool, the render arrays), shared helpers in
`particle_common.h`. Verify: `tests/verify/verify_particle_*.cpp` with the shared wrappers in `particle_verify.h`
(`RunBoth` / `RunBothEmit` / `RunBothElement` / `RunBothSetup`: capture source, particle, element array and generator,
run ours, restore, run the original, compare everything). Scenarios: `particles.txt` (creates every source type it
can), `effects.txt`. Weather, fog and the first callbacks are in `effects.md`; quaternion helpers and ISAAC in `core.md`.

Ghidra's HTTP disassembly stops at jump tables (00490130 came back truncated): use `dumpbin /disasm /range:` on
game.exe for the callbacks.

## Structure

- `CParticleSource` (vtable 0073CFD0, class id 0xC, 0x1F0 bytes, made by `NewParticleSource` 004981B0). Slot 2 `Create`
  fills the per-type parameters by `type_` (0x2000-0x2052; names in the table at 0078B120: name, type, lifetime) and
  stores three callbacks (all `__thiscall` on the source): +0x1E0 emit one particle `(Particle*, int)`, +0x1E4 update one
  particle (0 drops it), +0x1E8 update one element of the array at +0x154 (0x38-byte elements). Slot 6 `Tick`
  00496EB0 computes the source motion, runs the element callback per element and the update callback per particle
  (saving / restoring the "prev" fields), tracks the bounding box and emits up to `max(emitCount, distance moved /
  emitSpacing)` particles with `emitChance`% each. `Particle` (0x64 bytes) is pooled by 00498870 / 004987C0.
  Types 0x201B-0x2026 Modifier1-12, 0x2030 LightningBlast, 0x2038 ZoneExit, 0x203E Portal, 0x2043 Sparks, 0x2049
  FeebleMind, 0x204A / B FeetCloud, 0x2051 StartTrans; 0x2039-0x203B are unnamed. 00678EC0 is the world's script
  particle source (class 0x4A, `CreateParticleSource`): its first tick creates the `CParticleSource` (00498400), every tick
  copies the position and calls `ScaleSize(size / baseSize)`.
- Address map: 00490130-00496990 per-type callbacks (many are labels, not Ghidra functions); 00496A20-00497EB0
  AddParticle, Tick, Draw and helpers (shared arrays 007A9BD0 / 007A9BE0); 00497EB0-00498E80 element allocator, manager
  pieces (00498400 create source, 13 callers; 00498480 / 00498630 save / load-ish; 00498940 draw all sources, depth sort
  through 00499200 / 00499380 / 004994E0 / 00499C20 / 00499CD0, an STLport introsort on a `vector<float, source*>` at
  007A9C54); 00499DC0-0049B140 global-map helpers (0062Exxx callers) and a procedural-texture class (vtable 007420D4:
  0049A090 / 0049A220); 0049B140-0049E790 the renderer's line list (0x20 bytes, `LineList` in `figure_part_bounds.cpp`:
  0049B140 appends a point (17 callers), 0049B780 draws it, 0049B4B0 draws the strip: rewritten in `renderer_lines.cpp`,
  see `renderer.md`) and 0049BEA0 (median-cut colour quantisation, once per zone load from 00481660); 0049EE70-0049F810
  ISAAC; 0049F810-004A4800 static initialisers, a keyframe track evaluator (0049F890, from 0044875A) and a 3DS loader
  with its readers (no callers: dead).

## Rewritten

Verify "both" = ours first, state captured, restored, then the original, everything compared (source 0x1F0 bytes,
particle 0x64, element 0x38, the ISAAC generator at 007A9D40 0x814 bytes, return value). "Setup" = the setup call that
fills the element array is also compared against the original on a scratch source.

| Addresses | Names | Verify |
|---|---|---|
| 00496EB0 | `Tick`: ours on the real source, the original replays on a scratch copy with the same particles as ours left them, a keep-everything update callback, no elements, no emission (wind, motion, basis, bounds compared) | replay |
| 004976B0 | `Draw`: original first with a stand-in renderer capturing the arrays, then ours; arrays, flag, hidden ticks compared | |
| 00496A20, 00497D50, 004987C0 / 00498870, 00497EB0, 00497CE0, 004981B0, 00498310, 00498940, 00499200 | `AddParticle`, `ParticleSpriteArray::SetSize`, pool `Release` / `Acquire`, `AllocateElements`, `BindParticleTexture`, `NewParticleSource`, `Destruct`, `DrawAll`, depth-array `SetAtGrow` | ours only (list checks, alignment, sorted order and draw calls through the original sorts) |
| 00499380, 004994E0, 00499C20, 00499CD0 | introsort, final sort, pop heap, make heap by depth | both on copies, compared |
| 00484710, 004847E0, 00484AE0, 00485540 | `Clear` (ours only), `Reset` (original on a scratch copy), `WorldPosition`, `CreateForType` (13 KB switch; original first on a scratch source with the element array seeded from ours) | all 0x1F0 bytes and elements compared |
| 00490130 EmitModifier, 004923B0 EmitLightning, 00495A30, 00492D30, 004957E0 | emitters | both (Modifier / Lightning: first emission against the original on a scratch source) |
| 00494770, 00494930, 00496990, 004934F0, 00493280, 00494CD0, 004933F0 | UpdatePortal, TickPortal, TickStartTrans (ours only when it detaches), TickGroundPlane, UpdateGlow, UpdateSparks, UpdateGroundDust | both |
| Emit / Update / Tick triples (callbacks of one effect): Mushroom 004889D0 / 00488DB0; Nuke 00488F90; CastRing 00489320 / 00489A50 / 00489B40; Banshee 00489BE0 / 00489EF0 / 00489F30; Tornado 00489F70 / 0048A310 / 0048A560; Geyser 0048A8F0 / 0048ADA0 / 0048AEC0; PoisonFog 0048B080; Healing 0048B630 / 0048BBE0 / 0048BCE0; Vulkan 0048BD50 / 0048C010; Blast 0048C090 / 0048C590 / 0048C760; FireBall 0048C800 / 0048CAD0; ArrowSmoke 0048CBA0 / 0048CED0; AcidRay 0048D050 / 0048D330; Stench 0048D420 / 0048D7F0; Link 0048D8D0 / 0048DE30 / 0048E040; Orbit 0048E070; Tendril 0048ED60; Shower 0048EEA0 / 0048F300 / 0048F380; Falling 0048F3B0 / 0048F6F0; ClayRing 0048F7F0 / 0048FAB0 / 0048FBE0; Teleport 0048FDB0 / 00490060 / 00490110; Modifier 00490990 / 00490B00; CastSwirl 00490C10 / 00491230 / 00491390; Column 00491420 / 00491830 / 004918D0; Casting 004918E0 / 00492030 / 004921D0; Lightning 00492B80 / 00492CC0; ZoneExit 00492F40; Glow 00492FC0 / 004932F0; Bag 00493560 / 00493770 / 004938C0; PortalStar 00493D10 / 00493D50; Cylinder 00493DE0 / 00494170 / 00494340; Portal 00494470; FireStar 00494950 / 004949B0; Sparks 00494A10; VisionStar 00494E00 / 00494F30; Regeneration 004950B0 / 00495120; Silence 00495320 / 004955D0; FeebleMind 00495950; FeetCloud 00495C80 / 00495E00; Stun 00495FB0 / 004961B0; CurseStars 00496270 / 004964B0; StartTrans 004965E0 / 00496840 | both; Setup for Mushroom, CastRing, Banshee, Healing, Stench, Link, Orbit, Column, Casting, Glow; PoisonFog emit ours only (moves particles from the shared pool: nothing emitted, the new disc inside the bounds); TickShower ours only from the fifth tick and TickFalling from the second (they detach); Stun / CurseStars ours only without a carrier or once the stun is over (Attach) |

Only the `RET 4` / `RET 8` no-op stubs 00485530, 00492FB0, 00493DD0, 00494A00, 00494DF0, 004950A0, 00495310,
004957D0, 00495A20, 00496260, 004965D0 are not rewritten. Callbacks that let the source go (`Attach`) and the
factory / destructor are verified ours only; the Stun wrapper asks the carrier's slot 55 once more before running
the callback.

## How the callbacks were checked offline

Besides the verify build, every callback was run against the original in a lab (not in the repo): a 32-bit exe linked
at 0x10000 maps game.exe's sections at 0x400000 in a static arena, stubs the heap, texture load, FatalError, SurfaceHeight,
the attachment helpers, a fake unit carrier (vtable slots 12, 52, 55, 60; orientation, +0x134 / +0x264) and the terrain
vtable, installs the repo's verify wrappers over the original code and runs the original Tick on every type
0x2000-0x2052 (with and without a carrier, three seeds, 90 ticks) at 24- and 53-bit precision control: 104 functions,
4.8 million calls, no mismatch.

## Floating point

All files use `float_control(precise, on)`, `double` for FPU-stack values and a volatile `Store` for every FSTP to a float;
run-time constants are read from the exe through volatile reads. Every FCOMP "below" / "equal" test is written to take
the unordered case the same way. Remaining differences only at 64-bit precision control. The random-range constants
0073C468 (~2^-32) and 0073CDA0 (~2^-33) carry a stray mantissa bit; used as is.

## Original bugs kept on purpose

- `Tick` unlinks and pools a particle the previous update rejected, then still updates it and counts it in the bounds.
- `EmitLightning` never counts its bursts (+0x34 of point 0 is raised elsewhere, if at all).
- The renderer passes the blend flag with garbage in the upper bytes of the pushed register (the callee reads the low
  byte; ours passes 0 / 1).
- CastSwirl reads its locals one slot off after `POP EDI`: element +0x14 gets sign * carrier radius instead of the size.
- Mushroom's spread is ln(1) = 0; Falling's direction spread is ftol(10 degrees) = 0.
- Teleport adds its alpha step as an unsigned sum capped at 255: a fade out below 19 jumps back to opaque.
- LightningBlast sparks shift the alpha table by 22, not 24; FeetCloud uses the source's lifetime as the particles' RGB;
  the ring sparks (Casting / Healing) use the z range quirk; Shower and FireBlast keep odd counters (see the code).

## Risks and open items

- `Tick`'s emission count, element calls and particle-list surgery are checked only by list consistency (its floats by
  the replay); `Draw`'s comparison does not see the return value of the real draw.
- The Modifier effect needs a carrier: the scenario's script source only hits the no-carrier path.
- Paths the lab could not reach with its fake carrier (real figures and platforms, a stun that ends, units without
  parts) are checked only in game.
- 64-bit precision control is not reproduced (the game runs at 24 bits).
- Not rewritten: 0049BEA0 colour quantisation (zone load; STLport list / hash_map / vector / introsort instantiations
  0049BB70, 0049C*, 0049D*, 0049E0D0..: reproducing it exactly needs the game's node allocator and iteration order),
  00498480 / 00498630, the procedural texture class 0049A090 / 0049A220, 0049F660 ISAAC seeding (reads a random file from
  C:\ through the recursive CFileFind 0049F4B0), the dead 3DS readers.
