// The scenario harness of ei-effects (the effect viewer's exe; docs/notes/harness.md, "ei-effects' scenarios"): the
// verify build links it in, and with EI_HARNESS set (tools/run_all.py) it plays the scenario's steps between the
// program's frames (tools/effects_app.h) and writes harness_report.txt and harness_quit as the game's harness does.
// Steps, one a line ('#' lines are comments):
//   mod <folder>           (before the start) the mod played: a folder, relative to the run's output folder
//   effectviewer <command> one of the viewer's commands (tools/effect_viewer.cpp, Command); its error fails the run:
//                          pick <effect>, place figure|focus [dx dy dz]|free <x> <y> <z>, play, pause, step [n],
//                          scrub <tick>, restart, loop on|off, size <s>, emit start|stop, stop,
//                          input <name> <value>|<x y z>|<enum value>, fire <event>, field <path> <value>,
//                          tab fields|text, folder out [<name>]|<folder>, save, new <name>, convert, code,
//                          append <text, \n for a line end>, figure walk on|off|run on|off|animation <name>,
//                          expect particles <least>|tick <n>|stage <name>|problems <n>|status <text>|
//                          children <least>|subparticles <n>
//   write <file> <text>    <file> (relative to the run's output folder, no "..") written with <text>, \n for a line
//                          end: a file of the run's mod edited on disk, as a modder's editor saves it
//   wait <ms>              the frames of that much time
//   frames <n>             n frames
//   capture <name>         the next frame written to frames\<name>.bmp
//   timeout <seconds>      the run's limit (default 60; run_all reads it too): past it the run fails and quits
//   report                 the report written (also at the quit)
//   quit                   the program ends
// The viewer saves only into the run's output folder: a save anywhere else fails the run. The window is kept out of
// sight as the game's is (tests/verify/system, ConcealWindow and WatchWindows; the report's WINDOW: line).
#include "tools/effect_viewer.h"
#include "tools/effects_app.h"

#include "system/harness_system.h"

#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Step {
    std::string verb, argument;
};

std::vector<Step> g_steps;
std::size_t g_next = 0;
std::vector<std::string> g_failures;
std::vector<std::string> g_frames;
std::uint32_t g_started = 0;
std::uint32_t g_limitMs = 60000;
// The step waiting: frames left, or a clock time to reach.
int g_framesLeft = 0;
std::uint32_t g_waitUntil = 0;
bool g_waiting = false;
std::string g_capture;   // a capture asked for, taken before the next present
bool g_quitting = false;

std::string Trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

namespace sys = ei::verify::sys;

void* g_window = nullptr;

void WriteFile(const char* name, const std::string& text) {
    std::ofstream(ei::OutputPath(name), std::ios::binary) << text;
}

void WriteReport() {
    FILE* f = sys::OpenFile(ei::OutputPath("harness_report.txt"), "wb");
    if (!f) return;
    if (!g_frames.empty()) std::fputs("\nframes captured:\n", f);
    for (const std::string& frame : g_frames) std::fprintf(f, "  %s\n", frame.c_str());
    std::fputs("\n", f);
    sys::WindowReport(f);
    for (const std::string& failure : g_failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fputs(g_failures.empty() ? "RESULT: PASS\n" : "RESULT: FAIL\n", f);
    std::fclose(f);
    ei::Log("harness: report written (%u failures)", static_cast<unsigned>(g_failures.size()));
}

void Quit() {
    if (g_quitting) return;
    g_quitting = true;
    WriteReport();
    WriteFile("harness_quit", "");
    platform::PostQuit(0);
}

void Fail(const std::string& why) {
    ei::Log("harness: %s", why);
    g_failures.push_back(why);
}

// frames\<name>.bmp: 24-bit, rows from the bottom (as the game's harness writes its captures).
bool WriteBmp(const std::string& path, const render::FrameImage& image) {
    const int row = (image.width * 3 + 3) & ~3;
    const std::uint32_t size = 54u + static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(image.height);
    std::uint8_t header[54] = {'B', 'M'};
    auto put = [&header](int at, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) header[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, static_cast<std::uint32_t>(image.width));
    put(22, static_cast<std::uint32_t>(image.height));
    header[26] = 1;
    header[28] = 24;
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    std::vector<char> line(static_cast<std::size_t>(row), 0);
    for (int y = image.height - 1; y >= 0; --y) {
        const std::uint8_t* from = image.bgr.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 3;
        std::copy(from, from + image.width * 3, line.begin());
        file.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
    return static_cast<bool>(file);
}

// The "write" step: false (with the reason) for a path outside the run's output folder.
bool WriteStep(const std::string& argument, std::string* why) {
    const std::size_t space = argument.find(' ');
    std::string file = argument.substr(0, space);
    std::string text = space == std::string::npos ? std::string() : argument.substr(space + 1);
    if (file.empty() || file[0] == '\\' || file[0] == '/' || file.find(':') != std::string::npos ||
        file.find("..") != std::string::npos) {
        *why = "write: " + file + " is not a file in the run's output folder";
        return false;
    }
    // The scenario's `\` separators as `/`, which every system takes (on POSIX a `\` is part of a file's name).
    std::replace(file.begin(), file.end(), '\\', '/');
    for (std::size_t at = text.find("\\n"); at != std::string::npos; at = text.find("\\n", at + 1)) text.replace(at, 2, "\n");
    std::ofstream out(ei::OutputPath(file.c_str()), std::ios::binary);
    out << text << '\n';
    if (!out) *why = "write: " + file + " could not be written";
    return static_cast<bool>(out);
}

// Whether `folder` lies in the run's output folder (the viewer's saves go nowhere else).
bool InOutput(const std::string& folder) {
    const std::string out = platform::EnvironmentVariable("EI_OUT_DIR");
    if (out.empty() || folder.size() < out.size()) return false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char a = folder[i] == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(folder[i])));
        const char b = out[i] == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
        if (a != b) return false;
    }
    return true;
}

void Start(std::string* modFolder) {
    const std::string scenario = platform::EnvironmentVariable("EI_HARNESS");
    if (scenario.empty()) return;
    std::ifstream in(scenario);
    WriteFile("harness_report.txt", in ? "RESULT: RUNNING\n" : "FAILED: cannot read the scenario\nRESULT: FAIL\n");
    if (!in) std::exit(2);
    std::string line;
    while (std::getline(in, line)) {
        line = Trimmed(line);
        if (line.empty() || line[0] == '#') continue;
        const std::size_t space = line.find(' ');
        Step step{line.substr(0, space), space == std::string::npos ? "" : Trimmed(line.substr(space + 1))};
        if (step.verb == "mod") {
            const bool full = !step.argument.empty() &&
                              (step.argument[0] == '\\' || step.argument[0] == '/' ||
                               (step.argument.size() >= 2 && step.argument[1] == ':'));
            *modFolder = full ? step.argument : ei::OutputPath(step.argument.c_str());
            continue;
        }
        if (step.verb == "timeout") {
            g_limitMs = static_cast<std::uint32_t>(std::strtoul(step.argument.c_str(), nullptr, 10)) * 1000u;
            continue;
        }
        g_steps.push_back(step);
    }
    g_started = platform::FrameClock();
    tools::effects_app::g_harnessHooks.windowMade = [](void* window) {
        g_window = window;
        sys::ConcealWindow(window);
    };
    ei::Log("harness: %s, %u steps", scenario, static_cast<unsigned>(g_steps.size()));
    tools::effects_app::g_harnessHooks.beforePresent = [] {
        if (g_capture.empty()) return;
        render::EndFrame();
        const std::optional<render::FrameImage> frame = render::ReadFrame();
        const std::string name = std::exchange(g_capture, std::string());
        platform::CreateFolder(ei::OutputPath("frames"));
        const std::string path = ei::OutputPath(("frames\\" + name + ".bmp").c_str());
        if (!frame || !WriteBmp(path, *frame)) {
            Fail("capture " + name + ": no frame written");
            return;
        }
        g_frames.push_back(path);
        ei::Log("harness: captured %s (%dx%d)", path, frame->width, frame->height);
    };
    tools::effects_app::g_harnessHooks.afterFrame = [] {
        sys::WatchWindows(g_window);
        const std::uint32_t now = platform::FrameClock();
        if (!g_quitting && now - g_started > g_limitMs) {
            Fail("TIMEOUT: the scenario's " + std::to_string(g_limitMs / 1000) + " s limit passed at step " +
                 std::to_string(g_next + 1));
            Quit();
            return;
        }
        if (g_waiting) {
            if (g_framesLeft > 0 && --g_framesLeft > 0) return;
            if (g_waitUntil && now - g_waitUntil > 0x80000000u) return;
            g_waiting = false;
            g_waitUntil = 0;
        }
        while (!g_quitting && g_next < g_steps.size()) {
            const Step& step = g_steps[g_next++];
            ei::Log("harness: step %s %s", step.verb, step.argument);
            if (step.verb == "effectviewer") {
                if (step.argument == "save" && !InOutput(tools::effect_viewer::SaveFolder())) {
                    Fail("effectviewer save: the folder " + tools::effect_viewer::SaveFolder() +
                         " is outside the run's output folder (effectviewer folder out)");
                    continue;
                }
                const std::string error = tools::effect_viewer::Command(step.argument);
                if (!error.empty()) Fail(error);
            } else if (step.verb == "write") {
                std::string why;
                if (!WriteStep(step.argument, &why)) Fail(why);
            } else if (step.verb == "wait") {
                g_waiting = true;
                g_waitUntil = now + static_cast<std::uint32_t>(std::strtoul(step.argument.c_str(), nullptr, 10));
                return;
            } else if (step.verb == "frames") {
                g_waiting = true;
                g_framesLeft = std::max(1, std::atoi(step.argument.c_str()));
                return;
            } else if (step.verb == "capture") {
                g_capture = step.argument;
                g_waiting = true;
                g_framesLeft = 1;
                return;
            } else if (step.verb == "report") {
                WriteReport();
            } else if (step.verb == "quit") {
                Quit();
                return;
            } else {
                Fail("unknown step " + step.verb);
            }
        }
        if (!g_quitting && g_next >= g_steps.size()) {
            Fail("the scenario ended without quit");
            Quit();
        }
    };
}

// Set when the verify build links this file: the program asks for the scenario at its start.
[[maybe_unused]] const bool g_installed = (tools::effects_app::g_harnessHooks.start = &Start, true);

} // namespace
