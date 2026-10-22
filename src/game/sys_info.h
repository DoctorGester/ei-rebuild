#pragma once

// The system information object (CSysInfo; the global at 007AB8F8, game.exe's vtable 0073D644, class id 0xD):
// five texts about the machine and what Windows reported about itself, collected once by the
// constructor, which a static initialiser runs before WinMain. See sys_info.cpp and
// docs/notes/script.md (A-S11, A-E2).

#include "engine_object.h"

#include "lib/mfc.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <windows.h>
#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

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
    std::uint8_t unused_0D[3];
    std::uint32_t features;    // 007CAEE0
};
static_assert(offsetof(ProcessorInfo, terminator) == 0x0C && offsetof(ProcessorInfo, features) == 0x10 &&
              sizeof(ProcessorInfo) == 0x14);
inline constexpr std::uint32_t kProcessorMmx = 1u << 23;   // CPUID.1:EDX bit 23
inline constexpr std::uint32_t kProcessorSse = 0x02000000;   // CPUID.1:EDX bit 25
extern ProcessorInfo g_processorInfo;   // sys_info.cpp
inline ProcessorInfo* TheProcessorInfo() { return &g_processorInfo; }
// The terminator cleared, then CPUID function 1 into the globals when the processor has CPUID (the
// EFLAGS ID bit can be flipped; as in the original it stays flipped). Both writers inline this.
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

    // 004DDD6F: the engine object's fields, the five empty texts, then Create (called directly).
    CSysInfo();
    // 004DDE75 (deleting: 004DE8B0): the five texts from the last to the first.
    ~CSysInfo() override;

    int Create() override;                  // 2: 004DDF05 everything collected; 1
    void Destroy() override {}              // 3: 004DDF1D (empty)
    const char* GetClassName() override;    // 11: 004DE890 "CSysInfo"
    int GetClassId() override;              // 12: 004DE8A0 (0xD)

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

extern lib::StaticObject<CSysInfo> g_sysInfo;   // sys_info.cpp: constructed by its static initialiser
inline CSysInfo* TheSysInfo() { return &*g_sysInfo; }

// The translation unit's static initialisers (they run before WinMain; a start-up check compared
// what they left, verify_static_initialisers.cpp until S21d). The four constants: 2 pi, pi / 2, pi / 4 and
// 1 / pi (copies of a header's constants; nothing found reading these).
void __cdecl SysInfoInitTwoPi();       // 004DDCBA
void __cdecl SysInfoInitHalfPi();      // 004DDCD7
void __cdecl SysInfoInitQuarterPi();   // 004DDCF8
void __cdecl SysInfoInitInversePi();   // 004DDD19
