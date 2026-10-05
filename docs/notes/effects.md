# Weather, fog and particle helpers (0x004E1770-0x004E5000; particle pieces in 0x00480000-0x00490000)

Code: `src/game/effects.h` (layouts and class overview), `effects.cpp` (particle callbacks and
setters), `effects_weather.cpp` (rain/snow fade, fog). Verify: `tests/verify/verify_effects.cpp`.
Scenario: `tests/scenarios/effects.txt` (cannot check particles directly; relies on the verify
totals; rain/snow and fog only run where the map has weather / the fog is active). The rest of the
particle source (Create, Tick, Draw, other callbacks) is in `range_490.md` and `particle_*.cpp`.

## Facts

- 00678EC0 is the world's script particle source (`CreateParticleSource`, id 0x4A): creates the
  CParticleSource (00498400) on its first tick, then copies the position and calls
  `ScaleSize(size / baseSize)` every tick.
- CRainSnow: 004E3FC0 starts a fade (mode 1 rain with the `nature\rain_circle.wav` loop, 2 snow),
  004E41F0 is reached from the `SetWind` command via 00564190, 004E5680 is the per-frame entry
  (`weather_draw.cpp`, draws through forwarders 004E4240 rain / 004E4C80 snow).
- CVisibilityFog: cell map keyed `(y << 16) + x` of the map square; 004E2990 builds the 4x4 texture
  tile table at 007ABA58; 004E2A70..004E3630 are STLport hashtable/vector instantiations.

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 0048F670, 0048B570 | `CParticleSource::UpdateFalling`, `UpdateCloud` | both, state compared |
| 0048EC20, 0048E9E0 | `UpdateSmoke`, `EmitSmoke` | both, state compared |
| 00489190, 0048E7C0 | `UpdateDebris`, `UpdateOrbit` | both, state compared |
| 004892E0, 0048EE20 | `TickNuke` (element), `TickTendril` | both, state compared |
| 00488F00 | `TickMushroom` | both, state compared; camera-shake tick ours only |
| 00485480, 00485440, 00485450 | `ScaleSize`, `StopEmitting`, `StartEmitting` | ours on a scratch copy, object/flags compared |
| 00485360 | `Stop` | ours only: flags and attachment |
| 00488940 | `DivideVector` (`__fastcall`) | pure |
| 00488970 | `IsaacRandom::Range` | both, result bits and generator compared |
| 004E40B0, 004E4050 | `CRainSnow::UpdateFade`, `FinishFade` | original on a copy without the sound, ours on the real one |
| 004E41F0 | `CRainSnow::SetWind` | ours on a scratch copy, object compared |
| 004E1770 | `CVisibilityFog::ComputeBounds` | original on a scratch copy, 0x6C bytes compared |
| 004E1AB0 | `CVisibilityFog::UpdateCells` (slot 6) | ours only: count, threshold, dropped keys, result |
| 004E1C60 | `CVisibilityFog::Reveal` | ours only: exactly the visible squares marked, keys, count |

"Both, state compared": ours runs first; source, particle (0x64) / element (0x38) and the ISAAC
generator (007A9D40, 0x814 bytes) are captured and restored, the original runs, all plus the return
value are compared; the original's results stand.

## Floating point

Where the original stores a value to a float and reads it back (fog bounds, fog alpha test, smoke
emit velocities, the fog cell centre) the code goes through `StoreFloat` / `Load` (volatile); values
it keeps on the FPU stack across a store are repeated expressions (see the comments in
`effects.cpp`, `effects_weather.cpp`).

## Calls to originals

`Attach` 004849F0, camera shake 004113A0 (camera 0079B2A8), sound manager set-volume / stop
006B9100 / 006B94A0 (on *007AF784), `CEngineObject::World` 00455950. The fog's static dead-key
vector 007ABB58 uses the original constructor 004E2BA0, insert-overflow 004E2FA0 and atexit
006E727B; the cell map's insert 004E2E90 / erase 004E2F00 are forwarded, iteration and find use
`stlp::hash_map` (same order). None is Rebirth-patched.

## Original bugs kept on purpose

- `UpdateCloud` raises the alpha byte with `(step * 255 + alpha) << 24`, which wraps; particles die
  once the byte is at most 9.
- `Reveal` initialises a new cell by an overlapping `REP MOVSD` from its key, so the cell's padding
  holds key bytes; the template is filled the same way.
- `UpdateCells` calls `World()` and ignores the result.
- Random-range constants 0073C468 (~2^-32) and 0073CDA0 (~2^-33) carry a stray mantissa bit; used
  as is.

## Skipped and risks

- Rain / snow vertex building (forwarders 004E4240 / 004E4C80), fog renderer 004E1EB0, 004E3FC0
  (start fade), 004E3980 (random curve setup): Direct3D vertex building or setup.
- 0x00480000-0x00485000 apart from the particle source: error reporting, DirectX names,
  configuration, containers.
- `Reveal` / `UpdateCells` are checked only for consistency, not against the original.
