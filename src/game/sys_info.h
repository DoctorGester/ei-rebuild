#pragma once

// The system information object (CSysInfo; the global at 007AB8F8, vtable 0073D644, class id 0xD):
// five texts about the machine and what Windows reported about itself, collected once by the
// constructor, which a static initialiser runs before WinMain. See sys_info.cpp and
// docs/notes/script.md (A-S11, A-E2).

#include "engine_object.h"

#include "lib/mfc.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

// What CPUID function 1 reported (the globals at 007CAED0; zeros on a processor without CPUID):
// EAX, EBX, ECX as twelve bytes, a terminator, then EDX (the feature flags). Written by CSysInfo's
// collector and again by the renderer base's creation (00500540).
struct ProcessorInfo {
    std::uint32_t signature;   // 007CAED0
    std::uint32_t brand;       // 007CAED4
    std::uint32_t features2;   // 007CAED8
    std::uint8_t terminator;   // 007CAEDC
    std::uint8_t unknown_0D[3];
    std::uint32_t features;    // 007CAEE0
};
static_assert(offsetof(ProcessorInfo, terminator) == 0x0C && offsetof(ProcessorInfo, features) == 0x10 &&
              sizeof(ProcessorInfo) == 0x14);
inline constexpr std::uint32_t kProcessorMmx = 0x00800000;   // CPUID.1:EDX bit 23
inline constexpr std::uint32_t kProcessorSse = 0x02000000;   // CPUID.1:EDX bit 25
inline ProcessorInfo* TheProcessorInfo() { return reinterpret_cast<ProcessorInfo*>(0x007CAED0); }
// The terminator cleared, then CPUID function 1 into the globals when the processor has CPUID (the
// EFLAGS ID bit can be flipped; as in the original it stays flipped). Both writers inline this.
void ReadProcessorInfo();

struct CSysInfo : CEngineObject {
    static constexpr int kClassId = 0xD;
    // flags_ bits.
    static constexpr std::uint32_t kMmx = 0x8;       // always set by Collect
    static constexpr std::uint32_t kSse = 0x10;
    static constexpr std::uint32_t kWin32s = 0x20;
    static constexpr std::uint32_t kWindows95 = 0x40;
    static constexpr std::uint32_t kWindows98 = 0x80;
    static constexpr std::uint32_t kWindowsNt = 0x100;

    // 004DDD6F: the engine object's fields, the five empty texts, the vtable, then Create.
    CSysInfo* Construct();
    // 004DDF28: everything below filled from the system; returns 1.
    int Collect();
    // 004DE6F3: `version_` (GetVersionExA) and the platform's family name into systemName_ (Collect
    // replaces it with the exact name). Returns &version_.
    OSVERSIONINFOA* QueryVersion();
    // 004DE6C2: `system_` (GetSystemInfo). Returns &system_.
    SYSTEM_INFO* QuerySystem();
    // 004DE687: `memory_` (GlobalMemoryStatus). Returns &memory_.
    MEMORYSTATUS* QueryMemory();
    // 004DE789: the processor's CPUID report into the globals; kMmx and kSse set or cleared from it.
    void QueryProcessor();

    mfc::CString computerName_;    // +0x1C GetComputerNameA
    mfc::CString userName_;        // +0x20 GetUserNameA
    mfc::CString systemName_;      // +0x24 "Windows NT", "Windows 98", ...
    mfc::CString systemVersion_;   // +0x28 "<major>.<minor>.<build>" (empty on Windows 95 / 98)
    mfc::CString servicePack_;     // +0x2C szCSDVersion
    MEMORYSTATUS memory_;          // +0x30
    SYSTEM_INFO system_;           // +0x50
    OSVERSIONINFOA version_;       // +0x74
};
static_assert(offsetof(CSysInfo, computerName_) == 0x1C && offsetof(CSysInfo, servicePack_) == 0x2C &&
              offsetof(CSysInfo, memory_) == 0x30 && offsetof(CSysInfo, system_) == 0x50 &&
              offsetof(CSysInfo, version_) == 0x74 && sizeof(CSysInfo) == 0x108);

inline CSysInfo* TheSysInfo() { return reinterpret_cast<CSysInfo*>(0x007AB8F8); }

// 004DDE75: the destructor body (its vtable, the five texts from the last to the first, the engine
// object's base destructor).
void __fastcall SysInfoDestruct(CSysInfo* self);

// The translation unit's static initialisers (they run before WinMain; a start-up check compares
// what they left, tests/verify/verify_static_initialisers.cpp). The four constants: 2 pi at
// 007AB8F0, pi / 2 at 007ABA00, pi / 4 at 007ABA04, 1 / pi at 007ABA08 (copies of a header's
// constants; nothing found reading these).
void __cdecl SysInfoInitTwoPi();       // 004DDCBA
void __cdecl SysInfoInitHalfPi();      // 004DDCD7
void __cdecl SysInfoInitQuarterPi();   // 004DDCF8
void __cdecl SysInfoInitInversePi();   // 004DDD19
