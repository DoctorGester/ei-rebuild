// The silent audio backend (platform/audio.h): nothing is heard, but everything the game reads back runs as with a
// device. Voices end when their wave would have (WaveMilliseconds on platform::Milliseconds), the music's halves
// become free again as fast as they would play, and the MP3 decoders give silence of the file's length.
#include "platform/audio.h"
#include "platform/audio_formats.h"
#include "platform/clock.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string_view>

namespace platform::audio {

// ---- The device ----

namespace {
bool g_open = false;
} // namespace

bool OpenOutput(NativeWindow, unsigned, bool, std::string*) {
    g_open = true;
    return true;
}

void CloseOutput() { g_open = false; }

// ---- Voices ----

struct Voice {
    WaveInfo wave;
    int loops = 1;
    std::int32_t position = 0;   // bytes into the data where Start plays from
    int volume = 127;
    int pan = 64;
    bool running = false;        // started, its end not yet delivered
    bool stopped = false;        // paused while running: `left` is what remains
    bool forever = false;
    std::uint32_t startedAt = 0;
    std::uint32_t end = 0;
    std::uint32_t left = 0;
    EndCallback callback = nullptr;
    void* user = nullptr;
};

namespace {

std::vector<Voice*> g_voices;   // in the order they were made

// Bytes a second of the voice's wave, at least 1.
std::uint64_t BytesPerSecond(const WaveInfo& wave) { return std::max<std::uint32_t>(1, WaveBytesPerSecond(wave)); }

// The milliseconds the voice still plays from its position, all its loops.
std::uint32_t Remaining(const Voice& v) {
    const std::uint32_t total = WaveMilliseconds(v.wave);
    const std::uint32_t current =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(std::max(v.position, 0)) * 1000 / BytesPerSecond(v.wave));
    std::uint32_t rest = total > current ? total - current : 0;
    if (v.loops > 1) rest += total * static_cast<std::uint32_t>(v.loops - 1);
    return rest;
}

void StartTimer(Voice& v) {
    v.running = true;
    v.stopped = false;
    v.forever = v.loops == 0;
    v.startedAt = Milliseconds();
    v.end = v.startedAt + Remaining(v);
}

} // namespace

Voice* CreateVoice() {
    if (!g_open) return nullptr;
    auto* voice = new Voice;
    g_voices.push_back(voice);
    return voice;
}

void DestroyVoice(Voice* voice) {
    std::erase(g_voices, voice);
    delete voice;
}

void ResetVoice(Voice* voice) {
    *voice = Voice{};
}

void SetWave(Voice* voice, const void* wave, std::size_t) {
    voice->wave = ReadWaveInfo(wave);
    voice->position = 0;
}

void SetLoopCount(Voice* voice, int count) { voice->loops = count; }
void SetVolume(Voice* voice, int volume) { voice->volume = volume; }
void SetPan(Voice* voice, int pan) { voice->pan = pan; }
void SetReverb(Voice*, float, float, float) {}

void SetPositionMs(Voice* voice, std::uint32_t ms) {
    SetPosition(voice, static_cast<std::int32_t>(static_cast<std::uint64_t>(ms) * BytesPerSecond(voice->wave) / 1000));
}

// A voice moved while it plays goes on from there.
void SetPosition(Voice* voice, std::int32_t bytes) {
    voice->position = bytes;
    if (voice->running && !voice->stopped) StartTimer(*voice);
}

std::int32_t Position(const Voice* voice) {
    if (!voice->running || voice->stopped || voice->wave.dataLength == 0) return voice->position;
    const std::uint64_t played = static_cast<std::uint64_t>(Milliseconds() - voice->startedAt) * BytesPerSecond(voice->wave) / 1000;
    return static_cast<std::int32_t>((voice->position + played) % voice->wave.dataLength);
}

void Start(Voice* voice) { StartTimer(*voice); }

void Stop(Voice* voice) {
    if (!voice->running || voice->stopped) return;
    voice->stopped = true;
    const std::uint32_t now = Milliseconds();
    voice->left = static_cast<std::int32_t>(voice->end - now) > 0 ? voice->end - now : 0;
}

void Resume(Voice* voice) {
    if (!voice->running || !voice->stopped) return;
    voice->stopped = false;
    voice->end = Milliseconds() + voice->left;
}

void End(Voice* voice) { voice->running = false; }

void SetEndCallback(Voice* voice, EndCallback callback, void* user) {
    voice->callback = callback;
    voice->user = user;
}

void DeliverEnds(std::uint32_t now) {
    std::vector<Voice*> due;
    for (Voice* v : g_voices) {
        if (!v->running || v->stopped || v->forever || static_cast<std::int32_t>(now - v->end) < 0) continue;
        v->running = false;
        if (v->callback) due.push_back(v);
    }
    for (Voice* v : due) v->callback(v->user);
}

// ---- The music stream ----

// The halves' playing is a clock: the one playing started at `playStart` and lasts its `durations` ms.
struct Stream {
    std::mutex lock;   // the stream thread fills it; the main thread sets its volume and pauses it
    unsigned rate = 44100;
    int channels = 2;
    int bits = 16;
    int volume = 127;
    int pan = 64;
    bool queued[2] = {};
    std::uint32_t durations[2] = {};
    int playing = -1;
    int next = 0;                 // the half ReadyHalf offers
    std::uint32_t playStart = 0;
    bool stopped = false;
    std::uint32_t stoppedAt = 0;
};

namespace {

// The halves played out by `now` freed; the other one, when queued, plays on from where the last ended.
void Play(Stream& s, std::uint32_t now) {
    if (s.stopped) return;
    while (s.playing >= 0 && now - s.playStart >= s.durations[s.playing]) {
        s.playStart += s.durations[s.playing];
        s.queued[s.playing] = false;
        const int other = 1 - s.playing;
        s.playing = s.queued[other] ? other : -1;
    }
}

} // namespace

Stream* CreateStream() { return g_open ? new Stream : nullptr; }

void DestroyStream(Stream* stream) { delete stream; }

int MinimumHalfBytes(unsigned rate, int channels, int bits) {
    // 87 ms rounded up to 512 bytes: what Miles asked for 44.1 kHz 16-bit stereo through the wave mapper.
    const std::uint64_t bytes = static_cast<std::uint64_t>(rate) * channels * bits / 8 * 87 / 1000;
    return static_cast<int>((bytes + 511) & ~std::uint64_t{511});
}

void ResetStream(Stream* stream) {
    std::lock_guard hold(stream->lock);
    stream->queued[0] = stream->queued[1] = false;
    stream->playing = -1;
    stream->next = 0;
    stream->stopped = false;
}

void SetStreamFormat(Stream* stream, unsigned rate, int channels, int bits) {
    std::lock_guard hold(stream->lock);
    stream->rate = rate;
    stream->channels = channels;
    stream->bits = bits;
}

void SetStreamVolume(Stream* stream, int volume) { stream->volume = volume; }
void SetStreamPan(Stream* stream, int pan) { stream->pan = pan; }

int ReadyHalf(Stream* stream) {
    std::lock_guard hold(stream->lock);
    Play(*stream, Milliseconds());
    return stream->queued[stream->next] ? -1 : stream->next;
}

void QueueHalf(Stream* stream, int half, const void*, int bytes) {
    std::lock_guard hold(stream->lock);
    const std::uint32_t now = Milliseconds();
    Play(*stream, now);
    const std::uint64_t perSecond = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(stream->rate) * stream->channels * stream->bits / 8);
    stream->durations[half] = static_cast<std::uint32_t>(std::max<std::uint64_t>(1, static_cast<std::uint64_t>(bytes) * 1000 / perSecond));
    stream->queued[half] = true;
    stream->next = 1 - half;
    if (stream->playing < 0) {
        stream->playing = half;
        stream->playStart = now;
    }
}

void StopStream(Stream* stream) {
    std::lock_guard hold(stream->lock);
    if (stream->stopped) return;
    Play(*stream, Milliseconds());
    stream->stopped = true;
    stream->stoppedAt = Milliseconds();
}

void ResumeStream(Stream* stream) {
    std::lock_guard hold(stream->lock);
    if (!stream->stopped) return;
    stream->stopped = false;
    stream->playStart += Milliseconds() - stream->stoppedAt;
}

// ---- MP3 ----

struct Mp3Stream {
    Mp3Layout layout;
    std::uint64_t bytes = 0;   // the decoded file's
    std::uint64_t given = 0;
};

namespace {
constexpr int kReadStep = 0x10000;
} // namespace

Mp3Stream* OpenMp3(ReadFn read, void* user) {
    std::vector<char> file;
    for (int offset = 0;; offset = -1) {
        const std::size_t at = file.size();
        file.resize(at + kReadStep);
        const int got = read(user, file.data() + at, kReadStep, offset);
        file.resize(at + static_cast<std::size_t>(std::max(got, 0)));
        if (got <= 0) break;
    }
    auto mp3 = std::make_unique<Mp3Stream>();
    mp3->layout = ScanMp3(file.data(), file.size());
    if (mp3->layout.frames == 0) return nullptr;
    mp3->bytes = static_cast<std::uint64_t>(mp3->layout.samples) * mp3->layout.channels * 2;
    return mp3.release();
}

void CloseMp3(Mp3Stream* mp3) { delete mp3; }
unsigned Mp3Rate(const Mp3Stream* mp3) { return mp3->layout.rate; }
int Mp3Channels(const Mp3Stream* mp3) { return mp3->layout.channels; }
int Mp3Bits(const Mp3Stream*) { return 16; }

int DecodeMp3(Mp3Stream* mp3, void* to, int bytes) {
    const auto n = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max(bytes, 0)), mp3->bytes - mp3->given));
    std::fill_n(static_cast<std::uint8_t*>(to), n, std::uint8_t{0});
    mp3->given += static_cast<std::uint64_t>(n);
    return n;
}

void RewindMp3(Mp3Stream* mp3) { mp3->given = 0; }

namespace {
void Put16(std::uint8_t* p, unsigned v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void Put32(std::uint8_t* p, std::uint32_t v) {
    Put16(p, v & 0xFFFF);
    Put16(p + 2, v >> 16);
}
void PutTag(std::uint8_t* p, std::string_view tag) { std::copy(tag.begin(), tag.end(), p); }
} // namespace

bool DecodeMp3File(const void* data, std::size_t size, std::vector<std::uint8_t>* wave) {
    const Mp3Layout layout = ScanMp3(data, size);
    if (layout.frames == 0) return false;
    const unsigned frameBytes = static_cast<unsigned>(layout.channels) * 2;
    const std::uint32_t dataBytes = layout.samples * frameBytes;
    wave->assign(44 + static_cast<std::size_t>(dataBytes), 0);
    std::uint8_t* p = wave->data();
    PutTag(p, "RIFF");
    Put32(p + 4, 36 + dataBytes);
    PutTag(p + 8, "WAVEfmt ");
    Put32(p + 16, 16);
    Put16(p + 20, static_cast<unsigned>(kPcm));
    Put16(p + 22, static_cast<unsigned>(layout.channels));
    Put32(p + 24, layout.rate);
    Put32(p + 28, layout.rate * frameBytes);
    Put16(p + 32, frameBytes);
    Put16(p + 34, 16);
    PutTag(p + 36, "data");
    Put32(p + 40, dataBytes);
    return true;
}

} // namespace platform::audio
