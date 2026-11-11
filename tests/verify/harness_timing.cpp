// Where a harness run's wall time goes (harness_timing.h; ticket SV10).
#include "harness_timing.h"

#include "harness_clock.h"
#include "system/harness_system.h"
#include "../../src/platform/log.h"
#include "../../src/render/gpu.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace ei::verify::timing {

namespace {

constexpr const char* kPartNames[Parts] = {"server", "digest", "client", "steps", "pair wait", "window checks"};
constexpr double kLongFrameMs = 200.0;   // a frame this long loads something (a zone, a save, a screen)

long long g_wall[Parts] = {};               // the run's sum per part (steady clock nanoseconds)
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
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

unsigned long long ThreadCycles() {
    double ms = 0;
    std::uint64_t cycles = 0;
    sys::ThisThreadCpu(&ms, &cycles);
    return cycles;
}

double Ms(long long ticks) { return static_cast<double>(ticks) / 1e6; }

bool OnMainThread() { return clock::OnMainThread(); }

void Credit(int part, long long now, unsigned long long cycles) {
    const long long wall = now - g_currentStart;
    const unsigned long long used = cycles - g_currentCycles;
    g_wall[part] += wall;
    g_cycles[part] += used;
    g_frameWall += wall;
    g_frameCycles += used;
}

float Median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

} // namespace

Scope::Scope(Part newPart) : part(newPart), outer(-1) {
    if (!OnMainThread()) {
        part = Parts;
        return;
    }
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    if (g_current >= 0) Credit(g_current, now, cycles);
    outer = g_current;
    g_current = newPart;
    g_currentStart = now;
    g_currentCycles = cycles;
}

Scope::~Scope() {
    if (part == Parts) return;
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    Credit(part, now, cycles);
    g_current = outer;
    g_currentStart = now;
    g_currentCycles = cycles;
}

unsigned long long PartCycles(Part part) { return g_cycles[part]; }

void FrameEnd() {
    if (!OnMainThread()) return;
    const long long now = Ticks();
    const unsigned long long cycles = ThreadCycles();
    if (g_frames == 0) {
        double kernelMs = 0, userMs = 0;
        sys::ProcessTimes(&kernelMs, &userMs, &g_startupMs);
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
    std::uint64_t mainCycles = 0;
    sys::MainThreadCpu(&mainCpuMs, &mainCycles);
    sys::ProcessTimes(&processKernelMs, &processUserMs, &lifeMs);
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
    // Where the main thread waited on the GPU (H62d: on a software device the frame is the device's work, the process's
    // CPU above the main thread's) and the memory (H62f: WARP lost its device on the CI runner).
    const render::gpu::WaitTimes waits = render::gpu::Waits();
    const double perFrame = waits.swapchains ? 1.0 / static_cast<double>(waits.swapchains) : 0.0;
    std::fprintf(f, "TIMING: gpu: %llu swap chain textures, per one %.2f ms waiting for it (a frame in flight) and "
                    "%.2f ms in the %llu submissions; %llu download waits %.1f s; %d pipelines made after the start "
                    "in %.1f s",
                 static_cast<unsigned long long>(waits.swapchains), waits.acquireMs * perFrame,
                 waits.submitMs * perFrame, static_cast<unsigned long long>(waits.submissions),
                 static_cast<unsigned long long>(waits.downloadWaits), waits.downloadWaitMs / 1000.0,
                 waits.pipelinesMade, waits.pipelineMs / 1000.0);
    double memoryMb = 0, peakMb = 0, availableMb = 0;
    if (sys::ProcessMemory(&memoryMb, &peakMb, &availableMb))
        std::fprintf(f, "; memory %.0f MB (peak %.0f MB), the system's available %.0f MB", memoryMb, peakMb, availableMb);
    std::fprintf(f, "\n");
}

} // namespace ei::verify::timing
