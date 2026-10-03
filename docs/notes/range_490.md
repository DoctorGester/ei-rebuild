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
| 0049B140-0049E790 | 0049B140 (17 callers), 0049B4B0 / 0049B780 byte-array builders; 0049BEA0 (1563, called once per zone load from the map loader 00481660): vertex welding through STLport hash_map / vectors (0049BB70, 0049C*, 0049D*, 0049E0D0..0049E5F0 are its instantiations). |
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
- Not done (large): 0049BEA0 vertex welding (zone load, STLport hash containers), 00498940 depth
  sort (needs STLport's introsort reproduced exactly), 004A7630 / 004A7080 class templates
  (exception-state heavy, new game), the dead 3DS readers.
