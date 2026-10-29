// The camera's motion frame by frame: the drawn view's checks (harness_camera.h; tickets R18, R35a).
#include "harness_camera.h"

#include "exe_objects.h"
#include "image/image_side.h"
#include "ei/hook.h"
#include "../../src/hook/log.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ei::verify::camera_trace {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kJump = 2.0;   // a step more than this many times both neighbours' is a jump

enum Kind { Pan, Turn, Zoom, Height, Kinds };
constexpr const char* kKindNames[Kinds] = {"pan", "turn", "zoom", "height"};

struct Sample {
    double x, y, heading, distance, height;
};

struct Recording {
    int frames = 0;
    double total[Kinds] = {};
    double largest[Kinds] = {};
    int jumps[Kinds] = {};
};

bool g_recording = false;
bool g_logFrames = false;          // "camtrace start log": each frame's poses in the log
std::vector<Sample> g_samples;         // the drawn view's
std::vector<Sample> g_cameraSamples;   // the camera's own
double g_frameTime = 0.0;   // the frames' last camera updates' time, summed (camera time: 1 a tick)
unsigned g_flags = 0;       // the camera's flags at the last sample
std::map<std::string, Recording> g_recordings;

double Step(const Sample& a, const Sample& b, int kind) {
    switch (kind) {
    case Pan: return std::hypot(b.x - a.x, b.y - a.y);
    case Turn: {
        double d = std::fmod(b.heading - a.heading, 2.0 * kPi);
        if (d > kPi) d -= 2.0 * kPi;
        if (d < -kPi) d += 2.0 * kPi;
        return std::fabs(d);
    }
    case Zoom: return std::fabs(b.distance - a.distance);
    default: return std::fabs(b.height - a.height);
    }
}

Recording Measure(const std::vector<Sample>& samples) {
    Recording r;
    r.frames = static_cast<int>(samples.size());
    for (int kind = 0; kind < Kinds; ++kind) {
        std::vector<double> steps;
        for (std::size_t i = 1; i < samples.size(); ++i) steps.push_back(Step(samples[i - 1], samples[i], kind));
        for (double s : steps) {
            r.total[kind] += s;
            if (s > r.largest[kind]) r.largest[kind] = s;
        }
        // A jump: a step larger than kJump times both neighbours' (and not a rounding speck).
        for (std::size_t i = 1; i + 1 < steps.size(); ++i) {
            const double s = steps[i];
            if (s > 1e-6 * r.largest[kind] && s > 1e-7 && s > kJump * steps[i - 1] && s > kJump * steps[i + 1]) ++r.jumps[kind];
        }
    }
    return r;
}

int KindOf(const char* name) {
    for (int kind = 0; kind < Kinds; ++kind)
        if (std::strcmp(name, kKindNames[kind]) == 0) return kind;
    return -1;
}

} // namespace

// game.exe's camera through its view (exe_objects.h); the original draws its own pose.
image::CameraState OriginalCamera(const void* camera) {
    const auto* c = static_cast<const exe::Camera*>(camera);
    image::CameraState state;
    image::CameraPose& pose = state.own;
    pose.focusX = c->focus.x;
    pose.focusY = c->focus.y;
    pose.focusZ = c->focus.z;
    pose.turnZ = c->viewRotation.z;
    pose.turnW = c->viewRotation.w;
    pose.distance = c->distance;
    pose.position[0] = c->localPosition.x;
    pose.position[1] = c->localPosition.y;
    pose.position[2] = c->localPosition.z;
    pose.rotation[0] = c->localRotation.w;
    pose.rotation[1] = c->localRotation.x;
    pose.rotation[2] = c->localRotation.y;
    pose.rotation[3] = c->localRotation.z;
    state.drawn = pose;
    state.flags = c->flags;
    return state;
}

Sample SampleOf(const image::CameraPose& pose) {
    return Sample{pose.focusX, pose.focusY, 2.0 * std::atan2(static_cast<double>(pose.turnZ), static_cast<double>(pose.turnW)),
                  pose.distance, pose.position[2]};
}

void Frame(const void* camera, float seconds) {
    if (!g_recording || !camera) return;
    g_frameTime += seconds;
    const image::CameraState c = ei::Standalone() ? image::Camera(camera) : OriginalCamera(camera);
    g_flags = c.flags;
    const image::CameraPose& d = c.drawn;
    g_samples.push_back(SampleOf(d));
    g_cameraSamples.push_back(SampleOf(c.own));
    if (g_logFrames) {
        const image::CameraPose& o = c.own;
        Log("harness: camframe %d %.3f own %.5f %.5f %.5f %.6f %.6f %.6f %.6f focus %.5f %.5f %.5f drawn %.5f %.5f %.5f %.6f %.6f "
            "%.6f %.6f",
            static_cast<int>(g_samples.size()), seconds, o.position[0], o.position[1], o.position[2], o.rotation[0], o.rotation[1],
            o.rotation[2], o.rotation[3], o.focusX, o.focusY, o.focusZ, d.position[0], d.position[1], d.position[2], d.rotation[0],
            d.rotation[1], d.rotation[2], d.rotation[3]);
    }
}

bool UsedBy(const std::string& verb) {
    return verb == "camtrace" || verb == "camsame" || verb == "camsteadier" || verb == "camsmooth";
}

void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines) {
    char text[600];
    if (verb == "camtrace") {
        char what[16] = "", name[64] = "";
        sscanf_s(argument.c_str(), "%15s %63s", what, unsigned(sizeof(what)), name, unsigned(sizeof(name)));
        if (std::strcmp(what, "start") == 0) {
            g_samples.clear();
            g_cameraSamples.clear();
            g_frameTime = 0.0;
            g_recording = true;
            g_logFrames = std::strcmp(name, "log") == 0;
            return;
        }
        if (std::strcmp(what, "stop") != 0 || !name[0]) {
            failures->push_back("camtrace needs start or stop <name>");
            return;
        }
        g_recording = false;
        // The drawn view as <name>, the camera's own as <name>.camera.
        for (const bool own : {false, true}) {
            const std::vector<Sample>& samples = own ? g_cameraSamples : g_samples;
            const std::string kept = own ? std::string(name) + ".camera" : std::string(name);
            const Recording r = Measure(samples);
            g_recordings[kept] = r;
            const Sample first = samples.empty() ? Sample{} : samples.front();
            const Sample last = samples.empty() ? Sample{} : samples.back();
            std::snprintf(text, sizeof(text),
                          "camtrace %s: %d frames (last updates %.3f ticks, flags %X); pan %.4f (largest step %.4f, %d jumps), "
                          "turn %.5f (%.5f, %d), zoom %.4f (%.4f, %d), height %.4f (%.4f, %d); focus (%.2f, %.2f) to (%.2f, "
                          "%.2f), distance %.2f to %.2f",
                          kept.c_str(), r.frames, g_frameTime, g_flags, r.total[Pan], r.largest[Pan], r.jumps[Pan], r.total[Turn],
                          r.largest[Turn], r.jumps[Turn], r.total[Zoom], r.largest[Zoom], r.jumps[Zoom], r.total[Height],
                          r.largest[Height], r.jumps[Height], first.x, first.y, last.x, last.y, first.distance, last.distance);
            Log("harness: %s", text);
            lines->push_back(text);
        }
        return;
    }
    if (verb == "camsame" || verb == "camsteadier") {
        char a[64] = "", b[64] = "", kindName[16] = "";
        double fraction = 0.0;
        if (sscanf_s(argument.c_str(), "%63s %63s %15s %lf", a, unsigned(sizeof(a)), b, unsigned(sizeof(b)), kindName,
                     unsigned(sizeof(kindName)), &fraction) < 4 ||
            KindOf(kindName) < 0) {
            failures->push_back(verb + " needs two recordings, pan|turn|zoom|height and a fraction");
            return;
        }
        const int kind = KindOf(kindName);
        if (!g_recordings.count(a) || !g_recordings.count(b)) {
            failures->push_back(verb + " " + argument + ": no such recording");
            return;
        }
        if (verb == "camsteadier") {
            const double la = g_recordings[a].largest[kind], lb = g_recordings[b].largest[kind];
            std::snprintf(text, sizeof(text), "camsteadier %s %s %s: largest step %.5f against %.5f (allowed %.3f of it)", a, b,
                          kindName, la, lb, fraction);
            Log("harness: %s", text);
            lines->push_back(text);
            if (!(lb > 1e-4) || la > fraction * lb) failures->push_back(text);
            return;
        }
        const double ta = g_recordings[a].total[kind], tb = g_recordings[b].total[kind];
        const double larger = ta > tb ? ta : tb;
        const double differs = larger > 0.0 ? std::fabs(ta - tb) / larger : 0.0;
        std::snprintf(text, sizeof(text), "camsame %s %s %s: %.5f against %.5f, %.3f%% apart (allowed %.3f%%)", a, b, kindName, ta,
                      tb, 100.0 * differs, 100.0 * fraction);
        Log("harness: %s", text);
        lines->push_back(text);
        if (!(larger > 1e-4) || differs > fraction) failures->push_back(text);
        return;
    }
    // camsmooth
    char name[64] = "", kindName[16] = "", jerky[16] = "";
    if (sscanf_s(argument.c_str(), "%63s %15s %15s", name, unsigned(sizeof(name)), kindName, unsigned(sizeof(kindName)), jerky,
                 unsigned(sizeof(jerky))) < 2 ||
        KindOf(kindName) < 0) {
        failures->push_back("camsmooth needs a recording and pan|turn|zoom|height");
        return;
    }
    if (!g_recordings.count(name)) {
        failures->push_back("camsmooth " + argument + ": no such recording");
        return;
    }
    const bool wantJerky = std::strcmp(jerky, "jerky") == 0;
    const Recording& r = g_recordings[name];
    const int kind = KindOf(kindName);
    const int jumps = r.jumps[kind];
    std::snprintf(text, sizeof(text), "camsmooth %s %s: %d jumps over %d frames, moved %.5f (wanted %s)", name, kindName, jumps,
                  r.frames, r.total[kind], wantJerky ? "some" : "none");
    Log("harness: %s", text);
    lines->push_back(text);
    if (!(r.total[kind] > 1e-4) || (wantJerky ? jumps == 0 : jumps != 0)) failures->push_back(text);
}

} // namespace ei::verify::camera_trace
