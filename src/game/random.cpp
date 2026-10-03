#include "random.h"

#include "ei/hook.h"

std::uint32_t IsaacRandom::Next() {
    if (count_-- == 0) {
        Refill();
        count_ = 255;
    }
    return results_[count_];
}
EI_REPLACE_METHOD(0x004667F0, IsaacRandom::Next);

void IsaacRandom::Refill() {
    // One ISAAC step: mix a, produce memory and result words. `ind` reads memory_ at the byte
    // offset (x & 0x3FC), i.e. index (x >> 2) & 255.
    auto ind = [this](std::uint32_t x) { return memory_[(x >> 2) & 0xFF]; };

    std::uint32_t a = a_;
    std::uint32_t b = b_ + ++c_;
    std::uint32_t* m = memory_;
    std::uint32_t* r = results_;

    auto step = [&](std::uint32_t mix, std::uint32_t*& m2) {
        std::uint32_t x = *m;
        a = mix + *m2++;
        std::uint32_t y = ind(x) + a + b;
        *m++ = y;
        b = ind(y >> 8) + x;
        *r++ = b;
    };

    std::uint32_t* m2 = memory_ + 128;
    while (m < memory_ + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    m2 = memory_;
    while (m2 < memory_ + 128) {
        step(a ^ (a << 13), m2);
        step(a ^ (a >> 6), m2);
        step(a ^ (a << 2), m2);
        step(a ^ (a >> 16), m2);
    }
    b_ = b;
    a_ = a;
}
EI_REPLACE_METHOD(0x0049EE70, IsaacRandom::Refill);

__declspec(naked) void IsaacRandom::Seed() EI_ORIGINAL(0x0049F660)

namespace {

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

void IsaacRandom::Init() {
    if (seeded_ != 0) return;
    Seed();
    c_ = 0;
    b_ = 0;
    a_ = 0;
    constexpr std::uint32_t kGolden = 0x9E3779B9;
    InitMix m{kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden, kGolden};
    for (int i = 0; i < 4; ++i) m.Mix();
    m.Pass(results_, memory_);
    m.Pass(memory_, memory_);
    Refill();
    count_ = 256;
    seeded_ = 1;
}
EI_REPLACE_METHOD(0x0049F150, IsaacRandom::Init);
