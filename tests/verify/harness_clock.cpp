// The game's clocks in harness runs: virtual clock, held clocks, clock trace (harness_clock.h).
#include "harness_clock.h"

#include "../../src/hook/log.h"
#include "ei/main_loop.h"
#include "../../src/game/random.h"

#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ei::verify::clock {

namespace {

// game.exe's import table.
constexpr std::uintptr_t kTimeGetTimeSlot = 0x0073B6A0;
constexpr std::uintptr_t kGetTickCountSlot = 0x0073B344;
constexpr std::uintptr_t kSleepSlot = 0x0073B1A4;
constexpr std::uintptr_t kWaitForSingleObjectSlot = 0x0073B1EC;
constexpr std::uintptr_t kGetMessageTimeSlot = 0x0073B5BC;
constexpr std::uintptr_t kGetMessageASlot = 0x0073B454;
constexpr std::uintptr_t kGetLocalTimeSlot = 0x0073B1DC;
constexpr std::uintptr_t kGetSystemTimeSlot = 0x0073B228;
constexpr std::uintptr_t kCreateThreadSlot = 0x0073B348;

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// The process's first thread: the one with the earliest creation time. ei_launch starts the game
// suspended and loads ei_hook.dll from a thread of its own, so at that point the first thread is
// the one that will run WinMain.
DWORD FindMainThread() {
    DWORD found = 0;
    ULONGLONG earliest = ~0ull;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        FILETIME created, exited, kernel, user;
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            ULONGLONG when = (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime;
            if (when < earliest) {
                earliest = when;
                found = entry.th32ThreadID;
            }
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return found;
}

DWORD g_mainThread = 0;
void FindMainThreadOnce() {
    if (g_mainThread) return;
    g_mainThread = FindMainThread();
    Log("harness: main thread %lu", g_mainThread);
}
bool OnMainThread() { return GetCurrentThreadId() == g_mainThread; }

// The caller of the clock function the trace wraps (the trace's own return address otherwise).
thread_local const void* t_tracedCaller = nullptr;
const void* CallerOf(const void* returnAddress) { return t_tracedCaller ? t_tracedCaller : returnAddress; }

// ---- Virtual clock -------------------------------------------------------------------------------

constexpr std::uintptr_t kMainLoopTimeReturn = 0x004571E2;   // after the main loop's timeGetTime call
// As many as the main loop runs in one pass (kLongestPassTicks, app_main.cpp): fewer frames to draw.
constexpr unsigned kUpdatesPerFrame = 5;

bool g_virtual = false;
volatile bool g_frozen = false;
volatile DWORD g_now = 1000000;
const unsigned* g_tickLength = nullptr;
decltype(&::timeGetTime) g_timeGetTime;
decltype(&::GetTickCount) g_getTickCount;
decltype(&::Sleep) g_sleep;
decltype(&::WaitForSingleObject) g_waitForSingleObject;

// Whether a timeGetTime call is the main loop's frame time: the original WinMain's by its return
// address, the rewritten one's (src/game/app_main.cpp) by the site it names.
bool IsMainLoopTime(const void* caller) {
    if (reinterpret_cast<std::uintptr_t>(caller) == kMainLoopTimeReturn) return true;
    return ei::mainloop::g_site == ei::mainloop::Site::FrameTime && OnMainThread();
}

// Real time (our own GetTickCount import) when the main thread last moved the clock.
volatile DWORD g_realAtAdvance = 0;

void Advance(DWORD ms) {
    if (g_frozen || ms == 0) return;
    g_now = g_now + ms;
    g_realAtAdvance = GetTickCount();
}

// What other threads read: the virtual time plus the real time since the main thread last moved
// it, never going back. They do not move the clock, but they see time pass while the main thread
// stands still: the new-game load waits on one of them. PlayJingle / the calm music (sound_music.cpp,
// WaitAndEndAfter) busy-wait with Sleep(0) on the main thread until the stream thread takes up the
// next file, and the stream thread does so only when the music fader (on timeGetTime) has finished
// fading out; on a clock that only the main thread moves that never happened. Nothing those threads
// time touches game state (the music volume, the cursor's frame).
volatile LONG g_otherNow = 0;
DWORD OtherThreadsNow() {
    const DWORD now = g_now;
    const DWORD candidate = g_frozen ? now : now + (GetTickCount() - g_realAtAdvance);
    LONG seen = g_otherNow;
    while (static_cast<LONG>(candidate - static_cast<DWORD>(seen)) > 0) {
        LONG previous = InterlockedCompareExchange(&g_otherNow, static_cast<LONG>(candidate), seen);
        if (previous == seen) return candidate;
        seen = previous;
    }
    return static_cast<DWORD>(seen);
}

// ---- Sound ends on the virtual clock -------------------------------------------------------------
// The game learns that a voice's sample has ended from Miles' end-of-sample callback (006B75E0, on
// Miles' timer thread: the slot's `finished`), and that feeds game state: a unit talks (server flag
// kFlagTalking, unit_client_speech.cpp) until its speech sample ends, scripts wait on that (zone1's
// intro), and FindPlaying frees voices. Played in real time, that made runs part. On the virtual
// clock the harness keeps the callback from Miles and calls it itself, from the main loop's frame
// time, once the sample's length (Miles' own ms position and loop count at the start) has passed
// on the virtual clock. Stopped samples (PauseAll) keep what was left until resumed.
constexpr std::uintptr_t kAilRegisterEosCallbackSlot = 0x0073B75C;
constexpr std::uintptr_t kAilSetSampleMsPositionSlot = 0x0073B768;
constexpr std::uintptr_t kAilStartSampleSlot = 0x0073B76C;
constexpr std::uintptr_t kAilSetSampleLoopCountSlot = 0x0073B778;
constexpr std::uintptr_t kAilResumeSampleSlot = 0x0073B780;
constexpr std::uintptr_t kAilStopSampleSlot = 0x0073B784;
constexpr std::uintptr_t kAilEndSampleSlot = 0x0073B7D8;
using EosCallback = void(__stdcall*)(void*);
using RegisterEosFn = EosCallback(__stdcall*)(void*, EosCallback);
using SampleFn = void(__stdcall*)(void*);
using SampleIntFn = void(__stdcall*)(void*, int);
using MsPositionFn = void(__stdcall*)(void*, long*, long*);
RegisterEosFn g_ailRegisterEos;
SampleIntFn g_ailSetSampleMsPosition, g_ailSetSampleLoopCount;
SampleFn g_ailStartSample, g_ailResumeSample, g_ailStopSample, g_ailEndSample;
MsPositionFn g_ailSampleMsPosition;

struct Voice {
    void* sample;
    EosCallback callback = nullptr;
    int loops = 1;            // 0: forever
    bool running = false;     // started, its end not yet called
    bool stopped = false;     // stopped while running: `left` is what remains
    DWORD end = 0;            // virtual time of its end (running, finite)
    DWORD left = 0;
    bool forever = false;
};
std::vector<Voice> g_voices;   // in the order they first appeared (the order ends are called in)
CRITICAL_SECTION g_voiceLock;

Voice& VoiceOf(void* sample) {
    for (Voice& v : g_voices)
        if (v.sample == sample) return v;
    g_voices.push_back(Voice{sample});
    return g_voices.back();
}

// The milliseconds the sample still plays from its current position, by Miles.
DWORD Remaining(const Voice& v, bool* forever) {
    long total = 0, current = 0;
    if (g_ailSampleMsPosition) g_ailSampleMsPosition(v.sample, &total, &current);
    *forever = v.loops == 0;
    long rest = total > current ? total - current : 0;
    if (v.loops > 1) rest += total * (v.loops - 1);
    return static_cast<DWORD>(rest);
}

void StartTimer(Voice& v) {
    v.running = true;
    v.stopped = false;
    v.end = g_now + Remaining(v, &v.forever);
    RandomNote("sound start at %lu: end %lu%s loops %d", g_now, v.end, v.forever ? " forever" : "", v.loops);
}

EosCallback __stdcall HeldRegisterEos(void* sample, EosCallback callback) {
    EnterCriticalSection(&g_voiceLock);
    Voice& v = VoiceOf(sample);
    EosCallback previous = v.callback;
    v.callback = callback;
    LeaveCriticalSection(&g_voiceLock);
    g_ailRegisterEos(sample, nullptr);
    return previous;
}
void __stdcall TimedSetLoopCount(void* sample, int count) {
    g_ailSetSampleLoopCount(sample, count);
    EnterCriticalSection(&g_voiceLock);
    VoiceOf(sample).loops = count;
    LeaveCriticalSection(&g_voiceLock);
}
void __stdcall TimedStartSample(void* sample) {
    g_ailStartSample(sample);
    EnterCriticalSection(&g_voiceLock);
    StartTimer(VoiceOf(sample));
    LeaveCriticalSection(&g_voiceLock);
}
void __stdcall TimedSetMsPosition(void* sample, int ms) {
    g_ailSetSampleMsPosition(sample, ms);
    EnterCriticalSection(&g_voiceLock);
    Voice& v = VoiceOf(sample);
    if (v.running && !v.stopped) StartTimer(v);
    LeaveCriticalSection(&g_voiceLock);
}
void __stdcall TimedStopSample(void* sample) {
    g_ailStopSample(sample);
    EnterCriticalSection(&g_voiceLock);
    Voice& v = VoiceOf(sample);
    if (v.running && !v.stopped) {
        v.stopped = true;
        v.left = static_cast<LONG>(v.end - g_now) > 0 ? v.end - g_now : 0;
    }
    LeaveCriticalSection(&g_voiceLock);
}
void __stdcall TimedResumeSample(void* sample) {
    g_ailResumeSample(sample);
    EnterCriticalSection(&g_voiceLock);
    Voice& v = VoiceOf(sample);
    if (v.running && v.stopped) {
        v.stopped = false;
        v.end = g_now + v.left;
    }
    LeaveCriticalSection(&g_voiceLock);
}
void __stdcall TimedEndSample(void* sample) {
    g_ailEndSample(sample);
    EnterCriticalSection(&g_voiceLock);
    VoiceOf(sample).running = false;
    LeaveCriticalSection(&g_voiceLock);
}

// The ends due on the virtual clock (called after each frame's advance).
void CallSampleEnds() {
    std::vector<std::pair<EosCallback, void*>> due;
    EnterCriticalSection(&g_voiceLock);
    for (Voice& v : g_voices) {
        if (!v.running || v.stopped || v.forever || static_cast<LONG>(g_now - v.end) < 0) continue;
        v.running = false;
        RandomNote("sound end at %lu", g_now);
        if (v.callback) due.push_back({v.callback, v.sample});
    }
    LeaveCriticalSection(&g_voiceLock);
    for (auto& [callback, sample] : due) callback(sample);
}

void InstallSampleTimers() {
    InitializeCriticalSection(&g_voiceLock);
    if (HMODULE mss = GetModuleHandleA("mss32.dll"))
        g_ailSampleMsPosition = reinterpret_cast<MsPositionFn>(GetProcAddress(mss, "_AIL_sample_ms_position@12"));
    if (!g_ailSampleMsPosition) Log("harness: no _AIL_sample_ms_position in mss32.dll: sounds end at once");
    PatchSlot(kAilRegisterEosCallbackSlot, &HeldRegisterEos, g_ailRegisterEos);
    PatchSlot(kAilSetSampleMsPositionSlot, &TimedSetMsPosition, g_ailSetSampleMsPosition);
    PatchSlot(kAilStartSampleSlot, &TimedStartSample, g_ailStartSample);
    PatchSlot(kAilSetSampleLoopCountSlot, &TimedSetLoopCount, g_ailSetSampleLoopCount);
    PatchSlot(kAilResumeSampleSlot, &TimedResumeSample, g_ailResumeSample);
    PatchSlot(kAilStopSampleSlot, &TimedStopSample, g_ailStopSample);
    PatchSlot(kAilEndSampleSlot, &TimedEndSample, g_ailEndSample);
}

DWORD WINAPI VirtualTimeGetTime() {
    if (!OnMainThread()) return OtherThreadsNow();
    if (IsMainLoopTime(CallerOf(_ReturnAddress()))) {
        Advance(kUpdatesPerFrame * *g_tickLength);
        CallSampleEnds();
    }
    return g_now;
}
DWORD WINAPI VirtualGetTickCount() { return OnMainThread() ? g_now : OtherThreadsNow(); }

// The main thread's sleeps and timed waits take no real time: the clock moves by what they would
// have waited. Other threads wait for real (and only read the clock).
void WINAPI VirtualSleep(DWORD ms) {
    if (!OnMainThread()) return g_sleep(ms);
    Advance(ms);
    g_sleep(0);   // still let other threads run
}
DWORD WINAPI VirtualWaitForSingleObject(HANDLE object, DWORD ms) {
    if (!OnMainThread() || ms == 0 || ms == INFINITE) return g_waitForSingleObject(object, ms);
    DWORD result = g_waitForSingleObject(object, 0);
    if (result != WAIT_TIMEOUT) return result;
    Advance(ms);
    return WAIT_TIMEOUT;
}

// ---- Held clocks ("freeze" in a real-time run) ---------------------------------------------------

struct HeldClock {
    DWORD(WINAPI* base)() = nullptr;
    DWORD offset = 0;   // the game's time is base() - offset while running
    DWORD heldAt = 0;
    DWORD Read() const { return g_frozen ? heldAt : base() - offset; }
    void Hold() { heldAt = base() - offset; }
    void Resume() { offset = base() - heldAt; }
};
HeldClock g_heldTimeGetTime, g_heldTickCount;
DWORD WINAPI HeldTimeGetTime() { return g_heldTimeGetTime.Read(); }
DWORD WINAPI HeldGetTickCount() { return g_heldTickCount.Read(); }

// ---- Trace ---------------------------------------------------------------------------------------

enum Fn : std::uint8_t {
    kTimeGetTime, kGetTickCount, kGetTickCount64, kQueryPerformanceCounter, kGetMessageTime, kGetLocalTime,
    kGetSystemTime, kGetSystemTimeAsFileTime, kSleep, kSleepEx, kWaitForSingleObject, kWaitForSingleObjectEx,
    kWaitForMultipleObjects, kWaitForMultipleObjectsEx, kMsgWaitForMultipleObjects, kMsgWaitForMultipleObjectsEx,
    kGetMessageA, kTimeSetEvent, kSetTimer, kCreateThread, kFnCount
};
constexpr const char* kFnNames[kFnCount] = {
    "timeGetTime", "GetTickCount", "GetTickCount64", "QueryPerformanceCounter", "GetMessageTime", "GetLocalTime",
    "GetSystemTime", "GetSystemTimeAsFileTime", "Sleep", "SleepEx", "WaitForSingleObject", "WaitForSingleObjectEx",
    "WaitForMultipleObjects", "WaitForMultipleObjectsEx", "MsgWaitForMultipleObjects", "MsgWaitForMultipleObjectsEx",
    "GetMessageA", "timeSetEvent", "SetTimer", "CreateThread",
};

struct Entry {
    Fn fn;
    DWORD thread;
    std::uintptr_t caller;
    volatile unsigned long count;
    unsigned long dumped;     // count at the last dump (the trace thread's)
    unsigned long long ms;    // the timeouts / sleeps asked for, summed
};
constexpr int kMaxEntries = 4096;
// Append-only: a new entry is filled in before the count that publishes it grows (under the lock),
// and the counts are bumped without the lock (a busy-waiting main thread kept the lock to itself).
Entry g_entries[kMaxEntries];
volatile LONG g_entryCount = 0;
SRWLOCK g_traceLock = SRWLOCK_INIT;
bool g_tracing = false;

void DescribeAddress(std::uintptr_t a, char* out, std::size_t size) {
    if (a >= 0x00401000 && a < 0x00730000) {
        std::snprintf(out, size, "%08X", unsigned(a));
        return;
    }
    HMODULE module = nullptr;
    char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(a), &module))
        GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = std::strrchr(path, '\\');
    std::snprintf(out, size, "%s+%X", name ? name + 1 : path, unsigned(a - reinterpret_cast<std::uintptr_t>(module)));
}

void DescribeThread(DWORD thread, char* out, std::size_t size) {
    if (thread == g_mainThread) std::snprintf(out, size, "main");
    else std::snprintf(out, size, "thread %lu", thread);
}

void Record(Fn fn, const void* caller, DWORD ms, bool hasMs) {
    const DWORD thread = GetCurrentThreadId();
    const auto at = reinterpret_cast<std::uintptr_t>(caller);
    // An entry belongs to one thread, so only that thread changes its counts.
    Entry* entry = nullptr;
    const LONG count = g_entryCount;
    for (LONG i = 0; i < count; ++i) {
        Entry& e = g_entries[i];
        if (e.fn == fn && e.thread == thread && e.caller == at) {
            entry = &e;
            break;
        }
    }
    bool added = false;
    if (!entry) {
        AcquireSRWLockExclusive(&g_traceLock);
        if (g_entryCount < kMaxEntries) {
            entry = &g_entries[g_entryCount];
            *entry = Entry{fn, thread, at, 0, 0, 0};
            InterlockedIncrement(&g_entryCount);
            added = true;
        }
        ReleaseSRWLockExclusive(&g_traceLock);
    }
    if (entry) {
        ++entry->count;
        if (hasMs && ms != INFINITE) entry->ms += ms;
    }
    if (added) {
        char where[MAX_PATH + 16], who[32];
        DescribeAddress(at, where, sizeof(where));
        DescribeThread(thread, who, sizeof(who));
        if (hasMs) Log("clocktrace: new %s(%ld) from %s on %s (virtual %lu)", kFnNames[fn], long(ms), where, who, g_now);
        else Log("clocktrace: new %s from %s on %s (virtual %lu)", kFnNames[fn], where, who, g_now);
    }
}

// The milliseconds a call waits, from the argument at `index` (-1: none).
template <int kIndex, class... A>
DWORD WaitArgument(A... a) {
    if constexpr (kIndex < 0) {
        ((void)a, ...);
        return 0;
    } else {
        auto value = std::get<kIndex>(std::forward_as_tuple(a...));
        if constexpr (std::is_integral_v<decltype(value)>) return static_cast<DWORD>(value);
        else return 0;
    }
}

// One traced import: `inner` is what the slot held (game.exe) or the real export (other modules).
template <Fn kFn, bool kGame, int kWaitIndex, class R, class... A>
struct Traced {
    static inline R(WINAPI* inner)(A...) = nullptr;
    static R WINAPI Call(A... a) {
        const void* caller = _ReturnAddress();
        Record(kFn, caller, WaitArgument<kWaitIndex>(a...), kWaitIndex >= 0);
        const void* outer = t_tracedCaller;
        t_tracedCaller = caller;
        if constexpr (std::is_void_v<R>) {
            inner(a...);
            t_tracedCaller = outer;
        } else {
            R result = inner(a...);
            t_tracedCaller = outer;
            return result;
        }
    }
};

template <bool kGame>
struct TracedSet {
    using TimeGetTime = Traced<kTimeGetTime, kGame, -1, DWORD>;
    using GetTickCount = Traced<kGetTickCount, kGame, -1, DWORD>;
    using GetTickCount64 = Traced<kGetTickCount64, kGame, -1, ULONGLONG>;
    using QueryPerformanceCounter = Traced<kQueryPerformanceCounter, kGame, -1, BOOL, LARGE_INTEGER*>;
    using GetMessageTime = Traced<kGetMessageTime, kGame, -1, LONG>;
    using GetLocalTime = Traced<kGetLocalTime, kGame, -1, void, LPSYSTEMTIME>;
    using GetSystemTime = Traced<kGetSystemTime, kGame, -1, void, LPSYSTEMTIME>;
    using GetSystemTimeAsFileTime = Traced<kGetSystemTimeAsFileTime, kGame, -1, void, LPFILETIME>;
    using Sleep = Traced<kSleep, kGame, 0, void, DWORD>;
    using SleepEx = Traced<kSleepEx, kGame, 0, DWORD, DWORD, BOOL>;
    using WaitForSingleObject = Traced<kWaitForSingleObject, kGame, 1, DWORD, HANDLE, DWORD>;
    using WaitForSingleObjectEx = Traced<kWaitForSingleObjectEx, kGame, 1, DWORD, HANDLE, DWORD, BOOL>;
    using WaitForMultipleObjects = Traced<kWaitForMultipleObjects, kGame, 3, DWORD, DWORD, const HANDLE*, BOOL, DWORD>;
    using WaitForMultipleObjectsEx =
        Traced<kWaitForMultipleObjectsEx, kGame, 3, DWORD, DWORD, const HANDLE*, BOOL, DWORD, BOOL>;
    using MsgWaitForMultipleObjects =
        Traced<kMsgWaitForMultipleObjects, kGame, 3, DWORD, DWORD, const HANDLE*, BOOL, DWORD, DWORD>;
    using MsgWaitForMultipleObjectsEx =
        Traced<kMsgWaitForMultipleObjectsEx, kGame, 2, DWORD, DWORD, const HANDLE*, DWORD, DWORD, DWORD>;
    using GetMessageA = Traced<kGetMessageA, kGame, -1, BOOL, LPMSG, HWND, UINT, UINT>;
    using TimeSetEvent = Traced<kTimeSetEvent, kGame, 0, MMRESULT, UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT>;
    using SetTimer = Traced<kSetTimer, kGame, 2, UINT_PTR, HWND, UINT_PTR, UINT, TIMERPROC>;
    using CreateThread = Traced<kCreateThread, kGame, -1, HANDLE, LPSECURITY_ATTRIBUTES, SIZE_T,
                                LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD>;
};

// game.exe's imports: wrap whatever the slot holds now (the virtual or held clock, or the real one).
template <class T>
void TraceSlot(std::uintptr_t slot) {
    PatchSlot(slot, &T::Call, T::inner);
}

void TraceGameImports() {
    using S = TracedSet<true>;
    TraceSlot<S::TimeGetTime>(kTimeGetTimeSlot);
    TraceSlot<S::GetTickCount>(kGetTickCountSlot);
    TraceSlot<S::Sleep>(kSleepSlot);
    TraceSlot<S::WaitForSingleObject>(kWaitForSingleObjectSlot);
    TraceSlot<S::GetMessageTime>(kGetMessageTimeSlot);
    TraceSlot<S::GetMessageA>(kGetMessageASlot);
    TraceSlot<S::GetLocalTime>(kGetLocalTimeSlot);
    TraceSlot<S::GetSystemTime>(kGetSystemTimeSlot);
    TraceSlot<S::CreateThread>(kCreateThreadSlot);
}

// Other modules' imports, by name, wrapped around the real export.
struct NamedHook {
    const char* name;
    void* call;
    void** inner;
};
template <class T>
NamedHook Named(const char* name) {
    return {name, reinterpret_cast<void*>(&T::Call), reinterpret_cast<void**>(&T::inner)};
}

const std::vector<NamedHook>& ModuleHooks() {
    using S = TracedSet<false>;
    static const std::vector<NamedHook> hooks = {
        Named<S::TimeGetTime>("timeGetTime"),
        Named<S::GetTickCount>("GetTickCount"),
        Named<S::GetTickCount64>("GetTickCount64"),
        Named<S::QueryPerformanceCounter>("QueryPerformanceCounter"),
        Named<S::GetMessageTime>("GetMessageTime"),
        Named<S::GetLocalTime>("GetLocalTime"),
        Named<S::GetSystemTime>("GetSystemTime"),
        Named<S::GetSystemTimeAsFileTime>("GetSystemTimeAsFileTime"),
        Named<S::Sleep>("Sleep"),
        Named<S::SleepEx>("SleepEx"),
        Named<S::WaitForSingleObject>("WaitForSingleObject"),
        Named<S::WaitForSingleObjectEx>("WaitForSingleObjectEx"),
        Named<S::WaitForMultipleObjects>("WaitForMultipleObjects"),
        Named<S::WaitForMultipleObjectsEx>("WaitForMultipleObjectsEx"),
        Named<S::MsgWaitForMultipleObjects>("MsgWaitForMultipleObjects"),
        Named<S::MsgWaitForMultipleObjectsEx>("MsgWaitForMultipleObjectsEx"),
        Named<S::TimeSetEvent>("timeSetEvent"),
        Named<S::SetTimer>("SetTimer"),
        Named<S::CreateThread>("CreateThread"),
    };
    return hooks;
}

std::vector<HMODULE> g_tracedModules;

// Wraps the traced functions in the import table of `module` (by name; ordinal imports are left).
void TraceModuleImports(HMODULE module) {
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    int wrapped = 0;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        HMODULE from = GetModuleHandleA(reinterpret_cast<const char*>(base + d->Name));
        if (!from || !d->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            for (const NamedHook& hook : ModuleHooks()) {
                if (std::strcmp(name, hook.name) != 0) continue;
                if (!*hook.inner) *hook.inner = reinterpret_cast<void*>(GetProcAddress(from, name));
                if (!*hook.inner) break;
                void* replacement = hook.call;
                void* old = nullptr;
                PatchSlot(reinterpret_cast<std::uintptr_t>(&slots->u1.Function), replacement, old);
                ++wrapped;
                break;
            }
        }
    }
    char path[MAX_PATH] = "?";
    GetModuleFileNameA(module, path, MAX_PATH);
    Log("clocktrace: %d imports of %s wrapped", wrapped, path);
}

// The DLLs to trace: those outside the Windows folder, other than game.exe and ei_hook.dll.
void TraceModules() {
    HMODULE modules[512];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return;
    char windows[MAX_PATH] = "";
    UINT windowsLength = GetWindowsDirectoryA(windows, MAX_PATH);
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&TraceModules), &self);
    for (DWORD i = 0; i < bytes / sizeof(HMODULE) && i < 512; ++i) {
        HMODULE m = modules[i];
        if (m == GetModuleHandleA(nullptr) || m == self) continue;
        if (std::find(g_tracedModules.begin(), g_tracedModules.end(), m) != g_tracedModules.end()) continue;
        g_tracedModules.push_back(m);
        char path[MAX_PATH];
        if (!GetModuleFileNameA(m, path, MAX_PATH)) continue;
        if (windowsLength && _strnicmp(path, windows, windowsLength) == 0) continue;
        TraceModuleImports(m);
    }
}

DWORD WINAPI TraceThread(void*) {
    for (;;) {
        Sleep(1000);
        TraceModules();
        DumpTrace("the last second");
    }
}

} // namespace

DWORD MainThread() { return g_mainThread; }

void InstallVirtual(const unsigned* tickLength) {
    FindMainThreadOnce();
    g_virtual = true;
    g_tickLength = tickLength;
    g_realAtAdvance = GetTickCount();
    g_otherNow = static_cast<LONG>(g_now);
    PatchSlot(kTimeGetTimeSlot, &VirtualTimeGetTime, g_timeGetTime);
    PatchSlot(kGetTickCountSlot, &VirtualGetTickCount, g_getTickCount);
    PatchSlot(kSleepSlot, &VirtualSleep, g_sleep);
    PatchSlot(kWaitForSingleObjectSlot, &VirtualWaitForSingleObject, g_waitForSingleObject);
    InstallSampleTimers();
}

bool Virtual() { return g_virtual; }

void InstallHeld() {
    PatchSlot(kTimeGetTimeSlot, &HeldTimeGetTime, g_heldTimeGetTime.base);
    PatchSlot(kGetTickCountSlot, &HeldGetTickCount, g_heldTickCount.base);
}

void Freeze(bool on) {
    if (on == g_frozen) return;
    if (!g_virtual && g_heldTimeGetTime.base) {
        if (on) {
            g_heldTimeGetTime.Hold();
            g_heldTickCount.Hold();
        } else {
            g_heldTimeGetTime.Resume();
            g_heldTickCount.Resume();
        }
    }
    g_frozen = on;
}

bool Frozen() { return g_frozen; }

DWORD Now() { return g_virtual ? g_now : GetTickCount(); }

void InstallTrace() {
    FindMainThreadOnce();
    g_tracing = true;
    TraceGameImports();
    TraceModules();
    if (HANDLE thread = CreateThread(nullptr, 0, &TraceThread, nullptr, 0, nullptr)) CloseHandle(thread);
    Log("clocktrace: on (game.exe's clock and wait imports, and those of the DLLs in the game's folders)");
}

bool Tracing() { return g_tracing; }

void DumpTrace(const char* why) {
    if (!g_tracing) return;
    struct Line {
        Entry entry;
        unsigned long delta;
    };
    std::vector<Line> lines;
    const LONG count = g_entryCount;
    for (LONG i = 0; i < count; ++i) {
        Entry& e = g_entries[i];
        const unsigned long calls = e.count;
        if (calls != e.dumped) lines.push_back({e, calls - e.dumped});
        e.dumped = calls;
    }
    std::sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.delta > b.delta; });
    Log("clocktrace: --- %s (virtual %lu, real %lu): %u call sites busy", why, g_now, GetTickCount(), unsigned(lines.size()));    int shown = 0;
    for (const Line& line : lines) {
        if (++shown > 24) break;
        char where[MAX_PATH + 16], who[32];
        DescribeAddress(line.entry.caller, where, sizeof(where));
        DescribeThread(line.entry.thread, who, sizeof(who));
        Log("clocktrace: %8lu %-24s from %-28s on %s (total %lu, %llu ms asked)", line.delta, kFnNames[line.entry.fn],
            where, who, line.entry.count, line.entry.ms);
    }
}

// ---- Random trace ----------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kIsaacNext = 0x004667F0;
const unsigned* g_traceFrames = nullptr;
FILE* g_randomFile = nullptr;
unsigned g_randomFrame = ~0u;
std::uintptr_t g_selfBase = 0, g_selfEnd = 0;
CRITICAL_SECTION g_randomLock;

// One draw: "<frame> [(other thread) ][ei_hook+]<caller> <value>[ <what>]".
void WriteDraw(std::uintptr_t caller, std::uint32_t value, const char* what) {
    const bool ours = caller >= g_selfBase && caller < g_selfEnd;
    EnterCriticalSection(&g_randomLock);
    const unsigned frame = *g_traceFrames;
    if (frame != g_randomFrame) {
        std::fflush(g_randomFile);
        g_randomFrame = frame;
    }
    std::fprintf(g_randomFile, "%u %s%s%X %08X%s\n", frame, OnMainThread() ? "" : "(other thread) ", ours ? "ei_hook+" : "",
                 unsigned(ours ? caller - g_selfBase : caller), value, what);
    LeaveCriticalSection(&g_randomLock);
}

std::uint32_t __fastcall TracedNext(IsaacRandom* self, void*) {
    // IsaacRandom::Next (random.cpp), which jumps here too.
    if (self->count_-- == 0) {
        self->Refill();
        self->count_ = 255;
    }
    const std::uint32_t value = self->results_[self->count_];
    WriteDraw(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), value,
              self == &IsaacRandom::Global() ? "" : " (another generator)");
    return value;
}

// The game's CRT rand() (006E77F7; per thread: _getptd 006F00D1, holdrand at +0x14), which
// gamecrt::rand jumps to.
constexpr std::uintptr_t kCrtRand = 0x006E77F7;
constexpr std::uintptr_t kCrtThreadData = 0x006F00D1;
constexpr int kHoldRand = 0x14;
int __cdecl TracedRand() {
    auto* data = reinterpret_cast<std::uint8_t*(__cdecl*)()>(kCrtThreadData)();
    auto& hold = *reinterpret_cast<std::uint32_t*>(data + kHoldRand);
    hold = hold * 0x343FDu + 0x269EC3u;
    const int value = static_cast<int>((hold >> 16) & 0x7FFF);
    WriteDraw(reinterpret_cast<std::uintptr_t>(_ReturnAddress()), static_cast<std::uint32_t>(value), " rand()");
    return value;
}

void JumpFrom(std::uintptr_t entry, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(entry);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9;
    const auto relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (entry + 5));
    std::memcpy(p + 1, &relative, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

} // namespace

void RandomNote(const char* format, ...) {
    if (!g_randomFile) return;
    EnterCriticalSection(&g_randomLock);
    std::fprintf(g_randomFile, "%u note: ", *g_traceFrames);
    va_list args;
    va_start(args, format);
    std::vfprintf(g_randomFile, format, args);
    va_end(args);
    std::fputc('\n', g_randomFile);
    LeaveCriticalSection(&g_randomLock);
}

void InstallRandomTrace(const unsigned* frames) {
    if (g_randomFile) return;
    FindMainThreadOnce();
    char path[MAX_PATH];
    ei::OutputPath("randtrace.txt", path);
    if (fopen_s(&g_randomFile, path, "w") != 0 || !g_randomFile) return;
    std::setvbuf(g_randomFile, nullptr, _IOFBF, 1 << 20);
    InitializeCriticalSection(&g_randomLock);
    g_traceFrames = frames;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&TracedNext), &self);
    MODULEINFO info{};
    GetModuleInformation(GetCurrentProcess(), self, &info, sizeof(info));
    g_selfBase = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    g_selfEnd = g_selfBase + info.SizeOfImage;
    // The original's entry and ours (the rewritten callers call ours directly), and rand().
    auto ours = &IsaacRandom::Next;
    JumpFrom(kIsaacNext, reinterpret_cast<const void*>(&TracedNext));
    JumpFrom(*reinterpret_cast<std::uintptr_t*>(&ours), reinterpret_cast<const void*>(&TracedNext));
    JumpFrom(kCrtRand, reinterpret_cast<const void*>(&TracedRand));
    Log("harness: random trace to %s", path);
}

void FlushRandomTrace() {
    if (!g_randomFile) return;
    EnterCriticalSection(&g_randomLock);
    std::fflush(g_randomFile);
    LeaveCriticalSection(&g_randomLock);
}

} // namespace ei::verify::clock
