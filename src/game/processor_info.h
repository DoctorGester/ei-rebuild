#pragma once

// What CPUID function 1 reported, as the transform pipeline's constructor (004F0B90) and the
// renderer base's creation (00500540) read it into the globals at 007CAED0: the same inlined
// sequence in both. The renderer checks the MMX bit of the features.

#include <cstdint>

#include <intrin.h>

namespace processor {

// EAX, EBX, ECX as twelve bytes, a terminator, then EDX (the feature flags). The globals keep
// their zeros on a processor without CPUID.
struct Info {
    std::uint32_t signature;   // 007CAED0
    std::uint32_t brand;       // 007CAED4
    std::uint32_t features2;   // 007CAED8
    std::uint8_t terminator;   // 007CAEDC
    std::uint8_t unknown_0D[3];
    std::uint32_t features;    // 007CAEE0
};
inline Info& Global() { return *reinterpret_cast<Info*>(0x007CAED0); }

constexpr std::uint32_t kEflagsId = 0x00200000;    // EFLAGS.ID: can be flipped when CPUID exists
constexpr std::uint32_t kFeatureMmx = 0x00800000;  // CPUID.1:EDX bit 23

// Whether the processor has CPUID: the ID flag flips. As in the original it stays flipped.
inline bool HasCpuid() {
    const std::uint32_t before = static_cast<std::uint32_t>(__readeflags());
    __writeeflags(before ^ kEflagsId);
    return (static_cast<std::uint32_t>(__readeflags()) ^ before) != 0;
}

// The original's sequence: the terminator, then CPUID function 1 into the globals. (It goes on to
// copy the twelve bytes as a string into a local nothing reads.)
inline void Read() {
    Info& info = Global();
    info.terminator = 0;
    if (!HasCpuid()) return;
    int registers[4];
    __cpuid(registers, 1);
    info.features = static_cast<std::uint32_t>(registers[3]);
    info.signature = static_cast<std::uint32_t>(registers[0]);
    info.brand = static_cast<std::uint32_t>(registers[1]);
    info.features2 = static_cast<std::uint32_t>(registers[2]);
}

} // namespace processor
