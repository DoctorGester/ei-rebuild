// The camera's motion frame by frame: the smooth camera's check (harness_camera.h; ticket R18).
#include "harness_camera.h"

#include "game/camera.h"
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

enum Kind { Pan, Turn, Zoom, Kinds };
constexpr const char* kKindNames[Kinds] = {"pan", "turn", "zoom"};

struct Sample {
    double x, y, heading, distance;
};

struct Recording {
    int frames = 0;
    double total[Kinds] = {};
    double largest[Kinds] = {};
    int jumps[Kinds] = {};
};

bool g_recording = false;
std::vector<Sample> g_samples;
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
    default: return std::fabs(b.distance - a.distance);
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

void Frame(const void* camera, float seconds) {
    if (!g_recording || !camera) return;
    const CCamera* c = static_cast<const CCamera*>(camera);
    g_frameTime += seconds;
    g_flags = c->flags_;
    const Quaternion& q = c->viewRotation_;
    g_samples.push_back(Sample{c->focus_.x, c->focus_.y, 2.0 * std::atan2(static_cast<double>(q.z), static_cast<double>(q.w)),
                               c->distance_});
}

bool UsedBy(const std::string& verb) { return verb == "camtrace" || verb == "camsame" || verb == "camsmooth"; }

void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines) {
    if (!ei::Standalone()) {
        Log("harness: %s %s: the original code (the smooth camera is the image's): skipped", verb.c_str(), argument.c_str());
        return;
    }
    char text[600];
    if (verb == "camtrace") {
        char what[16] = "", name[64] = "";
        sscanf_s(argument.c_str(), "%15s %63s", what, unsigned(sizeof(what)), name, unsigned(sizeof(name)));
        if (std::strcmp(what, "start") == 0) {
            g_samples.clear();
            g_frameTime = 0.0;
            g_recording = true;
            return;
        }
        if (std::strcmp(what, "stop") != 0 || !name[0]) {
            failures->push_back("camtrace needs start or stop <name>");
            return;
        }
        g_recording = false;
        const Recording r = Measure(g_samples);
        g_recordings[name] = r;
        const Sample first = g_samples.empty() ? Sample{} : g_samples.front();
        const Sample last = g_samples.empty() ? Sample{} : g_samples.back();
        std::snprintf(text, sizeof(text),
                      "camtrace %s: %d frames (last updates %.3f ticks, flags %X); pan %.4f (largest step %.4f, %d jumps), "
                      "turn %.5f (%.5f, %d), zoom %.4f (%.4f, %d); focus (%.2f, %.2f) to (%.2f, %.2f), distance %.2f to %.2f",
                      name, r.frames, g_frameTime, g_flags, r.total[Pan], r.largest[Pan], r.jumps[Pan], r.total[Turn],
                      r.largest[Turn], r.jumps[Turn], r.total[Zoom], r.largest[Zoom], r.jumps[Zoom], first.x, first.y, last.x,
                      last.y, first.distance, last.distance);
        Log("harness: %s", text);
        lines->push_back(text);
        return;
    }
    if (verb == "camsame") {
        char a[64] = "", b[64] = "", kindName[16] = "";
        double fraction = 0.0;
        if (sscanf_s(argument.c_str(), "%63s %63s %15s %lf", a, unsigned(sizeof(a)), b, unsigned(sizeof(b)), kindName,
                     unsigned(sizeof(kindName)), &fraction) < 4 ||
            KindOf(kindName) < 0) {
            failures->push_back("camsame needs two recordings, pan|turn|zoom and a fraction");
            return;
        }
        const int kind = KindOf(kindName);
        if (!g_recordings.count(a) || !g_recordings.count(b)) {
            failures->push_back("camsame " + argument + ": no such recording");
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
        failures->push_back("camsmooth needs a recording and pan|turn|zoom");
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
