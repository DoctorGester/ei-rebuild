// platform::FloatingPointGuard (platform.h).
#include "platform/platform.h"

#include <xmmintrin.h>

namespace platform {

// MXCSR's control bits (flush to zero, rounding, the exception masks, denormals are zero); the low six are the status
// flags.
constexpr unsigned int kMxcsrControl = 0xFFC0;

FloatingPointGuard::FloatingPointGuard() : saved(_mm_getcsr()) {}

FloatingPointGuard::~FloatingPointGuard() {
    const unsigned int now = _mm_getcsr();
    if ((now ^ saved) & kMxcsrControl) _mm_setcsr((now & ~kMxcsrControl) | (saved & kMxcsrControl));
}

} // namespace platform
