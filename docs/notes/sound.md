# Sound and music (006B0000-006D0000)

Files: `src/game/sound.h`, `src/game/sound_manager.cpp`, `src/game/sound_music.cpp`,
`tests/verify/verify_sound.cpp`, scenario `tests/scenarios/sound.txt`. Shared files changed:
`src/lib/mfc.h` / `mfc.cpp` (`CString::operator+=(const char*)` forwarder, 00700D4C) and
`tests/verify/verify.cpp` (the verify counters are now taken under an SRW lock, because the fader
wrapper runs on the sound manager's stream thread).

## What the range holds

| Range | Contents |
|---|---|
| 006B01F0-006B4230 | STLport / MFC instantiations of other areas (strings, vectors, CArray copies) |
| 006B4350-006B5BC0 | **CMusicSystem** (vtable 00744098, 0x58 bytes, class id 0xE, "CMusicSystem"): constructor 006B4350, Create 006B4710 (reads `music.reg`: Common\Credits, Common\MainMenu, per allod Briefing, Constructor, CalmOpen / CalmDungeon, Combat), Destroy 006B4690, SetZoneMode 006B4E30, Stop 006B4F70, PlayBriefing 006B4F90, PlayConstructor 006B5080, PlayCredits 006B5170, PlayMainMenu 006B51F0, PlayJingle 006B5270 (script PlayMusic), Update 006B5350 (slot 6), its theme CArray (006B56C0..006B5BC0, element 0x38) |
| 006B5CD0 | SoundSlot::End (the sequences' voice end in Tick) |
| 006B5D20-006B5FF0 | **SoundFader**, the volume envelope (0x1C): reset 006B5D20 / set volume 006B5DB0 (no callers, inlined everywhere), SetHold 006B5E80, Update 006B5EE0 (stream thread), Recalc 006B5FF0 |
| 006B6040-006B73E0 | **CSoundManager** (vtable 007440F0, 0x8C0 bytes, class id 0xE, "CSoundManager", one instance at *007AF784): constructor 006B6040 (`_AIL_startup`), destructor 006B65D0, Open 006B6A30 (DirectSound device, `_AIL_waveOutOpen`, MP3 stream 006BAAC0, the stream thread through AfxBeginThread 007022A0 with entry 006B6A10 -> 006BB910 (006B6A20 -> 006BBCB0 is the MIDI thread's entry), 15 voices), Close 006B6D80, stream-file list cleanup 006B7060, 006B7110 (Rebirth-patched), StopAll 006B71C0, PauseAll / ResumeAll 006B7320 / 006B7380, Reset 006B73E0 |
| 006B75E0 | the Miles end-of-sample callback: sets the voice's `finished` (Miles timer thread) |
| 006B7610-006B9AA0 | voices: refresh volumes 006B7610 (no callers), SetEffectsVolume 006B7740, SetSpeechVolume 006B78A0, SetMusicVolume 006B7A00, AddSoundFile 006B7AB0, FindPlaying 006B7C10, FindDeferred 006B7CB0, AllocateVoice 006B7D10 (steals the lowest priority voice, deferring a looping one), UpdatePositional 006B7FC0, Play2D 006B8250, Play3D 006B8600, file lookup 006B8960, LoadSample 006B89F0 (wave cache with a 8 MB / 60 s budget, ASI decompression), SetSoundVolume 006B9100, SetSoundPosition 006B92F0, StopSound 006B94A0, wave length 006B96E0, SetListener 006B9750, ambient emitters Add / Move / Remove 006B9950 / 006B9A30 / 006B9AA0 |
| 006B9B10 | **Tick** (every world frame, from 005641F0): end finished voices, restart deferred sounds, advance sequences, ambient emitters every 100 ms |
| 006BA140-006BA7C0 | 3D sound sequences: PlayNext 006BA140, Add 006BA2D0, Move 006BA400, Update 006BA5E0, Remove 006BA7C0 |
| 006BA950-006BB910 | the MP3 music stream (CSoundManager + 0x100): ASI read callback 006BA950, stream reset 006BA9C0, open 006BAAC0 ("ASI codec", "Input file types", 44.1 kHz), close 006BAC90, SetMusicVolumeRaw 006BAD40 (no callers), 006BAE20 (path check), CurrentStreamName 006BAEA0, FadeOut / FadeIn 006BAFA0 / 006BAFE0, RequestStream 006BB020, StopStream 006BB240, the buffer fill 006BB3A0 (Rebirth-patched; switches files at "Markers1" / "Markers2" positions), **the stream thread** 006BB910 (100 ms loop under the stream lock: fader update, refill the Miles double buffer) |
| 006BBA00-006BBDF0 | MIDI sequence: markers 006BBA00, empty 006BBAC0, sequence start 006BBB80, resume 006BBC70, **the MIDI thread** 006BBCB0, the wave cache map's operator[] 006BBDF0 |
| 006BBF30-006BDDB0 | STLport hash_map / list instantiations of the manager (wave cache +0x870, deferred sounds +0x884, cache list +0x854) |
| 006BDDD0-006BFxxx | terrain (006BE3A0 is in terrain.cpp) |
| 006C13A0-006C67E0 | resources / text (not sound) |
| 006C68B0-006CBE60 | the map's ambient sounds: day/night ambient waves (006C68B0, Rebirth-patched, "ambient\%s\%s_day_%i.wav"), terrain-material ambience around the listener (006CB390: "ambient\%s\%s\%s_%i.wav", "ambient\%s\%s_circle.wav", materials Grass ..., Ingos / Suslanger / Gipat / Dungeon), day time names 006CBC00 (morning / evening / night) |
| 006C8xxx | terrain (Terrain::HeightAt, terrain.cpp) |
| 006CF660-006CFEA0 | foot / blood / fire prints (not sound) |

### The threads

- **Stream thread** (006BB910, started in Open): every 100 ms, under the stream lock (+0x1B0):
  `musicFader.Update()` (006B5EE0), the stream sample's volume, and for every free Miles buffer the
  buffer fill 006BB3A0 (reads the MP3 through the ASI codec; at the requested switch position it
  opens `nextName`). It reads the clock (timeGetTime) only for the fader's hold time.
- **MIDI thread** (006BBCB0): restarts the MIDI sequence at its markers.
- **Miles' own timer thread** calls the end-of-sample callback 006B75E0, which sets a voice's
  `finished`. Nothing on these threads touches game state or the random generator.

### Music state machine (CMusicSystem::Update, every world frame)

State 1 (a zone, after SetZoneMode and the first world frame clearing `holding`):
- a jingle (PlayMusic) is playing: nothing until it ends; then the calm music is due 120..180 s later;
- the party fights (007AFC9C == 2): a combat track of the theme (random among several), requested
  at the next marker of the current file (RequestStream mode 3); when the fight ends the stream is
  stopped with a fade (mode 2) and the calm music is due 120..180 s later;
- otherwise, when nothing plays: a calm track (random) when `calmDue` passes, requested at the
  current position (mode 4), then "stop after it" (mode 1) once the stream thread took it.
States 2-5 play one file (briefing / constructor / main menu / credits). The calm delay and the
track choice draw from the game's random generator (007A9D40).

### Positional sound

`UpdatePositional`: silent beyond `farDistance` (squared compare), full volume inside
`nearDistance`, linear in between (100 - round(100 (d - near) / (far - near))); the camera height
(listener z) fades 2D "height-faded" sounds from 30 (full) to 70 (silent) and 3D sounds the same
way; the level is round(v * option volume * 0.0127) clamped to 0..127. The pan is the angle
between the listener's axis and the direction to the sound, folded to 0..90 degrees and scaled to
0..100, negative on one side, mirrored again when `panDirect` is 0 (option "swap stereo").

### AI hearing ("Delayed sound")

The hearing events are not produced by the sound code or a sound thread. They live in the AI brain's
perception memory (AIMemory at brain + 0xD0, area of `docs/notes/unit_ai.md`): a CArray of 0x18-byte
delayed sounds at memory + 0xA8 {position, countdown, unit, CString text}, filled by
`AIBrain::CallAllies` 005D5990 (005CBBA0: a position sound, 26 brain ticks, "Unit %i call for help
...") and `CUnitServer::ClaimHostility` 00552D00 (005CBBC0: a unit sound, 30 ticks, "Unit %i claims
that player %i are ..."), counted down by the brain's update 005CB700 -> 005CC9D0 on the world
thread. When a countdown expires 005CC9D0 prints "Delayed sound: <text>" (position sounds; the memory's
noticed point is set to it) or "Unit N hear delayed sound: <text>" (unit sounds, then 00552CF0) to the console
stream that `filter all` shows.

### What makes sound non-deterministic (for "Deterministic harness runs")

The sound code draws from the game's ISAAC generator (007A9D40, shared with AI, combat and loot)
on schedules driven by the wall clock, so with sound on, the generator's sequence shifts between
runs:
- Tick's ambient emitters (every 100 ms of GetTickCount, 0..4 draws per emitter that fires);
- the music's calm delay and track choices (timeGetTime);
- random sequences (PlayNext) and the random start position of Play2D / Play3D when
  CSoundManager + 0x8BC is set;
- the terrain ambience 006CB390 (several draws per call).
The stream / MIDI threads themselves draw nothing. Making sound deterministic means freezing those
clocks (the harness's virtual clock covers timeGetTime / GetTickCount) or running with sound off
(`enabled` 0: every one of these returns early).

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 006B7FC0 | `CSoundManager::UpdatePositional` | original on a scratch voice, ours on the real one; volume and pan compared |
| 006B5EE0 | `SoundFader::Update` (stream thread) | scratch copy; a hold expiring between the two clock reads is not compared |
| 006B5E80 / 006B5FF0 | `SoundFader::SetHold` / `Recalc` | scratch copy |
| 006B7CB0 | `CSoundManager::FindDeferred` | pure, side by side |
| 006B9A30 / 006B9AA0 | `MoveEmitter` / `RemoveEmitter` | original on a byte copy of the manager; the emitter compared |
| 006B69E0 | `SetReverb` | manager copy |
| 006BAFA0 / 006BAFE0 | `FadeOutStream` / `FadeInStream` | manager copy; fader mode / finished / step / hold compared |
| 006B7740 / 006B78A0 | `SetEffectsVolume` / `SetSpeechVolume` | side by side: original, voices and listener captured and restored (not `finished`), ours, compared (the Miles calls run twice with the same values) |
| 006B9750 | `SetListener` (every frame, CCamera::Update) | the same |
| 006B7A00 | `SetMusicVolume` | ours only; the level must equal the original Recalc's |
| 006B7C10 | `FindPlaying` | side by side unless the voice would be ended (then ours only, result null) |
| 006B9100 / 006B92F0 | `SetSoundVolume` / `SetSoundPosition` | side by side (voices + deferred record) unless a voice would be ended |
| 006BA400 / 006BA5E0 | `MoveSequence` / `UpdateSequence` | the same plus the sequence |
| 006B94A0 | `StopSound` | ours only; no playing voice keeps the handle, no record unless a live voice had it |
| 006BA7C0 | `RemoveSequence` | ours only; the sequence is free |
| 006B9950 | `AddEmitter` | ours only; the new emitter holds the arguments |
| 006BA2D0 | `AddSequence` | ours only; a started sequence is active under its id |
| 006BA140 | `SoundSequence::PlayNext` | ours only; result 0/1, index in range |
| 006B7320 / 006B7380 | `PauseAll` / `ResumeAll` | ours only (Miles calls only) |
| 006BB020 | `RequestStream` | ours only (shared with the stream thread); result and state ranges |
| 006BB240 | `StopStream` | ours only; state range |
| 006B5CD0 | `SoundSlot::End` | ours only; the voice is free |
| 006B9B10 | `CSoundManager::Tick` | ours only; sequence ids, emitter clock |
| 006B4E30 | `CMusicSystem::SetZoneMode` | ours only; state 1, holding, the theme matches |
| 006B4F70 / 006B4F90 / 006B5080 / 006B5170 / 006B51F0 | `Stop` / `PlayBriefing` / `PlayConstructor` / `PlayCredits` / `PlayMainMenu` | ours only; the state each sets |
| 006B5270 | `CMusicSystem::PlayJingle` | ours only; jingle name and flag |
| 006B5350 | `CMusicSystem::UpdateMusic` (slot 6) | ours only; returns 1, inCombat follows the fight state |

All names are provisional.

### Offline check

All the above except the ones that end voices through the wave cache were also run side by side
offline (scratch harness, not committed, like core.md's): game.exe mapped at 0x400000 in a child
process (the range reserved before the child runs), its imports resolved, Miles / timeGetTime /
GetTickCount / Sleep replaced through the import table with recording stubs, the MFC CString
helpers, operator new / delete, CStringArray::Copy, FatalError, the wave cache lookup, LoadSample,
Play2D and Play3D patched with stubs, then the original and the rewrite run on copies of random
managers / voices / faders / streams / music systems / deferred maps / sequences / emitters
(including the 24-, 53- and 64-bit FPU precision settings, the random generator compared too):
564000 comparisons, no mismatches. Deliberately broken copies of the rewrite (a truncating cast
for FISTP, a reordered division, a `>=` for `>` in the deferred priority, a changed modulus) were
caught.

### Floating point

`sound_manager.cpp` is compiled with `float_control(precise, on)`; values the original keeps on
the FPU are `double`, stores to float go through `Store`. The constants it loads at run time are
exact or read from the exe (pi at 007440D8, pi/2 at 007C8808 from the static initialiser 006BE310,
the fade step dword at 007440E4 / 007440E8). UpdatePositional keeps dy unrounded for its own square
(`FST` then `FMUL` the stored copy) and uses the stored float distance for the falloff and the pan;
`_CIacos` (006E7320) is called through a naked helper.

## Forwarders added (the standalone build must replace these)

In `sound_manager.cpp`: the wave cache operator[] 006BBDF0, the deferred map's begin 006BC3E0,
next bucket 006BCFB0, operator[] 006BCFF0 and erase 006BD060, the STLport string free 006B72E0,
CStringArray::Copy 006FEB61, `CurrentStreamName` 006BAEA0, the empty 006BBAC0, `Play3D` 006B8600,
`Play2D` 006B8250, `LoadSample` 006B89F0, `_CIacos` 006E7320; the ASI codec close and the
sequence chooser are called through small naked thunks (`CallWithOne`, `CallChooser`). Miles is
called through game.exe's import table (0073B750..0073B808). `mfc::CString::operator+=(const char*)`
00700D4C (`src/lib/mfc.cpp`).

## Original bugs kept on purpose

- `MoveEmitter` / `RemoveEmitter` accept index 16, one past the 16 emitters (it addresses the
  emitter tick and the first sequence).
- The per-handle controls take the first voice whose handle matches; free voices hold -1, so a
  handle of -1 changes a free voice (and calls Miles on its sample).
- Tick recomputes a restarted flat sound's level from its saved Miles level (0..127) as if it were
  the 0..100 base volume (it reads the voice's +0x10, not +0x14), so such sounds come back louder.
- `SetZoneMode` passes the error text to FatalError as the format string.
- `PlayJingle` and the calm music busy-wait (Sleep(0)) on the main thread until the stream thread
  takes the request; an empty theme list (`combat` with 0 names) divides by zero.
- `FadeOutStream` / `FadeInStream` and `SetMusicVolume` change the music fader without the stream
  lock.
- 006B7610 and 006BAD40 have no callers (dead code), 006B5D20 / 006B5DB0 likewise.

## Skipped

- The Miles / DirectSound side: Open / Close / Reset (006B6A30, 006B6D80, 006B73E0), the stream
  open / close / fill (006BAAC0, 006BAC90, 006BB3A0 Rebirth-patched), the stream and MIDI thread
  loops (006BB910, 006BBCB0), MIDI (006BBB80, 006BBC70), voice allocation and starting
  (006B7D10, 006B8250, 006B8600, 006B89F0: the wave cache with ASI decompression).
- CMusicSystem Create 006B4710 (music.reg parsing, CStringArray / CArray heavy), constructor /
  destructor / Serialize (006B4350, 006B44B0, 006B5770).
- The map's ambient sounds 006C68B0 (Rebirth-patched) and 006CB390 (terrain material ambience,
  2 KB): good next candidates together with the terrain owner.
- STLport hash_map / list instantiations 006BBF30-006BDDB0.

## Scenario

`sound.txt`: a new game (SetZoneMode, UpdateMusic and Tick every frame, SetListener from the
camera, the volume setters at load), `@PlayMusic("victory")` (PlayJingle, RequestStream /
StopStream), `@CreateFX(...,"nature\thunder\1.wav")` (a positional sound: Play3D,
UpdatePositional, SetSoundPosition / StopSound), then a fight with two goblins (the combat music
request, and the brain's "Delayed sound" / "hear delayed sound" lines: `expect elayed sound`).

## Play2D (006B8250)

| Address | Name | Verify |
|---|---|---|
| 006B8250 | `CSoundManager::Play2D` (RET 0x20) | ours only: -1, or a flat voice holding the arguments under the returned handle, or a deferred record for a looping sound |

Arguments: name, priority, volume 0..100, pan -100..100, loop, height fade, speech, and a value
handed to the wave loader (006B89F0). Sound off: -1. No voice (006B7D10 returns -1): a one-shot
sound gives -1; a looping one becomes a deferred record (0x48 bytes, the name in an STLport string
reserved for 8 bytes and assigned, flat, volume 0, a new handle from +0x268) in the map at +0x884
and its handle is returned. With a voice: AIL_init_sample, the fields, the level as the volume
setters compute it except that a NaN camera height keeps the full level (the original's first
compare is "below 70 or unordered"); a silent one-shot sound returns -1 there, leaving the voice
initialised but not playing. Then the wave (failure: -1), loop count 0 for a looping sound,
volume and pan, the end-of-sample callback, the reverb when on, a random start position below
+0x8BC ms + 1 from the game's random generator when set, AIL_start_sample; returns the voice's
handle (set by the wave loader). Tick's emitters now call this rewrite directly.
