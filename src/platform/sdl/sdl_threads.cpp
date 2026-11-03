// The SDL backend's thread priority (platform.h, "Threads"), for every build on SDL (the harness's too).
#include "platform/platform.h"

#include <SDL3/SDL_thread.h>

namespace platform {

// SDL's time-critical priority is SetThreadPriority's THREAD_PRIORITY_TIME_CRITICAL on Windows.
void MakeThreadTimeCritical() { SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL); }

} // namespace platform
