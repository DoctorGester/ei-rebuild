// What the harness asks of a POSIX system (Linux; harness_system.h).
#include "harness_system.h"

#include "platform/log.h"
#include "platform/paths.h"
#include "platform/platform.h"
#include "platform/posix/platform_posix.h"

#include <arpa/inet.h>
#include <cxxabi.h>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <netinet/in.h>
#include <os/os_sync_wait_on_address.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#else
#include <linux/futex.h>
#endif
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/times.h>
#include <time.h>
#if !defined(__APPLE__)
#include <ucontext.h>   // Apple: sys/ucontext.h through signal.h
#endif
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#if !defined(__APPLE__)
extern "C" char __executable_start;
extern "C" char _etext;
#endif

namespace ei::verify::sys {

namespace {

// A harness path as the system names it: either separator, matched with Windows' case rules (paths.h).
std::string Native(const std::string& path) { return platform::NativePath(path).string(); }

} // namespace

// ==== Files ==========================================================================================================

FILE* OpenFile(const std::string& path, const char* mode) { return std::fopen(Native(path).c_str(), mode); }

FILE* OpenShared(const std::string& path) { return OpenFile(path, "rb"); }

bool Exists(const std::string& path) {
    struct stat s;
    return stat(Native(path).c_str(), &s) == 0;
}

long long FileSize(const std::string& path) {
    struct stat s;
    return stat(Native(path).c_str(), &s) == 0 && S_ISREG(s.st_mode) ? static_cast<long long>(s.st_size) : -1;
}

bool MakeFolder(const std::string& path) { return mkdir(Native(path).c_str(), 0777) == 0; }

bool RemoveFile(const std::string& path) { return unlink(Native(path).c_str()) == 0; }

bool CopyNewFile(const std::string& from, const std::string& to) {
    std::error_code error;
    return std::filesystem::copy_file(Native(from), Native(to), std::filesystem::copy_options::none, error);
}

std::string FullPath(const std::string& path) {
    if (path.empty()) return {};
    std::string whole = path;
    std::replace(whole.begin(), whole.end(), '/', '\\');
    if (whole[0] != '\\') whole = platform::CurrentFolder() + "\\" + whole;
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= whole.size()) {
        const std::size_t end = std::min(whole.find('\\', at), whole.size());
        const std::string part = whole.substr(at, end - at);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        at = end + 1;
    }
    std::string full;
    for (const std::string& part : parts) full += "\\" + part;
    if (full.empty() || whole.back() == '\\') full += "\\";
    return full;
}

long long FreeMegabytes(const std::string& folder) {
    struct statvfs s;
    if (folder.empty() || statvfs(Native(folder).c_str(), &s) != 0) return -1;
    return static_cast<long long>(s.f_bavail) * static_cast<long long>(s.f_frsize) / (1024 * 1024);
}

std::string ErrorText(unsigned long code) { return std::strerror(static_cast<int>(code)); }

std::string FileOfHandle(void* handle) {
    if (!handle) return {};
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1);   // platform_posix.cpp's handles
#if defined(__APPLE__)
    char path[MAXPATHLEN];
    return fcntl(fd, F_GETPATH, path) == 0 ? std::string(path) : std::string();
#else
    char link[64], target[4096];
    std::snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    const ssize_t n = readlink(link, target, sizeof(target) - 1);
    return n > 0 ? std::string(target, static_cast<std::size_t>(n)) : std::string();
#endif
}

bool WriteHandle(void* handle, const void* data, unsigned size, unsigned* written, unsigned long* error) {
    const int fd = handle ? static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1) : -1;
    const auto* bytes = static_cast<const char*>(data);
    unsigned done = 0;
    while (done < size) {
        const ssize_t n = write(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            *error = n ? static_cast<unsigned long>(errno) : static_cast<unsigned long>(ENOSPC);
            return false;
        }
        done += static_cast<unsigned>(n);
    }
    *written = done;
    return true;
}

const unsigned long kDiskFull = ENOSPC;

// ==== The game's calls as the system answers them ====================================================================


bool FontSmoothing() { return true; }

bool BringOtherGameForward(const char* name, const char* title) { return platform::posix::OtherInstanceRunning(name, title); }

// ==== Processes, threads and time ====================================================================================

std::uint32_t ProcessId() { return static_cast<std::uint32_t>(getpid()); }

ThreadId CurrentThread() {
#if defined(__APPLE__)
    std::uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return static_cast<ThreadId>(id);
#else
    return static_cast<ThreadId>(syscall(SYS_gettid));
#endif
}

void StartThread(void (*body)()) { std::thread(body).detach(); }

bool RunBounded(void (*body)(), unsigned ms) {
    auto* done = new std::atomic<bool>(false);   // left to the thread when it outlives the wait
    std::thread([body, done] {
        body();
        done->store(true);
    }).detach();
    for (unsigned waited = 0; waited < ms && !done->load(); waited += 10) SleepMs(10);
    return done->load();
}

void SleepMs(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void SleepForever() {
    for (;;) pause();
}

void Terminate(int code) { _exit(code); }

namespace {

std::uint32_t MonotonicMs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000);
}

} // namespace

std::uint32_t TickCount() { return MonotonicMs(); }

std::uint32_t Milliseconds() { return MonotonicMs(); }

Date LocalDate() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    std::tm t{};
    localtime_r(&now.tv_sec, &t);
    return {t.tm_year + 1900, t.tm_mon + 1, t.tm_wday, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
            static_cast<int>(now.tv_nsec / 1000000)};
}

void SetTimeZone(const char* rule) {
    setenv("TZ", rule, 1);
    tzset();
}

namespace {

pthread_t g_mainThread{};
pid_t g_mainThreadId = 0;
bool g_mainKnown = false;

double ClockMs(clockid_t clock, std::uint64_t* ns) {
    timespec t{};
    if (clock_gettime(clock, &t) != 0) return -1;
    const std::uint64_t total = static_cast<std::uint64_t>(t.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(t.tv_nsec);
    if (ns) *ns = total;
    return static_cast<double>(total) / 1e6;
}

} // namespace

// "Cycles" are the thread's CPU nanoseconds here: the timing's cycles-to-milliseconds ratio stays right.
void ThisThreadCpu(double* ms, std::uint64_t* cycles) {
    *ms = ClockMs(CLOCK_THREAD_CPUTIME_ID, cycles);
    if (*ms < 0) *ms = 0;
}

void RememberMainThread() {
    g_mainThread = pthread_self();
    g_mainThreadId = static_cast<pid_t>(CurrentThread());
    g_mainKnown = true;
}

bool MainThreadCpu(double* ms, std::uint64_t* cycles) {
#if defined(__APPLE__)
    // No CPU clock of another thread here: Mach's thread_info (user and system time, microseconds).
    if (!g_mainKnown) return false;
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(pthread_mach_thread_np(g_mainThread), THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info),
                    &count) != KERN_SUCCESS)
        return false;
    const std::uint64_t us = (static_cast<std::uint64_t>(info.user_time.seconds) + info.system_time.seconds) * 1000000ull +
                             static_cast<std::uint64_t>(info.user_time.microseconds) + info.system_time.microseconds;
    if (cycles) *cycles = us * 1000;
    *ms = static_cast<double>(us) / 1000.0;
    return true;
#else
    clockid_t clock;
    if (!g_mainKnown || pthread_getcpuclockid(g_mainThread, &clock) != 0) return false;
    *ms = ClockMs(clock, cycles);
    return *ms >= 0;
#endif
}

bool ProcessTimes(double* kernelMs, double* userMs, double* sinceStartMs) {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return false;
    *userMs = usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0;
    *kernelMs = usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
    // The process's start: field 22 of /proc/self/stat, in clock ticks since boot.
    *sinceStartMs = 0;
#if defined(__APPLE__)
    // The process's start from sysctl (wall-clock time).
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{};
    std::size_t length = sizeof(info);
    if (sysctl(mib, 4, &info, &length, nullptr, 0) == 0 && length > 0) {
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        const timeval& started = info.kp_proc.p_starttime;
        *sinceStartMs = (now.tv_sec - started.tv_sec) * 1000.0 + now.tv_nsec / 1e6 - started.tv_usec / 1e3;
    }
#else
    std::ifstream stat("/proc/self/stat");
    std::string text((std::istreambuf_iterator<char>(stat)), std::istreambuf_iterator<char>());
    const std::size_t close = text.rfind(')');
    if (close != std::string::npos) {
        std::istringstream fields(text.substr(close + 2));
        std::string field;
        for (int i = 3; i <= 22 && fields >> field; ++i)
            if (i == 22) {
                const double started = std::strtod(field.c_str(), nullptr) * 1000.0 / static_cast<double>(sysconf(_SC_CLK_TCK));
                timespec boot{};
                clock_gettime(CLOCK_BOOTTIME, &boot);
                *sinceStartMs = boot.tv_sec * 1000.0 + boot.tv_nsec / 1e6 - started;
            }
    }
#endif
    return true;
}

// ==== Code addresses and stacks ======================================================================================

Image SelfImage() {
#if defined(__APPLE__)
    // The executable's __TEXT segment, slid as loaded.
    unsigned long size = 0;
    const std::uint8_t* text = getsegmentdata(&_mh_execute_header, "__TEXT", &size);
    return {reinterpret_cast<std::uintptr_t>(text), reinterpret_cast<std::uintptr_t>(text) + size};
#else
    return {reinterpret_cast<std::uintptr_t>(&__executable_start), reinterpret_cast<std::uintptr_t>(&_etext)};
#endif
}

std::string ModuleOf(std::uintptr_t address, std::uintptr_t* base) {
    Dl_info info{};
    if (address < 0x10000 || !dladdr(reinterpret_cast<void*>(address), &info) || !info.dli_fname) return {};
    if (base) *base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
    const char* name = std::strrchr(info.dli_fname, '/');
    return name ? name + 1 : info.dli_fname;
}

bool DescribeAddress(std::uintptr_t address, std::string* text) {
    std::uintptr_t base = 0;
    const std::string module = ModuleOf(address, &base);
    if (module.empty()) return false;
    char line[512];
    std::snprintf(line, sizeof(line), "%s+%" PRIXPTR, module.c_str(), address - base);
    *text = line;
    return true;
}

std::uintptr_t StackTop() {
#if defined(__APPLE__)
    return reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));   // the stack's highest address
#else
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) != 0) return 0;
    void* low = nullptr;
    std::size_t size = 0;
    pthread_attr_getstack(&attr, &low, &size);
    pthread_attr_destroy(&attr);
    return reinterpret_cast<std::uintptr_t>(low) + size;
#endif
}

std::uintptr_t StackEnd(std::uintptr_t sp) {
#if defined(__APPLE__)
    // The region holding `sp` (Mach's region at or above it; 0 when it starts above sp).
    mach_vm_address_t address = sp;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info),
                       &count, &object) != KERN_SUCCESS || address > sp)
        return 0;
    return static_cast<std::uintptr_t>(address + size);
#else
    // The mapping holding `sp` (/proc/self/maps: "low-high perms ...").
    FILE* maps = std::fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    std::uintptr_t end = 0;
    while (std::fgets(line, sizeof(line), maps)) {
        std::uintptr_t low = 0, high = 0;
        if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &low, &high) == 2 && sp >= low && sp < high) {
            end = high;
            break;
        }
    }
    std::fclose(maps);
    return end;
#endif
}

std::uintptr_t StackHere() { return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)); }

namespace {

// The main thread's stack as SIGUSR2's handler there records it (backtrace: the frames the unwinder finds).
constexpr int kFrames = 64;
void* g_frames[kFrames];
std::atomic<int> g_frameCount{-1};

void RecordFrames(int) {
    const int n = backtrace(g_frames, kFrames);
    g_frameCount.store(n);
}

} // namespace

void LogMainThreadStack() {
    if (!g_mainKnown) return;
    struct sigaction action {};
    action.sa_handler = &RecordFrames;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, nullptr);
    g_frameCount.store(-1);
    if (pthread_kill(g_mainThread, SIGUSR2) != 0) return;
    for (int waited = 0; waited < 1500 && g_frameCount.load() < 0; waited += 5) SleepMs(5);
    const int n = g_frameCount.load();
    if (n < 0) {
        Log("harness: main thread %d did not answer for its stack", static_cast<int>(g_mainThreadId));
        return;
    }
    Log("harness: main thread %d; return addresses on its stack:", static_cast<int>(g_mainThreadId));
    // Frames 0-1 are the handler's own and the signal trampoline's.
    for (int i = 2; i < n; ++i) {
        std::string where = "?";
        DescribeAddress(reinterpret_cast<std::uintptr_t>(g_frames[i]), &where);
        Log("  %016" PRIXPTR " %s", reinterpret_cast<std::uintptr_t>(g_frames[i]), where.c_str());
    }
}

// ==== Crashes ========================================================================================================

namespace {

void (*g_firstChance)(const Fault&) = nullptr;
void (*g_fatal)(const Fault&) = nullptr;
std::string g_thrown;   // what std::terminate's handler found (an uncaught exception)

const char* SignalName(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    default: return "?";
    }
}

// The alternate stack the handlers run on (a stack overflow leaves no room on the thread's own).
alignas(16) char g_altStack[256 * 1024];

void OnSignal(int signal, siginfo_t* info, void* context) {
    static std::atomic<int> entered{0};
    if (entered.fetch_add(1) > 0) _exit(3);   // a fault inside the report
    Fault fault;
    fault.code = static_cast<std::uint32_t>(signal);
    fault.address = reinterpret_cast<std::uintptr_t>(info ? info->si_addr : nullptr);
    char what[64];
    std::snprintf(what, sizeof(what), "signal %d (%s)", signal, SignalName(signal));
    fault.what = signal == SIGABRT ? "abort" : what;
    fault.thrown = g_thrown;
    fault.cppThrow = !g_thrown.empty();
#if defined(__x86_64__)
    if (context) {
        const auto* uc = static_cast<const ucontext_t*>(context);
        const greg_t* r = uc->uc_mcontext.gregs;
        fault.pc = static_cast<std::uintptr_t>(r[REG_RIP]);
        fault.sp = static_cast<std::uintptr_t>(r[REG_RSP]);
        char registers[256];
        std::snprintf(registers, sizeof(registers), "rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rbp=%llx rsp=%llx",
                      static_cast<unsigned long long>(r[REG_RAX]), static_cast<unsigned long long>(r[REG_RBX]),
                      static_cast<unsigned long long>(r[REG_RCX]), static_cast<unsigned long long>(r[REG_RDX]),
                      static_cast<unsigned long long>(r[REG_RSI]), static_cast<unsigned long long>(r[REG_RDI]),
                      static_cast<unsigned long long>(r[REG_RBP]), static_cast<unsigned long long>(r[REG_RSP]));
        fault.registers = registers;
    }
#elif defined(__APPLE__) && defined(__aarch64__)
    if (context) {
        const auto* uc = static_cast<const ucontext_t*>(context);
        const auto& s = uc->uc_mcontext->__ss;
        fault.pc = static_cast<std::uintptr_t>(__darwin_arm_thread_state64_get_pc(s));
        fault.sp = static_cast<std::uintptr_t>(__darwin_arm_thread_state64_get_sp(s));
        char registers[256];
        std::snprintf(registers, sizeof(registers), "x0=%llx x1=%llx x2=%llx x3=%llx fp=%llx lr=%llx sp=%llx",
                      static_cast<unsigned long long>(s.__x[0]), static_cast<unsigned long long>(s.__x[1]),
                      static_cast<unsigned long long>(s.__x[2]), static_cast<unsigned long long>(s.__x[3]),
                      static_cast<unsigned long long>(__darwin_arm_thread_state64_get_fp(s)),
                      static_cast<unsigned long long>(__darwin_arm_thread_state64_get_lr(s)),
                      static_cast<unsigned long long>(fault.sp));
        fault.registers = registers;
    }
#endif
    if (!fault.sp) fault.sp = StackHere();
    if (signal != SIGABRT && g_firstChance) g_firstChance(fault);
    if (g_fatal) g_fatal(fault);
    _exit(3);
}

std::string CurrentException() {
    std::string type = "a C++ exception";
    if (const std::type_info* t = abi::__cxa_current_exception_type()) {
        int status = 0;
        char* name = abi::__cxa_demangle(t->name(), nullptr, nullptr, &status);
        type = name && status == 0 ? name : t->name();
        std::free(name);
    }
    try {
        if (std::exception_ptr e = std::current_exception()) std::rethrow_exception(e);
    } catch (const std::exception& e) {
        return type + " (what: " + e.what() + ")";
    } catch (...) {
    }
    return type;
}

} // namespace

void InstallFaultHandlers(void (*firstChance)(const Fault&), void (*fatal)(const Fault&)) {
    g_firstChance = firstChance;
    g_fatal = fatal;
    stack_t stack{};
    stack.ss_sp = g_altStack;
    stack.ss_size = sizeof(g_altStack);
    sigaltstack(&stack, nullptr);   // the main thread's: the game's work happens there
    struct sigaction action {};
    action.sa_sigaction = &OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(signal, &action, nullptr);
    std::set_terminate([] {
        g_thrown = CurrentException();
        std::abort();
    });
}

void ReassertFaultHandler() {}

// ==== A pair's shared memory and turns ===============================================================================

void* OpenSharedMemory(const std::string& name, std::size_t size) {
    const std::string object = "/" + name;
    const int fd = shm_open(object.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) return nullptr;
    struct stat s {};
    if (fstat(fd, &s) != 0 || (static_cast<std::size_t>(s.st_size) < size && ftruncate(fd, static_cast<off_t>(size)) != 0)) {
        close(fd);
        return nullptr;
    }
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return memory == MAP_FAILED ? nullptr : memory;
}

void ReleaseSharedName(const std::string& name) { shm_unlink(("/" + name).c_str()); }

// The futex word counts raises; a waiter remembers the count it has seen.
bool Signal::Open(const std::string&, std::uint32_t* shared) {
    word = shared;
    seen = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    return true;
}

void Signal::Raise() {
    __atomic_add_fetch(word, 1, __ATOMIC_RELEASE);
#if defined(__APPLE__)
    // The word is in memory the two games share (OpenSharedMemory): the shared form of the wait and wake.
    os_sync_wake_by_address_any(word, sizeof(*word), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
#else
    syscall(SYS_futex, word, FUTEX_WAKE, 1, nullptr, nullptr, 0);
#endif
}

bool Signal::Wait(unsigned ms) {
    std::uint32_t now = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    if (now == seen) {
#if defined(__APPLE__)
        os_sync_wait_on_address_with_timeout(word, seen, sizeof(*word), OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                             OS_CLOCK_MACH_ABSOLUTE_TIME, static_cast<std::uint64_t>(ms) * 1000000ull);
#else
        timespec timeout{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000) * 1000000L};
        syscall(SYS_futex, word, FUTEX_WAIT, seen, &timeout, nullptr, 0);
#endif
        now = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    }
    if (now == seen) return false;
    seen = now;   // every raise so far taken, as an auto-reset event's
    return true;
}

bool ProcessAlive(std::uint32_t pid) { return pid && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM); }

// ==== Network ========================================================================================================

namespace {

// The socket inodes this process holds (/proc/self/fd's "socket:[inode]" links).
std::set<unsigned long> OwnSockets() {
    std::set<unsigned long> inodes;
    DIR* folder = opendir("/proc/self/fd");
    if (!folder) return inodes;
    while (dirent* entry = readdir(folder)) {
        char link[300], target[128];
        std::snprintf(link, sizeof(link), "/proc/self/fd/%s", entry->d_name);
        const ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';
        unsigned long inode = 0;
        if (std::sscanf(target, "socket:[%lu]", &inode) == 1) inodes.insert(inode);
    }
    closedir(folder);
    return inodes;
}

// /proc/net/udp or tcp: "sl local_address rem_address st tx_queue:rx_queue tr:tm->when retrnsmt uid timeout inode";
// the address hex in network order as the machine reads it, the port hex.
void ScanTable(const char* file, const char* kind, bool listenersOnly, const std::set<unsigned long>& own,
               void (*outside)(const std::string&)) {
    FILE* f = std::fopen(file, "r");
    if (!f) return;
    char line[512];
    if (!std::fgets(line, sizeof(line), f)) {   // the heading
        std::fclose(f);
        return;
    }
    while (std::fgets(line, sizeof(line), f)) {
        unsigned address = 0, port = 0, state = 0;
        unsigned long inode = 0;
        if (std::sscanf(line, " %*d: %x:%x %*x:%*x %x %*x:%*x %*x:%*x %*x %*u %*u %lu", &address, &port, &state, &inode) != 4)
            continue;
        if (!own.count(inode) || (listenersOnly && state != 0x0A)) continue;   // 0x0A: TCP_LISTEN
        const auto* b = reinterpret_cast<const std::uint8_t*>(&address);
        if (b[0] == 127) continue;
        char text[64];
        std::snprintf(text, sizeof(text), "%s %u.%u.%u.%u:%u", kind, b[0], b[1], b[2], b[3], port);
        outside(text);
    }
    std::fclose(f);
}

} // namespace

void ScanEndpoints(void (*outside)(const std::string& endpoint)) {
#if defined(__APPLE__)
    // libproc's sockets of this process: its IPv4 UDP sockets and TCP listeners bound outside 127.x.
    const pid_t pid = getpid();
    const int bytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes <= 0) return;
    std::vector<proc_fdinfo> fds(static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo) + 16);
    const int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    for (int i = 0; i < got / static_cast<int>(sizeof(proc_fdinfo)); ++i) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
        socket_fdinfo info{};
        if (proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &info, sizeof(info)) != sizeof(info)) continue;
        const socket_info& s = info.psi;
        if (s.soi_family != AF_INET) continue;
        const bool udp = s.soi_kind == SOCKINFO_IN && s.soi_protocol == IPPROTO_UDP;
        const bool listener = s.soi_kind == SOCKINFO_TCP && s.soi_proto.pri_tcp.tcpsi_state == TSI_S_LISTEN;
        if (!udp && !listener) continue;
        const in_sockinfo& in = udp ? s.soi_proto.pri_in : s.soi_proto.pri_tcp.tcpsi_ini;
        const auto* b = reinterpret_cast<const std::uint8_t*>(&in.insi_laddr.ina_46.i46a_addr4.s_addr);
        if (b[0] == 127) continue;
        char text[64];
        std::snprintf(text, sizeof(text), "%s %u.%u.%u.%u:%u", udp ? "UDP" : "TCP listener", b[0], b[1], b[2], b[3],
                      static_cast<unsigned>(ntohs(static_cast<std::uint16_t>(in.insi_lport))));
        outside(text);
    }
#else
    const std::set<unsigned long> own = OwnSockets();
    ScanTable("/proc/net/udp", "UDP", false, own, outside);
    ScanTable("/proc/net/tcp", "TCP listener", true, own, outside);
#endif
}

// ==== The game window ================================================================================================

// No window manager call keeps a window out of sight here: the game window is shown as it is.
void ConcealWindow(void*) {}

void WatchWindows(void*) {}

void WindowReport(FILE*) {}

bool ShowWithoutActivating(void*, bool) { return false; }

std::string WindowClass(void*) { return {}; }

} // namespace ei::verify::sys

// ==== Windows-only tools ============================================================================================

// The sampling profiler (EI_PROFILE, profile_win32.cpp) and the debug-register watches (EI_WATCH_*, watch_win32.cpp)
// suspend the main thread and read or set its registers; this system has neither.
namespace ei {
void InstallProfiler() { ei::Log("profile: EI_PROFILE has no sampler on this system"); }
} // namespace ei

namespace ei::verify::watch {
bool Enabled() { return false; }
void Install() {}
void AfterWorldUpdate() {}
void ScrubStack() {}
} // namespace ei::verify::watch
