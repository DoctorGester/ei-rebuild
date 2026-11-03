#pragma once

// A thread context's instruction and stack pointers (Rip, Rsp). For the crash and hang reports, the profiler and the
// breakpoints.

#include <windows.h>

#include <cstdint>

namespace ei {

inline std::uintptr_t InstructionPointer(const CONTEXT& context) { return context.Rip; }

inline void SetInstructionPointer(CONTEXT& context, std::uintptr_t address) { context.Rip = address; }

inline std::uintptr_t StackPointer(const CONTEXT& context) { return context.Rsp; }

} // namespace ei
