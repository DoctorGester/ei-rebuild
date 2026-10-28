// The game's worker threads (thread.h).
#include "thread.h"

#include "gameapi.h"

#include <process.h>

namespace lib {

HANDLE StartThread(ThreadProcedure procedure, void* parameter, int priority) {
    unsigned id = 0;
    auto* thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, procedure, parameter, CREATE_SUSPENDED, &id));
    if (!thread) return nullptr;
    gameapi::SetThreadPriority(thread, priority);
    gameapi::ResumeThread(thread);
    return thread;
}

} // namespace lib
