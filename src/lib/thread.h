#pragma once

// The game's worker threads (the music stream, the cursor), as MFC's AfxBeginThread started them: created
// suspended, the priority set, then running.

#include <windows.h>

namespace lib {

using ThreadProcedure = unsigned(__stdcall*)(void* parameter);

// A new thread running `procedure(parameter)` at `priority` (SetThreadPriority's); its handle, which the caller
// closes, or null when it could not be made. The thread ends when the procedure returns.
HANDLE StartThread(ThreadProcedure procedure, void* parameter, int priority);

} // namespace lib
