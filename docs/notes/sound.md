# Sound and music (006B0000-006D0000)

Files: `src/game/sound.h` (layouts, static_asserts), `sound_manager.cpp`, `sound_music.cpp`;
`tests/verify/verify_sound.cpp` (counters taken under an SRW lock: the fader wrapper runs on the
stream thread); scenario `tests/scenarios/sound.txt`.

## Map of the range

| Range | Contents |
|---|---|
| 006B4350-006B5BC0 | CMusicSystem (vtable 00744098): Create 006B4710 (slot 2, reads `music.reg`), Update 006B5350 (slot 6), theme CArray 006B56C0..006B5BC0 |
| 006B5CD0-006B5FF0 | `SoundSlot::End`; SoundFader (0x1C). 006B5D20 / 006B5DB0 have no callers (inlined everywhere) |
| 006B6040-006B73E0 | CSoundManager (vtable 007440F0, one instance at `*007AF784`): Open 006B6A30 (DirectSound, `_AIL_waveOutOpen`, MP3 stream 006BAAC0, stream thread via AfxBeginThread 007022A0 entry 006B6A10 -> 006BB910; 006B6A20 -> 006BBCB0 is the MIDI thread entry; 15 voices); 006B7110 is Rebirth-patched |
| 006B75E0 | Miles end-of-sample callback (Miles timer thread): sets the voice's `finished` |
| 006B7610-006B9AA0 | voices, Play2D/3D, LoadSample, AllocateVoice 006B7D10 (steals the lowest priority voice, deferring a looping one), emitters. 006B7610 has no callers |
| 006B9B10 | Tick (every world frame, from 005641F0) |
| 006BA140-006BA7C0 | 3D sound sequences |
| 006BA950-006BB910 | MP3 stream (CSoundManager + 0x100), buffer fill 006BB3A0 (Rebirth-patched; switches files at "Markers1"/"Markers2"), stream thread 006BB910. 006BAD40 has no callers |
| 006BBA00-006BBDF0 | MIDI sequence, MIDI thread 006BBCB0 |
| 006BBF30-006BDDB0 | STLport hash_map / list instantiations (wave cache +0x870, deferred sounds +0x884, cache list +0x854) |
| 006C68B0-006CBE60 | map ambient sounds: day/night waves 006C68B0 (Rebirth-patched), terrain-material ambience 006CB390, day time names 006CBC00 |

Everything else in 006B0000-006D0000 (006B01F0-006B4230, 006BDDD0-006BFxxx, 006C13A0-006C67E0,
006C8xxx, 006CF660-006CFEA0) is not sound.

### Threads

Stream thread (006BB910): every 100 ms under the stream lock (+0x1B0): `musicFader.Update()`, the
stream sample's volume, and for every free Miles buffer the fill 006BB3A0 (opens `nextName` at the
requested switch position); reads timeGetTime only for the fader's hold time. The MIDI thread
restarts the sequence at its markers. Neither they nor Miles' callback touch game state or the
random generator.

The music state machine (`CMusicSystem::Update`, every world frame) and the positional formula (`UpdatePositional`) are summarised in
`sound_music.cpp` / `sound_manager.cpp`. `RequestStream` modes: 1 stop after the current file, 2 stop with a fade (a fight ended), 3 switch at the
next marker of the current file (combat), 4 switch at the current position (calm); the calm music is due 120..180 s after a jingle or fight. The
pan is mirrored when `panDirect` is 0 (option "swap stereo").

### AI hearing ("Delayed sound")

Not sound code. The AI brain's perception memory (brain + 0xD0, unit_ai.md) holds a CArray of
0x18-byte delayed sounds at memory + 0xA8 {position, countdown, unit, CString text}, filled by
`AIBrain::CallAllies` 005D5990 (005CBBA0: position sound, 26 brain ticks, "Unit %i call for help
...") and `CUnitServer::ClaimHostility` 00552D00 (005CBBC0: unit sound, 30 ticks, "Unit %i claims
that player %i are ..."), counted down by the brain update 005CB700 -> 005CC9D0 on the world thread.
On expiry 005CC9D0 prints "Delayed sound: <text>" (position sounds; the memory's noticed point is
set to it) or "Unit N hear delayed sound: <text>" (unit sounds, then 00552CF0) to the console
stream that `filter all` shows.

### Non-determinism (deterministic harness runs)

Sound draws from the shared ISAAC generator (007A9D40) on wall-clock schedules: Tick's ambient
emitters (every 100 ms of GetTickCount, 0..4 draws per firing emitter), the music's calm delay and
track choices (timeGetTime), random sequences (PlayNext) and the random start position of
Play2D/Play3D when CSoundManager + 0x8BC is set, terrain ambience 006CB390. Freeze the clocks (the
harness's virtual clock covers timeGetTime / GetTickCount) or run with sound off (`enabled` 0: all of
these return early).

## Rewritten (names provisional)

All in `sound_manager.cpp` except the CMusicSystem rows (`sound_music.cpp`). "Scratch" = original on a
scratch copy of the object, ours on the real one.

| Address | Name | Verify |
|---|---|---|
| 006B7FC0 | `CSoundManager::UpdatePositional` | scratch voice; volume and pan |
| 006B5EE0 / 006B5E80 / 006B5FF0 | `SoundFader::Update` / `SetHold` / `Recalc` | scratch; a hold expiring between the two clock reads not compared |
| 006B7CB0 | `FindDeferred` | pure, side by side |
| 006B9A30 / 006B9AA0, 006B69E0, 006BAFA0 / 006BAFE0 | `MoveEmitter` / `RemoveEmitter`, `SetReverb`, `FadeOutStream` / `FadeInStream` | manager byte copy; emitter / fader compared |
| 006B7740 / 006B78A0 / 006B9750 | `SetEffectsVolume` / `SetSpeechVolume` / `SetListener` | side by side with voices and listener captured and restored (not `finished`) |
| 006B7C10, 006B9100 / 006B92F0, 006BA400 / 006BA5E0 | `FindPlaying`, `SetSoundVolume` / `SetSoundPosition`, `MoveSequence` / `UpdateSequence` | side by side (voices, deferred record, sequence) unless a voice would be ended (then ours only) |
| 006B7A00, 006B94A0, 006BA7C0, 006B9950, 006BA2D0, 006BA140, 006B7320 / 006B7380, 006B5CD0 | `SetMusicVolume`, `StopSound`, `RemoveSequence`, `AddEmitter`, `AddSequence`, `SoundSequence::PlayNext`, `PauseAll` / `ResumeAll`, `SoundSlot::End` | ours only; post-state invariants |
| 006BB020 / 006BB240 | `RequestStream` (shared with the stream thread) / `StopStream` | ours only; result and state ranges |
| 006B9B10 | `Tick` | ours only; sequence ids, emitter clock |
| 006B8250 / 006B8600 | `Play2D` (RET 0x20) / `Play3D` (RET 0x1C) | ours only: -1, or a voice holding the arguments under the returned handle, or a deferred record |
| 006B89F0 | `LoadSample` (RET 0x10) | ours only: 0, or 1 with the voice playing under the newest handle, a cache node of that name (unless an MP3 was cached) |
| 006B4710 | `CMusicSystem::LoadThemes` (named so: Create is a CEngineObject virtual, rule 4) | original into a scratch music system after ours succeeded; credits, main menu, every theme compared |
| 006B4350, 006B44B0, 006B4490, 006B4690, 006B4470 / 006B4480, 006B4E10 / 006B4E20 | `CMusicSystem` Construct, DestructBody, DeletingDestruct, `ResetThemes` (slot 3; strings become "none" 007846F0, theme +0x3C kept), class name / id 0xE, `FadeOut` / `FadeIn` (the manager's stream; ECX unused) | name / id against the original; the rest ours only (`verify_sound_lifetime.cpp`; CMusicSystem now derives from CEngineObject) |
| 006B6290 / 006B62A0, 006BAEA0 | CSoundManager class name / id 0xE, `CurrentStreamName` (`sound_manager_lifetime.cpp`) | against the original (CurrentStreamName 7 stolen bytes) |
| 006B6D70, 006B62B0, 006B7060, 006B7AB0, 006B8960, 006BAE20, 006BA950 | `CreateBody` (slot 2: Reset, 1), `DeletingDestruct`, `ClearWaveSources`, `AddWaveSource`, `LoadWaveFile`, `SetPath`, `SoundStreamRead` (the stream file's read callback, stream thread) | ours only (`verify_sound_lifetime.cpp`); startup 00456440 calls SetPath and AddWaveSource |
| 006B71C0, 006B96E0 | `StopAll(stopStream)` (originals_ui.h's `LoadingStepOriginal` is this), `WaveLength` | StopAll ours only (no voice plays, handle 0, no deferred); WaveLength side by side (7 bytes) |
| 006B4E30, 006B4F70 / 006B4F90 / 006B5080 / 006B5170 / 006B51F0 / 006B5270, 006B5350 | `SetZoneMode`, `Stop` / `PlayBriefing` / `PlayConstructor` / `PlayCredits` / `PlayMainMenu` / `PlayJingle`, `UpdateMusic` (slot 6) | ours only; resulting state (returns 1, inCombat follows the fight state) |
| 006B7D10 | `AllocateVoice` (A-H17, sound_manager.cpp; Play2D / Play3D call it through its address): first free voice, else steals the lowest priority below `priority` (a looping sound deferred with its sample position) | ours only: -1 only when all voices play at >= priority; the voice is free; a stolen loop's handle is deferred |
| 006B6040, 006B65D0, 006BA9C0 | `Construct`, `DestructBody`, `ResetStream` (A-H15, sound_manager_lifetime.cpp; 006BA9C0 runs on the manager's +0x100, a `__fastcall` shim) | ours only (vtables, empty lists, fader volume 100 / level 127, switch mode 4) |
| 006B6A30, 006B6D80, 006B73E0 | `Open` (RET 0x14: window, reverb, DirectSound device index, rate, unused), `Close` (slot 3), `Reset` (A-H18, sound_manager_lifetime.cpp). +0x1C8 / +0x244 the stream / MIDI threads (`mfc::CWinThread`), +0x248 their quit flag. The sound option 0079BCC0 = 2 opens the wave mapper. Close frees each cached wave's own name where the original's RemoveHead (006BC6F0) copies it out and frees the copy (node-pool order only) | ours only: Open 1 with 15 free voices and a driver, or 0 in the reset state; Close / Reset the reset state (every scenario: startup, exit) |

Non-obvious: Play2D/Play3D with no voice (AllocateVoice -1) return -1 for a one-shot and make a
deferred record (0x48 bytes) for a looping sound; a NaN camera height keeps the full level (the
original's first compare is "below 70 or unordered"); a silent one-shot returns -1 leaving the voice
initialised but not playing; Play3D's deferred record leaves volume, base volume, pan and length as
operator new returned them. LoadSample's 4th argument means "the file is an MP3" (Play2D passes its
last argument, Play3D 0); a cached MP3 loads nothing; the cache trim keeps waves used within 60 s
while cache + new wave stays under 8 MB, otherwise frees a node with no uses. LoadThemes calls the
registry through forwarders (00694520 / 00694540 / 006945A0 / 00694880 / 00694D20 / 006958E0) so the
registry's wrappers stay in the path; the theme array grows with SetSize(n+1, -1) 006B59A0 and the
assignment 006B4D40.

Floating point: `sound_manager.cpp` uses `float_control(precise, on)` and `Store` (see its header).
Constants read from the exe: pi 007440D8, pi/2 007C8808 (set by static init 006BE310), fade step
dwords 007440E4/007440E8. `UpdatePositional` keeps dy unrounded for its own square (`FST` then
`FMUL` the stored copy) and uses the stored float distance for falloff and pan; `_CIacos` 006E7320
goes through a naked helper. Miles is called through game.exe's import table (0073B750..0073B808).

## Original bugs kept on purpose

- `MoveEmitter`/`RemoveEmitter` accept index 16, one past the 16 emitters (it addresses the emitter
  tick and the first sequence).
- Per-handle controls take the first voice whose handle matches; free voices hold -1, so handle -1
  changes a free voice (and calls Miles on its sample).
- Tick recomputes a restarted flat sound's level from its saved Miles level (0..127) as if it were the
  0..100 base volume (reads the voice's +0x10, not +0x14): such sounds come back louder.
- `SetZoneMode` passes the error text to FatalError as the format string.
- `PlayJingle` and the calm music busy-wait (Sleep(0)) on the main thread until the stream thread
  takes the request; an empty theme list (`combat` with 0 names) divides by zero.
- `FadeOutStream`/`FadeInStream`/`SetMusicVolume` change the music fader without the stream lock.
- `Open` keeps the GUID pointers DirectSoundEnumerateA's callback saw after the enumeration returned.

## Not rewritten

- Miles/DirectSound side: stream open / close / fill (006BAAC0, 006BAC90, 006BB3A0 Rebirth-patched), stream
  and MIDI thread loops (006BB910, 006BBCB0), MIDI (006BBB80, 006BBC70), the DirectSoundEnumerateA callback
  006B68A0 (Open passes it).
- Library (A-H13, config/library.csv): the CArray<MusicTheme> members 006B56C0-006B5BC0 and MusicTheme's operator= /
  destructor 006B4D40 / 006B4D90; the cache list and wave cache members 006BBDF0-006BE240; unwind-only destructors 006B62D0 /
  006B6350 / 006BE300 and the name array element constructors 006B63D0 / 006B63F0. 006BBA00 is dead (no references).
- Live game code left in 006B4000-006C0000: terrain sector pieces around 006BF4A0 (`models.md`; Construct 006BF5F0 and Project
  006BFC80 are done).
- A-H15 layout: +0x168 / +0x178 and the MIDI part's +0x1E4 / +0x1F4 are 32-byte aligned arrays (`lib::AlignedArray`;
  `markers` is the first); the MIDI part (+0x1CC) has two CStrings (+0x1DC / +0x1E0), a fader (+0x20C) and a lock (+0x22C).
  The constructor's hash maps get zero functor bytes where the original copies three uninitialised stack bytes (unused).
  The hash map members (006BBF90 / 006BC2B0 / 006BCE00 / 006BD100 / 006BC4D0 / 006BC510), DestructElements 006BE210 and
  the element constructors / destructors 006B63D0..006B6400 stay original (library); 006714A0 runs through its address
  (`MappedResourceDestroyOriginal`, originals_engine.h). CSoundManager now derives from CEngineObject; +0xE4 is a
  `CTypedPtrList<CMappedResource>` (the wave sources).
- Ambient sounds 006C68B0 (Rebirth-patched) and 006CB390 (terrain material ambience, 2 KB): good next
  candidates together with the terrain owner.
- STLport hash_map / list instantiations 006BBF30-006BDDB0.

## Scenario

`sound.txt`: new game (SetZoneMode, UpdateMusic and Tick every frame, SetListener, volume setters at
load), `@PlayMusic("victory")` (PlayJingle, RequestStream/StopStream), `@CreateFX(...,"nature\thunder\1.wav")`
(Play3D, UpdatePositional, SetSoundPosition/StopSound), then a fight with two goblins (combat music
request and the brain's "Delayed sound" / "hear delayed sound" lines: `expect elayed sound`).
