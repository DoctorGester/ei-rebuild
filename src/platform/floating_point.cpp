// platform::FloatingPointGuard (platform.h).
#include "platform/platform.h"

#include <float.h>
#include <xmmintrin.h>

namespace platform {

#if defined(_M_IX86)
FloatingPointGuard::FloatingPointGuard() { __control87_2(0, 0, &saved, nullptr); }

FloatingPointGuard::~FloatingPointGuard() {
    unsigned int now = 0;
    __control87_2(0, 0, &now, nullptr);
    if (now != saved) __control87_2(saved, _MCW_EM | _MCW_IC | _MCW_RC | _MCW_PC, &now, nullptr);
}
#else
// MXCSR's control bits (flush to zero, rounding, the exception masks, denormals are zero); the low six are the status
// flags.
constexpr unsigned int kMxcsrControl = 0xFFC0;

FloatingPointGuard::FloatingPointGuard() : saved(_mm_getcsr()) {}

FloatingPointGuard::~FloatingPointGuard() {
    const unsigned int now = _mm_getcsr();
    if ((now ^ saved) & kMxcsrControl) _mm_setcsr((now & ~kMxcsrControl) | (saved & kMxcsrControl));
}
#endif

} // namespace platform
