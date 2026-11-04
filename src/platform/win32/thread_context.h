#pragma once

// A thread context's instruction and stack pointers (Rip, Rsp). For the crash and hang reports, the profiler and the
// breakpoints.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // windows.h without its rarely used parts (shell, OLE, ...): 1.8 MB less to preprocess
#endif
#include <windows.h>

#include <cstdint>

namespace ei {

inline std::uintptr_t InstructionPointer(const CONTEXT& context) { return context.Rip; }

inline void SetInstructionPointer(CONTEXT& context, std::uintptr_t address) { context.Rip = address; }

inline std::uintptr_t StackPointer(const CONTEXT& context) { return context.Rsp; }

} // namespace ei
