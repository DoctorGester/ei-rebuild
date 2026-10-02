# Weather, fog and particles (0x004E1770-0x004E5000; particles in 0x00480000-0x00490000)

Code: `src/game/effects.h` (layouts), `src/game/effects.cpp` (particle callbacks and source
setters), `src/game/effects_weather.cpp` (rain/snow fade, visibility fog). Verify wrappers:
`tests/verify/verify_effects.cpp`. Scenario: `tests/scenarios/effects.txt`. Shared header touched:
`src/game/random.h` (declaration of `IsaacRandom::Range` only). Names are provisional and set in
Ghidra.

## Map

### Particles

- **CParticleSource** (vtable 0073CFD0, class id 0xC, 0x1F0 bytes, allocated by 004981B0). Slot 2
  (00485540, `Create`) fills the per-type parameters by `type_` (0x2000-0x2052; the script names
  are the table at 0078B120: name, type, ?) and stores three callbacks: +0x1E0 emit one particle
  (`(Particle*, int index)`), +0x1E4 update one particle (`(Particle*)`, 0 drops it), +0x1E8 update
  one element of the source's array at +0x154 (0x38-byte elements). All are `__thiscall` on the
  source. Slot 6 (00496EB0, `Tick`, not rewritten; outside the range) computes the source motion,
  runs the element callback for every element, the update callback for every particle (saving and
  restoring the "prev" fields around it), tracks the bounding box, and emits up to
  `max(emitCount, distance moved / emitSpacing)` particles with `emitChance`% each.
- **Particle** (0x64 bytes, pooled by 00498870 / 004987C0): position, size, previous position and
  size, a type-specific velocity / drift, frame, frame timer, life, state, colour, dead flag.
- 00678EC0 is the world's script particle source (created by `CreateParticleSource`, id 0x4A): on
  its first tick it creates the CParticleSource (00498400); every tick it copies the position and
  calls `ScaleSize(size / baseSize)`.
- Callbacks by type (from 00485540): CampFire/Fire/Smoke/0x2010 use 0048E9E0/0048EC20 (0x2010 also
  0048EE20); PoisonFog (0x2007/0x2008) 0048B080/0048B570; Tornado (0x200A) and CurseHolder
  (0x2050) 00489F70/0048A310; Nuke (0x200C) 00489190/004892E0; Mushroom/Transform 00488F00; the
  spheres (0x2014-0x2017) and Antimagic (0x201A) 0048E7C0; 0x2034-0x2037 0048F670. Many more
  callbacks are labels inside Ghidra's function bodies (0048C800, 0048CAD0, ...), up to 0x00496xxx.

### Weather and fog

- **CRainSnow** (the world's rain/snow layer): 004E3FC0 starts a fade towards a mode (1 rain with
  the `nature\rain_circle.wav` loop, 2 snow), 004E40B0 advances it, 004E4050 ends it, 004E41F0
  sets the wind (from the `SetWind` command through 00564190), 004E4240 / 004E4C80 build and draw
  the rain / snow vertices (Direct3D; not rewritten), 004E5680 is the per-frame entry, 004E3C20
  allocates the buffers, 004E3980 builds a smoothed random curve with rand() (setup).
- **CVisibilityFog** (vtable 0073D6B4, "CVisibilityFog", id 0x5F, 0x6C bytes, created by
  004E2290): a `hash_map<uint32, FogCell>` at +0x54 keyed by `(y << 16) + x` of the map square.
  004E1C60 marks the visible squares (adding cells), 004E1AB0 (slot 6) fades them per frame and
  drops the transparent ones, 004E1770 computes the culling bounds, 004E1EB0 renders (not
  rewritten), 004E2990 builds the 4x4 texture tile table at 007ABA58. 004E2A70..004E3630 are the
  STLport hashtable/vector instantiations (begin, next bucket, find, insert, erase, resize).

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 0048F670 | `CParticleSource::UpdateFalling` | both, state compared (source, particle, generator) |
| 0048B570 | `CParticleSource::UpdateCloud` | both, state compared |
| 0048EC20 | `CParticleSource::UpdateSmoke` | both, state compared |
| 0048E9E0 | `CParticleSource::EmitSmoke` | both, state compared |
| 00489190 | `CParticleSource::UpdateDebris` | both, state compared |
| 0048E7C0 | `CParticleSource::UpdateOrbit` | both, state compared |
| 004892E0 | `CParticleSource::TickNuke` | both, state compared (element) |
| 0048EE20 | `CParticleSource::TickTendril` | both, state compared |
| 00488F00 | `CParticleSource::TickMushroom` | both, state compared; the camera-shake tick ours only |
| 00485480 | `CParticleSource::ScaleSize` | ours on a scratch copy, 0x1F0 bytes compared |
| 00485440 / 00485450 | `CParticleSource::StopEmitting` / `StartEmitting` | scratch copy, flags compared |
| 00485360 | `CParticleSource::Stop` (flags, detach) | ours only: flags and attachment checked |
| 00488940 | `DivideVector` (`__fastcall`) | pure, ours into scratch first |
| 00488970 | `IsaacRandom::Range` | both, result bits and generator compared |
| 004E40B0 | `CRainSnow::UpdateFade` | original on a copy without the sound, ours on the real one; mode, density, fade compared |
| 004E4050 | `CRainSnow::FinishFade` | same |
| 004E41F0 | `CRainSnow::SetWind` | ours on a scratch copy, whole object compared |
| 004E1770 | `CVisibilityFog::ComputeBounds` | original on a scratch copy (map shared read-only), 0x6C bytes compared |
| 004E1AB0 | `CVisibilityFog::UpdateCells` (slot 6) | ours only: count, threshold, dropped keys gone, result |
| 004E1C60 | `CVisibilityFog::Reveal` | ours only: exactly the grid's visible squares marked, keys match squares, count |

"Both, state compared": ours runs first; the source (0x1F0 bytes), the particle (0x64) or element
(0x38) and the ISAAC generator (007A9D40, 0x814 bytes) are captured, restored, the original runs,
and everything plus the return value is compared. The original's results stand.

## Floating point

Each function's generated x87 code was compared with the original (`cl /FAs` with the build's
flags). Under `/fp:fast` the compiler drops `static_cast<float>` roundings and reuses register
values after a store, so:
- values the original rounds through a float temporary go through `StoreFloat`;
- values the original stores and then reads back from memory are read through `Load` (a volatile
  read) where the compiler would otherwise reuse the unrounded register (fog bounds, fog alpha
  test, smoke emit velocities, the fog cell centre passed to the ground height);
- values the original keeps on the FPU stack across a store (the smoke's risen z, the debris's
  vertical speed, the orbit's x base, the smoke growth push) are written as repeated expressions or
  kept in registers, and the listing confirms they are not spilled.
Remaining differences only at 64-bit precision control: x87::fcos / fsqrt and the game's _ftol
receive a double (the original passes the 80-bit value; same convention as the rest of the repo).

## Forwarders and original calls

- 004849F0 `CParticleSource::Attach` (forwarder), 004113A0 camera shake (on the camera 0079B2A8),
  006E853C the game's `_ftol`, 0044A230 `SubtractVectors` and 00401DE0 `Vector3::Construct`
  (rewritten elsewhere, not patched).
- 006B9100 / 006B94A0 the sound manager's set-volume / stop (on *007AF784).
- 00455950 `CEngineObject::World`, world vtable slot 30 (ground height at x, y).
- The fog's static dead-key vector at 007ABB58 (guard bit 007ABB64) with the original constructor
  004E2BA0, insert-overflow 004E2FA0 and atexit 006E727B (destructor 004E1C50); the cell map's
  insert 004E2E90 and erase-by-key 004E2F00. Iteration and find use `stlp::hash_map` (same order).
- None of the callees is on the Rebirth patch list.

## Original bugs kept on purpose

- `UpdateCloud` raises the alpha byte with `(step * 255 + alpha) << 24`, which wraps; particles die
  once the byte is at most 9.
- `Reveal` initialises a new cell by an overlapping `REP MOVSD` from its key, so the padding bytes
  of the cell hold key bytes; kept by filling the template the same way.
- `UpdateCells` calls `World()` and ignores the result.
- The random-range constants 0073C468 (~2^-32) and 0073CDA0 (~2^-33) carry a stray mantissa bit
  (0x3DF0000000100000, 0x3DE0000000100000); used as is.

## Skipped

- Particle emitters with heavy float/random code not yet done: 00489F70 / 0048A310 (Tornado),
  0048B080 (PoisonFog emit: terrain height, list insert), 0048B630, 0048BD50, 0048C590, 0048CED0,
  0048DE30, 0048E070, 0048EEA0, 0048F6F0, 0048FDB0 and the label callbacks; good next candidates
  (same pattern: thiscall on the source, state compare in verify).
- 00485540 `Create` (13 KB switch of constants; setup), 00484710 / 004847E0 (reset), 00484C00
  (attachment point lookup through the unit's figure), 00485280 (attachment offset).
- Rain/snow vertex building 004E4240 / 004E4C80 and the fog renderer 004E1EB0: they build vertex
  buffers and call Direct3D; 004E3980 / 004E3C20 setup.
- 0x00480000-0x00485000: error reporting, DirectX names, configuration, containers (not effects).

## Risks

- `Reveal` and `UpdateCells` are verified only for consistency, not against the original.
- The camera-shake tick of the Mushroom is not compared.
- The scenario cannot check particles directly; it relies on the verify totals in the report.
  Rain/snow fading and the fog only run where the map has weather / the fog is active.
