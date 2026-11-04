// src/audio's mixer without a device (docs/notes/sound.md, "The mixer"): harness runs open no sound device, so the
// mixing runs in no scenario. These checks make an offline output (audio::OpenOffline), play synthetic waves and
// halves through it and compare the frames with a model of the rules written out here (linear volume, a balance
// pan, one device period of ramp on a change, linear resampling, loops, the stream's two halves, the cave reverb's
// delays and fall), then keep a checked-in hash of one mix of everything together: the mixer is basic float operations
// in a fixed order (and lib::Pow), so the hash is the same on Windows and Linux.
#include "audio/audio.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    std::printf("audio mix: %s failed\n", what);
    ++g_failures;
}

using Frames = std::vector<std::array<float, 2>>;
using Samples = std::vector<std::int16_t>;

// ---- Waves and halves made here ----

void Put16(std::vector<std::uint8_t>& w, unsigned v) {
    w.push_back(static_cast<std::uint8_t>(v));
    w.push_back(static_cast<std::uint8_t>(v >> 8));
}
void Put32(std::vector<std::uint8_t>& w, std::uint32_t v) {
    Put16(w, v & 0xFFFF);
    Put16(w, v >> 16);
}
void PutTag(std::vector<std::uint8_t>& w, const char* tag) { w.insert(w.end(), tag, tag + 4); }

// A RIFF WAVE of PCM `bits` (8 or 16) with `data` as its bytes.
std::vector<std::uint8_t> PcmWave(unsigned rate, unsigned channels, unsigned bits, const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> w;
    PutTag(w, "RIFF");
    Put32(w, 36 + static_cast<std::uint32_t>(data.size()));
    PutTag(w, "WAVE");
    PutTag(w, "fmt ");
    Put32(w, 16);
    Put16(w, 1);
    Put16(w, channels);
    Put32(w, rate);
    Put32(w, rate * channels * bits / 8);
    Put16(w, channels * bits / 8);
    Put16(w, bits);
    PutTag(w, "data");
    Put32(w, static_cast<std::uint32_t>(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    return w;
}

std::vector<std::uint8_t> Wave16(unsigned rate, unsigned channels, const Samples& samples) {
    std::vector<std::uint8_t> data;
    for (std::int16_t s : samples) Put16(data, static_cast<std::uint16_t>(s));
    return PcmWave(rate, channels, 16, data);
}

// One IMA ADPCM block of mono samples that stay at `predictor`: step index 0 and nibbles of 0 (the step is 7, its
// eighth is 0, so the predictor does not move).
std::vector<std::uint8_t> AdpcmWave(unsigned rate, std::int16_t predictor) {
    constexpr unsigned kBlock = 20;                       // 4 bytes of header, 16 of nibbles
    constexpr unsigned kPerBlock = (kBlock - 4) * 2 + 1;  // 33 samples
    std::vector<std::uint8_t> w;
    PutTag(w, "RIFF");
    Put32(w, 4 + 8 + 20 + 8 + 4 + 8 + kBlock);
    PutTag(w, "WAVE");
    PutTag(w, "fmt ");
    Put32(w, 20);
    Put16(w, 0x11);
    Put16(w, 1);
    Put32(w, rate);
    Put32(w, rate * kBlock / kPerBlock);
    Put16(w, kBlock);
    Put16(w, 4);
    Put16(w, 2);
    Put16(w, kPerBlock);
    PutTag(w, "fact");
    Put32(w, 4);
    Put32(w, kPerBlock);
    PutTag(w, "data");
    Put32(w, kBlock);
    Put16(w, static_cast<std::uint16_t>(predictor));
    Put16(w, 0);
    w.insert(w.end(), kBlock - 4, 0);
    return w;
}

// A deterministic stand-in for sound: a triangle wave of `period` frames and `amplitude`, plus a little noise from a
// linear congruential generator (no libm, so every system gets the same integers).
Samples Tone(unsigned frames, unsigned channels, int period, int amplitude, std::uint32_t seed) {
    Samples out;
    std::uint32_t state = seed;
    for (unsigned i = 0; i < frames; ++i) {
        for (unsigned c = 0; c < channels; ++c) {
            const int phase = static_cast<int>((i + c * 7) % static_cast<unsigned>(period));
            const int tri = phase < period / 2 ? phase : period - phase;   // 0 .. period / 2
            state = state * 1664525u + 1013904223u;
            const int noise = static_cast<int>(state >> 24) - 128;
            out.push_back(static_cast<std::int16_t>(tri * 2 * amplitude / period - amplitude / 2 + noise));
        }
    }
    return out;
}

// ---- The model ----

struct Gain {
    double left;
    double right;
};
Gain GainOf(int volume, int pan, double dry = 1.0) {
    const double level = volume / 127.0 * dry;
    return {level * (pan <= 64 ? 1.0 : (127 - pan) / 63.0), level * (pan >= 64 ? 1.0 : pan / 64.0)};
}

// The source's frames (L, R as -1..1) played to `count` output frames at `outRate`: straight lines between the
// source's frames, the frame past the end counted as 0, and nothing once the first frame of the pair is past the end.
Frames Resample(const Frames& src, unsigned srcRate, unsigned outRate, std::size_t count) {
    Frames out(count, {0.0f, 0.0f});
    for (std::size_t i = 0; i < count; ++i) {
        const double p = static_cast<double>(i) * srcRate / outRate;
        const auto a = static_cast<std::size_t>(p);
        if (a >= src.size()) break;
        const double t = p - static_cast<double>(a);
        for (int c = 0; c < 2; ++c) {
            const double from = src[a][c];
            const double to = a + 1 < src.size() ? src[a + 1][c] : 0.0;
            out[i][c] = static_cast<float>(from + (to - from) * t);
        }
    }
    return out;
}

// 16-bit samples as frames (a mono source on both sides).
Frames AsFrames(const Samples& s, unsigned channels) {
    Frames f;
    for (std::size_t i = 0; i + channels <= s.size(); i += channels)
        f.push_back({s[i] / 32768.0f, s[i + (channels > 1 ? 1 : 0)] / 32768.0f});
    return f;
}

Frames Repeat(const Frames& f, int times) {
    Frames out;
    for (int i = 0; i < times; ++i) out.insert(out.end(), f.begin(), f.end());
    return out;
}

void Scale(Frames& f, Gain from, Gain to, std::size_t block) {
    // The gains at frame i of a block of `block`: from + (to - from) * i / block, restarting each block.
    for (std::size_t i = 0; i < f.size(); ++i) {
        const double t = static_cast<double>(i % block) / static_cast<double>(block);
        f[i][0] = static_cast<float>(f[i][0] * (from.left + (to.left - from.left) * t));
        f[i][1] = static_cast<float>(f[i][1] * (from.right + (to.right - from.right) * t));
    }
}

// ---- The device ----

constexpr unsigned kRate = 44100;

Frames Mix(std::uint32_t frames) {
    std::vector<float> raw(static_cast<std::size_t>(frames) * 2);
    audio::MixOffline(raw.data(), frames);
    Frames out(frames);
    for (std::uint32_t i = 0; i < frames; ++i) out[i] = {raw[2 * i], raw[2 * i + 1]};
    return out;
}

// The largest difference between the mix and the model over the frames of both (a length that differs counts).
float Difference(const Frames& got, const Frames& want, std::size_t from = 0) {
    if (got.size() != want.size()) return 1e9f;
    float worst = 0.0f;
    for (std::size_t i = from; i < got.size(); ++i)
        for (int c = 0; c < 2; ++c) worst = std::max(worst, std::fabs(got[i][c] - want[i][c]));
    return worst;
}

float Peak(const Frames& f, std::size_t from, std::size_t to) {
    float peak = 0.0f;
    for (std::size_t i = from; i < to && i < f.size(); ++i) peak = std::max({peak, std::fabs(f[i][0]), std::fabs(f[i][1])});
    return peak;
}

struct Device {
    bool open;
    explicit Device(unsigned rate = kRate) : open(audio::OpenOffline(rate)) {}
    ~Device() { audio::CloseOutput(); }
};

audio::Voice* VoiceOf(const std::vector<std::uint8_t>& wave) {
    audio::Voice* v = audio::CreateVoice();
    audio::SetWave(v, wave.data(), wave.size());
    return v;
}

constexpr float kTolerance = 2e-6f;

// ---- Checks ----

void WaveHeaders() {
    const auto pcm = Wave16(22050, 2, Tone(11025, 2, 100, 8000, 1));
    const audio::WaveInfo info = audio::ReadWaveInfo(pcm.data(), pcm.size());
    Check(info.format == audio::kPcm && info.rate == 22050 && info.channels == 2 && info.bits == 16 && info.samples == 11025,
          "PCM header");
    Check(audio::WaveMilliseconds(info) == 500, "PCM length");
    const auto ima = AdpcmWave(22050, 1000);
    const audio::WaveInfo adpcm = audio::ReadWaveInfo(ima.data(), ima.size());
    Check(adpcm.format == audio::kImaAdpcm && adpcm.samples == 33 && adpcm.blockSize == 20, "IMA ADPCM header");
    Check(audio::WaveMilliseconds(adpcm) == 20u * 1000 / (22050u * 20 / 33), "IMA ADPCM length");
}

void DryVoices() {
    Device device;
    Check(device.open, "offline output opens");
    const Samples tone = Tone(300, 1, 64, 12000, 2);
    const auto wave = Wave16(kRate, 1, tone);
    const Frames src = AsFrames(tone, 1);

    // Full volume, centred, at the output's rate: every frame as it is; silent once the wave has played.
    audio::Voice* voice = VoiceOf(wave);
    audio::Start(voice);
    Frames want = src;
    want.resize(400, {0.0f, 0.0f});
    Check(Difference(Mix(400), want) <= kTolerance, "dry mono voice at full volume");
    audio::DestroyVoice(voice);

    // Volume 40, pan 100 (the left side takes 27 / 63), and pan 20 (the right takes 20 / 64).
    for (int pan : {100, 20, 0, 127}) {
        voice = VoiceOf(wave);
        audio::SetVolume(voice, 40);
        audio::SetPan(voice, pan);
        audio::Start(voice);
        const Gain g = GainOf(40, pan);
        Frames scaled = src;
        for (auto& f : scaled) {
            f[0] = static_cast<float>(f[0] * g.left);
            f[1] = static_cast<float>(f[1] * g.right);
        }
        scaled.resize(300);
        char what[64];
        std::snprintf(what, sizeof what, "volume 40 pan %d", pan);
        Check(Difference(Mix(300), scaled) <= kTolerance, what);
        audio::DestroyVoice(voice);
    }

    // A change while it plays ramps over the next mix, from what the last mix ended on.
    voice = VoiceOf(Wave16(kRate, 1, Tone(1000, 1, 50, 20000, 3)));
    audio::SetLoopCount(voice, 0);
    audio::Start(voice);
    const Frames loop = Repeat(AsFrames(Tone(1000, 1, 50, 20000, 3), 1), 1);
    const Frames first = Mix(100);
    audio::SetVolume(voice, 30);
    audio::SetPan(voice, 90);
    const Frames second = Mix(100);
    Frames ramped(loop.begin() + 100, loop.begin() + 200);
    Scale(ramped, GainOf(127, 64), GainOf(30, 90), 100);
    Check(Difference(first, Frames(loop.begin(), loop.begin() + 100)) <= kTolerance, "before the change");
    Check(Difference(second, ramped) <= kTolerance, "gain ramp over one block");
    const Frames third = Mix(100);
    Frames settled(loop.begin() + 200, loop.begin() + 300);
    Scale(settled, GainOf(30, 90), GainOf(30, 90), 100);
    Check(Difference(third, settled) <= kTolerance, "gain settled after the ramp");
    audio::DestroyVoice(voice);
}

void Formats() {
    Device device;
    // 8 bits unsigned, stereo 16 bits, and IMA ADPCM.
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 64; ++i) bytes.push_back(static_cast<std::uint8_t>(i * 4));
    audio::Voice* v8 = VoiceOf(PcmWave(kRate, 1, 8, bytes));
    audio::Start(v8);
    Frames want;
    for (std::uint8_t b : bytes) want.push_back({static_cast<float>((b - 128) * 256) / 32768.0f, static_cast<float>((b - 128) * 256) / 32768.0f});
    want.resize(80, {0.0f, 0.0f});
    Check(Difference(Mix(80), want) <= kTolerance, "8-bit voice");
    audio::DestroyVoice(v8);

    const Samples stereo = Tone(200, 2, 40, 9000, 4);
    audio::Voice* vs = VoiceOf(Wave16(kRate, 2, stereo));
    audio::SetPan(vs, 64);
    audio::Start(vs);
    Frames wantStereo = AsFrames(stereo, 2);
    wantStereo.resize(220, {0.0f, 0.0f});
    Check(Difference(Mix(220), wantStereo) <= kTolerance, "stereo voice keeps its sides");
    audio::DestroyVoice(vs);

    audio::Voice* va = VoiceOf(AdpcmWave(kRate, 1000));
    audio::Start(va);
    Frames wantAdpcm(33, {1000.0f / 32768.0f, 1000.0f / 32768.0f});
    wantAdpcm.resize(40, {0.0f, 0.0f});
    Check(Difference(Mix(40), wantAdpcm) <= kTolerance, "IMA ADPCM voice");
    audio::DestroyVoice(va);
}

void Resampling() {
    for (unsigned rate : {22050u, 11025u, 32000u, 48000u}) {
        Device device;
        const Samples tone = Tone(500, 1, 70, 15000, rate);
        audio::Voice* v = VoiceOf(Wave16(rate, 1, tone));
        audio::Start(v);
        const std::size_t count = static_cast<std::size_t>(500) * kRate / rate + 20;
        const Frames want = Resample(AsFrames(tone, 1), rate, kRate, count);
        char what[64];
        std::snprintf(what, sizeof what, "resampled from %u Hz", rate);
        Check(Difference(Mix(static_cast<std::uint32_t>(count)), want) <= 1e-5f, what);
        audio::DestroyVoice(v);
    }
}

void LoopsPositionsAndPauses() {
    Device device;
    const Samples tone = Tone(120, 1, 30, 10000, 5);
    const auto wave = Wave16(kRate, 1, tone);
    const Frames src = AsFrames(tone, 1);

    // Three plays, then silence.
    audio::Voice* v = VoiceOf(wave);
    audio::SetLoopCount(v, 3);
    audio::Start(v);
    Frames want = Repeat(src, 3);
    want.resize(400, {0.0f, 0.0f});
    Check(Difference(Mix(400), want) <= kTolerance, "a wave played three times");
    audio::DestroyVoice(v);

    // For ever, in blocks that do not divide the wave.
    v = VoiceOf(wave);
    audio::SetLoopCount(v, 0);
    audio::Start(v);
    Frames got;
    for (std::uint32_t block : {77u, 200u, 33u, 90u}) {
        const Frames part = Mix(block);
        got.insert(got.end(), part.begin(), part.end());
    }
    Frames forever = Repeat(src, 4);
    forever.resize(got.size());
    Check(Difference(got, forever) <= kTolerance, "a looping wave across blocks");
    audio::DestroyVoice(v);

    // Started from a byte position (2 bytes a frame).
    v = VoiceOf(wave);
    audio::SetPosition(v, 2 * 50);
    audio::Start(v);
    Frames fromHalf(src.begin() + 50, src.end());
    fromHalf.resize(100, {0.0f, 0.0f});
    Check(Difference(Mix(100), fromHalf) <= kTolerance, "started from a position");
    audio::DestroyVoice(v);

    // Stopped: silence and the place kept; Resume goes on.
    v = VoiceOf(wave);
    audio::Start(v);
    const Frames before = Mix(40);
    audio::Stop(v);
    const Frames silent = Mix(40);
    audio::Resume(v);
    const Frames after = Mix(100);
    Check(Difference(before, Frames(src.begin(), src.begin() + 40)) <= kTolerance, "before the pause");
    Check(Peak(silent, 0, silent.size()) == 0.0f, "silent while paused");
    Frames rest(src.begin() + 40, src.end());
    rest.resize(100, {0.0f, 0.0f});
    Check(Difference(after, rest) <= kTolerance, "goes on after Resume");
    audio::End(v);
    Check(Peak(Mix(50), 0, 50) == 0.0f, "an ended voice is silent");
    audio::DestroyVoice(v);
}

void SumsAndClips() {
    Device device;
    const Samples loud = Tone(100, 1, 20, 60000, 6);
    std::vector<audio::Voice*> voices;
    const auto wave = Wave16(kRate, 1, loud);
    for (int i = 0; i < 3; ++i) {
        voices.push_back(VoiceOf(wave));
        audio::Start(voices.back());
    }
    Frames want = AsFrames(loud, 1);
    for (auto& f : want)
        for (float& s : f) s = std::clamp(s * 3.0f, -1.0f, 1.0f);
    Check(Difference(Mix(100), want) <= 1e-5f, "three voices add and clip at full scale");
    for (audio::Voice* v : voices) audio::DestroyVoice(v);
}

void Reverb() {
    Device device;
    // A click of 200 frames at 20000 in a cave (room 0.3, level 1, 3 s): the voice keeps 0.65 of itself, and its
    // send reaches the combs 892 frames (the first comb at 1116 * 0.8) after it starts.
    Samples click(200, 20000);
    const auto wave = Wave16(kRate, 1, click);
    audio::Voice* v = VoiceOf(wave);
    audio::SetReverb(v, 0.3f, 1.0f, 3.0f);
    audio::Start(v);
    const std::uint32_t total = 7 * kRate;
    Frames all;
    for (std::uint32_t done = 0; done < total; done += 441) {
        const Frames part = Mix(441);
        all.insert(all.end(), part.begin(), part.end());
    }
    const float dry = 20000.0f / 32768.0f * 0.65f;
    Check(std::fabs(all[0][0] - dry) < 1e-5f && std::fabs(all[199][1] - dry) < 1e-5f, "a cave voice keeps 0.65 of itself");
    Check(Peak(all, 200, 892) == 0.0f, "nothing before the first comb's delay");
    Check(Peak(all, 892, 1400) > 0.0f, "the tail starts after the first comb's delay");
    const float early = Peak(all, 1400, kRate / 2);
    const float late = Peak(all, static_cast<std::size_t>(3.0 * kRate), static_cast<std::size_t>(3.3 * kRate));
    Check(early > 0.001f && early < 0.1f, "the tail's level");
    Check(late < early * 1e-3f, "the tail has fallen 60 dB by the asked time");
    Check(Peak(all, static_cast<std::size_t>(6.5 * kRate), all.size()) == 0.0f, "the reverb rests after two tails");
    // The sides differ (the right side's lines are 23 frames longer).
    bool differ = false;
    for (std::size_t i = 892; i < 4000 && !differ; ++i) differ = all[i][0] != all[i][1];
    Check(differ, "stereo spread");
    audio::DestroyVoice(v);

    // A second room empties the lines: nothing from the old tail after a new SetReverb with another room.
    audio::Voice* a = VoiceOf(wave);
    audio::SetReverb(a, 0.3f, 1.0f, 3.0f);
    audio::Start(a);
    Mix(3000);
    audio::SetReverb(a, 0.9f, 1.0f, 3.0f);
    audio::DestroyVoice(a);
    const Frames emptied = Mix(1000);
    Check(Peak(emptied, 0, emptied.size()) == 0.0f, "a new room empties the lines");

    // A voice with room 0 (left the cave) feeds nothing.
    audio::Voice* dryVoice = VoiceOf(wave);
    audio::SetReverb(dryVoice, 0.0f, 1.0f, 3.0f);
    audio::Start(dryVoice);
    const Frames outside = Mix(4000);
    Check(std::fabs(outside[0][0] - 20000.0f / 32768.0f) < 1e-5f && Peak(outside, 200, 4000) == 0.0f, "no reverb outside a cave");
    audio::DestroyVoice(dryVoice);
}

void Stream() {
    Device device;
    audio::Stream* stream = audio::CreateStream();
    Check(audio::MinimumHalfBytes(44100, 2) == 15360, "the half's minimum size");
    audio::SetStreamFormat(stream, kRate, 2);
    const Samples a = Tone(1000, 2, 90, 14000, 7);
    const Samples b = Tone(1000, 2, 110, 14000, 8);
    const Samples c = Tone(600, 2, 70, 14000, 9);

    Check(audio::ReadyHalf(stream, 0) == 0, "an empty stream asks for half 0");
    audio::QueueHalf(stream, 0, a.data(), static_cast<int>(a.size() * 2));
    Check(audio::ReadyHalf(stream, 0) == 1, "then half 1");
    audio::QueueHalf(stream, 1, b.data(), static_cast<int>(b.size() * 2));
    Check(audio::ReadyHalf(stream, 0) == -1, "both queued: none is free");

    Frames got = Mix(500);
    Check(audio::ReadyHalf(stream, 0) == -1, "half 0 is still playing");
    Frames part = Mix(500);
    got.insert(got.end(), part.begin(), part.end());
    Check(audio::ReadyHalf(stream, 0) == 0, "half 0 is free once it has played");
    // A request that comes late: the next buffer is queued while half 1 plays, and the music goes on seamlessly.
    audio::QueueHalf(stream, 0, c.data(), static_cast<int>(c.size() * 2));
    part = Mix(1000);
    got.insert(got.end(), part.begin(), part.end());
    Check(audio::ReadyHalf(stream, 0) == 1, "half 1 is free after its frames");
    part = Mix(600);
    got.insert(got.end(), part.begin(), part.end());
    Frames want = AsFrames(a, 2);
    for (const Samples* s : {&b, &c}) {
        const Frames f = AsFrames(*s, 2);
        want.insert(want.end(), f.begin(), f.end());
    }
    want.resize(got.size(), {0.0f, 0.0f});
    Check(Difference(got, want) <= kTolerance, "two halves and a late third play without a gap");
    part = Mix(500);
    Check(Peak(part, 0, part.size()) == 0.0f, "a stream with no half left plays silence");

    // Reset: both halves free again. A stream that has played keeps its last two frames in its resampler (a fresh
    // one starts on its data), so each case below is a stream of its own.
    audio::ResetStream(stream);
    Check(audio::ReadyHalf(stream, 0) == 0, "a reset stream asks for half 0");
    audio::DestroyStream(stream);
    stream = audio::CreateStream();
    audio::SetStreamFormat(stream, kRate, 2);

    // Stopped: silent; Resume goes on where it was.
    audio::QueueHalf(stream, 0, a.data(), static_cast<int>(a.size() * 2));
    const Frames first = Mix(100);
    audio::StopStream(stream);
    const Frames paused = Mix(100);
    audio::ResumeStream(stream);
    const Frames rest = Mix(100);
    const Frames src = AsFrames(a, 2);
    Check(Difference(first, Frames(src.begin(), src.begin() + 100)) <= kTolerance, "reset stream plays its first half");
    Check(Peak(paused, 0, paused.size()) == 0.0f, "a stopped stream is silent");
    Check(Difference(rest, Frames(src.begin() + 100, src.begin() + 200)) <= kTolerance, "a resumed stream goes on");

    // Volume and pan, and a mono 22050 Hz half.
    audio::DestroyStream(stream);
    stream = audio::CreateStream();
    audio::SetStreamVolume(stream, 50);
    audio::SetStreamPan(stream, 100);
    audio::SetStreamFormat(stream, 22050, 1);
    const Samples mono = Tone(300, 1, 60, 12000, 10);
    audio::QueueHalf(stream, 0, mono.data(), static_cast<int>(mono.size() * 2));
    Frames wantMono = Resample(AsFrames(mono, 1), 22050, kRate, 600);
    const Gain g = GainOf(50, 100);
    for (auto& f : wantMono) {
        f[0] = static_cast<float>(f[0] * g.left);
        f[1] = static_cast<float>(f[1] * g.right);
    }
    Check(Difference(Mix(600), wantMono) <= 1e-5f, "a mono half at 22050 Hz with volume and pan");

    audio::DestroyStream(stream);
}

// FNV-1a over the frames' bits.
std::uint64_t Hash(const std::vector<float>& frames) {
    std::uint64_t h = 1469598103934665603ull;
    for (float f : frames) {
        const auto bits = std::bit_cast<std::uint32_t>(f);
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<std::uint8_t>(bits >> (8 * i));
            h *= 1099511628211ull;
        }
    }
    return h;
}

// Three voices (a looping mono one at 22050 Hz, a stereo one panned and ramped, an ADPCM one in a cave), a stream and
// a pause, mixed in the device's 441-frame periods for 1.5 s.
void Everything() {
    Device device;
    const Samples mono = Tone(3000, 1, 97, 18000, 11);
    const Samples stereo = Tone(40000, 2, 211, 24000, 12);
    const auto monoWave = Wave16(22050, 1, mono);
    const auto stereoWave = Wave16(kRate, 2, stereo);
    const auto adpcmWave = AdpcmWave(11025, 9000);
    audio::Voice* vm = VoiceOf(monoWave);
    audio::Voice* vs = VoiceOf(stereoWave);
    audio::Voice* va = VoiceOf(adpcmWave);
    audio::SetLoopCount(vm, 0);
    audio::SetLoopCount(va, 0);
    audio::SetVolume(vm, 80);
    audio::SetPan(vm, 30);
    audio::SetVolume(va, 127);
    audio::SetReverb(va, 0.5f, 0.8f, 2.0f);
    audio::Stream* stream = audio::CreateStream();
    audio::SetStreamFormat(stream, kRate, 2);
    audio::SetStreamVolume(stream, 60);
    const Samples a = Tone(7680, 2, 301, 20000, 13);
    const Samples b = Tone(7680, 2, 157, 20000, 14);
    audio::QueueHalf(stream, 0, a.data(), static_cast<int>(a.size() * 2));
    audio::Start(vm);
    audio::Start(vs);
    audio::Start(va);

    std::vector<float> all;
    int queued = 0;
    for (int period = 0; period < 150; ++period) {
        if (period == 20) audio::SetPan(vs, 110);
        if (period == 30) audio::SetVolume(vs, 50);
        if (period == 50) audio::Stop(vm);
        if (period == 60) audio::Resume(vm);
        if (period == 70) audio::SetReverb(va, 0.0f, 0.8f, 2.0f);
        const int ready = audio::ReadyHalf(stream, 0);
        if (ready >= 0 && queued < 4) {
            const Samples& next = queued % 2 == 0 ? b : a;
            audio::QueueHalf(stream, ready, next.data(), static_cast<int>(next.size() * 2));
            ++queued;
        }
        std::vector<float> raw(441 * 2);
        audio::MixOffline(raw.data(), 441);
        all.insert(all.end(), raw.begin(), raw.end());
    }
    audio::DestroyStream(stream);
    audio::DestroyVoice(vm);
    audio::DestroyVoice(vs);
    audio::DestroyVoice(va);
    constexpr std::uint64_t kExpected = 0x3905b07d341869dfull;
    const std::uint64_t hash = Hash(all);
    Check(queued == 4, "the stream asked for its halves in turn");
    Check(std::all_of(all.begin(), all.end(), [](float f) { return std::fabs(f) <= 1.0f; }), "nothing past full scale");
    if (hash != kExpected) std::printf("audio mix: the combined mix hashes to 0x%016llx\n", static_cast<unsigned long long>(hash));
    Check(hash == kExpected, "the combined mix is the checked-in one");
}

} // namespace

int RunAudioMixChecks() {
    WaveHeaders();
    DryVoices();
    Formats();
    Resampling();
    LoopsPositionsAndPauses();
    SumsAndClips();
    Reverb();
    Stream();
    Everything();
    std::printf("ei_unit_check: audio mix: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
