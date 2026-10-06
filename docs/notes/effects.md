# Weather, fog, particle helpers and effect objects (004E1770-004E5000; particle pieces in 00480000-00490000; effect objects 00675B40-0067E170)

Code: `src/game/effects.h` (layouts and class overview), `effects.cpp` (particle callbacks and
setters), `effects_weather.cpp` (rain/snow fade, fog). Verify: `tests/verify/verify_effects.cpp`.
Scenario: `tests/scenarios/effects.txt` (cannot check particles directly; relies on the verify
totals; rain/snow and fog only run where the map has weather / the fog is active). The rest of the
particle source (Create, Tick, Draw, other callbacks) is in `particles.md` and `particle_*.cpp`.

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

## Effect objects (00675B40-0067E170)

Code: `world_effect.h` (class map, slots, layouts), `world_effect_internal.h`, `world_effect.cpp` (base,
sounds), `world_effect_visual.cpp` (light, particles, wall, lightning). Verify:
`tests/verify/verify_world_effect.cpp`. Scenarios: `effects.txt` (CreateParticleSource: CEffectParticle
Unpack / Start / Spawn / Tick / Pack / setters; DeleteParticleSource: Reset), `spells.txt` (casts create sounds,
sound sequences, lights, particles, the fire wall and lightning through 0067F170-0067F7C0), probably
`sandbox_actions.txt` (healing spell). Save / Load of effects runs only when a game with live effects is saved
or loaded (no scenario does).

- Classes (vtable / id): CEffectSound 007433EC / 0x48, CEffectSoundSeq 0074344C / 99, CEffectAmbientSound
  0074350C / 0x49, CEffectPointLight 0074356C / 0x4B, CEffectParticle 007435D0 / 0x4A, CEffectWall 00743630 /
  0x5A, CEffectLightning 00743690 / 0x4D; not rewritten: CEffectArrow (0x6C0 bytes, embeds a particle, a light
  and a sound; missiles), CEffectArrowFigure ("arrow01", "efar1"), CEffectShowVisibility, CEffectMoshka
  (birds, "bird00", "unmobi"), CEffectTornado ("nature\tornado\circle.wav"), up to 0067E000. The effect
  factory is 0067E170 (class id 0x48..0x5B). CSpellEffect (vtable 0074399C, 0067E510-0067F870) is in
  `items_spells.md`.
- Every effect: +0x94 start tick, +0x98 velocity, +0xA4 "moving" (NaN components count as zero), +0xA8
  replicated state (`DeltaField`, net_delta.h), +0x148 change stamp (007AFFC8, -1 after Reset). The server
  ticks (slot 6; 0 removes) and Packs (slot 22) into the DeltaField (SendFull / SendDelta 00675B40 / 00675B70);
  the client Unpacks (slot 21 via Receive 00675BA0) and Starts (slot 23: 0 creates the sound / light /
  particle source, else moves it). Load / Save (slots 9 / 10) use the save stream's slot 1 / 2 and +8 version
  (fields added at versions 0x43, 0x58, 0x5C, 0x67, 0x6D, 100). Slot 19 is a radius (100 by default), slot 20
  the position used for visibility.
- Particle types 0x2002, 0x2006, 0x200B-0x200F, 0x2018, 0x2019, 0x201B-0x2026, 0x2030-0x2033, 0x203F-0x2043 are
  one-shot: the tick removes them 60 ticks after the start (0x2019: 120) and their first spawn catches up all
  ticks since the start (other types at most 30). Types 0x2002 and 0x2030 leave a scorch mark (0056D4C0 on the
  world client 007AF5B8), 0x2030 also shakes the camera. A fire wall (0x2010) leaves a scorch mark.
- Calls through game addresses: sound system (*007AF784) 006B8600 / 006B92F0 / 006B94A0 / 006BA5E0 / 006BA7C0 /
  006B9950 / 006B9A30 / 006B9AA0; light manager (*007AF738) 006CA720 / 006CA5D0 / 006CA830, CLight 0068D010;
  particle sources 00498400 (*007AF754), 00485480, 004849F0, 00485360; bolts (*007AF758 slot 23) 004680D0,
  00467E10. None is on the Rebirth list.

### Rewritten (world_effect*.cpp)

| Address | Name | Verify |
|---|---|---|
| 00675B40, 00675B70, 00675BA0, 00675BE0, 00675D10, 00675D40 | CEffect SendFull, SendDelta, Receive, ResetBase, CreateEffect, DestroyEffect | ours only (Pack is checked itself) |
| 00675CA0, 00675D50 | CEffect::MarkChanged, SetVelocity | ours, object restored, original, bytes compared |
| 00675FA0, 006768F0, 00676050, 00676930, 00677690, 00677F70 | Position, radius getters (slot 19) | pure |
| 00676200, 00676B10, 00677870, 006780C0, 00678A90, 00679AF0, 0067A1B0 | Reset (slot 18) of sound, sequence, ambient, light, particle, wall, lightning | ours only: the fields Reset sets |
| 006762F0, 00677250 | Tick of sound / sequence | ours, restore, original (moving a playing sound twice is harmless) |
| 006781B0, 0067A8F0 | CEffectPointLight::Tick, CEffectLightning::Tick | ours only |
| 006795C0 | CEffectParticle::Tick (also the wall's) | ours, restore, original: object, the source's position bytes, result |
| 006763A0, 00677950, 006782D0, 00678CF0, 0067A370 | Start (slot 23) | ours only (light: flag 0x400 matches the range's sign) |
| 00678EC0, 00679350 | CEffectParticle::Spawn, Attach | ours only |
| 00676430, 006779D0, 00678520, 00678D30, 00679C70, 0067A490 | Unpack (slot 21) | ours only (consume the reader, start things): result 0 or 1 |
| 00676550, 00677180, 00677B20, 00678610, 006791A0, 00679DB0, 0067A600 | Pack (slot 22) | ours into the field, streams saved, restored, original, compared (skipped when ours grew a buffer) |
| 006766D0, 00677430, 00677C60, 00678740, 006794E0, 00679ED0, 0067A7F0 | SaveState (slot 10) | original against a recording stand-in, ours through a forwarding recorder: bytes compared |
| 006765F0, 00678690, 006793E0, 00679EA0, 0067A770 | LoadState (slot 9) | original on a scratch copy against a stand-in that reads nothing, ours through a recorder: (offset, size) of every read compared |
| 006772D0, 00677BF0 | LoadState of sequence / ambient | ours only (the string-array readers use more of the stream) |
| 00678BA0, 00679280, 006792B0, 006792E0, 00679310, 00679C30, 0067A890, 0067A8C0, 0067A6B0 | particle Position and setters (SetAttachedTo, SetSize, SetColors, SetTarget, SetDirection, SetObjects, SetFlag, SetEnds) | ours, restore, original, bytes compared |
| 0066C4F0, 00677EC0 = 0066A300, 00675F70 | CMemoryStream::ConstructQueueStream, DeltaField::Construct (two identical copies), DeltaField::Empty | side by side on copies |
| 00675DD0, 00676760, 00677D30, 006787B0, 006797E0, 00679FC0 | Construct of sound, sequence, light, particle, wall, lightning (`world_effect_lifetime.cpp`) | original on scratch memory seeded with the real bytes, ours on the real object; whole object compared |
| 006760A0, 00676980, 00677FC0, 00678990, 006799E0, 0067A0B0; 00676080, 00676960, 00677FA0, 00678970, 006799C0, 0067A090 | Destruct and slot 0 (deleting destructor) of the same | ours only, in the slot 0 wrappers (bit 0 cleared, freed by the wrapper): CEngineObject vtable, no owner, CStream vtables in the field, handle / pointer gone |

Constructors / destructors (`world_effect_lifetime.cpp`; scenarios spells, spells_cast, effects, objects_more): the
lightning constructor does not Reset (only bolt, width 0, flag 0); the wall runs the particle's Reset under the particle
vtable before its own (both in the destructor too); sound and sequence destructors Reset twice. The bodies are called
only by slot 0, so their own wrappers stay at 0 calls.

x87: ticks and starts follow the original's operation order; small naked helpers repeat the original
instructions where the compiler could not be trusted: `StepFraction` (particle speed / distance ratio),
`SetLightRange` (range, range^2 with FST, 1 / range^2 from the unrounded square), `WallScorchShape` (FPATAN and
the length sum), `RandomAngle` (FILD qword of the random value: a dword FILD plus 2^32 would round at 24-bit
precision); floats the original copies with FLD / FSTP go through `CopyThroughFpu`; scalar float bits passed to
callees are read with `Bits(const float&)`.

Original quirks kept: CEffectSound / SoundSeq Tick move the sound when the handle is not 0, so a stopped (-1)
sound is moved too; Sound Reset stops a sound only when it loops; CEffectAmbientSound::Unpack reads the name
count into the low byte of the stack slot of its `field` argument; CEffectParticle::Attach reads the source's
position through a null source when there is none.

Skipped: the ambient sound's constructor / destructor (00677510, 006776E0; no scenario creates one), the base's
destructor 00675FB0 / 00676910 (only reachable through the abstract base's vtable) and the factory 0067E170; CEffectSoundSeq::Start 00676C00 and Unpack 00676FD0 (CStringArray
temporaries, random picks 00676DD0..00676F90 passed as callbacks); CEffectArrow / ArrowFigure / ShowVisibility /
Moshka / Tornado (0067AAB0-0067E000); dead code 00675CC0 (UpdateMoving), 0067A2C0 (UpdateEnds); STLport
instantiations.

## Missiles (world_effect_missile.cpp, A-H7)

CEffectMissile (abstract, vtable 00743754, 0x158): +0x14C arrived, +0x150 target id, +0x154 "from caster".
CEffectArrow (0x4E, vtable 007436F0, 0x6C0) embeds a CEffectParticle (+0x158), CEffectPointLight (+0x2D8) and
CEffectSound (+0x43C), then the sound name CString +0x6A0, light colour / range, particle type / size, caster id
+0x6BC. Steer (0067AAB0, shared with class 0x5B, which doubles the speed 0.6667) aims at the target's
+0x1C position with z + extent.z / 2 + pointOffset.z; arrived when the planar distance is below one step (or NaN).
On a client the missile is kept 1 above the world's slot 28 height. Tick moves, and on a client moves the parts
(slot 24, Start(1) each). Start copies the settings into the parts (light velocity (0.0001, 0, 0), flags 0x840,
sound radius 30, looping) and starts them. Scenario spells (arrow, acid ray).

| Address | Name | Verify |
|---|---|---|
| 0067AAB0, 0067AC70 | Steer, SetTarget | ours, restore, original (velocity CheckNear) / bytes |
| 0067AC90, 0067AD10, 0067B450, 0067B4E0 | Tick, Reset, MoveParts, Start | ours only: position moved / fields / parts at the arrow / parts' settings |
| 0067B690, 0067B800, 0067B9A0, 0067B8E0 | Unpack, Pack, SaveState, LoadState | as the other effects (Load ours only: CString reader) |
| 0067AE30, 0067B1C0, 0067B1A0 | Construct, Destruct, DeletingDestruct | original on seeded scratch, all bytes / ours only |

Kept: Load does not recompute the moving flag; Unpack does not either; Start copies the sound name unbounded.
Not rewritten: 0067AA00 (the missile's Reset) and 0067B390 / 0067B430 (its destructor), reachable only through the
abstract vtable; 0067B050 (an out-of-line CEffect constructor, called only by the arrow constructor, now unused).
CWorld gained slots 28 (RawSurfaceHeightAt) and 40 (ObjectWithId); CMapObject +0x144 extent.

CEffectShowVisibility (0x5E, vtable 0074381C, 0x170; world_effect_visibility.cpp): +0x14C observer, +0x150 seer
(SetUnits 0067C640, from vision fog 0x17 with caster and target), +0x154 counted fog object (client: made by the fog
manager *007AF75C, 004E28D0), +0x158 FogGrid (cells / rows allocated by the constructor). The server's Tick follows
the seer and recomputes the grid (005CD210) and marks the effect changed; Pack / Unpack send the grid through a stack
memory stream (FogGrid Write / Read 005C8CC0 / 005C8BF0); Start reveals it in the fog (CVisibilityFog::Reveal). Reset
does not reset the base. Verify: SetUnits side by side; Pack, Save, Load as the other effects; constructor against
the original with the two grid blocks compared by shape; the rest ours only. Script casts have no caster, so only the
new scenario spells_hero.txt (the hero casts vision fog and arrow from the battle screen) reaches it; not run yet.
Not rewritten: 0067C5D0 (FogGrid free, unwind only), 0067C450 / 0067C460 (class name / id).

The effect factory 0067E170 (EffectNewObject, the effect manager's slot 14, RET 4; world_effect_lifetime.cpp): operator new
of the class size and its constructor body, type_ = class id; the ambient sound, class 0x5B, Moshka (100) and Tornado
(0x65) constructors through their addresses. Verify: ours only, the object's slot 12 and type_ equal the request.

CEffectSoundSeq Start 00676C00 / Unpack 00676FD0 (world_effect_sequence.cpp): the first name (when not empty) and
the others go into a stack CStringArray handed to CSoundManager::AddSequence with a chooser for their count (ten
small ISAAC pickers, 00676DD0..00676F90, now ours); an update moves the sequence through SetSoundPosition with the
sequence id (kept). The chooser takes the number of sounds started so far in EDX: the rewritten
SoundSequence::PlayNext (sound_manager.cpp) left EDX undefined, so a sequence with a first name could skip it;
it now passes it (C++ fastcall, the naked CallChooser removed). Verify: ours only (Start: an id or -1; Unpack
result).

Next candidates (A-H7 left): class 0x5B (CEffectArrowFigure, vtable 007437B8, 0067BAA0..0067C290; bow shots from
DeliverAttack 0054F450, no scenario shoots a bow yet); the Moshka (100, 0067C950..0067D660; the client world keeps one at
+0x1B0 and draws it every frame through 0067CEF0, which only runs with a followed object); Tornado (0x65, 0067D670..0067E070,
from the server weather 0058CCD0); CPlayerServer sync (0066D100..0066F080, forwarded by world_sync.cpp, live every tick).

A-H8: spells_hero showed no missile / visibility calls because the hero was blocked (`BlockUnit`, flag 0x800000):
the server drops a blocked unit's orders, casts included (0066FFF0), so only the new game's own three casts
(point lights) ran; every newgame scenario shows those 3 CastSpell. The scenario now waits for the intro
script's block, lifts it and waits for the arrow to hurt the second wolf. ui_battle_spells blocks the hero the
same way (it only checks that UseOnUnit is reached).
Second central run (A-H8): the hero's two casts reached the server (CastSpell 5) but made three unit-attached
particles, a sound and a light and no CEffectArrow / ShowVisibility, which looks like a healing-type spell rather than
the two given ones: the column probably holds a spell the hero already had, shifting the buttons. The scenario now
gives "arrow{e2}" (damage, as spells.txt), uses all three buttons of a three-spell column and waits for either wolf to
lose hit points. Note: CSpellEffect::Update calls Start / UpdateMissile directly, so their wrappers never count, and the
factory constructs effects directly (constructor wrappers stay at 0); Tick / Pack / Unpack go through the vtables.

A-H9: spells_hero fixed. GiveUnitSpell never reached the hero's column: CUnit::AddSpell (00531820) fills the unit's own
slots, but a party unit's spells come from its player's party record (GetSpell 005319A0), so the column showed the
hero's own healing (0x18) and 0x0C. The scenario now gives "spell container[...]" items (GiveItem) and puts them on in
the camp's magic page (page 1; CanEquip 006100E0 wants the school skill, hence GiveSkill). "Or" is a Rebirth addon
function (vanilla's "Parameter of type float expected"). hudstate logs the column unit's spell slots (prototype index).
Calls in one run (both installs): CEffectArrow Reset 4, Tick 8, Unpack 1 (Pack none); CEffectMissile::SetTarget 1;
CEffectShowVisibility SetUnits 1, Reset 4, Tick 105, Pack 51, Unpack 52. ui_battle_spells also uses GiveUnitSpell, so
its column holds only the hero's two spells.
