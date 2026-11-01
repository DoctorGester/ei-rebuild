# Sound and music (006B0000-006D0000)

Files: `src/game/sound.h` (layouts, static_asserts), `sound_manager.cpp`, `sound_music.cpp`;
`tests/verify/verify_sound.cpp` (counters taken under an SRW lock: the fader wrapper runs on the
stream thread); scenario `tests/scenarios/sound.txt`.

The image plays through our own sound output (`src/audio/audio.h`; docs/notes/audio.md), not Miles: voices, the music's double-buffered stream and the MP3 decoders. A voice's end is timed by its
wave's length on the game's clock and delivered by the main loop after the frame's time, as the harness delivered
Miles' callbacks in game.exe's runs. The map below is game.exe's (Miles, DirectSound, the unused MIDI part); the
image has no MIDI part and no DirectSound device choice (the game always asked for the default device).

## Map of the range

| Range | Contents |
|---|---|
| 006B4350-006B5BC0 | MusicSystem (vtable 00744098): Create 006B4710 (slot 2, reads `music.reg`), Update 006B5350 (slot 6), theme CArray 006B56C0..006B5BC0 |
| 006B5CD0-006B5FF0 | `SoundSlot::End`; SoundFader (0x1C). 006B5D20 / 006B5DB0 have no callers (inlined everywhere) |
| 006B6040-006B73E0 | SoundManager (vtable 007440F0, one instance at `*007AF784`): Open 006B6A30 (DirectSound, `_AIL_waveOutOpen`, MP3 stream 006BAAC0, stream thread via AfxBeginThread 007022A0 entry 006B6A10 -> 006BB910; 006B6A20 -> 006BBCB0 is the MIDI thread entry; 15 voices); 006B7110 is Rebirth-patched |
| 006B75E0 | Miles end-of-sample callback (Miles timer thread): sets the voice's `finished`. Rewritten without a verify wrapper: a check (it logs) inside a Miles callback froze every vanilla run |
| 006B7610-006B9AA0 | voices, Play2D/3D, LoadSample, AllocateVoice 006B7D10 (steals the lowest priority voice, deferring a looping one), emitters. 006B7610 has no callers |
| 006B9B10 | Tick (every world frame, from 005641F0) |
| 006BA140-006BA7C0 | 3D sound sequences |
| 006BA950-006BB910 | MP3 stream (SoundManager + 0x100), buffer fill 006BB3A0 (Rebirth-patched; switches files at "Markers1"/"Markers2"), stream thread 006BB910. 006BAD40 has no callers |
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

The music state machine (`MusicSystem::Update`, every world frame) and the positional formula (`UpdatePositional`) are summarised in
`sound_music.cpp` / `sound_manager.cpp`. `RequestStream` modes: 1 stop after the current file, 2 stop with a fade (a fight ended), 3 switch at the
next marker of the current file (combat), 4 switch at the current position (calm); the calm music is due 120..180 s after a jingle or fight. The
pan is mirrored when `panDirect` is 0 (option "swap stereo").

### AI hearing ("Delayed sound")

Not sound code. The AI brain's perception memory (brain + 0xD0, unit_ai.md) holds a CArray of
0x18-byte delayed sounds at memory + 0xA8 {position, countdown, unit, CString text}, filled by
`AIBrain::CallAllies` 005D5990 (005CBBA0: position sound, 26 brain ticks, "Unit %i call for help
...") and `UnitServer::ClaimHostility` 00552D00 (005CBBC0: unit sound, 30 ticks, "Unit %i claims
that player %i are ..."), counted down by the brain update 005CB700 -> 005CC9D0 on the world thread.
On expiry 005CC9D0 prints "Delayed sound: <text>" (position sounds; the memory's noticed point is
set to it) or "Unit N hear delayed sound: <text>" (unit sounds, then 00552CF0) to the console
stream that `filter all` shows.

### Non-determinism (deterministic harness runs)

Sound draws from the shared ISAAC generator (007A9D40) on wall-clock schedules: Tick's ambient
emitters (every 100 ms of GetTickCount, 0..4 draws per firing emitter), the music's calm delay and
track choices (timeGetTime), random sequences (PlayNext) and the random start position of
Play2D/Play3D when SoundManager + 0x8BC is set, terrain ambience 006CB390. All of these run on the
main thread, so the harness's virtual clock (tests/verify/harness_clock.cpp, ticket SV1) covers them.
Two more things made runs part and are handled there too: sample ends (Miles' end-of-sample callback
006B75E0 on its timer thread sets the slot's `finished`, which ends a unit's speech flag and frees
voices) are fired from the main loop once the sample's length has passed on the virtual clock, and
the music's WaitAndEndAfter (PlayJingle / calm music) busy-waits on the main thread for the stream
thread, which other threads let through by reading the virtual time plus the real time since the
main thread last moved it. The sound threads themselves (stream, fader, Miles' timer) draw no random
numbers and touch no game state.

Not handled (SV5d5): the music's draws follow what the stream thread reports. UpdateMusic 006B5350 reads
CurrentStreamName 006BAEA0 (the name while the stream state is 1 or 2), which the stream thread changes as
it plays, in real time: a fight's start picks a combat track (PickName, a draw when the theme has more than
one) only when the playing one is not the last combat track, and the calm branch runs only while nothing
plays. So in a run with fights the number of draws follows how far the stream got, which follows the
machine's load: net_pair_quest parted that way on the original and the image alike (the C13b3 flake, three
classes in sixteen runs). Such a scenario plays with `music 0` (the AudioStreaming setting answered 0:
SetMusicVolume 006B7A00 stops the stream and RequestStream drops every request, so the stream state stays 0
and UpdateMusic's draws follow the virtual clock only). A virtual-time stream thread would cover every run.

## Rewritten (names provisional)

Every function carries its address in its source comment (grep it); the per-function table that used to be here is gone. All in
`sound_manager.cpp` except the MusicSystem functions (`sound_music.cpp`) and the lifetime ones (`sound_manager.cpp`).
- Voices and positions: `UpdatePositional` 006B7FC0, `SoundFader` Update / SetHold / Recalc 006B5EE0 / 006B5E80 / 006B5FF0, FindDeferred
  006B7CB0, MoveEmitter / RemoveEmitter 006B9A30 / 006B9AA0, SetReverb 006B69E0, FadeOutStream / FadeInStream 006BAFA0 / 006BAFE0,
  SetEffectsVolume / SetSpeechVolume / SetListener 006B7740 / 006B78A0 / 006B9750, FindPlaying 006B7C10, SetSoundVolume /
  SetSoundPosition 006B9100 / 006B92F0, MoveSequence / UpdateSequence 006BA400 / 006BA5E0, SetMusicVolume 006B7A00, StopSound 006B94A0,
  RemoveSequence, AddEmitter 006B9950, AddSequence 006BA2D0, `SoundSequence::PlayNext` 006BA140, PauseAll / ResumeAll 006B7320 / 006B7380,
  `SoundSlot::End` 006B5CD0, RequestStream 006BB020 (shared with the stream thread) / StopStream 006BB240, Tick 006B9B10, Play2D 006B8250
  (RET 0x20) / Play3D 006B8600 (RET 0x1C), LoadSample 006B89F0 (RET 0x10), AllocateVoice 006B7D10 (first free voice, else steals the
  lowest priority below `priority`; a looping sound is deferred with its sample position; -1 only when all voices play at >= priority),
  StopAll(stopStream) 006B71C0 (originals_ui.h's `LoadingStepOriginal` is this), WaveLength 006B96E0 (7 bytes).
- `MusicSystem`: `Create` 006B4710 (loads the themes), Construct 006B4350, DestructBody 006B44B0,
  DeletingDestruct 006B4490, `ResetThemes` 006B4690 (slot 3; strings become "none" 007846F0, theme +0x3C kept), class name / id 0xE
  006B4470 / 006B4480, FadeOut / FadeIn 006B4E10 / 006B4E20 (the manager's stream; ECX unused), SetZoneMode 006B4E30, Stop / PlayBriefing /
  PlayConstructor / PlayCredits / PlayMainMenu / PlayJingle 006B4F70-006B5270, UpdateMusic 006B5350 (slot 6; returns 1, inCombat follows
  the fight state). MusicSystem and SoundManager derive from EngineObject.
- `SoundManager` lifetime: class name / id 0xE 006B6290 / 006B62A0, CurrentStreamName 006BAEA0, Create 006B6D70 (slot 2: Reset),
  DeletingDestruct 006B62B0, ClearWaveSources, AddWaveSource, LoadWaveFile, SetPath 006BAE20 (startup 00456440 calls SetPath and
  AddWaveSource), SoundStreamRead 006BA950 (the stream file's read callback, stream thread), Construct 006B6040, DestructBody 006B65D0,
  ResetStream 006BA9C0 (runs on the manager's +0x100, a `__fastcall` shim), `Open` 006B6A30 (RET 0x14: window, reverb, DirectSound device
  index, rate, unused; +0x1C8 / +0x244 the stream / MIDI threads (`CWinThread`), +0x248 their quit flag; the sound option 0079BCC0 = 2
  opens the wave mapper), `Close` 006B6D80 (slot 3; frees each cached wave's own name where the original's RemoveHead 006BC6F0 copies it
  out and frees the copy, node-pool order only), `Reset` 006B73E0.

Non-obvious: Play2D/Play3D with no voice (AllocateVoice -1) return -1 for a one-shot and make a
deferred record (0x48 bytes) for a looping sound; a NaN camera height keeps the full level (the
original's first compare is "below 70 or unordered"); a silent one-shot returns -1 leaving the voice
initialised but not playing; Play3D's deferred record leaves volume, base volume, pan and length as
operator new returned them. LoadSample's 4th argument means "the file is an MP3" (Play2D passes its
last argument, Play3D 0); a cached MP3 loads nothing; the cache trim keeps waves used within 60 s
while cache + new wave stays under 8 MB, otherwise frees a node with no uses. Create calls the
registry through forwarders (00694520 / 00694540 / 006945A0 / 00694880 / 00694D20 / 006958E0) so the
registry's wrappers stay in the path; the theme array grows with SetSize(n+1, -1) 006B59A0 and the
assignment 006B4D40.

Constants read from the exe: pi 007440D8, pi/2 007C8808 (set by static init 006BE310), fade step
dwords 007440E4/007440E8.

### Floating point (SSE2 since C30a2)

`sound_manager.cpp` is float maths built for SSE2, as all the game code is. What of it reaches game
state is whether a voice is taken: a speech's Play2D (priority 1, unit_client_effects) answers -1 when all 15 voices
play at priority 1 or above, and its handle keeps or ends the unit's talking flag. Which voices play follows from
- Play3D's level (`UpdatePositional`: a one-shot at level 0 takes no voice);
- the ambient emitters' distance tests and scaled intervals (Tick: a firing emitter's Play2D at priority 1);
- the voices' ends: by the wave's length (`platform::audio::WaveMilliseconds`, Miles' rule; `WaveLength` /
  `SoundSlot::length` are read by nothing).
Play2D's silence test is on its integer volume, and Tick's restarts take free voices without a level test.
The first two are basic float operations (+ - * /, sqrt, compares, FISTP's rounding as `lib::RoundEven`) on floats
and small integers in the original's order. The original ran them on the main thread at 24-bit precision
control, where every x87 operation rounds once to a float significand, so the results are the same; x87's wider
exponent only matters for distances below about 1e-19, where both give the same decision. The emitter intervals are
exact below 2^24 ms. Checked by comparing the old x87 forms at PC24 with the float forms on 30 M random cases:
0 differ (at PC53 the same test differs in 29k of 3 M). The goldens hold it too (`sound` and every scenario with
speech). The pans (acos), the listener axis, the music fader (stream thread) and the wave lengths only reach the
output.

## Original bugs kept on purpose

- `MoveEmitter`/`RemoveEmitter` accept index 16, one past the 16 emitters (it addresses the emitter
  tick and the first sequence).
- Per-handle controls take the first voice whose handle matches; free voices hold -1, so handle -1
  changes a free voice (and calls the output on its voice).
- Tick recomputes a restarted flat sound's level from its saved output level (0..127) as if it were the
  0..100 base volume (reads the voice's +0x10, not +0x14): such sounds come back louder.
- `SetZoneMode` passes the error text to FatalError as the format string.
- `PlayJingle` and the calm music busy-wait (Sleep(0)) on the main thread until the stream thread
  takes the request; an empty theme list (`combat` with 0 names) divides by zero.
- `FadeOutStream`/`FadeInStream`/`SetMusicVolume` change the music fader without the stream lock.

## Not rewritten

- Miles/DirectSound side: stream open / close / fill (006BAAC0, 006BAC90, 006BB3A0 Rebirth-patched), stream
  and MIDI thread loops (006BB910, 006BBCB0), MIDI (006BBB80, 006BBC70), the DirectSoundEnumerateA callback
  006B68A0 (Open passes it).
- Library (A-H13, config/library.csv): the CArray<MusicTheme> members 006B56C0-006B5BC0 and MusicTheme's operator= /
  destructor 006B4D40 / 006B4D90; the cache list and wave cache members 006BBDF0-006BE240; unwind-only destructors 006B62D0 /
  006B6350 / 006BE300 and the name array element constructors 006B63D0 / 006B63F0. 006BBA00 is dead (no references).
- Live game code left in 006B4000-006C0000: terrain sector pieces around 006BF4A0 (`models.md`; Construct 006BF5F0 and Project
  006BFC80 are done).
- A-H15 layout: +0x168 / +0x178 and the MIDI part's +0x1E4 / +0x1F4 are 32-byte aligned arrays (`AlignedArray`;
  `markers` is the first); the MIDI part (+0x1CC) has two CStrings (+0x1DC / +0x1E0), a fader (+0x20C) and a lock (+0x22C).
  The constructor's hash maps get zero functor bytes where the original copies three uninitialised stack bytes (unused).
  The hash map members (006BBF90 / 006BC2B0 / 006BCE00 / 006BD100 / 006BC4D0 / 006BC510), DestructElements 006BE210 and
  the element constructors / destructors 006B63D0..006B6400 stay original (library); 006714A0 runs through its address
  (`MappedResourceDestroyOriginal`, originals_engine.h). SoundManager now derives from EngineObject; +0xE4 is a
  `CTypedPtrList<MappedResource>` (the wave sources).
- Ambient sounds 006C68B0 (Rebirth-patched) and 006CB390 (terrain material ambience, 2 KB): good next
  candidates together with the terrain owner.
- STLport hash_map / list instantiations 006BBF30-006BDDB0.

## Scenario

`sound.txt`: new game (SetZoneMode, UpdateMusic and Tick every frame, SetListener, volume setters at
load), `@PlayMusic("victory")` (PlayJingle, RequestStream/StopStream), `@CreateFX(...,"nature\thunder\1.wav")`
(Play3D, UpdatePositional, SetSoundPosition/StopSound), then a fight with two goblins (combat music
request and the brain's "Delayed sound" / "hear delayed sound" lines: `expect elayed sound`).

### SoundManager::ReadStreamVanilla

The stream thread's reader (`stream` is the manager's +0x100; Rebirth-patched, so hooked in the vanilla launch
only): fills the spare buffer with the next streamBufferFrames frames. Idle: silence. Else the playing file's
frames up to its loop point (then from its beginning again) or, when a switch is asked, up to the switch point
(mode 2: the whole buffer once the fade out has finished, the fade then held for 4 s; mode 1: where the file
ends), and the rest from the next file: its name looked up in the sound folder, its markers read from the
sound registry, the decoder opened on it and the sample set to its rate and format. No next file (or it does
not open): silence, the stream idle and its name empty. A file that ends early otherwise starts again from its
beginning.
