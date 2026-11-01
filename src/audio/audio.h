#pragma once

// The sound (docs/notes/audio.md): the output device, voices playing RIFF WAVE files from memory, the music's
// double-buffered stream, and the MP3 decoders the music and the briefing voices go through; our mixer on SDL3's
// audio device with the dr_wav / dr_mp3 decoders (audio.cpp). Game logic reads back only when a voice has ended (its
// end callback), so that end is timed by the wave's length (WaveMilliseconds) on the game's clock
// (platform::Milliseconds) and delivered from the main loop (DeliverEnds), whatever the device does: the harness's
// runs stay deterministic (its virtual clock). Volumes and pans are the game's 0..127 scales.

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audio {

// ---- Waves in memory ----

inline constexpr std::int32_t kPcm = 1;
inline constexpr std::int32_t kImaAdpcm = 0x11;

// What a RIFF WAVE's header says.
struct WaveInfo {
    std::int32_t format = 0;          // the format tag: kPcm or kImaAdpcm
    std::uint32_t rate = 0;           // samples per second
    std::int32_t bits = 0;            // per sample (4 for IMA ADPCM)
    std::int32_t channels = 0;
    std::uint32_t samples = 0;        // per channel: the fact chunk's count for IMA ADPCM, else what the data holds
    std::uint32_t dataLength = 0;     // bytes of the data chunk
    std::uint32_t blockSize = 0;      // the fmt chunk's block align
    const std::uint8_t* data = nullptr;   // the data chunk; null when the file is not a RIFF WAVE
};
// The wave's header; walks the chunks only inside the `size` bytes and clamps the data chunk to them. A buffer with no
// data chunk in it comes back empty (data null).
WaveInfo ReadWaveInfo(const void* wave, std::size_t size);
// How long the wave plays once, in whole milliseconds: the data's bytes over its byte rate (for IMA ADPCM the rate
// times the block size over the samples in a block, truncated), truncated. The original's rule, which the harness's
// runs timed every end by (checked on all 19k waves and briefing voices of the installs); 0 without a wave.
std::uint32_t WaveMilliseconds(const WaveInfo& info);

// ---- The device ----

// Opens the default output device for 16-bit stereo at `rate` (at most 44100), `reverb` making room for the voices'
// reverb. False with the reason in `error` when it cannot; then no voice or stream can be made. A backend may also
// go on without a device (UseNoDevice, or none to be had): the game then runs as with one, silently.
bool OpenOutput(platform::NativeWindow window, unsigned rate, bool reverb, std::string* error);
// From now on OpenOutput opens no device: nothing is heard, and what the game reads back keeps to the game's clock
// as it does with one (the harness's runs: silent and the same on every machine).
void UseNoDevice();
// Closes the device; every voice and stream must be destroyed first.
void CloseOutput();

// ---- Voices ----

struct Voice;
using EndCallback = void (*)(void* user);

// A voice of the open device; null when none is open.
Voice* CreateVoice();
void DestroyVoice(Voice* voice);
// Stopped and empty: no wave, loop once, volume 127, centred, no reverb, no end callback, at the beginning.
void ResetVoice(Voice* voice);
// The RIFF WAVE (PCM or IMA ADPCM) the voice plays, from the beginning. The memory stays the caller's while the
// voice has it.
void SetWave(Voice* voice, const void* wave, std::size_t size);
// How often the wave plays: 0 for ever.
void SetLoopCount(Voice* voice, int count);
void SetVolume(Voice* voice, int volume);   // 0..127
void SetPan(Voice* voice, int pan);         // 0 left, 64 centre, 127 right
void SetReverb(Voice* voice, float room, float level, float time);
// Where in the wave it plays next: in milliseconds, or bytes into the data.
void SetPositionMs(Voice* voice, std::uint32_t ms);
void SetPosition(Voice* voice, std::int32_t bytes);
std::int32_t Position(const Voice* voice);
void Start(Voice* voice);
// Paused; Resume goes on from there (its end comes as much later).
void Stop(Voice* voice);
void Resume(Voice* voice);
// Stopped for good: no end callback.
void End(Voice* voice);
// `callback(user)` once the started wave has played (all its loops), from DeliverEnds.
void SetEndCallback(Voice* voice, EndCallback callback, void* user);
// Calls the end callbacks due at `now` (platform::Milliseconds), in the order the voices were made. The main loop
// calls it once a pass, right after reading the frame's time.
void DeliverEnds(std::uint32_t now);

// ---- The music stream ----

// Two halves played one after the other: the game fills whichever half is free (ReadyHalf) with the next buffer.
struct Stream;

// A stream of the open device; null when none is open.
Stream* CreateStream();
void DestroyStream(Stream* stream);
// The bytes a half should hold at least for the format to play without gaps.
int MinimumHalfBytes(unsigned rate, int channels, int bits);
// Stopped and empty, both halves free.
void ResetStream(Stream* stream);
// The format of the halves queued from now on (16 bits signed, else 8 bits unsigned).
void SetStreamFormat(Stream* stream, unsigned rate, int channels, int bits);
void SetStreamVolume(Stream* stream, int volume);   // 0..127
void SetStreamPan(Stream* stream, int pan);         // 0..127
// The half to fill next (0 or 1), or -1 while both wait to play.
int ReadyHalf(Stream* stream);
// Queues `bytes` of `data` in `half`; the memory stays the caller's until the half is ready again.
void QueueHalf(Stream* stream, int half, const void* data, int bytes);
// Paused; Resume goes on.
void StopStream(Stream* stream);
void ResumeStream(Stream* stream);

// ---- MP3 ----

// The music's decoder reads its file through `read`: `bytes` into `to`, from `offset` when it is not negative,
// else from where the last read ended. Returns the bytes read.
using ReadFn = int (*)(void* user, char* to, int bytes, int offset);
struct Mp3Stream;

// Opens the MP3 file `read` gives; null when it holds no MP3 frame.
Mp3Stream* OpenMp3(ReadFn read, void* user);
void CloseMp3(Mp3Stream* mp3);
// The decoded samples' format.
unsigned Mp3Rate(const Mp3Stream* mp3);
int Mp3Channels(const Mp3Stream* mp3);
int Mp3Bits(const Mp3Stream* mp3);   // 16
// Decodes up to `bytes` into `to`; the bytes written, fewer at the end of the file.
int DecodeMp3(Mp3Stream* mp3, void* to, int bytes);
// From the file's beginning again.
void RewindMp3(Mp3Stream* mp3);

// An MP3 file decoded whole into a 16-bit PCM RIFF WAVE (44-byte header) at the file's rate and channels, with every
// whole frame's samples (1152 a frame for MPEG-1, 576 for MPEG-2 and 2.5: what the original's decoder gave, so the
// voice's length is the same). False when it holds no MP3 frame.
bool DecodeMp3File(const void* data, std::size_t size, std::vector<std::uint8_t>* wave);

} // namespace audio
