// Where a harness run's wall time goes (harness_timing.h; ticket SV10).
#include "harness_timing.h"

#include "harness_clock.h"
#include "../../src/hook/log.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace ei::verify::timing {

namespace {

constexpr const char* kPartNames[Parts] = {"server", "digest", "client", "steps", "pair wait", "window checks"};
constexpr double kLongFrameMs = 200.0;   // a frame this long loads something (a zone, a save, a screen)

long long g_frequency = 0;
long long g_wall[Parts] = {};               // the run's sum per part (QPC ticks)
unsigned long long g_cycles[Parts] = {};    // the main thread's cycles per part
long long g_frameWall = 0;                  // this frame's parts so far
unsigned long long g_frameCycles = 0;
long long g_restWall = 0;                   // the frames' time outside the parts
unsigned long long g_restCycles = 0;
long long g_lastEnd = 0, g_firstEnd = 0;
unsigned long long g_lastEndCycles = 0;
unsigned g_frames = 0;
unsigned g_longFrames = 0;
long long g_longWall = 0;
double g_startupMs = -1.0;
std::vector<float> g_periodMs, g_restMs;

// The part running now (scopes nest: an inner part's time is not the outer's).
int g_current = -1;
long long g_currentStart = 0;
unsigned long long g_currentCycles = 0;

long long Ticks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

unsigned long long ThreadCycles() {
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return c;
}

double Ms(long long ticks) {
    if (!g_frequency) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_frequency = f.QuadPart;
    }
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(g_frequency);
}

bool OnMainThread() { return GetCurrentThreadId() == clock::MainThread(); }

void Credit(int part, long long now, unsigned long long cycles) {
    const long long wall = now - g_currentStart;
    const unsigned long long used = cycles - g_currentCycles;
    g_wall[part] += wall;
    g_cycles[part] += used;
    g_frameWall += wall;
    g_frameCycles += used;
}

// In 100 ns units. Differences are taken in integers: the game runs the FPU at 24 bits (control word
// 007F), where a date in milliseconds as a double has no precision left.
unsigned long long FileTimeUnits(const FILETIME& t) {
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
double FileTimeMs(const FILETIME& t) { return static_cast<double>(FileTimeUnits(t)) / 10000.0; }
double SinceMs(const FILETIME& from, const FILETIME& to) {
    return static_cast<double>(static_cast<long long>(FileTimeUnits(to) - FileTimeUnits(from))) / 10000.0;
}

float Median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

} // namespace

Scope::Scope(Part part) : part_(part), outer_(-1) {
    if (!OnMainThread()) {
        part_ = Parts;
        return;
    }
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    if (g_current >= 0) Credit(g_current, now, cycles);
    outer_ = g_current;
    g_current = part;
    g_currentStart = now;
    g_currentCycles = cycles;
}

Scope::~Scope() {
    if (part_ == Parts) return;
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    Credit(part_, now, cycles);
    g_current = outer_;
    g_currentStart = now;
    g_currentCycles = cycles;
}

void FrameEnd() {
    if (!OnMainThread()) return;
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    if (g_frames == 0) {
        FILETIME created, exited, kernel, user, nowTime;
        GetSystemTimePreciseAsFileTime(&nowTime);
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            g_startupMs = SinceMs(created, nowTime);
        g_firstEnd = now;
        ei::Log("harness: timing: the first frame ended %.0f ms after the process started", g_startupMs);
    } else {
        const long long period = now - g_lastEnd;
        const long long rest = period - g_frameWall;
        g_restWall += rest;
        g_restCycles += (cycles - g_lastEndCycles) - g_frameCycles;
        const double periodMs = Ms(period);
        g_periodMs.push_back(static_cast<float>(periodMs));
        g_restMs.push_back(static_cast<float>(Ms(rest)));
        if (periodMs >= kLongFrameMs) {
            ++g_longFrames;
            g_longWall += period;
        }
    }
    ++g_frames;
    g_frameWall = 0;
    g_frameCycles = 0;
    g_lastEnd = now;
    g_lastEndCycles = cycles;
}

void Report(FILE* f) {
    if (g_frames < 2) return;
    const unsigned frames = g_frames - 1;   // frames timed: from the first frame's end on
    // Cycles to milliseconds by the main thread's own CPU time.
    double mainCpuMs = 0.0, processUserMs = 0.0, processKernelMs = 0.0, lifeMs = 0.0;
    unsigned long long mainCycles = 0;
    if (HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, clock::MainThread())) {
        FILETIME created, exited, kernel, user;
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) mainCpuMs = FileTimeMs(kernel) + FileTimeMs(user);
        ULONG64 c = 0;
        if (QueryThreadCycleTime(thread, &c)) mainCycles = c;
        CloseHandle(thread);
    }
    {
        FILETIME created, exited, kernel, user, nowTime;
        GetSystemTimePreciseAsFileTime(&nowTime);
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            processUserMs = FileTimeMs(user);
            processKernelMs = FileTimeMs(kernel);
            lifeMs = SinceMs(created, nowTime);
        }
    }
    const double msPerCycle = mainCycles ? mainCpuMs / static_cast<double>(mainCycles) : 0.0;
    const double totalMs = Ms(g_lastEnd - g_firstEnd);
    std::fprintf(f, "\nTIMING: %u frames in %.1f s after the first (start-up to it %.1f s, the process %.1f s so far); "
                    "per frame, ms of wall / main-thread CPU:",
                 frames, totalMs / 1000.0, g_startupMs / 1000.0, lifeMs / 1000.0);
    for (int p = 0; p < Parts; ++p)
        std::fprintf(f, " %s %.2f / %.2f,", kPartNames[p], Ms(g_wall[p]) / frames,
                     static_cast<double>(g_cycles[p]) * msPerCycle / frames);
    std::fprintf(f, " rest (interface, drawing, present, messages) %.2f / %.2f\n", Ms(g_restWall) / frames,
                 static_cast<double>(g_restCycles) * msPerCycle / frames);
    std::fprintf(f, "TIMING: median frame %.2f ms, median rest %.2f ms; %u frame(s) of %.0f ms or more took %.1f s; "
                    "CPU: main thread %.1f s, process %.1f s user + %.1f s kernel\n",
                 Median(g_periodMs), Median(g_restMs), g_longFrames, kLongFrameMs, Ms(g_longWall) / 1000.0,
                 mainCpuMs / 1000.0, processUserMs / 1000.0, processKernelMs / 1000.0);
}

} // namespace ei::verify::timing
