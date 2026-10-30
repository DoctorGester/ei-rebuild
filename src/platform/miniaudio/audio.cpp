// The audio backend on miniaudio (platform/audio.h; third_party/miniaudio): one output device, the voices' waves
// decoded by dr_wav and the music's halves mixed into it at their volume and pan, the cave voices through a reverb,
// the MP3 files decoded by dr_mp3.
// What the game reads back keeps to the game's clock whatever the device does: a voice ends when its wave would have
// (WaveMilliseconds on platform::Milliseconds, delivered by DeliverEnds). The music's halves become free as the device
// plays them; without a device (UseNoDevice, or none could be opened) as fast as they would play on the game's clock.
#include "platform/audio.h"
#include "platform/audio_formats.h"
#include "platform/clock.h"
#include "platform/debug.h"
#include "platform/miniaudio/miniaudio_setup.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

namespace platform::audio {

namespace {

constexpr int kOutputChannels = 2;
constexpr unsigned kDefaultRate = 44100;
constexpr float kSampleScale = 1.0f / 32768.0f;

// ---- Mixing ----

// What a source's left and right sample are multiplied by: the volume (0..127) linear, the pan (0 left .. 127 right)
// a balance that takes the far side down to nothing and keeps the near one, both full at 64, and `dry` (the part a
// voice keeps of itself next to its reverb).
struct Gains {
    float left = 1.0f;
    float right = 1.0f;
};

Gains GainsOf(int volume, int pan, float dry) {
    const float level = static_cast<float>(std::clamp(volume, 0, 127)) / 127.0f * dry;
    const int p = std::clamp(pan, 0, 127);
    const float left = p <= 64 ? 1.0f : static_cast<float>(127 - p) / 63.0f;
    const float right = p >= 64 ? 1.0f : static_cast<float>(p) / 64.0f;
    return {level * left, level * right};
}

// A source played at the output's rate: the output frame between the source's two frames `a` and `b` around the
// position, by straight lines (32 bits of fraction). At the output's own rate every frame comes out as it is.
struct Resampler {
    float a[2] = {};
    float b[2] = {};
    bool aLive = false;   // `a` is a frame of the source, not past its end
    bool bLive = false;
    std::uint32_t fraction = 0;

    // From the source's next frames; `fetch(frame)` gives one, false past the end.
    template <class Fetch>
    void Prime(Fetch fetch) {
        fraction = 0;
        aLive = fetch(a);
        bLive = aLive && fetch(b);
        if (!bLive) b[0] = b[1] = 0.0f;
    }
    void Frame(float out[2]) const {
        const float t = static_cast<float>(fraction) * (1.0f / 4294967296.0f);
        out[0] = a[0] + (b[0] - a[0]) * t;
        out[1] = a[1] + (b[1] - a[1]) * t;
    }
    // On by `step` source frames (32.32 fixed point); false once the source's last frame has played.
    template <class Fetch>
    bool Advance(std::uint64_t step, Fetch fetch) {
        const std::uint64_t to = static_cast<std::uint64_t>(fraction) + step;
        fraction = static_cast<std::uint32_t>(to);
        for (std::uint64_t whole = to >> 32; whole != 0 && aLive; --whole) {
            a[0] = b[0];
            a[1] = b[1];
            aLive = bLive;
            bLive = aLive && fetch(b);
            if (!bLive) b[0] = b[1] = 0.0f;
        }
        return aLive;
    }
};

// Source frames per output frame in 32.32 fixed point.
std::uint64_t Step(unsigned sourceRate, unsigned outputRate) {
    return (static_cast<std::uint64_t>(sourceRate) << 32) / std::max(1u, outputRate);
}

// ---- The reverb ----

// The voices' reverb (SetReverb), one network all of them feed: a Freeverb-style mono send into eight parallel
// damped combs and four series all-passes a side, the right side's delays a little longer (stereo spread). The room
// (0..1) scales the delays, the time is the tail's fall to -60 dB (each comb's feedback from its own delay). It runs
// on the mixer's thread under g_mix; with nothing sent for two tails' length it clears its lines and rests, so the
// lines never decay into denormals and an idle reverb costs nothing.
class Reverb {
public:
    // The lines for `rate`, long enough for the largest room.
    void Open(unsigned rate) {
        rate_ = rate;
        for (int side = 0; side < 2; ++side) {
            for (int i = 0; i < kCombs; ++i) combs_[side][i].Open(DelayAt(kCombDelays[i], side, 1.0f));
            for (int i = 0; i < kAllPasses; ++i) allPasses_[side][i].Open(DelayAt(kAllPassDelays[i], side, 1.0f));
        }
        send_.assign(4096, 0.0f);
        shaped_ = false;
        Clear();
    }

    // The room and the tail's time of the voices sent in from now on (the last SetReverb's: the game passes the same
    // to every voice). A new room moves the delays: the lines start empty.
    void Shape(float room, float time) {
        room = std::clamp(room, 0.0f, 1.0f);
        time = std::clamp(time, 0.1f, 20.0f);
        if (shaped_ && room == room_ && time == time_) return;
        if (!shaped_ || room != room_) {
            for (int side = 0; side < 2; ++side) {
                for (int i = 0; i < kCombs; ++i) combs_[side][i].Resize(DelayAt(kCombDelays[i], side, room));
                for (int i = 0; i < kAllPasses; ++i) allPasses_[side][i].Resize(DelayAt(kAllPassDelays[i], side, room));
            }
            Clear();
        }
        room_ = room;
        time_ = time;
        shaped_ = true;
        // Each pass round a comb loses delay / (time * rate) of the 60 dB.
        const float perFrame = -3.0f / (time * static_cast<float>(rate_));
        for (int side = 0; side < 2; ++side)
            for (Comb& comb : combs_[side])
                comb.feedback = std::pow(10.0f, perFrame * static_cast<float>(comb.Length()));
        tailFrames_ = static_cast<std::uint32_t>(2.0f * time * static_cast<float>(rate_)) + DelayAt(kCombDelays[kCombs - 1], 1, 1.0f);
    }

    // The send for a mix of `frames`, silent: the voices add to it.
    float* Send(std::uint32_t frames) {
        if (send_.size() < frames) send_.resize(frames);
        std::fill_n(send_.data(), frames, 0.0f);
        return send_.data();
    }

    // The send through the network, added to `out` (interleaved stereo).
    void Process(float* out, std::uint32_t frames) {
        bool sent = false;
        for (std::uint32_t i = 0; i < frames && !sent; ++i) sent = send_[i] != 0.0f;
        if (!sent) {
            if (resting_) return;
            quiet_ += frames;
            if (quiet_ >= tailFrames_) {
                Clear();
                return;
            }
        } else {
            quiet_ = 0;
            resting_ = false;
        }
        for (std::uint32_t i = 0; i < frames; ++i) {
            const float in = send_[i] * kInputGain;
            for (int side = 0; side < 2; ++side) {
                float wet = 0.0f;
                for (Comb& comb : combs_[side]) wet += comb.Process(in);
                for (AllPass& pass : allPasses_[side]) wet = pass.Process(wet);
                out[2 * i + side] += wet;
            }
        }
    }

private:
    // A delay line's samples and where it reads and writes.
    struct Line {
        std::vector<float> samples;
        std::uint32_t length = 1;
        std::uint32_t at = 0;

        void Open(std::uint32_t capacity) { samples.assign(capacity, 0.0f); }
        void Resize(std::uint32_t frames) {
            length = std::clamp<std::uint32_t>(frames, 1, static_cast<std::uint32_t>(samples.size()));
            at = 0;
        }
        void Clear() {
            std::fill(samples.begin(), samples.end(), 0.0f);
            at = 0;
        }
        float Read() const { return samples[at]; }
        void Write(float value) {
            samples[at] = value;
            if (++at == length) at = 0;
        }
    };
    // A feedback comb with a one-pole low-pass in its loop (the walls take the highs first).
    struct Comb {
        Line line;
        float feedback = 0.0f;
        float low = 0.0f;

        void Open(std::uint32_t capacity) { line.Open(capacity); }
        void Resize(std::uint32_t frames) { line.Resize(frames); }
        std::uint32_t Length() const { return line.length; }
        void Clear() {
            line.Clear();
            low = 0.0f;
        }
        float Process(float in) {
            const float out = line.Read();
            low = out + (low - out) * kDamping;
            line.Write(in + low * feedback);
            return out;
        }
    };
    // Schroeder's all-pass (Freeverb's form): it smears the combs' echoes into a dense tail.
    struct AllPass {
        Line line;

        void Open(std::uint32_t capacity) { line.Open(capacity); }
        void Resize(std::uint32_t frames) { line.Resize(frames); }
        void Clear() { line.Clear(); }
        float Process(float in) {
            const float delayed = line.Read();
            line.Write(in + delayed * 0.5f);
            return delayed - in;
        }
    };

    static constexpr int kCombs = 8;
    static constexpr int kAllPasses = 4;
    // Freeverb's tunings at 44.1 kHz (frames); the right side's are kSpread longer.
    static constexpr std::uint32_t kCombDelays[kCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr std::uint32_t kAllPassDelays[kAllPasses] = {556, 441, 341, 225};
    static constexpr std::uint32_t kSpread = 23;
    static constexpr float kDamping = 0.1f;
    // The send's level into the combs: a long broadband sound's tail comes out about 6 dB under the sound.
    static constexpr float kInputGain = 0.009f;

    // A tuning's delay at the output's rate in a room (0 the smallest: half the tuning, 1 one and a half).
    std::uint32_t DelayAt(std::uint32_t tuning, int side, float room) const {
        const float frames = static_cast<float>(tuning + (side != 0 ? kSpread : 0)) * (0.5f + room) *
                             static_cast<float>(rate_) / static_cast<float>(kDefaultRate);
        return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(frames));
    }

    void Clear() {
        for (int side = 0; side < 2; ++side) {
            for (Comb& comb : combs_[side]) comb.Clear();
            for (AllPass& pass : allPasses_[side]) pass.Clear();
        }
        quiet_ = 0;
        resting_ = true;
    }

    unsigned rate_ = kDefaultRate;
    Comb combs_[2][kCombs];
    AllPass allPasses_[2][kAllPasses];
    std::vector<float> send_;
    float room_ = 0.0f;
    float time_ = 0.0f;
    bool shaped_ = false;
    bool resting_ = true;            // the lines are empty: nothing to run until something is sent
    std::uint32_t quiet_ = 0;        // frames since the send last carried anything
    std::uint32_t tailFrames_ = 0;   // two tails' fall (-120 dB) and the longest delay
};

// The part of itself a voice gives up to the reverb at level 1 (Sound::UpdateGains).
constexpr float kReverbDryCut = 0.35f;

// ---- The device ----

bool g_open = false;
bool g_noDevice = false;
unsigned g_rate = kDefaultRate;
std::unique_ptr<ma_device> g_device;
// The device calls the mixer: the halves free as it plays them. False without one, or once it stopped by itself.
std::atomic<bool> g_deviceLive{false};
// Guards everything the device's thread reads: the voices' sounds, the streams, the lists of both.
std::mutex g_mix;
// What the device has been given, for the developer's log at the close.
unsigned g_voicesStarted = 0;
float g_peak = 0.0f;
bool g_heard = false;
Reverb g_reverb;   // under g_mix

void Log(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    DebugOutput(text);
}

void Mix(float* out, std::uint32_t frames);

void DeviceData(ma_device*, void* output, const void*, ma_uint32 frames) { Mix(static_cast<float*>(output), frames); }

void DeviceNotification(const ma_device_notification* notification) {
    if (notification->type == ma_device_notification_type_started) g_deviceLive = true;
    if (notification->type == ma_device_notification_type_stopped) g_deviceLive = false;
}

} // namespace

void UseNoDevice() { g_noDevice = true; }

bool OpenOutput(NativeWindow, unsigned rate, bool, std::string*) {
    g_open = true;
    g_rate = rate != 0 ? rate : kDefaultRate;
    {
        std::lock_guard hold(g_mix);
        g_reverb.Open(g_rate);
    }
    if (g_noDevice) return true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kOutputChannels;
    config.sampleRate = g_rate;
    config.dataCallback = DeviceData;
    config.notificationCallback = DeviceNotification;
    g_device = std::make_unique<ma_device>();
    ma_result result = ma_device_init(nullptr, &config, g_device.get());
    if (result == MA_SUCCESS) {
        result = ma_device_start(g_device.get());
        if (result != MA_SUCCESS) ma_device_uninit(g_device.get());
    }
    if (result != MA_SUCCESS) {
        // The game goes on as with a device, silently.
        g_device.reset();
        Log("audio: no output device (%s): silent\n", ma_result_description(result));
        return true;
    }
    g_deviceLive = true;
    Log("audio: %s through %s, %u Hz\n", g_device->playback.name, ma_get_backend_name(g_device->pContext->backend), g_rate);
    return true;
}

void CloseOutput() {
    if (g_device) {
        ma_device_uninit(g_device.get());
        g_device.reset();
        Log("audio: closed; %u voices started, the mix's peak %.3f\n", g_voicesStarted, static_cast<double>(g_peak));
    }
    g_deviceLive = false;
    g_open = false;
}

// ---- Voices ----

namespace {

// A voice as the device plays it (under g_mix).
struct Sound {
    std::vector<std::int16_t> samples;   // the wave decoded, interleaved
    int channels = 1;
    unsigned rate = 0;
    std::uint32_t frames = 0;
    std::uint32_t next = 0;    // the frame Fetch gives next
    int loopsLeft = 1;         // the plays still to come with this one, 0 for ever
    bool playing = false;
    bool paused = false;
    bool primed = false;       // the resampler holds the frames at `next`
    int volume = 127;
    int pan = 64;
    Gains gains;               // what the volume and pan ask for
    Gains applied;             // what the last mix ended on: the next goes there smoothly
    float reverb = 0.0f;       // the level it feeds the reverb at, 0 for none
    Resampler resampler;

    // The gains for the volume, the pan and the reverb: a voice in the reverb keeps less of itself (kReverbDryCut at
    // level 1), so a loud one and its tail stay under full scale.
    void UpdateGains() { gains = GainsOf(volume, pan, 1.0f - kReverbDryCut * reverb); }
    bool Fetch(float frame[2]) {
        if (next >= frames) {
            if (frames == 0 || loopsLeft == 1) return false;
            if (loopsLeft > 1) --loopsLeft;
            next = 0;
        }
        const std::int16_t* f = &samples[static_cast<std::size_t>(next) * channels];
        frame[0] = static_cast<float>(f[0]) * kSampleScale;
        frame[1] = channels > 1 ? static_cast<float>(f[1]) * kSampleScale : frame[0];
        ++next;
        return true;
    }
};

// A file in memory opened by miniaudio's decoder for `format` (dr_wav, dr_mp3): 16-bit samples at the file's rate
// and channels.
bool OpenDecoder(const void* data, std::size_t size, ma_encoding_format format, ma_decoder* decoder) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    config.encodingFormat = format;
    return data != nullptr && ma_decoder_init_memory(data, size, &config, decoder) == MA_SUCCESS;
}

// A RIFF WAVE decoded to 16-bit samples by dr_wav (PCM and IMA ADPCM), at most `samples` frames (WaveInfo's: dr_wav
// decodes IMA ADPCM's last block whole, past the fact chunk's count); no frames when it cannot.
void Decode(const void* wave, std::size_t size, std::uint32_t samples, Sound* sound) {
    ma_decoder decoder;
    if (!OpenDecoder(wave, size, ma_encoding_format_wav, &decoder)) return;
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frames) == MA_SUCCESS && decoder.outputChannels > 0 &&
        frames < (1u << 28)) {
        frames = std::min<ma_uint64>(frames, samples);
        const auto channels = static_cast<int>(decoder.outputChannels);
        sound->channels = channels;
        sound->rate = decoder.outputSampleRate;
        sound->samples.resize(static_cast<std::size_t>(frames) * channels);
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&decoder, sound->samples.data(), frames, &got);
        sound->frames = static_cast<std::uint32_t>(got);
        sound->samples.resize(static_cast<std::size_t>(got) * channels);
    }
    ma_decoder_uninit(&decoder);
}

// The frame `bytes` into the wave's data points at.
std::uint32_t FrameAt(const WaveInfo& wave, std::int32_t bytes) {
    if (bytes <= 0 || wave.blockSize == 0 || wave.channels <= 0) return 0;
    const auto at = static_cast<std::uint32_t>(bytes);
    if (wave.format != kImaAdpcm) return at / wave.blockSize;
    // A block: 4 bytes of header a channel (its first sample), then two samples a byte.
    const auto channels = static_cast<std::uint32_t>(wave.channels);
    const std::uint32_t header = 4 * channels;
    if (wave.blockSize <= header) return 0;
    const std::uint32_t perBlock = (wave.blockSize - header) * 2 / channels + 1;
    const std::uint32_t rest = at % wave.blockSize;
    return at / wave.blockSize * perBlock + (rest > header ? (rest - header) * 2 / channels : 0);
}

} // namespace

struct Voice {
    // What the game reads back, on the game's clock (the main thread's).
    WaveInfo wave;
    int loops = 1;
    std::int32_t position = 0;   // bytes into the data where Start plays from
    bool running = false;        // started, its end not yet delivered
    bool stopped = false;        // paused while running: `left` is what remains
    bool forever = false;
    std::uint32_t startedAt = 0;
    std::uint32_t end = 0;
    std::uint32_t left = 0;
    EndCallback callback = nullptr;
    void* user = nullptr;
    // What the device plays.
    Sound sound;
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

// The device plays the voice's wave from its position, with its loops.
void StartSound(Voice& v) {
    std::lock_guard hold(g_mix);
    Sound& s = v.sound;
    s.next = FrameAt(v.wave, v.position);
    s.loopsLeft = v.loops;
    s.playing = true;
    s.paused = false;
    s.primed = false;
}

} // namespace

Voice* CreateVoice() {
    if (!g_open) return nullptr;
    auto* voice = new Voice;
    std::lock_guard hold(g_mix);
    g_voices.push_back(voice);
    return voice;
}

void DestroyVoice(Voice* voice) {
    {
        std::lock_guard hold(g_mix);
        std::erase(g_voices, voice);
    }
    delete voice;
}

void ResetVoice(Voice* voice) {
    std::lock_guard hold(g_mix);
    *voice = Voice{};
}

void SetWave(Voice* voice, const void* wave, std::size_t size) {
    voice->wave = ReadWaveInfo(wave);
    voice->position = 0;
    Sound decoded;
    Decode(wave, size, voice->wave.samples, &decoded);
    std::lock_guard hold(g_mix);
    Sound& s = voice->sound;
    s.samples = std::move(decoded.samples);
    s.channels = decoded.channels;
    s.rate = decoded.rate;
    s.frames = decoded.frames;
    s.next = 0;
    s.playing = false;
}

void SetLoopCount(Voice* voice, int count) { voice->loops = count; }

void SetVolume(Voice* voice, int volume) {
    std::lock_guard hold(g_mix);
    voice->sound.volume = volume;
    voice->sound.UpdateGains();
}

void SetPan(Voice* voice, int pan) {
    std::lock_guard hold(g_mix);
    voice->sound.pan = pan;
    voice->sound.UpdateGains();
}

// The voice feeds the reverb at `level` (0..1) while `room` and `level` are above 0: the game sets room 0 when the
// party leaves a cave, the level and time kept. The room and time shape the one reverb every voice shares.
void SetReverb(Voice* voice, float room, float level, float time) {
    std::lock_guard hold(g_mix);
    const bool on = room > 0.0f && level > 0.0f;
    voice->sound.reverb = on ? std::min(level, 1.0f) : 0.0f;
    voice->sound.UpdateGains();
    if (on) g_reverb.Shape(room, time);
}

void SetPositionMs(Voice* voice, std::uint32_t ms) {
    SetPosition(voice, static_cast<std::int32_t>(static_cast<std::uint64_t>(ms) * BytesPerSecond(voice->wave) / 1000));
}

// A voice moved while it plays goes on from there.
void SetPosition(Voice* voice, std::int32_t bytes) {
    voice->position = bytes;
    if (voice->running && !voice->stopped) StartTimer(*voice);
    std::lock_guard hold(g_mix);
    voice->sound.next = FrameAt(voice->wave, bytes);
    voice->sound.primed = false;
}

std::int32_t Position(const Voice* voice) {
    if (!voice->running || voice->stopped || voice->wave.dataLength == 0) return voice->position;
    const std::uint64_t played = static_cast<std::uint64_t>(Milliseconds() - voice->startedAt) * BytesPerSecond(voice->wave) / 1000;
    return static_cast<std::int32_t>((voice->position + played) % voice->wave.dataLength);
}

void Start(Voice* voice) {
    StartTimer(*voice);
    StartSound(*voice);
    if (g_voicesStarted++ == 0 && g_device) Log("audio: the first voice plays\n");
}

void Stop(Voice* voice) {
    {
        std::lock_guard hold(g_mix);
        voice->sound.paused = true;
    }
    if (!voice->running || voice->stopped) return;
    voice->stopped = true;
    const std::uint32_t now = Milliseconds();
    voice->left = static_cast<std::int32_t>(voice->end - now) > 0 ? voice->end - now : 0;
}

void Resume(Voice* voice) {
    {
        std::lock_guard hold(g_mix);
        voice->sound.paused = false;
    }
    if (!voice->running || !voice->stopped) return;
    voice->stopped = false;
    voice->end = Milliseconds() + voice->left;
}

void End(Voice* voice) {
    voice->running = false;
    std::lock_guard hold(g_mix);
    voice->sound.playing = false;
}

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

namespace {

// A queued half: its bytes (the caller's) and their format.
struct Half {
    const std::uint8_t* data = nullptr;
    std::uint32_t frames = 0;
    std::uint32_t next = 0;   // the frame played next
    unsigned rate = kDefaultRate;
    int channels = 2;
    int bits = 16;
    bool queued = false;
};

} // namespace

// Two halves played one after the other (under g_mix). Without a device the playing half advances on the game's clock
// (`pumpedAt`, the milliseconds times the rate kept to the frame in `carry`).
struct Stream {
    unsigned rate = kDefaultRate;   // the format of the halves queued from now on
    int channels = 2;
    int bits = 16;
    int volume = 127;
    int pan = 64;
    Gains gains;
    Gains applied;
    Half halves[2];
    int playing = -1;
    int next = 0;                   // the half ReadyHalf offers
    bool stopped = false;
    std::uint32_t pumpedAt = 0;
    std::uint64_t carry = 0;
    Resampler resampler;
    bool primed = false;

    // The playing half done with: the other one plays when it is queued.
    void Finish() {
        halves[playing].queued = false;
        const int other = 1 - playing;
        playing = halves[other].queued ? other : -1;
    }
    // The next frame of the halves; silence while none is queued (a stream never ends).
    bool Fetch(float frame[2]) {
        while (playing >= 0 && halves[playing].next >= halves[playing].frames) Finish();
        if (playing < 0) {
            frame[0] = frame[1] = 0.0f;
            return true;
        }
        Half& h = halves[playing];
        const std::size_t at = static_cast<std::size_t>(h.next) * h.channels;
        for (int c = 0; c < 2; ++c) {
            const std::size_t i = at + static_cast<std::size_t>(std::min(c, h.channels - 1));
            if (h.bits == 16) {
                std::int16_t sample;
                std::memcpy(&sample, h.data + i * 2, sizeof(sample));
                frame[c] = static_cast<float>(sample) * kSampleScale;
            } else {
                frame[c] = static_cast<float>(h.data[i] - 128) * (1.0f / 128.0f);
            }
        }
        ++h.next;
        return true;
    }
    // Without a device, the frames the game's clock says have played by `now`, skipped.
    void Pump(std::uint32_t now) {
        if (stopped) return;
        if (g_deviceLive || playing < 0) {
            pumpedAt = now;
            carry = 0;
            return;
        }
        carry += static_cast<std::uint64_t>(now - pumpedAt) * halves[playing].rate;
        pumpedAt = now;
        std::uint64_t frames = carry / 1000;
        carry %= 1000;
        while (playing >= 0) {
            Half& h = halves[playing];
            const std::uint64_t take = std::min<std::uint64_t>(frames, h.frames - h.next);
            h.next += static_cast<std::uint32_t>(take);
            frames -= take;
            if (h.next < h.frames) break;
            Finish();
        }
    }
};

namespace {
std::vector<Stream*> g_streams;
} // namespace

Stream* CreateStream() {
    if (!g_open) return nullptr;
    auto* stream = new Stream;
    std::lock_guard hold(g_mix);
    g_streams.push_back(stream);
    return stream;
}

void DestroyStream(Stream* stream) {
    {
        std::lock_guard hold(g_mix);
        std::erase(g_streams, stream);
    }
    delete stream;
}

int MinimumHalfBytes(unsigned rate, int channels, int bits) {
    // 87 ms rounded up to 512 bytes: what Miles asked for 44.1 kHz 16-bit stereo through the wave mapper.
    const std::uint64_t bytes = static_cast<std::uint64_t>(rate) * channels * bits / 8 * 87 / 1000;
    return static_cast<int>((bytes + 511) & ~std::uint64_t{511});
}

void ResetStream(Stream* stream) {
    std::lock_guard hold(g_mix);
    stream->halves[0].queued = stream->halves[1].queued = false;
    stream->playing = -1;
    stream->next = 0;
    stream->stopped = false;
}

void SetStreamFormat(Stream* stream, unsigned rate, int channels, int bits) {
    std::lock_guard hold(g_mix);
    stream->rate = rate;
    stream->channels = channels;
    stream->bits = bits;
}

void SetStreamVolume(Stream* stream, int volume) {
    std::lock_guard hold(g_mix);
    stream->volume = volume;
    stream->gains = GainsOf(volume, stream->pan, 1.0f);
}

void SetStreamPan(Stream* stream, int pan) {
    std::lock_guard hold(g_mix);
    stream->pan = pan;
    stream->gains = GainsOf(stream->volume, pan, 1.0f);
}

int ReadyHalf(Stream* stream) {
    std::lock_guard hold(g_mix);
    stream->Pump(Milliseconds());
    return stream->halves[stream->next].queued ? -1 : stream->next;
}

void QueueHalf(Stream* stream, int half, const void* data, int bytes) {
    std::lock_guard hold(g_mix);
    stream->Pump(Milliseconds());
    Half& h = stream->halves[half];
    const int frameBytes = std::max(1, stream->channels * stream->bits / 8);
    h.data = static_cast<const std::uint8_t*>(data);
    h.frames = static_cast<std::uint32_t>(std::max(bytes, 0) / frameBytes);
    h.next = 0;
    h.rate = stream->rate;
    h.channels = std::max(1, stream->channels);
    h.bits = stream->bits;
    h.queued = true;
    stream->next = 1 - half;
    if (stream->playing < 0) stream->playing = half;
}

void StopStream(Stream* stream) {
    std::lock_guard hold(g_mix);
    stream->Pump(Milliseconds());
    stream->stopped = true;
}

void ResumeStream(Stream* stream) {
    std::lock_guard hold(g_mix);
    if (!stream->stopped) return;
    stream->stopped = false;
    stream->pumpedAt = Milliseconds();
}

// ---- The mixer ----

namespace {

// `frames` of the source into `out` (interleaved stereo) at its gains, going from `from` to `to` over the frames, and
// into `send` (mono, the reverb's) at `sendLevel` when that is above 0. False once the source has ended.
template <class Fetch>
bool Render(Resampler& resampler, bool& primed, std::uint64_t step, Gains from, Gains to, float* out, float* send,
            float sendLevel, std::uint32_t frames, Fetch fetch) {
    if (!primed) {
        resampler.Prime(fetch);
        primed = true;
    }
    const float slope = 1.0f / static_cast<float>(std::max<std::uint32_t>(frames, 1));
    const float sendHalf = sendLevel * 0.5f;
    for (std::uint32_t i = 0; i < frames; ++i) {
        if (!resampler.aLive) return false;
        const float t = static_cast<float>(i) * slope;
        float frame[2];
        resampler.Frame(frame);
        const float left = frame[0] * (from.left + (to.left - from.left) * t);
        const float right = frame[1] * (from.right + (to.right - from.right) * t);
        out[2 * i] += left;
        out[2 * i + 1] += right;
        if (sendHalf > 0.0f) send[i] += (left + right) * sendHalf;
        resampler.Advance(step, fetch);
    }
    return resampler.aLive;
}

void Mix(float* out, std::uint32_t frames) {
    std::fill_n(out, static_cast<std::size_t>(frames) * kOutputChannels, 0.0f);
    std::lock_guard hold(g_mix);
    float* send = g_reverb.Send(frames);
    for (Voice* v : g_voices) {
        Sound& s = v->sound;
        if (!s.playing || s.paused) continue;
        if (!s.primed) s.applied = s.gains;
        const auto fetch = [&s](float frame[2]) { return s.Fetch(frame); };
        if (!Render(s.resampler, s.primed, Step(s.rate, g_rate), s.applied, s.gains, out, send, s.reverb, frames, fetch))
            s.playing = false;
        s.applied = s.gains;
    }
    for (Stream* stream : g_streams) {
        if (stream->stopped) continue;
        if (!stream->primed) stream->applied = stream->gains;
        const unsigned rate = stream->playing >= 0 ? stream->halves[stream->playing].rate : stream->rate;
        const auto fetch = [stream](float frame[2]) { return stream->Fetch(frame); };
        Render(stream->resampler, stream->primed, Step(rate, g_rate), stream->applied, stream->gains, out, send, 0.0f, frames,
               fetch);
        stream->applied = stream->gains;
    }
    g_reverb.Process(out, frames);
    float peak = g_peak;
    for (std::size_t i = 0; i < static_cast<std::size_t>(frames) * kOutputChannels; ++i) {
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
        peak = std::max(peak, std::fabs(out[i]));
    }
    if (peak > 0.0f && !g_heard && g_device) {
        g_heard = true;
        Log("audio: the first sound reaches the device\n");
    }
    g_peak = peak;
}

} // namespace

// ---- MP3 ----

struct Mp3Stream {
    std::vector<char> file;
    Mp3Layout layout;
    ma_decoder decoder;
    bool decoding = false;           // dr_mp3 opened the file with the layout's channels
    std::uint64_t given = 0;         // frames
    std::vector<std::int16_t> samples;
};

namespace {

constexpr int kReadStep = 0x10000;

// dr_mp3 on the MP3 file in memory, when it decodes the layout's channels.
bool OpenMp3Decoder(const void* data, std::size_t size, const Mp3Layout& layout, ma_decoder* decoder) {
    if (!OpenDecoder(data, size, ma_encoding_format_mp3, decoder)) return false;
    if (decoder->outputChannels == static_cast<ma_uint32>(layout.channels)) return true;
    ma_decoder_uninit(decoder);
    return false;
}

// `frames` frames of `decoder` into `to` (16-bit, `channels` a frame), silence where it gives fewer.
void DecodeFrames(ma_decoder* decoder, bool decoding, int channels, std::uint64_t frames, std::vector<std::int16_t>* samples,
                  std::uint8_t* to) {
    samples->assign(static_cast<std::size_t>(frames) * channels, 0);
    if (decoding) ma_decoder_read_pcm_frames(decoder, samples->data(), frames, nullptr);
    std::memcpy(to, samples->data(), samples->size() * sizeof(std::int16_t));
}

} // namespace

Mp3Stream* OpenMp3(ReadFn read, void* user) {
    auto mp3 = std::make_unique<Mp3Stream>();
    std::vector<char>& file = mp3->file;
    for (int offset = 0;; offset = -1) {
        const std::size_t at = file.size();
        file.resize(at + kReadStep);
        const int got = read(user, file.data() + at, kReadStep, offset);
        file.resize(at + static_cast<std::size_t>(std::max(got, 0)));
        if (got <= 0) break;
    }
    mp3->layout = ScanMp3(file.data(), file.size());
    if (mp3->layout.frames == 0) return nullptr;
    mp3->decoding = OpenMp3Decoder(file.data(), file.size(), mp3->layout, &mp3->decoder);
    return mp3.release();
}

void CloseMp3(Mp3Stream* mp3) {
    if (mp3->decoding) ma_decoder_uninit(&mp3->decoder);
    delete mp3;
}

unsigned Mp3Rate(const Mp3Stream* mp3) { return mp3->layout.rate; }
int Mp3Channels(const Mp3Stream* mp3) { return mp3->layout.channels; }
int Mp3Bits(const Mp3Stream*) { return 16; }

int DecodeMp3(Mp3Stream* mp3, void* to, int bytes) {
    const int frameBytes = mp3->layout.channels * 2;
    const std::uint64_t frames =
        std::min<std::uint64_t>(static_cast<std::uint64_t>(std::max(bytes, 0) / frameBytes), mp3->layout.samples - mp3->given);
    DecodeFrames(&mp3->decoder, mp3->decoding, mp3->layout.channels, frames, &mp3->samples, static_cast<std::uint8_t*>(to));
    mp3->given += frames;
    return static_cast<int>(frames) * frameBytes;
}

void RewindMp3(Mp3Stream* mp3) {
    mp3->given = 0;
    if (mp3->decoding) ma_decoder_seek_to_pcm_frame(&mp3->decoder, 0);
}

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
    // Every whole frame's samples, as Miles gave: what dr_mp3 decodes, cut or padded with silence to that count.
    auto decoder = std::make_unique<ma_decoder>();
    const bool decoding = OpenMp3Decoder(data, size, layout, decoder.get());
    std::vector<std::int16_t> samples;
    DecodeFrames(decoder.get(), decoding, layout.channels, layout.samples, &samples, p + 44);
    if (decoding) ma_decoder_uninit(decoder.get());
    return true;
}

} // namespace platform::audio
