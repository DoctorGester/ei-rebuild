// The random trace's observers in the image (image_side.h; harness_clock.cpp).
#include "image_side.h"

#include "game/random.h"
#include "lib/crt.h"

namespace ei::verify::image {

namespace {

DrawWriter g_write = nullptr;

void ObservedDraw(IsaacRandom& random, std::uint32_t value, const void* caller) {
    g_write(reinterpret_cast<std::uintptr_t>(caller), value, &random == &IsaacRandom::Global() ? "" : " (another generator)",
            random.c_ * 256u + 255u - random.count_);
}

void ObservedRand(int value, const void* caller) {
    g_write(reinterpret_cast<std::uintptr_t>(caller), static_cast<std::uint32_t>(value), " rand()", libcrt::RandState());
}

} // namespace

void ObserveDraws(DrawWriter write) {
    g_write = write;
    IsaacRandom::SetDrawObserver(&ObservedDraw);
    gamecrt::SetRandObserver(&ObservedRand);
}

} // namespace ei::verify::image
