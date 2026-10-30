#include "random.h"

#include "lib/crt.h"
#include "lib/pc24.h"
#include <cstdint>
#include <cstring>
#include <random>

#include "lib/return_address.h"

// The game's generator: unseeded (the static initialiser zeroes seeded; Init seeds it on first use).
IsaacRandom g_random = /*007A9D40*/ {};
IsaacRandom g_clientRandom = {};

namespace {
IsaacRandom::SeedHook g_seedHook;
IsaacRandom::DrawObserver g_drawObserver;

// The 2^-32 scale of Range. The original's double constant has its last bit set (2^-32 + 2^-64, sic), but times a
// 24-bit product rounded to 24 bits that bit never shows.
constexpr float kRangeScale = 0x1p-32f;

// The reference ISAAC mix of the eight randinit words.
struct InitMix {
    std::uint32_t a, b, c, d, e, f, g, h;

    void Mix() {
        a ^= b << 11; d += a; b += c;
        b ^= c >> 2;  e += b; c += d;
        c ^= d << 8;  f += c; d += e;
        d ^= e >> 16; g += d; e += f;
        e ^= f << 10; h += e; f += g;
        f ^= g >> 4;  a += f; g += h;
        g ^= h << 8;  b += g; h += a;
        h ^= a >> 9;  c += h; a += b;
    }

    // One pass over memory: add eight words of `source`, mix, store into memory.
    void Pass(const std::uint32_t* source, std::uint32_t* memory) {
        for (int i = 0; i < 256; i += 8) {
            a += source[i]; b += source[i + 1]; c += source[i + 2]; d += source[i + 3];
            e += source[i + 4]; f += source[i + 5]; g += source[i + 6]; h += source[i + 7];
            Mix();
            memory[i] = a; memory[i + 1] = b; memory[i + 2] = c; memory[i + 3] = d;
            memory[i + 4] = e; memory[i + 5] = f; memory[i + 6] = g; memory[i + 7] = h;
        }
    }
};

} // namespace

void IsaacRandom::SetSeedHook(SeedHook hook) { g_seedHook = hook; }
void IsaacRandom::SetDrawObserver(DrawObserver observer) { g_drawObserver = observer; }

std::uint32_t IsaacRandom::Next() {
    if (count-- == 0) {
        Refill();
        count = 255;
    }
    const std::uint32_t value = results[count];
    if (g_drawObserver) g_drawObserver(*this, value, _ReturnAddress());
    return value;
}

// Defined here so that the function vectors link it.
float IsaacRandom::Range(float min, float max) {
    // The draw (a 32-bit integer) times the range, rounded once to a float as the original's multiply at 24-bit
    // precision; the scale is exact (in the normal float range); the sum is the float sum.
    return pc24::Mul(max - min, static_cast<double>(Next())) * kRangeScale + min;
}

void IsaacRandom::Refill() {
    // One ISAAC step: mix a, produce memory and result words. `ind` reads memory at the byte
    // offset (x & 0x3FC), i.e. index (x >> 2) & 255.
    auto ind = [this](std::uint32_t x) { return memory[(x >> 2) & 0xFF]; };

    std::uint32_t a = aa;
    std::uint32_t b = bb + ++cc;
    std::uint32_t* m = memory;
    std::uint32_t* r = results;

    auto step = [&](std::uint32_t mix, std::uint32_t*& m2) {
        std::uint32_t x = *m;
        a = mix + *m2++;
        std::uint32_t y = ind(x) + a + b;
        *m++ = y;
        b = ind(y >> 8) + x;
        *r++ = b;
    };

    std::uint32_t* m2 = memory + 128;
    while (m < memory + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    m2 = memory;
    while (m2 < memory + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    bb = b;
    aa = a;
}

// The seed material only has to be unpredictable: the system's random source fills it, and seeds the CRT's rand() too
// (its other users draw from it). The original read 1 KB at a random offset of a random file on C:\ (a depth-first
// walk of the drive, up to 512 files deep) xored with rand() seeded from the clock, retrying while the bytes were all
// zero; with exactly one file found its second search divided by zero.
void IsaacRandom::Seed() {
    if (g_seedHook) {
        g_seedHook(*this);
        return;
    }
    std::random_device device;
    for (std::uint32_t& word : results) word = device();
    libcrt::Srand(device());
}

void IsaacRandom::Init() {
    if (seeded != 0) return;
    Seed();
    MixSeed();
}

void IsaacRandom::InitFrom(const IsaacRandom& source) {
    if (seeded != 0) return;
    std::memcpy(results, source.memory, sizeof(results));
    MixSeed();
}

void IsaacRandom::MixSeed() {
    cc = 0;
    bb = 0;
    aa = 0;
    constexpr std::uint32_t kGolden = 0x9E3779B9;
    InitMix m{kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden};
    for (int i = 0; i < 4; ++i) m.Mix();
    m.Pass(results, memory);
    m.Pass(memory, memory);
    Refill();
    count = 256;
    seeded = 1;
}

int ClientRand() { return static_cast<int>(g_clientRandom.Next() & 0x7FFF); }
