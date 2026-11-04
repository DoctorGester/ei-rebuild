// What the harness asks of Windows (harness_system.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // windows.h without its rarely used parts (shell, OLE, ...): 1.8 MB less to preprocess
#endif
#include <winsock2.h>   // before windows.h
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod, timeGetTime
#include <dwmapi.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <tlhelp32.h>

#include "harness_system.h"

#include "platform/log.h"
#include "platform/platform.h"
#include "platform/win32/platform_win32.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <share.h>
#include <stdlib.h>
#include <thread>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace ei::verify::sys {

namespace {

std::wstring Wide(const std::string& path) { return text::Wide(path); }

std::uintptr_t InstructionPointer(const CONTEXT& c) { return c.Rip; }
std::uintptr_t StackPointer(const CONTEXT& c) { return c.Rsp; }

} // namespace

// ==== Files ==========================================================================================================

FILE* OpenFile(const std::string& path, const char* mode) {
    FILE* f = nullptr;
    return _wfopen_s(&f, Wide(path).c_str(), Wide(mode).c_str()) == 0 ? f : nullptr;
}

FILE* OpenShared(const std::string& path) { return _wfsopen(Wide(path).c_str(), L"rb", _SH_DENYNO); }

bool Exists(const std::string& path) { return GetFileAttributesW(Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES; }

long long FileSize(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(Wide(path).c_str(), GetFileExInfoStandard, &data)) return -1;
    return static_cast<long long>((static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
}

bool MakeFolder(const std::string& path) { return CreateDirectoryW(Wide(path).c_str(), nullptr) != FALSE; }

bool RemoveFile(const std::string& path) { return DeleteFileW(Wide(path).c_str()) != FALSE; }

bool CopyNewFile(const std::string& from, const std::string& to) {
    return CopyFileW(Wide(from).c_str(), Wide(to).c_str(), TRUE) != FALSE;
}

std::string FullPath(const std::string& path) {
    wchar_t full[MAX_PATH];
    const DWORD n = GetFullPathNameW(Wide(path).c_str(), MAX_PATH, full, nullptr);
    if (n == 0 || n >= MAX_PATH) return {};
    return text::Narrow(std::wstring_view(full, n));
}

long long FreeMegabytes(const std::string& folder) {
    ULARGE_INTEGER available{};
    if (folder.empty() || !GetDiskFreeSpaceExW(Wide(folder).c_str(), &available, nullptr, nullptr)) return -1;
    return static_cast<long long>(available.QuadPart / (1024 * 1024));
}

std::string ErrorText(unsigned long code) {
    char text[160] = "";
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
                             MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), text, sizeof(text), nullptr);
    while (n > 0 && (text[n - 1] == '\r' || text[n - 1] == '\n' || text[n - 1] == ' ' || text[n - 1] == '.')) text[--n] = '\0';
    return text;
}

std::string FileOfHandle(void* handle) {
    wchar_t name[MAX_PATH];
    const DWORD n = GetFinalPathNameByHandleW(handle ? handle : INVALID_HANDLE_VALUE, name, MAX_PATH,
                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring_view path(name, n);
    if (path.starts_with(L"\\\\?\\")) path.remove_prefix(4);
    return text::Narrow(path);
}

bool WriteHandle(void* handle, const void* data, unsigned size, unsigned* written, unsigned long* error) {
    DWORD done = 0;
    if (!::WriteFile(handle ? handle : INVALID_HANDLE_VALUE, data, size, &done, nullptr)) {
        *error = GetLastError();
        return false;
    }
    *written = done;
    return true;
}

const unsigned long kDiskFull = ERROR_DISK_FULL;

// ==== The game's calls as the system answers them ====================================================================

bool FontSmoothing() {
    BOOL smoothing = FALSE;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
}

bool BringOtherGameForward(const char* name, const char* title) {
    return platform::win32::BringForward(FindWindowW(text::Wide(name).c_str(), text::Wide(title).c_str()));
}

// ==== Processes, threads and time ====================================================================================

std::uint32_t ProcessId() { return GetCurrentProcessId(); }

ThreadId CurrentThread() { return GetCurrentThreadId(); }

void StartThread(void (*body)()) { std::thread(body).detach(); }

bool RunBounded(void (*body)(), unsigned ms) {
    HANDLE thread = CreateThread(nullptr, 0, [](void* p) -> DWORD {
        reinterpret_cast<void (*)()>(p)();
        return 0;
    }, reinterpret_cast<void*>(body), 0, nullptr);
    if (!thread) return false;
    const bool done = WaitForSingleObject(thread, ms) != WAIT_TIMEOUT;
    CloseHandle(thread);
    return done;
}

void SleepMs(unsigned ms) { ::Sleep(ms); }

void SleepForever() {
    for (;;) ::Sleep(INFINITE);
}

void Terminate(int code) {
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    for (;;) ::Sleep(INFINITE);
}

std::uint32_t TickCount() { return GetTickCount(); }

std::uint32_t Milliseconds() { return timeGetTime(); }

Date LocalDate() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return {t.wYear, t.wMonth, t.wDayOfWeek, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds};
}

void SetTimeZone(const char* rule) {
    _putenv_s("TZ", rule);
    _tzset();
}

namespace {

double FileTimeMs(const FILETIME& t) {
    return static_cast<double>((static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10000.0;
}

DWORD g_mainThread = 0;

} // namespace

void ThisThreadCpu(double* ms, std::uint64_t* cycles) {
    FILETIME created, exited, kernel, user;
    *ms = GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) ? FileTimeMs(kernel) + FileTimeMs(user) : 0;
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    *cycles = c;
}

void RememberMainThread() { g_mainThread = GetCurrentThreadId(); }

bool MainThreadCpu(double* ms, std::uint64_t* cycles) {
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, g_mainThread);
    if (!thread) return false;
    FILETIME created, exited, kernel, user;
    const bool known = GetThreadTimes(thread, &created, &exited, &kernel, &user);
    if (known) *ms = FileTimeMs(kernel) + FileTimeMs(user);
    ULONG64 c = 0;
    if (QueryThreadCycleTime(thread, &c)) *cycles = c;
    CloseHandle(thread);
    return known;
}

bool ProcessTimes(double* kernelMs, double* userMs, double* sinceStartMs) {
    FILETIME created, exited, kernel, user, now;
    GetSystemTimePreciseAsFileTime(&now);
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return false;
    *kernelMs = FileTimeMs(kernel);
    *userMs = FileTimeMs(user);
    *sinceStartMs = FileTimeMs(now) - FileTimeMs(created);
    return true;
}

// ==== Code addresses and stacks ======================================================================================

Image SelfImage() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&SelfImage), &self);
    MODULEINFO info{};
    GetModuleInformation(GetCurrentProcess(), self, &info, sizeof(info));
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    return {base, base + info.SizeOfImage};
}

std::string ModuleOf(std::uintptr_t address, std::uintptr_t* base) {
    HMODULE m = nullptr;
    if (address < 0x10000 ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &m))
        return {};
    char path[MAX_PATH] = "";
    GetModuleFileNameA(m, path, MAX_PATH);
    if (base) *base = reinterpret_cast<std::uintptr_t>(m);
    const char* name = std::strrchr(path, '\\');
    return name ? name + 1 : path;
}

bool DescribeAddress(std::uintptr_t address, std::string* text) {
    std::uintptr_t base = 0;
    const std::string module = ModuleOf(address, &base);
    if (module.empty()) return false;
    char line[MAX_PATH + 32];
    std::snprintf(line, sizeof(line), "%s+%X", module.c_str(), unsigned(address - base));
    *text = line;
    return true;
}

std::uintptr_t StackTop() { return reinterpret_cast<std::uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase); }

std::uintptr_t StackEnd(std::uintptr_t sp) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<const void*>(sp), &mbi, sizeof(mbi))) return 0;
    return reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

std::uintptr_t StackHere() { return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()); }

namespace {

// Whether `a` follows a CALL instruction in executable memory (a return address, not a stale code pointer).
bool AfterCallInstruction(std::uintptr_t a) {
    MEMORY_BASIC_INFORMATION mbi{};
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!VirtualQuery(reinterpret_cast<const void*>(a - 7), &mbi, sizeof(mbi)) || !(mbi.Protect & kExecutable)) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(a);
    if (p[-5] == 0xE8) return true;   // CALL rel32
    for (int n = 2; n <= 7; ++n)
        if (p[-n] == 0xFF && ((p[-n + 1] >> 3) & 7) == 2) return true;   // CALL r/m
    return false;
}

} // namespace

void LogMainThreadStack() {
    HANDLE thread = g_mainThread ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_mainThread) : nullptr;
    if (!thread) return;
    std::vector<std::uintptr_t> stack(4096);
    std::size_t words = 0;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (SuspendThread(thread) != DWORD(-1)) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (GetThreadContext(thread, &context) &&
            VirtualQuery(reinterpret_cast<const void*>(StackPointer(context)), &mbi, sizeof(mbi))) {
            const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            words = (std::min)((end - StackPointer(context)) / sizeof(std::uintptr_t), stack.size());
            // Through the kernel, not memcpy: the AddressSanitizer build would report the other thread's poisoned
            // stack slots.
            SIZE_T read = 0;
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(StackPointer(context)), stack.data(),
                              words * sizeof(std::uintptr_t), &read);
            words = read / sizeof(std::uintptr_t);
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    std::string where = "?";
    DescribeAddress(InstructionPointer(context), &where);
    Log("harness: main thread %lu at %08llX %s; return addresses on its stack:", g_mainThread,
        static_cast<unsigned long long>(InstructionPointer(context)), where.c_str());
    int logged = 0;
    for (std::size_t i = 0; i < words && logged < 48; ++i) {
        const std::uintptr_t a = stack[i];
        if (a < 0x10000 || !DescribeAddress(a, &where) || !AfterCallInstruction(a)) continue;
        Log("  %08llX %s", static_cast<unsigned long long>(a), where.c_str());
        ++logged;
    }
}

// ==== Crashes ========================================================================================================

namespace {

void (*g_firstChance)(const Fault&) = nullptr;
void (*g_fatal)(const Fault&) = nullptr;

// An MSVC throw: ExceptionInformation[0] the magic, [1] the thrown object's address, [2] its throw information, [3]
// the throwing module's base. The throw information lists the types a handler can catch it as, the thrown type first,
// each by its decorated name (".?AVexception@std@@" a std::exception), by 32-bit offsets from that base.
constexpr DWORD kMsvcThrow = 0xE06D7363;

using Ref = std::int32_t;
template <class T>
const T* At(Ref ref, std::uintptr_t base) {
    return reinterpret_cast<const T*>(base + static_cast<std::uintptr_t>(ref));
}
struct TypeDescriptor {
    const void* vtable;
    void* spare;
    char name[1];
};
struct CatchableType {
    unsigned properties;
    Ref type;
    int displacement[3];
    int size;
    Ref copy;
};
struct CatchableTypes {
    int count;
    Ref types[1];
};
struct ThrowInfo {
    unsigned attributes;
    Ref unwind;
    Ref forwardCompat;
    Ref catchable;
};

// ".?AVexception@std@@" -> "std::exception", ".PEAVFoo@@" -> "Foo*"; other decorations as they are.
std::string Undecorate(const char* name) {
    const char* p = name;
    if (*p == '.') ++p;
    if (*p == '?') ++p;
    bool pointer = false;
    if ((p[0] == 'P' || p[0] == 'Q') && p[1] == 'E') ++p;   // __ptr64
    if ((p[0] == 'P' || p[0] == 'Q') && (p[1] == 'A' || p[1] == 'B')) {
        pointer = true;
        ++p;
    }
    if (!((p[0] == 'A' || p[0] == 'B') && (p[1] == 'V' || p[1] == 'U'))) return name;
    p += 2;
    std::vector<std::string> parts;
    while (*p && *p != '@') {
        const char* at = std::strchr(p, '@');
        if (!at) break;
        parts.emplace_back(p, at);
        p = at + 1;
    }
    if (parts.empty()) return name;
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) out += (out.empty() ? "" : "::") + *it;
    return pointer ? out + "*" : out;
}

struct Thrown {
    char type[128];
    char what[512];
    bool standard;
};

// Reads the throw; false when anything in it is not readable. No C++ objects here (__try).
bool ReadThrow(const EXCEPTION_RECORD* record, Thrown* thrown) {
    __try {
        const auto* info = reinterpret_cast<const ThrowInfo*>(record->ExceptionInformation[2]);
        const auto* object = reinterpret_cast<const void*>(record->ExceptionInformation[1]);
        const std::uintptr_t base = record->NumberParameters > 3 ? record->ExceptionInformation[3] : 0;
        if (!info || !info->catchable) return false;
        const auto* catchable = At<CatchableTypes>(info->catchable, base);
        if (catchable->count <= 0) return false;
        const int count = catchable->count < 32 ? catchable->count : 32;
        const char* first = At<TypeDescriptor>(At<CatchableType>(catchable->types[0], base)->type, base)->name;
        CopyText(thrown->type, first);
        for (int i = 0; i < count && !thrown->standard; ++i) {
            const char* name = At<TypeDescriptor>(At<CatchableType>(catchable->types[i], base)->type, base)->name;
            thrown->standard = std::strcmp(name, ".?AVexception@std@@") == 0;
        }
        if (thrown->standard) CopyText(thrown->what, static_cast<const std::exception*>(object)->what());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return thrown->type[0] != '\0';
    }
}

std::string DescribeThrow(const EXCEPTION_RECORD* record) {
    if (!record || record->ExceptionCode != kMsvcThrow || record->NumberParameters < 3) return "";
    Thrown thrown{};
    if (!ReadThrow(record, &thrown)) return "a C++ throw whose type is unreadable";
    std::string out = Undecorate(thrown.type);
    if (thrown.standard) out += std::string(" (what: ") + thrown.what + ")";
    return out;
}

Fault FaultOf(const EXCEPTION_POINTERS* e) {
    Fault fault;
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    const CONTEXT* c = e->ContextRecord;
    fault.code = r->ExceptionCode;
    fault.pc = InstructionPointer(*c);
    fault.sp = StackPointer(*c);
    fault.address = r->NumberParameters > 1 ? r->ExceptionInformation[1] : 0;
    char text[64];
    std::snprintf(text, sizeof(text), "exception %08lX", r->ExceptionCode);
    fault.what = text;
    fault.cppThrow = r->ExceptionCode == kMsvcThrow;
    fault.thrown = DescribeThrow(r);
    char registers[256];
    std::snprintf(registers, sizeof(registers), "rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p rbp=%p rsp=%p",
                  reinterpret_cast<void*>(c->Rax), reinterpret_cast<void*>(c->Rbx), reinterpret_cast<void*>(c->Rcx),
                  reinterpret_cast<void*>(c->Rdx), reinterpret_cast<void*>(c->Rsi), reinterpret_cast<void*>(c->Rdi),
                  reinterpret_cast<void*>(c->Rbp), reinterpret_cast<void*>(c->Rsp));
    fault.registers = registers;
    return fault;
}

LONG CALLBACK FirstChance(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (g_firstChance) g_firstChance(FaultOf(e));
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* e) {
    if (g_fatal) g_fatal(FaultOf(e));
    return EXCEPTION_EXECUTE_HANDLER;
}

// A fault the runtime reports itself (abort, a pure virtual call, std::terminate): where it was called.
[[noreturn]] void RuntimeFault(const char* what, std::string thrown = {}) {
    Fault fault;
    fault.what = what;
    fault.thrown = std::move(thrown);
    fault.pc = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    fault.sp = StackHere();
    if (g_fatal) g_fatal(fault);
    Terminate(3);
}

std::string CurrentException() {
    try {
        if (std::exception_ptr e = std::current_exception()) std::rethrow_exception(e);
    } catch (const std::exception& e) {
        return std::string("std::exception (what: ") + e.what() + ")";
    } catch (...) {
        return "a C++ exception of another type";
    }
    return {};
}

} // namespace

void InstallFaultHandlers(void (*firstChance)(const Fault&), void (*fatal)(const Fault&)) {
    g_firstChance = firstChance;
    g_fatal = fatal;
    AddVectoredExceptionHandler(1, &FirstChance);
    SetUnhandledExceptionFilter(&Unhandled);
    // A crash must end the run: no Windows Error Reporting dialog, which freezes every thread (the watchdog
    // included) until someone closes it, and no runtime message box (abort's "abnormal program termination", a pure
    // virtual call's): those are the run's failures.
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, [](int) { RuntimeFault("abort"); });
    _set_purecall_handler([] { RuntimeFault("pure virtual call"); });
    std::set_terminate([] { RuntimeFault("std::terminate", CurrentException()); });
}

void ReassertFaultHandler() { SetUnhandledExceptionFilter(&Unhandled); }

// ==== A pair's shared memory and turns ===============================================================================

void* OpenSharedMemory(const std::string& name, std::size_t size) {
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(size),
                                        ("Local\\" + name).c_str());
    if (!mapping) return nullptr;
    return MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);   // the mapping lives as long as the process
}

void ReleaseSharedName(const std::string&) {}

bool Signal::Open(const std::string& name, std::uint32_t* shared) {
    word = shared;
    handle = CreateEventA(nullptr, FALSE, FALSE, ("Local\\" + name).c_str());   // auto-reset
    return handle != nullptr;
}

void Signal::Raise() { SetEvent(handle); }

bool Signal::Wait(unsigned ms) { return WaitForSingleObject(handle, ms) == WAIT_OBJECT_0; }

bool ProcessAlive(std::uint32_t pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

// ==== Network ========================================================================================================

namespace {

// The table GetExtendedUdpTable / GetExtendedTcpTable fills (grown as asked).
template <class Get>
bool Table(std::vector<std::uint8_t>& buffer, Get get) {
    for (int tries = 0; tries < 4; ++tries) {
        DWORD size = static_cast<DWORD>(buffer.size());
        const DWORD result = get(buffer.empty() ? nullptr : buffer.data(), &size);
        if (result == NO_ERROR) return true;
        if (result != ERROR_INSUFFICIENT_BUFFER) return false;
        buffer.resize(size + 4096);
    }
    return false;
}

std::string Endpoint(const char* kind, DWORD address, DWORD port) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&address);
    char text[64];
    std::snprintf(text, sizeof(text), "%s %u.%u.%u.%u:%u", kind, b[0], b[1], b[2], b[3], ntohs(static_cast<u_short>(port)));
    return text;
}

bool IsLoopback(DWORD address) { return (ntohl(address) >> 24) == 127; }

} // namespace

void ScanEndpoints(void (*outside)(const std::string& endpoint)) {
    // Never destroyed: the watchdog may be scanning while the process exits.
    static auto* udp = new std::vector<std::uint8_t>;
    static auto* tcp = new std::vector<std::uint8_t>;
    const DWORD self = GetCurrentProcessId();
    if (Table(*udp, [](void* t, DWORD* size) { return GetExtendedUdpTable(t, size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0); })) {
        const auto* table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(udp->data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
            if (table->table[i].dwOwningPid == self && !IsLoopback(table->table[i].dwLocalAddr))
                outside(Endpoint("UDP", table->table[i].dwLocalAddr, table->table[i].dwLocalPort));
    }
    if (Table(*tcp, [](void* t, DWORD* size) {
            return GetExtendedTcpTable(t, size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
        })) {
        const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(tcp->data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i)
            if (table->table[i].dwOwningPid == self && !IsLoopback(table->table[i].dwLocalAddr))
                outside(Endpoint("TCP listener", table->table[i].dwLocalAddr, table->table[i].dwLocalPort));
    }
}

// ==== The game window kept out of sight ==============================================================================

// No window of a harness game takes the foreground (SV1c) or is seen (WB12, WB12a). The game window (SDL's, made
// hidden by the harness's backend) is made a window that is never activated, a tool window (no taskbar button, not in
// Alt+Tab) and cloaked (DWMWA_CLOAK: Windows still composes it, so its swap chain presents as before, but nothing of it
// reaches the screen) before it is first shown. Its position, size and messages stay as they were, so the game and the
// frames (read from the renderer's target) do not change. EI_HARNESS_SHOW_WINDOW=1 leaves it in sight (to watch a
// run). Every top-level window of the process is checked as the run goes (WatchWindows): one seen is concealed too,
// and the report says so.

namespace {

bool g_conceal = false;
bool g_concealKnown = false;

bool Concealing() {
    if (!g_concealKnown) {
        g_concealKnown = true;
        g_conceal = platform::EnvironmentVariable("EI_HARNESS_SHOW_WINDOW") != "1";
        if (g_conceal)
            Log("harness: the game's windows are kept out of sight (cloaked tool windows; EI_HARNESS_SHOW_WINDOW=1 shows them)");
    }
    return g_conceal;
}

bool TopLevel(HWND window) {
    return GetAncestor(window, GA_PARENT) == GetDesktopWindow() && !(GetWindowLongW(window, GWL_STYLE) & WS_CHILD);
}

void Cloak(HWND window) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return;
    BOOL cloak = TRUE;
    const HRESULT result = DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak, sizeof(cloak));
    char cls[64] = "";
    GetClassNameA(window, cls, sizeof(cls));
    if (FAILED(result))
        Log("harness: window %p (class %s) not cloaked (%08X): it may be seen", static_cast<void*>(window), cls, unsigned(result));
    else
        Log("harness: window %p (class %s) cloaked", static_cast<void*>(window), cls);
}

// The windows' cloak states as DWM last reported them. The query is a call into DWM, which waits while DWM is busy
// (SV10: some 6 s of a pair game's 150 s, five queries a second): it is made on the sweeps and for a window not asked
// about yet.
std::vector<std::pair<HWND, DWORD>> g_cloakStates;
DWORD CloakState(HWND window, bool ask) {
    auto it = std::find_if(g_cloakStates.begin(), g_cloakStates.end(), [&](const auto& s) { return s.first == window; });
    if (it != g_cloakStates.end() && !ask) return it->second;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (it != g_cloakStates.end()) it->second = cloaked;
    else g_cloakStates.emplace_back(window, cloaked);
    return cloaked;
}

// What of a top-level window of the game the user could see, empty when nothing: shown (visible, not cloaked or
// minimised, on a monitor), a taskbar button, the foreground window.
std::string Seen(HWND window, bool ask) {
    std::string seen;
    if (!IsWindowVisible(window)) return seen;
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE));
    const DWORD cloaked = CloakState(window, ask);
    RECT rect{}, both{};
    const RECT screen{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                      GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const bool onScreen = GetWindowRect(window, &rect) && IntersectRect(&both, &rect, &screen);
    if (!cloaked && !IsIconic(window) && onScreen) seen += " shown";
    if ((exStyle & WS_EX_APPWINDOW) || (!(exStyle & WS_EX_TOOLWINDOW) && !GetWindow(window, GW_OWNER)))
        seen += " on the taskbar";
    if (GetForegroundWindow() == window) seen += " in front";
    if (seen.empty()) return seen;
    char text[200];
    char cls[64] = "";
    GetClassNameA(window, cls, sizeof(cls));
    std::snprintf(text, sizeof(text), "window %p (class %s, (%ld,%ld)-(%ld,%ld), ex style %08X, cloaked %lu):",
                  static_cast<void*>(window), cls, rect.left, rect.top, rect.right, rect.bottom, unsigned(exStyle),
                  static_cast<unsigned long>(cloaked));
    return text + seen;
}

unsigned g_checks;          // checks of the game's windows so far
std::string g_firstSeen;    // the first thing seen, for the report
std::string g_lastSeen;     // logged when it changes
std::vector<DWORD> g_windowThreads;   // the threads that own top-level windows of the game

struct Check {
    std::string seen;
    bool sweep = false;
    std::vector<HWND> toConceal;
};

BOOL CALLBACK CheckWindow(HWND window, LPARAM parameter) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    if (std::find(g_windowThreads.begin(), g_windowThreads.end(), thread) == g_windowThreads.end())
        g_windowThreads.push_back(thread);
    Check& check = *reinterpret_cast<Check*>(parameter);
    const std::string seen = Seen(window, check.sweep);
    if (!seen.empty()) {
        if (check.seen.empty()) check.seen = seen;
        check.toConceal.push_back(window);
    }
    return TRUE;
}

} // namespace

void ConcealWindow(void* native) {
    HWND window = static_cast<HWND>(native);
    if (!window) return;
    const DWORD thread = GetWindowThreadProcessId(window, nullptr);
    if (std::find(g_windowThreads.begin(), g_windowThreads.end(), thread) == g_windowThreads.end())
        g_windowThreads.push_back(thread);
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(window, GWL_EXSTYLE)) | WS_EX_NOACTIVATE;
    if (Concealing()) exStyle = (exStyle | WS_EX_TOOLWINDOW) & ~DWORD(WS_EX_APPWINDOW);
    SetWindowLongW(window, GWL_EXSTYLE, static_cast<LONG>(exStyle));
    if (Concealing()) Cloak(window);
}

// At most every 200 ms of real time (from the main loop's peeks; logged when what is seen changes). Without
// concealment only the foreground matters: a harness game never takes it (SV1c). A check walks the windows of the
// threads that own the game's windows with the cloak states last reported; every tenth (every 2 s) is a sweep: every
// top-level window of the desktop (a window of a thread not known yet) and DWM asked again (SV10: a sweep at every
// check cost 0.4-2 ms of wall time a frame).
void WatchWindows(void* game) {
    constexpr unsigned kSweepEvery = 10;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    Check check;
    check.sweep = g_checks % kSweepEvery == 0;
    if (Concealing() && check.sweep) {
        EnumWindows(&CheckWindow, reinterpret_cast<LPARAM>(&check));
    } else if (Concealing()) {
        const std::vector<DWORD> threads = g_windowThreads;
        for (DWORD thread : threads) EnumThreadWindows(thread, &CheckWindow, reinterpret_cast<LPARAM>(&check));
    } else if (GetForegroundWindow() == static_cast<HWND>(game)) {
        check.seen = "the game window is the foreground window";
    }
    for (HWND window : check.toConceal)
        if (Concealing() && TopLevel(window) && !CloakState(window, false)) {
            Cloak(window);
            CloakState(window, true);
        }
    const std::string seen = check.seen;
    ++g_checks;
    if (seen == g_lastSeen) return;
    g_lastSeen = seen;
    if (seen.empty()) {
        Log("harness: no window of the game is in sight any more");
        return;
    }
    if (g_firstSeen.empty()) g_firstSeen = seen;
    Log("harness: in sight: %s", seen.c_str());
}

void WindowReport(FILE* f) {
    if (!Concealing()) return;
    if (g_firstSeen.empty())
        std::fprintf(f, "\nWINDOW: out of sight (%u checks: no window of the game shown, on the taskbar or in front)\n",
                     g_checks);
    else
        std::fprintf(f, "\nWINDOW: in sight: %s\n", g_firstSeen.c_str());
}

bool ShowWithoutActivating(void* window, bool minimised) {
    ShowWindow(static_cast<HWND>(window), minimised ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
    return true;
}

std::string WindowClass(void* window) {
    char name[256] = "";
    GetClassNameA(static_cast<HWND>(window), name, sizeof(name));
    return name;
}

} // namespace ei::verify::sys
