# Sound, music and movies

The game's sound code is `src/game/sound.h`, `sound_manager.cpp`, `sound_music.cpp`; it plays through our own output
(`src/audio/audio.h`: a mixer on an SDL3 audio stream, dr_wav and dr_mp3 of third_party/dr_libs) and our own Bink 1
decoder (`src/video`). Neither Miles nor Bink is loaded. Scenario: `tests/scenarios/sound.txt` (new game, a jingle,
a 3D effect, a fight with combat music and the brain's "Delayed sound" lines).

## What the game reads back

Only these reach game logic; volumes, pans, reverb, positions and the pixels and sound of movies are output only.
- **A voice's end** (`SoundSlot::finished`): Tick frees the voice; `FindPlaying` reads it for a unit's talking flag
  (unit_client_effects / unit_client_update, map_object_companion) and the briefing's next phrase; sequences start
  their next sound. The goldens depend on when it comes.
- **Which voices are free**: `AllocateVoice` takes the first free voice, else steals the lowest priority below the
  request (a looping sound is deferred with its sample position); -1 only when all 15 voices play at the priority or
  above. A speech's Play2D (priority 1) then answers -1.
- **The music stream's state** (`streamState`, `CurrentStreamName`), read by `UpdateMusic`: on real time (the stream
  thread), see "Non-determinism".
- **A movie's frame numbers**: when a player ends (the blocking player after the last frame; the credits close once
  their text is done too; the loading screen's movie stops at its last frame).

## The rules the sound keeps

- **Voice ends on the game's clock.** A voice's end is due its wave's length after Start (`WaveMilliseconds`: the
  data's bytes over the byte rate (IMA ADPCM: `rate * block / samplesPerBlock` truncated), truncated to ms, less the
  start position, times the loop count; never with loop count 0), on `platform::Milliseconds`; Stop / Resume keep
  what is left. The main loop delivers due ends with `DeliverEnds(now)` right after reading the frame's time, on the
  main thread, in voice order. A backend that plays for real still times ends this way, not by the device. This is
  Miles' own rule, checked on every wave and briefing voice of the vanilla, Rebirth and LostInAstral archives.
- **Decoded MP3 voices** (the briefing voices): a 16-bit PCM wave with a 44-byte header at the file's rate and
  channels and every whole frame's samples (1152 for MPEG-1, 576 for MPEG-2 / 2.5), as Miles' mp3dec.asi gave (mono
  stays mono), so the length and the cache's size are the original's. dr_mp3 gives fewer frames on about 40% of the
  voices (it drops the LAME info frame, the encoder's delay and padding); the backend pads with silence.
- **The stream**: a half is free again once its frames have played: with a device when the mixer takes its last
  frame, without one on `platform::Milliseconds` from the stream thread, counted to the frame (the game's 7680-frame
  halves free at 175, 349, 523 ms). `MinimumHalfBytes` is 87 ms rounded up to 512 bytes (15360 for 44.1 kHz stereo).
- **Harness runs open no device** (`UseNoDevice` when EI_HARNESS is set): silent and the same on every machine; ends
  and the stream run on the harness's clocks exactly as with a device.

## The mixer

- SDL3's default playback device, a 32-bit float stereo stream at 44100 Hz whose callback runs our mixer. SDL
  follows a change of the default device; a device that cannot open leaves the game running silently, so logic never
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
- The stream mixes straight from the caller's halves at each half's format; more than one stream can play (a movie's
  sound takes its own). All mixer state is under one lock; the voices' clock fields are the main thread's.

## Game side

- `SoundManager` (an EngineObject): 15 voices, Play2D / Play3D, LoadSample, the wave cache (keeps waves used within
  60 s while cache + new wave stays under 8 MB, else frees a node with no uses), emitters (map ambient sounds),
  sequences, the MP3 music stream and its thread. `Tick` runs every world frame.
- `UpdatePositional` computes 3D sound itself and hands a 2D volume and pan (the pan mirrored when `panDirect` is 0,
  the "swap stereo" option); a one-shot at level 0 takes no voice; a NaN camera height keeps the full level.
- Play2D / Play3D with no voice return -1 for a one-shot and make a deferred record for a looping sound. LoadSample's
  4th argument means "the file is an MP3"; a cached MP3 loads nothing.
- `MusicSystem` (reads `music.reg`): `UpdateMusic` every world frame. `RequestStream` modes: 1 stop after the current
  file, 2 stop with a fade (a fight ended), 3 switch at the next marker of the current file (combat), 4 switch at the
  current position (calm); the calm music is due 120..180 s after a jingle or fight. The stream thread's reader fills
  the spare buffer from the playing file up to its loop point or the switch point (mode 2: the whole buffer once the
  fade has finished, held 4 s; mode 1: where the file ends), then from the next file (its markers from the sound
  registry); no next file: silence, the stream idle and its name empty.
- **Floating point**: what reaches game state is whether a voice is taken, which follows from Play3D's level and the
  emitters' distances and intervals: basic float operations in the original's order (exact against its 24-bit
  results; checked on 30 M random cases). The pans (acos), the listener axis, the fader and the wave lengths only
  reach the output.
- **AI hearing** ("Delayed sound") is not sound code: the brain's perception memory holds delayed sounds (a position
  or a unit, a countdown of 26 or 30 brain ticks, a text) from `AIBrain::CallAllies` and
  `UnitServer::ClaimHostility`; on expiry they print "Delayed sound: <text>" or "Unit N hear delayed sound: <text>" to
  the console stream `filter all` shows.

## Non-determinism

Sound draws from the game's ISAAC generator on the main thread (Tick's ambient emitters every 100 ms of game clock, the
music's calm delay and track choices, random sequences, random start positions, terrain ambience), so the harness's
virtual clock covers them. Not covered: `UpdateMusic` reads `CurrentStreamName`, which the stream thread changes in
real time; a fight's start draws a combat track only when the playing one is not the last combat track, so in a run
with fights the number of draws follows how far the stream got, i.e. the machine's load. Such scenarios play with
`music 0` (the stream stays idle). A virtual-time stream thread would cover every run.

## Original bugs kept

- `MoveEmitter` / `RemoveEmitter` accept index 16, one past the 16 emitters.
- Per-handle controls take the first voice whose handle matches; free voices hold -1, so handle -1 changes a free
  voice.
- Tick recomputes a restarted flat sound's level from its saved output level (0..127) as if it were the 0..100 base
  volume: such sounds come back louder.
- `SetZoneMode` passes the error text to FatalError as the format string.
- `PlayJingle` and the calm music busy-wait on the main thread until the stream thread takes the request; an empty
  theme list (`combat` with 0 names) divides by zero.
- `FadeOutStream` / `FadeInStream` / `SetMusicVolume` change the music fader without the stream lock.

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
  nearer. `Waiting` paces on std::chrono::steady_clock (the game's clock stands still while a blocking movie plays).
  The sound goes to a stream in 87 ms halves as they free; `SetVolume` 0..32768 maps to 0..127. Video and sound keep
  their own clocks.
- Checked: on all 46 files every frame's YUV is identical to ffmpeg's (16113 frames); the sound within 1 of ffmpeg's
  s16 (ffmpeg blends before clipping). Cost: a 1920x816 frame about 4 ms on one core.
