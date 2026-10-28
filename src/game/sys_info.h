#pragma once

// The system information object (CSysInfo; a global, class id 0xD):
// five texts about the machine and what Windows reported about itself, collected once by the
// constructor, which a static initialiser runs before WinMain. See sys_info.cpp and
// docs/notes/script.md.

#include "engine_object.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include "lib/static_object.h"

#include <windows.h>
#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)


// What CPUID function 1 reported (the globals; zeros on a processor without CPUID):
// EAX, EBX, ECX as twelve bytes, a terminator, then EDX (the feature flags). Written by CSysInfo's
// collector and again by the renderer base's creation.
struct ProcessorInfo {
    std::uint32_t signature;
    std::uint32_t brand;
    std::uint32_t features2;
    std::uint8_t terminator;
    std::uint8_t unused_0D[3];
    std::uint32_t features;
};
static_assert(offsetof(ProcessorInfo, terminator) == 0x0C && offsetof(ProcessorInfo, features) == 0x10 &&
              sizeof(ProcessorInfo) == 0x14);
inline constexpr std::uint32_t kProcessorMmx = 1u << 23;   // CPUID.1:EDX bit 23
inline constexpr std::uint32_t kProcessorSse = 0x02000000;   // CPUID.1:EDX bit 25
extern ProcessorInfo g_processorInfo;   // sys_info.cpp
inline ProcessorInfo* TheProcessorInfo() { return &g_processorInfo; }
// The terminator cleared, then CPUID function 1 into the globals when the processor has CPUID (the
// EFLAGS ID bit can be flipped; as in the game it stays flipped). Both writers inline this.
void ReadProcessorInfo();

class CSysInfo final : public CEngineObject {
public:
    static constexpr int kClassId = 0xD;
    // flags_ bits.
    static constexpr std::uint32_t kMmx = 0x8;       // always set by Collect
    static constexpr std::uint32_t kSse = 0x10;
    static constexpr std::uint32_t kWin32s = 0x20;
    static constexpr std::uint32_t kWindows95 = 0x40;
    static constexpr std::uint32_t kWindows98 = 0x80;
    static constexpr std::uint32_t kWindowsNt = 0x100;

    CSysInfo();
    ~CSysInfo() override;

    int Create() override;                  // everything collected; 1
    void Destroy() override {}              // empty
    const char* GetClassName() override;    // "CSysInfo"
    int GetClassId() override;              // 0xD

    // Everything below filled from the system; returns 1.
    int Collect();
    // `version_` (GetVersionExA) and the platform's family name into systemName_ (Collect
    // replaces it with the exact name). Returns &version_.
    OSVERSIONINFOA* QueryVersion();
    // `system_` (GetSystemInfo). Returns &system_.
    SYSTEM_INFO* QuerySystem();
    // `memory_` (GlobalMemoryStatus). Returns &memory_.
    MEMORYSTATUS* QueryMemory();
    // The processor's CPUID report into the globals; kMmx and kSse set or cleared from it.
    void QueryProcessor();

    std::string computerName_;    // GetComputerNameA
    std::string userName_;        // GetUserNameA
    std::string systemName_;      // "Windows NT", "Windows 98", ...
    std::string systemVersion_;   // "<major>.<minor>.<build>" (empty on Windows 95 / 98)
    std::string servicePack_;     // szCSDVersion
    MEMORYSTATUS memory_;
    SYSTEM_INFO system_;
    OSVERSIONINFOA version_;
};

extern lib::StaticObject<CSysInfo> g_sysInfo;   // sys_info.cpp: constructed by its static initialiser

// The translation unit's static initialisers (they run before WinMain). The four
// constants: 2 pi, pi / 2, pi / 4 and
// 1 / pi (copies of a header's constants; nothing found reading these).
void SysInfoInitTwoPi();
void SysInfoInitHalfPi();
void SysInfoInitQuarterPi();
void SysInfoInitInversePi();
