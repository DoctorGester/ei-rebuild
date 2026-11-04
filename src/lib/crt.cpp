#include "crt.h"

#include "lib/caller.h"

#include <cstddef>

namespace game {

// The release build's debug log writes nothing (its callers ignore the result).
unsigned DebugLog(const char*, ...) { return 0; }

} // namespace game

namespace libcrt {

namespace {

RandObserver g_randObserver;
// The seed rand advances: one per thread (the game CRT's _tiddata::_holdrand).
unsigned& RandSeed() {
    thread_local unsigned seed = 1;
    return seed;
}

} // namespace

void SetRandObserver(RandObserver observer) { g_randObserver = observer; }

int Rand() {
    unsigned& seed = RandSeed();
    seed = seed * 214013u + 2531011u;
    const int value = static_cast<int>((seed >> 16) & 0x7FFF);
    if (g_randObserver) g_randObserver(value, EI_RETURN_ADDRESS());
    return value;
}

void Srand(unsigned seed) { RandSeed() = seed; }
unsigned& RandState() { return RandSeed(); }

} // namespace libcrt
