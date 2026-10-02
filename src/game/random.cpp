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
