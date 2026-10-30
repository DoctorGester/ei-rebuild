#pragma once

// A thread context's instruction and stack pointers in either build: Eip / Esp in the 32-bit one, Rip / Rsp in the x64
// one (build.cmd x64, verify-x64). For the crash and hang reports, the profiler and the breakpoints.

#include <windows.h>

#include <cstdint>

namespace ei {

inline std::uintptr_t InstructionPointer(const CONTEXT& context) {
#if defined(_M_X64)
    return context.Rip;
#else
    return context.Eip;
#endif
}

inline void SetInstructionPointer(CONTEXT& context, std::uintptr_t address) {
#if defined(_M_X64)
    context.Rip = address;
#else
    context.Eip = static_cast<DWORD>(address);
#endif
}

inline std::uintptr_t StackPointer(const CONTEXT& context) {
#if defined(_M_X64)
    return context.Rsp;
#else
    return context.Esp;
#endif
}

// The x87 control word the context holds (precision, rounding).
inline unsigned FpuControlWordOf(const CONTEXT& context) {
#if defined(_M_X64)
    return context.FltSave.ControlWord;
#else
    return context.FloatSave.ControlWord & 0xFFFF;
#endif
}

} // namespace ei
