// The movies (movie.h) on the Bink 1 decoder: frames read and decoded on demand, converted to RGB when copied, due at
// the movie's own rate on a real-time clock; the sound track decoded with the frames and played as a stream of the
// audio output (audio/audio.h).
#include "video/movie.h"

#include "audio/audio.h"
#include "video/bink_audio.h"
#include "video/bink_file.h"
#include "video/bink_video.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace video {

namespace {

// BT.601 limited range to RGB in 15-bit fixed point, each term truncated: the luma's part per Y, the chroma's parts
// per U or V.
struct ColorTables {
    int luma[256];
    int redV[256];
    int greenU[256];
    int greenV[256];
    int blueU[256];
};

const ColorTables& Colors() {
    static const ColorTables tables = [] {
        ColorTables t{};
        for (int i = 0; i < 256; ++i) {
            t.luma[i] = (std::clamp(i, 16, 235) - 16) * 38154 / 32768;
            t.redV[i] = (i - 128) * 52299 / 32768;
            t.greenV[i] = (128 - i) * 26639 / 32768;
            t.greenU[i] = (128 - i) * 12837 / 32768;
            t.blueU[i] = (i - 128) * 66101 / 32768;
        }
        return t;
    }();
    return tables;
}

std::uint8_t Clamp8(int v) { return static_cast<std::uint8_t>(std::clamp(v, 0, 255)); }

} // namespace

struct Movie {
    bink::File file;
    std::optional<bink::VideoDecoder> video;
    unsigned next = 1;
    unsigned decoded = 0;
    std::chrono::steady_clock::time_point start;
    std::uint64_t advanced = 0;   // Advance calls: the next frame is due that many frame times after the start
    std::vector<std::uint8_t> pixels;   // FramePixels' R, G, B, X frame

    // The sound: decoded with the frames into `pending`, played through the stream's two halves.
    std::optional<bink::AudioDecoder> sound;
    audio::Stream* stream = nullptr;
    std::vector<std::int16_t> pending;
    std::size_t pendingStart = 0;
    std::size_t halfSamples = 0;

    ~Movie() {
        if (stream) audio::DestroyStream(stream);
    }

    void OpenSound() {
        stream = audio::CreateStream();
        if (!stream) return;
        sound.emplace(file.AudioRate(), file.AudioChannels());
        const auto rate = static_cast<unsigned>(file.AudioRate());
        audio::SetStreamFormat(stream, rate, file.AudioChannels());
        audio::SetStreamVolume(stream, 127);
        halfSamples = static_cast<std::size_t>(audio::MinimumHalfBytes(rate, file.AudioChannels())) / 2;
    }

    // Frame `number` (from 1) decoded, its sound appended unless `quiet`.
    void Decode(unsigned number, bool quiet) {
        bink::FramePackets packets;
        if (!file.ReadFrame(number - 1, packets)) return;
        video->Decode(packets.video, packets.videoBytes);
        if (sound && !quiet && packets.audio) sound->Decode(packets.audio, packets.audioBytes, packets.audioSamples, pending);
    }

    // The stream's free half filled with the sound decoded so far: a whole half's worth, or what is left once the
    // last frame is decoded.
    void Feed() {
        if (!stream) return;
        for (;;) {
            const std::size_t available = pending.size() - pendingStart;
            if (available == 0 || (available < halfSamples && decoded < file.FrameCount())) break;
            const int half = audio::ReadyHalf(stream, platform::FrameClock());
            if (half < 0) break;
            const std::size_t take = std::min(available, halfSamples);
            audio::QueueHalf(stream, half, pending.data() + pendingStart, static_cast<int>(take * sizeof(std::int16_t)));
            pendingStart += take;
        }
        if (pendingStart > halfSamples) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(pendingStart));
            pendingStart = 0;
        }
    }
};

Movie* Open(const char* path, bool sound) {
    auto movie = std::make_unique<Movie>();
    if (!movie->file.Open(path)) return nullptr;
    movie->video.emplace(movie->file.Width(), movie->file.Height());
    if (sound && movie->file.AudioRate() != 0) movie->OpenSound();
    movie->start = std::chrono::steady_clock::now();
    return movie.release();
}

void Close(Movie* movie) { delete movie; }

int Width(const Movie* movie) { return movie->file.Width(); }
int Height(const Movie* movie) { return movie->file.Height(); }
unsigned FrameCount(const Movie* movie) { return movie->file.FrameCount(); }
unsigned NextFrameNumber(const Movie* movie) { return movie->next; }
unsigned DecodedFrameNumber(const Movie* movie) { return movie->decoded; }

bool Pump(Movie* movie) {
    movie->Feed();
    const std::uint64_t micros = movie->advanced * 1000000 * movie->file.Divider() / movie->file.Rate();
    return std::chrono::steady_clock::now() >= movie->start + std::chrono::microseconds(micros);
}

// In order (and from the last frame back to the first) as it comes; a jump decodes from the keyframe at or before the
// frame, or on from the frame decoded last when that is nearer, without the frames' sound.
void DecodeFrame(Movie* movie) {
    const unsigned next = movie->next;
    if (next == movie->decoded + 1 || next == 1) {
        movie->Decode(next, false);
    } else {
        unsigned from = next;
        while (from > 1 && from != movie->decoded + 1 && !movie->file.Keyframe(from - 1)) --from;
        if (movie->sound) {
            movie->sound->Reset();
            movie->pending.clear();
            movie->pendingStart = 0;
        }
        for (unsigned frame = from; frame < next; ++frame) movie->Decode(frame, true);
        movie->Decode(next, false);
    }
    movie->decoded = next;
    movie->Feed();
}

const std::uint8_t* FramePixels(Movie* movie) {
    const ColorTables& c = Colors();
    const bink::Plane& luma = movie->video->Luma();
    const bink::Plane& u = movie->video->ChromaU();
    const bink::Plane& v = movie->video->ChromaV();
    const int pitch = movie->file.Width() * 4;
    movie->pixels.resize(static_cast<std::size_t>(pitch) * movie->file.Height());
    std::uint8_t* row = movie->pixels.data();
    for (int y = 0; y < movie->file.Height(); ++y, row += pitch) {
        const std::uint8_t* ys = luma.Row(y);
        const std::uint8_t* us = u.Row(y >> 1);
        const std::uint8_t* vs = v.Row(y >> 1);
        std::uint8_t* out = row;
        for (int x = 0; x < movie->file.Width(); ++x, out += 4) {
            const int l = c.luma[ys[x]];
            const int cu = us[x >> 1], cv = vs[x >> 1];
            out[0] = Clamp8(l + c.redV[cv]);
            out[1] = Clamp8(l + c.greenU[cu] + c.greenV[cv]);
            out[2] = Clamp8(l + c.blueU[cu]);
            out[3] = 0xFF;
        }
    }
    return movie->pixels.data();
}

void Advance(Movie* movie) {
    movie->next = movie->next >= movie->file.FrameCount() ? 1 : movie->next + 1;
    ++movie->advanced;
}

void GoToFrame(Movie* movie, unsigned frame) {
    movie->next = std::clamp(frame, 1u, movie->file.FrameCount());
}

void SetVolume(Movie* movie, int volume) {
    if (movie->stream) audio::SetStreamVolume(movie->stream, std::clamp(volume, 0, 127));
}

} // namespace video
