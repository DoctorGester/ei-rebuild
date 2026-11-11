// The image's renderer for the harness (image_side.h; harness.cpp, harness_capture.cpp): its present counts, its
// frame placement and its steps.
#include "image_side.h"

#include "../harness_capture.h"
#include "../harness_digest.h"
#include "../harness_timing.h"

#include "platform/log.h"
#include "system/harness_system.h"

#include "render/gpu.h"
#include "render/render.h"
#include "render/render_checks.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ei::verify::image {

CursorPresents CursorPresentCounts() {
    const render::CursorInfo info = render::CursorState();
    CursorPresents counts;
    counts.presentsShown = info.presentsShown;
    counts.presentsHeld = info.presentsHeld;
    counts.presentsHidden = info.presentsHidden;
    counts.made = info.made;
    counts.failures = info.failures;
    counts.shapeChanges = info.shapeChanges;
    counts.frameChanges = info.frameChanges;
    counts.shown = info.shown;
    counts.width = info.width;
    counts.height = info.height;
    counts.hotX = info.hotX;
    counts.hotY = info.hotY;
    return counts;
}

const std::uint8_t* GammaRampEntries() { return render::GammaRampTable(); }

WindowPresents WindowPresentCounts() {
    const render::WindowPresentInfo info = render::WindowPresents();
    WindowPresents counts;
    counts.presents = info.presents;
    counts.rampChecks = info.rampChecks;
    counts.rampPoints = info.rampPoints;
    counts.rampDiffering = info.rampDiffering;
    counts.rampCheckPending = info.rampCheckPending;
    counts.rampIdentity = info.rampIdentity;
    return counts;
}

std::optional<Size> FrameInWindow() {
    const std::optional<render::FramePlacement> shown = render::FrameInWindow();
    if (!shown) return std::nullopt;
    return shown->frame;
}
std::optional<Size> FramePlaced() {
    const std::optional<render::FramePlacement> shown = render::FrameInWindow();
    if (!shown) return std::nullopt;
    return shown->placed.Extent();
}
Point FrameToWindow(Point p) { return render::FrameToWindow(p); }
Point WindowToFrame(Point p) { return render::WindowToFrame(p); }
void ConfigureRenderer(const char* spec) { render::Configure(spec ? spec : ""); }
void SetWaitForVsync(bool on) { render::SetWaitForVsync(on); }
void EndFrame() { render::EndFrame(); }
std::optional<FrameImage> ReadFrame() {
    std::optional<render::FrameImage> frame = render::ReadFrame();
    if (!frame) return std::nullopt;
    return FrameImage{frame->width, frame->height, std::move(frame->bgr)};
}

// "losedevice": the device removed just before the renderer's next call at the step's point (gpu.h, LoseDeviceAt).
static void LoseDevice() {
    std::string error;
    if (sys::RemoveGraphicsDevice(render::gpu::DeviceName(), &error)) Log("harness: losedevice: the device removed");
    else Log("harness: losedevice: %s", error.c_str());
}

bool RenderStep(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
                std::vector<std::string>* frames) {
    if (verb == "terrainlevels") {
        if (argument == "on") render::TerrainLevels(render::TerrainLevelMode::Levels);
        else if (argument == "point") render::TerrainLevels(render::TerrainLevelMode::PointMips);
        else if (argument == "off") render::TerrainLevels(render::TerrainLevelMode::Off);
        else failures->push_back("terrainlevels needs on, point or off");
    } else if (verb == "levelsteps") {
        char frame[64] = "";
        double least = 0, most = 1;
        if (std::sscanf(argument.c_str(), "%63s %lf %lf", frame, &least, &most) < 3) {
            failures->push_back("levelsteps needs a frame name, a least and a most fraction");
            return true;
        }
        double steps = 0;
        long long pairs = 0;
        const char* why = "";
        if (!capture::LevelSteps(frame, &steps, &pairs, &why)) {
            failures->push_back(std::string("levelsteps ") + frame + ": " + why);
            return true;
        }
        char text[200];
        std::snprintf(text, sizeof(text), "levelsteps %s: %.4f%% of %lld neighbouring pairs step a level (allowed %.4f%% .. %.4f%%)",
                      frame, 100.0 * steps, pairs, 100.0 * least, 100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        if (steps < least || steps > most) failures->push_back(text);
    } else if (verb == "hues") {
        char frame[64] = "", hue[16] = "";
        double least = 0, most = 1;
        if (std::sscanf(argument.c_str(), "%63s %lf %lf %15s", frame, &least, &most, hue) < 3) {
            failures->push_back("hues needs a frame name, a least and a most fraction");
            return true;
        }
        static const char* const kHues[4] = {"red", "green", "blue", "yellow"};
        int only = -1;   // with a hue named, only its share is held to the range
        for (int k = 0; k < 4; ++k)
            if (std::strcmp(hue, kHues[k]) == 0) only = k;
        if (hue[0] && only < 0) {
            failures->push_back(std::string("hues: ") + hue + " is not red, green, blue or yellow");
            return true;
        }
        double shares[4] = {0, 0, 0, 0};
        const char* why = "";
        if (!capture::HueShares(frame, shares, &why)) {
            failures->push_back(std::string("hues ") + frame + ": " + why);
            return true;
        }
        char text[200];
        std::snprintf(text, sizeof(text),
                      "hues %s: red %.3f%% green %.3f%% blue %.3f%% yellow %.3f%% (allowed %.3f%% .. %.3f%% %s)", frame,
                      100.0 * shares[0], 100.0 * shares[1], 100.0 * shares[2], 100.0 * shares[3], 100.0 * least,
                      100.0 * most, only < 0 ? "each" : kHues[only]);
        Log("harness: %s", text);
        frames->push_back(text);
        for (int k = 0; k < 4; ++k)
            if ((only < 0 || k == only) && (shares[k] < least || shares[k] > most)) {
                failures->push_back(text);
                break;
            }
    } else if (verb == "mmplevels") {
        double most = 0;
        if (std::sscanf(argument.c_str(), "%lf", &most) < 1) {
            failures->push_back("mmplevels needs the largest mean difference allowed");
            return true;
        }
        render::MmpLevelCheck check = render::CheckMmpLevels();
        char text[240];
        std::snprintf(text, sizeof(text),
                      "mmplevels: %d textures, %d levels; largest mean difference %.2f (allowed %.2f; level %d of a "
                      "%dx%d texture, format %08X)",
                      check.textures, check.levels, check.worst, most, check.worstLevel, check.worstWidth,
                      check.worstHeight, static_cast<unsigned>(check.worstFormat));
        Log("harness: %s", text);
        frames->push_back(text);
        if (check.levels == 0 || check.worst > most) failures->push_back(text);
    } else if (verb == "msaa" || verb == "anisotropy") {
        int level = 0;
        if (std::sscanf(argument.c_str(), "%d", &level) < 1) failures->push_back(verb + " needs a level");
        else if (verb == "msaa") render::SetMsaa(level);
        else render::SetAnisotropy(level);
    } else if (verb == "smoothcheck") {
        char plain[64] = "", smooth[64] = "";
        double least = 0, most = 1;
        if (std::sscanf(argument.c_str(), "%63s %63s %lf %lf", plain, smooth,
                     &least, &most) < 4) {
            failures->push_back("smoothcheck needs two frame names, a least and a most fraction");
            return true;
        }
        render::SmoothInfo info = render::SmoothState();
        double differing = 0;
        int largest = 0;
        const char* why = "";
        bool compared = capture::Compare(plain, smooth, 8, &differing, &largest, &why);
        char text[360];
        std::snprintf(text, sizeof(text),
                      "smoothcheck %s %s: msaa %d, anisotropy %d; %d samples (the device gives %d), last frame %d "
                      "multisampled passes, %d models anisotropic, %d as their stage; %.3f%% of the pixels differ by more "
                      "than 8 (largest %d; allowed %.3f%% .. %.3f%%)",
                      plain, smooth, info.msaa, info.anisotropy, info.targetSamples, info.deviceSamples,
                      info.multisampledPasses, info.anisotropicModels, info.plainModels, 100.0 * differing, largest,
                      100.0 * least, 100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        // The levels asked for are what was drawn: as many samples as asked, or the device's next count above (a
        // device without 2 draws 4) or its most (at least 2), anisotropic world models exactly when asked.
        const int samples = info.msaa < info.deviceSamples ? info.msaa : info.deviceSamples;
        const bool drawn = info.msaa > 1 ? samples >= 2 && info.targetSamples >= samples &&
                                               info.targetSamples <= info.deviceSamples && info.multisampledPasses >= 1
                                         : info.targetSamples == 1 && info.multisampledPasses == 0;
        const bool sampled = info.anisotropy > 1 ? info.anisotropicModels >= 1 : info.anisotropicModels == 0;
        if (!compared) failures->push_back(std::string("smoothcheck ") + plain + " " + smooth + ": " + why);
        else if (!drawn || !sampled || differing < least || differing > most)
            failures->push_back(text);
    } else if (verb == "smoothedges") {
        char plain[64] = "", smooth[64] = "";
        int tolerance = 0;
        double most = 0;
        int left = 0, top = 0, right = 1 << 30, bottom = 1 << 30;
        const int read = std::sscanf(argument.c_str(), "%63s %63s %d %lf %d %d %d %d", plain, smooth, &tolerance, &most,
                                     &left, &top, &right, &bottom);
        if (read != 4 && read != 8) {
            failures->push_back("smoothedges needs two frame names, a tolerance, a most fraction and maybe a rectangle");
            return true;
        }
        double outside = 0;
        long long count = 0;
        const char* why = "";
        if (!capture::OutsideNeighbours(plain, smooth, tolerance, left, top, right, bottom, &outside, &count, &why)) {
            failures->push_back(std::string("smoothedges ") + plain + " " + smooth + ": " + why);
            return true;
        }
        char area[64] = "the frame";
        if (read == 8) std::snprintf(area, sizeof(area), "%d,%d-%d,%d", left, top, right, bottom);
        char text[300];
        std::snprintf(text, sizeof(text),
                      "smoothedges %s %s in %s: %lld pixels (%.4f%%) outside their neighbours' range by more than %d "
                      "(allowed %.4f%%)",
                      plain, smooth, area, count, 100.0 * outside, tolerance, 100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        if (outside > most) failures->push_back(text);
    } else if (verb == "smoothorder") {
        static render::SmoothOrder order = render::SmoothOrder::Game;
        if (argument == "game" || argument == "before" || argument == "split") {
            order = argument == "game" ? render::SmoothOrder::Game
                    : argument == "before" ? render::SmoothOrder::Before : render::SmoothOrder::Split;
            render::SetSmoothOrder(order);
            return true;
        }
        if (argument != "check") {
            failures->push_back("smoothorder needs game, before, split or check");
            return true;
        }
        render::SmoothInfo info = render::SmoothState();
        char text[280];
        std::snprintf(text, sizeof(text),
                      "smoothorder check (%s): msaa %d, %d samples, last frame %d multisampled passes (%d writing the "
                      "depth copy), %d copies into them, %d depth copies back",
                      order == render::SmoothOrder::Game ? "game" : order == render::SmoothOrder::Before ? "before" : "split",
                      info.msaa, info.targetSamples, info.multisampledPasses, info.depthCopyPasses, info.copiesInto,
                      info.depthCopiesBack);
        Log("harness: %s", text);
        frames->push_back(text);
        bool ok = info.msaa > 1 && info.targetSamples >= 2;
        // The battle frame in the game's order and with the pass before: nothing after the world reads the depth copy
        // (the depth cleared right after it); split, the pass after the world's first half does.
        const bool withoutCopy = info.depthCopyPasses == 0 && info.depthCopiesBack == 0;
        if (order == render::SmoothOrder::Game)
            ok = ok && info.multisampledPasses >= 1 && info.copiesInto == 0 && withoutCopy;
        else if (order == render::SmoothOrder::Before)
            ok = ok && info.multisampledPasses >= 1 && info.copiesInto >= 1 && withoutCopy;
        else
            ok = ok && info.multisampledPasses >= 2 && info.copiesInto >= 1 && info.depthCopiesBack >= 2 &&
                 info.depthCopyPasses == info.multisampledPasses;
        if (!ok) failures->push_back(text);
    } else if (verb == "shadowcasters") {
        if (argument == "view") render::SetOffscreenCasters(render::OffscreenCasters::View);
        else if (argument == "all") render::SetOffscreenCasters(render::OffscreenCasters::All);
        else if (argument == "walked") render::SetOffscreenCasters(render::OffscreenCasters::Walked);
        else failures->push_back("shadowcasters needs view, all or walked");
    } else if (verb == "groundreach") {
        if (argument == "on" || argument == "off") {
            render::SetGroundReach(argument == "on");
            return true;
        }
        char* end = nullptr;
        const double least = std::strtod(argument.c_str(), &end);
        if (end == argument.c_str()) {
            failures->push_back("groundreach needs on, off or the least reach");
            return true;
        }
        const render::GroundReachInfo info = render::GroundReachState();
        char text[200];
        std::snprintf(text, sizeof(text), "groundreach: the ground reaches %.1f units (allowed from %.1f); %d of %d runs cut by the far side",
                      info.reach, least, info.cut, info.runs);
        Log("harness: %s", text);
        frames->push_back(text);
        if (!info.measured) failures->push_back("groundreach: nothing measured (groundreach on, then a frame of the world)");
        else if (info.reach < least || info.cut != 0) failures->push_back(text);
    } else if (verb == "shadowview" || verb == "shadowshift") {
        if (verb == "shadowshift") render::SetShadowShift(std::atoi(argument.c_str()));
        else if (argument == "on" || argument == "off") render::SetShadowView(argument == "on");
        else failures->push_back("shadowview needs on or off");
    } else if (verb == "decals") {
        if (argument == "on" || argument == "off") render::SetGroundDecals(argument == "on");
        else failures->push_back("decals needs on or off");
    } else if (verb == "weatherparticles") {
        if (argument == "on" || argument == "off") render::SetWeatherParticles(argument == "on");
        else failures->push_back("weatherparticles needs on or off");
    } else if (verb == "panelweather") {
        if (argument == "on" || argument == "off") render::SetInterfaceUnitWeather(argument == "on");
        else failures->push_back("panelweather needs on or off");
    } else if (verb == "roofs") {
        if (argument == "on" || argument == "off") {
            render::SetRoofShelter(argument == "on");
        } else if (argument.rfind("check", 0) == 0) {
            // roofs check <least roofs>
            const int least = std::atoi(argument.c_str() + 5);
            const render::RoofInfo info = render::RoofState();
            char text[200];
            std::snprintf(text, sizeof(text), "roofs: map %s (%d texels over %.0f units), %d roof meshes (at least %d)",
                          info.made ? "made" : "not made", info.size, static_cast<double>(info.extent), info.roofs,
                          least);
            Log("harness: %s", text);
            frames->push_back(text);
            if (!info.made || info.roofs < least) failures->push_back(text);
        } else {
            failures->push_back("roofs needs on, off or check <least roofs>");
        }
    } else if (verb == "terraincache") {
        if (argument == "on" || argument == "off") render::SetTerrainCache(argument == "on");
        else failures->push_back("terraincache needs on or off");
    } else if (verb == "meshcache") {
        if (argument == "on" || argument == "off") {
            render::SetMeshCache(argument == "on");
        } else if (argument == "check") {
            render::MeshCacheInfo info = render::MeshCacheState();
            char text[200];
            std::snprintf(text, sizeof(text), "meshcache: %s, %zu meshes kept, %llu checks against a fresh build, %llu stale",
                          info.on ? "on" : "off", info.meshes, static_cast<unsigned long long>(info.checks),
                          static_cast<unsigned long long>(info.stale));
            Log("harness: %s", text);
            Log("harness: meshcache: %zu copies on the GPU, %llu times a mesh took another's", info.copies,
                static_cast<unsigned long long>(info.shared));
            frames->push_back(text);
            if (info.stale > 0) failures->push_back(text);
        } else {
            failures->push_back("meshcache needs on, off or check");
        }
    } else if (verb == "shadowcheck") {
        // shadowcheck <least casters> [<least casters outside the view> (R20)]
        int least = 1, leastOutside = 0;
        if (std::sscanf(argument.c_str(), "%d %d", &least, &leastOutside) < 1) {
            failures->push_back("shadowcheck needs the least number of casters");
            return true;
        }
        render::ShadowInfo info = render::ShadowState();
        char text[480];
        std::snprintf(text, sizeof(text),
                      "shadowcheck: map %s (%d texels over %.0f units), %d casters in %d draws, %d meshes, %d "
                      "ground batches and %d of %d ground prints sampling it (at least %d casters); shadowed objects "
                      "not drawn %d, drawn but not cast %d; outside the view %d casters in %d draws",
                      info.made ? "made" : "not made", info.size,
                      static_cast<double>(info.extent), info.casters, info.casterDraws, info.receivingMeshes,
                      info.receivingTerrain, info.receivingDecals, info.groundDecals, least, info.offScreen, info.lost,
                      info.outsideCasters, info.outsideDraws);
        Log("harness: %s", text);
        frames->push_back(text);
        if (!info.made || info.casters < least || info.receivingMeshes < 1 || info.receivingTerrain < 1 ||
            info.receivingDecals < info.groundDecals || info.lost > 0 || info.outsideCasters < leastOutside)
            failures->push_back(text);
    } else if (verb == "cloudcheck") {
        // cloudcheck <least light> <most light> [<least cover> <most cover>]
        float least = 0.0f, most = 0.0f, leastCover = 0.0f, mostCover = 1.0f;
        if (std::sscanf(argument.c_str(), "%f %f %f %f", &least, &most, &leastCover, &mostCover) < 2) {
            failures->push_back("cloudcheck needs the least and the most light");
            return true;
        }
        render::ShadowInfo info = render::ShadowState();
        char text[200];
        std::snprintf(text, sizeof(text),
                      "cloudcheck: a full cloud hid %.3f of the sun (%.3f .. %.3f), cover %.3f (%.3f .. %.3f)",
                      static_cast<double>(info.cloudLight), static_cast<double>(least), static_cast<double>(most),
                      static_cast<double>(info.cloudCover), static_cast<double>(leastCover),
                      static_cast<double>(mostCover));
        Log("harness: %s", text);
        frames->push_back(text);
        if (info.cloudLight < least || info.cloudLight > most || info.cloudCover < leastCover ||
            info.cloudCover > mostCover)
            failures->push_back(text);
    } else if (verb == "weathercheck") {
        // weathercheck <least wet> <most wet> [<least snow> <most snow>]
        float least = 0.0f, most = 0.0f, leastSnow = 0.0f, mostSnow = 1.0f;
        if (std::sscanf(argument.c_str(), "%f %f %f %f", &least, &most, &leastSnow, &mostSnow) < 2) {
            failures->push_back("weathercheck needs the least and the most wetness");
            return true;
        }
        render::ShadowInfo info = render::ShadowState();
        char text[200];
        std::snprintf(text, sizeof(text),
                      "weathercheck: wetness %.3f (%.3f .. %.3f), puddles %.3f, snow %.3f (%.3f .. %.3f)",
                      static_cast<double>(info.wetness), static_cast<double>(least), static_cast<double>(most),
                      static_cast<double>(info.puddles), static_cast<double>(info.snowCover),
                      static_cast<double>(leastSnow), static_cast<double>(mostSnow));
        Log("harness: %s", text);
        frames->push_back(text);
        if (info.wetness < least || info.wetness > most || info.snowCover < leastSnow || info.snowCover > mostSnow)
            failures->push_back(text);
    } else if (verb == "unitweather") {
        // unitweather leader|#<id>|<name> <least wet> <most wet> [<least snow> <most snow> [<least shelter> <most shelter>]]
        char unit[64] = "";
        float least = 0.0f, most = 0.0f, leastSnow = 0.0f, mostSnow = 1.0f, leastShelter = 0.0f, mostShelter = 1.0f;
        if (std::sscanf(argument.c_str(), "%63s %f %f %f %f %f %f", unit, &least, &most, &leastSnow, &mostSnow,
                        &leastShelter, &mostShelter) < 3) {
            failures->push_back("unitweather needs the unit, the least and the most wetness");
            return true;
        }
        const int id = std::strcmp(unit, "leader") == 0 ? digest::LocalLeader()
                       : unit[0] == '#'                ? std::atoi(unit + 1)
                                                       : TextKey(unit);
        const render::UnitWeather w = render::UnitWeatherState(ClientObject(id));
        char text[300];
        std::snprintf(text, sizeof(text),
                      "unitweather %s: %s at (%.1f, %.1f, %.1f), wetness %.3f (%.3f .. %.3f), snow %.3f (%.3f .. %.3f), "
                      "shelter %.2f (%.2f .. %.2f)",
                      unit, w.known ? "drawn" : "not drawn", static_cast<double>(w.at.x), static_cast<double>(w.at.y),
                      static_cast<double>(w.at.z), static_cast<double>(w.wet), static_cast<double>(least),
                      static_cast<double>(most), static_cast<double>(w.snow), static_cast<double>(leastSnow),
                      static_cast<double>(mostSnow), static_cast<double>(w.shelter), static_cast<double>(leastShelter),
                      static_cast<double>(mostShelter));
        Log("harness: %s", text);
        frames->push_back(text);
        if (!w.known || w.wet < least || w.wet > most || w.snow < leastSnow || w.snow > mostSnow ||
            w.shelter < leastShelter || w.shelter > mostShelter)
            failures->push_back(text);
    } else if (verb == "occlusion") {
        if (argument == "on") render::SetOcclusion(render::OcclusionMode::On);
        else if (argument == "off") render::SetOcclusion(render::OcclusionMode::Off);
        else if (argument == "view") render::SetOcclusion(render::OcclusionMode::View);
        else if (argument != "check") failures->push_back("occlusion needs on, off, view or check");
        if (argument != "check") return true;
        render::OcclusionInfo info = render::OcclusionState();
        char text[300];
        std::snprintf(text, sizeof(text),
                      "occlusion check: %s (%dx%d), depth of %d ground and %d mesh draws, %d ground batches and %d "
                      "meshes taking it; the ground's own kept for %d sectors, worked out %d times",
                      info.made ? "made" : "not made", info.width, info.height, info.terrainDraws, info.meshDraws,
                      info.receivingTerrain, info.receivingMeshes, info.groundSectors, info.groundMade);
        Log("harness: %s", text);
        frames->push_back(text);
        if (!info.made || info.terrainDraws < 1 || info.meshDraws < 1 || info.receivingTerrain < 1 ||
            info.receivingMeshes < 1 || info.groundSectors < 1)
            failures->push_back(text);
    } else if (verb == "losedevice") {
        // gpu::LossPoint's order.
        static const char* const kPoints[] = {"commands", "pass", "uniforms", "acquire", "submit", "write", "wait"};
        int point = 0;
        while (point < 7 && argument != kPoints[point]) ++point;
        const std::string driver = render::gpu::DriverName();
        if (point == 7) failures->push_back("losedevice needs commands, pass, uniforms, acquire, submit, write or wait");
        else if (driver != "direct3d12") failures->push_back("losedevice: Direct3D 12 only, not " + driver);
        else render::gpu::LoseDeviceAt(static_cast<render::gpu::LossPoint>(point), LoseDevice);
    } else if (verb == "gputiming") {
        if (argument == "off") render::gpu::SetGpuTiming(render::gpu::GpuTiming::Off);
        else if (argument == "frame") render::gpu::SetGpuTiming(render::gpu::GpuTiming::Frame);
        else if (argument == "pass") render::gpu::SetGpuTiming(render::gpu::GpuTiming::Pass);
        else failures->push_back("gputiming needs off, frame or pass");
    } else if (verb == "renderstats") {
        render::FrameStats t = render::TakeFrameStats();
        const render::gpu::GpuTimes gpuTimes = render::gpu::TakeGpuTimes();
        // The main thread's CPU time (its cycles, in milliseconds by the thread's CPU time so far, as the
        // TIMING lines) and the wall time since the last renderstats (R16h: what a frame costs the main
        // thread, the game's work with the renderer's). Differences of whole counts first: the main thread
        // computes at the game's 24-bit precision.
        static unsigned long long lastCycles = 0;
        static long long lastTicks = 0;
        std::uint64_t cycles = 0;
        double cpuMs = 0.0;
        sys::ThisThreadCpu(&cpuMs, &cycles);
        const double msPerCycle = cycles ? cpuMs / static_cast<double>(cycles) : 0.0;
        const long long now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const double frameCpuMs = static_cast<double>(cycles - lastCycles) * msPerCycle;
        // The world updates' and the client ticks' share of it (MF15f1: the particles tick in the client's).
        static unsigned long long lastServer = 0, lastClient = 0;
        const unsigned long long server = timing::PartCycles(timing::Server);
        const unsigned long long client = timing::PartCycles(timing::Client);
        const double serverMs = static_cast<double>(server - lastServer) * msPerCycle;
        const double clientMs = static_cast<double>(client - lastClient) * msPerCycle;
        lastServer = server;
        lastClient = client;
        const double frameWallMs = lastTicks ? static_cast<double>(now - lastTicks) / 1e6 : static_cast<double>(now) / 1e6;
        lastCycles = cycles;
        lastTicks = now;
        if (argument == "-" || t.frames == 0) return true;   // a new count begins
        double n = static_cast<double>(t.frames);
        auto kb = [n](std::uint64_t bytes) { return static_cast<double>(bytes) / 1024.0 / n; };
        auto per = [n](std::uint64_t count) { return static_cast<double>(count) / n; };
        using F = render::FrameStats;
        auto count = [&t, per](F::Count k) { return per(t.counts[k]); };
        auto countKb = [&t, kb](F::Count k) { return kb(t.counts[k]); };
        auto ms = [&t, n](F::Time k) { return t.ms[k] / n; };
        const render::SmoothInfo quality = render::SmoothState();
        char text[2400];
        std::snprintf(
            text, sizeof(text),
            "renderstats %s: %u frames of %dx%d, per frame: hand-over %.0f terrain sectors -> %.0f passes, %.0f meshes / %.0f "
            "passes (%.0f flat, %.1f KB), %.0f pre-transformed, %.0f blits, %.0f depth clears, %.0f state reads, "
            "%.0f matrices; against the last frame (same / relit / moved / new): terrain sectors %.0f / %.0f / %.0f / %.0f, meshes %.0f / %.0f / "
            "%.0f / %.0f, %.0f meshes share another's geometry; upload KB "
            "terrain %.1f v + %.1f i, meshes %.1f v + %.1f i, uniforms %.1f; %.1f passes, %.0f draws in %.0f calls, pipelines %.0f "
            "applied / %.0f changed, bindings %.0f (first texture changed %.0f); images made (count / KB): mmp %.2f / "
            "%.1f, movie %.2f / %.1f, small %.2f / %.1f; "
            "CPU ms terrain %.3f, hand-over %.3f, textures %.3f, end of frame %.3f (counting %.3f), device mesh draws %.3f; "
            "decals projected: %.1f prints %.3f ms (%.0f vertices); flat draws: %.1f, %.3f ms "
            "(%.0f vertices); shadow builds %.1f, %.3f ms; "
            "shadow maps %.1f draws in %.1f calls, CPU ms %.3f; morphed meshes %.1f "
            "(%.0f corners); weather %.3f ms (%.0f vertices); main thread ms CPU %.2f / wall %.2f (world updates %.2f, client ticks %.2f); quality: msaa %d (%d samples, the device gives %d), "
            "anisotropy %d",
            argument.c_str(), t.frames, t.width, t.height, count(F::Count::TerrainSectors), count(F::Count::TerrainBatches), count(F::Count::Meshes),
            count(F::Count::MeshPasses), count(F::Count::FlatMeshes), countKb(F::Count::FlatVertexBytes),
            count(F::Count::TransformedDraws), count(F::Count::Blits), count(F::Count::DepthClears), count(F::Count::StateReads), count(F::Count::Matrices),
            per(t.terrainReuse[F::Reuse::Same]), per(t.terrainReuse[F::Reuse::Relit]), per(t.terrainReuse[F::Reuse::Moved]),
            per(t.terrainReuse[F::Reuse::New]), per(t.meshReuse[F::Reuse::Same]), per(t.meshReuse[F::Reuse::Relit]), per(t.meshReuse[F::Reuse::Moved]),
            per(t.meshReuse[F::Reuse::New]), count(F::Count::SharedMeshes),
            countKb(F::Count::TerrainVertexBytes), countKb(F::Count::TerrainIndexBytes), countKb(F::Count::MeshVertexBytes),
            countKb(F::Count::MeshIndexBytes), countKb(F::Count::UniformBytes),
            count(F::Count::Passes), count(F::Count::Draws), count(F::Count::DrawCalls), count(F::Count::PipelineApplies), count(F::Count::PipelineChanges), count(F::Count::Bindings),
            count(F::Count::TextureChanges), per(t.uploads[F::Upload::Mmp]), kb(t.uploadBytes[F::Upload::Mmp]),
            per(t.uploads[F::Upload::Movie]), kb(t.uploadBytes[F::Upload::Movie]), per(t.uploads[F::Upload::Small]), kb(t.uploadBytes[F::Upload::Small]),
            ms(F::Time::TerrainMs), ms(F::Time::HandOverMs), ms(F::Time::TextureMs), ms(F::Time::EndFrameMs), ms(F::Time::StatsMs), ms(F::Time::DeviceMeshMs),
            count(F::Count::DecalPrints), ms(F::Time::DecalPrintMs), count(F::Count::DecalVertices),
            count(F::Count::FlatDraws), ms(F::Time::FlatMs), count(F::Count::FlatDrawVertices), count(F::Count::ShadowBuilds), ms(F::Time::ShadowBuildMs),
            count(F::Count::ShadowDraws), count(F::Count::ShadowCalls), ms(F::Time::ShadowMs), count(F::Count::MorphMeshes), count(F::Count::MorphCorners),
            ms(F::Time::WeatherMs), count(F::Count::WeatherVertices), frameCpuMs / n,
            frameWallMs / n, serverMs / n, clientMs / n, quality.msaa, quality.targetSamples, quality.deviceSamples, quality.anisotropy);
        Log("harness: %s", text);
        frames->push_back(text);
        // The GPU's times ("gputiming"), only logged: they differ from run to run.
        if (gpuTimes.frames > 0) {
            std::string parts;
            for (const auto& [label, partMs] : gpuTimes.parts) {
                char part[96];
                std::snprintf(part, sizeof(part), "%s%s %.3f", parts.empty() ? "" : ", ", label.c_str(), partMs);
                parts += part;
            }
            Log("harness: renderstats %s GPU ms a frame: %.3f (median %.3f, p10 %.3f, p90 %.3f; %d frames, %.0f%% of the "
                "submissions sent while the GPU ran the one before)%s%s",
                argument.c_str(), gpuTimes.meanMs, gpuTimes.medianMs, gpuTimes.p10Ms, gpuTimes.p90Ms, gpuTimes.frames,
                gpuTimes.waited * 100.0, parts.empty() ? "" : "; by pass: ", parts.c_str());
        }
    } else {
        return false;
    }
    return true;
}

} // namespace ei::verify::image
