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
| 006B75E0 | Miles end-of-sample callback (Miles timer thread): sets the voice's `finished`. Rewritten without a verify wrapper: a check (it logs) inside a Miles callback froze every vanilla run |
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
Play2D/Play3D when CSoundManager + 0x8BC is set, terrain ambience 006CB390. All of these run on the
main thread, so the harness's virtual clock (tests/verify/harness_clock.cpp, ticket SV1) covers them.
Two more things made runs part and are handled there too: sample ends (Miles' end-of-sample callback
006B75E0 on its timer thread sets the slot's `finished`, which ends a unit's speech flag and frees
voices) are fired from the main loop once the sample's length has passed on the virtual clock, and
the music's WaitAndEndAfter (PlayJingle / calm music) busy-waits on the main thread for the stream
thread, which other threads let through by reading the virtual time plus the real time since the
main thread last moved it. The sound threads themselves (stream, fader, Miles' timer) draw no random
numbers and touch no game state.

## Rewritten (names provisional)

Every function carries its address in its source comment (grep it); the per-function table that used to be here is gone. All in
`sound_manager.cpp` except the CMusicSystem functions (`sound_music.cpp`) and the lifetime ones (`sound_manager_lifetime.cpp`).
- Voices and positions: `UpdatePositional` 006B7FC0, `SoundFader` Update / SetHold / Recalc 006B5EE0 / 006B5E80 / 006B5FF0, FindDeferred
  006B7CB0, MoveEmitter / RemoveEmitter 006B9A30 / 006B9AA0, SetReverb 006B69E0, FadeOutStream / FadeInStream 006BAFA0 / 006BAFE0,
  SetEffectsVolume / SetSpeechVolume / SetListener 006B7740 / 006B78A0 / 006B9750, FindPlaying 006B7C10, SetSoundVolume /
  SetSoundPosition 006B9100 / 006B92F0, MoveSequence / UpdateSequence 006BA400 / 006BA5E0, SetMusicVolume 006B7A00, StopSound 006B94A0,
  RemoveSequence, AddEmitter 006B9950, AddSequence 006BA2D0, `SoundSequence::PlayNext` 006BA140, PauseAll / ResumeAll 006B7320 / 006B7380,
  `SoundSlot::End` 006B5CD0, RequestStream 006BB020 (shared with the stream thread) / StopStream 006BB240, Tick 006B9B10, Play2D 006B8250
  (RET 0x20) / Play3D 006B8600 (RET 0x1C), LoadSample 006B89F0 (RET 0x10), AllocateVoice 006B7D10 (first free voice, else steals the
  lowest priority below `priority`; a looping sound is deferred with its sample position; -1 only when all voices play at >= priority),
  StopAll(stopStream) 006B71C0 (originals_ui.h's `LoadingStepOriginal` is this), WaveLength 006B96E0 (7 bytes).
- `CMusicSystem`: `LoadThemes` 006B4710 (named so: Create is a CEngineObject virtual, rule 4), Construct 006B4350, DestructBody 006B44B0,
  DeletingDestruct 006B4490, `ResetThemes` 006B4690 (slot 3; strings become "none" 007846F0, theme +0x3C kept), class name / id 0xE
  006B4470 / 006B4480, FadeOut / FadeIn 006B4E10 / 006B4E20 (the manager's stream; ECX unused), SetZoneMode 006B4E30, Stop / PlayBriefing /
  PlayConstructor / PlayCredits / PlayMainMenu / PlayJingle 006B4F70-006B5270, UpdateMusic 006B5350 (slot 6; returns 1, inCombat follows
  the fight state). CMusicSystem and CSoundManager derive from CEngineObject.
- `CSoundManager` lifetime: class name / id 0xE 006B6290 / 006B62A0, CurrentStreamName 006BAEA0, CreateBody 006B6D70 (slot 2: Reset, 1),
  DeletingDestruct 006B62B0, ClearWaveSources, AddWaveSource, LoadWaveFile, SetPath 006BAE20 (startup 00456440 calls SetPath and
  AddWaveSource), SoundStreamRead 006BA950 (the stream file's read callback, stream thread), Construct 006B6040, DestructBody 006B65D0,
  ResetStream 006BA9C0 (runs on the manager's +0x100, a `__fastcall` shim), `Open` 006B6A30 (RET 0x14: window, reverb, DirectSound device
  index, rate, unused; +0x1C8 / +0x244 the stream / MIDI threads (`mfc::CWinThread`), +0x248 their quit flag; the sound option 0079BCC0 = 2
  opens the wave mapper), `Close` 006B6D80 (slot 3; frees each cached wave's own name where the original's RemoveHead 006BC6F0 copies it
  out and frees the copy, node-pool order only), `Reset` 006B73E0.

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
