// CSysInfo (004DDCB0-004DE8B0; A-S11, A-E2). The system information object is a global (007AB8F8;
// an engine object, vtable 0073D644, class id 0xD) holding five texts about the machine and what
// Windows reported. A static initialiser (table entry 00783320: 004DDD30) constructs it and
// registers its destructor with atexit; the constructor (004DDD6F) calls Create, which collects
// everything (004DDF28). All of that runs before the hooks install, so the game never reaches the
// rewrites of the initialisers, the constructor, the collector and its helpers; a start-up check
// compared what they build with what the original built
// (verify_static_initialisers.cpp until S21d). The destructor (at exit) and the virtual slots
// can run later. The original is compiled without optimisation (every function keeps a frame and
// spills ECX). See docs/notes/script.md (A-S11, A-E2).
#include "sys_info.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/vecmath.h"
#include "originals_library.h"

#include <intrin.h>

#include <cstring>
#include <new>

namespace {

void SetSysInfoVtable(CSysInfo* self) { ei::SetVtable(self, 0x0073D644); }
const char* SysInfoClassNameText() { return reinterpret_cast<const char*>(0x0078DCD8); }   // "CSysInfo"

constexpr std::uint32_t kEflagsId = 0x00200000;   // EFLAGS.ID: can be flipped when CPUID exists

// Windows 98's first build; a lower build of the 9x platform is Windows 95.
constexpr unsigned kFirstWindows98Build = 1998;

// The translation unit's constants and what they are made of (the exe's pi, 2, 4 and 1).
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
float& GameFloatAt(std::uintptr_t address) { return *reinterpret_cast<float*>(address); }
float Pi() { return GameFloat(0x0073D640); }

// The guard of a function-local static with an empty destructor (bit 0 of 007CAEE4).
std::uint8_t& EmptyStaticGuard() { return *reinterpret_cast<std::uint8_t*>(0x007CAEE4); }

} // namespace

void ReadProcessorInfo() {
    ProcessorInfo* processor = TheProcessorInfo();
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

// 004DE687.
MEMORYSTATUS* CSysInfo::QueryMemory() {
    std::memset(&memory_, 0, sizeof(memory_));
    memory_.dwLength = sizeof(memory_);
    gameapi::GlobalMemoryStatus(&memory_);
    return &memory_;
}
EI_REPLACE_METHOD(0x004DE687, CSysInfo::QueryMemory);

// 004DE6C2.
SYSTEM_INFO* CSysInfo::QuerySystem() {
    std::memset(&system_, 0, sizeof(system_));
    gameapi::GetSystemInfo(&system_);
    return &system_;
}
EI_REPLACE_METHOD(0x004DE6C2, CSysInfo::QuerySystem);

// 004DE6F3.
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
EI_REPLACE_METHOD(0x004DE6F3, CSysInfo::QueryVersion);

// 004DE789. (The original also copies the report's twelve bytes as a string into a local nothing
// reads.) Without CPUID the globals keep what they held.
void CSysInfo::QueryProcessor() {
    ReadProcessorInfo();
    const std::uint32_t features = TheProcessorInfo()->features;
    if (features & kProcessorMmx)
        flags_ |= kMmx;
    else
        flags_ &= ~kMmx;
    if (features & kProcessorSse)
        flags_ |= kSse;
    else
        flags_ &= ~kSse;
}
EI_REPLACE_METHOD(0x004DE789, CSysInfo::QueryProcessor);

// 004DDF28. (The original's two unused locals, a registry key holder that is never opened and an
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
        systemVersion_.Format("%d.%d.%d", version_.dwMajorVersion, version_.dwMinorVersion, version_.dwBuildNumber & 0xFFFF);
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
        systemVersion_.Format("%d.%d.%d", version_.dwMajorVersion, version_.dwMinorVersion, version_.dwBuildNumber);
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

    mfc::CString report;
    report = "OS\t\t\t" + systemName_ + " build " + systemVersion_ + "\n";
    char digits[0x400];
    report += mfc::CString("Total physical memory\t") + libcrt::Itoa(static_cast<int>(memory_.dwTotalPhys), digits, 10) + "\n";
    report += mfc::CString("Available physical memory\t") + libcrt::Itoa(static_cast<int>(memory_.dwAvailPhys), digits, 10) + "\n";
    report += mfc::CString("Total virtual memory\t\t") + libcrt::Itoa(static_cast<int>(memory_.dwTotalVirtual), digits, 10) + "\n";
    report += mfc::CString("Available virtual memory\t") + libcrt::Itoa(static_cast<int>(memory_.dwAvailVirtual), digits, 10) + "\n";
    return 1;
}
EI_REPLACE_METHOD(0x004DDF28, CSysInfo::Collect);

// ---- The object ------------------------------------------------------------------------------------

// 004DDD6F.
CSysInfo* CSysInfo::Construct() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    ClearFields();
    new (&computerName_) mfc::CString();
    new (&userName_) mfc::CString();
    new (&systemName_) mfc::CString();
    new (&systemVersion_) mfc::CString();
    new (&servicePack_) mfc::CString();
    SetSysInfoVtable(this);
    Collect();   // Create (004DDF05), called directly
    return this;
}
EI_REPLACE_METHOD(0x004DDD6F, CSysInfo::Construct);

// 004DDF05 (slot 2, Create): everything collected; returns 1. The constructor calls it directly.
int __fastcall SysInfoCreate(CSysInfo* self) {
    self->Collect();
    return 1;
}
EI_REPLACE(0x004DDF05, SysInfoCreate);

// 004DDF1D (slot 3, Destroy): nothing.
void __fastcall SysInfoDestroy(CSysInfo*) {}
EI_REPLACE(0x004DDF1D, SysInfoDestroy);

// 004DE890 / 004DE8A0 (slots 11 / 12): the class name and id.
const char* __fastcall SysInfoClassName(CSysInfo*) { return SysInfoClassNameText(); }
EI_REPLACE(0x004DE890, SysInfoClassName);
int __fastcall SysInfoClassId(CSysInfo*) { return CSysInfo::kClassId; }
EI_REPLACE(0x004DE8A0, SysInfoClassId);

// 004DDE75: the destructor body: its vtable, the five texts from the last to the first, the engine
// object's base destructor.
void __fastcall SysInfoDestruct(CSysInfo* self) {
    SetSysInfoVtable(self);
    self->servicePack_.~CString();
    self->systemVersion_.~CString();
    self->systemName_.~CString();
    self->userName_.~CString();
    self->computerName_.~CString();
    self->DestructBase();
}
EI_REPLACE(0x004DDE75, SysInfoDestruct);

// 004DE8B0 (slot 0, RET 4): the destructor, then operator delete when bit 0 of `flags` is set.
CSysInfo* CSysInfo::DeleteScalar(unsigned flags) {
    SysInfoDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004DE8B0, CSysInfo::DeleteScalar);

// 004DDD60 (cdecl; registered with atexit by the static initialiser): the global's destructor.
void __cdecl SysInfoAtExit() { SysInfoDestruct(TheSysInfo()); }
EI_REPLACE(0x004DDD60, SysInfoAtExit);

// ---- The static initialisers (each table entry is a stub that calls the function after it) --------

// 004DDCBA / 004DDCD7 / 004DDCF8 / 004DDD19: the four constants, each one operation on the exe's
// pi rounded to a float by the store.
void __cdecl SysInfoInitTwoPi() { GameFloatAt(0x007AB8F0) = static_cast<float>(Pi() + Pi()); }
EI_REPLACE(0x004DDCBA, SysInfoInitTwoPi);
void __cdecl SysInfoInitHalfPi() { GameFloatAt(0x007ABA00) = static_cast<float>(Pi() / GameFloat(0x0073CDB8)); }
EI_REPLACE(0x004DDCD7, SysInfoInitHalfPi);
void __cdecl SysInfoInitQuarterPi() { GameFloatAt(0x007ABA04) = static_cast<float>(Pi() / GameFloat(0x0073BAA0)); }
EI_REPLACE(0x004DDCF8, SysInfoInitQuarterPi);
void __cdecl SysInfoInitInversePi() { GameFloatAt(0x007ABA08) = static_cast<float>(GameFloat(0x0073B84C) / Pi()); }
EI_REPLACE(0x004DDD19, SysInfoInitInversePi);

// 004DDCB0 / 004DDCCD / 004DDCEE / 004DDD0F: the table's entries for them.
void __cdecl SysInfoStaticTwoPi() { SysInfoInitTwoPi(); }
EI_REPLACE(0x004DDCB0, SysInfoStaticTwoPi);
void __cdecl SysInfoStaticHalfPi() { SysInfoInitHalfPi(); }
EI_REPLACE(0x004DDCCD, SysInfoStaticHalfPi);
void __cdecl SysInfoStaticQuarterPi() { SysInfoInitQuarterPi(); }
EI_REPLACE(0x004DDCEE, SysInfoStaticQuarterPi);
void __cdecl SysInfoStaticInversePi() { SysInfoInitInversePi(); }
EI_REPLACE(0x004DDD0F, SysInfoStaticInversePi);

// 004DDD3F: the global constructed. 004DDD4E: its destructor registered with atexit. 004DDD30: the
// table's entry: both.
void __cdecl SysInfoStaticConstruct() { TheSysInfo()->Construct(); }
EI_REPLACE(0x004DDD3F, SysInfoStaticConstruct);
void __cdecl SysInfoStaticRegisterExit() { GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x004DDD60)); }
EI_REPLACE(0x004DDD4E, SysInfoStaticRegisterExit);
void __cdecl SysInfoStaticInit() {
    SysInfoStaticConstruct();
    SysInfoStaticRegisterExit();
}
EI_REPLACE(0x004DDD30, SysInfoStaticInit);

// 004DE87C: an empty destructor (004013D0, a bare RET) registered with atexit. 004DE855: the
// table's entry: the static's guard bit set when it is clear, then the registration either way.
void __cdecl SysInfoStaticRegisterEmptyExit() { GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x004013D0)); }
EI_REPLACE(0x004DE87C, SysInfoStaticRegisterEmptyExit);
void __cdecl SysInfoStaticGuard() {
    if (!(EmptyStaticGuard() & 1)) EmptyStaticGuard() |= 1;
    SysInfoStaticRegisterEmptyExit();
}
EI_REPLACE(0x004DE855, SysInfoStaticGuard);
