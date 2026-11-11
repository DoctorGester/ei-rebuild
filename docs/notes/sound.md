# Sound, music and movies

The game's sound code is `src/game/sound/sound.h`, `sound_manager.cpp`, `sound_music.cpp`; it plays through our own
output (`src/audio/audio.h`: a mixer on an SDL3 audio stream, dr_wav and dr_mp3 of third_party/dr_libs) and our own Bink 1
decoder (`src/video`). Neither Miles nor Bink is loaded. Scenario: `sound` in `tests/scenarios/batch_sound_ai.txt`
(new game, a jingle, a 3D effect, a fight with combat music and the brain's "Delayed sound" lines).

## What the game reads back

Only these reach game logic; volumes, pans, reverb, positions and the pixels and sound of movies are output only.
- **A voice's end** (`audio::Ended`, set by `DeliverEnds`): Tick frees the voice; `FindPlaying` reads it for a unit's
  talking flag (unit_client_effects / unit_client_update, map_object_companion) and the briefing's next phrase; sequences start
  their next sound. The goldens depend on when it comes.
- **Which voices are free**: `AllocateVoice` takes the first free voice, else steals the lowest priority below the
  request (a looping sound is deferred with its sample position); -1 only when all 15 voices play at the priority or
  above. A speech's Play2D (priority 1) then answers -1.
- **The music stream's state** (`streamState`, `CurrentStreamName`, the fader), read by `UpdateMusic`: stepped on the
  main thread on the game's clock; with a device its halves free as they are played, see "Non-determinism".
- **A movie's frame numbers**: when a player ends (the blocking player after the last frame; the credits close once
  their text is done too; the loading screen's movie stops at its last frame).

## The rules the sound keeps

- **Voice ends on the game's clock.** A voice's end is due its wave's length after Start (`WaveMilliseconds`: the
  data's bytes over the byte rate (IMA ADPCM: `rate * block / samplesPerBlock` truncated), truncated to ms, less the
  start position, times the loop count; never with loop count 0), on `platform::FrameClock`; Stop / Resume keep
  what is left. The main loop delivers due ends with `DeliverEnds(now)` right after reading the frame's time, on the
  main thread, in voice order. A backend that plays for real still times ends this way, not by the device. This is
  Miles' own rule, checked on every wave and briefing voice of the vanilla, Rebirth and LostInAstral archives.
- **Decoded MP3 voices** (the briefing voices): a 16-bit PCM wave with a 44-byte header at the file's rate and
  channels and every whole frame's samples (1152 for MPEG-1, 576 for MPEG-2 / 2.5), as Miles' mp3dec.asi gave (mono
  stays mono), so the length and the cache's size are the original's. dr_mp3 gives fewer frames on about 40% of the
  voices (it drops the LAME info frame, the encoder's delay and padding); the backend pads with silence.
- **The stream**: a half is free again once its frames have played: with a device when the mixer takes its last
  frame, without one by the time `ReadyHalf` is given, counted to the frame (the game's 7680-frame halves free at 175,
  349, 523 ms). `MinimumHalfBytes` is 87 ms rounded up to 512 bytes (15360 for 44.1 kHz stereo).
- **The stream's steps** (`StepStream(now)`: a fader step, then every free half queued and the spare buffer refilled)
  come every 100 ms of the game's clock, with a device or without, all on the main thread (`FollowClock(now)`: each
  step at its own time 100 ms after the last, as many as are due, so the music keeps pace with the clock even when a
  frame is longer than the two halves). The main loop calls it right after `DeliverEnds`; the passes that hold the
  main loop longer call it too (`app::StepMusic`): each loading-screen step (`ShowLoadingMovie`) and the system's
  modal move / size loop (`platform::SetModalLoopStep`, every 10 to 17 ms on Windows and macOS).
- **Harness runs open no device** (`UseNoDevice` when EI_HARNESS is set): silent and the same on every machine; ends
  and the stream's steps run on the virtual clock, from the main thread. `EI_HARNESS_SOUND=1` keeps the real device for
  a run that measures the sound (with `EI_AUDIO_TRACE`, below); not for goldens.
- **The game thread never waits for the device.** Its calls only take the mixer's lock (held for one mix of a device
  period); a one-off music file (`PlayStreamOnce`: PlayJingle, the calm music) is requested and its "silence after
  it" is asked for by the step that switches to it (`endAfterSwitch`; any other request clears it), while
  `UpdateMusic` chooses nothing until then (`StreamSwitchPending`). Music requests change the stream's state at once:
  no lock and no queue, the steps being on the same thread. The MP3 work is a step's: about 0.1-0.2 ms a half's
  decoding, 1-3 ms a file's opening.
- **Why the stream has no thread.** The halves are 174 ms (7680 frames) each, so a step may come up to 174 ms after a
  half frees: a pass longer than about 74 ms can starve the music, by at most its length less 74 ms, when a half frees
  just after a step. The long passes are the loads (a zone change 0.2-0.4 s warm, 2.7 s cold); the music is stopped
  before each (`LoadZone`'s and the main menu's `StopAll(true)`, the load's), and the loading screen's steps feed the
  stream through them. In play the passes stay under about 140 ms (the F5 save among them: harness.md, "Save and load
  cost") and the stream does not starve. A deeper queue was turned down: a cold load needs 3 s or more, which puts every
  Now / AtMarker switch that much later and frees the no-device halves on other frames (goldens).

## The mixer

- SDL3's default playback device, a 32-bit float stereo stream at 44100 Hz whose callback runs our mixer (the log
  names the device, its driver, its rate and its period: WASAPI 441 frames, pulseaudio 768). SDL follows a change of
  the default device; a device that cannot open leaves the game running silently, so logic never
  depends on the sound hardware. The device's name, the first voice and sound, and at the close the voices started and
  the mix's peak go to the debugger's output.
- Voices decode the whole wave with dr_wav (PCM 8 / 16 bits, IMA ADPCM) to 16-bit samples, cut to the header's count,
  played at the output rate by linear interpolation. Volume 0..127 is linear; pan 0..127 a balance (64 both sides
  full); changes ramp over one device period.
- **Cave reverb** (`SetReverb(room, level, time)`; every new voice gets the cave's 0.3, 1.0, 3.0 s while the party is
  in a cave zone): a voice with room and level above 0 feeds a mono send at its level and keeps 1 - 0.35 * level of
  itself. One shared Freeverb-style network (eight damped combs in parallel, four all-passes in series a side, tunings
  scaled by 0.5 + room and the rate, the right side 23 frames longer; each comb's feedback the time's -60 dB over its
  delay, low-pass 0.1). A new room empties the lines; with nothing sent for two tails it clears and rests (no
  denormals, no cost). Checked: tails fall 60 dB in 2.6-2.7 s of the 3.0 asked, nothing clips that did not clip dry.
- **Offline check** (`tests/unit/audio_mix.cpp`, run by the unit check and the POSIX port check): `OpenOffline` opens no
  device and no thread; `MixOffline` takes the frames through a plain SDL audio stream that `DeviceData` fills, so the
  mixer runs as with a device (the halves free as they are played). The check plays synthetic waves (PCM 8 / 16 bits,
  IMA ADPCM, other rates, loops, positions, pauses), halves and the cave reverb against a model of the rules written out
  in the test, and keeps a hash of one mix of everything together, the same on Windows and Linux (basic float operations
  in a fixed order, `lib::Pow` for the reverb's feedback). Under 1 s. A change to the mix that moves the hash is intended
  or not: the check prints the new hash. Not reached: the trace (`NoteFrame`), the real device, the MP3 decoders.
- A stream's halves are 16-bit; `QueueHalf` copies one with its format and the mixer plays the copy; more than one
  stream can play (a movie's sound takes its own). All mixer state is under one lock; the voices' clock fields are the
  main thread's.

## How the sound keeps up (EI_AUDIO_TRACE)

With `EI_AUDIO_TRACE=1` the log gets an `audio trace:` line a second: the passes of the main loop (and the longest),
the game thread's time in the sound's calls a pass (mean and most) and its waits for the mixer's lock, the device's
requests and frames (the largest, the longest gap between two, the longest mix), how far the sound given runs ahead
of real time (the device's buffer; a falling value is a device that takes less than real time), and the stream frames
played as silence for want of a queued half. In a play run set it in the environment; in a harness run add
`EI_HARNESS_SOUND=1` (both reach the game through run_all), with `realtime` in the scenario for real-time spacing.

Typical values: the game thread's sound time a pass is a few hundredths of a ms on average, at most a few ms (a wave's
decoding in SetWave); lock waits below 0.1 ms; the device 10-30 ms ahead. Under WSLg pulseaudio's "RDP Sink" can stall
(docs/notes/linux.md, "Sound under WSLg"), which is why nothing on the main thread waits for the stream.

## Game side

- `SoundManager` (an EngineObject): 15 voices, Play2D / Play3D, LoadSample, the wave cache (keeps waves used within
  60 s while cache + new wave stays under 8 MB, else frees a node with no uses), emitters (map ambient sounds),
  sequences, the MP3 music stream. `Tick` runs every world frame.
- `UpdatePositional` computes 3D sound itself and hands a 2D volume and pan (the pan mirrored when `panDirect` is 0,
  the "swap stereo" option); a one-shot at level 0 takes no voice; a NaN camera height keeps the full level.
- Play2D / Play3D with no voice return -1 for a one-shot and make a deferred record for a looping sound: the
  sound's `SoundParams` (also its voice's while it plays) and its sample position, by handle in `deferred`.
  `IsPlaying(handle)` is a voice or a deferred record. LoadSample's 4th argument means "the file is an MP3"; a cached
  MP3 loads nothing. The music's files are read whole (`platform::ReadWholeFile`) and decoded from memory.
- `MusicSystem` (reads `music.reg`): `UpdateMusic` every world frame. `RequestStream` takes a `StreamSwitch`: `AtEnd`
  stops after the current file, `AfterFade` stops with a fade (a fight ended), `AtMarker` switches at the next marker
  of the current file (combat), `Now` at the current position (calm; `StopStream(Now)` closes the file at once); the
  calm music is due 120..180 s after a jingle or fight. `FillSpareBuffer` fills the spare buffer from the playing file
  up to its loop point or the switch point (`AfterFade`: the whole buffer once the fade has finished, held 4 s;
  `AtEnd`: where the file ends), then from the next file (its markers from the sound registry); no next file:
  silence, the stream idle and its name empty. Play2D takes its loop / height fade / speech / MP3 flags as a
  `Play2DOptions`.
- **Floating point**: what reaches game state is whether a voice is taken, which follows from Play3D's level and the
  emitters' distances and intervals: basic float operations in the original's order. The pans (acos), the listener
  axis and the fader only reach the output.
- **AI hearing** ("Delayed sound") is not sound code: the brain's perception memory holds delayed sounds (a position
  or a unit, a countdown of 26 or 30 brain ticks, a text) from `AIBrain::CallAllies` and
  `UnitServer::ClaimHostility`; on expiry they print "Delayed sound: <text>" or "Unit N hear delayed sound: <text>" to
  the console stream `filter all` shows.

## Non-determinism

Sound draws from the client effects' generator (`g_clientRandom`) on the main thread (Tick's ambient emitters every
100 ms of game clock, the music's calm delay and track choices, random sequences, random start positions, terrain
ambience), so the harness's virtual clock covers them. They still reach game logic through the sound: a speech
variant's wave length is when its voice ends. `UpdateMusic` reads the stream's state (`CurrentStreamName`,
`StreamSwitchPending`): a fight's start draws a combat track only when the playing one is not the last combat track.
With a device the halves free as the device plays them, so the steps' results follow how far the device got; without
one (every harness run) they free on the virtual clock and the draws come on the same frames in every run.

## Movies

Our Bink 1 decoder (`src/video`: `bink_file` the container, `bink_video`, `bink_audio`, `bink_tables`, `bit_reader`,
`movie.cpp`), written from the format's public descriptions (multimedia.cx; Kostya Shishkov's notes) with the game's
binkw32.dll studied for what they leave out; FFmpeg's sources were never read (FFmpeg ran only offline as the oracle).
- Plays Bink 1 revision i ("BIKi") with video flags 0 and the first sound track when it is the RDFT variant: every
  movie the installs ship (46 files). Other revisions, alpha, grayscale and scaled / interlaced modes do not open
  (the game logs "Can't open"); a DCT track plays silent.
- Video: per frame the luma's size word, then the planes Y, V, U, each from a 32-bit word of its own; 8x8 blocks with
  the bundles and prefix codes; the frame before the first is all zero; a motion vector is kept inside the plane.
  Frames as RGBA by BT.601 limited range with RAD's 15-bit tables (each term truncated).
- Sound: transform frames of 2048 samples a channel at 44.1 kHz (1024 at 22.05), channels interleaved in one real
  transform (computed in double); a frame's first sixteenth is blended with the previous frame's tail, each frame
  clipped to 16 bits before the blend.
- Playback: `DecodeFrame` decodes in order (after the last frame back to the first: the titles loop with their sound
  going on); a jump (`GoToFrame`) decodes from the keyframe at or before the frame, or on from the decoded one when
  nearer. `Pump` feeds the sound and says whether the next frame is due, on std::chrono::steady_clock (the game's
  clock stands still while a blocking movie plays); a player calls it every pass. The sound goes to a stream in 87 ms
  halves as they free; `SetVolume` takes 0..127 (the options' streaming volume times 127 / 100). `FramePixels`
  converts the decoded frame into the movie's own RGBX buffer. Video and sound keep their own clocks.
- Checked: on all 46 files every frame's YUV is identical to ffmpeg's (16113 frames); the sound within 1 of ffmpeg's
  s16 (ffmpeg blends before clipping). Cost: a 1920x816 frame about 4 ms on one core.
