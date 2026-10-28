// CSysInfo. The system information object is a global
// (an engine object, class id 0xD) holding five texts about the machine and what
// Windows reported. A static initialiser constructs it and
// registers its destructor with atexit; the constructor calls Create, which collects
// everything. All of that runs before the hooks install, so the game never reaches the
// rewrites of the initialisers, the constructor, the collector and its helpers.
// The destructor (at exit) and the virtuals
// can run later. The game is compiled without optimisation (every function keeps a frame and
// spills ECX). See docs/notes/script.md.
#include "sys_info.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <new>
#include <string>

// What CPUID function 1 reported, the system information object (constructed by its static initialiser) and
// the translation unit's four constants, which their initialisers store.
ProcessorInfo g_processorInfo = /*007CAED0*/ {};
constinit lib::StaticObject<CSysInfo> g_sysInfo = /*007AB8F8*/ {};

namespace {

float g_twoPi = /*007AB8F0*/ {};
float g_halfPi = /*007ABA00*/ {};
float g_quarterPi = /*007ABA04*/ {};
float g_inversePi = /*007ABA08*/ {};

const char* SysInfoClassNameText() { return "CSysInfo"; }

constexpr std::uint32_t kEflagsId = 0x00200000;   // EFLAGS.ID: can be flipped when CPUID exists

// Windows 98's first build; a lower build of the 9x platform is Windows 95.
constexpr unsigned kFirstWindows98Build = 1998;

} // namespace

void ReadProcessorInfo() {
    ProcessorInfo* processor = &g_processorInfo;
    processor->terminator = 0;
    const std::uint32_t before = static_cast<std::uint32_t>(__readeflags());
    __writeeflags(before ^ kEflagsId);
    if ((static_cast<std::uint32_t>(__readeflags()) ^ before) == 0) return;
    int registers[4];
    __cpuid(registers, 1);
    processor->features = static_cast<std::uint32_t>(registers[3]);
    processor->signature = static_cast<std::uint32_t>(registers[0]);
    processor->brand = static_cast<std::uint32_t>(registers[1]);
    processor->features2 = static_cast<std::uint32_t>(registers[2]);
}

// ---- The collector and its helpers ---------------------------------------------------------------

MEMORYSTATUS* CSysInfo::QueryMemory() {
    std::memset(&memory_, 0, sizeof(memory_));
    memory_.dwLength = sizeof(memory_);
    gameapi::GlobalMemoryStatus(&memory_);
    return &memory_;
}

SYSTEM_INFO* CSysInfo::QuerySystem() {
    std::memset(&system_, 0, sizeof(system_));
    gameapi::GetSystemInfo(&system_);
    return &system_;
}

OSVERSIONINFOA* CSysInfo::QueryVersion() {
    std::memset(&version_, 0, sizeof(version_));
    version_.dwOSVersionInfoSize = sizeof(version_);
    gameapi::GetVersionExA(&version_);
    if (version_.dwPlatformId == VER_PLATFORM_WIN32s)
        systemName_ = "Windows 3.11/Win32s";
    else if (version_.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS)
        systemName_ = "Windows 95/98";
    else if (version_.dwPlatformId == VER_PLATFORM_WIN32_NT)
        systemName_ = "Windows NT";
    return &version_;
}

// (The game also copies the report's twelve bytes as a string into a local nothing
// reads.) Without CPUID the globals keep what they held.
void CSysInfo::QueryProcessor() {
    ReadProcessorInfo();
    const std::uint32_t features = g_processorInfo.features;
    if (features & kProcessorMmx)
        flags_ |= kMmx;
    else
        flags_ &= ~kMmx;
    if (features & kProcessorSse)
        flags_ |= kSse;
    else
        flags_ &= ~kSse;
}

// (The game's two unused locals, a registry key holder that is never opened and an
// empty string, are left out.) The report built at the end goes nowhere: it is a local.
int CSysInfo::Collect() {
    QueryVersion();
    QuerySystem();
    QueryMemory();
    QueryProcessor();
    flags_ |= kMmx;

    switch (version_.dwPlatformId) {
    case VER_PLATFORM_WIN32s:
        systemName_ = "Windows 3.11/Win32s";
        systemVersion_ = str::Format("%d.%d.%d", version_.dwMajorVersion, version_.dwMinorVersion, version_.dwBuildNumber & 0xFFFF);
        flags_ |= kWin32s;
        break;
    case VER_PLATFORM_WIN32_WINDOWS:
        if ((version_.dwBuildNumber & 0xFFFF) < kFirstWindows98Build) {
            systemName_ = "Windows 95";
            flags_ |= kWindows95;
        } else {
            systemName_ = "Windows 98";
            flags_ |= kWindows98;
        }
        break;
    case VER_PLATFORM_WIN32_NT:
        systemName_ = "Windows NT";
        systemVersion_ = str::Format("%d.%d.%d", version_.dwMajorVersion, version_.dwMinorVersion, version_.dwBuildNumber);
        flags_ |= kWindowsNt;
        break;
    default:
        systemName_ = "UNKNOWN OS";
        systemVersion_ = "UNKNOWN VERSION";
        flags_ &= ~(kWin32s | kWindows95 | kWindows98 | kWindowsNt);
        break;
    }
    servicePack_ = version_.szCSDVersion;

    char computer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computerSize = sizeof(computer);
    gameapi::GetComputerNameA(computer, &computerSize);
    computerName_ = computer;

    char user[0x101];
    DWORD userSize = sizeof(user);
    gameapi::GetUserNameA(user, &userSize);
    userName_ = user;

    std::string report;
    report = "OS\t\t\t" + systemName_ + " build " + systemVersion_ + "\n";
    char digits[0x400];
#pragma warning(push)
#pragma warning(disable : 4996)   // _itoa
    report += std::string("Total physical memory\t") + _itoa(static_cast<int>(memory_.dwTotalPhys), digits, 10) + "\n";
    report += std::string("Available physical memory\t") + _itoa(static_cast<int>(memory_.dwAvailPhys), digits, 10) + "\n";
    report += std::string("Total virtual memory\t\t") + _itoa(static_cast<int>(memory_.dwTotalVirtual), digits, 10) + "\n";
    report += std::string("Available virtual memory\t") + _itoa(static_cast<int>(memory_.dwAvailVirtual), digits, 10) + "\n";
#pragma warning(pop)
    return 1;
}

// ---- The object ------------------------------------------------------------------------------------

CSysInfo::CSysInfo() {
    ClearFields();
    Collect();   // Create, called directly
}

CSysInfo::~CSysInfo() = default;

int CSysInfo::Create() {
    Collect();
    return 1;
}

const char* CSysInfo::ClassName() { return SysInfoClassNameText(); }
ClassId CSysInfo::GetClassId() { return ClassId::SysInfo; }

// The global's destructor (registered with atexit by the static initialiser).
void SysInfoAtExit() { g_sysInfo.Destroy(); }

// ---- The static initialisers (each table entry is a stub that calls the function after it) --------

// The four constants, each one operation on the exe's
// pi rounded to a float by the store.
void SysInfoInitTwoPi() { g_twoPi = static_cast<float>(kPi + kPi); }
void SysInfoInitHalfPi() { g_halfPi = static_cast<float>(kPi / 2.0f); }
void SysInfoInitQuarterPi() { g_quarterPi = static_cast<float>(kPi / 4.0f); }
void SysInfoInitInversePi() { g_inversePi = static_cast<float>(1.0f / kPi); }

// The table's entries for them.
void SysInfoStaticTwoPi() { SysInfoInitTwoPi(); }
void SysInfoStaticHalfPi() { SysInfoInitHalfPi(); }
void SysInfoStaticQuarterPi() { SysInfoInitQuarterPi(); }
void SysInfoStaticInversePi() { SysInfoInitInversePi(); }

// The global constructed, then its destructor registered with atexit: the
// table's entry covers both.
void SysInfoStaticConstruct() { g_sysInfo.Construct(); }
void SysInfoStaticRegisterExit() { std::atexit(SysInfoAtExit); }
void SysInfoStaticInit() {
    SysInfoStaticConstruct();
    SysInfoStaticRegisterExit();
}

// A static with an empty destructor: its guard bit set when it is clear (nothing is registered for the destructor).
void SysInfoStaticGuard() {
    if (!(lib::g_emptyStaticGuard & 1)) lib::g_emptyStaticGuard |= 1;
}
