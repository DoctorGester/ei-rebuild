#pragma once

// The game's worker threads (the music stream), as MFC's AfxBeginThread started them: platform/thread.h's Thread
// for code that keeps a native handle.

#include "lib/win_types.h"
#include "platform/thread.h"

namespace lib {

using ThreadProcedure = platform::ThreadProcedure;

// A new thread running `procedure(parameter)` at `priority` (SetThreadPriority's); its handle, which the caller
// closes, or null when it could not be made. The thread ends when the procedure returns.
inline HANDLE StartThread(ThreadProcedure procedure, void* parameter, int priority) {
    platform::Thread thread;
    thread.Start(procedure, parameter, priority);
    return thread.Release();
}

} // namespace lib
