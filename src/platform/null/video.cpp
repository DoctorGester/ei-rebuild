// The movie backend without a decoder (platform/video.h): a Bink 1 file opens with its size, frame count and rate
// from its header, its frames come black at the movie's own rate, and its sound is silent. So the game's players run
// as long as with the pictures: the titles' movie loops under the text, a skippable movie ends with its last frame.
#include "platform/video.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace platform::video {

struct Movie {
    int width = 0;
    int height = 0;
    unsigned frames = 0;
    unsigned next = 1;
    unsigned decoded = 0;
    // Frames per second as rate / divider.
    std::uint32_t rate = 0;
    std::uint32_t divider = 0;
    std::chrono::steady_clock::time_point start;
    std::uint64_t advanced = 0;   // Advance calls: the next frame is due that many frame times after the start
};

namespace {

// The Bink 1 header: "BIK" and a revision letter, the file's size less 8, the frame count, the largest frame's
// size, the frame count again, the width, the height, and the rate as a dividend and a divider.
constexpr std::size_t kHeaderBytes = 36;
constexpr int kLargestSide = 4096;

std::uint32_t Read32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

Movie* Open(const char* path, bool) {
    std::u8string name(reinterpret_cast<const char8_t*>(path));
    std::ifstream file(std::filesystem::path(name), std::ios::binary);
    unsigned char header[kHeaderBytes];
    if (!file.read(reinterpret_cast<char*>(header), kHeaderBytes) || std::memcmp(header, "BIK", 3) != 0) return nullptr;
    auto* movie = new Movie;
    movie->frames = Read32(header + 8);
    movie->width = static_cast<int>(Read32(header + 20));
    movie->height = static_cast<int>(Read32(header + 24));
    movie->rate = Read32(header + 28);
    movie->divider = Read32(header + 32);
    if (movie->frames == 0 || movie->width <= 0 || movie->width > kLargestSide || movie->height <= 0 ||
        movie->height > kLargestSide || movie->rate == 0 || movie->divider == 0) {
        delete movie;
        return nullptr;
    }
    movie->start = std::chrono::steady_clock::now();
    return movie;
}

void Close(Movie* movie) { delete movie; }

int Width(const Movie* movie) { return movie->width; }
int Height(const Movie* movie) { return movie->height; }
unsigned FrameCount(const Movie* movie) { return movie->frames; }
unsigned NextFrameNumber(const Movie* movie) { return movie->next; }
unsigned DecodedFrameNumber(const Movie* movie) { return movie->decoded; }

bool Waiting(Movie* movie) {
    const auto due = movie->start + std::chrono::microseconds(movie->advanced * 1000000 * movie->divider / movie->rate);
    return std::chrono::steady_clock::now() < due;
}

void DecodeFrame(Movie* movie) { movie->decoded = movie->next; }

void CopyFrame(const Movie* movie, void* to, int pitch) {
    auto* row = static_cast<unsigned char*>(to);
    for (int y = 0; y < movie->height; ++y, row += pitch) std::fill_n(row, movie->width * 4, static_cast<unsigned char>(0));
}

void Advance(Movie* movie) {
    movie->next = movie->next >= movie->frames ? 1 : movie->next + 1;
    ++movie->advanced;
}

void GoToFrame(Movie* movie, unsigned frame) { movie->next = frame < 1 ? 1 : frame > movie->frames ? movie->frames : frame; }

void SetVolume(Movie*, int) {}

} // namespace platform::video
