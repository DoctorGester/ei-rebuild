#pragma once

// What the harness itself asks of the system (verify build only): its threads, clocks, files, crash reports, stack
// dumps, a pair's shared memory, the endpoint checks and the window watch. One header for both systems; the answers
// are system_win32.cpp's and system_posix.cpp's (CMakeLists.txt builds the one for its system). The game's own calls
// that a harness run answers are the harness's platform backend's (tests/verify/platform/), not these.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The calling function's return address (the code that called it).
#if defined(_MSC_VER)
#include <intrin.h>
#define EI_HARNESS_CALLER() _ReturnAddress()
#else
#define EI_HARNESS_CALLER() __builtin_return_address(0)
#endif

namespace ei::verify::sys {

// The longest path the harness's fixed buffers hold (the game's own paths are strings).
inline constexpr int kMaxPath = 260;

// `from` into `to`, cut to fit (strcpy_s / strncpy_s with truncation).
template <std::size_t N>
void CopyText(char (&to)[N], const char* from) {
    const std::size_t n = from ? std::strlen(from) : 0;
    const std::size_t kept = n < N - 1 ? n : N - 1;
    if (kept) std::memcpy(to, from, kept);
    to[kept] = '\0';
}
// `from` appended to `to`, cut to fit.
template <std::size_t N>
void AppendText(char (&to)[N], const char* from) {
    const std::size_t at = std::strlen(to);
    const std::size_t n = from ? std::strlen(from) : 0;
    const std::size_t kept = at + n < N - 1 ? n : N - 1 - at;
    if (kept) std::memcpy(to + at, from, kept);
    to[at + kept] = '\0';
}

// ---- Files: the harness's own (UTF-8 paths, either separator; never the game's overlay) ----

FILE* OpenFile(const std::string& path, const char* mode);
// For reading while another writer has the file open (the run's log).
FILE* OpenShared(const std::string& path);
bool Exists(const std::string& path);
// The file's size in bytes, -1 when it is not there.
long long FileSize(const std::string& path);
// Makes the folder (its parent must exist); false when it could not, also when it exists already.
bool MakeFolder(const std::string& path);
// Removes the file; false when it could not (also when it is not there).
bool RemoveFile(const std::string& path);
// Copies `from` to `to`, which must not exist yet; false when it could not.
bool CopyNewFile(const std::string& from, const std::string& to);
// `path` in full in the game's form (backslashes; on POSIX "\home\..."): a relative path taken from the current
// folder, "." and ".." resolved, as Windows' GetFullPathName; "" when it cannot be.
std::string FullPath(const std::string& path);
// Free megabytes on the disk holding `folder`, -1 when unknown.
long long FreeMegabytes(const std::string& folder);
// A system error code's text (GetLastError's, errno's).
std::string ErrorText(unsigned long code);
// The file a handle of the game's file objects writes (platform::File's: a handle or a descriptor), as the system names
// it now; "" unknown.
std::string FileOfHandle(std::intptr_t handle);

// ---- Processes, threads and time ----

std::uint32_t ProcessId();
using ThreadId = std::uint64_t;
ThreadId CurrentThread();
// Starts `body` on a thread of its own (never joined).
void StartThread(void (*body)());
// Runs `body` on a thread of its own and waits at most `ms` for it; false when it is still running.
bool RunBounded(void (*body)(), unsigned ms);
void SleepMs(unsigned ms);
[[noreturn]] void SleepForever();
// Ends the process at once with `code`: no exit functions, no other thread runs on.
[[noreturn]] void Terminate(int code);
// The system's millisecond clocks: GetTickCount's and timeGetTime's on Windows (the game's two), the monotonic
// clock elsewhere.
std::uint32_t TickCount();
std::uint32_t Milliseconds();
// The local date and time now.
struct Date {
    int year, month, dayOfWeek, day, hour, minute, second, milliseconds;
};
Date LocalDate();
// The C library's time zone from now on (localtime, mktime): a POSIX TZ rule such as "EET-2" (UTC+2, no daylight
// saving), which glibc and the UCRT read alike.
void SetTimeZone(const char* rule);
// CPU time of the calling thread (ms) and its cycles where the system counts them (else 0).
void ThisThreadCpu(double* ms, std::uint64_t* cycles);
// CPU time of the main thread (Remember'd at the harness's start); false when unknown.
void RememberMainThread();
bool MainThreadCpu(double* ms, std::uint64_t* cycles);
// The process's kernel and user CPU time and the wall time since it started (ms).
bool ProcessTimes(double* kernelMs, double* userMs, double* sinceStartMs);

// ---- Code addresses and stacks ----

// This program's image.
struct Image {
    std::uintptr_t base = 0, end = 0;
};
Image SelfImage();
// "module+offset" for a code address in a loaded module; false when it is in none.
bool DescribeAddress(std::uintptr_t address, std::string* text);
// The file name (no folder) of the module holding `address` and its base; "" when none.
std::string ModuleOf(std::uintptr_t address, std::uintptr_t* base);
// The end (highest address) of the calling thread's stack.
std::uintptr_t StackTop();
// The end of the stack region holding `sp` (any thread's), 0 when unknown.
std::uintptr_t StackEnd(std::uintptr_t sp);
// The calling thread's stack pointer, near enough (a local's address).
std::uintptr_t StackHere();
// Logs (ei-rebuild.log) where the main thread is: its code address and the return addresses on its stack.
void LogMainThreadStack();

// ---- Crashes ----

// A fatal fault: an exception (Windows) or a signal (POSIX) the game did not handle, an abort, a pure virtual call.
struct Fault {
    std::uint32_t code = 0;        // the exception code (Windows) or the signal (POSIX)
    std::uintptr_t pc = 0, sp = 0;
    std::uintptr_t address = 0;    // the address an access fault touched
    std::string what;              // "exception C0000005", "signal 11 (SIGSEGV)", "abort", "pure virtual call"
    std::string thrown;            // a C++ throw's type and contents, "" when the fault is none
    bool cppThrow = false;
    std::string registers;         // "rax=... rbx=..." where known
};
// `firstChance`: faults of the kinds the game never raises on purpose (access violations, illegal instructions,
// divisions by zero, stack overflows), as they happen, whether or not handled later (Windows: first chance; POSIX:
// the fatal signals before `fatal`). `fatal`: a fault nothing handled; it must end the process. Also: no system
// error box, an abort and a pure virtual call are faults (no runtime message box), an uncaught C++ exception names
// what it threw.
void InstallFaultHandlers(void (*firstChance)(const Fault&), void (*fatal)(const Fault&));
// The fatal handler asserted again (the game may install its own unhandled-exception filter).
void ReassertFaultHandler();

// ---- A pair's shared memory and turns (harness_pair.h) ----

// A block of `size` bytes shared by the processes that open `name`, zero filled by its first opener; null on failure.
void* OpenSharedMemory(const std::string& name, std::size_t size);
// The name let go once every process has opened it (POSIX: unlinked, so nothing is left behind; the memory stays
// mapped; Windows: the mapping ends with its last process anyway).
void ReleaseSharedName(const std::string& name);
// A wake-up between two processes: one side's Raise lets the other side's Wait return (a raise while nobody waits is
// kept for the next Wait). `word` lies in the shared memory; `name` names it for the systems that need a handle.
class Signal {
public:
    bool Open(const std::string& name, std::uint32_t* word);
    void Raise();
    // Waits for a raise, at most `ms`; false on timeout.
    bool Wait(unsigned ms);

private:
    void* handle = nullptr;
    std::uint32_t* word = nullptr;
    std::uint32_t seen = 0;
};
// Whether the process `pid` still runs.
bool ProcessAlive(std::uint32_t pid);

// ---- Network: the process's endpoints outside loopback ----

// Calls `outside` with "UDP a.b.c.d:port" / "TCP listener a.b.c.d:port" for each UDP endpoint and TCP listener of this
// process bound to an address outside 127.0.0.0/8.
void ScanEndpoints(void (*outside)(const std::string& endpoint));

// ---- The game window kept out of sight (Windows; elsewhere nothing to do) ----

// Before the window is first shown: never activated, no taskbar button, cloaked (out of sight; frames still drawn)
// unless EI_HARNESS_SHOW_WINDOW=1.
void ConcealWindow(void* window);
// At most every 200 ms: whether any top-level window of the process could be seen (shown, on the taskbar, in front);
// what it finds is concealed and logged.
void WatchWindows(void* game);
// The report's WINDOW: line (concealed runs only).
void WindowReport(FILE* f);
// The window minimised or restored without activating it (the focus steps); false where the system has no such call
// (the backend asks SDL then).
bool ShowWithoutActivating(void* window, bool minimised);
// The window's class as the system names it; "" where windows have none.
std::string WindowClass(void* window);
// The client area keeps the size the harness sets even where the window does not fit on the screen (macOS: AppKit
// fits a titled window into the screen when it is shown, which made a 1024x768 CI runner's 960x720 client 960x645;
// `window` is the NSWindow, H62c). Nothing to do elsewhere: Windows and X11 without a window manager never fit it.
void KeepClientSize(void* window);

} // namespace ei::verify::sys
