# Sound and movies

Miles (mss32.dll) and Bink (binkw32.dll) are gone from both builds (user 2026-10-30): no slot of theirs (nor
DSOUND's `DirectSoundEnumerateA`) is reached, so neither DLL loads, nor the `*.asi` providers Miles loaded (mp3dec.asi;
Rebirth's EI_Plugin.asi, which the image kept out anyway: its features are Rebirth flags of our code). Game code calls
src/audio/audio.h (our mixer on an SDL3 audio stream; dr_wav and dr_mp3 of third_party/dr_libs; miniaudio until PL9)
and src/video/movie.h (our own Bink 1 decoder). Both were platform.h's until 2026-10-31, with a seam for other
backends that never came: SDL3 is the one sound device on every system, and the decoders are plain C++.

## What the game reads back

| What | Read by | Kind |
|---|---|---|
| A voice's end (Miles' end-of-sample callback: `SoundSlot::finished`) | Tick frees the voice; `FindPlaying` (a unit's talking flag in unit_client_effects / unit_client_update, map_object_companion, the briefing's next phrase in ui_briefing_create); sequences start their next sound | **logic**: the goldens depend on when it comes |
| Which voices are free (follows from the ends) | AllocateVoice: a speech gets -1 with every voice taken | logic |
| A voice's byte position (`AIL_sample_position`) | kept in a stolen looping sound's deferred record, handed back at its restart | output only |
| The wave's length (`AIL_WAV_info` -> `SoundSlot::length`) | copied into deferred records | read by nothing |
| The decoded briefing voice's size | the wave cache's byte count (evictions) | memory only |
| The music stream's free half (`AIL_sample_buffer_ready`), the MP3 decoder's bytes and file end | the stream thread: `streamState`, `streamName`, `CurrentStreamName`, which UpdateMusic reads; PlayJingle / the calm music busy-wait on `streamState` | logic, but on real time (the stream thread): not deterministic before either (docs/notes/sound.md, "Non-determinism"); fights play with `music 0` |
| `AIL_minimum_sample_buffer_size` | the stream's buffer size (frames per buffer: RequestStream's switch points) | stream timing only |
| Volumes, pans, reverb, 3D | nothing: the game computes 3D itself (UpdatePositional) and hands a 2D volume and pan | output only |
| MIDI | the game ended and released a sequence it never started | gone |
| A movie's frame numbers (`FrameNum`, `LastFrameNum`, `Frames`) | the players' loops: the blocking player ends after the last frame, BinkVideo::Frame answers 0 then (the credits close once their text is done too), the loading screen's movie stops at its last frame | logic (when a player ends), not game state |
| `BinkWait` (the next frame is due) | the players' pacing | real time, not the game's clock |
| A movie's sound, volume, pixels | nothing | output only |

## The rules the sound keeps

- **Voice ends on the game's clock.** A voice's end is due its wave's length after Start (`WaveMilliseconds`: the
  data's bytes over the byte rate, IMA ADPCM's byte rate `rate * block / samplesPerBlock` truncated, then truncated
  to ms; less the start position, times the loop count; never with loop count 0), on `platform::Milliseconds`; Stop /
  Resume keep what is left. The main loop delivers the due ends with `DeliverEnds(now)` right after reading the
  frame's time (app_main.cpp TimeSinceLastPass), so the callbacks run on the main thread, in voice order, as the
  harness called Miles' callbacks on the virtual clock before (tests/verify/harness_clock.cpp, game.exe's runs). A
  backend that plays for real still times the end this way, not by the device. The rule is Miles' own: checked
  equal on every wave and briefing voice of the vanilla, Rebirth and LostInAstral archives (18993; the header fields,
  the ms and the decoded sizes), with a 32-bit tool that asks mss32.dll (scratch; a sweep against its output takes
  seconds).
- **Decoded MP3 voices** (`DecodeMp3File`, the briefing voices, `compressed` in LoadSample): a 16-bit PCM wave with a
  44-byte header at the file's rate and channels and every whole frame's samples (1152 for MPEG-1, 576 for MPEG-2 /
  2.5). That is what Miles' mp3dec.asi gave (mono stayed mono although the game asked for stereo), so the length and
  the cache's size stay the same. dr_mp3 gives fewer frames on 3968 of the installs' 9908 MP3 voices (2880..4031
  fewer: it drops the LAME / Xing info frame and the encoder's delay and padding) and the same on the rest; the
  backend pads with silence at the end. `DecodeMp3` (the music) keeps to the same count.
- **The stream** (`ReadyHalf` / `QueueHalf`): a half is free again once its frames have played. With a device the
  mixer frees it when it takes the half's last frame (no drift from the device's clock); without one (or once the
  device stopped by itself) the frames advance on `platform::Milliseconds` from the stream thread (the harness's
  other-threads clock, a real-time one), counted to the frame: the game's 7680-frame halves free 175, 349, 523 ms after
  the first was queued (the null backend's whole-ms rule gave 174, 348, 522). `MinimumHalfBytes` is 87 ms rounded up
  to 512 bytes (15360 for the game's 44.1 kHz stereo, what Miles gave through the wave mapper; through DirectSound it
  varied run to run).
- **Movies** pace on a real-time clock (std::chrono::steady_clock): the blocking player's loop reads the game's
  clock, which the harness holds while no frame runs. `Advance` after the last frame goes back to the first (the
  titles' movie loops under the text); `GoToFrame` stops at the last.

## The mixer (SDL3 and dr_libs)

- The device: SDL3's default playback device (`SDL_OpenAudioDeviceStream`, WASAPI on Windows), an audio stream of
  32-bit float stereo at the game's rate (44100; SDL converts to the device's own) whose callback runs our mixer for the
  bytes the device asks for. SDL follows a change of the default device itself, and a device that goes away is
  replaced by SDL's stand-in, which takes the frames at the device's pace. A device that cannot open leaves the game
  running as without one
  (silently, `OpenOutput` still true), so the game's logic never depends on the machine's sound hardware. Its name, the
  first voice and the first sound heard, and at the close the voices started and the mix's peak go to the debugger's
  output (`platform::DebugOutput`; DebugView shows them).
- Voices: `SetWave` decodes the whole wave with dr_wav (`drwav_read_pcm_frames_s16`: PCM 8 / 16 bits, IMA ADPCM)
  into 16-bit samples the voice
  keeps, cut to WaveInfo's count (dr_wav decodes IMA ADPCM's last block whole, past the fact chunk's count). The mixer
  plays them from the position (`SetPosition`'s bytes as frames: PCM by the block align, IMA ADPCM by its blocks) with
  the loop count, at the output's rate by straight-line interpolation (the waves at 44.1 kHz come out sample for
  sample). Volume 0..127 is linear; pan 0..127 is a balance (64 both sides full, the far side down to nothing at 0 /
  127). Volume and pan changes ramp over one device period. The end the game sees stays the clock's (above): the
  device's own end only stops the mixing.
- The cave reverb (`SetReverb(room, level, time)`; the game gives every new voice the cave's 0.3, 1.0, 3.0 s while
  the party is in a cave zone, world_client_maps.cpp, and room 0 with the level kept once it leaves one): a voice with
  room and level above 0 feeds a mono send at its level (after its volume and pan) and keeps 1 - 0.35 * level of itself
  (`kReverbDryCut`), so a full-scale voice and its tail stay under full scale. One shared Freeverb-style network
  (`Reverb`): eight damped feedback combs in parallel and four all-passes in series a side, Freeverb's tunings scaled
  by the room (0.5 + room: 0.8 for caves) and the rate, the right side 23 frames longer; each comb's feedback is the
  time's -60 dB over its own delay, the loop's low-pass at 0.1. The last SetReverb's room and time shape it (the game
  passes the same to every voice); a new room empties the lines. With nothing sent for two tails (-120 dB) and the
  longest delay it clears its lines and rests: no denormals ever, and no cost while no cave voice plays. Output only:
  the voices' ends, positions and the stream are untouched, and harness runs (no device) never mix.
- How the reverb was checked (scratch tool, A1a): a click's tail falls 60 dB in 2.6 s and steady noise's in 2.7 s of
  the 3.0 asked (the damping takes the highs sooner); the steady tail sits 11 dB under the voice; over all 3848 waves
  of the vanilla sfx.res at full volume none clips that did not clip dry (the worst peak with the reverb 1.19 times
  the dry's); dry and reverb takes of every voice play the same frames and end at the same clock time; the network
  costs about 1.4 ms of mixer time a second of audio while it runs (one core) and nothing once it rests, the same in
  every 5 s window of a 60 s tail.
- The stream: the halves are mixed straight from the caller's buffers (it writes a half only once ReadyHalf gave it)
  at each half's format (`SetStreamFormat` applies to the halves queued after it), 16 bits signed or 8 unsigned, any
  rate. More than one stream can play (a movie's sound takes one of its own).
- MP3: `OpenMp3` reads the whole file and decodes it with dr_mp3 (`DecodeMp3` in whole frames; `RewindMp3` seeks to
  the start); `DecodeMp3File` as above. Both run in every build, the harness's runs included. dr_wav 0.14.6 and
  dr_mp3 0.7.4 are the standalone headers of the versions miniaudio 0.11.25 carried (0.14.5, 0.7.3), called directly
  as miniaudio's decoder called them (16-bit output at the file's rate and channels).
- All the mixer's state (voices' sounds, streams) is under one lock; the device's thread takes it once a period. The
  voices' clock fields (ends, positions) are the main thread's, as the game calls them.
- **Harness runs open no device**: tests/verify/image/harness_audio_image.cpp calls `UseNoDevice` before main when
  EI_HARNESS is set (the harness's own trigger), so a run is silent and the same on every machine; the voices' ends and
  the stream run on the harness's clocks exactly as with a device. game.exe's runs mute Miles as before (harness.cpp's
  `MutedWaveOutOpen`). The play, release and x64 builds, and the verify build started without a scenario, make sound.
- The goldens did not move with the null backends (122 the same, 2026-10-30), nor with this one. One difference from
  the harness's timers in game.exe's runs: they keep a voice's loop count across `AIL_init_sample`, where Miles resets
  it to 1, so there a one-shot sound on a voice that once looped never ends; `ResetVoice` resets it as Miles did. No
  golden depends on it today.
- How it was checked (no ear): an offline tool that includes the backend and calls its mixer as the device would
  (scratch; it decodes the archives' entries): every wave of the vanilla, Rebirth and LostInAstral sfx / speech
  archives (9085) decodes to its header's frames, rate and channels; every MP3 voice (9908) opens in dr_mp3 with the
  scanned rate and channels and `DecodeMp3File` gives the whole frames' size; voices of each kind mixed at full volume
  have the source's length (twice the frames at 22.05 kHz, twice with two loops) and peak, the 44.1 kHz ones sample
  for sample; pan 0 / 127 leaves the other side at 0, volume 64 gives 64/127 of the peak; two Stream\*.mp3 queued in
  halves as the stream thread does come out sample for sample as dr_mp3 decodes them. A windowed run of the release
  exe (a run folder of junctions, Work path / CD-ROM Path `.\`, 45 s to the menu, closed with WM_CLOSE) logged the
  WASAPI device, the first voice and sound, and 206 voices at the close; a `sound` scenario run with a DebugView-style
  listener showed no device and no audio session opened. Goldens: 122 + 24 (Rebirth) the same.

## Movies

The backend is our own Bink 1 decoder (`src/video`): `bink_file` the container (the header, the frame index,
a frame's packets read from the file when it is decoded), `bink_video` the video, `bink_audio` the sound,
`bink_tables` the format's constant tables, `bit_reader` the bit order, `movie.cpp` platform.h's movies. It was written from the
format's descriptions (multimedia.cx: Bink Container, Bink Video, Bink Audio; Kostya Shishkov's notes), with the game's
binkw32.dll studied for what they leave out (the tables are its numbers). FFmpeg's sources were never read (the exe is
MIT and links nothing of FFmpeg's); FFmpeg ran only offline as the oracle.

- What it plays: Bink 1 revision i ("BIKi") with video flags 0, the first sound track when it is the RDFT variant.
  That is every movie the installs ship: 46 distinct files (vanilla 640x272 and 800x600, Rebirth's 1920x816 /
  1080 / 960, LostInAstral's; sound 44.1 or 22.05 kHz stereo, many without). Other revisions, alpha, grayscale and
  the scaled / interlaced modes do not open (`Open` gives null: the game logs "Can't open"); a DCT track plays
  silent (none ships, and binkw32 1.0 the game came with has no DCT decoder).
- The video: per frame the luma's size word, then the planes Y, V, U (V comes first), each from a 32-bit word of its
  own; 8x8 blocks with the bundles and prefix codes as the format describes. The frame before the first is all 0
  bytes; a motion vector is kept inside the plane. Frames as R, G, B, 255 by BT.601 limited range with RAD's 15-bit
  tables (each term truncated), a chroma sample per 2x2 pixels.
- The sound: transform frames of 2048 samples a channel at 44.1 kHz (1024 at 22.05), the channels interleaved in one
  real transform; a frame's first sixteenth is blended with the frame before's tail, each frame clipped to 16 bits
  before the blend (RAD's order). A packet gives as many samples as its header says (the last frame of the track is
  cut there). The transform is computed in double.
- Playback: `DecodeFrame` decodes in order (the last frame on to the first too: the titles loop with their sound
  going on); a jump (`GoToFrame` past the next frame, the loading screen) decodes from the keyframe at or before the
  frame, or on from the decoded one when that is nearer, without those frames' sound. `Waiting` is due the movie's
  frame time per `Advance` after `Open`, on std::chrono::steady_clock.
- The sound plays when `Open`'s `sound` and the output gives a stream (`CreateStream`): the frames' samples are queued
  as they decode (Bink's first frame carries a lead of several frames) and handed to the stream in halves of
  `MinimumHalfBytes` (87 ms) whenever `ReadyHalf` frees one (in `Waiting` and `DecodeFrame`); the rest goes once the
  last frame is decoded. Format: the track's rate and channels, 16 bits; `SetVolume` 0..32768 maps linearly to the
  stream's 0..127 (127 until set). Video and sound each keep their own clock: no resync after a stall of the game.
- Cost (x64 release, one core): a 1920x816 frame about 4 ms, an 800x600 one about 1.3 ms; the sound about 1% of
  real time.
- How it was checked (scratch tool and ffmpeg 9.0.2, gyan.dev's essentials build, both outside the repository; a sweep
  takes 2 minutes): on all 46 files every frame's YUV (ffmpeg `-pix_fmt yuv420p -f framemd5`) is identical, 16113
  frames. The sound of the 27 files with a track matches ffmpeg's s16 within 1 (about 96.7% of the samples exact:
  ffmpeg decodes in float and converts float samples to 16 bits); larger differences only inside the blended frame
  starts of loud passages (ffmpeg blends before clipping), at most 726 samples of a file; ffmpeg gives the
  last transform frame whole (up to one frame more at the end). RGB against ffmpeg's rgb0: within 2 per channel,
  mean 0.65 (swscale's own rounding). A windowed run of the release exe (a run folder of junctions without
  binkw32 / mss32, Work path / CD-ROM Path `.\`) showed the start-up movies in their colours and reached the menu;
  the movie's stream was the first sound to reach the device; closed with WM_CLOSE.
- Harness runs: the output has no device but gives streams, so a movie's sound decodes and its halves wait on the
  game's clock; movies pace on the real-time clock as before. The goldens did not move.
