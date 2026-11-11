// The camera's and the objects' motion frame by frame: the drawn view's checks (harness_camera.h; tickets R18,
// R35a, R19).
#include "harness_camera.h"

#include "harness_digest.h"
#include "image/image_side.h"
#include "../../src/platform/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace ei::verify::camera_trace {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kJump = 2.0;   // a step more than this many times both neighbours' is a jump

enum Kind { Pan, Turn, Zoom, Height, Reach, Kinds };
constexpr const char* kKindNames[Kinds] = {"pan", "turn", "zoom", "height", "reach"};

struct Sample {
    double x, y, heading, distance, height, reach;   // reach: the camera's distance from its focus
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

// The object trace: the object followed (0: the first arrow found, then its id), its part, whether it records and
// whether the object is gone (the recording then ends).
bool g_objectRecording = false;
int g_objectId = 0;
std::string g_objectPart;
bool g_objectLost = false;
bool g_objectLight = false;     // "spelllight": a point light effect, not an object
bool g_logObjects = false;
std::vector<Sample> g_objectSamples;      // the drawn place
std::vector<Sample> g_objectOwnSamples;   // the object's own
std::vector<std::string> g_objectAnimations;   // the animations it played (objplayed), each once
std::map<std::string, double> g_objectFurthest;   // each animation's furthest frame played (objplayed past)

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
    case Height: return std::fabs(b.height - a.height);
    default: return std::fabs(b.reach - a.reach);
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

Sample SampleOf(const image::CameraPose& pose) {
    const double dx = static_cast<double>(pose.position[0]) - pose.focusX, dy = static_cast<double>(pose.position[1]) - pose.focusY,
                 dz = static_cast<double>(pose.position[2]) - pose.focusZ;
    return Sample{pose.focusX, pose.focusY, 2.0 * std::atan2(static_cast<double>(pose.turnZ), static_cast<double>(pose.turnW)),
                  pose.distance, pose.position[2], std::sqrt(dx * dx + dy * dy + dz * dz)};
}

void Frame(const void* camera, float seconds) {
    if (!g_recording || !camera) return;
    g_frameTime += seconds;
    const image::CameraState c = image::Camera(camera);
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

Sample SampleOf(const image::ObjectPlace& place) { return Sample{place.x, place.y, place.yaw, 0.0, place.z, 0.0}; }

void ObjectFrame() {
    if (!g_objectRecording || g_objectLost) return;
    image::ObjectState s;
    if (!(g_objectLight ? image::ClientLight(g_objectId, &s) : image::ClientObject(g_objectId, g_objectPart.c_str(), &s))) {
        if (!g_objectSamples.empty()) g_objectLost = true;
        return;
    }
    g_objectId = s.id;
    g_objectSamples.push_back(SampleOf(s.drawn));
    g_objectOwnSamples.push_back(SampleOf(s.own));
    if (!s.animation.empty() &&
        std::find(g_objectAnimations.begin(), g_objectAnimations.end(), s.animation) == g_objectAnimations.end())
        g_objectAnimations.push_back(s.animation);
    if (!s.animation.empty()) {
        auto [it, added] = g_objectFurthest.try_emplace(s.animation, s.frame);
        if (!added) it->second = std::max(it->second, static_cast<double>(s.frame));
    }
    if (g_logObjects)
        Log("harness: objframe %d id %d own %.5f %.5f %.5f %.5f drawn %.5f %.5f %.5f %.5f animation %s %.3f blend %.3f",
            static_cast<int>(g_objectSamples.size()), s.id, s.own.x, s.own.y, s.own.z, s.own.yaw, s.drawn.x, s.drawn.y, s.drawn.z,
            s.drawn.yaw, s.animation.empty() ? "-" : s.animation.c_str(), s.frame, s.blend);
}

bool UsedBy(const std::string& verb) {
    return verb == "camtrace" || verb == "camsame" || verb == "camsteadier" || verb == "camsmooth";
}

bool ObjectsUsedBy(const std::string& verb) { return verb == "objtrace" || verb == "objplayed" || verb == "animblend"; }

void Step(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
          std::vector<std::string>* lines) {
    char text[600];
    if (verb == "animblend") {
        // The world figures' blends begun since the last "animblend mark" (or the run's start).
        static std::uint32_t marked = 0;
        int least = 0, most = 0;
        if (argument == "mark") {
            marked = image::WorldBlendsBegun();
        } else if (std::sscanf(argument.c_str(), "count %d %d", &least, &most) == 2) {
            const int begun = static_cast<int>(image::WorldBlendsBegun() - marked);
            Log("harness: animblend count: %d blends begun in world figures (wanted %d .. %d)", begun, least, most);
            if (begun < least || begun > most)
                failures->push_back("animblend count: " + std::to_string(begun) + " blends begun in world figures (wanted " +
                                    std::to_string(least) + " .. " + std::to_string(most) + ")");
        } else if (argument == "on" || argument == "off") {
            image::SetAnimationBlends(argument == "on");
        } else {
            failures->push_back("animblend needs on, off, mark or count <least> <most>");
        }
        return;
    }
    if (verb == "objplayed") {
        char name[64] = "";
        double past = -1.0;
        std::sscanf(argument.c_str(), "%63s past %lf", name, &past);
        const bool played = std::find(g_objectAnimations.begin(), g_objectAnimations.end(), name) != g_objectAnimations.end();
        std::string seen;
        for (const std::string& each : g_objectAnimations) seen += (seen.empty() ? "" : " ") + each;
        Log("harness: objplayed %s: %s (played: %s)", argument.c_str(), played ? "yes" : "NO", seen.c_str());
        if (!played) {
            failures->push_back(std::string("objplayed: the traced object did not play ") + name + " (played: " + seen + ")");
        } else if (past >= 0.0) {
            const double furthest = g_objectFurthest[name];
            Log("harness: objplayed %s: furthest frame %.3f", name, furthest);
            if (furthest <= past) {
                std::snprintf(text, sizeof(text), "objplayed: %s played to frame %.3f, not past %.3f", name, furthest, past);
                failures->push_back(text);
            }
        }
        return;
    }
    if (verb == "objtrace") {
        char what[16] = "", who[64] = "", part[32] = "", log[16] = "";
        std::sscanf(argument.c_str(), "%15s %63s %31s %15s", what, who, part, log);
        if (std::strcmp(what, "start") == 0 && who[0]) {
            g_objectLight = std::strcmp(who, "spelllight") == 0;
            const bool first = g_objectLight || std::strcmp(who, "arrow") == 0;
            g_objectId = first                                 ? 0
                         : std::strcmp(who, "leader") == 0 ? digest::LocalLeader()
                         : who[0] == '#'                    ? std::atoi(who + 1)
                                                            : image::TextKey(who);
            if (g_objectId < 0 || (g_objectId == 0 && !first)) {
                failures->push_back(std::string("objtrace: no unit ") + who);
                return;
            }
            g_objectPart = std::strcmp(part, "-") == 0 ? "" : part;
            g_logObjects = std::strcmp(log, "log") == 0;
            g_objectSamples.clear();
            g_objectOwnSamples.clear();
            g_objectAnimations.clear();
            g_objectFurthest.clear();
            g_objectLost = false;
            g_objectRecording = true;
            return;
        }
        if (std::strcmp(what, "stop") != 0 || !who[0]) {
            failures->push_back("objtrace needs start <who> [part|-] [log] or stop <name>");
            return;
        }
        g_objectRecording = false;
        for (const bool own : {false, true}) {
            const std::vector<Sample>& samples = own ? g_objectOwnSamples : g_objectSamples;
            const std::string kept = own ? std::string(who) + ".own" : std::string(who);
            const Recording r = Measure(samples);
            g_recordings[kept] = r;
            std::snprintf(text, sizeof(text),
                          "objtrace %s: %d frames (object %d%s); pan %.4f (largest step %.4f, %d jumps), turn %.5f (%.5f, %d), "
                          "height %.4f (%.4f, %d)",
                          kept.c_str(), r.frames, g_objectId, g_objectLost ? ", gone before the end" : "", r.total[Pan],
                          r.largest[Pan], r.jumps[Pan], r.total[Turn], r.largest[Turn], r.jumps[Turn], r.total[Height],
                          r.largest[Height], r.jumps[Height]);
            Log("harness: %s", text);
            lines->push_back(text);
        }
        return;
    }
    if (verb == "camtrace") {
        char what[16] = "", name[64] = "";
        std::sscanf(argument.c_str(), "%15s %63s", what, name);
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
                          "turn %.5f (%.5f, %d), zoom %.4f (%.4f, %d), height %.4f (%.4f, %d), reach %.4f (%.4f, %d); focus "
                          "(%.2f, %.2f) to (%.2f, %.2f), distance %.2f to %.2f",
                          kept.c_str(), r.frames, g_frameTime, g_flags, r.total[Pan], r.largest[Pan], r.jumps[Pan], r.total[Turn],
                          r.largest[Turn], r.jumps[Turn], r.total[Zoom], r.largest[Zoom], r.jumps[Zoom], r.total[Height],
                          r.largest[Height], r.jumps[Height], r.total[Reach], r.largest[Reach], r.jumps[Reach], first.x, first.y, last.x, last.y, first.distance, last.distance);
            Log("harness: %s", text);
            lines->push_back(text);
        }
        return;
    }
    if (verb == "camsame" || verb == "camsteadier") {
        char a[64] = "", b[64] = "", kindName[16] = "";
        double fraction = 0.0;
        if (std::sscanf(argument.c_str(), "%63s %63s %15s %lf", a, b, kindName,
                     &fraction) < 4 ||
            KindOf(kindName) < 0) {
            failures->push_back(verb + " needs two recordings, pan|turn|zoom|height|reach and a fraction");
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
    if (std::sscanf(argument.c_str(), "%63s %15s %15s", name, kindName, jerky) < 2 ||
        KindOf(kindName) < 0) {
        failures->push_back("camsmooth needs a recording and pan|turn|zoom|height|reach");
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
